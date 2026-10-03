# Changelog — Shinobi Legions PC port

Version scheme: MAJOR.MINOR.PATCH. 0.x = work in progress (not playable).
Bump `project(... VERSION x.y.z)` in CMakeLists.txt and add an entry here.

## 0.3.0 — 2026-10-03
- Physics ported: generic object integrator (0x060452A4: gravity 0x5000/frame,
  terminal fall -0x7A000, inverted gravity in posture 4), ground speed/facing
  (0x06040DE8: walk 2.25, run 3.5, guard 1.75, crouch 0.75 px/frame), jump
  take-off (0x060439CC) and variable jump height (0x06040FF0: hold C up to 12
  frames; ~57 px tap, ~112 px held), player physics wrapper (0x060450E4, hitbox).
- TEMPORARY: terrain collision (0x06045340) replaced by a flat floor placeholder.
- Unit test: tests/test_physics.c.

## 0.2.0 — 2026-10-03
- Player state machine ported from SHINOBI.PPB: tick, posture dispatch, standing
  selector, 81-case action executor, slash combo, shuriken throw, walk, jump start.
- Emulated Saturn HWRAM (big-endian) with original addresses; A.BIN / SHINOBI.dec.bin
  loaded at their real bases when present in `assets/`.
- Unit tests: tests/test_pad.c, tests/test_player.c.

## 0.1.0 — 2026-10-03
- Real input chain: SMPC report emulation, Pad_Update, soft reset (A+B+C+Start),
  Game_LatchPad1 with the 6 button layouts and L/R mirroring.

## 0.0.x — 2026-08
- C11 + SDL2 skeleton: boot loop, object lists, sound scheduler, streaming audio,
  resource cache, debug renderer.
