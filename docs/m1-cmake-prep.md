# M1 CMake prep — what to flip before `add_subdirectory(dolphin)`

When M1 starts, the first patch wires `add_subdirectory(dolphin)` into
the top-level `CMakeLists.txt`. Before that line is reached, every
incompatible Dolphin feature must be force-disabled in the cache. This
file is the checklist. All citations are against the current state of
`/home/seongjinkim/dev/dolphin-switch/dolphin/CMakeLists.txt` (upstream
master). If upstream rebases, re-run the survey.

## 1. Options to force OFF (cache-force, before `add_subdirectory`)

```cmake
set(NINTENDO_SWITCH ON CACHE BOOL "Building for Nintendo Switch" FORCE)

# UI / packaging
set(ENABLE_QT          OFF CACHE BOOL "" FORCE)   # dolphin/CMakeLists.txt:86  - Qt6 not available
set(ENABLE_TESTS       OFF CACHE BOOL "" FORCE)   # :94
set(ENABLE_AUTOUPDATE  OFF CACHE BOOL "" FORCE)   # :98
set(ENABLE_CLI_TOOL    OFF CACHE BOOL "" FORCE)   # :76

# Networking / presence
set(USE_DISCORD_PRESENCE OFF CACHE BOOL "" FORCE) # :96
set(USE_UPNP             OFF CACHE BOOL "" FORCE) # :84
set(USE_SHARED_ENET      OFF CACHE BOOL "" FORCE) # use bundled enet

# Graphics — re-enabled in M3
set(ENABLE_VULKAN OFF CACHE BOOL "" FORCE)        # :95
set(ENABLE_X11    OFF CACHE BOOL "" FORCE)        # :69
set(ENABLE_EGL    OFF CACHE BOOL "" FORCE)        # :72

# Audio — replaced by audren in M4
set(ENABLE_ALSA       OFF CACHE BOOL "" FORCE)    # :90
set(ENABLE_PULSEAUDIO OFF CACHE BOOL "" FORCE)    # :91

# Input — host backends not usable
set(ENABLE_SDL   OFF CACHE BOOL "" FORCE)         # :124
set(ENABLE_EVDEV OFF CACHE BOOL "" FORCE)         # :145
set(ENABLE_HWDB  OFF CACHE BOOL "" FORCE)         # :144

# Misc
set(ENABLE_LLVM        OFF CACHE BOOL "" FORCE)   # :93
set(USE_MGBA           OFF CACHE BOOL "" FORCE)   # :97  (depends on ENABLE_QT anyway)
set(ENCODE_FRAMEDUMPS  OFF CACHE BOOL "" FORCE)   # :109 (FFmpeg not available)
```

## 2. Upstream patches needed inside `dolphin/`

Three small patches (one concern each, per CLAUDE.md commit policy):

- **`patches/0001-build-libusb-skip-on-switch.patch`** — change
  `dolphin/CMakeLists.txt:703` from `if(NOT ANDROID)` to
  `if(NOT ANDROID AND NOT NINTENDO_SWITCH)` so libusb auto-detect is
  skipped.
- **`patches/0002-build-hidapi-skip-on-switch.patch`** — same fix at
  `dolphin/CMakeLists.txt:730` for HIDAPI.
- **`patches/0003-build-cli-tool-skip-on-switch.patch`** — gate the
  `ENABLE_CLI_TOOL` option declaration at `dolphin/CMakeLists.txt:76`
  with `if(NOT ANDROID AND NOT NINTENDO_SWITCH)`. Forcing the option
  OFF (above) is enough at runtime; the gate just keeps the option
  out of the help text.

Without 0001 and 0002, configure will warn but won't fail (the
`dolphin_find_optional_system_library_pkgconfig` helper falls back to
bundled). With them, the warnings go away and the configure log stays
clean.

## 3. `find_package` calls to expect-and-ignore

These will fail under devkitA64's sysroot. Most are already gated by an
option above; listed here so future grep-based surveys don't get
confused:

| package | dolphin/CMakeLists.txt | gate |
|---------|------------------------|------|
| OpenGL  | :497 | unconditional today; needs `if(NOT NINTENDO_SWITCH)` wrapper if it errors. Most likely it just doesn't find OpenGL and skips silently. |
| X11 / XRANDR / X11_INPUT | :503-510 | gated by `ENABLE_X11`; off |
| EGL | :518 | gated by `ENABLE_EGL`; off |
| LIBUDEV / LIBEVDEV | :557, :567, :568 | gated by `ENABLE_HWDB` / `ENABLE_EVDEV`; off |
| LibUSB | :703 | needs upstream patch (above) |
| HIDAPI | :731 | needs upstream patch (above) |
| FFmpeg | :535 | gated by `ENCODE_FRAMEDUMPS`; off |
| systemd | :747 | optional; warns but doesn't fail |

