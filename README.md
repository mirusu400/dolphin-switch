# dolphin-switch

Native port of the **Dolphin** GameCube/Wii emulator to **Nintendo Switch**
homebrew (Horizon OS). Targets Atmosphère CFW, runs as a normal `.nro`
launched from `hbmenu`. No Linux layer, no Switchroot, no closed-source
dependencies — direct port using devkitPro/devkitA64 + libnx.

The roadmap and per-milestone done-criteria live in
[`CLAUDE.md`](CLAUDE.md). Session history is in
[`docs/milestones.md`](docs/milestones.md).

---

## Build — Docker (recommended)

Reproducible build env using devkitPro's official image. No host
toolchain install required besides Docker.

```sh
git clone --recurse-submodules <this-repo>
cd dolphin-switch
./scripts/docker-build.sh           # release NRO
./scripts/docker-build.sh debug     # -O0 + symbols
./scripts/docker-build.sh clean     # nuke build/ then rebuild
./scripts/docker-build.sh shell     # interactive shell in container
```

Output:
```
build/switch-release/frontend/dolphin-switch-frontend.nro
```

The container bind-mounts the repo and runs as your UID, so artifacts
end up owned by you on the host filesystem.

---

## Build — host devkitPro

If you already have devkitPro installed:

```sh
# One-time toolchain check
./scripts/setup-toolchain.sh

# Install Switch portlibs (if not already)
sudo dkp-pacman -Sy switch-mesa switch-sdl2 switch-zlib switch-libpng switch-pkg-config

# Build
./scripts/build.sh                  # release
./scripts/build.sh --debug          # -O0 + symbols
./scripts/build.sh --clean          # nuke build/ then rebuild
```

Required: `cmake ≥ 3.20`, `ninja`, `git`, devkitPro with `devkitA64`,
`libnx`, and the portlibs listed above.

---

## Run on hardware

You need a Switch running Atmosphère CFW.

```sh
# 1. Push the NRO to a Switch on the LAN running `nxlink -s`
./scripts/deploy-to-switch.sh <switch-ip>

# 2. Or: copy build/.../dolphin-switch-frontend.nro to
#    sdmc:/switch/dolphin-switch.nro and launch from hbmenu
```

When the NRO boots, the debug log streams to:
- `nxlink -s` on your PC (live)
- `sdmc:/switch/dolphin/logs/dolphin-switch-YYYYMMDD-HHMMSS.log`
- An in-app ImGui panel on the Switch screen

ROMs go in `sdmc:/roms/` (extensions: `.iso .gcm .ciso .gcz .rvz .wbfs .wad .dol .elf`).

---

## License

GPLv2-or-later, matching upstream Dolphin. See
[`dolphin/license.txt`](dolphin/license.txt).
