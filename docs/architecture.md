# Architecture

High-level map of the Switch port. Read this with `CLAUDE.md` and
`docs/jit-memory.md` open.

## Layering

```
+------------------------------------------------------------+
|  frontend/  (M5)        ImGui + switch-sdl2                |
|  - game browser, settings, save-state UI                   |
+------------------------------------------------------------+
|  dolphin/Source/Core/Core   (M1-M2)                        |
|  - PowerPC interpreter + JitArm64 recompiler               |
|  - HLE / boot / DSP / movie / save-state engine            |
+----------------+-----------+--------------+----------------+
|   Video        |  Audio    |   Input      |   FS / Paths   |
|   (M3)         |  (M4)     |   (M4)       |   (M1+)        |
| Vulkan via     | audren    | hid → GC pad | sdmc:/switch/  |
| mesa/libnx WSI |           | + Wiimote    |   dolphin/     |
+----------------+-----------+--------------+----------------+
|              libnx services + Horizon SVCs                 |
|   jit_t • virtmem • applet • hid • audren • socket • fs    |
+------------------------------------------------------------+
|              Atmosphère CFW + Horizon OS kernel            |
+------------------------------------------------------------+
```

The hard rule from CLAUDE.md: every Switch-specific code path lives behind
`#ifdef __SWITCH__`. Host (Linux/macOS/Windows) builds keep working. Run
`scripts/build-host.sh` (M1+) after any patch to shared code.

## Repository layout

| Path                            | Purpose                                                        |
|---------------------------------|----------------------------------------------------------------|
| `dolphin/`                      | Submodule — our fork of `dolphin-emu/dolphin`. M1+ touches it. |
| `reference/xerpi-dolphin-switch/` | Read-only historical port. Symbol/file recon only.            |
| `cmake/`                        | Project-level toolchain / find_package overrides.              |
| `frontend/`                     | M0 hello-world today; ImGui frontend in M5.                    |
| `patches/`                      | `git format-patch` output from `dolphin/`. Never hand-edited.  |
| `scripts/`                      | `setup-toolchain.sh`, `build.sh`, `build-host.sh` (M1+), `package-nro.sh` (folded into CMake), `deploy-to-switch.sh`. |
| `docs/`                         | This file, `jit-memory.md`, `milestones.md`, `debugging.md`.   |
| `tests/`                        | Test ROMs / harness scripts.                                   |

## Toolchain

- devkitA64 ≥ 15.x (devkitPro pacman-managed): `aarch64-none-elf-gcc` 15.2.0
  is the verified version. Cortex-A57 target.
- libnx (devkitPro): headers at `/opt/devkitpro/libnx/include/switch/`,
  static lib at `/opt/devkitpro/libnx/lib/libnx.a`.
- switch-tools: `elf2nro`, `nacptool`, `nxlink`, `build_pfs0` at
  `/opt/devkitpro/tools/bin/`.
- CMake toolchain file: **`/opt/devkitpro/cmake/Switch.cmake`**
  (CLAUDE.md previously had this path wrong; fixed).
- Host: `cmake` ≥ 3.20 + `ninja-build` (Ubuntu apt).

`scripts/setup-toolchain.sh` verifies all of the above.

## Build

`scripts/build.sh` invokes:

```
cmake -S <repo> -B build/switch-release \
      -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/Switch.cmake \
      -DCMAKE_BUILD_TYPE=Release
cmake --build build/switch-release
```

`Switch.cmake` in turn pulls in `Platform/NintendoSwitch.cmake`, which
defines `nx_create_nro` and `nx_generate_nacp`. Our `frontend/CMakeLists.txt`
calls both — NACP carries metadata, `nx_create_nro` packages
`elf2nro --icon ... --nacp ...` into the `.nro`.

## Critical platform constraints

These are repeated from CLAUDE.md so cross-references are easy. The
authoritative version is CLAUDE.md.

### 1. JIT memory

No RWX pages. Use libnx `jit_t`. See `docs/jit-memory.md` for the swap
plan. M2 is the milestone where this lands.

### 2. Filesystem paths

`dolphin/Source/Core/Common/UserPath.cpp` assumes Unix-style user dirs.
Switch overrides:

- Writable state → `sdmc:/switch/dolphin/`
- ROMs → user-configurable, default `sdmc:/roms/`
- `Sys/` (per-game compatibility DB, GC IPL hashes, etc.) — bundled
  inside the NRO's romfs so we never need write access to it

`File::SetUserPath` and friends will need a `__SWITCH__` arm. M1+.

### 3. Memory budget

~3.2 GB usable (see `docs/jit-memory.md` "Memory budget"). Switch defaults
must differ from desktop:

| Setting              | Desktop default | Switch default |
|----------------------|-----------------|----------------|
| Audio backend        | cubeb (HW DSP)  | HLE            |
| CPU thread mode      | dual-core JIT   | single-core JIT |
| Texture cache size   | large           | conservative   |
| GPU texture decoding | GPU             | GPU only if perf wins |

Wired up in M4 (audio default) and M6 (perf tuning).

### 4. ABI / endianness

