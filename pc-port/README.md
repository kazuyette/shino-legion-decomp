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

## Version

The port version lives in `CMakeLists.txt` (`project(... VERSION x.y.z)`) and is
logged in `CHANGELOG.md`. CMake generates `version.h` (version + short git hash);
it is shown in the window title, printed at startup, and by `--version`.

## Controls

| Action | Keyboard | Controller (Xbox / PlayStation) |
|---|---|---|
| Move / crouch | Arrows | D-pad or left stick |
| Sword (hold = guard) | X | X / Square |
| Jump (hold = higher) | C | A / Cross |
| Shuriken | Z | B / Circle |
| Ninjutsu | A | Y / Triangle |
| Guard | S | LB / L1 |
| Start | Enter | Start |
| Debug / cheat menu (PC only, pauses the game) | F1 | Back / Select |

## Assets

The executable reads the disc files from an `assets/` folder (or `--assets DIR`); the
layout is the one of `extracted/` (`python3 tools/iso_extract.py extracted/track1.iso NAME
extracted/NAME` extracts one file from the data track):

| file(s) | used for |
|---|---|
| `A.BIN` | system program, loaded at 0x06004000 at power-on (required) |
| `DIRECTOR.dec.bin` (or `DIRECTOR.PPB`) | front-end overlay at 0x06040000: logo, title, stage start, tally, continue... |
| `SHINOBI.dec.bin` (or `SHINOBI.PPB`) | in-game overlay at 0x06040000 |
| `SHOU.CHR` | Sho's graphics (LWRAM 0x00200000, loaded by DIRECTOR) |
| `TITLE.PBC`, `STGCLEAR.PBC`, `STAFFROL.PBC` | title, stage clear, staff roll pictures |
| `STAGEn_m.CHR/.MAP/.SPR` (n = 1..9, m = 1..2) | stage graphics/maps (game 1-1 = `STAGE7_1`, see docs/director.md §4.1) |
| `*.CPK` | movies (not played yet: the movie player is a stub, every movie is skipped) |
| `*.SND`, `TITLE.PCM`, `ENDING.PCM` | sound banks / CD-DA tracks (not handled yet, only logged) |

`X.dec.bin` is `X.PPB` already decompressed (`tools/ppb_decompress.py`); when it is absent
the `.PPB` is decompressed at load time. Without `A.BIN` nothing runs.

## Run

```
./build/shinobi_legions_pc [--assets DIR] [--skip-intro] [--stage N] [--verbose]
```

The program follows A.BIN's main loop (`src/boot.c`): DIRECTOR.PPB is loaded first
(SEGA logo, title, Start = GAME START, stage start, story), then SHINOBI.PPB for the stage;
stage clear / game over / demo end load DIRECTOR back (`src/director*.c`, `src/shinobi.c`).

- `--skip-intro`: no SEGA logo at boot (on the Saturn the BIOS shows it and the game skips
  its own; without this option the port shows DIRECTOR's logo in place of the BIOS one).
- `--stage N`: development shortcut, boots straight into stage index N (0 = 1-1 ... 17 = 9-2;
  index 0 and 12 have their per-stage setup ported, the others load with a warning).
- `--verbose`: log the player's action changes.

The loop runs a drift-free ~60fps (measures elapsed time and sleeps off the remainder).
Arrow keys = D-pad, Z/X/C = A/B/C, A/S/D = X/Y/Z, Q/W = L/R, Enter = Start.
The input chain is the real one (see `pad.*`): pressed buttons are logged to
the console, and A+B+C+Start triggers the original soft reset.

## Layout

- `src/input.*` — keyboard -> emulated SMPC report (active-low raw pad word per port),
  replaces the SMPC/PER library (`PER_GetPer` @ A.BIN 0x0602D076).
- `src/pad.*` — faithful ports of `Pad_Update` (A.BIN 0x0600572A, table 0x0601C4A0),
  `Pad_Clear`, `Sys_CheckSoftReset` (A.BIN 0x060042B8) and `Game_LatchPad1`
  (SHINOBI 0x06040CAC: game pad vars, 6 button layouts, L/R mirroring).
  Unit test: `gcc -std=c11 -Isrc tests/test_pad.c src/pad.c src/player.c`.
- `src/ram.*` — emulated Saturn HWRAM (big-endian). Ported code keeps original
  addresses; `assets/A.BIN` and `assets/SHINOBI.dec.bin` are loaded at their real bases if present.
- `src/player.*` — player state machine ported from SHINOBI (tick, posture dispatch,
  standing selector, 81-case action executor, slash combo, shuriken, walk). Action changes
  are printed to the console. Unported callees are logging stubs in `src/player_stubs.c`.
  Test: `gcc -std=c11 -Isrc tests/test_player.c src/player.c src/player_stubs.c src/pad.c src/ram.c`.

- `src/boot.*` — A.BIN boot + main loop `Boot_And_MainLoop` 0x06004038 (`Boot_Init`,
  one `Boot_Frame` per frame: pad, soft reset 0x060042B8, resource queue, overlay entry
  0x06040000, `Fade_Update`), `--stage` shortcut.
- `src/director*.c`, `src/director_int.h` — DIRECTOR.PPB (front-end): entry, init, scene
  table, SEGA logo, stage start, story movies, new game / continue variables, font
  (`director_font.c`); title / tally / continue+game over+staff roll are stubs for now
  (`director_title.c`, `director_tally.c`, `director_gameover.c`). See docs/director.md.
- `src/movie.*`, `src/cpk.h`, `src/movie_stub.c` — DIRECTOR's movie wrapper (playlist,
  skip rules, frame as a VDP1 RGB char) over a CPK/Cinepak backend (stub: movies skipped).
