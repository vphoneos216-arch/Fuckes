#!/system/bin/sh
# InxMods watchdog. Started at boot by service.sh, at flash time by
# customize.sh, and by the WebUI Install button -- all through svc.sh.
#
# Keeps daemon_service_@api running so the menu appears on every game open
# without opening the WebUI. The daemon returns the favour: it restarts this
# script if it goes missing, so killing either one recovers within seconds.
#
# Two failure modes are covered, which is what 24 h of uptime needs:
#   1. the daemon process is gone      -> start it
#   2. the daemon process is alive but its poll loop has stopped turning
#      (wedged on a hung dumpsys, for example) -> kill it, then start it
# Case 2 is detected from the "updated" timestamp the daemon writes into
# webroot/status.json on every poll.
#
# Process checks never use `pidof daemon_service_@api`: the kernel truncates
# /proc/<pid>/comm to 15 characters and that name is 19, so it can never
# match. The pid file is authoritative, with a cmdline search as fallback.
# Search patterns are bracketed so this script is not seen as its own hit.

MOD=/data/adb/modules/inx_mods
DAEMON="$MOD/api/arm64-v8a/daemon_service_@api"
PIDFILE="$MOD/config/daemon_service.pid"
STATUS="$MOD/webroot/status.json"
LOG="$MOD/webroot/watchdog.log"
PIDF="$MOD/config/watchdog.pid"

# A poll is 2-5 s, so a heartbeat older than this means the loop is stuck.
STALE_SECS=90

mkdir -p "$MOD/config"
echo $$ > "$PIDF"
chmod 644 "$PIDF" 2>/dev/null

log() {
    [ -f "$LOG" ] && [ "$(wc -c < "$LOG" 2>/dev/null || echo 0)" -gt 50000 ] && : > "$LOG"
    echo "[$(date '+%H:%M:%S')] $1" >> "$LOG"
    chmod 644 "$LOG" 2>/dev/null
}

daemon_pid() {
    p=$(cat "$PIDFILE" 2>/dev/null)
    if [ -n "$p" ] && [ -r "/proc/$p/cmdline" ] &&
       tr '\0' '\n' < "/proc/$p/cmdline" 2>/dev/null | grep -q daemon_service; then
        echo "$p"; return 0
    fi
    p=$(pgrep -f 'daemon_service_[@]api' 2>/dev/null | head -n 1)
    [ -n "$p" ] && { echo "$p"; return 0; }
    return 1
}

# Seconds since the daemon last finished a poll. Prints nothing if unknown.
heartbeat_age() {
    [ -r "$STATUS" ] || return 1
    u=$(tr -d ' "' < "$STATUS" 2>/dev/null | sed -n 's/^updated:\([0-9]*\),*$/\1/p' | head -n 1)
    [ -n "$u" ] || return 1
    now=$(date +%s)
    echo $((now - u))
}

start_daemon() {
    if [ ! -f "$DAEMON" ]; then
        log "daemon binary missing: $DAEMON"
        return 1
    fi
    chmod 0755 "$DAEMON" 2>/dev/null
    chown 0:0 "$DAEMON" 2>/dev/null
    # Fully detached: the caller keeps none of its descriptors.
    ( setsid "$DAEMON" </dev/null >/dev/null 2>&1 & ) </dev/null >/dev/null 2>&1
}

log "watchdog started (pid $$)"

while true; do
    if ! pid=$(daemon_pid); then
        start_daemon && log "daemon was not running, started it"
    else
        age=$(heartbeat_age)
        if [ -n "$age" ] && [ "$age" -gt "$STALE_SECS" ]; then
            log "daemon pid $pid wedged (no heartbeat for ${age}s), restarting it"
            kill -9 "$pid" 2>/dev/null
            rm -f "$PIDFILE"
            sleep 1
            start_daemon && log "daemon restarted"
        fi
    fi
    sleep 5
done
