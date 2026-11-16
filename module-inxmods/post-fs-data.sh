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

BOOTLOG="$MOD/webroot/boot.log"
echo "[$(date '+%H:%M:%S')] post-fs-data: permissions set" >> "$BOOTLOG" 2>/dev/null

# [NEW] Environment fingerprint, written once at the earliest stage this
# module runs at. The point is Android 17 specifically: when something
# fails only on that version, this is what tells us WHICH difference it
# actually is instead of guessing (page size, SELinux mode and root
# solution are the three real candidates for "works on some phones, not
# others" on a new Android release) -- without this, a report of "not
# working on Android 17" has no way to say what on that device is
# actually different from a working one.
{
    echo "[$(date '+%H:%M:%S')] env: android=$(getprop ro.build.version.release) sdk=$(getprop ro.build.version.sdk) security_patch=$(getprop ro.build.version.security_patch)"
    echo "[$(date '+%H:%M:%S')] env: abi=$(getprop ro.product.cpu.abi) manufacturer=$(getprop ro.product.manufacturer) model=$(getprop ro.product.model)"
    echo "[$(date '+%H:%M:%S')] env: kernel=$(uname -r) page_size=$(getconf PAGESIZE 2>/dev/null || echo unknown)"
    echo "[$(date '+%H:%M:%S')] env: selinux=$(getenforce 2>/dev/null || echo unknown)"
    if [ -d /data/adb/ksu ] || [ -n "$KSU" ]; then
        ROOT_SOLUTION="KernelSU"
    elif [ -d /data/adb/ap ] || command -v apatch >/dev/null 2>&1; then
        ROOT_SOLUTION="APatch"
    elif [ -f /data/adb/magisk.db ] || [ -d /sbin/.magisk ]; then
        ROOT_SOLUTION="Magisk"
    else
        ROOT_SOLUTION="unknown"
    fi
    echo "[$(date '+%H:%M:%S')] env: root_solution=$ROOT_SOLUTION"
    # A binary built without 16 KB page-size alignment can fail to exec at
    # all on a device where page_size=16384 (Android 15+ hardware target) --
    # this check turns that straight into a named log line instead of a
    # silent "did not come up" from svc.sh with no reason attached.
    PS=$(getconf PAGESIZE 2>/dev/null)
    if [ -n "$PS" ] && [ "$PS" != "4096" ]; then
        echo "[$(date '+%H:%M:%S')] env: WARNING non-4K page size ($PS) -- binary must be 16KB-page-aligned or exec will fail"
    fi
} >> "$BOOTLOG" 2>/dev/null
chmod 644 "$BOOTLOG" 2>/dev/null
exit 0
