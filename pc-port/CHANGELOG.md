# Changelog — Shinobi Legions PC port

Version scheme: MAJOR.MINOR.PATCH. 0.x = work in progress (not playable).
Bump `project(... VERSION x.y.z)` in CMakeLists.txt and add an entry here.

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
