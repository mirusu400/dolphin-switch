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