GameCube/Wii are big-endian PowerPC; Switch is little-endian ARM64.
Dolphin already handles the byte-swap for x86-64 little-endian hosts. Do
not add new swaps. The `JitArm64` backend already accounts for ARM64 ABI.

## Dependencies on Dolphin internals

The Android port (`dolphin/Source/Android/`) is the **best architectural
reference** because it has already separated headless core from Qt
frontend. Specifically:

- `dolphin/Source/Android/jni/MainAndroid.cpp` — entry that boots the
  emulator without Qt. We will mirror this shape in `frontend/`.
- `dolphin/Source/Core/UICommon/UICommon.{h,cpp}` — the shared
  initialization layer used by both Qt and Android. Our frontend uses
  this directly.
- `dolphin/Source/Core/Core/HW/GBACore.cpp` and friends — already have
  `#ifdef ANDROID` arms we can study for path conventions.

We do **not** depend on:

- `dolphin/Source/Core/DolphinQt/` — excluded entirely.
- Bundled Qt-specific assets, themes, translations — none.
- `cubeb` (replaced by audren in M4).
- Host SDL2 integration — replaced by `switch-sdl2` (devkitPro port,
  used only inside ImGui).

## Subsystem mapping

### CPU / JIT (M1, M2)
- `dolphin/Source/Core/Core/PowerPC/JitArm64/` — recompiler. M2 swap
  point detailed in `docs/jit-memory.md`.
- `dolphin/Source/Core/Core/PowerPC/Interpreter/` — fallback. Untouched
  by us.
- `dolphin/Source/Core/Common/MemoryUtil.{h,cpp}` —
  `AllocateExecutableMemory` / `FreeMemoryPages` /
  `JITPageWriteEnableExecuteDisable` / `JITPageWriteDisableExecuteEnable`
  all need `__SWITCH__` arms. See `docs/jit-memory.md`.
- `dolphin/Source/Core/Common/MemArena.{h,cpp}` — emulated GameCube/Wii
  RAM with multi-VA aliasing. Separate problem from the JIT buffer; M1
  stubs out, M2.5 implements. See `docs/emulated-memory.md`.

### Video (M3)
- `dolphin/Source/Core/VideoBackends/Vulkan/` — primary target.
- `dolphin/Source/Core/VideoBackends/Vulkan/VulkanLoader.cpp` —
  WSI surface creation. Will need a Switch arm using `nwindowGetDefault`
  and the libnx Vulkan WSI extension (`VK_NN_vi_surface` is the relevant
  upstream extension; verify against the mesa Switch driver before
  coding — do not assume).
- `dolphin/Source/Core/VideoCommon/VertexLoaderARM64.cpp` — already
  ARM64. JIT memory swap (M2) covers it via the same allocator.

### Audio (M4)
- `dolphin/Source/Core/AudioCommon/` — backend-agnostic mixer.
- New: `AudioCommon/AudrenStream.{h,cpp}` — implements the
  `SoundStream` interface against libnx `audren`. Replaces `CubebStream`.

### Input (M4)
- `dolphin/Source/Core/InputCommon/ControllerInterface/` — backend
  registry. New `Switch/SwitchHIDController.{h,cpp}` translates
  libnx `hid` into the existing `ControllerInterface`. GC pad and
  Wiimote profiles map onto Switch pad layouts.

### Frontend (M5)
- `frontend/src/main.cpp` — current M0 stub. M5 expands it into:
  - ImGui main loop on top of `switch-sdl2`
  - `UICommon::Init` / `UICommon::Shutdown` lifecycle
  - file picker over `sdmc:/`
  - settings UI for the Switch defaults table above

### FS (M1+)
- `dolphin/Source/Core/Common/CommonPaths.h`,
  `dolphin/Source/Core/Common/FileUtil.{h,cpp}`,
  `dolphin/Source/Core/Common/UserPath.{h,cpp}` — all need
  `sdmc:/switch/dolphin/` paths.
- `dolphin/Sys/` shipped in NRO romfs (read-only). The
  `nx_create_nro(... ROMFS ...)` argument will package it once we get
  there.

## Build outputs

```
build/switch-<type>/frontend/dolphin-switch-frontend.elf   # raw ELF, with debug info if --debug
build/switch-<type>/frontend/dolphin-switch-frontend.nro   # final homebrew binary (M5+ ships this)
build/switch-<type>/frontend/dolphin-switch.nacp           # generated metadata
```

The `.nro` is what `scripts/deploy-to-switch.sh` pushes via `nxlink`.

## What we are not doing (and why)

- **No Switchroot / Lakka / Linux-on-Switch.** Defeats the point of a
  native HOS port.
- **No libretro core.** Dolphin's libretro core is far behind upstream and
  the API mismatches Dolphin's threading model.
- **No Switch 2.** Original Switch first.
- **No deko3d as M3 starting point.** Mesa Vulkan ships in libnx; deko3d
  is a future optimization if profiling demands it.
- **No x86-64 intrinsics, anywhere.** ARM64 only.
- **No closed-source bits / Tico-derived code.** GPLv2+ only. Output
  must be redistributable.
