#!/usr/bin/env bash
# Build the Switch NRO.
#
# Usage:
#   scripts/build.sh           # release
#   scripts/build.sh --debug   # symbols, -O0
#   scripts/build.sh --clean   # nuke build/ and rebuild

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_TYPE="Release"
DO_CLEAN=0

for arg in "$@"; do
    case "$arg" in
        --debug) BUILD_TYPE="Debug" ;;
        --clean) DO_CLEAN=1 ;;
        -h|--help)
            grep -E '^# ' "$0" | sed 's/^# \{0,1\}//'
            exit 0 ;;
        *) echo "unknown arg: $arg" >&2; exit 2 ;;
    esac
done

: "${DEVKITPRO:?DEVKITPRO is unset; run: source /etc/profile.d/devkit-env.sh}"

TOOLCHAIN="$DEVKITPRO/cmake/Switch.cmake"
[ -f "$TOOLCHAIN" ] || { echo "Switch.cmake not found at $TOOLCHAIN"; exit 1; }

BUILD_DIR="$REPO_ROOT/build/switch-$(echo "$BUILD_TYPE" | tr '[:upper:]' '[:lower:]')"

if [ "$DO_CLEAN" -eq 1 ]; then
    rm -rf "$BUILD_DIR"
fi

GENERATOR="Unix Makefiles"
if command -v ninja >/dev/null; then
    GENERATOR="Ninja"
fi

cmake -S "$REPO_ROOT" -B "$BUILD_DIR" \
    -G "$GENERATOR" \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE"

cmake --build "$BUILD_DIR" --parallel "$(nproc)"

NRO=$(find "$BUILD_DIR" -name 'dolphin-switch-frontend.nro' -print -quit)
if [ -n "$NRO" ]; then
    echo
    echo "NRO: $NRO"
    ls -lh "$NRO" | awk '{print "size:", $5}'
else
    echo "NRO not produced - check build output above" >&2
    exit 1
fi
