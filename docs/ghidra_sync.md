# Ghidra sync — putting the port's knowledge back into the Ghidra project

Status (2026-10-04): **tooling ready and validated on a copy; NOT yet applied to the real project**,
because `shinolegions/Shinobi Legions decomp.lock` (host Jarvis, user lwiss, 2026-10-04 10:41 CEST)
says the project is open in the Ghidra GUI, and the GhidraMCP bridge (port 8080) was not answering.
Close the project in Ghidra, then run `tools/apply_ghidra_names.sh` (see "Re-run").

## Which project / programs

| project | content |
|---|---|
| `shinolegions/Shinobi Legions decomp.gpr` (default target) | `SHINOBI.dec.bin` (A.BIN block 0x06004000-0x0603FFFF + overlay 0x06040000-0x060EA2A9), `DIRECTOR.dec.bin` (A.BIN 0x06004000-0x0603FFFF + overlay 0x06040000-0x06087DAB), plus `track1.iso`, `SHINOBI.PPB`, `DIRECTOR.PPB` (skipped) |
| `projet/shinolegions.gpr` | **empty** (no program imported, NEXT-ID 0) — nothing to sync there; select it with `PROJECT_DIR=projet PROJECT_NAME=shinolegions` once programs are imported |

There is no separate `A.BIN` program: the A.BIN rows of the CSV are applied to **both** overlay
programs (each carries a copy of A.BIN). If an `A.BIN*` program is added later, the script handles it
(role `A.BIN`).

## Method

