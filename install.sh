#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────────
# install.sh -- install Scanit for this user: the app and its desktop entry.
# No root; all under ~/.local. (Fowler's shape, without the tile.)
#
#   ~/.local/lib/scanit/scanit   the binary
#   ~/.local/bin/scanit         a link to it, for the command line
#   ~/.local/share/...          desktop entry, icons (install-desktop.sh)
#
# Builds first (./build.sh is incremental). Re-run after pulling changes.
# Safe while Scanit runs: the binary is replaced, not overwritten; quit and
# reopen Scanit to use the new one.
#
# ./install.sh --uninstall  takes all of it out. Your scans stay
# (~/Pictures/Scanit).
# ─────────────────────────────────────────────────────────────────────────────
set -e
cd "$(dirname "${BASH_SOURCE[0]}")"

LIB="$HOME/.local/lib/scanit"
BIN="$HOME/.local/bin"

if [ "${1:-}" = "--uninstall" ]; then
    ./install-desktop.sh --uninstall
    rm -f "$BIN/scanit"
    rm -rf "$LIB"
    echo ""
    echo "Scanit uninstalled. Kept: ~/Pictures/Scanit (your scans)."
    exit 0
fi

./build.sh
[ -f build/scanit ] || { echo "Missing build/scanit after the build." >&2; exit 1; }

mkdir -p "$LIB" "$BIN"
# Copy beside, then rename: a running Scanit keeps its old file, nothing is
# half-written ("Text file busy" never happens).
install -m 755 build/scanit "$LIB/.scanit.new" && mv -f "$LIB/.scanit.new" "$LIB/scanit"
ln -sfn "$LIB/scanit" "$BIN/scanit"
echo "Installed: $LIB/scanit, linked from $BIN/scanit"
case ":$PATH:" in *":$BIN:"*) ;; *) echo "Note: $BIN is not on your PATH -- the app grid does not need it." ;; esac
echo ""

APP_EXEC="$LIB/scanit" ./install-desktop.sh
