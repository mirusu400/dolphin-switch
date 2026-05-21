#!/usr/bin/env bash
# Initialize Dolphin submodules for this Switch port.
#
# A plain `git submodule update --init --recursive` fails because the
# dolphin submodule points at two local nested-submodule commits
# (SFML/curl) that are not present in the upstream nested remotes.
# This script checks out the upstream base commits and reapplies the
# tiny Switch-only source edits locally.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DOLPHIN_DIR="$REPO_ROOT/dolphin"

detect_jobs() {
    if command -v nproc >/dev/null 2>&1; then
        nproc
    elif command -v getconf >/dev/null 2>&1; then
        getconf _NPROCESSORS_ONLN
    elif command -v sysctl >/dev/null 2>&1; then
        sysctl -n hw.ncpu
    else
        echo 4
    fi
}

git -C "$REPO_ROOT" submodule update --init dolphin reference/xerpi-dolphin-switch

git -C "$DOLPHIN_DIR" submodule update --init --recursive --jobs "$(detect_jobs)" -- \
    ':(exclude)Externals/SFML/SFML' \
    ':(exclude)Externals/curl/curl'

SFML_DIR="$DOLPHIN_DIR/Externals/SFML/SFML"
CURL_DIR="$DOLPHIN_DIR/Externals/curl/curl"

ensure_nested_repo() {
    local sub_path="$1"
    local repo_path="$DOLPHIN_DIR/$sub_path"
    local url
    url="$(git -C "$DOLPHIN_DIR" config --file .gitmodules --get "submodule.$sub_path.url")"

    git -C "$DOLPHIN_DIR" submodule init "$sub_path" >/dev/null
    if [ ! -e "$repo_path/.git" ]; then
        rmdir "$repo_path" 2>/dev/null || true
        git clone "$url" "$repo_path"
    fi
}

ensure_nested_repo Externals/SFML/SFML
ensure_nested_repo Externals/curl/curl

git -C "$SFML_DIR" fetch origin 0fa201c969e48ecc253581c5841ce73f44d42f49
git -C "$SFML_DIR" checkout 0fa201c969e48ecc253581c5841ce73f44d42f49
if ! grep -q '__SWITCH__' "$SFML_DIR/include/SFML/Config.hpp"; then
    perl -0pi -e 's/#elif defined\(__linux__\)\n\n\/\/ Linux\n#define SFML_SYSTEM_LINUX/#elif defined(__linux__) || defined(__SWITCH__)\n\n\/\/ Linux \/ Nintendo Switch libnx BSD-socket environment\n#define SFML_SYSTEM_LINUX/' \
        "$SFML_DIR/include/SFML/Config.hpp"
fi
perl -0pi -e 's/#elif defined\(__unix__\)/#elif defined(__unix__) || defined(__SWITCH__)/' \
    "$SFML_DIR/include/SFML/Config.hpp"

git -C "$CURL_DIR" fetch origin cfbfb65047e85e6b08af65fe9cdbcf68e9ad496a
git -C "$CURL_DIR" checkout cfbfb65047e85e6b08af65fe9cdbcf68e9ad496a
if ! grep -q '__SWITCH__' "$CURL_DIR/lib/socketpair.h"; then
    perl -0pi -e 's/#if defined\(USE_UNIX_SOCKETS\) && defined\(HAVE_SOCKETPAIR\)\n#define SOCKETPAIR_FAMILY AF_UNIX\n#elif !defined\(HAVE_SOCKETPAIR\)/#if defined(USE_UNIX_SOCKETS) \&\& defined(HAVE_SOCKETPAIR)\n#define SOCKETPAIR_FAMILY AF_UNIX\n#elif defined(__SWITCH__)\n#define SOCKETPAIR_FAMILY 0 \/* disabled by CURL_DISABLE_SOCKETPAIR *\/\n#elif !defined(HAVE_SOCKETPAIR)/' \
        "$CURL_DIR/lib/socketpair.h"
fi

echo "Submodules initialized. SFML and curl carry local Switch edits by design."
