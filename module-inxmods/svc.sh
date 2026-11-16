#!/system/bin/sh
# Service control: svc.sh start|stop|status|restart
#
# Used by boot (service.sh), by flashing (customize.sh) and by the WebUI
# Install button, so all three take one tested path.
#
# Two things this file is careful about:
#
#  * Detaching. ksu.exec does not return until every process holding its
#    output pipe lets go, so a long-running child started carelessly leaves
#    the WebUI waiting for ever. Every such child is started inside its own
#    subshell with stdin, stdout and stderr all replaced.
#
#  * Saying what happened. Boot problems are invisible otherwise, so each
#    step is written to webroot/boot.log, which the WebUI shows in the
#    Daemon Logs panel.
#
# Process checks never use `pidof daemon_service_@api`: the kernel truncates
# /proc/<pid>/comm to 15 characters and that name is 19, so it can never
# match. The pid file is authoritative, with a cmdline search as fallback.
# Search patterns are bracketed so this script is not its own hit.

# A boot-stage shell can start with almost nothing on PATH.
PATH=/system/bin:/system/xbin:/vendor/bin:/sbin:/su/bin:$PATH
export PATH

MOD=${INX_MOD_DIR:-/data/adb/modules/inx_mods}
DAEMON="$MOD/api/arm64-v8a/daemon_service_@api"
WD="$MOD/watchdog.sh"
PIDFILE="$MOD/config/daemon_service.pid"
BOOTLOG="$MOD/webroot/boot.log"

mkdir -p "$MOD/config" "$MOD/webroot" 2>/dev/null

blog() {
    [ -f "$BOOTLOG" ] && [ "$(wc -c < "$BOOTLOG" 2>/dev/null || echo 0)" -gt 50000 ] && : > "$BOOTLOG"
    echo "[$(date '+%H:%M:%S')] svc:$1" >> "$BOOTLOG" 2>/dev/null
    chmod 644 "$BOOTLOG" 2>/dev/null
}

detach() {
    if command -v setsid >/dev/null 2>&1; then
        ( setsid "$@" </dev/null >/dev/null 2>&1 & ) </dev/null >/dev/null 2>&1
    else
        ( nohup "$@" </dev/null >/dev/null 2>&1 & ) </dev/null >/dev/null 2>&1
    fi
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

watchdog_running() { pgrep -f 'watchdog[.]sh' >/dev/null 2>&1; }

do_start() {
    if [ ! -f "$DAEMON" ]; then
        blog "daemon binary missing: $DAEMON"
        echo "error: daemon binary missing"
        return 1
    fi

    chmod 0755 "$DAEMON" "$WD" "$MOD/svc.sh" 2>/dev/null
    chown 0:0 "$DAEMON" "$WD" "$MOD/svc.sh" 2>/dev/null

    # The daemon first: it is what launches the menu, and it only needs /proc,
    # which is available immediately at boot. Waiting for anything else here
    # is what delayed the menu.
    if daemon_pid >/dev/null; then
        blog "daemon already running (pid $(daemon_pid))"
    else
        # [NEW] detach() throws away stdout+stderr, so a daemon that fails
        # to even exec (wrong ABI, "Exec format error" from a 16KB-page
        # mismatch, SELinux denial, missing library) left nothing behind
        # except "daemon did NOT come up after 10s" ten seconds later --
        # true, but no reason, and no way to tell an exec failure apart
        # from the daemon starting fine and just taking its time. This is
        # the SAME single detached launch as before, not a second probe
        # instance (two daemon_service processes racing for even a second
        # is not a risk worth taking) -- its stderr goes to a small file
        # next to the pidfile instead of /dev/null, and the "did not come
        # up" branch below reads it back if the daemon never confirms.
        STARTERR="$MOD/config/daemon_start.err"
        : > "$STARTERR" 2>/dev/null
        if command -v setsid >/dev/null 2>&1; then
            ( setsid "$DAEMON" </dev/null >/dev/null 2>"$STARTERR" & ) </dev/null >/dev/null 2>&1
        else
            ( nohup "$DAEMON" </dev/null >/dev/null 2>"$STARTERR" & ) </dev/null >/dev/null 2>&1
        fi
        blog "daemon started"
    fi

    # Then the watchdog, which keeps it alive from now on.
    if watchdog_running; then
        blog "watchdog already running"
    else
        detach /system/bin/sh "$WD"
        blog "watchdog started"
    fi

    # Report what actually came up, so a silent failure is visible.
    i=0
    while [ "$i" -lt 10 ]; do
        p=$(daemon_pid) && { blog "confirmed daemon pid $p"; echo "started"; return 0; }
        i=$((i + 1))
        sleep 1
    done
    # [NEW] Read back whatever the launch itself wrote to stderr -- an exec
    # failure (wrong ABI, 16KB-page mismatch, missing library, SELinux
    # denial) writes here immediately and explains the "did NOT come up"
    # below instead of leaving it a bare timeout.
    if [ -s "$STARTERR" ]; then
        blog "daemon stderr: $(tr '\n' ' ' < "$STARTERR" 2>/dev/null)"
    fi
    blog "daemon did NOT come up after 10s"
    echo "error: daemon did not start"
    return 1
}

do_stop() {
    pkill -f 'watchdog[.]sh' 2>/dev/null
    p=$(cat "$PIDFILE" 2>/dev/null)
    [ -n "$p" ] && kill -15 "$p" 2>/dev/null
    sleep 1
    pkill -9 -f 'daemon_service_[@]api' 2>/dev/null
    pkill -f 'optimum[_]bar' 2>/dev/null
    rm -f "$PIDFILE"
    blog "stopped"
    echo stopped
}

case "$1" in
    start)   do_start ;;
    stop)    do_stop ;;
    restart) do_stop; do_start ;;
    status)  if daemon_pid >/dev/null; then echo running; else echo stopped; fi ;;
    *)       echo "usage: svc.sh start|stop|restart|status" >&2; exit 2 ;;
esac
exit 0
