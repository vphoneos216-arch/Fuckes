#!/system/bin/sh
# invisiblex_loader — runs once at late_start (KernelSU/Magisk/APatch all
# support this service.d-style script). Only fixes permissions and makes
# sure config.json exists; it does NOT auto-launch INX.SH — that is
# a deliberate choice the user makes from the WebUI every time, never
# silently resumed at boot.
MODDIR=${0%/*}

chmod 755 "$MODDIR/bin/INX.SH" 2>/dev/null
chmod 755 "$MODDIR/bin/kd_ctl" 2>/dev/null

if [ ! -f "$MODDIR/config/config.json" ]; then
    printf '{\n  "driver": "auto",\n  "menu_open": "bar"\n}\n' > "$MODDIR/config/config.json"
    chmod 644 "$MODDIR/config/config.json"
fi
