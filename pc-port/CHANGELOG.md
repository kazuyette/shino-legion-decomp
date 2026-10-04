# Changelog — Shinobi Legions PC port

Version scheme: MAJOR.MINOR.PATCH. 0.x = work in progress (not playable).
Bump `project(... VERSION x.y.z)` in CMakeLists.txt and add an entry here.

## 0.22.1 — 2026-10-04
- VDP2 register setup of the real game (bug report: 1-1 temple wall, Sho's hand / kunai drawn
  over the stone pillar, pillar looking see-through). SHINOBI's VDP2 init 0x060400B0 was only
  partly ported and SBL SCL_SetColMix 0x0602B344 not at all, so in the PC game (unlike the
  BizHawk transplants of docs/ground_truth.md, which carry the real registers) the shadow
  0x0603B46C kept SFPRMD, CCCTL, SFCCMD, SDCTL, PRIR, CCRSA..CCRSD at 0: no special priority per
  character, no colour calculation. The 1-1 pillar column is NBG0 characters with the SPR bit
  (priority 4 | 1 = 5) plus priority-5 sprites for its edges and ornament; with SFPRMD = 0 the
  column fell to priority 4 = Sho's sprite priority, and sprites win ties, so Sho was drawn over
  the column but cut by the edge sprites. Same cause for every per-character foreground (2-1
  grass, 1-1 ground band / bushes...).
  - shinobi.c: Shinobi_InitVdp2 now writes the whole register shadow like 0x06040214..0x06040A6C
    (CRAOFA/B, SPCTL, PRISA..D, CCRSA..D = 0 8 16 24 31 31 31 31, LNCLEN, SFPRMD = 0x0015, PRINA/B,
    PRIR, CCCTL, SFCCMD = 0x0155, CLOFEN/CLOFSL, colour offsets, SDCTL = 0x0100) after
    SCL_SetColMix(NBG0..NBG3, 0); Game_InitScreens / Game_LoadStage call SCL_SetColMix(0,0) and
    (0x100,0) like 0x0606B634 / 0x0606B6EC: CCCTL = 0x004F. In 1-1 the shadow is now identical
    to the real game's dumps (s11a/s11m/s11c/s11cp).
  - sprite.c: Scl_SetColMix (0x0602B344): CCCTL enable bits + ratio registers (sprites CCRSA..D,
    NBG0..3 CCRNA/CCRNB, RBG0 CCRR), CCRTMD / CCMD cleared. Also called by Director_Init
    (0x06040094), the movie VDP2 setup 0x060409C0 and the 3-2 hook 0x0606D7F2 (NBG2 ratio 15).
  - render.c: NBG colour calculation (CCCTL bits 0-3, SFCCMD per screen / per character / colour
    MSB, CCRNA/CCRNB ratio, add mode), drawn as alpha (or additive) blending of page variants:
    the page textures are split by SPR bit (special priority) and by the colour-calc flag. Plain
    SDL_BLENDMODE_BLEND / ADD + alpha mod: works on every backend. soft_render.c uses the same
    register decoding (Vdp2_LayerColorCalc, Vdp2_DecodePageSel).
  - render.c: the sprite-layer composite (custom blend modes in render targets) is checked once
    at startup by a read-back self-test (draw, erase, premultiplied colour calculation, copy); if
    a backend gets it wrong the portable path (sprites grouped by priority) is used. The console
    prints which one.
  - shinobi.ini `[video] renderer = auto | direct3d | direct3d11 | direct3d12 | opengl |
    opengles2 | software` (SDL_HINT_RENDER_DRIVER; falls back to auto if it cannot be created);
    the renderer in use and the available ones are printed at startup.
  - Checked: the pillar spot (1-1 checkpoint 2, x 8888) in soft_render, SDL software and SDL
    OpenGL, before / after (`Claude outputs/bugs/pillar_1-1/`); all tests/test_*.c pass; the 17
    ground-truth screens of docs/ground_truth.md unchanged (soft / OpenGL / software).

