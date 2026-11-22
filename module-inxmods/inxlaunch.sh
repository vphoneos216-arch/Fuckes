#!/system/bin/sh
# ============================================================
#  InvisibleX Terminal Launcher
#  Usage:  sh /data/adb/modules/inx_mods/inxlaunch.sh
#
#  1. Enter your license key
#  2. Login — runs inx_login_@api
#
#  The mode (Kernel / Normal) and, in Kernel Mode, the driver and the
#  menu-open mode are asked by the binary itself, after the key.
# ============================================================

MODULE_DIR="/data/adb/modules/inx_mods"
LOGIN_BIN="$MODULE_DIR/api/arm64-v8a/inx_login_@api"
CONFIG="$MODULE_DIR/webroot/config.json"

# ── helpers ──────────────────────────────────────────────────
red()    { printf '\033[1;31m%s\033[0m\n' "$*"; }
green()  { printf '\033[1;32m%s\033[0m\n' "$*"; }
yellow() { printf '\033[1;33m%s\033[0m\n' "$*"; }
cyan()   { printf '\033[1;36m%s\033[0m\n' "$*"; }
bold()   { printf '\033[1m%s\033[0m\n'    "$*"; }
reset()  { printf '\033[0m'; }

# ── banner ───────────────────────────────────────────────────
clear
cyan  "╔══════════════════════════════════════════╗"
cyan  "║          InvisibleX  Launcher            ║"
cyan  "║      Terminal Login & Driver Select      ║"
cyan  "╚══════════════════════════════════════════╝"
printf '\n'

# ── sanity checks ────────────────────────────────────────────
if [ ! -f "$LOGIN_BIN" ]; then
    red  "[ERROR] Login binary not found:"
    red  "        $LOGIN_BIN"
    red  "        Make sure the module is fully installed."
    exit 1
fi
if [ ! -x "$LOGIN_BIN" ]; then
    chmod 755 "$LOGIN_BIN" 2>/dev/null
fi

# ── step 1: license key ──────────────────────────────────────
bold "Step 1 — License Key"
printf 'Enter your key: '
read -r KEY
KEY=$(printf '%s' "$KEY" | tr -d '[:space:]')
if [ -z "$KEY" ]; then
    red "[ERROR] Key cannot be empty."
    exit 1
fi
printf '\n'

# ── driver selection: REMOVED ────────────────────────────────
# This asked for the driver right after the key, which is why the
# driver question appeared before the mode question. The binary now
# asks the mode first and then, in Kernel Mode only, which of the two
# drivers to use -- it sets c_driver::g_driver_choice directly, so
# nothing has to be written into config.json here any more.
#
# A "driver" value already in config.json is still honoured when the
# binary runs with no terminal (started by the daemon), which is the
# path ix_get_driver_choice() covers.

# ── step 4: run login ─────────────────────────────────────────
bold "Step 2 — Login"
yellow "→ Verifying key with server …"
printf '\n'

RESULT=$("$LOGIN_BIN" "$KEY" 2>/dev/null)
EXIT_CODE=$?

if [ $EXIT_CODE -eq 0 ]; then
    # Parse "message" field from JSON with awk (no jq needed)
    MSG=$(printf '%s' "$RESULT" | awk -F'"message"[[:space:]]*:[[:space:]]*"' 'NF>1{split($2,a,"\"");print a[1]}')
    green "╔══════════════════════════════════════════╗"
    green "║  LOGIN SUCCESS                           ║"
    green "╚══════════════════════════════════════════╝"
    printf '\n'
    if [ -n "$MSG" ]; then
        green "  $MSG"
    else
        green "  Key verified successfully."
    fi
    printf '\n'
    cyan  "  Config : $CONFIG"
    printf '\n'
    green "  Module is ready. If the game is running, the"
    green "  daemon will pick up the new config automatically."
else
    MSG=$(printf '%s' "$RESULT" | awk -F'"message"[[:space:]]*:[[:space:]]*"' 'NF>1{split($2,a,"\"");print a[1]}')
    red  "╔══════════════════════════════════════════╗"
    red  "║  LOGIN FAILED                            ║"
    red  "╚══════════════════════════════════════════╝"
    printf '\n'
    if [ -n "$MSG" ]; then
        red "  Reason: $MSG"
    else
        red "  Key verification failed. Check your key and internet connection."
    fi
    printf '\n'
    red  "  Config was NOT updated."
fi

printf '\n'
exit $EXIT_CODE
