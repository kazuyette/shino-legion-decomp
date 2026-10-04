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

Default bindings (all game buttons can be changed in `shinobi.ini`, see Config):

| Action | Saturn | Keyboard | Controller (Xbox / PlayStation) |
|---|---|---|---|
| Move / crouch | D-pad | Arrows | D-pad or left stick |
| Sword (hold = guard) | B | X | X / Square |
| Jump (hold = higher) | C | C | A / Cross |
| Shuriken | A | Z | B / Circle |
| Ninjutsu | X | A | Y / Triangle |
| Guard | Y | S | LB / L1 |
| - | Z | D | RB / R1 |
| - | L / R | Q / W | LT / RT (L2 / R2) |
| Start | Start | Enter (or keypad Enter) | Start |

PC-only keys (fixed):

| Key | Action |
|---|---|
| F1 / controller Back (Select) | debug / cheat menu (pauses the game) |
| Alt+Enter or F11 | fullscreen (desktop resolution) on/off |
| F12 | screenshot: 320x224 PNG (or BMP) in `screenshots/` next to the executable |
| Esc | nothing in game (closes the debug menu); quit by closing the window |

Keyboard + controller 1 = Saturn port 1, controller 2 = port 2. Controllers can be plugged
in or out while the game runs. A+B+C+Start is the original soft reset.

## Config

`shinobi.ini` lives next to the executable (`--config FILE` to use another one). When it is
missing it is created with every option at its default value and commented; delete it to
get the defaults back. Changes are read at startup. Format: `[section]`, `key = value`,
`;` or `#` comments; unknown keys and bad values are reported in the console and the
default is kept.

| Section / key | Default | Meaning |
|---|---|---|
| `[video] scale` | 3 | window = scale x 320x224 (1..6), shrunk to fit the desktop; the window can be resized (letterboxed) |
| `[video] fullscreen` | 0 | start in desktop fullscreen |
| `[video] integer_scaling` | 0 | only whole zoom factors (sharpest pixels, black borders) |
| `[video] vsync` | 1 | sync to the monitor refresh |
| `[video] bilinear` | 0 | smooth upscaling instead of sharp pixels |
| `[video] aspect` | native | `native` = square pixels (320x224), `4:3` = stretched to 4:3 like on a TV |
| `[audio] master_volume`, `music_volume`, `sfx_volume` | 100 | 0..100 %; music also covers the movie soundtracks |
| `[misc] screenshot_format` | png | `png` or `bmp` (F12) |
| `[keyboard] A B C X Y Z L R Start Up Down Left Right` | see Controls | SDL key (scancode) names, comma separated, up to 4 per button: `Z`, `Space`, `Return`, `Left Shift`, `Right Ctrl`, `Keypad 8`... Names with `;` `#` `,` go in quotes (`";"`); `none` = unbound |
| `[controller] A B C ... Right` | see Controls | SDL GameController names (Xbox layout) for both controllers: `a b x y start leftshoulder rightshoulder leftstick rightstick dpup dpdown dpleft dpright misc1 paddle1..4 touchpad`, triggers `lefttrigger righttrigger`, stick directions `+leftx -leftx +lefty -lefty +rightx ...` (`-` = left/up) |
| `[controller] stick_as_dpad` | 1 | the left stick also drives the D-pad |
| `[controller] deadzone` | 37 | stick dead zone in % of its travel (5..95) |

Keyboard names are physical key positions (US layout names: on AZERTY the default `Z`
key is the one labelled W). Unknown names are reported as `[input] ... unknown key name`.
Bilinear, 4:3 and screenshots need render-target support (every SDL hardware renderer and
the software one have it). Unit test of the parser and image writers (no SDL):
`gcc -std=c11 -DCONFIG_NO_SDL -Isrc tests/test_config.c src/config.c src/screenshot.c`.

## Getting the game files / Obtenir les fichiers du jeu

**English.** The port ships without any game data. Players build the `assets/` folder from
their own *Shinobi Legions (USA)* disc image (Saturn product T-2301H): a `.cue` + `.bin`
(single `.bin` or one `.bin` per track), or a plain `.iso` (data track only: no CD audio,
so the title / ending music is missing). Three ways:

1. put the `.cue` and its `.bin` file(s) next to the executable and start it: when the
   asset folder is missing or incomplete, it offers to extract them (Yes/No box);
2. drag the `.cue` (or `.bin` / `.iso`) onto the executable: extraction, then the game starts;
3. command line: `shinobi_legions_pc --extract "Shinobi Legions (USA).cue" [--assets DIR]`
   (console progress; exit code 0 = ok, 2 = ok but music missing / different, 1 = error).

The files (115, about 400 MB) go to `assets/` next to the executable (or `--assets DIR`).
Each one is written as `NAME.part`, checked against the manifest of the original disc
(size + CRC-32, `src/asset_manifest.h`) and renamed. Errors name the problem: not a Saturn
image, wrong disc, other region (Japanese *Shinobiden*, European *Shinobi X*), bad /
truncated dump (the files that differ are listed and not installed). A music track that
differs from the reference rip (other audio read offset) is installed with a warning.

