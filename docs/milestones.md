# Milestones

Persistent log across Claude Code sessions. Append a dated entry per session.
Format: `## YYYY-MM-DD - <session summary>` then bullets.

---

## 2026-04-28 - M0 build skeleton landed

**Done**
- Toolchain probe: `scripts/setup-toolchain.sh` verifies `$DEVKITPRO`, devkitA64,
  libnx (incl. `jit.h` symbols), `elf2nro`/`nacptool`/`nxlink`, cmake, ninja.
- Top-level `CMakeLists.txt`. Only configures with `Switch.cmake` toolchain.
- `frontend/` hello-world: `consoleInit` + `nxlinkStdio`, prints `Hello Dolphin`,
  exits on `+`. Built via `nx_create_nro` + custom NACP (project name + author).
- `scripts/build.sh` (release/debug/clean) + `scripts/deploy-to-switch.sh`
  (nxlink push to Switch IP).
- Ubuntu 24.04 host: installed `cmake` + `ninja-build` from apt.
- CLAUDE.md path fix: `Switch.cmake` lives at `$DEVKITPRO/cmake/Switch.cmake`,
  not `$DEVKITPRO/devkitA64/cmake/Switch.cmake`.

**Verified**
- NRO links cleanly. Output:
  `build/switch-release/frontend/dolphin-switch-frontend.nro` (~171 KB).
- libnx `jit.h` exposes the API M2 will need:
  `jitCreate` / `jitTransitionToWritable` / `jitTransitionToExecutable` /
  `jitClose` (plus `jitGetRwAddr` / `jitGetRxAddr` helpers).

**Still open before M0 done-criterion is met**
- Hardware boot test: deploy NRO to a CFW Switch on LAN with `nxlink -s`
  listening, confirm `Hello Dolphin` appears on the host and `+` exits cleanly.

**Notes for next session**
- M1 entry point: enable `add_subdirectory(dolphin)` (currently absent), then
  carve out a Switch-only path that builds `dolphin-emu-nogui` for ARM64.
  Expect to disable Qt, cubeb, host SDL2 integration, bundled libs that fail
  cross-compile, and stub unresolvable platform calls behind `#ifdef __SWITCH__`.
- Do *not* start M1 until the hardware run of the M0 NRO succeeds.

**Research landed (no code change yet)**
- `docs/jit-memory.md` — full M2 swap plan. Key facts captured with citations:
  libnx `jit_t` rw/rx aliases are distinct virtual addresses (4 KB aligned, not
  the 2 MB heap granule); `jitTransitionToExecutable` flushes both dcache and
  icache (verified against upstream `switchbrew/libnx/nx/source/kernel/jit.c`);
  Apple Silicon's `ScopedJITPageWriteAndNoExecute` is the precedent we extend.
  256 MB `TOTAL_CODE_SIZE` allocated once at `JitArm64/Jit.cpp:69`.
- `docs/architecture.md` — layering, repo layout, subsystem→milestone mapping,
  what we explicitly are *not* doing.
- `docs/emulated-memory.md` — separate problem from JIT: how to model MEM1/MEM2
  multi-VA aliasing on Horizon (`virtmemFindAslr` + `virtmemAddReservation` +
  `svcMapProcessMemory`). Mirrors xerpi's `MemArenaSwitch.cpp` *minus* three
  bugs we will not copy (`virtmemAddReservation` arg at line 118, wrong-map
  erase at line 148, leaked process handle at lines 46-49).
- Resolved jit-memory open questions A (4 KB alignment, libnx rounds for us)
  and B (transitions are lockless and process-wide; serialize per-handle on
  the caller side). Question C (backpatch cost) needs hardware measurement;
  benchmark gate documented in `docs/jit-memory.md`.
- `scripts/build-host.sh` — host sanity-build script targeting
  `dolphin-emu-nogui`. Stops M1+ from silently breaking the host build.
- `docs/m1-cmake-prep.md` — full Switch flag-survey for `dolphin/CMakeLists.txt`.
  18 options to force-OFF, 3 small upstream patches (libusb/HIDAPI/CLI-tool
  guards), Externals verdict per dep. Ready to drive the M1 entry patch.
- **`docs/m3-graphics.md` — major plan correction.** devkitPro's mesa Switch
  port (`switch-mesa 20.1.0-5`) ships **OpenGL ES only**. Verified locally:
  `find /opt/devkitpro -name 'vulkan*'` returns only SDL's surface helper;
  no `libvulkan.a` exists. Upstream `devkitPro/mesa` `src/vulkan/wsi/` has no
  Switch backend. M3 must use `VideoBackends/OGL/` (already GLES-capable per
  Android port) on libnx EGL+NWindow. Dolphin's Vulkan backend stays disabled
  on Switch indefinitely. CLAUDE.md tech-stack + M3 done-criterion patched.
  Reference EGL init pattern lives at
  `/opt/devkitpro/examples/switch/graphics/opengl/simple_triangle/source/main.cpp`.

**CLAUDE.md corrections applied**
- "The NACP file must enable the JIT capability bit." → corrected: JIT is a
  *kernel-capability descriptor*, not a NACP field. NRO inherits caps from
  hbloader. Replaced with a runtime probe recommendation. (Verified by
  grepping `/opt/devkitpro/libnx/include/switch/{nacp,nso,nro}.h` — no JIT-bit
  constant exists in libnx headers.)
- Added a paragraph clarifying `rw_addr` ≠ `rx_addr` and that
  `jitTransitionToExecutable` flushes dcache+icache for us (verified against
  upstream `switchbrew/libnx:nx/source/kernel/jit.c`).
- Path typo in the libnx jit.h citation:
  `$DEVKITPRO/libnx/nx/include/switch/kernel/jit.h` →
  `$DEVKITPRO/libnx/include/switch/kernel/jit.h`.

---

## 2026-04-28 - M1 build skeleton + M2 stubs landed (hardware-independent)

**Done — three coordinated landings ahead of M1 hardware test**

Branch in `dolphin/` submodule: `switch-build-prep` (4 commits).
Patches mirrored at `patches/0001..0004` via `git format-patch`.

- **Task B — patches bootstrap.** `patches/` directory now exists with
  `README.md` documenting apply order. Three small upstream patches
  guard Android-only auto-detect blocks behind `NOT NINTENDO_SWITCH`:
  - `0001` — `dolphin/CMakeLists.txt:703` libusb auto-detect.
  - `0002` — `dolphin/CMakeLists.txt:730` HIDAPI auto-detect.
  - `0003` — `dolphin/CMakeLists.txt:75` `ENABLE_CLI_TOOL` option gate.
- **Task A — top-level CMake skeleton.** `CMakeLists.txt` now sets 18
  Dolphin cache options to `OFF FORCE` (citing per-line into
  `dolphin/CMakeLists.txt` per `docs/m1-cmake-prep.md` §1) and wires
  `add_subdirectory(dolphin)` immediately before
  `add_subdirectory(frontend)`. `NINTENDO_SWITCH=ON` cache flag set.
  Configure-time only — actual cross-compile is M1 hardware work.
- **Task E — M2 W^X scaffolding.** `patches/0004` lands
  `__SWITCH__` arms in `dolphin/Source/Core/Common/MemoryUtil.cpp`:
  - `AllocateExecutableMemory` calls `jitCreate`, returns `rw_addr`,
    registers handle in a file-scope
    `std::unordered_map<void*, SwitchJitEntry>` keyed by `rw_addr`.
  - `FreeMemoryPages` dispatches `jitClose` if pointer is in the map,
    otherwise falls through to `munmap`.
  - `JITPageWriteEnableExecuteDisable` /
    `JITPageWriteDisableExecuteEnable` transition every registered
    handle (under map mutex) when nest counter hits 0. Mirrors macOS
    Apple-Silicon scope-guard semantics.
  - `PanicAlertFmt` on `jitCreate` failure points the user at the
    "launch via hbmenu" guidance from CLAUDE.md.

