#!/usr/bin/env bash
# Convenience wrapper around `docker compose run` for the Switch build.
#
# Usage:
#   scripts/docker-build.sh                # release
#   scripts/docker-build.sh release        # release (explicit)
#   scripts/docker-build.sh debug          # -O0 + symbols
#   scripts/docker-build.sh clean          # nuke build/ + rebuild release
#   scripts/docker-build.sh host           # host-side dolphin sanity build
#   scripts/docker-build.sh shell          # interactive shell in container
#
# Builds the image on first invocation (cached after that). Image tag:
# dolphin-switch:dev. Bind-mounts the repo so the produced NRO ends up
# at build/switch-release/frontend/dolphin-switch-frontend.nro on the
# host filesystem.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

# Pass UID/GID through so build artifacts are owned by the invoking user
# instead of root. Compose reads these from the env at run time.
export UID="${UID:-$(id -u)}"
export GID="${GID:-$(id -g)}"

target="${1:-release}"
case "$target" in
    release)  service=build ;;
    debug)    service=build-debug ;;
    clean)    service=build-clean ;;
    host)     service=host-build ;;
    shell)    service=shell ;;
    -h|--help)
        grep -E '^# ' "$0" | sed 's/^# \{0,1\}//'
        exit 0 ;;
    *)
        echo "unknown target: $target (try: release | debug | clean | host | shell)" >&2
        exit 2 ;;
esac

# Use `docker compose` (v2) when available, else fall back to docker-compose v1.
if docker compose version >/dev/null 2>&1; then
    exec docker compose run --rm "$service"
elif command -v docker-compose >/dev/null 2>&1; then
    exec docker-compose run --rm "$service"
else
    echo "neither 'docker compose' nor 'docker-compose' is installed" >&2
    exit 1
fi