## 0.22.0 — 2026-10-04
- Attract demo (docs/director.md §1.4): the recorded demo input is ported — Demo_InputInit
  0x060469FE (tail of Player_InitForStage) and Demo_Input 0x06046A98 (pad.c, called by the pad
  latch when 0x06014408 & 0x8000); tables {held, frames} in SHINOBI.PPB at 0x060CA71C /
  0x060CA8E4 / 0x060CAAE4 / 0x060CAD28 / 0x060CAED4 for the demos 1-1, 2-1, 3-1, 4-1, 5-1.
  Idle title (2280 frames) -> demo -> 1801 stage frames -> SEGA logo -> title, Start -> title,
  as on the real game. 3-1 demo lock-step against BizHawk (demo31.State): identical over 1790
  frames.
- shinobi.c: RNG seed at the end of the stage load (0x0606B838: 0 in a demo, else the frame
  counter) was missing; Player_InitForStage HP rule (HP = HP max except after a clear into an
  act 2, magic state cleared) replaces the PC defaults of Player_Init, which also overwrote HP
  max with 16 and the shurikens with 20 at every (re)load.
- enemy_t16.c: bomb (projectile 0x13) blasts re-read x/y from the freed slot like the original
  (the four outer blasts were 16 px too low).
- fx.c: effect 7 (0x06062578, Ikazuchi shield flash when the shield absorbs a hit, spawned by
  Player_TakeDamage): sprite list 0x060AD2C2, drifts 3 px/frame, drawn on odd frames.
- tests/test_demo.c, tests/test_effect7.c.
- CRT filter (src/crt.c, PC only): shinobi.ini `[video] crt = off | scanlines | crt`, `crt_strength`
  (0..100, 70), `crt_mask = aperture | slot | none`, `crt_curvature` (0..100, 0); F10 cycles off ->
  scanlines -> crt with an on-screen toast (session only, the ini is not rewritten). Render_Present
  scales the 320x224 frame to the output rectangle, adds it again for brightness, multiplies it
  (SDL_BLENDMODE_MOD) by overlays built on the CPU once per window size at the exact output
  resolution (Gaussian beam profile per source line, box-filtered per output row, gamma-aware;
  phosphor mask per output column = no moire), adds a 160x112 / 80x56 glow (crt), optional bent
  mesh (SDL_RenderGeometry) with the mask applied after it. No shader, works on every SDL
  renderer with render targets. Shift+F12 = screenshot of the picture as shown (F12 stays the
  clean 320x224 frame). tests/test_crt.c (pattern maths + ini keys), tests/shot_crt.c (offscreen
  captures + Render_Present timing); comparison captures in `Claude outputs/crt/`.
- Sound (docs/sound.md §3, §6): sfx.c now follows the SBL path as read from A.BIN — the SH-2
  request queue 0x06007FA8 / scheduler 0x0600863E (17 x 12-byte requests, priority sort, slot 0
  = tunes, slots 1-7 = SE, steal one frame later, unplaced SE dropped, no same-id restart),
  0x06008154 / 0x06007DBA sequence volume and fades, 0x060084BE stop by slot, and the 68K
  driver: timer-B ticks of 88 samples with its 8.8 tempo maths, note table (note-off by channel
  + key), event step = delay BEFORE the event (was after: every event was one step early),
  channels 0-31, CC 0x20 resets volume/program, program change resets bend, velocity table +
  TL law (velocity and CC 7 linear in dB: TL = 255 - (2v)(255 - TL)(2 vol) >> 16), driver pan
  law (DIPAN side was reversed; pan changes follow on channel 0 only, as the driver), pitch =
  driver OCT/FNS tables (fine tune = 1/256 semitone, was 1/128), bend applied by the main-loop
  refresh, gate extensions 0x88-0x8B confirmed, EG key rate scaling, hardware PLFO/ALFO, mono
  flag = centred slots (audio.c: SFX no longer averaged). DSP: the game loads no effect
  program (no DSP area, SND_ChgEfct/ChgMix not linked, MPRO cleared, mixers zero) — no reverb
  on the Saturn, none here. Output gain 0.9. Sfx_*Locked API for the remaining 0x06008154 /
  0x060084BE callers. tests/test_sfx.c rewritten (driver tables, queue, fades, mono, clipping,
  loop timing; listening WAVs with a 2nd argument).

