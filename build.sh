#!/usr/bin/env bash
# build.sh -- configure (first time) and build Scanit into ./build.
# Incremental: re-run after any change. BUILD_TYPE=Release ./build.sh for an
# optimised build (install.sh uses Release).
set -e
cd "$(dirname "${BASH_SOURCE[0]}")"
BUILD_TYPE="${BUILD_TYPE:-Debug}"
if [ ! -f build/CMakeCache.txt ] || ! grep -q "CMAKE_BUILD_TYPE:STRING=$BUILD_TYPE" build/CMakeCache.txt; then
    cmake -S . -B build -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
fi
cmake --build build -j"$(nproc)"
./build/scanit_selftest | tail -1
