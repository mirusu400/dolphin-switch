# M3 graphics — Vulkan is not available; use OpenGL ES

**Important finding from the M3 prep recon — read before starting M3.**
The original CLAUDE.md plan was "mesa Vulkan via libnx WSI - start
here." Reality, verified locally and against devkitPro's package
listing, is that **devkitPro's mesa Switch port does not provide
Vulkan**. The project must use OpenGL ES on Switch — either by reusing
Dolphin's existing OGL backend, or by writing a new deko3d backend
later. Vulkan is off the table until either (a) someone publishes a
Vulkan WSI in mesa-switch or (b) we write one ourselves, which is its
own multi-month project and out of scope.

## Evidence

Local devkitPro install (`/opt/devkitpro/`):

- `dkp-pacman -Ql switch-mesa` → ships `EGL/`, `GL/`, `GLES/`,
  `GLES2/`, `GLES3/`, `KHR/` headers only. **No `vulkan/` directory,
  no `vulkan.h`, no `libvulkan.a`.** `switch-mesa` package version is
  `20.1.0-5` (mesa 20.1, ~2020 vintage).
- `find /opt/devkitpro -name 'vulkan*' -o -name 'libvulkan*'` → only
  hit is `portlibs/switch/include/SDL2/SDL_vulkan.h`, which is
  SDL's *surface helper* and is dead code without a Vulkan
  implementation underneath.
- `/opt/devkitpro/portlibs/switch/lib/pkgconfig/` lists `egl.pc`,
  `glapi.pc`, `glesv1_cm.pc`, `glesv2.pc`, `libdrm_nouveau.pc` — and
  no Vulkan `.pc`.
- `/opt/devkitpro/examples/switch/graphics/` directories: `deko3d/`,
  `opengl/`, `printing/`, `sdl2/`, `shared_font/`, `simplegfx/`,
  `simplegfx_moviemaker/`. **No `vulkan/` example.** That's
  consistent with no Vulkan support.
- `dkp-pacman -Ss vulkan` returns one match: `switch-glfw`, whose
  description claims Vulkan support — but glfw will only succeed if
  the underlying loader exists, which it doesn't.

Upstream devkitPro/mesa repo (verified via WebFetch):

- `src/vulkan/wsi/` has only `wsi_common_x11.c`,
  `wsi_common_wayland.c`, `wsi_common_display.c`. No Switch backend.
- `src/gallium/winsys/` has `tegra/drm`, `nouveau/drm`, etc., but no
  `nx`/`switch`/`libnx` entry.
- GitHub code search ("nwindow", "nn::vi") behind auth wall, so we
  cannot rule out a private branch — but the absence in the master
  tree on a 2020-vintage fork is telling.

The `VK_NN_vi_surface` extension is real — it lives in Khronos's
official Vulkan-Headers (`vulkan_vi.h`) and would be the right
extension for Switch's VI display. But mesa-switch does not implement
it. Calling `vkCreateInstance` with `VK_NN_VI_SURFACE_EXTENSION_NAME`
on Switch will return `VK_ERROR_EXTENSION_NOT_PRESENT` — there is no
loader to satisfy it.

## What this changes

CLAUDE.md must be amended in two places:

1. **Tech stack table** — "mesa Vulkan (via libnx) - start here" is
   wrong. The reality is "mesa OpenGL ES (via libnx EGL/native) - start
   here. May migrate to deko3d later."
2. **M3 done criterion** — "A test ROM reaches the first rendered
   frame on the Switch display" still applies, but the path to that
   frame is GLES, not Vulkan.

## Revised M3 plan: Dolphin's OGL backend on GLES

Dolphin's `dolphin/Source/Core/VideoBackends/OGL/` backend already
supports GLES. Confirmed via grep on `IsGLES()` — at least 9 call sites
in `OGLConfig.cpp` alone, with explicit handling of GLES 3.0/3.1/3.2
extension differences (e.g. `VERSION_GLES_3_2` checks at
`OGLConfig.cpp:383`). The Android port uses this backend exclusively.

The work to land M3 with GLES:

1. **WSI / surface creation.** Dolphin's OGL backend uses a `GLContext`
   abstraction (`dolphin/Source/Core/Common/GL/GLContext.{h,cpp}` and
   subclasses). Existing subclasses: `GLContextEGL`, `GLContextGLX`,
   `GLContextWGL`, `GLContextAGL`. Switch needs a new subclass —
   probably `GLContextSwitch` — that uses `nwindowGetDefault()` from
   libnx as the EGL `NativeWindowType`. Mesa Switch's EGL is the
   client; libnx's NWindow is the server.
   - Look at switch-sdl2's `SDL_video_switch.c` (in
     `/opt/devkitpro/portlibs/switch/lib/libSDL2.a` source if
     available) for prior art on EGL-on-NWindow.
   - `/opt/devkitpro/examples/switch/graphics/opengl/` likely contains
     a working EGL+OpenGL init we can crib from. Read this first
     during M3.
2. **Backend selection.** Force `Vulkan` and `D3D*` and `Metal` off,
   keep `OGL` and `Null` and `Software`. CMake gating already covered
   in `docs/m1-cmake-prep.md` (set `ENABLE_VULKAN OFF`); we'll need
   the equivalent for the OGL build (it auto-enables when GLES
   headers are found, but verify).
3. **GLES profile probe.** Tegra X1 supports OpenGL ES 3.2 via
   nouveau. Confirm during M3 startup with `glGetString(GL_VERSION)`
   logged via nxlink.
4. **Performance.** mesa-switch is 2020-vintage and the nouveau driver
   is reverse-engineered, not Nvidia-blessed. Expect 30 fps targets
   to be tight. M6 may need to revisit deko3d if GLES proves too
   slow. Don't over-optimize until M6 has data.

## What about deko3d

deko3d is devkitPro's open-source 3D graphics abstraction layer for
Switch. It targets the Tegra X1 GPU directly and is the path Atmosphère
homebrew typically uses for high-performance 3D. It is well-maintained
and ships with examples.

Why we still don't start with deko3d:

- It has its own API. Adding a deko3d backend to Dolphin means writing
  a new `VideoBackends/Deko3D/` from scratch — comparable in scope to
  the existing `VideoBackends/Vulkan/` module. That's 3-6 months of
  M3-equivalent work.
- The OGL backend already exists and already works on GLES. It is
  effectively "free" for M3.
- deko3d remains the right answer for M6+ if perf demands it. Plan
  for the migration but don't pay for it up front.

## Open questions for M3 day-1

- ~~Does `/opt/devkitpro/examples/switch/graphics/opengl/` exist?~~
  Confirmed. The reference EGL+NWindow init pattern is in
  `/opt/devkitpro/examples/switch/graphics/opengl/simple_triangle/source/main.cpp`
  (`initEgl(NWindow* win)` function). Pattern:
  `eglGetDisplay(EGL_DEFAULT_DISPLAY)` → `eglInitialize` →
  `eglBindAPI(EGL_OPENGL_API)` → `eglChooseConfig` →
  `eglCreateWindowSurface(display, config, win, nullptr)` where `win`
  is from `nwindowGetDefault()`. For Dolphin's GLES path, swap
  `EGL_OPENGL_API` for `EGL_OPENGL_ES_API` and request a GLES 3.x
  context. Other examples in the same tree:
  `simple_triangle`, `textured_cube`, `dynamic_resolution`,
  `es2gears`, `gpu_console`, `lenny`. `es2gears` confirms GLES 2 path
  works.
- What is the exact GLES profile we get? Mesa/nouveau on Tegra X1
  publicly claims GLES 3.2; confirm by running a tiny probe NRO once
  hardware is available.
- Does Dolphin's `GLContextEGL` work as-is when given the libnx
  NativeWindow, or do we need a Switch-specific subclass? Likely the
  latter — the EGL display setup differs.
- Mesa-switch's age (20.1, ~2020). Are there known graphics-pipeline
  bugs that affect Dolphin's heavy fragment shaders? Search the
  devkitPro mesa issue tracker during M3 spike.

## Action items now (before M3)

- **Update CLAUDE.md** — tech-stack row, M3 plan. Done in the same
  patch series as this doc.
- **Read** `/opt/devkitpro/examples/switch/graphics/opengl/source/`
  end-to-end to extract the EGL+NWindow init pattern. Pin the
  reference here when done.
- **Do not** try to enable `ENABLE_VULKAN` during M1. It will
  configure cleanly (since we force it OFF), but if we later flip it
  on the build will fail at link time with no `libvulkan.a`. Keeping
  it OFF saves a confusing dead-end.
