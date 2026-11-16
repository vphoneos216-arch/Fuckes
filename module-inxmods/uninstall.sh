#!/system/bin/sh
# Module removal. The watchdog has to go first: it restarts the daemon every
# 5 s, so stopping only the daemon left the watchdog running until reboot,
# retrying a binary that was no longer there.
#
# Patterns are bracketed so this script is not matched as its own hit, and
# `pidof daemon_service_@api` is never used -- the kernel truncates
# /proc/<pid>/comm to 15 characters and that name is 19.

MOD=/data/adb/modules/inx_mods

pkill -f 'watchdog[.]sh' 2>/dev/null

PID=$(cat "$MOD/config/daemon_service.pid" 2>/dev/null)
[ -n "$PID" ] && kill -15 "$PID" 2>/dev/null
sleep 1
pkill -9 -f 'daemon_service_[@]api' 2>/dev/null

# The overlay binary is launched by the daemon and would outlive it.
pkill -f 'optimum[_]bar' 2>/dev/null

echo "InxMods stopped"
exit 0
