#!/usr/bin/env bash
# Scanit build. Deps: see the top of CMakeLists.txt (Fedora and Ubuntu package names).
set -e
cd "$(dirname "${BASH_SOURCE[0]}")"
cmake -B build
cmake --build build -- -j"$(nproc)"
echo ""
echo "Built: ./build/scanit  and  ./build/scanit_selftest"
echo "Smoke: ./build/scanit_selftest"
