#!/usr/bin/env bash
# Verify devkitPro toolchain for the Switch port.
# Fails fast with an actionable message if anything is missing.

set -euo pipefail

red()  { printf '\033[31m%s\033[0m\n' "$*"; }
grn()  { printf '\033[32m%s\033[0m\n' "$*"; }
ylw()  { printf '\033[33m%s\033[0m\n' "$*"; }

fail() { red "FAIL: $*"; exit 1; }
ok()   { grn "OK:   $*"; }

: "${DEVKITPRO:?DEVKITPRO is unset. Install devkitPro and source /etc/profile.d/devkit-env.sh}"
[ -d "$DEVKITPRO" ] || fail "DEVKITPRO=$DEVKITPRO does not exist"
ok "DEVKITPRO=$DEVKITPRO"

[ -d "$DEVKITPRO/devkitA64" ]  || fail "devkitA64 missing - run: dkp-pacman -S devkitA64"
[ -d "$DEVKITPRO/libnx" ]      || fail "libnx missing - run: dkp-pacman -S libnx"
[ -d "$DEVKITPRO/tools" ]      || fail "tools dir missing - run: dkp-pacman -S switch-tools"
ok "devkitA64, libnx, tools dirs present"

GCC="$DEVKITPRO/devkitA64/bin/aarch64-none-elf-gcc"
[ -x "$GCC" ] || fail "$GCC not executable"
"$GCC" --version | head -1 | sed 's/^/      /'
ok "aarch64-none-elf-gcc"

for tool in elf2nro nacptool nxlink build_pfs0; do
    p="$DEVKITPRO/tools/bin/$tool"
    [ -x "$p" ] || fail "$tool missing at $p"
done
ok "elf2nro nacptool nxlink build_pfs0"

TC="$DEVKITPRO/cmake/Switch.cmake"
[ -f "$TC" ] || fail "Switch.cmake missing at $TC"
ok "Switch.cmake at $TC"

JIT_HDR="$DEVKITPRO/libnx/include/switch/kernel/jit.h"
[ -f "$JIT_HDR" ] || fail "libnx jit.h missing at $JIT_HDR (M2 will fail without it)"
grep -q 'jitCreate'                "$JIT_HDR" || fail "jitCreate symbol missing in $JIT_HDR"
grep -q 'jitTransitionToWritable'  "$JIT_HDR" || fail "jitTransitionToWritable missing in $JIT_HDR"
grep -q 'jitTransitionToExecutable' "$JIT_HDR" || fail "jitTransitionToExecutable missing in $JIT_HDR"
ok "libnx jit.h has expected symbols"

[ -f "$DEVKITPRO/libnx/lib/libnx.a" ] || fail "libnx.a missing"
ok "libnx.a present"

command -v cmake >/dev/null || fail "cmake not in PATH"
ok "cmake $(cmake --version | head -1 | awk '{print $3}')"

command -v ninja >/dev/null && ok "ninja $(ninja --version)" || ylw "WARN: ninja not in PATH (build.sh will fall back to make)"

grn "Toolchain OK."
