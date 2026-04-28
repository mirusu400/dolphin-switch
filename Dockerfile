# Reproducible build environment for the Dolphin → Switch port.
#
# Uses devkitPro's official devkitA64 image. Adds the portlibs we
# depend on (mesa GLES, SDL2, zlib via system pkg-config). Runs the
# project's build.sh inside the container.
#
# Usage:
#   docker build -t dolphin-switch:dev .
#   docker run --rm -v "$PWD":/workspace -u "$(id -u):$(id -g)" dolphin-switch:dev
#
# Or just `docker compose run --rm build` (see docker-compose.yml).
#
# Pinned image tag — bump deliberately. Track upstream releases at
#   https://hub.docker.com/r/devkitpro/devkita64/tags

FROM devkitpro/devkita64:latest

# Project-side build deps that ship inside portlibs/switch.
# `switch-mesa` brings libEGL / libGLESv2 / libdrm_nouveau.
# `switch-sdl2` brings libSDL2 (depends on mesa + libnx).
# `switch-zlib` is required by Dolphin's HashCommon and FatFs paths.
# `switch-pkg-config` is the cross-aware pkg-config wrapper our CMake uses.
RUN dkp-pacman -Sy --noconfirm \
        switch-mesa \
        switch-sdl2 \
        switch-zlib \
        switch-libpng \
        switch-pkg-config \
    && dkp-pacman -Scc --noconfirm

# Host-side build deps (cmake / ninja / git already present in the base
# image, but install explicitly so the build does not regress on
# upstream image rebuilds).
RUN apt-get update && apt-get install -y --no-install-recommends \
        cmake \
        ninja-build \
        git \
        ca-certificates \
        pkg-config \
    && rm -rf /var/lib/apt/lists/*

# Make sure DEVKITPRO is exported for non-login shells. The base image
# already does this, but compose `run --rm` sometimes spawns a shell that
# doesn't source /etc/profile.d/devkit-env.sh.
ENV DEVKITPRO=/opt/devkitpro
ENV DEVKITARM=/opt/devkitpro/devkitARM
ENV DEVKITPPC=/opt/devkitpro/devkitPPC
ENV PATH=/opt/devkitpro/tools/bin:/opt/devkitpro/devkitA64/bin:${PATH}

WORKDIR /workspace

# Default action: run the project build. Override with `docker run … bash`
# to drop into an interactive shell.
CMD ["bash", "scripts/build.sh"]
