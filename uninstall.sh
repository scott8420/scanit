#!/usr/bin/env bash
# uninstall.sh [--system] -- remove Scanit (same as ./install.sh --uninstall).
# Your scans in ~/Pictures/Scanit stay.
exec "$(dirname "${BASH_SOURCE[0]}")/install.sh" --uninstall "$@"
