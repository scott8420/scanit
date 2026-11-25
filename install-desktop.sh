#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────────
# install-desktop.sh -- make Scanit findable by GNOME. Run by install.sh;
# alone, it points at the build tree's binary.
#
#   the .desktop entry  -- Scanit in the app grid.
#   the icon            -- the logo is compiled into the binary; the shell can't
#                          see that, so the app grid needs a file on disk.
#
# Everything under ~/.local/share; no root. `--uninstall` takes it all out.
# ─────────────────────────────────────────────────────────────────────────────
set -e
cd "$(dirname "${BASH_SOURCE[0]}")"

APP_ID="io.github.scott8420.Scanit"
LOGO="resources/icons/scalable/apps/scanit-logo-symbolic.svg"
DATA="${XDG_DATA_HOME:-$HOME/.local/share}"
APPS="$DATA/applications"
ICONS="$DATA/icons/hicolor/scalable/apps"
SYMBOLIC="$DATA/icons/hicolor/symbolic/apps"

if [ "${1:-}" = "--uninstall" ]; then
    rm -f "$APPS/$APP_ID.desktop" "$ICONS/$APP_ID.svg" "$SYMBOLIC/$APP_ID-symbolic.svg"
    command -v update-desktop-database >/dev/null && update-desktop-database "$APPS" || true
    echo "Removed Scanit's desktop entry and icons."
    exit 0
fi

# install.sh passes the installed copy; run alone, the build tree's binary.
APP_EXEC="${APP_EXEC:-$(pwd)/build/scanit}"
if [ ! -x "$APP_EXEC" ]; then
    echo "No binary at $APP_EXEC -- run ./build.sh (or ./install.sh) first." >&2
    exit 1
fi

mkdir -p "$APPS" "$ICONS" "$SYMBOLIC"
sed "s|@EXEC@|$APP_EXEC|g" "data/$APP_ID.desktop.in" > "$APPS/$APP_ID.desktop"
# A colour app icon goes in data/$APP_ID.svg when the app has one; until then
# the symbolic logo stands in for both.
if [ -f "data/$APP_ID.svg" ]; then cp "data/$APP_ID.svg" "$ICONS/$APP_ID.svg"
else                              cp "$LOGO" "$ICONS/$APP_ID.svg"; fi
cp "$LOGO" "$SYMBOLIC/$APP_ID-symbolic.svg"

command -v update-desktop-database >/dev/null && update-desktop-database "$APPS" || true
command -v gtk4-update-icon-cache  >/dev/null && gtk4-update-icon-cache -qtf "$DATA/icons/hicolor" || true

echo "Installed:"
echo "  $APPS/$APP_ID.desktop"
echo "  $ICONS/$APP_ID.svg"
echo ""
echo "Exec points at $APP_EXEC -- re-run this if it moves."
