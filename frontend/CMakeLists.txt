# Custom Switch frontend (M5-prep skeleton).
# ImGui + switch-sdl2 + libnx EGL/GLES2 stack. Hello-world fallback path
# kept under nxlink for early-boot debug output.

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
pkg_check_modules(GLESV2 REQUIRED IMPORTED_TARGET glesv2)

set(IMGUI_BACKENDS "${CMAKE_SOURCE_DIR}/dolphin/Externals/imgui/imgui/backends")

add_executable(dolphin-switch-frontend
    src/main.cpp
    src/switch_libc_shim.c
    src/debug_log.cpp
    "${IMGUI_BACKENDS}/imgui_impl_sdl2.cpp"
    "${IMGUI_BACKENDS}/imgui_impl_opengl3.cpp"
)

target_compile_definitions(dolphin-switch-frontend PRIVATE
    __SWITCH__=1
    _M_ARM_64=1
    IMGUI_IMPL_OPENGL_ES3=1
)

# Dolphin headers (e.g. Common/StringUtil.h) use std::to_underlying which is
# C++23 only. Frontend defaults to C++20 from the top-level project; bump it
# locally for this target.
set_target_properties(dolphin-switch-frontend PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)

target_include_directories(dolphin-switch-frontend PRIVATE
    "${CMAKE_SOURCE_DIR}/dolphin/Source/Core"
    "${CMAKE_SOURCE_DIR}/dolphin/Externals/imgui"
    "${CMAKE_SOURCE_DIR}/dolphin/Externals/imgui/imgui"
    "${IMGUI_BACKENDS}"
)

# libnx is wired in by Switch.cmake's platform module via the toolchain.
# Dolphin libs go first so the linker picks symbols in the right order;
# SDL2 / GLES come from devkitPro portlibs.
target_link_libraries(dolphin-switch-frontend PRIVATE
    uicommon
    core
    discio
    videocommon
    videoogl
    videonull
    videosoftware
    common
    imgui
    PkgConfig::SDL2
    PkgConfig::GLESV2
    nx
)

set(DOLPHIN_SWITCH_ROMFS_DIR "${CMAKE_CURRENT_BINARY_DIR}/romfs")
file(GLOB_RECURSE DOLPHIN_SWITCH_SYS_FILES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/dolphin/Data/Sys/*"
)
dkp_add_asset_target(dolphin-switch-romfs "${DOLPHIN_SWITCH_ROMFS_DIR}")
add_custom_command(
    OUTPUT "${DOLPHIN_SWITCH_ROMFS_DIR}/.sys-stamp"
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${DOLPHIN_SWITCH_ROMFS_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${DOLPHIN_SWITCH_ROMFS_DIR}/Sys"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/dolphin/Data/Sys"
            "${DOLPHIN_SWITCH_ROMFS_DIR}/Sys"
    COMMAND ${CMAKE_COMMAND} -E touch "${DOLPHIN_SWITCH_ROMFS_DIR}/.sys-stamp"
    DEPENDS ${DOLPHIN_SWITCH_SYS_FILES}
    COMMENT "Staging Dolphin Sys files for NRO romfs"
    VERBATIM
)
add_custom_target(dolphin-switch-romfs-stage
    DEPENDS "${DOLPHIN_SWITCH_ROMFS_DIR}/.sys-stamp"
)
add_dependencies(dolphin-switch-romfs dolphin-switch-romfs-stage)
add_dependencies(dolphin-switch-frontend dolphin-switch-romfs)

# Produce the .nro alongside the ELF. The romfs contains Dolphin's read-only
# Sys tree at romfs:/Sys/ so boot code can find GameINI, fonts, codehandler,
# and other compatibility resources without writing them to sdmc:.
nx_create_nro(dolphin-switch-frontend
    NACP "${CMAKE_CURRENT_BINARY_DIR}/dolphin-switch.nacp"
    ROMFS dolphin-switch-romfs
)

# Generate NACP with project metadata (overrides the default name/author).
nx_generate_nacp(
    OUTPUT  "${CMAKE_CURRENT_BINARY_DIR}/dolphin-switch.nacp"
    NAME    "Dolphin (Switch)"
    AUTHOR  "dolphin-switch contributors"
    VERSION "${PROJECT_VERSION}"
)