- `src/shinobi.*` — SHINOBI.PPB entry and game modes (0x0606B54C: init, load stage,
  stage frames, stage-clear bonuses, game over, back to DIRECTOR).
  Headless test of the whole flow: `tests/test_director.c`.
- `src/sound.*` — real 7-entry command queue feeding an 8-channel
  voice-stealing scheduler (`Snd_ChannelScheduler`, `Snd_FindFreeOrEvictChannel`,
  `Snd_StopMatchingChannels`), matching the traced allocator in A.BIN.
- `src/object.*` — object/channel table resets, the mode-4/8/0x10/0x20
  transform dispatcher, a confirmed second per-slot data channel
  (`Obj_SetTransformParamSecondary`, mode 4 only — the other modes and its
  clamp pre-pass weren't traced), and a real sentinel-based doubly linked
  list for `Obj_InitLinkedLists`.
- `src/mode.*` — old speculative port (actually Cpk_Reset territory, see docs/director.md); unused.
- `src/resource.*` — A.BIN file requests (`Res_Request` 0x06004A1C: raw / LZ /
  sound bank, the 8-slot queue 0x060049C4 / 0x0600446A, `Load_ShinobiPPB` /
  `Load_DirectorPPB` overlay swaps at 0x06040000) reading from the asset directory.
- `src/stream.*` — ring buffer (`RingBuf_*`) + streaming audio engine
  (`Stream_InitFromCallback`/`Stream_Update`/`Stream_BeginPlayback`) wired to
  a real SDL2 audio device callback with underrun-to-silence handling. Takes
  a PCM fill callback instead of parsing the original track-table header,
  since that format lives in data we can't read yet (DIRECTOR.PPB). No longer
  called (it is actually the CPK movie library, see docs/director.md); was feeding a synthesized 440Hz test tone
  through the real ring-buffer path — proves the plumbing works end-to-end;
  swap the callback for a real decoder once track data is readable.
- `src/render.*` — new: minimal SDL2 renderer. Clears the screen each frame
  (stand-in for `Vdp1_EraseFrameBuffer`) and draws two debug markers from
  `Obj_GetTransformBlock()`'s mode-4/mode-8 slots, so movement in the traced
  object-transform data is visible on screen. Not real sprite rendering —
  we don't have sprite/tile data yet — just a data-flow sanity check.
- `src/input.*` — new: keyboard-to-pad scaffolding (`Input_Update`/
  `Input_GetPadState`), Saturn-digital-pad-shaped `PadState` struct. Not
  wired to any game logic yet — the real pad-reading code was never found
  in A.BIN and is presumed to live in `DIRECTOR.PPB`/`SHINOBI.PPB`, blocked
  on the disassembly wall documented in `docs/jump_table_functions.md`.
  Exists so the plumbing is ready the moment the real mapping is known.
- `src/sys.*` — generic helpers: `Sys_MemCopy` (byte copy), `Sys_MemCopyWords`
  (confirmed word-oriented copy used by the VDP1/VDP2 dirty-region flush
  functions), the master/slave sync stub.

Each function is named to match its Ghidra counterpart — cross-reference
`docs/jump_table_functions.md` for what's confirmed vs. still guessed.

## Status

Compiles and runs (verified in CI-less sandbox build, 2026-08-05) with real
implementations for: object linked lists + the mode-4/8/0x10/0x20 transform
dispatcher, the sound scheduler, the per-frame boot loop, the streaming
audio ring buffer/SDL backend (test tone), cached file loading, a minimal
SDL2 renderer (debug markers only), and keyboard-to-pad input scaffolding
(not wired to anything yet). No real sprite/tile rendering, no real music,
no real input handling — all three are blocked on the same wall: gameplay
logic (including pad input) lives in `DIRECTOR.PPB`/`SHINOBI.PPB`, and
those files resist static disassembly in both Ghidra and a from-scratch
disassembler (see `docs/jump_table_functions.md` for the full
investigation — several hypotheses tested, root cause still unconfirmed).
Next step once that's unblocked: wire real per-slot object data, real pad
reads, and real track/sprite data into the plumbing that's already in
place.