**Français.** Le portage est distribué sans aucune donnée du jeu. Le joueur crée le dossier
`assets/` à partir de l'image de son propre disque *Shinobi Legions (USA)* (référence
Saturn T-2301H) : un `.cue` + `.bin` (un seul `.bin` ou un `.bin` par piste), ou un `.iso`
simple (piste de données seule : pas d'audio CD, donc pas de musique au titre ni à la fin).
Trois façons :

1. mettre le `.cue` et son ou ses `.bin` à côté de l'exécutable et le lancer : si le dossier
   des fichiers manque ou est incomplet, il propose de les extraire (boîte Oui/Non) ;
2. faire glisser le `.cue` (ou le `.bin` / `.iso`) sur l'exécutable : extraction puis lancement ;
3. en ligne de commande : `shinobi_legions_pc --extract "Shinobi Legions (USA).cue" [--assets DIR]`
   (progression dans la console ; code de sortie 0 = ok, 2 = ok mais musique absente /
   différente, 1 = erreur).

Les fichiers (115, environ 400 Mo) sont écrits dans `assets/` à côté de l'exécutable (ou
`--assets DIR`), chacun vérifié (taille + CRC-32 du disque original) avant d'être installé.
Les erreurs indiquent la cause : image non Saturn, mauvais disque, autre région (*Shinobiden*
japonais, *Shinobi X* européen), image abîmée ou tronquée (fichiers en cause listés).

Developers / développeurs:
- `src/extract.c` (plain C, no SDL) reads the cue sheet (FILE / TRACK / INDEX / PREGAP /
  POSTGAP, MODE1/2352, MODE1/2048, MODE2, AUDIO), the ISO9660 tree of the data track, copies
  the files, decompresses `DIRECTOR.PPB` / `SHINOBI.PPB` to `.dec.bin` (`lz.c`) and rips CD-DA
  tracks 2 and 3 exactly like `tools/rip_cdda.py` (the `TITLE.PCM` / `ENDING.PCM` ISO extents
  give the LBAs; the DW0536 cue's track-3 INDEX 01 is 150 frames late).
- `src/asset_manifest.h` is generated from the reference set:
  `python3 tools/gen_manifest.py [pc-port/bin/windows/assets] [pc-port/src/asset_manifest.h]`.
- Test (no SDL, run from `pc-port/`): `gcc -std=c11 -O2 -Isrc tests/test_extract.c src/extract.c
  src/lz.c -o test_extract && ./test_extract [--iso ../extracted/track1.iso] [IMAGE.cue] [REF_DIR]
  [OUT_DIR]` extracts the real image to a temp folder and compares all 115 files byte for byte
  with `bin/windows/assets`.
- `--no-asset-check` skips the startup check (development asset folders).
- Player package (no game data): `python3 tools/make_dist.py --exe build/shinobi_legions_pc.exe
  --sdl SDL2.dll` -> `pc-port/dist/ShinobiLegionsPC-<version>-win64.zip` (exe, SDL2.dll,
  `packaging/README.txt` FR+EN, `packaging/NOTICE.txt`, CHANGELOG); `--dry-run`, `--self-test`.

## Assets

The executable reads the disc files from an `assets/` folder (default: `assets/` in the
working directory when it holds `A.BIN`, else `assets/` next to the executable; or
`--assets DIR`). It is built by the extractor above; the layout is the one of `extracted/`
(`python3 tools/iso_extract.py extracted/track1.iso NAME extracted/NAME` extracts one file
from the data track):

| file(s) | used for |
|---|---|
| `A.BIN` | system program, loaded at 0x06004000 at power-on (required) |
| `DIRECTOR.dec.bin` (or `DIRECTOR.PPB`) | front-end overlay at 0x06040000: logo, title, stage start, tally, continue... |
| `SHINOBI.dec.bin` (or `SHINOBI.PPB`) | in-game overlay at 0x06040000 |
| `SHOU.CHR` | Sho's graphics (LWRAM 0x00200000, loaded by DIRECTOR) |
| `TITLE.PBC`, `STGCLEAR.PBC`, `STAFFROL.PBC` | title, stage clear, staff roll pictures |
| `STAGEn_m.CHR/.MAP/.SPR` (n = 1..9, m = 1..2) | stage graphics/maps (game 1-1 = `STAGE7_1`, see docs/director.md §4.1) |
| `*.CPK` | movies (not played yet: the movie player is a stub, every movie is skipped) |
| `*.SND`, `TRACK02.WAV`, `TRACK03.WAV` | sound banks / CD-DA tracks (`TITLE.PCM` = track 2, `ENDING.PCM` = track 3) |

`X.dec.bin` is `X.PPB` already decompressed (`tools/ppb_decompress.py`); when it is absent
the `.PPB` is decompressed at load time. Without `A.BIN` nothing runs.

## Run

```
./build/shinobi_legions_pc [--assets DIR] [--config FILE] [--skip-intro] [--stage N] [--verbose] [--no-asset-check]
./build/shinobi_legions_pc --extract DISC.cue|.bin|.iso [--assets DIR]
./build/shinobi_legions_pc DISC.cue|.bin|.iso        # extract, then play (drag and drop)
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
Default keys: arrows = D-pad, Z/X/C = A/B/C, A/S/D = X/Y/Z, Q/W = L/R, Enter = Start
(see Controls / Config).
The input chain is the real one (see `pad.*`): pressed buttons are logged to
the console, and A+B+C+Start triggers the original soft reset.

## Ground truth (real game)

The port is checked against the real game running in BizHawk (tools/bizhawk/, Lua file bridge):
`tests/snap_compare.c` loads memory dumps of the real game into the port (same RAM layout) and
renders them or steps the port next to the real game; see `docs/ground_truth.md`.

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
