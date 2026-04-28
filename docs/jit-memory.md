# JIT memory on Horizon OS

Notes for the M2 milestone (replace Dolphin's JIT memory allocator with libnx
`jit_t`). Every claim cites a file:line. Speculative claims live in the
"Open questions" section at the bottom — never silently assumed.

## libnx `jit_t` API

Header: `/opt/devkitpro/libnx/include/switch/kernel/jit.h`. Implementation
inspected upstream at `switchbrew/libnx:nx/source/kernel/jit.c` (libnx 4.x).

```c
Result  jitCreate(Jit* j, size_t size);                  // jit.h:37
Result  jitTransitionToWritable(Jit* j);                 // jit.h:44
Result  jitTransitionToExecutable(Jit* j);               // jit.h:51
Result  jitClose(Jit* j);                                // jit.h:58
void*   jitGetRwAddr(Jit* j);                            // jit.h:65 (constexpr)
void*   jitGetRxAddr(Jit* j);                            // jit.h:74 (constexpr)
```

`Jit` struct (jit.h:18-29) holds two virtual aliases of one physical region:

- `rw_addr`: writable view. Recompiler stores opcodes here.
- `rx_addr`: executable view. Dispatcher branches here.

**These are distinct virtual addresses** in both backends (libnx jit.c:
`virtmemFindCodeMemory(size, 0x1000)` is called separately for `rx_addr`,
and for `JitType_CodeMemory` also for `rw_addr`). Alignment is 4 KB
(`0x1000`), **not** 2 MB. The 2 MB granule applies to `svcSetHeapSize`,
not to `jit_t`.

Consequence for the JitArm64 emitter: relative branch math must use
`rx_addr` as the program-counter origin. Writes go to `rw_addr`. The two
addresses differ by an unpredictable offset (ASLR), so the existing emitter
cannot assume "PC = write address".

### Lifecycle

1. `jitCreate(&j, size)` — allocate, pick backend, map both aliases.
   Initial state: rw writable, rx executable.
2. `jitTransitionToWritable(&j)` — make rw writable; rx becomes inaccessible
   in the `JitType_SetProcessMemoryPermission` backend, R-X in the
   `JitType_CodeMemory` backend.
3. write recompiled instructions into `rw_addr`.
4. `jitTransitionToExecutable(&j)` — flush dcache on rw, invalidate icache
   on rx, lock permissions. **libnx flushes for us** — confirmed in upstream
   `jit.c`: `armDCacheFlush(rw_addr, size); armICacheInvalidate(rx_addr, size);`
   are called inside the transition. Calling the existing
   `ARM64XEmitter::FlushIcache()` again is harmless but redundant for the
   freshly-emitted region.
5. `jitClose(&j)` — unmap aliases, free backing handle.

### Backend selection (jit.h:11-15, libnx jit.c)

| Type                                 | SVCs                                                               | Available  |
|--------------------------------------|--------------------------------------------------------------------|------------|
| `JitType_SetProcessMemoryPermission` | `svcMapProcessCodeMemory` / `svcSetProcessMemoryPermission` / `svcUnmapProcessCodeMemory` | 1.0.0+     |
| `JitType_CodeMemory`                 | `svcCreateCodeMemory` / `svcControlCodeMemory` (MapOwner/MapSlave/UnmapOwner/UnmapSlave) / `svcCloseHandle` | 4.0.0+ (preferred) |

`jitCreate` auto-selects. On any modern Atmosphère firmware (≥ 4.0.0) we
will land on `JitType_CodeMemory`. We do not pin a specific backend.

## Where this plugs into Dolphin

Allocator and W^X scope-guard scaffolding already exist upstream — Apple
Silicon paved the way. The cleanest swap is to add a `__SWITCH__` arm to
the existing abstraction, **not** invent a parallel allocator.

### Single 256 MB code buffer

`dolphin/Source/Core/Core/PowerPC/JitArm64/Jit.cpp:43-49`:

```
NEAR_CODE_SIZE  = 64 MB
FAR_CODE_SIZE   = 64 MB
TOTAL_CODE_SIZE = 256 MB   // (NEAR*2 + FAR*2 — two JitArm64 instances)
```

Allocated in one shot: `Jit.cpp:69` via `AllocCodeSpace(TOTAL_CODE_SIZE)`,
which dispatches to `Common::AllocateExecutableMemory` at
`dolphin/Source/Core/Common/MemoryUtil.cpp:36-54`. On Linux/macOS today
this is `mmap(... PROT_READ|PROT_WRITE|PROT_EXEC, MAP_ANON|MAP_PRIVATE)`.
On Windows it's `VirtualAlloc(..., PAGE_EXECUTE_READWRITE)`. Neither will
work on Horizon OS — RWX pages are forbidden.

### W^X scope guard (Apple Silicon precedent)

The abstraction we extend lives at
`dolphin/Source/Core/Common/MemoryUtil.h:19-27`:

