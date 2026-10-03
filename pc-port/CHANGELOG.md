# Changelog — Shinobi Legions PC port

Version scheme: MAJOR.MINOR.PATCH. 0.x = work in progress (not playable).
Bump `project(... VERSION x.y.z)` in CMakeLists.txt and add an entry here.

## 0.5.0 — 2026-10-03
- First on-screen graphics: Sho is drawn from the real game data
  (docs/screenshots/v0.5.0_player.png).
- Ported: word-LZ decompressor in C (lz.c), animation player Anim_Start/Anim_Tick
  (A.BIN 0x0600648C/0x060064C0, script commands F900-FF00), Char_Register
  (0x06006DE4), Sprite_Draw geometry (0x060068C0: flips, rotation, colour bank),
  Player_SubmitSprite streaming (0x06044AF8), Pal_SetFaded (0x06005C8C) into an
  emulated shadow CRAM, Player_UpdatePalette (0x06044D78), Player_Draw (0x06043FDA,
  incl. somersault rotation).
- Emulated LWRAM (0x00200000) for SHOU.CHR; SDL renderer at 320x224 with a texture
  cache (8bpp + palette, index 0 transparent).
- Real animation scripts now drive the state machine (fake "anim finished" only
  when the game data is missing).
- Assets needed in assets/: A.BIN, SHINOBI.dec.bin, SHOU.CHR (extract from your disc).
- Tests: tests/test_realdata.c (real data, headless), tests/shot.c (screenshots).

## 0.4.0 — 2026-10-03
- Air selector ported (0x06041E48): air slash (B, +Up, diagonal), Down+B down-stab
  (0x16, bounces off enemies -> 0x4D), air shuriken, Y/Z air attack 0x3C, double
  jump/somersault near the apex (0x06, velY 8.0, SFX 0x120), wall-jump flip attack
  (0x1A, C toward a wall), ceiling grab (Up against a ceiling -> posture 4, 0x40),
  jump buffering while falling, air hurt/death.
- Air pose from vertical speed (0x0604241E): rise 0x37/0x39, apex 0x35/0x38, fall 0x3A.
- Crouch selector ported (0x06041B3C): crouch slash + combo (9 -> 0x32), crouch
  shuriken, crouch guard, crawl (0x08), stand up, crouch hurt/death.
- Somersault exec (0x06043C58) and crouch slash exec (0x06043BC8).
- Flat-floor placeholder now lands into 0x3D and raises the "landed" flag (flags4 & 0x20).

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