## 0.21.1 — 2026-10-04
- Unicode file paths on Windows (u8path.c): every file access (assets, disc image, .cue
  contents, shinobi.ini, screenshots) goes through UTF-8 -> UTF-16 wide APIs, so folders or
  disc images with accents / non-Latin characters work; a .cue saved in the ANSI code page
  still works (fallback). Console output in UTF-8.

## 0.21.0 — 2026-10-04
- Game files from the player's own disc image, so the port is distributed without game data:
  - src/extract.c/.h (plain C, no SDL): Extract_FromCue(cue_or_bin, out_dir, progress) parses the
    cue sheet (single or multi .bin, MODE1/2352, MODE1/2048, MODE2, AUDIO, PREGAP/POSTGAP), also
    a lone .bin or a plain .iso (data track only: music missing, warning), reads the ISO9660
    tree, copies the files, writes DIRECTOR.dec.bin / SHINOBI.dec.bin (lz.c) and rips CD-DA
    tracks 2/3 to TRACK02.WAV / TRACK03.WAV like tools/rip_cdda.py (TITLE.PCM / ENDING.PCM
    extents; the DW0536 cue's track-3 index is 150 frames late). Each file is written to
    NAME.part, verified (size + CRC-32) and renamed. Checks the Saturn header (product T-2301H,
    area U): clear errors for a non-Saturn image, wrong disc, other region, bad / truncated dump
    (mismatching files listed, not installed); a WAV that differs (audio read offset) is kept
    with a warning.
  - src/asset_manifest.h (115 files: name, size, CRC-32, source), generated from
    bin/windows/assets by tools/gen_manifest.py.
  - main.c: `--extract IMG [--assets DIR]` (console progress, exit 0 / 2 = music missing / 1);
    a .cue/.bin/.iso as argument (dropped on the exe) is extracted, then the game starts; at
    startup an incomplete asset folder (names + sizes) makes the exe offer to extract the *.cue
    found next to it (Yes/No box), or explain how to provide the disc image, then quit.
    Default asset folder: assets/ in the working directory when it holds A.BIN, else
    assets/ next to the executable. `--no-asset-check` skips the check.
  - tests/test_extract.c (no SDL): error paths on synthetic images, then the real image is
    extracted to a temp folder: 115/115 files identical to bin/windows/assets (also checked
    by hand: a Redump-style 3-file image, 115/115; extracted/track1.iso, 113/115 + warning).
  - tools/make_dist.py: dist/ShinobiLegionsPC-<version>-win64.zip from a given exe + SDL2.dll
    + packaging/README.txt (players, FR + EN) + packaging/NOTICE.txt (no game data) +
    CHANGELOG; refuses game-data files; `--dry-run`, `--self-test`. dist/ added to .gitignore.

## 0.20.0 — 2026-10-04
- Ground truth: the real game (BizHawk Saturn core, Lua file bridge tools/bizhawk/) is now the
  reference. tests/snap_compare.c (not in the suite) transplants BizHawk dumps of HWRAM / LWRAM /
  VDP1 / VDP2 / CRAM into the port and renders them (soft_render, or the SDL renderer with
  -DSNAP_SDL) or steps the port next to the real game (lock-step RAM diffs); tools/bizhawk/
  snap.sh, imgdiff.py, ramdiff.py, lockstep.py, ls_bisect.py, goto_stage.py, play.py.
  Method and results: docs/ground_truth.md (17 screens, 14 lock-step runs of 450-1300 frames).
- Logic fixes found against the real game:
  - Checkpoints: 0x060487FC (stage tick) was not ported, so a death never restarted at the
    checkpoint Sho had reached (shinobi.c Checkpoint_Update).
  - Death after a fall started the death animation at frame 7: Player_OnActionChange kept four
    special cases that are dead code in the original (it tests +0x22 after overwriting it).
  - 7-2 boss Koryu: the "attacking" bit is in the flags word +2 (was +4); the breath
    alternation 0x060F4D00 is a 32-bit variable (the port toggled the high half).
  - HBlank line table: converted words at 0x0601BC58 (was 0x0601CC58: every frame with a
    per-line effect the port wrote 0x840 bytes over character table entries 187-398).
  - Effects' current pointer 0x060F3FD4 (was 0x060F33D0 = the projectile loop index); loop
    indices 0x060F33D0 / 0x060F3FD8 / 0x060F4BE0 kept like the original.
  - Scroll_Update writes the NBG0-2 scroll shadows and runs the zoom 0x0604455A (sprite / NBG
    zoom around Sho; its factor is only set by SHINOBI's debug menu, so it stays 1.0).
  - Boot_Frame: frame counter and VDP2 "shadow changed" bits at VBlank-IN, after the frame (the
    phase the real game is in between frames).
- Rendering fixes (both renderers unless noted):
  - VDP2 colour calculation: sprites (CCCTL / SPCTL condition, ratio registers; result
    top * (31 - r) / 32 + second * (r + 1) / 32) and NBGs (per character / CRAM MSB, SFCCMD):
    6-1's translucent clouds, the cloud over 1-1's moon, the dim title-menu items, the 31/32
    level of every sprite.
  - Special priority per character (SFPRMD 0x15 set by the boot): 2-1's grass in front of Sho.
  - Static layers on 2x1 / 2x2 planes: plate address aligned to the plane (the VDP2 ignores the
    map register's low bits): 1-1's mountains / city-lights band showed a wrong page.
  - Line scroll: 16.16 values added before taking the pixel (9-1's back wall was 1 px off).
  - Back screen colour from the VDP2 (BKTA) instead of a fixed dark blue (SDL renderer).
  - soft_render: real VDP1 framebuffer (later sprite overwrites, any priority) then per-pixel
    VDP2 composition (top / second screen).
  - SDL renderer: sprite layer targets premultiplied; negative colour offsets read back and
    applied on the CPU when the renderer has no REV_SUBTRACT (7-2 / 8-1 tint was missing on
    SDL's software and some OpenGL renderers).
- Remaining (docs/ground_truth.md): VDP1 distorted-sprite rasterisation (1-px edges on rotated
  / slanted sprites), CD load times (the real game spends 70-200 frames in a stage load, the
  port one), the real pad being read one frame later (SMPC), sound driver RAM not modelled.

## 0.19.0 — 2026-10-04
- Movies: video clock now follows the audio actually played (ENDING.CPK drifted
  seconds out of sync); main loop paced at exactly 60 Hz on the performance counter
  (the integer 16 ms step ran the game at 62.5 fps).
- Settings file `shinobi.ini` next to the executable (config.c, `--config FILE`), created with
  commented defaults when missing: window scale 1..6 (default 3), desktop fullscreen, integer
  scaling, vsync, bilinear filter, aspect native / 4:3, master / music / SFX volume,
  screenshot format, keyboard and controller bindings per Saturn button, stick-as-D-pad and
  dead zone. Robust parser (BOM, CRLF, quotes, comments, range clamping, warnings);
  unit test tests/test_config.c (no SDL).
- Window: resizable (letterboxed through a 320x224 frame target + SDL logical size),
  Alt+Enter / F11 toggle desktop fullscreen (cursor hidden), Windows per-monitor DPI aware,
  initial size shrunk to fit the desktop.
- F12 screenshot: 320x224 PNG (built-in writer) or BMP in `screenshots/` next to the exe.
- Input: keyboard / controller mapping from the ini (several keys per button, triggers and
  stick directions bindable); Alt+Enter no longer presses Start; port 2 also filters
  opposite directions. 2 controllers and hot-plug unchanged. Esc never quits.
- Audio: Audio_SetVolumes (master, music + movie sound, SFX); output unchanged at 100 %.
- Stage audit (tests/audit_stages.c, not part of the suite; PNGs in docs/screenshots/audit/):
  every stage index / checkpoint played with a scripted pad. Fixes from it:
  - Scripted pad 0x060463DC ported (player flags 4 bit 0x10, was a TODO): 5-1 jump out of the
    water onto the mine-cart track (Sho fell back into the water and died forever), 5-2 lift
    (Sho stuck at the start), end-of-stage walk-outs, 1-1 drop-in jump.
  - `--stage N` / debug-menu "go to stage" from the title: stages other than 1-1 start at their
    checkpoint like after a stage clear (0x060252DC = 1) instead of dropping Sho from y = 1248
    (fell into 5-1 / 6-1 / 8-1 / 9-1 geometry).
  - Stage 2-2: statue-head fireball 0x1012 (0x0604FADE: aimed beam, hurts Sho, floor blast,
    debris, burning patch) and rock falls 0x100E (0x0604E6AC) ported: the heads never fired and
    the rock falls after the heads / on impacts were dropped.
  - Item kind 8 (mine, 0x06063078) and effect 5 (0x060627C8) ported (were "not ported").
  - VDP2: layers the stage does not stream (control block +0x20 bits 0x10..0x40) are drawn from
    their VDP2 name tables through the SclConfig plane / plates (both renderers): no more black
    holes in the scrolling cloud bands of 1-2 / 6-2 / 8-1 and the 9-1 back wall (their MAP
    layouts hold extra or 0xFF pages the hardware never displays).

## 0.18.0 — 2026-10-04
- PC debug / cheat menu (debug_menu.c, not in the Saturn game): F1 or controller
  Back/Select opens it and pauses the game. Lives, energy refill / infinite energy,
  shurikens (+/-10, infinite), ninjutsu, "finish the stage" (posts a stage clear like a
  boss death), go to any stage 1-1..9-2 (from the title too).

## 0.17.0 — 2026-10-04
- All enemies ported: regular types 0xA (stone giant), 0xB (bird), 0xC (ceiling golem),
  0x19 (sled launcher); bosses 0x21 (2-1), 0x1A (3-2), 9 (6-2), 6 (7-2 Koryu), 0x17 (8-2),
  0x1F (9-2 Kazuma, orbs + NYORAI finish -> ending). No "not ported" left in any stage.
- Ninjutsu: player cast 0x060437C0, effect state machine 0x06063554 (ZIRYU, BISYAMON,
  burst/shield, NYORAI), magic kinds 0-6 (shield, damage, heal, armed sword, boss kill).
- Player stubs ported (on-hit, command buffer, terrain damage, afterimages, sword clink,
  shuriken fan, third slash, hang posture); __modsi3 (0x0600A604) fixed at its callers.
- LZ decompressor fix: header holds word count - 1 (A.BIN 0x06004EE8); SHINOBI.dec.bin
  regenerated (last word was lost), Sho sprites no longer lose their last 2 pixels.

## 0.16.0 — 2026-10-04
- Stage frame hooks: SHINOBI 0x0606BCD0 (17 per-stage handlers, table 0x060B10CC),
  per-stage hook inits 0x0606C618 (table 0x060B1114), layer animation (line/cell scroll,
  NBG1 wave), HBlank line table (per-line NBG2 scroll + colour offset) in both renderers.
- Stage 4-2: ledge colours fixed (sprite banks 0-4 loaded by 0x0606BF12), boss life bar.
- Object handlers 0x1007-0x1050 all ported (lanterns, gates, crumbling blocks, statue
  heads, turrets, crates, ropes, carts, elevators, rockfalls, collapsing walls...), fx
  effects 0x08/0x09/0x0A-0x0E.
- Enemies: types 2, 5, 7, 0xF, 0x16, 0x22, 0x23, 0x24, soldier subtypes 5/6/9, bosses
  0x15 (2-2) and 0x18 (1-2) with their projectiles.
- Remaining: enemies 0xA, 0xB, 0xC, 0x19; bosses 0x21, 0x1A, 9, 6, 0x17, 0x1F; ninjutsu.

## 0.15.0 — 2026-10-04
- Sound: all 10 .SND banks parsed (area map + word-LZ blocks, A.BIN 0x06004C52); SBL
  sequencer + TON voice mixer (32 voices, 8 sequence slots) in sfx.c. System and stage
  sound effects play, plus sequenced tunes: stage music, stage start/clear, death.
  No DSP reverb / LFO yet.
- HUD: status bar values (life bar + HP ticks, shurikens, 1-up tokens, lives, ninjutsu
  icon), SHINOBI 0x06061A10; boss bar routine ported.
- Stage 4-2: ledges 0x1025 (scrolling floor), beams 0x1026, lightning halo 0x1027,
  lanterns 0x1028; boss (enemy 0x20: teleport, homing orb to reflect, lightning, clone)
  -> stage clear. Known gap: ledges drawn as black silhouettes (sprite colour banks 0-4
  empty), stage hook 0x0606D99E not ported.

## 0.14.0 — 2026-10-04
- All 18 per-stage inits ported (table 0x060A8B0C, Stage_InitLayers 0x060471D4,
  Layer_FillVram/PutTile, NBG3 status-bar map); every stage loads and the player lands
  (except 4-2: floor objects 0x1025/0x1026/0x1028 not ported yet).
- VDP2 map word decode fixed (H/V flip, palette bank, special priority): stage 1-1 ground
  no longer garbled; NBG3 status bar drawn; software reference compositor (soft_render.c).
- Title screen complete: all 18 steps, options, sound test, cheats, mono/stereo; VDP2
  sprite colour calculation dims unselected menu items.
- End screens complete: stage-clear tally (score/lives bonus), continue / game over,
  staff roll.

## 0.13.0 — 2026-10-03
- The game now boots like the Saturn: SEGA logo -> title screen (with the CD-DA title
  music) -> Start -> "STAGE 1 START" movie -> intro movie -> stage 1-1 (the real one:
  file STAGE7_1, the bamboo forest) — DIRECTOR.PPB and SHINOBI.PPB are swapped at
  0x06040000 as on the hardware (A.BIN main loop 0x06004038, resource queue).
- DIRECTOR ported: scene system, SEGA logo, title flow (menus partly), stage start,
  new game / continue setup, story movies, font, stage-clear tally entry; SHINOBI's
  mode dispatcher (load stage, frames, bonus tally, game over, return to DIRECTOR).
- Movies: native Sega FILM/Cinepak player (bit-exact vs ffmpeg on all 26 .CPK) with sound.
- Audio: SDL audio output, CD-DA tracks ripped by tools/rip_cdda.py (TRACK02/03.WAV).
- Discovery: stage index 0 = game stage 1-1 = file STAGE7_1; the level ported so far
  (file STAGE1_1, harbour) is game stage 7-1.
- Options: --stage N (boot straight into stage index N), --skip-intro, --assets DIR.
- Known gaps: title menus/options (steps 6+), tally / continue / game over screens are
  partial, other stages' per-stage init (stage 1-1 ground layer colours are wrong),
  sound effects (SCSP) still silent.

## 0.12.0 — 2026-10-03
- Rendering: full sprite submission (A.BIN 0x060068C0 + SBL SPR_2DistSpr 0x0602905C).
  Every sprite is a VDP1 quad: scaling (waves, debris, statue fragments), rotation (now
  turning the right way), distorted sprites (trap doors), global sprite zoom, screen
  culling, SPR_2 draw-priority ordering, and VDP2 sprite/layer priorities (0x060472D2:
  e.g. sprites with priority register S0 pass behind the level layer).
- Screen fades: A.BIN colour-offset fader (sequencer 0x06007084, 0x060072AC, 0x06006FB8,
  0x06006FDE, …) — 16-frame fade-in at stage start, fade-out before a death reload or
  a stage change; drawn as VDP2 colour offset A.
- Fix: 0x06006F56 is Fade_Reset (not an object-list init as the old docs said).

## 0.11.0 — 2026-10-03
- Stage 1-1 is complete as a level: Sho drops in from the sky at the original start
  (checkpoint table 0x060A8D94), item crates (0x1002, written into the collision map,
  fall, break into debris and drop items), hanging boxes (0x1000), the 7-piece stone
  statue (0x1001), trap doors that release guard ninjas (0x1003), water waves (0x1005)
  and splash (0x1006). Screenshot: docs/screenshots/v0.11.0_stage.png.
- Stage end: fall death 0x06045EE4, exit zone 0x06045F64 (all stages), end sequence
  0x06046190 (death -> lives -1 and reload, clear -> next stage). On PC the next stage
  (1-2), the stage-clear screen, game over and DIRECTOR are not ported yet: the game
  restarts 1-1 instead.
- Renderer: map page textures are rebuilt when their cells change (crates).
- Known gaps: trap door drawing (distorted sprites), sprite scaling (waves, debris),
  item kind 8, fade in/out, sound.

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
