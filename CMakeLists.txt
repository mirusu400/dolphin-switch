cmake_minimum_required(VERSION 3.20)

# Toolchain (Switch.cmake) is expected to be passed via -DCMAKE_TOOLCHAIN_FILE.
# scripts/build.sh wires this up.

project(dolphin-switch
    VERSION 0.0.1
    DESCRIPTION "Dolphin GameCube/Wii emulator - native Nintendo Switch (Horizon OS) port"
    LANGUAGES C CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "NintendoSwitch")
    message(FATAL_ERROR
        "This project only cross-compiles for Nintendo Switch (Horizon OS).\n"
        "Configure with: -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/Switch.cmake\n"
        "Use scripts/build.sh which sets this automatically.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ----------------------------------------------------------------------------
# Override Dolphin build options BEFORE add_subdirectory(dolphin).
# Per-line citations into upstream dolphin/CMakeLists.txt are kept for
# the next time the submodule is rebased — see docs/m1-cmake-prep.md §1.
# ----------------------------------------------------------------------------

set(NINTENDO_SWITCH ON CACHE BOOL "Building for Nintendo Switch" FORCE)

# UI / packaging
set(ENABLE_QT          OFF CACHE BOOL "" FORCE)   # dolphin/CMakeLists.txt:86  - Qt6 not available
set(ENABLE_TESTS       OFF CACHE BOOL "" FORCE)   # :94
set(ENABLE_AUTOUPDATE  OFF CACHE BOOL "" FORCE)   # :98
set(ENABLE_CLI_TOOL    OFF CACHE BOOL "" FORCE)   # :76 (declaration also gated by patches/0003)
set(ENABLE_NOGUI       OFF CACHE BOOL "" FORCE)   # frontend/ replaces DolphinNoGUI

# Networking / presence
set(USE_DISCORD_PRESENCE OFF CACHE BOOL "" FORCE) # :96
set(USE_UPNP             OFF CACHE BOOL "" FORCE) # :84
set(USE_SHARED_ENET      OFF CACHE BOOL "" FORCE) # use bundled enet

# Graphics — Vulkan removed in M3 (mesa Switch ships GLES only).
set(ENABLE_VULKAN OFF CACHE BOOL "" FORCE)        # :95
set(ENABLE_X11    OFF CACHE BOOL "" FORCE)        # :69
set(ENABLE_EGL    OFF CACHE BOOL "" FORCE)        # :72  (M3 wires libnx EGL via custom GLContextSwitch)

# Audio — replaced by audren in M4
set(ENABLE_ALSA       OFF CACHE BOOL "" FORCE)    # :90
set(ENABLE_PULSEAUDIO OFF CACHE BOOL "" FORCE)    # :91
set(ENABLE_CUBEB      OFF CACHE BOOL "" FORCE)    # :92  (audren replaces it)

# Input — host backends not usable
set(ENABLE_SDL   OFF CACHE BOOL "" FORCE)         # :124
set(ENABLE_EVDEV OFF CACHE BOOL "" FORCE)         # :145
set(ENABLE_HWDB  OFF CACHE BOOL "" FORCE)         # :144

# Misc
set(ENABLE_LLVM        OFF CACHE BOOL "" FORCE)   # :93
set(USE_MGBA           OFF CACHE BOOL "" FORCE)   # :97  (depends on ENABLE_QT anyway)
set(ENCODE_FRAMEDUMPS  OFF CACHE BOOL "" FORCE)   # :109 (FFmpeg not available)

# Externals/curl: devkitA64 has no system OpenSSL, and curl's bundled
# FindMbedTLS.cmake (Externals/curl/curl/CMake/FindMbedTLS.cmake:94) does
# not auto-locate Dolphin's bundled Externals/mbedtls (different trees).
# For M1 link-only purposes, build curl with no TLS backend. HTTPS paths
# inside Dolphin (RetroAchievements, AutoUpdate) are already gated OFF
# above; what remains is plain-HTTP/local I/O. Re-enable a TLS backend
# in M5+ once HTTPS code paths are exercised end-to-end.
set(CURL_USE_OPENSSL    OFF CACHE BOOL "" FORCE)
set(CURL_USE_MBEDTLS    OFF CACHE BOOL "" FORCE)
set(CURL_USE_GNUTLS     OFF CACHE BOOL "" FORCE)
set(CURL_USE_WOLFSSL    OFF CACHE BOOL "" FORCE)
set(CURL_USE_BEARSSL    OFF CACHE BOOL "" FORCE)
set(CURL_USE_RUSTLS     OFF CACHE BOOL "" FORCE)
set(CURL_USE_LIBSSH2    OFF CACHE BOOL "" FORCE)
set(CURL_USE_LIBSSH     OFF CACHE BOOL "" FORCE)
set(CURL_DISABLE_LDAP        ON  CACHE BOOL "" FORCE)
set(CURL_DISABLE_LDAPS       ON  CACHE BOOL "" FORCE)
# devkitA64 has socketpair() but no AF_UNIX support — that combo
# triggers curl/lib/socketpair.h:60 #error. Disable the feature.
set(CURL_DISABLE_SOCKETPAIR  ON  CACHE BOOL "" FORCE)
set(USE_NGHTTP2         OFF CACHE BOOL "" FORCE)
set(USE_LIBIDN2         OFF CACHE BOOL "" FORCE)
set(BUILD_CURL_EXE      OFF CACHE BOOL "" FORCE)

# Externals/mbedtls: entropy_poll.c #errors out under Switch because
# devkitA64 is neither Unix nor Windows from mbedtls's POV. Disable
# platform entropy via the documented switch — Dolphin's own callers
# pass mbedtls a deterministic seed for the paths it actually
# exercises (WiiSave + http signing). M5+ wires libnx randomGet for a
# proper entropy source.
add_compile_definitions(MBEDTLS_NO_PLATFORM_ENTROPY)

# devkitA64's newlib gates pthread_setname_np, pthread_getattr_np,
# and vasprintf behind __GNU_VISIBLE (sys/features.h). _GNU_SOURCE
# turns __GNU_VISIBLE on, exposing the BSD/GNU extensions Dolphin's
# Common module relies on. Same effect Linux/glibc gets implicitly.
add_compile_definitions(_GNU_SOURCE)

# ----------------------------------------------------------------------------
# Subprojects
# ----------------------------------------------------------------------------

add_subdirectory(dolphin)
add_subdirectory(frontend)
