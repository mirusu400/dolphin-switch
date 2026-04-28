#!/usr/bin/env bash
# Push the built NRO to a Switch running hbmenu via nxlink.
#
# Usage:
#   scripts/deploy-to-switch.sh <switch-ip>
#
# The Switch must be on hbmenu with nxlink listening (Y button enables it).

set -euo pipefail

if [ "$#" -ne 1 ]; then
    echo "usage: $0 <switch-ip>" >&2
    exit 2
fi

SWITCH_IP="$1"
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"

: "${DEVKITPRO:?DEVKITPRO is unset}"
NXLINK="$DEVKITPRO/tools/bin/nxlink"
[ -x "$NXLINK" ] || { echo "nxlink missing at $NXLINK"; exit 1; }

NRO=$(find "$REPO_ROOT/build" -name 'dolphin-switch-frontend.nro' -print -quit 2>/dev/null || true)
if [ -z "$NRO" ]; then
    echo "no NRO found - run scripts/build.sh first" >&2
    exit 1
fi

echo "deploying $NRO -> $SWITCH_IP"
exec "$NXLINK" -a "$SWITCH_IP" -s "$NRO"
