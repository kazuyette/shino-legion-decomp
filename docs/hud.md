# In-game HUD (status bar) — SHINOBI.PPB

PC port: `pc-port/src/hud.c` / `hud.h`, test `pc-port/tests/test_hud.c`.
Screenshot (stage 1-1, HP 12/16, 150 shurikens, 4 tokens, 3 lives, ninjutsu ready, plus a test
boss bar 10/40): `docs/screenshots/hud_stage1-1.png`.

## Where it runs

Game mode 6 (stage frame, `0x0606B8FA`):

```
0x06005BA0 display on
0x06066958 frame counter
0x06061A10 Hud_Update            <- the status bar (this file)
0x06040B44 stage tick (scroll, player, stage state) -> return if the stage changed
0x06048954 / 0x06060014 / 0x0606088C / 0x06061FA0 / 0x06062894 enemies, objects, shots, fx, items
0x0606BCD0 per-stage frame hook + layer animation   (NOT the HUD, see below)
0x06063554 event / script state machine (0x060F4CA8)
0x0606BB8A attract-demo end
```

The HUD is drawn **before** the stage tick, so it shows the values of the previous frame
(same as the Saturn).

## Layers

* Static part: NBG3 map built once per stage by `Hud_InitNbg3Map` 0x06047244 (LZ 0x060CE5B8 ->
  0x060EA658 -> 64x32 2-word pattern names at VDP2 0x25E6C000). Shows "PLAYER", the bar
  frames, the icons and the "-" separators.
* Dynamic cells are rewritten every frame in the same page: cell (x, y) is the u32 at
  `0x25E6C000 + y*256 + x*4`; value = `0x2C00 | tile` (character base 0x2C00 = last 32 KB of the
  stage CHR) `| palette << 16`.
* HP ticks and the ninjutsu icon are VDP1 sprites through the sprite work struct 0x060250AC
  (`Sprite_Draw` 0x060068C0), flags 0x300 (priority offset 0xFF, not zoomed).

## Functions

| Address | Name | What |
|---|---|---|
| 0x06061A10 | Hud_Update | life bar, 3 numbers, HP ticks, ninjutsu icon |
| 0x06061A56 | Hud_DrawLifeBar | HP 0x060EA2CA (clamped >= 0) / HP max 0x060252B0 -> bar at (6,1) |
| 0x06061AC0 | Hud_DrawHpTicks | HPmax-1 sprites char 0x4DD at x = 48 + i*95/HPmax, y offset 12, colour bank 112 |
| 0x06061B58 | Hud_DrawBossBar(max, value) | bar at (6,3) + label (cols 1-5, rows 3-4, table 0x060ACA48); stage index 17: no bar, last label column from 0x060ACA5C. Called by the boss objects (0x0606A29E, 0x0606DC30, 0x06071E92, 0x060761E0, 0x0607AFE6, 0x0607E8AA, 0x06089E7C, 0x0609B46E, 0x060A01A0) |
| 0x06061C6E | Hud_DrawBar(x, y, fill, colour) | 12 cells x 8 units (fill clamped to 95), colour 0..3 picks table 0x060D2EF4[colour] (9 x {top,bottom}: 0/8 .. 8/8), end cap cells 0x25/0x35 |
| 0x06061D28 | Hud_PutDigit(x, y, digit, pal) | 2 cells (rows y, y+1) from table 0x060ACAF0 ({top,bottom} per digit, 10 = blank) |
| 0x06061D8E | Hud_DrawNumber(x, y, value, digits, flags) | flags: 1 zero-pad, 2 left-align + trailing blanks (if not 1), 4 palette 8 on digits; otherwise right-aligned with leading blanks; only the low `digits` digits |
| 0x060632D8 | Hud_DrawMagicIcon | if 0x060252BE != 0: sprite char 0x4E5 at (152, 8), colour bank 123 |

Bar maths (life and boss): `fill = ((value << 8) / max * 95 + 0x80) / 256` (0..95, signed
`__divsi3` 0x060092BC, /0 = 0), `colour = clamp(3 - fill / 23, 0, 3)` (3 = red/low .. 0 = full).

Numbers drawn by `Hud_Update` (all flags 2 = left-aligned, row 1):

| Column | Digits | Variable |
|---|---|---|
| 25 | 3 | 0x060252B6 shurikens |
| 32 | 1 | 0x060252B2 1-up tokens (0..9; the 10th gives a life, 0x06062BC6) |
| 37 | 2 | 0x060252B8 lives |

The score (0x060252C8) is not shown during a stage (DIRECTOR's tally only). There is no timer.

A.BIN helpers used: 0x060092BC `__divsi3` (r1 / r0, signed), 0x0600A55C `__udivsi3`,
0x0600CEF4 `__umodsi3` (r1 % r0).

## 0x0606BCD0 (not the HUD)

`0x0606BCD0` (Stage_FrameHooks) calls the per-stage frame handler of table 0x060B10CC (18
entries, 0 for index 3), then LayerAnim_Update `0x0606F6AC` and LayerAnim_Commit `0x0606EF9C`
(line scroll / wave / cell scroll on the block 0x060F4D80, VDP2 shadow SCRCTL 0x0603A60A,
VCSTA 0x0603A60C, LSTA0/1 0x0603A610/14 of the register shadow 0x0603A570). The per-stage
inits are in table 0x060B1114 (called by StageHooks_Init 0x0606C618 at stage load). NBG2 line
scroll and per-line colour offsets go through the A.BIN HBlank table 0x0601ABD8 (flags
0x0601C498). Ported in `pc-port/src/stage_hooks.c` (layout in stage_hooks.h); the 4-2 handler
0x0606D99E draws the boss bar (0x06061B58). 0x06063554 is the ninjutsu effect state machine
(not an event system): only its idle path is ported.

Other callers of the digit routines: 0x06067246 (a debug/menu display at row 24,
counter 0x060F4CF8) — not ported.