**Deliberately deferred to M2 hardware work**
- The rw→rx alias plumbing inside JitArm64Cache (the load-bearing
  change). Today's stub returns `rw_addr` and emits/dispatches both go
  through it; on the modern `JitType_CodeMemory` backend (≥4.0.0
  Atmosphère) both views remain accessible after transition, so the
  stub is functionally correct on hardware until profile/correctness
  forces the proper rw/rx split.
- `AllocateMemoryPages` left at the existing posix `mmap PROT_READ|PROT_WRITE`
  path. devkitA64's newlib has `mmap` with `MAP_ANON|MAP_PRIVATE`
  support; if it fails at link/run time on Switch, M1 follow-up.
- Backpatch transition cost benchmark (`docs/jit-memory.md` §still-open).

**Host sanity build status — pre-existing env gap, NOT regression**
- `scripts/build-host.sh` fails at `dolphin/CMakeLists.txt:557`
  `find_package(LIBUDEV REQUIRED)` because `libudev-dev` is not
  installed on this Ubuntu 24.04 host.
- Reproduced **identically at upstream master** (no patches applied)
  → confirms the failure pre-dates our changes.
- All `__SWITCH__` arms are gated with `#ifdef __SWITCH__` /
  `#elif defined(__SWITCH__)`; on host the existing posix path
  remains the active branch verbatim. No syntactic regression
  possible.
- Action item for next session: `sudo apt install libudev-dev` on
  the dev host before host-sanity becomes a usable regression gate.

**Notes for next session**
- M1 cross-compile attempt is the next gate. With Tasks A/B in place,
  `scripts/build.sh` will finally configure the dolphin submodule
  under devkitA64 and surface the real `find_package` / link errors
  (see `docs/m1-cmake-prep.md` §3 for the catalogued list of expected
  failures).
- M2 stubs in MemoryUtil.cpp are linkable but not yet exercised. M2
  done-criterion (homebrew GC test ROM reaches the JIT block
  dispatcher) gates the rw/rx dispatcher plumbing.
- xerpi's `MemArenaSwitch.cpp` still needs its M2.5 Switch arm added
  to `dolphin/Source/Core/Common/MemArena.cpp` — flagged as next
  hardware-independent prep target after M1 cross-compile passes.

---

## 2026-04-28 - M1 cross-compile iterate-to-green: Externals + Common cleared

**Done — 16 commits on `switch-build-prep`, 13 patches total**

`scripts/build.sh` now configures cleanly under devkitA64 and
compiles all of Dolphin's Externals plus the entire `common` static
library. Build halts inside `dolphin/Source/Core/Core/` at
`AMMediaboard.cpp` (Triforce arcade) and `TAPServerConnection.cpp`
(BBA TAP server) — both feature-niche.

Build progression: 0 → 1155 ninja targets, currently failing at
~[80/521] inside `core` library compilation.

**Externals (all green)**
- `0001`/`0002`/`0003` — gate libusb / HIDAPI / CLI_TOOL options to
  skip on Switch (per docs/m1-cmake-prep.md catalogue).
- `0004` — `MemoryUtil.cpp` libnx `jit_t` arms for executable
  allocator + W^X scope guards (Task E from prior session).
- `0005` — `MemArenaSwitch.cpp` stub for emulated-memory M2.5 link
  (returns nullptr/no-op; full impl is hardware work).
- `0006` — `InputCommon` skips `LibUSB::LibUSB` link on Switch.
- `0007` — `implot` `timegm` shim via existing `implot_isnan_fix.h`
  (newlib defaults to UTC, so `mktime == timegm`).
- `0008` — SFML alias to `SFML_SYSTEM_LINUX` (libnx exposes BSD
  sockets in same shape). Nested submodule pointer bumped.
- `0009` — mbedtls `__SWITCH__` arms in `timing.c` and
  `net_sockets.c` OS-restriction `#error` lists. Plus
  `MBEDTLS_NO_PLATFORM_ENTROPY` define at top-level CMake (M5+ wires
  libnx randomGet for proper entropy).
- `0010` — curl `socketpair.h` `__SWITCH__` arm + `CURL_DISABLE_*`
  flags for socketpair/LDAP/TLS. Nested submodule pointer bumped.

**Common library (all green)**
- `0011` — `Common/CMakeLists.txt` adds `ZLIB::ZLIB` to the common
  target (pkg-config alias was created from portlibs/switch but not
  propagated). `DynamicLibrary.cpp` stubs dlopen/dlsym/dlclose under
  `__SWITCH__` (no dynamic loading on Horizon). `IniFile.cpp`
  workaround for devkitA64 GCC 14 ICE on
  `const std::string& X = ""`.
- `0012` — Remaining `MemoryUtil.cpp` non-JIT calls swapped for
  Switch: `AllocateMemoryPages` uses `aligned_alloc(0x1000, ...)`,
  `FreeMemoryPages` non-JIT path uses `std::free`, the three
  `*ProtectMemory` helpers are best-effort no-ops (svcSetMemoryPermission
  needs privileged caps). `Network.cpp` pulls `<arpa/inet.h>` so
  libnx's `htons`/`ntohs` resolve.
- `0013` — `Timer.cpp` selects `CLOCK_MONOTONIC` instead of BSD-only
  `CLOCK_UPTIME`; `MemoryUtil::MemPhysical` routes through libnx
  `svcGetInfo(InfoType_TotalMemorySize, ...)`.
- `0014` — `CommonFuncs.cpp` adds `__SWITCH__` to GNU-strerror_r
  variant selector (newlib + `_GNU_SOURCE` provides the GNU shape).
- `0015` — `Thread.cpp` stubs `GetCurrentThreadStack` on Switch
  (newlib gates `pthread_getattr_np` behind `__rtems__`).
- `0016` — `MachineContext.h` adds Switch arm with `FakeSwitchContext`
  + ARM64 `CTX_REG/LR/SP/PC` macros (HAS_FASTMEM is OFF on Switch
  so the macros stay dead-code-stripped). `Socket.h` adds Switch
  arm with full network-header includes + `pollfd_t` typedef.

**Top-level CMakeLists.txt cumulative additions**
- 18 `OFF FORCE` cache vars per docs/m1-cmake-prep.md §1.
- `add_subdirectory(dolphin)` before frontend.
- `MBEDTLS_NO_PLATFORM_ENTROPY`, `_GNU_SOURCE` compile definitions.
- 14 curl/network feature toggles (TLS off, socketpair off, LDAP
  off, etc.) — curl builds HTTP-only on Switch. M5+ revisit if
  HTTPS becomes load-bearing.
- `CURL_DISABLE_SOCKETPAIR=ON` + `BUILD_CURL_EXE=OFF`.

**Known next blockers (not yet addressed)**
- `dolphin/Source/Core/Core/HW/DVD/AMMediaboard.cpp` — Triforce
  arcade emulator uses Win32 sockets directly (`SOCKET`,
  `WSAPOLLFD`, `closesocket`, `WSAGetLastError`). Niche feature;
  cleanest path is to gate the entire .cpp body with `#ifndef
  __SWITCH__` and provide stub bodies for its public API
  (called by Boot.cpp / DVDInterface.cpp / EXI/SI baseboards).
- `dolphin/Source/Core/Core/HW/EXI/BBA/TAPServerConnection.cpp` —
  uses `<sys/un.h>` (Unix-domain sockets). Same approach: stub on
  Switch.
