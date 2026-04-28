# Emulated memory on Horizon OS

Notes for M1 (cross-compile `dolphin-emu-nogui` for Switch, then start
wiring up emulated GameCube/Wii memory). This is **not** the JIT
recompiler buffer — that lives in `docs/jit-memory.md`. The two problems
are adjacent but use different kernel paths and have different
constraints. Get the distinction right before you write any code.

## What "emulated memory" means here

Dolphin needs to model the GameCube/Wii memory map:

- MEM1 (~24 MB): main RAM at guest physical 0x00000000.
- MEM2 (~64 MB on Wii): aux RAM at guest physical 0x10000000.
- ARAM (~16 MB): audio RAM, accessed via DMA from the DSP.
- L1 dcache emulation regions, EXRAM, etc.

These regions are **mirrored** in guest virtual address space — the same
physical memory shows up at multiple guest VAs (e.g. 0x80000000-cached
and 0xC0000000-uncached views of MEM1). On the host, this means **one
backing buffer mapped into multiple host VAs**.

On Linux today this is a `memfd_create` + repeated `mmap MAP_SHARED`.
On Windows it's `CreateFileMapping` + `MapViewOfFile` repeatedly. The
abstraction is `dolphin/Source/Core/Common/MemArena.{h,cpp}` —
specifically `MemArena::CreateView` / `ReleaseView` /
`ReserveMemoryRegion` / `MapInMemoryRegion`. Every host platform has its
own arm.

## Switch path: same shape, different SVCs

The pattern that works on Horizon (validated by xerpi's port,
`reference/xerpi-dolphin-switch/Source/Core/Common/MemArenaSwitch.cpp`)
is:

1. **Allocate one backing buffer.** A page-aligned host allocation:
   `aligned_alloc(0x1000, size)`.
2. **Map it into a code-memory region** via `svcMapProcessCodeMemory`
   (or the equivalent libnx wrapper) so the kernel treats it as a
   process-managed region we can sub-map. Set `Perm_Rw` permissions
   with `svcSetProcessMemoryPermission`. This anchor mapping is
   `m_memory` in xerpi's terminology.
3. **For each mirrored view:** find a free virtual range with
   `virtmemFindAslr(size, 0x1000)`, lock it with
   `virtmemAddReservation`, then `svcMapProcessMemory(view_addr,
   own_proc_handle, m_memory + offset, size)`. Each of these creates an
   additional VA alias of the same physical bytes.
4. **Teardown** is the inverse: `svcUnmapProcessMemory` then
   `virtmemRemoveReservation` per view, then unmap the anchor.

The "find ASLR + reserve" dance is necessary because libnx will reuse
freed virtual ranges otherwise — without an explicit reservation, the
next allocator call could land on top of our view.

## API surface to fill in `MemArena.cpp`

The functions Dolphin's `MemArena` exposes (and that we'll need a
`__SWITCH__` arm for):

- `MemArena::GrabSHMSegment(size, base_name)` — allocate the backing
  buffer, anchor-map it. Sets `m_memory`.
- `MemArena::ReleaseSHMSegment()` — drop the anchor mapping. Must
  *first* release every view tracked in `m_views`/`m_maps`.
- `MemArena::CreateView(offset, size)` — find ASLR slot, reserve, map a
  slice of `m_memory` into it. Returns the view pointer.
- `MemArena::ReleaseView(view, size)` — unmap, unreserve, drop tracking.
- `MemArena::ReserveMemoryRegion(memory_size)` — find one large ASLR
  slot for "fastmem" (direct guest pointer access). Returns the base.
- `MemArena::ReleaseMemoryRegion()`.
- `MemArena::MapInMemoryRegion(offset, size, base)` — sub-map a range
  inside the fastmem reservation.
- `MemArena::UnmapFromMemoryRegion(view, size)`.

xerpi tracks views/maps in two `std::map<void*, ...>` members, keyed by
the view pointer. Mirror that.

## Caveats inherited from xerpi (do not repeat)

The xerpi port has at least three correctness issues we should fix
during the M1 port, **not** copy:

- **`MemArenaSwitch.cpp:118`** —
  `virtmemAddReservation(m_memory, memory_size)` reserves the wrong
  pointer. It should be the freshly-found `dst` from
  `virtmemFindAslr`. Reserving `m_memory` does not protect the new
  region from being reallocated.
- **`MemArenaSwitch.cpp:148`** — `UnmapFromMemoryRegion` erases from
  `m_views` instead of `m_maps`. The fastmem map entry leaks; on heavy
  game-switching this grows unbounded.
- **`MemArenaSwitch.cpp:46-49`** — destructor leaves
  `svcCloseHandle(m_cur_proc_handle)` commented out. Process handle
  leaks across `MemArena` lifetime. Probably benign at process scope
  but wrong on principle.

Also worth questioning: xerpi uses `virtmemFindCodeMemory` for the
backing-buffer anchor, not just for the JIT buffer. That works (the
mapping is RW after `svcSetProcessMemoryPermission`) but conflates
"code memory" with "data memory" semantically. If libnx exposes a
plain-data variant by the time we land M1, prefer it. Otherwise the
xerpi pattern is fine — just document it.

## Stubbed protections we need to think about

xerpi left these as no-ops in `MemoryUtil.cpp`:

- `ReadProtectMemory` (line 224)
- `WriteProtectMemory` (line 238)
- `UnWriteProtectMemory` (line 255)
- `MemPhysical` (line 294, returns 0)

`WriteProtectMemory` is used by Dolphin's *fastmem* exception path —
the emulator marks pages as no-write so guest stores trap into a
handler that decodes the access and emulates it. On a desktop OS this
is `mprotect`/`VirtualProtect`. On Switch we have
`svcSetProcessMemoryPermission`, which can flip a range to `Perm_R`. We
*can* implement these, but only after we have the SIGSEGV-equivalent
exception handler wired up (xerpi has a sketch in
`Source/Core/Core/MemTools.cpp` using `__libnx_exception_handler` and
`ThreadExceptionDump`). Sequencing for M1: stub these out behind
`#ifdef __SWITCH__` returning false / no-op so the emulator falls back
to slow-mem; revisit during M2 once JIT is alive.

`MemPhysical` is informational (used by some heuristics). Wire it to
`svcGetInfo(InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0)` when we
get there.

## Build-system flags xerpi set (still useful)

His root `CMakeLists.txt` set the `NINTENDO_SWITCH` define and forced
off: `ENABLE_QT`, `ENABLE_VULKAN` (we re-enable in M3), `USE_UPNP`,
`USE_DISCORD_PRESENCE`, `__LIBUSB__`, `HIDAPI`. We will need our own
edits when wiring `add_subdirectory(dolphin)` in M1, but his list is a
useful starting checklist of what to disable. He did **not** add an
explicit `-lnx`; the devkitPro toolchain wires that in.

## What this means for milestone sequencing

- **M1 (cross-compile)**: stub out `MemArena`'s Switch arm with
  enough skeleton to link. Returning `nullptr` from `GrabSHMSegment` is
  fine — emulated memory does not need to *work* yet, just compile.
- **M2 (JIT)**: focuses on `jit_t`. `MemArena` stays stubbed.
- **Between M2 and M3**: implement the real `MemArena` arm using the
  pattern above. Test ROM that requires emulated memory (any
  non-trivial homebrew) won't run until this is done.
- **M3 (Vulkan)** assumes M2.5 above is in place — the GPU video
  backend reads guest memory through the same `MemArena` views.
