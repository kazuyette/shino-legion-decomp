# PC port skeleton

C + SDL2 skeleton for porting Shinobi Legions to PC. This is **not** a
matching decompilation — it's a hand-written reimplementation shell that
compiles and runs (opens a window, loops at ~60fps) while the real per-cluster
logic gets ported in from `docs/jump_table_functions.md` as we identify it in
Ghidra. Most functions are currently stubs (see `TODO` comments in `src/`).

## Build

Requires SDL2 dev headers and CMake.

```
sudo apt install cmake libsdl2-dev   # Linux
# or vcpkg/MSYS2 on Windows
cmake -S . -B build
cmake --build build
```

## Run

The game expects the extracted disc files under an `assets/` folder next to
the executable (same layout as `extracted/` from `tools/extract_disc.py`,
e.g. `assets/DIRECTOR.PPB`).

```
./build/shinobi_legions_pc
```

Expect it to open a blank window and print log lines to the console — no
rendering or real game logic is implemented yet.

## Layout

- `src/boot.*` — boot sequence and per-frame loop, mirrors `Boot_And_MainLoop` /
  `Stage_ResetAndLoadDirector` in A.BIN. `Boot_RunFrame` now runs the real
  4-slot object update + sound scheduler sequence traced from the main loop.
- `src/sound.*` — real 7-entry command queue feeding an 8-channel
  voice-stealing scheduler (`Snd_ChannelScheduler`, `Snd_FindFreeOrEvictChannel`,
  `Snd_StopMatchingChannels`), matching the traced allocator in A.BIN.
- `src/object.*` — object/channel table resets, the transform dispatcher
  (still stubbed pending the control-block struct), and a real sentinel-based
  doubly linked list for `Obj_InitLinkedLists`.
- `src/mode.*` — idle/attract mode entry.
- `src/resource.*` — `DIRECTOR.PPB` loading, handle cleanup, and a real
  `Res_LoadFileByName` with an 8-slot in-memory cache (round-robin eviction)
  matching the traced resource-loader behavior.
- `src/stream.*` — ring buffer (`RingBuf_*`) + streaming audio engine
  (`Stream_InitFromCallback`/`Stream_Update`/`Stream_BeginPlayback`) wired to
  a real SDL2 audio device callback with underrun-to-silence handling. Takes
  a PCM fill callback instead of parsing the original track-table header,
  since that format lives in data we can't read yet (DIRECTOR.PPB). Now wired
  into `Boot_Init`/`Boot_RunFrame`, feeding a synthesized 440Hz test tone
  through the real ring-buffer path — proves the plumbing works end-to-end;
  swap the callback for a real decoder once track data is readable.
- `src/render.*` — new: minimal SDL2 renderer. Clears the screen each frame
  (stand-in for `Vdp1_EraseFrameBuffer`) and draws two debug markers from
  `Obj_GetTransformBlock()`'s mode-4/mode-8 slots, so movement in the traced
  object-transform data is visible on screen. Not real sprite rendering —
  we don't have sprite/tile data yet — just a data-flow sanity check.
- `src/sys.*` — generic helpers (memcpy, the master/slave sync stub).

Each function is named to match its Ghidra counterpart — cross-reference
`docs/jump_table_functions.md` for what's confirmed vs. still guessed.

## Status

Compiles and runs (verified in CI-less sandbox build, 2026-08-05) with five
subsystems now doing real work instead of stub logging: object linked lists,
the sound scheduler, the per-frame boot loop, the streaming audio ring
buffer/SDL backend, and cached file loading. Still no rendering and no real
per-slot object data (blocked on DIRECTOR.PPB being readable) — next step is
porting more traced behavior in as more of A.BIN/DIRECTOR.PPB gets
identified.
