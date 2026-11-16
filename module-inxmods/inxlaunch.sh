#!/system/bin/sh
# ============================================================
#  InvisibleX Terminal Launcher
#  Usage:  sh /data/adb/modules/inx_mods/inxlaunch.sh
#
#  1. Enter your license key
#  2. Select driver
#  3. Press Login — runs inx_login_@api and updates config.json
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

# ── step 2: driver selection ─────────────────────────────────
bold "Step 2 — Driver"
yellow "  1) auto      (let the module choose)"
yellow "  2) sodium    (Sodium KPM)"
yellow "  3) kma       (KMA KPM)"
yellow "  4) ldg       (LDG KPM)"
yellow "  5) paradise  (Paradise — recommended)"
yellow "  6) bing      (Bing)"
yellow "  7) ditkpm    (Dit KPM)"
yellow "  8) rthook    (RT Hook)"
printf '\nSelect driver [1-8, default=1]: '
read -r DRIVER_NUM

case "$DRIVER_NUM" in
    2) DRIVER="sodium"   ;;
    3) DRIVER="kma"      ;;
    4) DRIVER="ldg"      ;;
    5) DRIVER="paradise" ;;
    6) DRIVER="bing"     ;;
    7) DRIVER="ditkpm"   ;;
    8) DRIVER="rthook"   ;;
    *) DRIVER="auto"     ;;
esac

yellow "→ Driver selected: $DRIVER"
printf '\n'

# ── step 3: write driver into config.json ────────────────────
# Read existing config, inject driver field, write back.
# Uses only sh + awk — no jq required on the device.
if [ -f "$CONFIG" ]; then
    # Remove any existing "driver" line and append the new one before the closing }
    TMP_CFG="${CONFIG}.tmp.$$"
    awk -v drv="$DRIVER" '
        /^[[:space:]]*"driver"[[:space:]]*:/ { next }  # drop old driver line
        /^[[:space:]]*\}[[:space:]]*$/ {
            # last closing brace — inject driver before it
            print "    \"driver\": \"" drv "\","
        }
        { print }
    ' "$CONFIG" > "$TMP_CFG" 2>/dev/null

    if [ -s "$TMP_CFG" ]; then
        mv "$TMP_CFG" "$CONFIG"
        chmod 0644 "$CONFIG"
    else
        rm -f "$TMP_CFG"
        # Fallback: build a minimal config from scratch
        printf '{\n    "driver": "%s"\n}\n' "$DRIVER" > "$CONFIG"
        chmod 0644 "$CONFIG"
    fi
else
    # config.json doesn't exist yet — create it
    mkdir -p "$(dirname "$CONFIG")"
    printf '{\n    "driver": "%s"\n}\n' "$DRIVER" > "$CONFIG"
    chmod 0644 "$CONFIG"
fi

# ── step 4: run login ─────────────────────────────────────────
bold "Step 3 — Login"
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
    cyan  "  Driver : $DRIVER"
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
