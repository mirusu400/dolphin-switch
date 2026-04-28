# Nested-submodule patches

Only patches against **nested** submodules live here. The main
`dolphin/` submodule's branch (`switch-build-prep`, 27 commits) is
pushed to the user fork at `https://github.com/mirusu400/dolphin`,
so the dolphin-side changes do not need to be carried as patch
files — a fresh clone follows the submodule pointer to that branch
directly.

The two patches that remain are for submodules nested **inside**
`dolphin/Externals/`. Their upstream remotes (`SFML/SFML`,
`curl/curl`) are not pushable from this project, so the only way
to record the changes for re-clone is as `git format-patch` files.

## Files

1. `0008-switch-Externals-SFML-bump-nested-submodule-for-Swit.patch`
   — `dolphin/Externals/SFML/SFML/include/SFML/Config.hpp` adds a
   `__SWITCH__` arm aliasing to `SFML_SYSTEM_LINUX` in the OS
   detection cascade.
2. `0010-switch-Externals-curl-bump-nested-submodule-for-sock.patch`
   — `dolphin/Externals/curl/curl/lib/socketpair.h` adds a
   `__SWITCH__` arm before the `#error` so curl compiles when
   `HAVE_SOCKETPAIR` is set without `USE_UNIX_SOCKETS`.

## Re-applying after a fresh clone

If the parent repo's `dolphin` submodule is recursively initialized
and the nested SFML / curl submodules show no `switch-build-prep`
branch (because they pull from upstream by default):

```sh
cd dolphin/Externals/SFML/SFML
git checkout -b switch-build-prep
git am ../../../../patches/0008-*.patch

cd ../../curl/curl
git checkout -b switch-build-prep
git am ../../../../patches/0010-*.patch
```

The dolphin parent submodule's pointer commits then resolve the
nested submodule pointers correctly.

## Regeneration

If either nested submodule's branch gets re-rolled:

```sh
git -C dolphin/Externals/SFML/SFML format-patch -1 \
    -o ../../../../patches/

git -C dolphin/Externals/curl/curl format-patch -1 \
    -o ../../../../patches/
```

Numbering preserves the original 0008 / 0010 slots so cross-references
in `docs/milestones.md` stay valid. Never hand-edit the patch files.

## Long-term plan

Forking `SFML/SFML` and `curl/curl` on GitHub and updating the
nested submodule URLs in `dolphin/.gitmodules` would let those
branches ship via push and remove the need for these patch files.
Until then, this directory is the canonical record.
