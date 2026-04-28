# Custom Switch frontend.
# M0: hello-world NRO that prints over nxlink.
# Later milestones replace src/main.cpp with the ImGui-based frontend.

add_executable(dolphin-switch-frontend
    src/main.cpp
    src/switch_libc_shim.c
)

target_compile_definitions(dolphin-switch-frontend PRIVATE
    __SWITCH__=1
)

target_include_directories(dolphin-switch-frontend PRIVATE
    "${CMAKE_SOURCE_DIR}/dolphin/Source/Core"
)

# libnx is wired in by Switch.cmake's platform module via the toolchain.
# We still link it explicitly so a non-default link order does not bite us later.
#
# M1 link-validation: pull in the headless half of the Dolphin core so the
# frontend NRO actually contains dolphin code. The ImGui/UI piece lands in
# M5 — for now this just proves the libs link end-to-end into an NRO.
target_link_libraries(dolphin-switch-frontend PRIVATE
    uicommon
    core
    discio
    videocommon
    videoogl
    videonull
    videosoftware
    common
    nx
)

# Produce the .nro alongside the ELF. Default icon + auto-generated NACP for now.
nx_create_nro(dolphin-switch-frontend
    NACP "${CMAKE_CURRENT_BINARY_DIR}/dolphin-switch.nacp"
)

# Generate NACP with project metadata (overrides the default name/author).
nx_generate_nacp(
    OUTPUT  "${CMAKE_CURRENT_BINARY_DIR}/dolphin-switch.nacp"
    NAME    "Dolphin (Switch)"
    AUTHOR  "dolphin-switch contributors"
    VERSION "${PROJECT_VERSION}"
)
