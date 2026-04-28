# Dolphin → Nintendo Switch (Horizon OS) Native Port

## Mission

Port the Dolphin GameCube/Wii emulator to run as a **native homebrew NRO** on Nintendo Switch's Horizon OS, requiring only Atmosphère CFW. No Android, no Linux layer, no closed-source dependencies. The output must remain GPLv2+ compatible with upstream Dolphin.

## Non-Goals - do not pursue without explicit instruction

- Switchroot / Lakka / Linux-on-Switch path (defeats the point of native HOS)
- libretro / RetroArch core (Dolphin's libretro core is ~4500 commits behind upstream; the API is a poor fit for Dolphin's threading model)
- Tico-style closed-source distribution
- Stock Switch / non-CFW support (technically impossible - do not waste effort discussing workarounds)
- Switch 2 specific paths until the original Switch port is stable
- Reaching for x86-64 intrinsics, SSE, AVX. ARM64 (Cortex-A57) only.

## Tech Stack

| Layer       | Choice                                         | Notes                                                                |
| ----------- | ---------------------------------------------- | -------------------------------------------------------------------- |
| Toolchain   | devkitPro / devkitA64                          | ARM64, Cortex-A57 target                                             |
| System libs | libnx                                          | Horizon OS homebrew library                                          |
| Graphics    | mesa OpenGL ES (via libnx EGL) - start here    | devkitPro's mesa Switch port has no Vulkan loader (verified `dkp-pacman -Ql switch-mesa`). Use Dolphin's existing `VideoBackends/OGL/` (already supports GLES). May migrate to deko3d in M6 if profiling demands it. Don't start with deko3d. See `docs/m3-graphics.md`. |
| Audio       | libnx audren                                   | Replaces Dolphin's cubeb backend                                     |
| Input       | libnx hid service                              | Maps to Wiimote / GC pad emulation                                   |
| Frontend    | ImGui + switch-sdl2                            | Replaces Qt6 GUI (Qt does not exist on Switch)                       |
| Build       | CMake + `$DEVKITPRO/cmake/Switch.cmake`        | Cross-compile from host                                              |
| Base        | Fork of `dolphin-emu/dolphin` master           | Rebased on upstream periodically                                     |

## Repository Layout

```
.
├── CLAUDE.md                    # this file - read first every session
├── dolphin/                     # git submodule: our fork of dolphin-emu/dolphin
├── reference/
│   └── xerpi-dolphin-switch/    # historical reference port. READ-ONLY. Do not edit.
├── cmake/
│   └── Switch.cmake             # project-level toolchain wrapper
├── patches/                     # patches against upstream dolphin/
│   ├── 0001-build-switch-target.patch
│   ├── 0002-jit-libnx-memory.patch
│   ├── 0003-vulkan-mesa-switch.patch
│   ├── 0004-fs-paths-sdmc.patch
│   ├── 0005-remove-qt-frontend.patch
│   └── …
├── frontend/                    # custom Switch frontend (ImGui-based)
│   ├── src/
│   ├── assets/
│   └── CMakeLists.txt
├── scripts/
│   ├── setup-toolchain.sh       # verifies devkitPro install
│   ├── build.sh                 # full Switch NRO build
│   ├── build-host.sh            # regression build for host platform
│   ├── package-nro.sh           # produces .nro with NACP + icon
│   └── deploy-to-switch.sh      # nxlink/ftpd push to a CFW Switch on LAN
├── docs/
│   ├── architecture.md
│   ├── jit-memory.md            # libnx jit_t notes - keep updated
│   ├── milestones.md            # progress log
│   └── debugging.md
└── tests/
```

## Build Commands

```bash
# First-time setup (verifies $DEVKITPRO, dependencies)
./scripts/setup-toolchain.sh
git submodule update --init --recursive

# Build the Switch NRO
./scripts/build.sh                # release
./scripts/build.sh --debug        # symbols, -O0
./scripts/build.sh --clean        # nuke build/ and rebuild

# Deploy to a Switch running hbmenu on the LAN
./scripts/deploy-to-switch.sh <switch-ip>

# Host sanity check - MUST PASS after every patch to shared code
./scripts/build-host.sh
```

The host sanity check exists because Switch-specific changes can break Linux/macOS/Windows builds. Run it after any patch that touches files outside Switch-only directories.

## Development Phases - work in order, no skipping

