#!/system/bin/sh
# Runs before service.sh. Nothing is started here on purpose: this stage runs
# before the system is up and a daemon started now can hold up boot. It only
# fixes permissions, so the later start cannot fail on a missing execute bit,
# and records that this stage was reached.

PATH=/system/bin:/system/xbin:/vendor/bin:/sbin:/su/bin:$PATH
export PATH

MOD=/data/adb/modules/inx_mods
mkdir -p "$MOD/config" "$MOD/webroot" 2>/dev/null

chmod 0755 \
    "$MOD/svc.sh" \
    "$MOD/service.sh" \
    "$MOD/watchdog.sh" \
    "$MOD/api/arm64-v8a/daemon_service_@api" \
    "$MOD/api/arm64-v8a/inx_login_@api" \
    "$MOD/bin/arm64-v8a/optimum_bar" 2>/dev/null

echo "[$(date '+%H:%M:%S')] post-fs-data: permissions set" >> "$MOD/webroot/boot.log" 2>/dev/null
chmod 644 "$MOD/webroot/boot.log" 2>/dev/null
exit 0
