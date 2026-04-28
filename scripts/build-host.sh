#!/usr/bin/env bash
# Host sanity build for the Dolphin submodule.
#
# Purpose: every patch that touches shared (non-Switch-only) code in
# `dolphin/` must keep the host build green. This is the gate for it.
# Before M1 wires `add_subdirectory(dolphin)` into the top-level
# CMakeLists, this script just configures and builds dolphin/ on its
# own with the host (native) toolchain.
#
# Usage:
#   scripts/build-host.sh              # release
#   scripts/build-host.sh --debug      # symbols, -O0
#   scripts/build-host.sh --clean      # nuke build/ and rebuild
#   scripts/build-host.sh --jobs N     # override -j

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_TYPE="Release"
DO_CLEAN=0
JOBS="$(nproc)"

while [ "$#" -gt 0 ]; do
    case "$1" in
        --debug) BUILD_TYPE="Debug" ;;
        --clean) DO_CLEAN=1 ;;
        --jobs)  shift; JOBS="$1" ;;
        -h|--help)
            grep -E '^# ' "$0" | sed 's/^# \{0,1\}//'
            exit 0 ;;
        *) echo "unknown arg: $1" >&2; exit 2 ;;
    esac
    shift
done

DOLPHIN_DIR="$REPO_ROOT/dolphin"
if [ ! -f "$DOLPHIN_DIR/CMakeLists.txt" ]; then
    echo "$DOLPHIN_DIR/CMakeLists.txt not found - did you run 'git submodule update --init --recursive'?" >&2
    exit 1
fi

BUILD_DIR="$REPO_ROOT/build/host-$(echo "$BUILD_TYPE" | tr '[:upper:]' '[:lower:]')"

if [ "$DO_CLEAN" -eq 1 ]; then
    rm -rf "$BUILD_DIR"
fi

GENERATOR="Unix Makefiles"
if command -v ninja >/dev/null; then
    GENERATOR="Ninja"
fi

# Configure dolphin/ with its own CMakeLists. Disable the Qt frontend
# unless the host has it - we don't need GUI for the sanity build, just
# proof that headless/core/common compile.
EXTRA_FLAGS=(
    -DENABLE_QT=OFF
    -DENABLE_TESTS=OFF
    -DENABLE_AUTOUPDATE=OFF
    -DUSE_DISCORD_PRESENCE=OFF
    -DUSE_SHARED_ENET=OFF
)

cmake -S "$DOLPHIN_DIR" -B "$BUILD_DIR" \
    -G "$GENERATOR" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    "${EXTRA_FLAGS[@]}"

# Build only the headless target. A successful link of `dolphin-emu-nogui`
# is the green-build signal. If it ever passes, then a full GUI build
# almost certainly will too (any Qt-only breakage doesn't gate Switch).
cmake --build "$BUILD_DIR" --parallel "$JOBS" --target dolphin-emu-nogui

echo
echo "host build OK ($BUILD_TYPE)"