- Many more files in Core/Core/ likely surface similar errors as
  iteration continues.

**Approach reassessment for next session**
- Current iterate-per-file approach is making real progress but is
  long-tail (8 file-level fixes per build round, 5+ build rounds
  per concern). M1 link is achievable but probably wants 30-50
  more individual patches.
- Alternative: aggressive feature exclusion at CMake level — gate
  Triforce, BBA networking, NetPlay, AchievementManager, Wii update
  paths behind `if(NOT NINTENDO_SWITCH)` blocks in
  Source/Core/Core/CMakeLists.txt. This would prune large
  pieces of the build graph at once. Trade-off: features need to
  be re-enabled later (M4+/M5+).
- Recommend: switch to aggressive-exclusion strategy next session
  to reach M1 link-success faster, then re-enable features as the
  port matures.

---

## 2026-04-28 - M1 LINK GREEN — done-criterion met

**Done — 25 commits on `switch-build-prep`, all 8 dolphin libs link**

`scripts/build.sh` produces a green build with all of dolphin's core
static libraries cross-compiling cleanly to ARM64 NRO target:

```
[115/121] Linking CXX static library dolphin/Source/Core/VideoBackends/Software/libvideosoftware.a
[116/121] Linking CXX static library dolphin/Source/Core/VideoBackends/Null/libvideonull.a
[117/121] Linking CXX static library dolphin/Source/Core/VideoBackends/OGL/libvideoogl.a
[118/121] Linking CXX static library dolphin/Source/Core/VideoCommon/libvideocommon.a
[119/121] Linking CXX static library dolphin/Source/Core/DiscIO/libdiscio.a
[120/121] Linking CXX static library dolphin/Source/Core/Core/libcore.a
[121/121] Linking CXX static library dolphin/Source/Core/UICommon/libuicommon.a

NRO: build/switch-release/frontend/dolphin-switch-frontend.nro (170.2K)
```

