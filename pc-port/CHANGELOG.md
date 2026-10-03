# Changelog — Shinobi Legions PC port

Version scheme: MAJOR.MINOR.PATCH. 0.x = work in progress (not playable).
Bump `project(... VERSION x.y.z)` in CMakeLists.txt and add an entry here.

## 0.10.0 — 2026-10-03
- All 1-1 enemies: soldier subtypes 2 (ambusher that respawns), 3/11 (walk-in shooter),
  4 (drops from the top of the screen), 7/8 (squad of four with a leader giving fire
  orders), 10 (drop-in ambusher), 12 (rides a floating platform, leaps out and throws),
  type 3 (leaping ninja: hops, drops, counter-slash, burns) and type 4 (guard ninja:
  runs, slashes, back-flips, wall-jumps; spawned by object 0x1003, not yet ported).
- Items: drops, pickup (shurikens, health, 1-up tokens, magic), timeout blink;
  animated common sprites 0x06063350; random generator 0x0602DDD2.
- Fix: object list byte 6 is the flags byte for Obj_Register (was shifted by 8) and the
  param is sign-extended — this is what selects soldier subtypes and platform riders.
- New tests: test_soldier_a/b/c, test_enemy_t34 (tests/SOURCES.txt lists the sources).

## 0.9.0 — 2026-10-03
- Combat complete on 1-1 soldiers: shurikens fly both ways (Sho's: button A; the
  ninjas' aimed throws), hit sparks, guard sparks, explosions and slash trails are
  drawn (docs/screenshots/v0.9.0_combat.png). Sho can now be hurt from a distance.
- Ported: projectile system (64 slots 0x060F23D0): update 0x0606088C, spawn 0x0606098C,
  collision 0x06060A7C (walls, weapons reflect, guards, weak spots, piercing/bouncing
  modes), target search 0x06061062, hitbox test 0x06060E8E, bounce 0x0606137C, Sho's
  shuriken 0x060613BA + throw 0x06042D80 (Ikazuchi powered type), enemy shuriken
  0x06061854; aim 0x06093518 with the atan routine A.BIN 0x0600635A (table 0x0600EEFC).
- Effects (64 slots 0x060F33D4): update 0x06061FA0 (start delays), explosion 0x06062224,
  hit spark 0x06062314, guard spark 0x060623E4, slash trail 0x060624B4; clear 0x0606210A.
- Common sprite characters 0x4B0+ and palettes (0x06040A90).
- Still missing: effect types 5, 7-0x14 (shield flash, magic...), powered shuriken handler
  (type 4), item drops, soldier subtypes, enemy types 3/4.

## 0.8.0 — 2026-10-03
- Enemies: the black ninja soldiers of 1-1 (type 0x0001) wake up when on screen, face
  Sho, shoot / crouch / jump to dodge shurikens, take sword hits, die with their
  explosion overlay and give 100 points (docs/screenshots/v0.8.0_enemies.png).
- Ported: enemy loop 0x06048954 (slots per stage 0x060A91B8, 0x06048D00), type loaders
  0x06048C8A / 0x0608FA74, char banks 0x06049AF4, soldier handler 0x0608FB9C with its
  AI (0x06092C74, 0x06092E14, 0x06092ECE, 0x0609307A, 0x060936DE, 0x0609379C, 0x06093856,
  0x06093ACC, 0x060935F0, 0x06093930), anim switch 0x06048D3C, overlay 0x06049A4C.
- Combat: hitbox lists 0x06048DB2 / 0x06048EBC (kinds 0 body, 1 guard, 2 attack,
  3 weak spot), enemy attack vs Sho 0x0604931E (guard block, damage, touch), Sho's
  sword / shurikens vs enemy 0x060494B4 / 0x0604967C, damage 0x06045BEC (incl. the
  Ikazuchi shield), knock direction 0x06060E40, sword power 0x06045BA6, attack kind
  0x0604634E, death burst 0x06049256, score 0x06062B38 (high score, extra life
  every 30000/60000/90000), lives 0x06062B0C, effect allocator 0x060621B2.
- Not yet: projectiles (enemy shurikens 0x0606098C, Sho's shurikens), effect
  animations (sparks/explosions are allocated but not drawn), item drops 0x06062A14,
  soldier subtypes 2-12 (e.g. the ones riding platforms), types 0x0003 / 0x0004.

## 0.7.0 — 2026-10-03
- Stage objects come alive: the floating platforms of 1-1 (type 0x1004) bob on the
  water, sink and tilt under Sho and carry him (docs/screenshots/v0.7.0_platforms.png).
- Ported: object update loop 0x06060014 (handler table 0x060AC820), platform handler
  0x0604B9F8, player/object contact 0x06060268, box intersection 0x060606D0 /
  0x06048E84, push accumulator 0x0606061C (also used by the screen clamp now),
  carried-object contact 0x0604BE64, anim reset 0x06006464, sine/cosine 0x0600621C /
  0x06006286, FixMul 0x0602DD54, FixDiv 0x0602DD68.
- Emulated VDP1 VRAM (0x25C00000): STAGEx_y.SPR is loaded at 0x25C04740 like the
  original, stage sprite characters 0x10+ registered by 0x06060108; Char_Register
  0x06006DE4 is now exact (VRAM offsets) and Sho's frames are decompressed into his
  two VRAM slots (0x06044A78 / 0x06044AF8, with the post-hit blink).
- Renderer decodes VDP1 colour modes (4bpp bank/LUT, 64/128/256 colours, RGB).
- Frame order follows the original (Scroll_Update -> pad latch -> Player_Tick ->
  objects); frame counters 0x060143EA / 0x060F4CCC advance.
- Other object types (0x1000-0x1003, 0x1005, 0x1006, enemies) are placed but not run yet.

## 0.6.1 — 2026-10-03
- Backgrounds fixed (docs/screenshots/v0.6.1_stage1_1.png):
  - per-layer colour RAM offsets (VDP2 CRAOFA, 0x06005EAC(0,1,2,3)): the "sunset"
    was a wrong palette; it is a blue sky over the Hong Kong skyline;
  - full scroll update ported (0x060443D8): camera clamped to the world bounds,
    screen shake, layer positions from the frame-start camera (0x060470A4,
    exact formula FixMul(cam - 1024, rate) + offset). No more scroll approximation.
- Layer roles: 0 = level, 1 = harbour (ships), 2 = sky/mountains/city.

## 0.6.0 — 2026-10-03
- Stage 1-1 playable: real backgrounds and real map collision (flat-floor
  placeholder removed). Screenshot: docs/screenshots/v0.6.0_stage1_1.png.
- Ported: stage load/init (0x0604861C common part + 1-1 init 0x060473B4, palettes
  0x06047128), map lookup 0x06046F12, attribute + 8x8-quadrant test 0x06046FE2 /
  0x06047066, surface types 0x06045B88, box probe 0x06045670, push-out 0x0604562C,
  collision resolve 0x06045340, floor probes 0x060457EC / 0x0604588E, screen clamp
  0x060451C8, camera follow 0x060446FC.
- Emulated VDP2 VRAM (stage tiles); 3 background layers rendered (sky, city
  parallax, level) from 512x512 page textures.
- APPROXIMATIONS: layer scroll formula (VDP2 scroll update not traced; layer 1
  sits a bit low), H-flip bit, player start position, camera "previous" vars.
- Assets now also need STAGE1_1.CHR and STAGE1_1.MAP.

## 0.5.1 — 2026-10-03
- Game controller support (SDL GameController: Xbox, PlayStation, Switch Pro and
  most pads), hot-plug, 2 ports. Keyboard and controller 1 work together.
  Mapping: A=jump (Saturn C), X=sword/guard (B), B=shuriken (A), Y=ninjutsu (X),
  LB=guard (Y), RB=Z, LT/RT=L/R, Start, D-pad or left stick.
- Windows build (MinGW-w64 + SDL2 2.30.9).

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