Each milestone has a binary done criterion. Do not start the next milestone until the current one's criterion is met.

### M0 - Toolchain & build skeleton
Empty NRO that prints "Hello Dolphin" via nxlink, boots on hardware, exits cleanly.
**Done when:** NRO runs on Switch, output appears on the host running `nxlink -s`.

### M1 - Cross-compile dolphin/ for Switch (no run)
- CMake recognizes `__SWITCH__` (auto-defined by devkitA64)
- Disable Qt, cubeb, host SDL2 integration, bundled libs that fail to cross-compile
- Stub unresolvable platform calls
**Done when:** `dolphin-emu-nogui` links to an ARM64 NRO. It will not run yet.

### M2 - JIT memory via libnx `jit_t`
- Replace JIT memory allocator in `dolphin/Source/Core/Core/PowerPC/JitArm64/` with libnx `jit_t` API
- Confirm runtime JIT capability is granted (inherited from `hbloader`; if launched from somewhere else, the NRO needs an explicit kernel-capability descriptor — see `docs/jit-memory.md`)
**Done when:** A homebrew GameCube test ROM reaches the JIT block dispatcher, executes recompiled code, and does not crash.

### M3 - OpenGL ES rendering
Activate Dolphin's `VideoBackends/OGL/` backend on mesa Switch's GLES via libnx EGL/NWindow. (Vulkan was the original plan but devkitPro's mesa Switch port ships GLES only. Full reasoning in `docs/m3-graphics.md`.)
**Done when:** A test ROM reaches the first rendered frame on the Switch display.

### M4 - Audio + input
- audren backend replacing cubeb
- hid → Wiimote / GC controller mapping
**Done when:** A commercial title (e.g. Luigi's Mansion intro) plays audio and responds to controller input.

### M5 - ImGui frontend
Game browser, settings, save-state UI.
**Done when:** User can boot a ROM end-to-end without nxlink or shell access.

### M6 - Stability & perf
Profile A57 hot paths. Tune defaults.
**Done when:** One mid-tier GameCube title sustains 30 fps with no crashes over 30 minutes.

## Critical Constraints - read before writing any code

### 1. JIT memory is THE hardest part of this port

Horizon OS forbids plain RWX pages for unprivileged processes. You **must** use libnx's JIT API:
- `jitCreate()`
- `jitTransitionToWritable()` - write recompiled instructions
- `jitTransitionToExecutable()` - flip to executable, flush icache
- `jitClose()`

The process must hold the **JIT kernel capability**. This is *not* a NACP field — it is a kernel-capability descriptor on the NRO/NSO. NRO homebrew launched from `hbmenu`/`hbloader` *inherits* the JIT capability (Atmosphère's stock loaders enable it), so the M0/M1 build pipeline does not have to set anything special. If the NRO is ever launched outside hbmenu, an explicit descriptor is required — `docs/jit-memory.md` has the details. Either way, `jitCreate` should fail loudly at startup with a probe; do not assume.

If you ever reach for `mmap` with `PROT_EXEC`, `mprotect`, or any RWX trick - stop. It will not work. The runtime failure is a `0xCAFE`-family abort that's painful to diagnose.

`rw_addr` and `rx_addr` returned by `jitCreate` are **distinct virtual addresses** aliased onto the same physical pages (verified against upstream `switchbrew/libnx:nx/source/kernel/jit.c`). The recompiler must write through `rw_addr` and the dispatcher must branch through `rx_addr`. `jitTransitionToExecutable` flushes both dcache and icache for the region, so duplicate `FlushIcache` calls on the same region are redundant but harmless.

**Always grep the headers before using a libnx symbol:**
```bash
grep -rn "jitCreate\|jitTransition" $DEVKITPRO/libnx/include/
```
Do not invent libnx function names. Read `$DEVKITPRO/libnx/include/switch/kernel/jit.h` directly when in doubt.

### 2. Never break the host build

Every Switch change must keep `dolphin-emu-nogui` building and running on Linux/macOS. Switch-specific code goes behind:
```cpp
#ifdef __SWITCH__
    // Switch path
#else
    // existing upstream path
#endif
```
Run `./scripts/build-host.sh` after every patch to shared files. A green host build is a hard prerequisite for committing.

### 3. Qt is gone - don't reach for it

If you find yourself `#include`-ing `<Q...>`, stop. The `dolphin/Source/Core/DolphinQt/` directory is excluded from the Switch build entirely. All UI lives in `frontend/` using ImGui.

The Dolphin Android port has already done the hard work of separating headless core from Qt frontend. **Check `dolphin/Source/Android/` for prior art before reimplementing anything UI-adjacent.**

### 4. Filesystem paths

Dolphin's `UserPath.cpp` assumes Unix-style user directories. On Switch:
- Writable state → `sdmc:/switch/dolphin/`
- ROMs → user-configurable, default `sdmc:/roms/`
- Sys folder (per-game compatibility DB) must ship inside the NRO's romfs

### 5. Memory budget

Switch homebrew gets roughly 3.2 GB usable. Dolphin's PC defaults (large texture cache, DSP LLE, dual-core JIT) will OOM. Switch defaults must be: HLE audio, single-core JIT, conservative texture cache size, no dual-core unless explicitly enabled.

### 6. Endianness, ABI, calling conventions

GC/Wii are big-endian PowerPC; Switch is little-endian ARM64. Dolphin already handles the byte-swap correctly because it does so for x86-64 little-endian hosts - do **not** add new swaps. ARM64 ABI differs from x86-64; the `JitArm64` backend already accounts for this.

## Coding Conventions

- Inside `dolphin/`: match upstream's `.clang-format`. Run `clang-format -i` on changed files.
- Inside `frontend/`: Google C++ style.
- Switch-only guards: `#ifdef __SWITCH__` (devkitA64 auto-defines this).
- Patches in `patches/` are regenerated with `git format-patch` - never hand-edit patch files.
- Commit messages on the dolphin submodule fork: `[switch] <component>: <change>`.
- One concern per commit. JIT changes and FS changes never go in the same commit.

## Things to Verify Before Doing - never assume

| Before… | Verify by… |
| --- | --- |
| Calling a libnx API | `grep -rn <name> $DEVKITPRO/libnx/include/` |
| Using a CMake var from devkitPro toolchain | reading `$DEVKITPRO/cmake/Switch.cmake` |
| Patching Dolphin by line number | `grep -rn` the symbol - Dolphin refactors often |
| Setting a NACP capability flag | checking the Switchbrew wiki page for that flag, not guessing the bit |
| Reimplementing a subsystem | checking if `dolphin/Source/Android/` already abstracts it |
| Using a GLES extension | running `dkp-pacman -Ql switch-mesa` and confirming the header exists; running a probe NRO and checking `glGetString(GL_EXTENSIONS)` |

## Reference Resources

- libnx source/headers: https://github.com/switchbrew/libnx
- libnx generated docs: https://switchbrew.github.io/libnx/
- Switchbrew wiki (HOS internals, NACP, services): https://switchbrew.org/wiki/Main_Page
- deko3d: https://github.com/devkitPro/deko3d
- mesa for Switch: https://github.com/devkitPro/mesa
- Dolphin upstream: https://github.com/dolphin-emu/dolphin
- xerpi reference port (dated, read-only): `reference/xerpi-dolphin-switch/`
- Dolphin Android port (best architectural reference): `dolphin/Source/Android/`

## Pitfalls / Don'ts

- **Do not** add closed-source or non-GPLv2+-compatible dependencies.
- **Do not** read or import code from Tico or any closed-source Switch Dolphin distribution.
- **Do not** push to public GitHub mirrors without a Codeberg/Gitea fallback ready - Nintendo has been DMCA-takedown-active against Switch emulation repos since 2024.
- **Do not** speculate about libnx APIs. Grep first.
- **Do not** introduce x86-64 intrinsics anywhere, even in shared code, without an ARM64 path.
- **Do not** rebase the Dolphin fork on upstream without running the host sanity build immediately afterward.
- **Do not** edit anything in `reference/`. It exists for inspection only.
- **Do not** skip milestones. M2 must work before M3 is started.

## When You Are Stuck

If a task requires information you cannot directly verify (exact NACP capability bit, current libnx Vulkan WSI extension name, Dolphin's current JIT entry symbol, etc.), **stop and ask the user**. Do not fabricate API names or guess bit flags. The failure mode of guessed APIs on Horizon OS is silent runtime aborts that take hours to bisect.

Update `docs/milestones.md` at the end of every meaningful work session - what was attempted, what worked, what didn't, what's next. This is the project's memory across Claude Code sessions.