## 4. `add_subdirectory` exclusions

Inside `dolphin/Source/Core/CMakeLists.txt`:

| subdir | line | Switch action |
|--------|------|---------------|
| AudioCommon | 1 | keep — backend-agnostic; cubeb disabled |
| Common | 2 | keep — needs `__SWITCH__` arms in MemoryUtil/MemArena (M2, M2.5) |
| Core | 3 | keep |
| DiscIO | 4 | keep |
| InputCommon | 5 | keep — host backends already disabled by options |
| UICommon | 6 | keep — used by our frontend |
| VideoCommon | 7 | keep |
| VideoBackends | 8 | keep — only `Null` and `Software` build by default; `Vulkan` re-enabled in M3 |
| DolphinNoGUI | 11 | gated by `ENABLE_NOGUI`; recommend OFF for Switch (we have our own frontend) |
| DolphinTool | 15 | gated by `ENABLE_CLI_TOOL`; off |
| DolphinQt | 19 | gated by `ENABLE_QT`; off |
| UpdaterCommon | 23 | gated `if (APPLE OR WIN32)`; auto-skipped |
| MacUpdater | 27 | auto-skipped |
| WinUpdater | 31 | auto-skipped |

## 5. Externals — verdict per dependency

Most bundled libs are platform-portable and just work. Highlights:

- **`Externals/Bochs_disasm`** — gated by `_M_X86_64`; auto-skipped on
  ARM64. Good.
- **`Externals/MoltenVK`** — Apple-only; auto-skipped. Good.
- **`Externals/libadrenotools`** — Android-only; auto-skipped.
- **`Externals/discord-rpc`** — gated by `USE_DISCORD_PRESENCE`; off.
- **`Externals/HIDAPI`** — gated by `if(NOT ANDROID)`; needs the
  upstream patch above.
- **`Externals/cpp-ipc`** — guarded by `if(WIN32 OR LINUX OR FreeBSD OR
  QNX)`; auto-skipped on Switch.
- **`Externals/SFML`** — used for netplay server discovery. Bundled and
  portable, but heavy. Tag for "swap or skip" review during M5; not
  load-bearing for early milestones.
- **`Externals/FatFs`** — useful on Switch (SD card layouts). Keep.

Compression / crypto / format libs (`fmt`, `xxhash`, `zstd`, `zlib-ng`,
`minizip-ng`, `lz4`, `LZO`, `bzip2`, `liblzma`, `libspng`, `mbedtls`,
`curl`, `iconv`) — all bundled, all portable, all `keep`.

## 6. Pattern for the top-level `CMakeLists.txt` once M1 starts

```cmake
cmake_minimum_required(VERSION 3.20)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "NintendoSwitch")
    message(FATAL_ERROR "configure with $DEVKITPRO/cmake/Switch.cmake")
endif()

project(dolphin-switch
    VERSION 0.0.1
    DESCRIPTION "Dolphin GameCube/Wii emulator - native Nintendo Switch port"
    LANGUAGES C CXX)

# --- Override Dolphin options BEFORE add_subdirectory ---
set(NINTENDO_SWITCH ON CACHE BOOL "Building for Nintendo Switch" FORCE)
# (see section 1 above for the full list)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(dolphin)   # M1: enable this line
add_subdirectory(frontend)
```

## 7. Platform-conditional code — order of attack

Roughly 550 platform-conditional lines (`__APPLE__` / `_WIN32` /
`__linux__` / `__FreeBSD__` / `ANDROID`) in `dolphin/Source/Core/`.
Tackle in passes during M1:

1. **Pass 1** — make it link. Touch only files that fail to compile.
   Add minimal `#ifdef __SWITCH__` arms that stub or `static_assert(0)`
   so the linker doesn't pull in unimplemented platform code. Pursue
   the cleanest "compiles but doesn't run" build first.
2. **Pass 2** — `Common/FileUtil.cpp` + `Common/CommonPaths.cpp` for
   `sdmc:/switch/dolphin/`. ~50 lines.
3. **Pass 3** — `Common/MemoryUtil.cpp` + `Common/MemArena.cpp` JIT
   stubs. M2 fills these in.
4. **Pass 4** — `AudioCommon/` and `InputCommon/` backend selectors
   (M4 work, but stubs needed to link in M1).

`VideoCommon` / `VideoBackends/` get their own milestone (M3); during
M1 leave the `Null` and `Software` backends only — both are
platform-agnostic and should compile out of the box.

## 8. What we *don't* need to worry about

- **No `try_run`** in the root CMakeLists. Cross-compilation's main
  pitfall is absent.
- **macOS bundle signing** is `if (APPLE AND ENABLE_QT)` — never fires
  on Switch.
- **Git scmrev** runs at configure time on the build host — fine
  because git is a host tool.
- **No host-tool `add_custom_command`** beyond the macOS path above.