```cpp
struct ScopedJITPageWriteAndNoExecute {
  ScopedJITPageWriteAndNoExecute()  { JITPageWriteEnableExecuteDisable(); }
  ~ScopedJITPageWriteAndNoExecute() { JITPageWriteDisableExecuteEnable(); }
};
```

The macOS-ARM64 implementation at `MemoryUtil.cpp:94-121` uses
`pthread_jit_write_protect_np`. On every other platform today the two
calls are no-ops (RWX assumed). Existing call sites in JitArm64 already
wrap every emit / patch / clear:

- `Jit.cpp:175`   `JitArm64::ClearCache`
- `Jit.cpp:989`   block emit
- `JitArm64Cache.cpp:86`  `WriteLinkBlock`
- `JitArm64Cache.cpp:98`  `WriteDestroyBlock`
- `JitArm64_BackPatch.cpp:346`  backpatch handler

If we plumb `__SWITCH__` arms through `JITPageWriteEnableExecuteDisable` /
`JITPageWriteDisableExecuteEnable`, **none of the call sites need to
change**.

### icache flush sites (currently always called)

`ARM64XEmitter::FlushIcache` / `FlushIcacheSection` in
`dolphin/Source/Core/Common/Arm64Emitter.cpp:125-171` are called from at
least:

- `JitArm64/Jit.cpp:1453,1454`  block + far-code emit
- `JitArm64/JitArm64Cache.cpp:91,100`  link / destroy patches
- `JitArm64/JitArm64_BackPatch.cpp:356`  backpatch
- `JitArm64/JitAsm.cpp`  common asm
- `VideoCommon/VertexLoaderARM64.cpp`  vertex JIT

libnx already flushes inside `jitTransitionToExecutable`, so on Switch
these are redundant for code regions covered by a transition. Leave them
as-is — extra `dc civac; ic ivau` is cheap, and they still matter for the
rare patches that don't pair with a full transition (e.g. small backpatch
rewrites that share the same JIT region but use the running scope guard).

### Teardown

`JitArm64::Shutdown` at `dolphin/Source/Core/Core/PowerPC/JitArm64/Jit.cpp:249-255`
calls `FreeCodeSpace` → `Common::FreeMemoryPages` at
`dolphin/Source/Core/Common/CodeBlock.h:72-85`, dispatched to
`MemoryUtil.cpp:156-175` (`munmap` / `VirtualFree`).

## Swap strategy for M2

One commit per concern (per CLAUDE.md). Order:

1. **`__SWITCH__` arm in `MemoryUtil.cpp:36-54`** —
   `AllocateExecutableMemory(size)` calls `jitCreate(&g_jit, size)` and
   returns `jitGetRwAddr(&g_jit)`. The `Jit` struct lives in a new
   `Common::Switch::JitMemory` translation unit (one global handle keyed
   by allocation pointer; Dolphin allocates exactly one such buffer per
   `JitArm64` instance and Dolphin uses at most two instances, so a small
   `std::unordered_map<void*, Jit>` suffices). Save the `rx_addr` → `rw_addr`
   mapping for use in step 3.

2. **`__SWITCH__` arm in `MemoryUtil.cpp:156-175`** —
   `FreeMemoryPages(ptr, size)` calls `jitClose` and erases the map entry.

3. **`__SWITCH__` arm in `MemoryUtil.cpp:94-121`** —
   `JITPageWriteEnableExecuteDisable` calls `jitTransitionToWritable` on
   the active handle; `JITPageWriteDisableExecuteEnable` calls
   `jitTransitionToExecutable`. Recompiler keeps writing through the
   pointer Dolphin already holds (the `rw_addr`). The dispatcher must
   branch through `rx_addr` — this is the load-bearing change. JitArm64
   currently uses one buffer pointer everywhere; we will need to track
   the rx alias and translate "block start in rw" → "block start in rx"
   inside the JIT entry path. Exact site: TBD during M2 (likely
   `JitArm64Cache::GetEntryFromAddress` or wherever the dispatcher reads
   block-start pointers).

4. **NACP/runtime sanity check** — at startup, call `jitCreate` with a
   1 MB probe and assert success. If the NRO was launched without JIT
   capability inheritance, fail fast with a readable error rather than a
   `0xCAFE`-family abort.

Steps 1+2 land first because they are isolated. Step 3 is the load-bearing
one and gets its own branch with an integration test against a homebrew
test ROM (M2 done-criterion).

## NACP / JIT capability — the nuance

CLAUDE.md says "the NACP file must enable the JIT capability bit". This is
half-correct and needs clarification before M2 ships:

- The "JIT" capability is a **kernel-capability descriptor**, not a NACP
  flag. The NACP carries metadata only.
- NRO homebrew **inherits** kernel capabilities from `hbloader` (the
  homebrew launcher). Atmosphère's stock `hbloader` and `hbmenu` already
  enable JIT for every NRO they launch. Hence: launching from `hbmenu`
  works without any extra build step.
