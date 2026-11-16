#!/system/bin/sh

ui_print ".___       ____  ___"
ui_print "|   | ____ \\   \\/  /"
ui_print "|   |/    \\ \\     /"
ui_print "|   |   |  \\/     \\"
ui_print "|___|___|  /___/\\  \\"
ui_print "         \\  /  \\_ /"

ui_print ""
ui_print "Setting up files..."

# Permissions are set here so nothing has to wait for the next boot.
set_perm_recursive "$MODPATH" 0 0 0755 0644
for f in \
    "$MODPATH/api/arm64-v8a/daemon_service_@api" \
    "$MODPATH/api/arm64-v8a/inx_login_@api" \
    "$MODPATH/bin/arm64-v8a/optimum_bar" \
    "$MODPATH/service.sh" \
    "$MODPATH/watchdog.sh" \
    "$MODPATH/svc.sh"
do
    [ -f "$f" ] && set_perm "$f" 0 0 0755
done

# ── Keep the existing settings on an update ─────────────────────────────
# The zip ships webroot/config.json with every field blank, and installing
# lays that file straight over the live one -- which wiped the licence key,
# the selected driver and both driver switches on every single update. That
# is why a driver had to be picked again after each flash.
#
# The live file is only trusted when it still looks like the config: starts
# with "{", ends with "}" and carries the "key" field. Anything else is left
# alone so a damaged file can never be carried forward. A key this build
# added and the old file lacks is not a problem: every reader of config.json,
# in the WebUI and in the binaries, already has a default for a missing key.
LIVECFG=/data/adb/modules/inx_mods/webroot/config.json
NEWCFG="$MODPATH/webroot/config.json"
if [ -s "$LIVECFG" ] && [ -f "$NEWCFG" ] &&
   [ "$(head -c 1 "$LIVECFG")" = "{" ] &&
   [ "$(tr -d ' \t\r\n' < "$LIVECFG" | tail -c 1)" = "}" ] &&
   grep -q '"key"' "$LIVECFG"; then
    cp "$LIVECFG" "$NEWCFG" && chmod 644 "$NEWCFG"
    ui_print "- Kept your key, driver and switches"
fi

ui_print "✅ Done"

# On an update the live module directory already exists, so the watchdog can
# be started now and the menu works without a reboot. On a first install the
# live directory only appears after reboot, so service.sh handles it then.
LIVE=/data/adb/modules/inx_mods
if [ -d "$LIVE" ] && [ -f "$LIVE/svc.sh" ]; then
    chmod 0755 "$LIVE/svc.sh" "$LIVE/watchdog.sh" 2>/dev/null
    /system/bin/sh "$LIVE/svc.sh" stop  >/dev/null 2>&1
    /system/bin/sh "$LIVE/svc.sh" start >/dev/null 2>&1
    ui_print "Service restarted — no reboot needed"
else
    ui_print "Reboot to start"
fi