1. **Mining** — `tools/mine_names.py` (deterministic, writes only `docs/ghidra_names.csv` and
   `docs/ghidra_names_conflicts.csv`). Sources, by priority:
   - `c_def` (100): C function definition whose trailing / preceding comment starts with the original
     address (`/* 0x0606BCD0 */`, `/* A.BIN 0x06009254: ... */`); `c_def_loose` (95): `Name 0xADDR:` or
     `Projectile 2 (0x06069CAC): ...` (rejected when the comment says "inline in", "part of", "called
     from", or is a `----` section header over a lowercase static helper);
   - `c_proto` (90) prototypes; `doc_table` (80) `| 0xADDR | \`Name\` |` rows of the docs;
     `c_call` (70) `Foo(...); /* 0xADDR */` call-site annotations (dropped when the same name is
     annotated with several addresses, or is a macro / register-like name);
   - `doc_prose` (60) `` `Name` 0xADDR `` in jump_table_functions.md, director.md, hud.md, sound.md,
     ninjutsu.md, ground_truth.md (jump_table_functions.md overlay addresses before the
     "Pad input FOUND" section refer to the raw .PPB and are ignored);
   - `c_data` (50) `#define NAME 0x0602526Au`, `doc_data` (45);
   - `c_sub` (10) `sub_0600A604` / `FUN_` style names: create the function, no name.
2. **Program** — address < 0x06040000 → `A.BIN`; overlay → an explicit `SHINOBI`/`DIRECTOR` just
   before the address, else the file (`director*.c`, `movie*.c`, `director.md` → DIRECTOR, rest →
   SHINOBI). director.md addresses with no explicit overlay go to SHINOBI when SHINOBI already knows
   the address or the name is a gameplay module (`Player_`, `Game_`, `Stage_`, `Enemy_`, `Obj_` …).
3. **Conflict resolution** (one name per program/address) — functions: name style first
   (`Module_Verb` > other > index-based `EnemyHandler_0006`/`abin_06005BB4`), then source priority,
   then number of files agreeing, then latest file mtime. Data: priority, then descriptive (> 3 chars),
   files, mtime. The losers go to the `aliases` column (and into the Ghidra comment). Names that end
   up on several addresses are qualified: public-style names keep the plain name on the best-attested
   address and get `_<ADDR>` elsewhere; file-local statics get the C file stem
   (`stage_inits_init_12` / `stage_hooks_init_12`, `enemy_boss1a_KIND` / `enemy_boss09_KIND`).
   All names are Ghidra-safe (`[A-Za-z0-9_]`).
4. **Apply** — `ghidra_scripts/ApplyPortNames.java` (headless, one run per program):
   - functions: existing → rename if the current name is default (`FUN_`, `LAB_`, `sub_`, `f_` …,
     source DEFAULT/ANALYSIS) or was set by a previous run (source IMPORTED), or is index-based
     (`EnemyHandler_0B`, `ProjHandler_05`, `ObjHandler10_xx` from CreateMissingFunctions.java) while the
     port has a real name. Human names (USER_DEFINED) are **kept**; the port name is added as a
     secondary label and in the comment. Missing → disassemble + create function where the bytes are
     not defined data. An address inside another function's body → split it out only if something
     `bsr`/`jsr`s to it, else a secondary label + PRE comment ("inside").
   - plate comment `[port-sync] … [/port-sync]` = C file:line, C function, the source comment, other
     port names; replaced (not duplicated) on each run.
   - data: primary label (DAT_/none → port name), EOL comment `[port] …`.
   - `bss=1` (default): creates uninitialized blocks `BSS` (end of image .. 0x060FFFFF) and
     `WRAM_H_LOW` (0x06000000-0x06003FFF) so the work-RAM globals get labels (otherwise 171 SHINOBI / 27
     DIRECTOR data rows would have no memory to live in). Use `BSS=0` to leave the memory map untouched.
5. **Verify** — `ghidra_scripts/VerifyPortNames.java` (read-only): every CSV row of the program must
   have its name as a symbol at the address and a `[port…]` comment; prints a sample + totals into
   `notes/ghidra_sync/verify.txt`.

## Counts

CSV (`docs/ghidra_names.csv`, 1312 rows): A.BIN 133 functions (2 unnamed) + 91 data;
SHINOBI 689 functions (7 unnamed) + 264 data; DIRECTOR 91 functions + 44 data.
78 addresses had several candidate names (35 functions, 12 of them same-priority ties),
see `docs/ghidra_names_conflicts.csv`.

Dry run on a copy of the 2026-10-03 18:44 save of the real project
(`notes/ghidra_sync/dryrun_20261004/`; A.BIN rows counted in each program):

| program | fn renamed | fn created | fn same | fn kept (human) | fn inside other fn | fn comment only | data renamed | data new label |
|---|---|---|---|---|---|---|---|---|
| SHINOBI.dec.bin | 516 | 23 | 208 + 22 index-equivalent + 12 compatible | 13 | 19 | 9 | 338 | 17 |
| DIRECTOR.dec.bin | 104 | 15 | 74 + 5 compatible | 6 | 18 | 2 | 127 | 8 |

Verify after the dry run: SHINOBI 1177/1177 rows OK, DIRECTOR 359/359 OK. A second run is a no-op
(everything "same"), so the script is safe to re-run after the port evolves.

## Need a human decision

**Kept human names that disagree with the port** (port name added as secondary label):

| program | address | Ghidra (kept) | port |
|---|---|---|---|
| SHINOBI | 0x06004038 | Boot_And_MainLoop | Boot_Init |
| SHINOBI | 0x0604455A | Camera_UpdateSpriteZoom | Zoom_Update |
| SHINOBI | 0x060451C8 | Camera_PushFromPlayer | Player_ClampToScreen |
| SHINOBI | 0x06045D1E | Game_StateMachine | Magic_Apply |
| SHINOBI | 0x06046B14 | Map_PutCell | Layer_PutTile |
| SHINOBI | 0x0604879C | Player_SetCheckpoint | Stage_SpawnPlayer |
| SHINOBI | 0x0604B7AC | Obj_BlockShurikens | ObjF_BlockShurikens |
| SHINOBI | 0x0606078C | Obj_SwordClang | ObjF_SwordClang |
| SHINOBI | 0x06061062 | Projectile_FindTarget | Proj_FindTarget |
| SHINOBI | 0x0606BA08 | Game_StageClearTally | Game_StageClearBonus |
| SHINOBI | 0x0606BAA4 | Game_ReturnToDirector | Game_BackToDirector |
| SHINOBI | 0x06080808 | Obj_ClampToStageX | Guard4_ClampX |
| SHINOBI | 0x06097130 | Enemy_SolveJump | EM_SolveJump |
| DIRECTOR | 0x060042B8 | Sys_CheckSoftReset | Sys_SoftReset |
| DIRECTOR | 0x06007CDE | Snd_Play | Sfx_Lookup |
| DIRECTOR | 0x06008CC4 | Cpk_Reset | Mode_EnterIdle |
| DIRECTOR | 0x06040596 | Director_SndDefaultMap | Director_SetMenuSoundMap |
| DIRECTOR | 0x06040AE6 | Movie_BeginCpk | Movie_Prepare |
| DIRECTOR | 0x06040E18 | Options_CheckInvincibleCheat | Options_InvincibilityCheat |

Suspicious ones: 0x06045D1E (Game_StateMachine vs Magic_Apply), 0x06008CC4 (Cpk_Reset vs
Mode_EnterIdle), 0x06007CDE (Snd_Play vs Sfx_Lookup; SHINOBI.dec.bin already calls it Sfx_Lookup).
A.BIN is named independently in the two programs: after the run the A.BIN halves differ only on
these kept names.

**Same-priority ties in the port** (two C files name the same routine differently; the chosen one is
first): A.BIN 0x06009254 vdp2_dirty / shadow_mark; 0x0600A55C rnd_div / rnd_div10;
SHINOBI 0x06046B14 Layer_PutTile / Map_PutCell; 0x0604B7AC ObjF_BlockShurikens / Obj_BlockShurikens;
0x0604E12C Debris_Spawn / Leaf_Spawn; 0x0605817C Obj_CarryEnemies / ObjF_CarryEnemies;
0x060601BA ObjF_SwordHurtsPlayer / Obj_HurtPlayer; 0x0606078C ObjF_SwordClang / Obj_SwordClang;
0x0606081A ObjF_BreakParts / Obj_Burst6; 0x06060E8E Proj_Hitbox / EM_ProjHitbox / ObjF_ProjHitbox;
0x06092DA4 Soldier_HideCheck / Soldier_SetHidden / hide_check; 0x06097130 EM_SolveJump / Guard4_SolveJump.
(These are duplicated static re-implementations in the port — unify the C names and re-run.)

**Port functions that Ghidra keeps inside a bigger function** (37: labelled + PRE comment, not
split — no call reference; they are state-machine steps / jump-table targets): DIRECTOR Movie_Update
(movie_setup_vdp2, movie_open_next, movie_play), Scene1_Title (Dir_NextStep, SoundTest_Init,
Title_NewGameDefaults, Title_Step1_LoadPbc, Title_Step3_Setup, Title_Step4_Loop, Title_Launch),
Scene5_ContinueGameOver (GameOver_StartMovie, Continue_*), SoundTest_Update+0xE; SHINOBI
ObjHandler_1001_Statue7Pieces (Statue_Master, Statue_Piece1..6), EnemyHandler_0004 (Guard4_Act,
Guard4_Dying), Guard4_Sub1 (Guard4_Sub1Act), Game_ModeDispatch (Game_Start, Game_InitScreens),
Game_LoadStage (Fade_StageIntro), EnemyHandler_01 (Soldier_Sub05/06/0C), EnemyHandler_0003
(Ninja3_Think), Stage_InitLayers (Hud_InitNbg3Map), FUN_060463dc (walk_right). Splitting them is a
manual choice (Create Function in the GUI).

## Re-run

```
# Ghidra GUI: File > Close Project (the .lock file must disappear), then from the repo root:
tools/apply_ghidra_names.sh
#   -> mines the port, backs up shinolegions/ to shinolegions_backup_<date>/ (gitignored),
#      applies to SHINOBI.dec.bin and DIRECTOR.dec.bin, verifies (notes/ghidra_sync/verify.txt)
# variants:
BSS=0 tools/apply_ghidra_names.sh                         # do not add the BSS / WRAM_H_LOW blocks
PROJECT_DIR=projet PROJECT_NAME=shinolegions tools/apply_ghidra_names.sh   # the (empty) shared project
APPLY=0 NO_BACKUP=1 tools/apply_ghidra_names.sh           # verify only
```

The script refuses to run while `<project>.lock` exists (`FORCE=1` only for a stale lock left by a
crashed / killed Ghidra). It needs bash + python3 + JDK 21 (`JAVA_HOME`, defaults to `~/w/jdk-21*`);
on Windows run it from Git Bash, it falls back to `analyzeHeadless.bat`. Headless runs take ~45 s per
program. A project owned by another user name is opened with `-Duser.name=<owner>`.

Per-row results: `notes/ghidra_sync/report.<ROLE>.tsv` (program, address, kind, csv name, action,
previous name). Undo: restore the backup folder (Ghidra closed).