- libnx headers **do not** expose a symbolic constant for the JIT bit
  (verified by grepping `nacp.h`, `nso.h`, `nro.h` in
  `/opt/devkitpro/libnx/include/switch/`). Switchbrew documents the
  encoding at https://switchbrew.org/wiki/NPDM — but for NRO this is moot
  unless we self-launch outside hbmenu.
- `nx_create_nro` / `nx_generate_nacp` (`/opt/devkitpro/cmake/Platform/NintendoSwitch.cmake:28-151`)
  expose only `NAME` / `AUTHOR` / `VERSION` / `ICON` / `ROMFS`. No JIT bit.

Action: keep the M0 build pipeline as-is. Document in the README that
`dolphin-switch.nro` must be launched via hbmenu. Revisit only if the
runtime probe in step 4 above ever fails.

## Memory budget

CLAUDE.md states "~3.2 GB usable". Querying the actual budget at runtime
is the safe path (libnx supports it; do not hard-code):

```c
#include <switch.h>
u64 total_mem = 0;
svcGetInfo(&total_mem, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
```

Functions of interest in libnx:

- `svcSetHeapSize(out, size)` — `size` must be a multiple of `0x200000`
  (2 MB), per `svc.h`.
- `envHasHeapOverride` / `envGetHeapOverrideAddr` / `envGetHeapOverrideSize`
  — `runtime/env.h:68-73`. hbloader can pass an explicit heap region;
  respect it if present.
- `virtmemFindAslr` (used internally by `jitCreate`) — virtual address
  layout for JIT regions; we do not call it directly.

The 256 MB JitArm64 buffer fits comfortably. Texture cache and ARAM
emulation are the real budget pressures — out of scope for M2 but flagged
here so we don't regret over-spending on JIT.

## Resolved questions

Verified against upstream `switchbrew/libnx` (`nx/source/kernel/jit.c` and
`nx/source/kernel/virtmem.c`):

- **JIT page alignment.** `jitCreate` rounds `size` up to a 4 KB multiple
  internally (`size = (size + 0xFFF) &~ 0xFFF`) and passes `0x1000` as
  the alignment to `virtmemFindCodeMemory` for both `rw_addr` and
  `rx_addr`. Any size we pass in is safe — no caller-side rounding
  required. Dolphin's 256 MB `TOTAL_CODE_SIZE` is already a multiple
  and unaffected.
- **Thread affinity of transitions.** `jitTransitionToWritable` and
  `jitTransitionToExecutable` use **no locks and no thread-local state**.
  They are pure wrappers around SVCs (`svcMapProcessCodeMemory` /
  `svcUnmapProcessCodeMemory` / `svcControlCodeMemory`) and an
  `is_executable` flag in the `Jit` struct. Internally they short-circuit
  if already in the target state. Consequence: any thread can call them,
  but the **`Jit` struct itself is not thread-safe** — concurrent
  transitions on the same handle race on the `is_executable` flag *and*
  on the kernel mapping state. Dolphin's compile/dispatch threads must
  serialize transitions on a single recompiler-buffer handle. The
  existing `ScopedJITPageWriteAndNoExecute` is already used inside the
  CPU thread's emit critical section; if we keep transitions inside that
  section, no extra synchronization is needed for single-core JIT.
  Dual-core JIT (M6 territory) needs a mutex around the scope guard, or
  a separate `Jit` per worker thread.

## Still open (cannot resolve without hardware)

- **Backpatch transition cost.** `JitArm64_BackPatch.cpp:346` rewrites
  small ranges inside an already-executable region. With the libnx
  swap this turns each backpatch into a full
  `executable → writable → write → executable` round-trip — and each
  transition is two SVCs plus dcache+icache flushes over the *whole*
  buffer (not just the patched range, per current libnx behavior).
  256 MB of dcache+icache flush per backpatch is unacceptable on hot
  exception paths.

  Plan: M2 measures this on hardware first. If the cost is real, two
  mitigations are on the table: (a) split JIT memory into a tiny
  trampoline buffer with frequent transitions and a large block buffer
  with rare transitions, or (b) batch backpatches and transition once
  per frame. (b) is simpler; (a) is more invasive but may be needed for
  games with dynamic-recompiler-heavy paths (Wii titles with DSP+CPU
  cross-traffic). Decide based on perf data, not speculation.

  Concrete benchmark gate before M2 is declared done: run a 30-second
  trace on Luigi's Mansion intro, measure backpatch frequency and
  per-backpatch latency, compare against the same trace on a Linux/x86
  build. If Switch is more than 5× slower in the backpatch-dominated
  segment, escalate to mitigation (b).

- **xerpi's reference port** used raw `svcMapProcessCodeMemory` /
  `svcSetProcessMemoryPermission` directly (see
  `reference/xerpi-dolphin-switch/Source/Core/Common/MemArenaSwitch.cpp`).
  We deliberately use libnx `jit_t` instead. The non-JIT half of his
  patches (emulated MEM1/MEM2 backing store + multiple aliased views)
  is a separate, parallel problem we will face during M1 — captured in
  `docs/emulated-memory.md` so M1 can pick it up directly.
