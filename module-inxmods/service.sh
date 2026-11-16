#!/system/bin/sh
# Boot entry (late_start service).
#
# The daemon is started straight away, with no waiting. It finds the game by
# scanning /proc, which works from the moment the system is up, so there is
# nothing to wait for -- an earlier version waited up to two minutes for
# sys.boot_completed and that delay was itself the reason the menu was late.
#
# A second attempt is then made in the background once boot has completed, in
# case anything at late_start stopped the first one from surviving. svc.sh is
# idempotent, so a second call with the daemon already up changes nothing.
#
# Everything is written to webroot/boot.log, which the WebUI shows in the
# Daemon Logs panel: without it a boot failure is invisible.

PATH=/system/bin:/system/xbin:/vendor/bin:/sbin:/su/bin:$PATH
export PATH

MOD=/data/adb/modules/inx_mods
SVC="$MOD/svc.sh"
BOOTLOG="$MOD/webroot/boot.log"

mkdir -p "$MOD/config" "$MOD/webroot" 2>/dev/null

blog() {
    echo "[$(date '+%H:%M:%S')] boot:$1" >> "$BOOTLOG" 2>/dev/null
    chmod 644 "$BOOTLOG" 2>/dev/null
}

: > "$BOOTLOG" 2>/dev/null
blog "service.sh ran (boot_completed=$(getprop sys.boot_completed), uid=$(id -u 2>/dev/null))"

chmod 0755 "$SVC" "$MOD/watchdog.sh" "$MOD/api/arm64-v8a/daemon_service_@api" 2>/dev/null

# ── Load on boot ────────────────────────────────────────────────────────
# When the module's "Load on boot" switch is on and a custom driver PATH is
# set, run it here so the driver is already in place before the game is ever
# opened. A preset name (paradise, ditpro) is not a file: the binary selects
# those itself, so there is nothing to execute for them.
CFG="$MOD/webroot/config.json"
jkey() {  # jkey <key> -> bare value, no quotes
    tr -d ' \t' < "$CFG" 2>/dev/null \
      | sed -n 's/^"'"$1"'":\(.*\)$/\1/p' | head -n 1 | tr -d '",'
}
if [ -r "$CFG" ]; then
    LOB=$(jkey driver_load_on_boot)
    DRV=$(jkey driver)
    # Login gate -- the same one the daemon applies before it launches
    # anything: config.json must show a verified login (status=success with a
    # non-empty key). Running a driver is a privileged operation, so it is
    # not done at boot for an unauthenticated device just because the switch
    # is on. The binary still performs its own independent server round-trip
    # afterwards; this gate only decides whether boot-time loading happens.
    KEY=$(jkey key)
    ST=$(jkey status)
    case "$LOB" in
        true|1)
            if [ "$ST" != "success" ] || [ -z "$KEY" ]; then
                blog "load on boot: skipped, no verified login"
            else
            case "$DRV" in
                /*)
                    if [ -f "$DRV" ]; then
                        chmod 0755 "$DRV" 2>/dev/null
                        ( setsid "$DRV" </dev/null >/dev/null 2>&1 & ) </dev/null >/dev/null 2>&1
                        blog "load on boot: executed $DRV"
                    else
                        blog "load on boot: driver missing at $DRV"
                    fi
                    ;;
                "") blog "load on boot: no driver set" ;;
                paradise)
                    KMAJ=$(uname -r | cut -d. -f1)
                    KMIN=$(uname -r | cut -d. -f2 | tr -cd 0-9)
                    KV=$((KMAJ * 1000 + KMIN))
                    # [BUG FIX] Upper bound was 6006 (= kernel 6.6), which
                    # rejected 6.12 (KV=6012) and 6.18 (KV=6018) even though
                    # drivers/paradise/ ships a .ko.sh for both. Raised to
                    # 6018 to match the actual driver files shipped.
                    if [ "$KV" -ge 5010 ] && [ "$KV" -le 6018 ]; then
                        blog "load on boot: paradise on kernel $(uname -r), binary loads it"
                    else
                        blog "load on boot: paradise needs kernel 5.10-6.18, this is $(uname -r)"
                    fi
                    ;;
                *)  blog "load on boot: '$DRV' is a preset, the binary loads it itself" ;;
            esac
            fi
            ;;
    esac
fi

OUT=$(/system/bin/sh "$SVC" start 2>&1)
blog "first start: $OUT"

# Background re-check after boot completes. Detached so it cannot hold up the
# boot script runner.
(
    i=0
    while [ "$i" -lt 60 ]; do
        [ "$(getprop sys.boot_completed)" = "1" ] && break
        i=$((i + 1))
        sleep 2
    done
    OUT2=$(/system/bin/sh "$SVC" start 2>&1)
    blog "post-boot check: $OUT2"
) </dev/null >/dev/null 2>&1 &

echo "InxMods service started"
exit 0
