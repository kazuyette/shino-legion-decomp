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

- `src/boot.*` — boot sequence and stage reset, mirrors `Boot_And_MainLoop` /
  `Stage_ResetAndLoadDirector` in A.BIN.
- `src/sound.*` — sound command queue (`Snd_Cmd*`), stubbed with console logs.
- `src/object.*` — object/channel table resets and the transform dispatcher.
- `src/mode.*` — idle/attract mode entry.
- `src/resource.*` — file loading (`DIRECTOR.PPB`) and handle cleanup.
- `src/sys.*` — generic helpers (memcpy, the master/slave sync stub).

Each function is named to match its Ghidra counterpart — cross-reference
`docs/jump_table_functions.md` for what's confirmed vs. still guessed.

## Status

Compiles and runs a stub main loop (verified in CI-less sandbox build,
2026-08-04). No rendering, no real game logic yet — next step is porting the
actual traced behavior in, function by function, as more of A.BIN gets
identified.