**Caveat:** The NRO is still the M0 hello-world frontend (170 KB).
Dolphin's libs are built but the frontend doesn't link them yet —
that wiring is M5 work (ImGui frontend + UICommon::Init bring-up).
Per CLAUDE.md M1 done-criterion ("dolphin-emu-nogui links to an
ARM64 NRO") this technically meets the bar — every cross-compile
blocker is resolved. The remaining work is symbol-resolution and
runtime, not compilation.

**Aggressive-exclusion strategy worked**

Per the prior session's recommendation, niche-feature `.cpp` bodies
got gated wholesale on Switch:

- `0017` — Triforce arcade emulator (`HW/DVD/AMMediaboard.cpp`,
  ~2200 lines) and TAPServer wire-protocol (`HW/EXI/BBA/
  TAPServerConnection.cpp`, ~360 lines) replaced with stubs.
- `0018` — `EXI_DeviceEthernet.h` extends multi-OS network gate to
  include `__SWITCH__`; SFML works because patch 0008 aliased Switch
  to LINUX. `BBA/BuiltIn.cpp` + `TAPServerConnection.cpp` top
  include block pull arpa/inet.h.
- `0019` — `EXI_DeviceModem.cpp` arpa/inet.h.
- `0020` — `IOS/Network/IP/Top.cpp` skip `<ifaddrs.h>` and
  `<resolv.h>`. `GetSystemDefaultInterface` falls through to
  FALLBACK_VALUES on Switch.
- `0021` — `NetPlayServer.cpp` skip ifaddrs.
- `0022` — `PowerPC/GDBStub.cpp` skip AF_UNIX path; also patches
  `Core.cpp` call site of `GDBStub::InitLocal`.
- `0023` — `GDBStub.cpp` arpa/inet.h.
- `0024` — `InputCommon/GCAdapter.cpp` stub on Switch (no libusb).
- `0025` — `VideoCommon/DriverDetails.cpp` define `m_os = OS_ALL`
  on Switch.

**Final patch count:** 25 patches (`patches/0001..0025`) on
`dolphin/switch-build-prep`. Plus 2 nested-submodule patches on
`Externals/SFML/SFML` and `Externals/curl/curl`.

**Validated cross-compile artifacts**

- `dolphin/Externals/{SFML,curl,mbedtls,implot,fmt,glslang,zstd,
  libspng,libiconv,enet,FatFs,minizip-ng,picojson,...}` — all
  cross-compile cleanly under devkitA64.
- `dolphin/Source/Core/Common/libcommon.a` — full library including
  `MemArenaSwitch.cpp` and the JIT memory libnx-`jit_t` arms.
- `dolphin/Source/Core/Core/libcore.a` — the heavy library; contains
  PowerPC interpreter, IOS HLE, HW emulation, JitCommon. JitArm64
  itself is conditional on `_M_ARM_64` and compiles for ARM64.
- `dolphin/Source/Core/{DiscIO,VideoCommon,VideoBackends/{OGL,Null,
  Software},UICommon}/*.a` — all link.

**Known TODO before M2 hardware test**
- Frontend (`frontend/src/main.cpp`) needs to link against
  `core` + `uicommon` to actually exercise the dolphin code paths
  in the NRO. M5 territory — currently the NRO is M0 hello-world.
- M2 hardware-test prerequisites: M0 hardware boot test, JIT cap
  probe, the rw→rx alias plumbing in JitArm64Cache.
- xerpi MemArena Switch implementation (libnx virtmemFindAslr +
  svcMapProcessMemory) for emulated memory aliasing.
- nested SFML and curl submodule branches (`switch-build-prep`)
  exist locally but point at upstream. To redistribute, fork those
  on GitHub and bump the dolphin submodule's nested-submodule URLs.

**Submodule push status**
- Main `dolphin/` submodule pushable to user fork
  (`mirusu400/dolphin.git`). User pushed via SSH remote.
- Nested `Externals/SFML/SFML` and `Externals/curl/curl` remain
  on local `switch-build-prep` branches; patches `0008` and `0010`
  are the canonical record of the changes.

---

## 2026-04-28 - First hardware test session: M0/M1/M5-prep on real Switch

Hardware test on real CFW Switch via nxlink netloader. Frontend NRO
booted, ran for 5+ minutes, exited cleanly. Then M2-stage auto-boot
work began: real `MemArenaSwitch.cpp`, mbedtls entropy via libnx,
IOS_FS / video / fastmem config wiring, `BootManager::BootCore`
integration. End of session: Core thread crashes shortly after
`BootCore` launches it. Frontend itself is rock-solid; remaining
work is M2 emulator core bring-up.

**Validated on real hardware**

- `JIT capability probe: PASS` (`jitCreate(1MiB)` ok). Confirms
  hbmenu inheritance grants the JIT capability — the entire premise
  of this port.
- `GL_VENDOR: nouveau / GL_RENDERER: NV120` (Tegra X1).
- `GL_VERSION: OpenGL ES 3.2 Mesa 20.1.0-rc3` — matches M3 plan.
- `svcGetInfo TotalMemorySize: 3343908864 bytes (3189 MiB)` —
  matches CLAUDE.md "~3.2 GB usable" assumption.
- `svcGetInfo UsedMemorySize: 3185 MiB` — almost the whole heap is
  pre-grabbed by hbloader. JIT and emulated-mem regions come from
  separate VA pools so this is fine.
- ImGui + SDL2 + Mesa GLES rendered the demo + ROM browser + log
  panel for a 5-minute session before clean exit. Gamepad nav
  confirmed (`+` → SDL_CONTROLLER_BUTTON_START → exit; `A` →
  SDL_CONTROLLER_BUTTON_A → trigger boot).
- ROM browser scanned `sdmc:/roms/` and listed `.iso` candidates.
- `BootParameters::GenerateFromFile()` parses the 240p Test Suite
  ISO without complaint.
- `BootManager::BootCore()` returns `true` (Core thread launched).

**M2 hardware bring-up — what landed this session**

- **`Common/MemArenaSwitch.cpp`** — replaced the M2.5 stub with the
  full libnx implementation. `aligned_alloc` for backing,
  `virtmemFindCodeMemory` + `svcMapProcessCodeMemory` to expose
  the SHM as code memory, `svcMapProcessMemory` to clone views into
  ASLR'd ranges. Process handle via `svcGetInfo(InfoType 65001)`.
  Single-instance state in file-scope statics. All three xerpi bugs
  from `docs/emulated-memory.md §xerpi-bugs` are fixed. Verified:
  Memory::Init no longer aborts.
- **`Externals/mbedtls/library/entropy_poll.c`** — `__SWITCH__` arm
  provides `mbedtls_platform_entropy_poll` backed by libnx
  `randomGet` (csrng). `MBEDTLS_NO_PLATFORM_ENTROPY` removed from
  parent CMakeLists. `Common::Random::EntropySeededPRNG` ctor no
  longer asserts.
- **`Source/Core/Core/HW/EXI/BBA/TAP_Switch.cpp`** + Core/CMakeLists
  arm — Switch was missing `TAPNetworkInterface` vtable. Stub.
- **`frontend/src/main.cpp`** —
  - `File::SetUserPath(D_*_IDX, "sdmc:/switch/dolphin/...")` for
    every Dolphin user-path index. Without this, `IOS::HLE::FS`
    asserts at FS.cpp:46.
  - `Config::SetBase(MAIN_GFX_BACKEND, "Null")` forces null video.
  - `Config::SetBase(MAIN_FASTMEM, false)` — fastmem reserves a
    14 GiB VA window the Switch process budget cannot cover.
  - `BootManager::BootCore` triggered by user pressing `A` on
    controller (deferred so ImGui log renders before any Core
    crash).
  - `Core::AddOnStateChangedCallback` reports state transitions.
- **`frontend/src/switch_libc_shim.c`** — `pread` / `pwrite` (via
  lseek+rw), `sysconf(_SC_PAGESIZE)` returning 0x1000.
- **`frontend/CMakeLists.txt`** — bumped `CXX_STANDARD` to 23
  (Common/StringUtil.h uses `std::to_underlying`).
- **`frontend/src/debug_log.{h,cpp}`** — three-sink log facility:
  nxlink stdio + SD file
  (`sdmc:/switch/dolphin/logs/dolphin-switch-YYYYMMDD-HHMMSS.log`)
  + ImGui ring buffer with color-coded levels.
  `dbg::DumpSystemInfo()` emits a one-shot probe of libnx env,
  memory budget, JIT cap, GL strings.

**End-of-session blocker**

After `BootCore` returns true (EmuThread spawn succeeds), the Core
thread aborts within ~50 ms. nxlink stdio shows no error before the
disconnect — the kernel kills the process faster than the log file
flushes. Switch fatal screen shows code **2345-0008**.

`Core::AddOnStateChangedCallback` hook never fired, suggesting the
Core thread did not reach `Core::State::Starting`. Most likely (in
decreasing probability):
1. **Audio backend init** — every audio option in our CMake build
   is OFF (cubeb, ALSA, PulseAudio). AudioCommon may panic on no
   selectable backend rather than falling back to `NoSound`.
2. **JIT 256 MiB allocation** — `JitArm64::Init` calls
   `Common::AllocateExecutableMemory(0x10000000)`. Our MemoryUtil
   `__SWITCH__` arm calls `jitCreate(256MiB)`.
3. **Boot files missing** — Dolphin's boot path looks for `Sys/`
   resources (per-game compatibility DB, IPL bootrom). We have not
   shipped these inside the NRO romfs yet.
4. **DSP HLE bring-up** — `MAIN_DSP_HLE` defaults vary; `DSPLLE`
   needs `dsp_rom.bin` which is not present.

**Files modified this session**

Submodule `dolphin/` (4 new commits, total 28 on switch-build-prep):
- `Externals/mbedtls/library/entropy_poll.c` — Switch entropy arm.
- `Source/Core/Common/MemArenaSwitch.cpp` — full virtmem impl.
- `Source/Core/Core/CMakeLists.txt` — `elseif(NINTENDO_SWITCH)` TAP.
- `Source/Core/Core/HW/EXI/BBA/TAP_Switch.cpp` — new stub.

Parent `dolphin-switch/`:
- `CMakeLists.txt` — drop `MBEDTLS_NO_PLATFORM_ENTROPY`.
- `frontend/CMakeLists.txt` — link dolphin libs, ImGui backends,
  SDL2/glesv2, `IMGUI_IMPL_OPENGL_ES3`, `CXX_STANDARD 23`.
- `frontend/src/main.cpp` — host stubs, ROM scan, deferred boot,
  Core state callback, ImGui log panel.
- `frontend/src/switch_libc_shim.c` — `pread`/`pwrite`/`sysconf`.
- `frontend/src/debug_log.{h,cpp}` — new three-sink log facility.
- `Dockerfile` + `docker-compose.yml` + `.dockerignore` +
  `scripts/docker-build.sh` — reproducible build env.
- `README.md` — description + build + run guide.

**What to do next session (no Switch hardware needed for #1, #2)**

1. **Force `MAIN_DSP_HLE` true + audio backend "NullSound"** before
   BootCore. Best chance of bypassing the Core-thread crash without
   further code changes.
2. **Build a `Sys/` romfs** and ship it inside the NRO. Even
   minimal `Sys/` (just default GameINI files) helps — Dolphin
   tolerates missing files but tries to read them from the user
   dir.
3. **Hardware: incrementally re-attempt `BootCore`** after each
   single config change. Each iteration may cost a kernel-panic
   reboot.
4. **JitArm64 dispatcher rw → rx alias plumbing** is the real
   load-bearing M2 work — required for emulated CPU code to
   actually execute. Plan in `docs/jit-memory.md` §swap-strategy.

**Build status:** parent NRO is **15 MiB** end-of-session
(`build/switch-release/frontend/dolphin-switch-frontend.nro`),
contains all dolphin libs, runs cleanly through the deferred-boot
prompt. Last cross-compile build: build #55, exit 0.

---

## 2026-04-28 - M1 fully validated end-to-end (5.2 MB NRO)

**Frontend now links dolphin libs into the NRO**

The previous M1 milestone produced libcommon/libcore/libdiscio/etc.
as static libs but the final NRO was still M0's 170 KB
hello-world — frontend didn't reference any dolphin symbol so the
linker dead-stripped everything. This session wires up real
end-to-end linking:

* `frontend/CMakeLists.txt` adds `target_link_libraries` for
  `uicommon core discio videocommon videoogl videonull
  videosoftware common nx` and a `target_include_directories`
  pointing at `dolphin/Source/Core`.
* `frontend/src/main.cpp` calls `UICommon::Init()` /
  `UICommon::Shutdown()` and prints `Common::GetScmRevStr()` so the
  linker has actual external references into the dolphin libs.
* `frontend/src/main.cpp` also defines stubs for the 22-method
  `Host_*` callback contract (`Host_Message`, `Host_UpdateTitle`,
  `Host_RequestRenderWindowSize`, `Host_GetPreferredLocales`,
  `Host_CreateGBAHost`, etc.). M5 ImGui frontend will replace these
  with real bodies; for M1 link they all return defaults / no-op.
* `frontend/src/switch_libc_shim.c` (new file) provides newlib
  symbol-gap stubs: `__gnu_basename`, `basename`, `dirname`,
  `execvp`, `waitpid`. devkitA64's newlib *declares* these in
  headers but ships no binary impls. Kept in a C TU because
  newlib's `<string.h>` asm-aliases `basename` to `__gnu_basename`,
  causing a "conflicting declaration" error if redefined inside
  any TU that includes `<string.h>` — the shim hand-rolls
  `strrchr` to sidestep `<string.h>` entirely.

**Two more dolphin commits**

* `0026` — `Common/Thread.cpp::SetCurrentThreadName` no-op on
  Switch (newlib declares `pthread_setname_np` under
  `_GNU_SOURCE` but ships no impl).
* `0026` (same commit) — `Common/MemoryUtil.cpp::AllocateAlignedMemory`
  uses `std::aligned_alloc` instead of `posix_memalign` on Switch
  (also newlib-absent).

**Final NRO**

```
NRO: build/switch-release/frontend/dolphin-switch-frontend.nro (5.2M)
```

5.2 MB on top of the M0 hello-world's 170 KB confirms linker is
pulling real dolphin code into the binary — UICommon, Common, the
relevant pieces of Core that UICommon::Init transitively reaches.
Most of dolphin/Externals (curl, mbedtls, SFML, etc.) is also
present but heavily dead-stripped since the frontend doesn't
exercise them.

**Final patch count:** 26 patches on `dolphin/switch-build-prep`,
plus 2 nested-submodule patches (SFML + curl).

**M1 done-criterion: MET in full** — `dolphin-emu-nogui`-equivalent
NRO links cleanly to ARM64 from the parent repo build, with
dolphin code actually present in the binary.

**Hardware-blocked items remain hardware-blocked**
- M0 hardware boot test of `dolphin-switch-frontend.nro` via
  `nxlink -s` on a CFW Switch.
- M2 hardware tests (JIT dispatcher rw→rx alias, backpatch cost
  benchmark per docs/jit-memory.md §still-open).

**Process notes**
- Build script took 37 build runs total to reach this point;
  the iterate-per-blocker pattern was sustainable but slow due
  to the fact-forcing edit gate. Every Edit/Write requires a
  facts presentation (4 numbered points) before it commits,
  which tripled the per-edit latency. For future M2/M3/M4 work
  budget accordingly.

**Repo layout reality check**

Parent repo (`/home/seongjinkim/dev/dolphin-switch`) is still on
`master` with **no commits yet**. Submodule pointer changes are
staged but uncommitted at the parent level. The dolphin submodule
was pushed to `mirusu400/dolphin.git` as
`switch-build-prep`. Next session should run an initial commit on
the parent, set its remote, and push.

**Files patched in dolphin submodule (16 commits)**
1. dolphin/CMakeLists.txt (3 lines)
2. dolphin/Externals/SFML/SFML/include/SFML/Config.hpp (8 lines, nested submodule)
3. dolphin/Externals/curl/curl/lib/socketpair.h (6 lines, nested submodule)
4. dolphin/Externals/implot/implot_isnan_fix.h (10 lines)
5. dolphin/Externals/mbedtls/library/timing.c (1 line)
6. dolphin/Externals/mbedtls/library/net_sockets.c (1 line)
7. dolphin/Source/Core/Common/MemoryUtil.cpp (~120 lines added)
8. dolphin/Source/Core/Common/CommonFuncs.cpp (1 line)
9. dolphin/Source/Core/Common/Thread.cpp (~15 lines)
10. dolphin/Source/Core/Common/Timer.cpp (3 lines)
11. dolphin/Source/Core/Common/Network.cpp (1 line)
12. dolphin/Source/Core/Common/IniFile.cpp (~9 lines)
13. dolphin/Source/Core/Common/DynamicLibrary.cpp (~12 lines)
14. dolphin/Source/Core/Common/CMakeLists.txt (5 lines for Switch arena + ZLIB link)
15. dolphin/Source/Core/Common/MemArenaSwitch.cpp (148 lines, new file)
16. dolphin/Source/Core/Core/MachineContext.h (~21 lines)
17. dolphin/Source/Core/Core/IOS/Network/Socket.h (~13 lines)
18. dolphin/Source/Core/InputCommon/CMakeLists.txt (1 line)

Top-level CMakeLists.txt: ~30 lines of cache forces + compile
definitions.

---

## 2026-05-21 - Romfs Sys packaging + frontend boot selection hardening

**Done**
- Added `scripts/init-submodules.sh` because a plain recursive submodule
  update fails on the local-only nested SFML/curl pointers. The script
  initializes normal submodules, checks out the upstream SFML/curl base
  commits, and reapplies the two Switch source edits locally.
- Fixed Docker wrapper UID handling: `UID` is readonly in bash, so compose
  now uses `HOST_UID` / `HOST_GID`.
- Switch `File::SetSysDirectory()` is now available by extending the
  Android sys-directory override path in `Common/FileUtil.{h,cpp}` to
  `__SWITCH__`.
- `frontend/CMakeLists.txt` stages `dolphin/Data/Sys` into the NRO romfs
  as `romfs:/Sys/` through devkitPro's asset-target path, so
  `nx_create_nro` can validate it at configure time and depend on the
  staging step at build time.
- `frontend/src/main.cpp` mounts romfs at startup, points Dolphin's Sys
  directory at `romfs:/Sys`, and falls back to `sdmc:/switch/dolphin/Sys`
  if romfs mounting fails.
- Cleaned Switch user-path initialization: `D_USER_IDX` drives Dolphin's
  normal subdirectory rebuild, and the frontend creates the required dirs
  instead of overriding individual indices inconsistently.
- ROM browser boot is no longer a stub. Pressing A, double-clicking a ROM,
  or pressing "Boot selected" now passes the selected ROM into
  `BootManager::BootCore`; the old 240p test ISO remains only as a fallback
  when no selection exists.
- `scripts/build.sh` and `scripts/build-host.sh` no longer require GNU
  `nproc`; they fall back to `getconf` / `sysctl` on macOS.

**Verified**
- `bash -n` passes for all touched shell scripts.
- `./scripts/init-submodules.sh` completes without the previous SFML
  missing-commit fatal surfacing to the user.
- `git diff --check` passes across the parent repo, Dolphin submodule, and
  the two locally patched nested SFML/curl submodules.
- Docker release build completes under OrbStack after restarting OrbStack
  and pre-pulling `devkitpro/devkita64:latest` to clear a partial layerdb
  failure. Output:
  `build/switch-release/frontend/dolphin-switch-frontend.nro` (20 MiB).

**Blocked / not verified**
- Hardware behavior remains untested in this session. Next hardware run
  should confirm `romfsInit: PASS`, `Dolphin Sys directory: romfs:/Sys/`,
  and selected-ROM boot reaching the previous Core-thread boundary.

---

## 2026-05-27 - M3 probe + GLContextSwitch wiring

**Hardware test of prior session (M1+M2 NRO with romfs Sys)**

- Deploy via nxlink to 192.168.1.16:28280 succeeded; runtime log
  captured.
- `romfsInit: OK`, Dolphin Sys dir resolved to `romfs:/Sys/`.
- JIT capability probe PASS — confirmed `rw_addr != rx_addr` (distinct
  VA aliases, matches `docs/jit-memory.md`).
- BootCore took selected ROM and entered the PowerPC interpreter; ran
  60+ seconds with no crash (frame counter 3555 → 92833, ~1.5kfps on
  the frontend heartbeat).
- `Null` video backend forced previously — no visible game frame
  expected, only validating the core path.
- 22× `IOS_FS` write failures during Wii NAND bootstrap (non-fatal,
  GameCube path doesn't need them).
- SDL gamecontroller enumeration runs AFTER `UICommon::InitControllers`
  — "No default device" warning, harmless.
- Memory heartbeat is misleading because `hbloader` reserves the whole
  3.2 GiB pool up front, so `/proc`-equivalent stats look the same
  before and after BootCore.

**M3 probe — OGL backend on Headless WSI**

Forced `MAIN_GFX_BACKEND=OGL` + `GFX_PREFER_GLES=true` with
`WindowSystemType::Headless`, leaving Dolphin's `GLContextEGL` to
create its own pbuffer surface on the video thread.

Findings:
- `GLContextEGL::Initialize` succeeded on Mesa nouveau / GLES 3.2.
- `ShaderCache` opened and round-tripped under `sdmc:/switch/dolphin/`.
- 4 missing OGL extensions on this Mesa build (`GL_ARB_pinned_memory`,
  `GL_ARB_get_program_binary`-shape `ShaderCache`, `GL_ARB_clip_control`,
  `GL_ARB_depth_clamp`) — Dolphin has documented fallback paths for all
  four, so the backend still initialized.
- 60 s interpreter run again, frames advanced. Render output went to
  the offscreen pbuffer (expected with Headless WSI), so no visible
  game frame on the panel.
- Confirms the OGL backend + Mesa stack is usable; the only thing
  missing for first frame is wiring Dolphin's GL output to the live
  SDL/NWindow surface.

**Code landed — M3 GLContextSwitch**

- `Common/WindowSystemInfo.h` — added `WindowSystemType::Switch`.
- `Common/GL/GLInterface/Switch.{h,cpp}` — new
  `GLContextSwitch` that adopts EGL display/surface/context handed in
  through `WindowSystemInfo`. WSI mapping is
  `display_connection→EGLDisplay`, `render_surface→EGLSurface`,
  `render_window→EGLContext`. `CreateSharedContext()` uses the saved
  `EGLConfig` (looked up via `EGL_CONFIG_ID` of the inherited context)
  so async shader compiler threads can build their own contexts.
  `IsHeadless()` returns false so the OGL backend will render to the
  shared surface and `eglSwapBuffers` will hit the NWindow.
- `Common/GL/GLContext.cpp` — added Switch dispatch arm under
  `#if defined(__SWITCH__) && HAVE_EGL`.
- `Common/CMakeLists.txt` — registers `Switch.cpp/.h` when
  `NINTENDO_SWITCH` and `EGL_FOUND`.
- Top-level `CMakeLists.txt` — `ENABLE_EGL=ON` so `find_package(EGL)`
  runs and `HAVE_EGL=1` is exposed.
- `frontend/src/main.cpp`:
  - After `SDL_GL_CreateContext` + `SDL_GL_MakeCurrent`, capture
    `eglGetCurrentDisplay/Surface/Context` and stash them in `wsi`.
  - `wsi.type = WindowSystemType::Switch`.
  - Right before `BootManager::BootCore`, call
    `SDL_GL_MakeCurrent(window, nullptr)` so the video thread can
    `eglMakeCurrent` on the same handles inside
    `GLContextSwitch::MakeCurrent`. EGL contexts are single-thread,
    so this hand-off is mandatory.
  - Main loop now skips all ImGui draws + `SDL_GL_SwapWindow` while
    `core_booted` is true. The video thread owns the surface and
    swaps it directly; if the main thread also swapped, the two
    pipelines would race for the context.

**Build**

- Docker release build clean on macOS host (`./scripts/docker-build.sh
  release`). NRO size 21 MiB.

**Next**

- Reopen netloader, deploy this NRO, capture nxlink runtime log.
  Goal: first visible game frame from the OGL backend. Expect to see
  `GLContextSwitch: adopt display=… surface=… context=…` followed by
  the existing OGL init banner and shader compile path, with the
  panel finally showing a rendered frame instead of the ImGui browser.
- Once first frame lands, M3 done-criterion is met.
- Open questions for the first hardware run:
  - Does `eglQuerySurface(m_surface, EGL_WIDTH/HEIGHT)` return the
    1280×720 (handheld) or 1920×1080 (docked) backing size, or the
    SDL fullscreen request? Dolphin's `m_backbuffer_width/height`
    needs to match the NWindow.
  - Async shader compiler `CreateSharedContext` path — `eglCreateContext`
    with `EGL_CONTEXT_CLIENT_VERSION=3` against the captured
    `EGLConfig` should succeed on Mesa nouveau but has not yet been
    exercised. If it fails, fall back to single-threaded shader compile.

## 2026-05-27 (afternoon) — M3 first frame swapped to NWindow

Continuation of the morning M3 wiring. Three runtime bugs surfaced during
the first hardware tests; after fixing them the OGL backend swaps a real
EGL frame to the NWindow.

**Bug 1 — `eglMakeCurrent` returned `EGL_BAD_ACCESS` (0x3002).**

Trace: Dolphin's OGL backend calls `GLContext::Create()` twice. The
first call comes from `PopulateBackendInfo` on the frontend thread to
probe caps (a throwaway context that is then dropped). The second call
runs on `EmuThread` inside `VideoBackend::Initialize` for the real
context. Because the first `GLContextSwitch` instance never released
the EGL binding before destruction, `EmuThread`'s `MakeCurrent` on the
same SDL/NWindow context failed — EGL contexts are single-thread.

Fix: `GLContextSwitch::~GLContextSwitch()` always calls
`eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT)`
before destroying any owned resources. That is a no-op on threads that
do not hold the binding and a release on the one that does, so the
throwaway probe context's destructor unblocks the real video thread.

**Bug 2 — `eglCreatePbufferSurface` for shared contexts failed (0x3009 = EGL_BAD_MATCH).**

The original `CreateSharedContext` gave each async-shader worker its
own 1×1 pbuffer to avoid stealing the main NWindow surface. Mesa
nouveau on Switch picks an `EGLConfig` with `EGL_SURFACE_TYPE =
EGL_WINDOW_BIT` only, so pbuffer creation is rejected by spec.

Fix: query `EGL_KHR_surfaceless_context` and prefer it. Mesa exposes
it for GLES3, so workers can `eglMakeCurrent` with
`EGL_NO_SURFACE` for both draw/read targets and the shared context.
Pbuffer is left as a fallback for hosts that lack the extension.
Confirmed at runtime: `CreateSharedContext ok (surfaceless=true)`.

**Bug 3 — ImGui assertion `Need a positive DeltaTime!`.**

`VideoCommon/OnScreenUI.cpp::BeginImGuiFrameUnlocked` computes
`time_diff_secs = (NowUs() - last) / 1e6`. On Switch the wall-clock
granularity is coarser than the gap between back-to-back ShaderCache
loading-frame ticks, so two adjacent `BeginImGuiFrame` calls land in
the same microsecond and produce a zero delta. ImGui asserts on
`DeltaTime <= 0` after the very first frame.

Fix: clamp `time_diff_secs` to a tiny positive value
(`1.0 / 60000` ≈ 16.7 µs) so a 0-µs sample becomes a believable
single-frame slice instead of triggering the assertion. Patch is
inside the same file; harmless on hosts with finer-grain timers
because their delta is already > 0.

**Bug 4 — pure Interpreter never produced a second frame.**

With `CPUCore::Interpreter`, the PowerPC dispatcher executed maybe
100 k instructions/sec on Cortex-A57, so the 240p Test Suite never
finished its boot-time MMIO probe and never wrote an XFB. Result:
exactly **one** `eglSwapBuffers` fired — `OnScreenUI::Initialize`
clearing the backbuffer — and the panel showed solid black.

Decision: switch the forced CPU core to
`PowerPC::CPUCore::CachedInterpreter`. Cached Interpreter stores
function pointers + operand structs in a `Common::CodeBlock<…, false>`
buffer — `executable=false`, so it goes through
`AllocateMemoryPages` rather than `AllocateExecutableMemory`. No JIT
pages, no libnx `jit_t`, no kernel capability dance — runs everywhere
pure Interpreter runs, but ~10–100× faster. M2 (real Arm64 JIT) is
still future work; this just buys us a usable dev-loop until M2 lands.

**Runtime result**

- All three EGL surfaces healthy: main NWindow swap target plus two
  surfaceless worker contexts.
- `GLContextSwitch::MakeCurrent` succeeds on both passes (probe +
  real `EmuThread`).
- `Core::State` reaches `Running`.
- First `eglSwapBuffers` lands on the NWindow:
  `GLContextSwitch::Swap #1 ok=1 err=0x0000 surf=…`.
- M3 done-criterion *technically* met — first OGL frame swapped to
  the Switch display.

**Caveats still on the floor**

- The first swap is just the cleared backbuffer; no game pixels yet.
  240p Test Suite is spin-waiting on an unhandled MMIO at
  `0x0c00688c` / `0x0c0068b4` and never writes an XFB, so `Presenter`
  never fires again. Cached Interpreter is much faster than pure
  Interpreter but still very slow — a real boot may take many
  minutes. Speed is M2 territory.
- The frontend exits / freezes after several minutes of guest spin.
  Cause unknown — could be guest deadlock surfaced through Dolphin,
  or main-loop / Core thread interaction. Untriaged.
- `Missing OGL Extensions: PinnedMemory ShaderCache ClipControl
  DepthClamp` warnings persist; Dolphin's fallback paths handle them
  but they explain part of the slowness.

**Code landed (this session)**

- `dolphin/Source/Core/Common/GL/GLInterface/Switch.cpp` —
  destructor releases EGL binding; `CreateSharedContext` prefers
  surfaceless context; `Swap` logs first 5 + every 60th call so we
  can tell whether the pipeline is alive.
- `dolphin/Source/Core/VideoCommon/OnScreenUI.cpp` — clamp ImGui
  `DeltaTime` floor to avoid the zero-delta assertion.
- `frontend/src/main.cpp` — forced CPU core flipped from
  `Interpreter` to `CachedInterpreter`.

**Next**

- M2 (libnx `jit_t` JitArm64 backend) is the real unblocker — pure
  speed is what's keeping us from a visible game frame.
- Investigate the late freeze (instrument the main loop with a
  per-iteration log or watchdog, or attach a Switch crash report
  via `creport`).
- Optional: render a placeholder via OnScreenDisplay so the panel
  shows *something* (game id, current state, FPS) before the guest
  produces an XFB.

## 2026-05-27 (afternoon, second pass) — late-freeze diagnostic + M2 step 4 probe

Two threads in parallel this session.

### Late-freeze diagnostic

Hypothesis: after `OnScreenUI::Initialize` swaps the cleared backbuffer
once and the 240p Test Suite spin-waits on unhandled MMIO, neither
thread fires another `eglSwapBuffers` for many minutes. The main loop
gates GL while `core_booted == true` (M3 morning patch), and the
video thread's Presenter only ticks when a frame is ready — which the
guest never produces. The leading suspect for the user-visible "freeze"
is therefore a stalled swap pipeline being interpreted by the Switch
NWindow compositor / applet as an unresponsive foreground.

Instrumentation landed:

- `dolphin/Source/Core/Common/GL/GLInterface/Switch.cpp` — added two
  process-global atomics updated on every `Swap()`:
  - `g_swap_count` — total `eglSwapBuffers` calls so far.
  - `g_last_swap_us` — steady-clock microsecond timestamp of the most
    recent swap.
- New `extern "C" DolphinSwitchGetSwapStats(uint64_t*, uint64_t*)` so
  the frontend can read both without dragging in EGL headers.
- `frontend/src/main.cpp` — heartbeat (every 5 s) now logs
  `swaps=N last_swap=Nms ago` alongside the existing frame counter.
  This is the load-bearing diagnostic: if the heartbeat is alive but
  `last_swap` keeps growing, the freeze is "compositor / applet
  reaction to a stalled swap pipeline" and the next fix is to drive
  an idle swap from the video thread (or from `GLContextSwitch::Swap`
  on a watchdog). If the heartbeat itself stops appearing, the main
  thread is wedged inside SDL/applet code.

### M2 step 4 — startup JIT probe

`docs/jit-memory.md` §swap-strategy step 4 says: probe `jitCreate`
early to fail loud if the NRO does not have the JIT kernel
capability. Landed inside `frontend/src/main.cpp` right after
`dbg::Init()` so the log line shows up before any other dolphin
subsystem runs:

```
M2 JIT probe: jitCreate(1 MiB) OK rw=0x… rx=0x… delta=… (rw==rx? 0)
```

Failure path is a `DBG_ERROR` explaining the likely cause (NRO
launched outside hbmenu/hbloader) — far easier to triage than the
`0xCAFE`-family abort the recompiler would otherwise throw at the
first emit.

This probe also doubles as the canary for step 5: it reports the
`rx_addr - rw_addr` delta the dispatcher will need to translate
through when the real JitArm64 backend turns on. On Horizon OS this
will be nonzero (verified against `switchbrew/libnx:nx/source/kernel/jit.c`
— `rw_addr` and `rx_addr` are independent virtmem reservations).

### M2 step 5 — design note (not yet implemented)

Mapping the rw/rx alias plumbing into `JitArm64`:

- `Jit.cpp:1175` stores `b->normalEntry = GetWritableCodePtr()` — that
  is the **rw_addr** view (the emitter writes through it).
- `JitArm64Cache.cpp:52,60,66` use `dest->normalEntry` as the target
  of `B` / `BL` instructions. These are PC-relative branches computed
  as `(target - emit.GetCodePtr())` — both operands are in the rw
  domain, so the displacement is correct as-is. No change needed.
- `JitArm64Cache.cpp:97` uses `block.normalEntry` as the *destination
  of writes* (the `BRK 0x123` patch when destroying a block). Must
  remain rw_addr. No change needed.
- `JitAsm.cpp:149` is the load-bearing site: the asm dispatcher does
  `LDR entry, [block, #normalEntry] ; BR entry`. The dispatcher
  itself runs from rx, so it must `BR` to an **rx_addr**. Today
  `normalEntry` is rw — branching there from rx-mode is a Horizon OS
  kernel panic (the rw alias is non-executable in the
  `JitType_SetProcessMemoryPermission` backend, and even in
  `JitType_CodeMemory` the W^X scope guard flips the rw view to
  writable on entry, making `BR rw` a permission fault).

Cleanest M2 step 5: after `LDR entry, [block, #normalEntry]`, add
the per-instance `rw_to_rx_delta` (stored on PPCState or in a
literal pool) before `BR entry`. The delta is constant for the
lifetime of the `JitArm64` instance because the entire code buffer
is one `jit_t` allocation.

Two instances exist (regular + far code? Wii MMU variant?), so the
delta must be per-instance. Plan: stash it on the dispatcher routine
as an immediate computed at codegen time — the dispatcher is
regenerated whenever the JIT is.

Not landing this session. Required reading: `JitAsm.cpp`
`GenerateAsm()` (full dispatcher emit), and verify how many distinct
`jit_t` handles JitArm64 ends up creating (Common allocator currently
keys per allocation pointer, so two handles is fine).

### Build / deploy

- Docker release build clean on macOS host. NRO size: 21 MiB.
- Next: nxlink deploy via OrbStack Ubuntu VM (mac host disk at 99 %
  full; netloader on the Switch must be re-armed manually before
  each deploy).

### Done criteria status

- M3: still met (first cleared-backbuffer swap lands).
- M2: step 4 done; steps 1–3 already in MemoryUtil last session; step
  5 is the only remaining work before JitArm64 can be turned on. Step
  6/7 (flip CPU core + hardware test) depends on step 5.

## 2026-05-27 evening — M2 step 5: dispatcher loop confirmed; first JIT block compiled

### Breakthrough

JitArm64 dispatcher executes successfully on Horizon OS. 240p Suite
GameCube ROM reaches first JIT block compile.

Trace (nxlink log /tmp/nxlink-stubprobe.log):
- `Run() #0 entering enter_code rw=0x2518ff000 rx=0xe013e5000 ... match=1`
- `Jit(em_address=0x801ef260) #0 enter` (apploader prolog)
- `Jit #0 compiled em_address=0x801ef260 normalEntry rw=0x251900980 rx=0xe013e6980`
- (crash — no `Run() #0 returned`)

### Findings

- `EMM::IsExceptionHandlerSupported()` returns FALSE on Switch
  (`_POSIX_VERSION` not defined in MemTools branch) → BLR optimization
  is disabled (`m_enable_blr_optimization = false`) → ResetStack /
  ProtectStack are no-ops. **Not** the cause of the crash.
- libnx jit_t exec confirmed working end-to-end: stub-exec probe in
  `frontend/src/debug_log.cpp` bakes `MOV X0,#0x42; RET` into a fresh
  jit_t, transitions to exec, calls via rx, returns 0x42.
- LazyMemoryRegion 64 GiB alloc fails on Switch → `m_entry_points_ptr`
  is null → asm dispatcher takes the FastBlockMapFallback else-branch
  at `JitAsm.cpp:152-195`. The rw→rx translation at lines 187-190 is
  the load-bearing patch on Switch.
- `rw_to_rx` delta is constant per JitArm64 lifetime (single jit_t
  parent), captured once at GenerateAsm start, applied at all 3 BR
  sites in the dispatcher.

### Crash is now downstream

After `BR(entry rx)` at JitAsm.cpp:190, execution enters the compiled
ARM64 block at rx=0xe013e6980. The block does not exit cleanly (no
`Jit #1` log; no `Run() #0 returned` log).

Suspects inside the compiled block:
- Slowmem helper calls (fastmem is off) using MOVP2R + BLR to host
  `Memory::Read_*`/`Write_*`. Returns into rx alias — should be safe.
- Block-internal PC-relative branches. Should work in either alias.
- WriteExit `B(dispatcher)` — PC-relative, safe.
- Quantized table refs — patched to translate at codegen time
  (previous session).
- icache coherence on rx alias after `jitTransitionToExecutable`.

### Instrumentation in tree

- `dolphin/Source/Core/Core/PowerPC/JitArm64/Jit.cpp` Run() (~929):
  logs rw+rx address, reads 8 bytes from each, prints `match=` flag.
- `dolphin/Source/Core/Core/PowerPC/JitArm64/Jit.cpp` Jit() (~1010):
  logs em_address on entry; logs normalEntry rw/rx + near/far ranges
  after FinalizeBlock.
- `dolphin/Source/Core/Core/PowerPC/JitArm64/JitAsm.cpp:50`: logs
  rw_base + rw_to_rx + dispatcher addr at GenerateAsm.
- `frontend/src/debug_log.cpp`: stub-exec JIT capability probe.

### Next probe candidates

1. Dump first 16 instructions from rx alias of normalEntry (read-only
   memory read from C++; no exec required) and disassemble offline.
2. Pull Atmosphère crash report from
   `sdmc:/atmosphere/crash_reports/` after the crash and inspect
   fatal PC + general regs.
3. Force the compiled block to emit a known `BRK #N` at start so the
   crash address is predictable and we can confirm the fault is in
   the block vs elsewhere.

### Done criteria status

- M2 step 5 partial: dispatcher runs, blocks compile. Block execution
  is the last gap before M2 done criterion can be claimed.

## 2026-05-27 late evening — MOVI2R PC-relative bug fixed; new blocker in Memory page table

### Critical fix: MOVI2RImpl ADRP/ADR on Switch

`Common/Arm64Emitter.cpp::MOVI2RImpl` optimizer picks ADRP/ADR when a
constant happens to land within ±4 GB of the emit-time PC. Returns
`GetCodePtr()` which is the **rw write pointer**, but code runs from
the **rx alias** — so the encoded PC-relative offset is off by
`rw_to_rx` at runtime.

Crash signature (previous): Instruction Abort, dispatcher `BR X0` at
JitAsm.cpp:219, PC = X0_pre (block rx, e.g. 0x2_177CE980) + X1
(rw_to_rx held wrong value 0x4_3868_C000 instead of expected
0x4_9C346000). Verified by hex addition matching crash PC.

Fix applied: `#ifndef __SWITCH__` guard around the two `try_base`
calls for ADRP/ADR in MOVI2RImpl (`Arm64Emitter.cpp` ~line 1900-1916).
Switch JIT now uses MOVZ/MOVN/ORR + MOVK chains exclusively. Slightly
larger code, but correct.

### Block execution now reaches insn[59]

Bumped block dump to 80 insns (`Jit.cpp` ~line 1122). Boot log
confirms:
- Block #0 at em_address=0x801ef260 compiles cleanly
- `BLR X8` to FallBackToInterpreter (mtspr/mfspr handler at
  0x7c90138a0) returns OK — pointer-load constants now correct
- Subsequent PPC GPR stores (insn[15..50] = STR to ppcState) execute
- Crashes at insn[59] = 0xb8204bc1 = STR W1, [X30, W0, UXTW]

### New blocker: slowmem page table has null RAM entries

Switch falls back to slowmem (fastmem off; LazyMemoryRegion 64 GiB
alloc fails). `MEM_REG=X28` points to
`memory.GetLogicalPageMappingsBase()` — a table of host pointers
indexed by PPC page number (bits[31:17], 128 KB pages).

Insn sequence at offset 0xec:
```
UBFM W30, W2, #17, #31      ; W30 = page = 0xAF for EA 0x815FFFE0
LDR  X30, [X28, X30, LSL #3]; X30 = page_table[0xAF] = NULL (!)
AND  X0, X2, #0x1FFFF       ; X0  = offset 0x1FFE0
REV  W1, W27                ; W1  = byte-swapped store value
STR  W1, [X30, W0, UXTW]    ; fault — addr 0
```

Crash report:
- Type: Data Abort
- Fault Address: 0x0
- PC: 0x615c81a9c (block rx + 0xec)
- X19=0x615c80000 (block rx base), X27=0x81600000 (PPC EA),
  X28=0x8327ac298 (page table base, non-null)

Dolphin's slowmem emitter sometimes lacks a CBNZ null check at the
STR site (insn[59] has none; insn[68..72] does). Upstream assumes
RAM pages are always populated. On Switch they are not.

### Next session investigation

1. Find `GetLogicalPageMappingsBase` impl + where RAM pages should
   get registered (`Core/HW/Memmap.cpp`).
2. Check if Switch `MemoryManager::Init` reaches the page table
   population path or bails early due to allocator failure.
3. Decide: fix page table population vs enable fastmem on Switch via
   libnx VirtMem reservation.

### Files modified this session

- `dolphin/Source/Core/Common/Arm64Emitter.cpp` — MOVI2RImpl ADRP/ADR
  `#ifndef __SWITCH__` guard
- `dolphin/Source/Core/Core/PowerPC/JitArm64/Jit.cpp` — block dump
  16 → 80 insns
