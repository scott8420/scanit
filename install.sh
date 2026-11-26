#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────────
# install.sh -- build Scanit and install it so GNOME shows it in the app grid.
#
#   ./install.sh               for you only, under ~/.local (no root)
#   ./install.sh --system      for everyone, under /usr/local (asks for sudo)
#   ./install.sh --uninstall   take it out again (add --system for that one)
#   (./uninstall.sh [--system] does the same as --uninstall)
#
# What goes where (PREFIX = ~/.local or /usr/local):
#   PREFIX/bin/scanit                                         the app
#   PREFIX/share/applications/io.github.scott8420.Scanit.desktop   the app grid entry
#   PREFIX/share/icons/hicolor/scalable/apps/io.github.scott8420.Scanit.svg
#   PREFIX/share/icons/hicolor/symbolic/apps/io.github.scott8420.Scanit-symbolic.svg
#   PREFIX/share/metainfo/io.github.scott8420.Scanit.metainfo.xml  (GNOME Software / About)
#   PREFIX/share/scanit/installed.txt                         what was installed, for uninstall
#
# Uninstall removes exactly what installed.txt lists. Your scans
# (~/Pictures/Scanit) and settings cache (~/.cache/scanit) are never touched.
# Safe while Scanit runs: the binary is replaced, not overwritten.
# ─────────────────────────────────────────────────────────────────────────────
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

APP_ID="io.github.scott8420.Scanit"
MODE=user
ACTION=install
for a in "$@"; do
    case "$a" in
        --system)    MODE=system ;;
        --user)      MODE=user ;;
        --uninstall) ACTION=uninstall ;;
        -h|--help)   sed -n '2,22p' "$0"; exit 0 ;;
        *) echo "Unknown option: $a (try --help)" >&2; exit 2 ;;
    esac
done

if [ "$MODE" = system ]; then
    PREFIX=/usr/local
    SUDO=""; [ "$(id -u)" -ne 0 ] && SUDO=sudo
else
    PREFIX="$HOME/.local"
    SUDO=""
fi
SHARE="$PREFIX/share"
MANIFEST="$SHARE/scanit/installed.txt"

refresh() {   # let GNOME notice the change (each is optional)
    command -v update-desktop-database >/dev/null && $SUDO update-desktop-database -q "$SHARE/applications" 2>/dev/null || true
    # Refresh an icon cache only where one already exists: creating one in
    # ~/.local would have to be kept fresh by every other app from then on.
    if [ -f "$SHARE/icons/hicolor/icon-theme.cache" ]; then
        command -v gtk-update-icon-cache  >/dev/null && $SUDO gtk-update-icon-cache -qtf "$SHARE/icons/hicolor" 2>/dev/null || true
        command -v gtk4-update-icon-cache >/dev/null && $SUDO gtk4-update-icon-cache -qtf "$SHARE/icons/hicolor" 2>/dev/null || true
    fi
}

if [ "$ACTION" = uninstall ]; then
    files=()
    if [ -f "$MANIFEST" ]; then
        mapfile -t files < "$MANIFEST"
    else   # installed by an older script: the known places
        files=("$PREFIX/bin/scanit"
               "$SHARE/applications/$APP_ID.desktop"
               "$SHARE/icons/hicolor/scalable/apps/$APP_ID.svg"
               "$SHARE/icons/hicolor/symbolic/apps/$APP_ID-symbolic.svg"
               "$SHARE/metainfo/$APP_ID.metainfo.xml")
    fi
    # s001's install.sh put the binary in ~/.local/lib/scanit.
    [ "$MODE" = user ] && files+=("$HOME/.local/lib/scanit/scanit")
    for f in "${files[@]}"; do
        [ -n "$f" ] || continue
        if [ -e "$f" ] || [ -L "$f" ]; then $SUDO rm -f "$f"; echo "  removed $f"; fi
    done
    $SUDO rm -f "$MANIFEST"
    $SUDO rmdir "$SHARE/scanit" "$HOME/.local/lib/scanit" 2>/dev/null || true
    refresh
    echo ""
    echo "Scanit uninstalled ($MODE). Kept: ~/Pictures/Scanit (your scans)."
    exit 0
fi

echo "Building (Release)..."
BUILD_TYPE=Release ./build.sh
[ -x build/scanit ] || { echo "No build/scanit after the build." >&2; exit 1; }

echo ""
echo "Installing for $([ "$MODE" = system ] && echo everyone || echo you) into $PREFIX"
installed=()
put() {   # put SRC DEST MODE -- copy beside, then rename (a running Scanit keeps its file)
    $SUDO mkdir -p "$(dirname "$2")"
    $SUDO install -m "$3" "$1" "$2.new"
    $SUDO mv -f "$2.new" "$2"
    installed+=("$2")
    echo "  $2"
}

put build/scanit "$PREFIX/bin/scanit" 755
tmp="$(mktemp)"
sed "s|@EXEC@|$PREFIX/bin/scanit|g" "data/$APP_ID.desktop.in" > "$tmp"
put "$tmp" "$SHARE/applications/$APP_ID.desktop" 644
rm -f "$tmp"
put "data/$APP_ID.svg"              "$SHARE/icons/hicolor/scalable/apps/$APP_ID.svg" 644
put "data/$APP_ID-symbolic.svg"     "$SHARE/icons/hicolor/symbolic/apps/$APP_ID-symbolic.svg" 644
put "data/$APP_ID.metainfo.xml"     "$SHARE/metainfo/$APP_ID.metainfo.xml" 644

$SUDO mkdir -p "$(dirname "$MANIFEST")"
printf '%s\n' "${installed[@]}" | $SUDO tee "$MANIFEST" >/dev/null

# s001 installed into ~/.local/lib/scanit with a link in ~/.local/bin: tidy it.
if [ "$MODE" = user ] && [ -e "$HOME/.local/lib/scanit/scanit" ]; then
    rm -f "$HOME/.local/lib/scanit/scanit"; rmdir "$HOME/.local/lib/scanit" 2>/dev/null || true
fi

refresh
echo ""
echo "Done. Scanit is in the app grid (log out and in if it does not show at once)."
case ":$PATH:" in *":$PREFIX/bin:"*) ;; *) echo "Note: $PREFIX/bin is not on your PATH -- the app grid does not need it." ;; esac
echo "Uninstall: ./uninstall.sh$([ "$MODE" = system ] && echo ' --system')"
