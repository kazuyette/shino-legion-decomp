# Ground truth: the port against the real game

The real game runs in BizHawk (Saturn core, Mednafen-based) driven by a Lua file bridge
(`tools/bizhawk/sl_bridge.lua`, client `bh.py`). Because the port keeps the original RAM layout,
the real game's memory can be loaded into the port as is ("transplant"), rendered by the port's
renderers, or run by the port's logic next to the real game. Everything below was measured on
PC port v0.19.0 -> v0.20.0 (2026-10-04).

## Tools

| tool | what it does |
|---|---|
| `tools/bizhawk/snap.sh NAME` | dumps VDP1 RAM (frame N-1), then HWRAM, LWRAM, VDP2 RAM, CRAM + `NAME.State` (frame N) and the screenshot shown at N+1 into `tools/bizhawk/bridge/` |
| `tests/snap_compare.c` | transplant: loads `NAME_{hw,lw,v1,v2,cr}.bin`, detects the resident overlay (`Res_SetOverlayResident`), arms the HBlank DMA flags (`LineTable_SetDmaActive`), then `render` (soft_render; the VDP1 command list of the dump becomes the sprite list; also each screen alone and `OUT_src.bin` = which screens touch each pixel), `step N PAD` (N port frames, pad schedule `HEX:count,...`, HWRAM/LWRAM/VDP2 out; `from=F` dumps every frame) or, built with `-DSNAP_SDL` against render.c + SDL2, `sdl` (the SDL renderer, dummy video driver or `xvfb-run` for OpenGL) |
| `tools/bizhawk/imgdiff.py` | port PNG vs BizHawk shot (330x240, Mednafen border 5,8): % identical within 6/255, per-screen statistics, side-by-side + diff image |
| `tools/bizhawk/ramdiff.py` | game-state regions (globals, player, layer descriptors, stage control, checkpoint, objects, projectiles, effects, items, stage vars, layer animation, fade, VDP2 shadows, CRAM shadow, scroll shadows, line table, RNG seed 0x0603A4B0) of two HWRAM dumps |
| `tools/bizhawk/lockstep.py` | same pad schedule in BizHawk (from a state) and in the port (transplanted from that state's dumps), RAM diff at checkpoints |
| `tools/bizhawk/ls_bisect.py` | first divergent frame between A and B (per-frame dumps on both sides, deleted afterwards) |
| `tools/bizhawk/goto_stage.py N` | new game from the title menu, stage index N written during DIRECTOR's stage start (like a stage clear: 0x0602526A = N, 0x060252DC = 1), movies skipped, `stageN.State` at game mode 6 |
| `tools/bizhawk/play.py F [--hp]` | scripted walk / hop / slash for traversal (`--hp` refills HP; traversal only) |

Build (from `pc-port/`): `S=$(cat tests/SOURCES.txt); gcc -std=c11 -O2 -Isrc -Itests tests/snap_compare.c $S -lm -o snap`
then e.g. `./snap ../extracted ../tools/bizhawk/bridge/s12 /tmp/s12` and
`python3 ../tools/bizhawk/imgdiff.py /tmp/s12.png ../tools/bizhawk/bridge/s12_s.png /tmp/s12_cmp.png /tmp/s12_src.bin`;
lock-step: `python3 ../tools/bizhawk/lockstep.py s12.State ../tools/bizhawk/bridge/s12 ./snap ../extracted /tmp/ls "8000:30,8200:14,0:100" 60,150`.

## Timing facts measured on the real game

- BizHawk stops between VBlank-IN and VBlank-OUT: `0x0603A4A4 = 1` (frame done), `0x0601C49C = 0`,
  line flags bit 15 still set. The VBlank-IN handler 0x06004FD4 has incremented the frame counter
  0x060143EA and converted the HBlank table; the next frame starts with VBlank-OUT 0x060050A4
  (DMA armed, `Pad_Update`). `Boot_Frame` now follows that order (counter and shadow upload at the end).
- The VDP2 state of frame N is shown at N+1, the sprites one frame later: SBL copies frame N's
  command list at VBlank, VDP1 draws it during N+1, it is displayed at N+2. `snap.sh` therefore
  dumps VDP1 RAM one frame before the rest.
- The pad is read one frame later than the port reads it: what is held during BizHawk frame f is
  seen by the logic of frame f+1 (SMPC data fetched at the previous VBlank). `snap_compare step`
  feeds the port the pad held before the dump on its first frame.
- BizHawk outputs a 5-bit channel as `c << 3`; the port as `c * 255 / 31` (imgdiff converts).
- CD loads are not instantaneous: a stage (re)load stays 70-200 frames in game mode 5, and the 7-2
  boss intro (KORYU PBC load) has lag frames (`0x0603A4A4 = 0`, `0x0601ABD4` counting). The port
  loads in one frame, so lock-step comparisons stop at a reload and restart from a new transplant.

## Results

Renderer: transplanted frame vs the BizHawk screenshot, % of pixels within 6/255. "before" =
v0.19.0 soft_render (with the LINE_HBL_W fix only); SDL = render.c under OpenGL (Mesa, xvfb).

| screen (save state) | soft before | soft after | SDL after | verdict / cause of the remaining pixels |
|---|---|---|---|---|
| title (title.State) | 90.8 | 100 | 100 | identical. Was: no VDP2 sprite colour calc (31/32 on every sprite) |
| title menu (menu.State) | 89.7 | 100 | 100 | identical. Was: OPTIONS not dimmed (ratio 16), SDL back screen dark blue |
| 1-1 start (s11a.State) | 97.9 | 100 | 100 | identical. Was: red line, line table read at 0x0601CC58 |
| 1-1 wall + moon (s11m.State) | 99.1 | 100 | 100 | identical. Was: cloud over the moon opaque (ratio 8) |
| 1-1 city-lights band (s11c.State) | 90.9 | 100 | 100 | identical. Was: NBG2 2x1 plane read page 0x6A000 (plate address not aligned) |
| 1-2 boss (s12.State) | 97.9 | 99.94 | 99.94 | minor: 1-px edges of a slanted (distorted) sprite |
| 2-1 forest (st2.State) | 99.2 | 99.95 | 99.97 | minor (sprite edges). Was: grass not in front of feet (special priority) |
| 3-1 lab (st4.State) | 94.3 | 100 | 100 | identical. Was: colour calculation |
| 4-2 boss (s42.State) | 98.2 | 100 | 99.6 | SDL minor: GPU triangle rasterisation of the lightning sprites |
| 5-1 cave (st8.State) | 99.1 | 99.4 | 99.5 | minor: rotated Sho (VDP1 distorted-sprite texel walk) |
| 5-2 (st9.State) | - | 100 | 100 | identical |
| 6-1 sky (st10.State) | 83.1 | 99.97 | 99.99 | identical but for edges. Was: clouds opaque (sprite colour calc) |
| 7-2 start (st13.State) | 99.0 | 100 | 100 | identical |
| 7-2 boss (st13b.State) | 98.8 | 100 | 100 | identical. SDL v0.19 42.8 (negative per-line colour offset dropped without REV_SUBTRACT) |
| 8-1 (st14.State) | 99.4 | 100 | 100 | identical. SDL v0.19 7.2 (same tint cause) |
| 9-1 (st16.State) | 88.0 | 100 | 100 | identical. Was: NBG1 line scroll 1 px off (integer parts added) |
| 9-2 Kazuma (st17.State) | - | 100 | 100 | identical |

The SDL software renderer (no render targets with custom blends) reaches 96.6-100: without the
split sprite layers it cannot reproduce the framebuffer overwrite across priorities exactly.

Logic: lock-step from a transplant, same pad schedule (walk, jump, slash, shuriken, guard),
diff of all game-state regions. "identical" = only the benign words listed below differ.

| run | frames | result |
|---|---|---|
| 1-1 start (s11a) | 600 | identical (player, 80 objects, damage taken, RNG) |
| 1-1 checkpoint (s11cp) | 450 | identical after the fix (checkpoint 0 -> 1 at x 3072; v0.19.0 stayed 0) |
| 1-2 boss (s12) | 600 | identical (boss, projectiles, RNG 0x0603A4B0) |
| 2-1 (st2), 3-1 (st4), 5-1 (st8), 5-2 (st9), 8-1 (st14), 9-2 (st17) | 600 each | identical |
| 4-2 boss (s42) | 600 | identical |
| 6-1 (st10) | 374 | identical until Sho dies; then the real game is still loading (mode 5) |
| 9-1 (st16) | 540 | diverged at 212 (death animation, fixed), then identical until the reload |
| 7-2 boss (st13b, after the intro's CD lag) | 1300 | diverged at 134 and 553 (both fixed), then identical |
| 3-1 attract demo (demo31, no input: recorded demo pad) | 1790 | diverged at 764 (bomb blasts, fixed), then identical; the port run from boot through the demo loop gives the same state at 1790 (after v0.21.1) |

Benign differences (not game state, or not modelled on PC): 0x060252E8-F0 (A.BIN VDP1 transfer
queue 0x06008FE4..), 0x0603A4F8-0x0603A554 / 0x0603BB0C.. (SBL command-list pointers),
0x0603A5E0-0x0603A600 (register image written by the main loop's SCL upload), 0x06025138/3C and
0x06025244 (sound slot table / flag of A.BIN's sound code), 0x060F21B0-E8 (afterimage trail extra
record: 0x06049BF0 clears only its first long, the second is stack garbage, unused because the
flags are 0), 0x060FFE00-0x060FFEFF (PC scratch for the originals' stack records), 0x06001Exx
(stack), 0x0601C498 bit 15 (cleared at VBlank-OUT).

## Fixes made (v0.20.0)

| fix | files |
|---|---|
| HBlank line table words at 0x0601BC58 (0x0601ABD8 + 0x1080, the DMA tables 0x0603E000 / 0x0603F000 point there); 0x0601CC58 overwrote character table entries 187-398 every frame | stage_hooks.h |
| Checkpoint_Update 0x060487FC ported (stage tick) | shinobi.c |
| Zoom_Update 0x0604455A + NBG scroll shadows in Scroll_Update (zoom stays 1.0: factor 0x06014410 only set by a debug menu) | camera.c/.h, sprite.c (comment) |
| Player_OnActionChange: the four +0x5A special cases are dead code in the original | player.c |
| 7-2 Koryu: flags word +2 for actions 1/2/6; 0x060F4D00 is 32-bit | enemy_boss06.c |
| Effects current pointer 0x060F3FD4, loop indices like the original | fx.c, fx.h |
| Boot_Frame: frame counter / VDP2 "changed" bits at VBlank-IN (end of frame) | boot.c |
| soft_render: VDP1 framebuffer + per-pixel VDP2 (priorities, special priority, colour calculation, offsets), line-scroll 16.16, plane-aligned plates, back colour, `Vdp2_SpriteColorCalc`, `Vdp2_SpecialPrioMode`, `Vdp2_DecodePageSp`, `Vdp2_BackColor` | soft_render.c/.h |
| SDL renderer: back colour, sprite colour calculation (premultiplied sprite targets), special priority (page halves), exact negative colour offset without REV_SUBTRACT | render.c |
| PC hooks for the transplant: `Res_SetOverlayResident`, `LineTable_SetDmaActive` | resource.c/.h, stage_hooks.c/.h |

All tests/test_*.c (except test_audio / test_cpk) pass; the SDL executable builds and boots.

## Live VDP2 registers (after v0.22.0)

The transplants above carry the real game's VDP2 register shadow (0x0603B46C), so they validated
the renderers but not the port's own register setup. In the PC game itself SHINOBI's VDP2 init
0x060400B0 and SCL_SetColMix 0x0602B344 were only partly ported: SFPRMD, CCCTL, SFCCMD, SDCTL,
PRIR and CCRSA..D stayed 0 (no special priority per character, no colour calculation), e.g. the
1-1 temple pillar (NBG0 SPR characters, priority 5) fell behind Sho. Fixed (CHANGELOG
"Unreleased"): the PC shadow in 1-1 is now identical to s11a/s11m/s11c/s11cp
(SPCTL 2003, SDCTL 0100, SFPRMD 0015, CCCTL 004F, SFCCMD 0155, PRIR 0006, CCRSA..D 0800 1810
1F1F 1F1F); `ramdiff.py` (region "VDP2 shadows") compares a PC HWRAM dump with a real one.

## Remaining divergences

- VDP1 distorted sprites are mapped affinely (soft) or as two GPU triangles (SDL); the VDP1 walks
  the texture along the left/right edges, so rotated or slanted sprites differ by 1 px on edges
  (5-1 somersault, 1-2 pole, 4-2 lightning). Polygons / lines in the command list are not drawn
  by the transplant (the game only uses an invisible clear polygon at y 180-223).
- Loading time: stage loads and PBC loads are instantaneous on PC (real: 70-200 lag frames).
- Input latency: the port reads the pad one frame earlier than the hardware (SMPC).
- Sound driver state in RAM (0x06025138.., 0x06025244) is not modelled.
- Not checked yet: the 2-1 / 7-2 debug zoom with a non-zero factor, 2-2 / 3-2 / 6-2 / 8-2 boss
  rooms, DIRECTOR movies / tally / continue screens, VDP2 line colour screen and windows (not
  used in the screens above).

## Save states (tools/bizhawk/bridge/)

`title.State` (title screen; demo counter 4, idle 313: the 3-1 attract demo starts by itself),
`demo31.State` (first frame of game mode 6 of that demo, title.State + 2511 frames), `title_menu.State` / `menu.State` (GAME START / OPTIONS menu),
`s11_mode6.State` (1-1, first frame of game mode 6), `s11_start.State` / `s11a.State` (1-1 start,
fade-in done), `s11m.State` / `s11_mid.State` (1-1 wall with the moon), `s11cp.State` (1-1 before
checkpoint 1), `s11c.State` (1-1 city-lights band), `s12.State` (1-2 boss), `s42.State` (4-2
boss), `stN.State` for N = 2 (2-1), 4 (3-1), 8 (5-1), 9 (5-2), 10 (6-1), 13 (7-2 start), 13b
(7-2 boss after the intro), 14 (8-1), 16 (9-1), 17 (9-2 Kazuma), and `stageN.State` = the same
stages at their first frame (goto_stage.py). Each `NAME.State` matches the `NAME_*.bin` dumps.
