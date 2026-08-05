# The 10 entries of the boot jump table @ 0x06004118

Quick pass over each target (see `docs/entry_point_notes.md` for the table itself).
Use `tools/sh2dis.py A.BIN <file_offset> <len>` to reproduce/extend any of these.

| addr       | verdict |
|------------|---------|
| 0x06004418 | real code — init routine, calls one BIOS/SGL trampoline (`JSR @R13` after loading it via `MOV.L @(d,PC)`) then does register/memory setup. Small, looks like a "wait for ready flag" loop (`BF` back-branch). |
| 0x06005130 | **BIOS/SGL call trampoline**, see `entry_point_notes.md` — dispatches through a fixed low-RAM vector at 0x06000344, args (R4,R5) vary per call site. This IS the callable "vector" itself, i.e. table entries may just be *thin wrappers*, not the real logic. |
| 0x0602A8C4 | real code — small dispatcher: loads 3 pointers, then a chain of `BRA`s into a block around 0x602A9xx that looks like a jump-table/switch (repeated `MOV #imm,R0` followed by `BRA` to the same landing spot 0x602A9C6 — classic "set result code, jump to common return" pattern). Likely an event/command dispatcher. |
| 0x0602A894 | real code — tiny: load a 16-bit flag via a pointer, test it, branch, either clear or return a value. Looks like a "get status flag" accessor. |
| 0x0602A8A8 | real code — same shape as above, one branch fewer: load a 16-bit flag, `TST`, return 0 or 1 in R3. Looks like a boolean predicate ("is X ready/done"). Sits right before 0x0602A8C4 in the file — these three are a small cluster of related accessor functions. |
| 0x06029B40 | real code — pushes R8–R14 + PR (`STS.L PR,@-R15`), allocates a 20-byte stack frame, loads a stack arg (`MOV.W @R15,R11`) then does `R11 <<= 14` (`SHLL8`+`SHLL2`×3) and `AND`s it with a mask — classic "shift a small field into its bit position within a hardware register value" idiom. Consistent with building a VDP1/VDP2 register value from a caller-supplied parameter. `sh2dis.py` now decodes the SH-2 shift/rotate class (`SHLL/SHLR/SHAL/SHAR/ROTL/ROTR/DT/STS.L PR`, etc.), added this session. |
| 0x0602524C | **not code** — 32+ bytes of literal `0x0000`. This is a data pointer (buffer address), not a function pointer, despite living in the "jump table". Likely a workspace/state buffer passed to one of the other init calls. |
| 0x06005840 | real code — five `JSR` calls through the same function pointer (R14, loaded once from 0x6005944) with descending bitmask args (4, 8, 16, 32) and descending priority args (4, 3, 2, 1), preceded by one `JSR @R3` (different function, arg R5=5). Shape matches "for each of N channels/units: init(mask, priority)" — plausible candidate for SCU DMA channel setup or SGL sprite/queue priority init. |
| 0x060057F0 | real code — calls a function then writes the 16-bit constants **0x00DF and 0x013F** (223 and 319 — i.e. 224×320 minus one) into two literal-pool slots, then two more `JSR`s and a tail-call (`JMP @R3`). 320×224 is the standard Saturn/NTSC display resolution, so **this is very likely the VDP2 display-resolution init call** — strong anchor point for finding the graphics init sequence. |
| 0x06005B0A | real code — no function-pointer calls, just writes zero (R4=0) into a struct at offsets 16/32/48/64/80/96/112 through several base registers (R5,R6,R3,R7 each incremented and used as `@Rn`). Looks like zeroing a fixed-size state/control-block struct — an object constructor, not a BIOS call. |

## Working theory

This table is a **subsystem init dispatch table** called once at boot (likely by the
loop hinted at 0x0600401C region, still needs to be traced): clear-BSS → load R11-R14
literal pool region (SAME range as this table, worth double-checking they're not the
same 4 of the 10) → walk the 10 entries, calling each with no/default args. Mix of
real BIOS trampolines (entry 2), struct zeroing (entry 9), and what's likely VDP2
resolution setup (entry 8) is consistent with a generic "engine boot: for each
subsystem, run its init()" table — exactly the shape you'd expect from a Saturn
game linked against SGL, where `SGL_INIT`-style code sets up VDP1/VDP2/SCU/SCSP one
at a time.

## Next concrete step

Disassemble 0x06029B40 properly — needs `sh2dis.py` extended with the SH-2 shift/rotate
instruction class (opcodes `4nXX` where XX ∈ {00,01,08,09,0A,0B,...}: `SHLL`, `SHLR`,
`SHAL`, `SHAR`, `ROTL`, `ROTR`, `DT`, etc.) since several `.word`s in that function are
almost certainly shifts, not junk.

## Ghidra MCP now connected (2026-08-04)

GhidraMCP (LaurieWired/GhidraMCP 1.4 plugin + bridge) is wired up and working, giving
direct `decompile_function_by_address` / `get_current_function` / xref tools instead of
hand disassembly. Use this from now on for anything beyond a quick spot-check.

Notable gotcha for future setup on this machine: `pip install mcp` now defaults to the
v2.0.0 SDK (major rewrite, released 2026-07-28) which dropped `mcp.server.fastmcp` —
`bridge_mcp_ghidra.py` needs the old v1.x API, so the fix was pinning
`pip install "mcp[cli]<2"` on the specific Python interpreter the MCP config actually
invokes (there were 3 Pythons on PATH; had to match the one Claude's config launches).

## FUN_06004038 is the real boot+main-loop function

Decompiled via Ghidra (not just disassembly): `FUN_06004038` runs the full init sequence
(walking well past the 10-entry table above, up to ~0x4194, then writing directly into
recognized hardware registers — Ghidra auto-labeled `SCU_D1EN` at 0x0600415c, confirming
SCU DMA channel 1/2 are disabled here as part of SGL-style boot) and then drops into an
infinite `do { ... } while(true)` — this is the per-VBlank main loop, not a one-shot
init walker. Inside the loop it re-calls table entries 0 (0x0602A8C4, dispatcher),
1 (0x0602A894, per-channel flag accessor, called with masks 4/8/0x10/0x20 = 4 channels),
2 (0x0602A8A8, "commit"/flush, no args) and 3 (0x06029B40, builds a register value from
a shifted parameter) once per channel, operating on 4 channel-descriptor structs 0x10
bytes apart at `PTR_DAT_06004288`.

Two more calls happen every frame after the channel processing, straight function calls
(not through the table):

- **`FUN_06007084`** — re-read via the Ghidra decompiler (much clearer than the hand
  disassembly). It's a **3-state hardware-request sequencer**, state var at
  `DAT_0600711c` (renamed from the earlier guess `DAT_0602510c` — that was this same
  variable, just a different symbol name than first assumed):
  - **state 0 (idle)**: if a request flag (`DAT_06007128`) is set and no request is
    already active (`DAT_06007124 == 0`), latch the request — copy the flag into the
    "active" slot, copy a 4-byte parameter pair from `DAT_0600712c` into `DAT_06007130`,
    clear the request flag. Pure "accept a new request" transition, no state change yet
    (state advances via `DAT_0602510c`/next-state byte set elsewhere).
  - **state 1 (busy-A)**: polls `FUN_06007960(param)` — when it returns nonzero (done),
    decrements a retry/repeat counter, jumps to the next queued state (a byte stashed
    at `DAT_06007120`), and fires a completion call `FUN_060077a4(DAT_06007138)`.
  - **state 2 (busy-B)**: calls a kickoff function `PTR_FUN_06007140()` every time
    through, then polls `FUN_06007a5e(param)` — same completion pattern as state 1 on
    success.
  - Always calls `FUN_0600718a()` first (gate/toggle, see below) and `FUN_060078ec()`
    last (a shared "tick" call, likely display-list flush based on where else it's used).

  This is the shape of a generic **request → poll-until-done → chain to next state**
  driver wrapper — used for some one-shot hardware operation that takes multiple frames
  to complete (issue, then poll each frame until acknowledged). `FUN_06007a5e` is reused
  by `FUN_0600718a` too (see below), so it's a shared low-level "is operation X done"
  poll, not specific to this one call site. Not yet pinned to a specific subsystem
  (screen transition and CD-block command are both still plausible); the VDP1/VDP2
  angle is slightly favored since `FUN_060078ec` (the closing tick call) is also reached
  as a tail-call from the boot table's channel-processing context.

  `FUN_0600718a` (the gate, called from the top of `FUN_06007084`): reads a flags word
  at `DAT_060071b4`. Bit0 = "enabled/requested" (must be set or the whole function is a
  no-op). If bit2 is also set, it just clears bit2 and exits (one frame's grace/defer).
  Otherwise it clears bit0 (self-disarming — this only fires once per request) and, if
  two other conditions are clear (`DAT_06007240 == 0` and `DAT_0600723c == 0`), calls
  `FUN_060077a4(DAT_06007244)` then polls `FUN_06007a5e(DAT_06007250)` — **and toggles
  bit1 of the same flags word based on the poll result** (sets it if it was clear,
  clears it if it was set). Bit1 acting as a flip-flop toggled on successful poll is a
  strong signature of a **buffer-swap acknowledgement** (e.g. "which VDP1 frame buffer
  is currently displayed" flag) — this is the best lead so far for what subsystem
  `FUN_06007084`/`FUN_0600718a` actually drive.

- **`FUN_0600863e`** — confirmed via Ghidra decompile: **the per-frame SCSP (sound)
  channel scheduler.** Iterates up to 8 channel slots (12 bytes each, in an array at
  `PTR_DAT_060086e0`/`...d8`/`...dc`), reads a per-slot state field (0/1/2/3) and
  dispatches: 0 = stop-channel busy-wait, 1 = reset volume (`0x7f,0`), 2 = set
  volume/pan from slot data, 3 = trigger a new note/sample via `PTR_FUN_0600897c`. Has
  an early-exit branch to a completely different handler `FUN_06008c08` when a flag at
  `PTR_PTR_060086e4` is set.

### Sound engine, fully traced (2026-08-04, via Ghidra decompiler)

- **`FUN_06008c08`** (the early-exit alt path): not a CD-DA/PCM switch as guessed — it's
  a **"stop/find matching channels" command processor**. Mode flag at `*PTR_PTR_06008cb8`:
  mode 1 scans all 8 channel slots for one whose ID field equals `DAT_06008caa` and calls
  `PTR_FUN_06008cbc(slot)` on each match (search-by-sound-ID); mode 2 does the same but
  matches a different field against `DAT_06008cac` via `PTR_FUN_06008cc0(slot)`
  (search-by-group/priority, probably "stop all sounds in category X"). Always calls
  `PTR_FUN_06008cb4()` first (unconditional per-call poll/housekeeping). Resets the mode
  flag to 0 once no more matches are found — so this runs for a few frames after a
  "stop matching sounds" request until it's fully drained.

- **`FUN_06030a82`** (target of `PTR_FUN_0600897c`, the "trigger new note" call from the
  state-3 case above): takes 4 byte params (channel/note/volume/pan or similar). Checks
  queue space via `FUN_060314fe()`; if no room, returns 1 (busy/dropped). If there's
  room, writes the 4 params into offsets +2..+5 of the queue slot pointed to by
  `*PTR_DAT_06030b20`, and writes a command-tag constant (`DAT_06030b1e`) into the first
  2 bytes — classic **producer side of a command queue**, not a direct hardware write.

- **`FUN_060314fe`** (the queue-space check): confirms a genuine **ring buffer**: base
  pointer `PTR_DAT_06031558`, write cursor `PTR_DAT_0603155c`, entries are **0x10 (16)
  bytes** each, buffer holds **0x70/0x10 = 7 entries**. Advances the cursor past
  occupied (non-zero tag) slots up to the 7-entry limit; returns 0 (room available) or 1
  (full). This is a 7-deep sound-command queue — game code pushes "play"/"stop" commands
  into it every frame via the functions above.

- **Consumer search, dead end inside A.BIN**: checked Ghidra xrefs to both the ring
  buffer globals (`06031558`/`0603155c`) and the actual write-target pointer used by the
  producers (`06030b20`) — the only other reader is `FUN_06030a1a`, which turns out to be
  a third producer (pushes a no-param command, tag `DAT_06030b1a` — likely "stop"/"pause",
  vs. `FUN_06030a82`'s "play note" tag `DAT_06030b1e`). **No code in A.BIN drains this
  queue.** Strong conclusion: the Saturn's SCSP has its own onboard Motorola 68000
  running a separate sound driver program uploaded into sound RAM — that M68K driver is
  the actual consumer, and it isn't part of A.BIN at all (different CPU, different
  binary, probably uploaded via DMA from an `.SND`/`.PCM` file on the disc, or embedded
  and copied out at boot). To go further here you'd need to: (1) find the DMA/upload
  code in A.BIN that copies a driver blob to SCSP sound RAM (search for writes to the
  SCSP sound-RAM address range), and (2) disassemble that blob as M68K, not SH-2 — a
  different tool entirely. Marking the SH-2-side sound investigation as complete for now.

## Ghidra project renames (2026-08-04, GhidraMCP direct)

With GhidraMCP connected, functions are now renamed directly in the Ghidra project
database (`shinolegions/` — gitignored, not in this repo, lives only on the machine
running Ghidra) rather than just described here. Current renames, all confirmed via
decompilation:

| old name | new name | why |
|---|---|---|
| `FUN_06004038` | `Boot_And_MainLoop` | boot init sequence + the per-VBlank main loop, see above |
| `FUN_0600863e` | `Snd_ChannelScheduler` | 8-channel SCSP scheduler |
| `FUN_06008c08` | `Snd_StopMatchingChannels` | stop-by-ID / stop-by-group command processor |
| `FUN_06030a1a` | `Snd_CmdStopAll` | no-param queue command, tag `DAT_06030b1a` |
| `FUN_06030a82` | `Snd_CmdPlayNote` | 4-param (ch/note/vol/pan) queue command, tag `DAT_06030b1e` |
| `FUN_06030b24` | `Snd_CmdSetParamA` | 1-param queue command, tag `DAT_06030bd6` — exact meaning TBD |
| `FUN_06030b92` | `Snd_CmdSetParamB` | 1-param queue command, tag `DAT_06030c76` — exact meaning TBD |
| `FUN_06030c08` | `Snd_CmdSetParamC` | 1-param queue command, tag `DAT_06030c7a` — exact meaning TBD |
| `FUN_06030c80` | `Snd_CmdSetParam3` | 3-param queue command — exact meaning TBD |
| `FUN_060314fe` | `Snd_QueueHasSpace` | ring-buffer space check shared by all `Snd_Cmd*` producers |
| `FUN_06031388` | `Snd_LookupSfxDef` | index → (masked word, byte) table lookup, likely SFX definition fetch |
| `FUN_06009370` | `Sys_MemCopy` | generic byte-copy loop (corrects earlier "enqueue" guess from hand-disassembly — it's just memcpy, used to stage args on the stack before calls) |

### New cluster: object/sprite transform dispatcher (0x0602a900-ish region)

- **`Obj_SetTransformParam`** (was `FUN_0602a8c4`) — dispatcher taking 3 params, branches
  on a mode value read from `PTR_DAT_0602a970` (seen values: 1, 2, 4, 8, 0x10, 0x20 —
  bitflag-shaped). Each mode writes the two data params into different offsets of one of
  three structs (`0602a974`, `0602a988`, `0602a96c` — the first two have fields at
  +0x44/+0x48, suggesting fairly large ~0x50+ byte control blocks). Modes 1/2 also have
  extra gating logic and can fire a callback (`PTR_FUN_0602a984`/`PTR_FUN_0602aa30`).
  Working theory: generic "set field N of object/sprite M" — matches the shape of an
  SGL-style transform setter (position/rotation/scale channels selected by mode bit).
- **`Obj_SetReadyFlag`** (was `FUN_0602a8a8`) — trivial: if flag at `0602a8bc` is 0, set
  it to 1.
- **`Obj_ConsumeFlagAndStore`** (was `FUN_0602a894`) — takes a param; if the same flag is
  1, clears it to 0; unconditionally stores the param into `0602a8c0`. Shape suggests a
  producer/consumer handshake with `Obj_SetReadyFlag` (one raises, the other
  acknowledges-and-stores), but the two aren't called from the same place we've traced
  yet — still needs a caller to confirm the pairing.
- **Next step**: find what writes `PTR_DAT_0602a970` (the mode selector) to see how modes
  get chosen, and what calls `Obj_SetTransformParam` to see which frame/object data
  actually flows through it — see task list for continuation plan.

### Obj_SetTransformParam fully decompiled — struct layout resolved (2026-08-05)

Got the full decompile. Confirms and completes the theory above:

- **Modes 4/8/0x10/0x20** — exactly the 4 modes the real per-frame loop uses
  (`kObjectSlotModes` in `boot.c`) — write two fields each into **one shared**
  control block at `PTR_DAT_0602a96c`, no gating, no callback:
  - mode 4: full 32-bit `param_1`/`param_2` at offsets `+0x00`/`+0x04`
  - mode 8: full 32-bit `param_1`/`param_2` at offsets `+0x10`/`+0x14`
  - mode 0x10: **high 16 bits only** of `param_1`/`param_2` at `+0x20`/`+0x22`
  - mode 0x20: **high 16 bits only** of `param_1`/`param_2` at `+0x24`/`+0x26`
  - The high-16-only writes for 0x10/0x20 match SGL fixed-point convention
    (upper word = integer part) — likely angle/scale fields vs. the
    full-32-bit position fields at mode 4/8.
- **Modes 1/2** — write into two *separate* control blocks (`0602a974`,
  `0602a988`) at offsets `+0x44`/`+0x48`, with extra gating logic that can
  fire a callback through a function pointer (`PTR_FUN_0602a984`/
  `PTR_FUN_0602aa30`) — looks like a real SGL render/commit call. Not used
  by the observed 4-slot call site, not investigated further (real SGL
  hardware call, not portable as-is).

**Ported to pc-port**: `Obj_SetTransformParam`/`Obj_SetReadyFlag`/
`Obj_ConsumeFlagAndStore` now do the real thing in `pc-port/src/object.c`
(shared `ObjTransformBlock` struct matching the offsets above, plus the
ready-flag handshake) instead of stub logging. Modes 1/2 intentionally left
unhandled (see comment in `object.h`).

## Status (2026-08-04)

529 functions total in A.BIN per Ghidra's auto-analysis. ~20 identified and renamed so
far (this file + the boot table + the two clusters above). Remaining scope is large;
tracked as ongoing tasks rather than attempted in one pass.

**Correction**: the block around 0x06035000-0x06039000 is NOT game logic — checked three
functions there (`FUN_06035102`, `FUN_060351ac`, `FUN_06035138`) and they're CD-ROM/file
driver code (busy-wait loops on status bits, classic CDC-style polling). Don't prioritize
that range for "game logic" hunting.

**Better lead found: search by string, not by address.** `list_strings` turned up
game-specific text instead of library boilerplate:

- `SHINOBI.PPB` / `DIRECTOR.PPB` (0x0600edcc/0600edd8) — custom data files with a
  game-specific `.PPB` extension (not seen in the ISO listing under those exact names —
  likely embedded/packed, or on a subdirectory not yet checked). **`DIRECTOR.PPB` is
  loaded very early at boot** by `Load_DirectorPPB` (was `FUN_06004eac`) — confirmed via
  decompile, it's a retry-loop file read call `(1,1,0,0xffffffff,buffer,"DIRECTOR.PPB")`
  matching a standard Sega file-load signature. Called directly from
  `Boot_And_MainLoop` and also from `FUN_060042b8` (the function tentatively identified
  earlier as "pad/SMPC read" — that guess may need revisiting, since it also triggers
  this file load; could be a more general "boot phase 2" gate rather than pad-specific).
  `SHINOBI.PPB`'s only reference so far is inside `Load_DirectorPPB`'s own function
  body area (no separate loader found yet — worth another look).
- `"Hi! Come come everybody."` / `"Guu... I'm sleepinggguuu..."` (0x0600f2c4/f2e0) —
  attract-mode/idle flavor text (classic "get bored and taunt the player" pattern).
  Traced one level up: pointer table entry at `0600ab48` is written by
  `Idle_InitMessageSystem` (was `FUN_0600aaa4`), which zeroes a cluster of state globals
  and calls a couple of sub-inits. That in turn is called from `FUN_060096bc` — looks
  like a mode-table entry function (single global reset + one indirect call, returns 1)
  — plausibly "on enter idle/attract mode". Not fully confirmed; good place to resume.
- `` `$ver1.28 94/12/29SATURN(S) master` `` (0x06010326) — a Sega internal library
  version tag (standard `$ver` convention). Identifies a specific linked library
  component dated 1994-12-29, version 1.28 — worth a web search against known SGL/BIOS
  library version histories to pin down exactly which library this is.
- `"Cpk system is not initialized."` (0x06014444) — confirms Sega's Cinepak (CPK) video
  library is linked in, matching the `.CPK` files on the disc (opening/ending/demo FMVs).

**Next concrete steps**: (1) find `SHINOBI.PPB`'s loader — checked, it has **zero code
xrefs** in A.BIN, so it's either loaded data-driven from inside `DIRECTOR.PPB` itself
(not visible to static analysis of A.BIN) or unused in this build; (2) figure out what
`.PPB` actually contains; (3) confirm the idle/attract mode table hypothesis by finding
sibling entries near `FUN_060096bc`.

### Correction: `FUN_060042b8` is not pad/SMPC input — it's the stage reset handler

Re-examined with a clean decompile now that `Snd_CmdStopAll`/`Load_DirectorPPB` show up
by name in the disassembly (nice side benefit of renaming as we go — xrefs get more
readable). **Renamed to `Stage_ResetAndLoadDirector`.** It's a 3-way branch on a status
check (peripheral/CD ready, via `PTR_DAT_06004304`); the "not ready" and "special"
branches just bail or tail-call elsewhere, but the default path does a full stage
teardown: disable SCU DMA (writes to `SCU_D1EN` again), mask+write the **VDP2 `TVMD`
register** (`PTR_VDP2_TVMD_060043cc` — Ghidra auto-labeled this; TVMD = TV Mode, the
display-resolution/interlace register), call ~7 unidentified reset function pointers,
`Snd_CmdStopAll()`, then **reload `DIRECTOR.PPB`** and reset a handful of state globals.
This means `DIRECTOR.PPB` isn't just a one-time boot load — it's **reloaded on every
stage transition**, which fits "DIRECTOR" being per-stage script/event data rather than
a one-off global config file. The earlier "pad/SMPC read" guess was wrong; retracting it.

### The ~7 reset callbacks from `Stage_ResetAndLoadDirector`, resolved

Followed the 7 unidentified function-pointer calls (xrefs from 0x060043c0-0x060043ec)
that `Stage_ResetAndLoadDirector` fires on every stage transition:

| target | renamed to | what it does |
|---|---|---|
| 0x06005bb4 | *(not renamed)* | trivial: mask one flag, set another to 1 |
| 0x060048d8 | `Res_CloseIfOpen` | poll-close-then-free a resource handle |
| 0x0602ccc4 | `Sys_SignalStopAndWaitAck` | signal + busy-wait on two ack flags — possible master/slave SH-2 sync |
| 0x06008cc4 | `Mode_EnterIdle` | *(corrected below — not sound-related)* |
| 0x06007bac | `Reset_ObjectAndChannelTables` | zero a 17×12-byte array and set an 8×8-byte array to 0xffff (matches 8 SCSP channels) |
| 0x06006f56 | `Obj_InitLinkedLists` | init two list-head pointers to a shared sentinel node |

(The other two targets, 0x06004418/`06004e10`, were already identified earlier.)

**Correction**: `0x06008cc4` was first named `Snd_ResetOnStageChange` on the assumption it
was sound-related (it sits near the sound cluster addresses). Traced further: it just
calls a fixed function pointer (`PTR_FUN_06008cec`, a literal pool constant, not a
runtime-changeable mode variable) then clears an unrelated 16-bit flag. That pointer
target is `0x060096bc` — zeroes a global and calls `Idle_InitMessageSystem` — matching
the earlier "mode-table entry" hypothesis for idle/attract mode from `FUN_060096bc`.
Renamed: `0x06008cc4` → **`Mode_EnterIdle`**, `0x060096bc` → **`Idle_ModeTableEntry`**.
So `Stage_ResetAndLoadDirector` resetting to idle mode on every stage transition is a bit
odd — worth revisiting whether it's really called on *every* transition or only specific
ones (e.g. death/game-over → back to attract mode), since the earlier read was "every
stage teardown does this unconditionally."

**Running total: 27 functions renamed out of 529.**

### `Idle_InitMessageSystem`'s sub-inits, resolved — new "message system" cluster

Traced the 5 calls inside `Idle_InitMessageSystem` (2 direct, 3 indirect via
literal-pool pointers):

| target | renamed to | what it does |
|---|---|---|
| 0x0600c700 | `Msg_ResetState` | mask a flag byte to state 2, zero a dword + a word |
| 0x0600d2d8 | `Msg_InitSlotPool` | inits two parallel arrays (32-slot + dynamic-count) with index-chain + capacity marker (0x1f) — looks like a free-list/slot allocator, plausibly the attract-mode text message pool |
| 0x0600d010 | `Sys_StrCopy` | strcpy: byte-by-byte with null check, aligned fast path via a separate pointer — generic runtime helper, not message-specific despite being called here |
| 0x0600aa38 | `Idle_ClearMsgState` | zeroes 3 globals |
| 0x0600aa8e | `Idle_NoOpHook` | empty function body (just `return`) — likely an unused extension point |

Fits the "attract-mode message/taunt system" hypothesis from the `"Hi! Come come
everybody."` string cluster — `Msg_InitSlotPool` shape (32 slots, index chains)
matches a rotating-message queue. Next: find what writes into the slots
`Msg_InitSlotPool` sets up, to confirm and locate the actual message strings/table.

**Running total: 32 functions renamed out of 529.**

Checked `Sys_StrCopy`'s other callers: also used by `FUN_060356b8`, the ISO9660
directory-entry parser in the CD driver cluster (already deprioritized — confirms
`Sys_StrCopy` is a generic runtime helper, not part of the message system).
`Msg_InitSlotPool`'s slot arrays have no other static xrefs (likely indexed via
register-relative addressing Ghidra doesn't resolve statically) — dead end for now.

### Big find: the real per-frame main loop body, decoded

`Boot_And_MainLoop`'s `do { ... } while(true)` loop — the actual per-VBlank game
loop — is now fully visible in the decompile. Per iteration:

1. `Sys_SignalStopAndWaitAck()`
2. `Stage_ResetAndLoadDirector()`
3. Four still-unnamed calls: `FUN_06004290`, `FUN_06004294`, `FUN_06004298(1)`,
   `FUN_0600429c` — not yet traced, good next targets.
4. A self-referential store+call at `060042a0` (stores its own function pointer
   into a global then immediately calls it — odd pattern, maybe a "first frame
   only" self-disabling init, worth a closer look) then `FUN_060042a8`.
5. **The object-processing block, 4x repeated for "slots" identified by mode bits
   4/8/0x10/0x20** (same mode constants as `Obj_SetTransformParam`'s dispatch):
   - `Gfx_PackSpriteAttrByMode(mode, data_ptr)` — was `FUN_06029b40`, confirmed:
     mode-dispatched (1/2/4/8/0x10/0x20) bitfield packer building what looks like
     VDP1 command-table-shaped words (color mode, flip, size fields packed with
     shifts/masks matching VDP1's `CMDPMOD` bit layout) — this is very likely SGL's
     internal sprite/texture attribute setter, not top-level game logic.
   - `Obj_ConsumeFlagAndStore(mode)` — called with the same mode constant right
     before `Obj_SetTransformParam`, strongly suggesting **this is what feeds the
     mode selector `Obj_SetTransformParam` reads** (previous notes had the selector
     at `PTR_DAT_0602a970`; `Obj_ConsumeFlagAndStore` stores its param at `0602a8c0`
     — need to re-check whether these are actually the same cell before concluding,
     addresses looked different on first read).
   - `Obj_SetTransformParam(data_a, data_b, 0)` — confirms it's a 3-arg call, args
     come from a struct array (`PTR_DAT_06004288`, offsets 0/0x14/0x24/0x34 across
     the 4 slots) rather than a literal mode parameter.
   - For slots 1-2 (mode 4, 8) only: `FUN_060042ac(extra_c, extra_d)` — extra step
     slots 3-4 (mode 0x10, 0x20) skip, suggesting slots 1-2 are a different/more
     complex object type than 3-4 (maybe player + weapon vs. two simpler layers).
   - `Obj_SetReadyFlag()` closes out each slot.
6. `FUN_060042b0()`, then `Snd_ChannelScheduler()` at the very end.

**Working theory**: 4 fixed "slots" get updated every frame — plausibly the
player sprite, a linked sub-object (weapon/shadow?), and two simpler layers
(background parallax? UI?). Matches a Saturn action game's typical fixed sprite
budget for hand-authored (non-dynamic-list) objects. Next steps: identify
`FUN_06004290/94/98/9c/a8/ac/b0` (7 more per-frame calls) and confirm the
`Obj_ConsumeFlagAndStore` → `Obj_SetTransformParam` mode-cell link.

**Running total: 33 functions renamed out of 529.**

### Major find: stage code is likely a dynamically-loaded overlay at 0x06040000

Resolved the remaining per-frame calls from `Boot_And_MainLoop`:

| target | renamed to / verdict |
|---|---|
| 0x0600446a | `Evt_ProcessQueue` — iterates a counted array of 0x20-byte entries, dispatches on a type field (1/2/3) to 3 handler functions, resets count to 0 after. Generic command/event queue processor. |
| 0x06004e1c | `Evt_ProcessSingleCommand` — same shape but single-slot (type check ==1, one handler call, clear flag). |
| 0x060288a8 | *(not renamed — SGL-internals territory, see below)* |
| 0x06005f4c | *(not renamed)* builds 4 fixed-size records and submits each to a different function pointer — possibly HUD/window registration, too speculative to name yet. |
| **0x06040000** | **no static code here** — Ghidra decompiles it as `halt_baddata()`. |

**The `060042a0` call is the important one**: `Boot_And_MainLoop` stores the literal
constant `0x06040000` into a fixed RAM cell (`*0x060145fc = 0x06040000`) and then
*calls through it*, every single frame. 0x06040000 is exactly the byte right after
A.BIN's declared max size (`0x06004000 + 0x3C000 = 0x06040000`) — i.e., free work
RAM immediately following the loaded executable, with no static code there.

**Working hypothesis**: stage-specific logic isn't in A.BIN at all — it's loaded
at runtime into this RAM region and executed from there. This would explain both
open mysteries at once: why `DIRECTOR.PPB` gets reloaded on every single stage
transition (`Stage_ResetAndLoadDirector`), and why `SHINOBI.PPB` has zero code
xrefs in A.BIN (it's not called *from* A.BIN — it likely *is* the code that runs
*after* being loaded). Need to confirm the actual load address `Load_DirectorPPB`
writes to matches `0x06040000` (not yet checked), then dump `DIRECTOR.PPB`/
`SHINOBI.PPB` from the disc and disassemble as raw SH-2 code as a next session's
priority — this could be a bigger unlock than any single function rename so far.

**Also noted**: `0x0602894c` and `0x0602ab1c` (the other two slot-processing
helpers near `Gfx_PackSpriteAttrByMode`) build VDP1-command-table-shaped entries
(command-type tags 4/10, `0x20`-byte records) into count-tracked arrays — this
whole `0x06028000-0x0602c000` region looks like **SGL library internals** (the
3D/2D command-list builder), same as the CD driver at `0x06035000` — deprioritize
for "game logic" hunting, same reasoning as before.

### CONFIRMED: `DIRECTOR.PPB` is a dynamically-loaded SH-2 code overlay

Checked `Load_DirectorPPB`'s actual file-read call: buffer arg resolves to literal
`0x06040000` — **exactly** the address `Boot_And_MainLoop` jumps into every frame.
Extracted `DIRECTOR.PPB` (248400 bytes) and `SHINOBI.PPB` (492976 bytes) directly
from `extracted/track1.iso` via `pycdlib` (both are top-level files on the disc,
listed as `DIRECTOR.PPB;1` / `SHINOBI.PPB;1` in `docs/file_listing.txt`) and
disassembled the first 128 bytes of `DIRECTOR.PPB` at base `0x06040000` with
`tools/sh2dis.py`:

- Starts with a textbook function prologue: `STS.L PR,@-R15` / `ADD #-36,R15`
  (push return address, allocate a 36-byte stack frame).
- Immediately followed by a state-read + branch-table dispatch: loads a value via
  `R14` (PC-relative pointer), then a chain of `CMP/EQ`+`BT` pairs branching to
  different handlers — the classic shape of a **stage/director script state
  machine** (read current state, dispatch to per-state handler).

This is **not garbage/data** — it's confirmed executable SH-2 code, matching the
hypothesis exactly. `DIRECTOR.PPB` (and presumably `SHINOBI.PPB`, same mechanism)
are runtime-loaded overlays containing the actual stage/game logic, which is why
none of it was findable by grepping A.BIN — **it was never there**.

**This changes the shape of the whole project**: `A.BIN` alone is the boot
loader + hardware/SGL plumbing; the real gameplay code lives in these `.PPB`
overlays. Next session priority: load `DIRECTOR.PPB` into Ghidra as a second
program with base address `0x06040000` (same Saturn SH-2 loader settings) and
start the same rename/trace workflow there — this is likely where most of the
actual "Shinobi Legions gameplay" logic will be found, as opposed to `A.BIN`'s
mostly-SGL-plumbing content.

### `DIRECTOR.PPB` imported into Ghidra (2026-08-05)

Raw Binary import (SH-2 big-endian, base `0x06040000`) worked, but needed a
manual kickstart: Ghidra's analyzers can't seed themselves on a raw binary with
no entry symbol, so nothing was disassembled until we manually selected the
whole range and disassembled it, then ran Auto Analyze. After that: **476
functions found** (`FUN_06040000` through `FUN_0604a541` and beyond).

**Likely 6-byte file header, found by comparing both `.PPB` files:**

```
DIRECTOR.PPB:  8002 3ED5 FFFF | 2FE6 4F22 7FDC DE1E ...
SHINOBI.PPB:   8005 5155 FFFF | 4F22 D416 6041 A00E ...
                              ^ real SH-2 code starts here (offset +6)
```

Both files independently show the same shape: 2 header words, then an `FFFF`
sentinel, then valid code (a real function prologue: `MOV.L Rn,@-R15` /
`STS.L PR,@-R15`). This broke Ghidra's linear disassembly at exactly
`FUN_06040000` (decompile shows "bad instruction data" after ~4 bytes) — the
auto-disassemble pass started at file offset 0 and didn't know to skip the
header. **Fix needed**: manually `Create Function` at `0x06040006` (offset +6)
to get a clean decompile of the real entry point.

**Open question**: `Boot_And_MainLoop` calls exactly `0x06040000` (confirmed via
literal pool xref), not `+6` — so either something else patches the jump-table
cell to the post-header address after `Load_DirectorPPB` runs (not yet traced),
or the header bytes are themselves meant to be executed/skipped via a different
mechanism (e.g. the first header word might actually BE a valid branch
instruction we haven't decoded correctly). Worth re-checking once the function
at `+6` is cleanly decompiled and its prologue confirmed.

**Confirmed**: created the function manually at `0x06040006` — clean decompile,
matches the hand-disassembled shape exactly. **Renamed to `Director_EntryDispatch`.**
Reads a state value via a PC-relative pointer (`@0x6040088`), then branches
across 8 cases (0-7) to different handler blocks (`0x06040018`, `0x06040020`,
`0x06040040`, `0x06040084`, `0x0604004a`, `0x06040050`, `0x06040056`,
`0x0604005c`) — a genuine per-frame state-machine dispatcher for the director
script. The `+0` vs `+6` question is still open (not yet re-checked), but doesn't
block further work — this is clearly the real entry logic either way.

**Running total for DIRECTOR.PPB: 1 function renamed out of 476** (separate
counter from A.BIN's 529 — two different programs in the same Ghidra project
now).

**Paused (2026-08-05)**: bulk disassembly of `DIRECTOR.PPB` is unreliable —
sampled ~20 functions spread across the file (before and after fixing the
6-byte header alignment) and only ~10-15% decompile cleanly; the rest hit
"bad instruction"/"unimplemented instruction" almost immediately. This isn't
an alignment fluke fixable by skipping the header — the hand-optimized SH-2
code likely has literal pools interspersed throughout (same issue A.BIN had,
but A.BIN got a specialized Saturn ISO loader that handled it; this raw file
has no such help). `Clear Code Bytes` + re-disassemble did NOT improve the
ratio. Fixing this properly means going function-by-function from confirmed
call graph edges (like we did for `Director_EntryDispatch`), not bulk/blind
disassembly — much slower, deprioritized for now. **Back to `A.BIN`** per
user direction; resume `DIRECTOR.PPB` later with the targeted approach.

### Back to A.BIN: resolved the remaining per-frame literal pool calls

Read the raw file bytes directly (Python + pycdlib) to resolve the last two
unresolved `Boot_And_MainLoop` per-frame call targets that GhidraMCP's
`get_xrefs_from` wasn't returning anything for:

- `0x060042a8` → `0x0602894c` — confirms this is the same "SGL command-list
  builder" function noted earlier (VDP1-command-table entries, `0x06028000`
  region), called once per frame before the 4-slot loop. Deprioritized, same
  as before.
- `0x060042ac` → `0x0602ab1c` — confirms the other SGL-internals slot helper,
  called with `(extra_c, extra_d)` for slots 1-2 only, matching what was
  already documented.
- `0x060042b0` → **`0x06007084`** — this is the function the user hand-traced
  extensively earlier in the project (large pasted disassembly dumps,
  `FUN_06007084`/`FUN_0600718a`) but **no Function object exists at this
  address in the current Ghidra project** — `list_functions`/`get_xrefs_to`
  both come up empty for it, despite being a confirmed call target. Likely
  lost during the panic-disassemble incident or never resurfaced after the
  `.gitignore`/repo cleanup. **Action needed**: in Ghidra, go to `06007084`
  in `A.BIN`'s CodeBrowser and check whether it's disassembled; if not, same
  fix as `DIRECTOR.PPB`'s entry point — `Create Function` there manually.

**Resolved**: GhidraMCP just wasn't connected to the right tool — `track1.iso`
(the program A.BIN's code actually lives in, via the Saturn ISO loader) wasn't
open. Once reopened, `FUN_06007084` was already cleanly disassembled (this is
the function the user hand-traced early in the project). Decompiled the full
cluster:

| address | renamed to | what it does |
|---|---|---|
| 0x06007084 | `Fx_UpdateInterpEvent` | per-frame state dispatcher (states 0/1/2): 0 = pick up a newly-queued request if idle, 1 = drive a 3-axis interpolation to completion, 2 = copy+verify a data block |
| 0x0600718a | `Fx_ProcessSwapFlags` | bit-flag-driven double-buffer swap: checks/clears flags 1/2/4, copies data (`Sys_MemCopy`) into one of two buffers based on a toggle bit, verifies via `Fx_VerifyFieldsMatch` |
| 0x06007960 | `Fx_StepInterpolate3Axis` | for 3 parallel fields, steps each toward a target value via a shared callback (accel/decel-style increment), returns true once all 3 have converged (fixed-point, upper 16 bits of each field reaching 0) |
| 0x06007a5e | `Fx_VerifyFieldsMatch` | calls a shared callback for 3 fields, compares each against an expected value, returns true if all 3 match |
| 0x060077a4 | `Fx_SetFlagBits7` | updates 7 status bits from a bitmask parameter, one at a time, wrapped in what looks like an atomic disable/enable (interrupt mask?) pair around each bit |

**Working theory**: this cluster is a generic 3-axis interpolation/animation
engine — `Fx_StepInterpolate3Axis`'s shape (move 3 fields toward a target via
a callback, done when converged) strongly resembles smooth camera or object
movement easing, plausibly driven by `DIRECTOR.PPB`'s script data (fits the
project's "director" framing). Not fully confirmed which axis set (camera vs.
object vs. something else) — next step would be finding callers of
`Fx_UpdateInterpEvent` beyond the main loop to see what feeds it its targets.

**Running total: 38 functions renamed out of 529 in A.BIN.**

### Note: several "who writes this" dead ends may point to DIRECTOR.PPB

Checked whether anything in A.BIN writes `Fx_UpdateInterpEvent`'s trigger cell
(`0x06007128`) — no writers found, same dead end as `Obj_SetTransformParam`'s
mode selector and `Msg_InitSlotPool`'s slot arrays. Tested the "written from DIRECTOR.PPB" hypothesis directly: byte-searched
both `DIRECTOR.PPB` and `SHINOBI.PPB` for the raw 32-bit address values of
`0x0602a970`, `0x0602a8c0`, `0x06007128`, and `0x060273e4` (as they'd appear
in a literal pool if referenced by a `MOV.L @(d,PC),Rn`). **Zero matches in
either file.** So the overlays don't reference these cells via a direct
literal address — either the write is computed (base+offset arithmetic,
invisible to a raw byte search) or the real writer is somewhere else
entirely. Hypothesis not confirmed; don't assume DIRECTOR.PPB explains these
dead ends without more evidence.

### Evt_ProcessQueue's command handlers: resource file loader cluster

Traced the 3 type handlers (1/2/3) dispatched from `Evt_ProcessQueue`:

| address | renamed to | what it does |
|---|---|---|
| 0x060047a8 | `Res_LoadFileByName` | cached file loader — compares a 14-char filename against the currently-loaded one, returns immediately on cache hit; otherwise closes the current file (`Res_CloseIfOpen`), opens the new one, kicks off a read (retry loops around low-level open/seek calls) |
| 0x06004528 | `Res_PumpAsyncRead` | chunked async read state pump (2 states: issue read / check completion) — called repeatedly in a loop until it returns 0 (done). This is the actual byte-by-byte streaming reader. |
| 0x060046c4 | `Evt_CmdLoadAndPump` | type-1 command: calls `Res_LoadFileByName`, then loops `Res_PumpAsyncRead` to completion synchronously; on a genuinely new file (not cache hit) also resets sound + object/channel tables — same "new content loaded" reset pattern as `Stage_ResetAndLoadDirector` |
| 0x0600475c | `Evt_CmdLoadAsync` | type-3 command: same load + reset logic as above, but does NOT pump the read loop — true async, caller must pump separately |

This is the same CD file-loading idiom seen in `Load_DirectorPPB`, generalized
to load arbitrary named resources through a command queue instead of a direct
call — likely how stage-specific assets other than `DIRECTOR.PPB` itself (CG
files, sound banks, etc.) get streamed in.

**Running total: 42 functions renamed out of 529 in A.BIN.**

Two small helpers used by `Res_LoadFileByName`:

- `0x06004930` → `Res_GetFileSize` — seeks (retry loop) then reads file info,
  computes size accounting for the 2048-byte CD-ROM sector granularity, with
  an override if a size field is explicitly set (not `-1`).
- `0x0600498a` → `Res_CopyFilename12` — simple bounded string copy, max 12
  chars, null-terminated.

**Running total: 44 functions renamed out of 529 in A.BIN.**

### Sound channel scheduler internals (closes out task #1, the sound cluster)

Traced the helper functions called from `Snd_ChannelScheduler`:

| address | renamed to | what it does |
|---|---|---|
| 0x060089d4 | `Snd_QuickSortByPriority` | recursive quicksort on the channel array, sorting by a 16-bit priority field at offset +2 |
| 0x06008af0 | `Snd_QuickSortByField10` | same quicksort shape, sorting by a byte field at offset +10 (different priority axis — possibly volume vs. age) |
| 0x06007c14 | `Snd_RefreshChannelDefs` | loops all 8 channels, refreshes each via `Snd_LookupSfxDef`, marks channel unused (`0xffff`) if its definition is empty |
| 0x06007c84 | `Snd_FindFreeOrEvictChannel` | classic voice-allocator: scans for a free channel slot (marker `-1`); if none free, finds an insertion point by priority and returns it OR'd with an eviction flag |

This closes out the sound scheduler cluster — `Snd_ChannelScheduler` (8-channel
SCSP scheduler) now has essentially all its supporting cast identified: sort by
two different priority axes, refresh definitions, and free-or-evict allocation,
matching a standard voice-stealing audio mixer architecture.

**Running total: 48 functions renamed out of 529 in A.BIN.**

### New cluster: ring-buffer streaming subsystem (0x0600a000-0x0600b800)

Sampled functions in the previously-unexplored range between the message
system and the SGL-internals block. Found a distinct streaming/ring-buffer
subsystem, separate from `Res_LoadFileByName`'s simpler cached-file loader:

| address | renamed to | what it does |
|---|---|---|
| 0x0600ab68 | `Sys_RaiseIrqLevel` | manipulates the SH-2 status register's interrupt mask bits and increments a nesting counter — classic "enter critical section, return previous level" idiom. Likely the atomic wrapper called repeatedly by `Fx_SetFlagBits7`. |
| 0x0600adcc | `RingBuf_GetContiguousRead` | computes how many contiguous bytes are currently readable from a ring buffer before wraparound, returns a status code + pointer/length via out-params |
| 0x0600ae44 | `RingBuf_AdvanceRead` | advances the ring buffer's read position by N bytes, handling wraparound (mirrors `RingBuf_GetContiguousRead`) |
| 0x0600b336 | `Res_FixupPointers` | converts 4 fixed base-relative offsets plus an array of offsets (via a callback) into absolute pointers — a resource "relocation fixup" pass, the kind you'd run once after loading a blob with internal relative offsets |
| 0x0600a0b0 | `Stream_PumpRingBuffer` | pulls a contiguous chunk via `RingBuf_GetContiguousRead`, and once enough data has accumulated (threshold check), stashes the buffer struct pointer globally, calls a processing callback, then advances the read pointer — the "consume and process" half of the streaming pipeline |

**Working theory**: this is a general streaming-data pipeline (ring buffer +
periodic processing callback + pointer fixup for loaded blobs), likely used
for something bigger than `Res_LoadFileByName` handles — a good candidate for
CD-audio/FMV streaming or level/stage data too large to load in one shot.
Distinct from the `Snd_Cmd*` ring buffer (that one's a fixed 7×16-byte command
queue; this one has variable-length reads and wraparound math). Worth revisiting
to find what `Stream_PumpRingBuffer`'s processing callback actually does.

**Running total: 53 functions renamed out of 529 in A.BIN.**

Also confirmed: the `0x0600e000-0x06028000` gap (no functions found there at
all) is expected, not a Ghidra miss — this whole region is where the earlier
string-search finds live (`SHINOBI.PPB`/`DIRECTOR.PPB` names, the SGL version
string, the CPK string, attract-mode text at `0x0600edcc`-`0x06014444`+) — it's
data, not code.

### Streaming subsystem lifecycle, more of it identified

| address | renamed to | what it does |
|---|---|---|
| 0x0600b414 | `Stream_InitFromHeader` | validates a magic-number header, parses a track/entry table (stride 0x10, count at header+0x3c), computes ring-buffer sizing/offsets, calls `Res_FixupPointers`, sets state to 3 (initialized). Error paths call a logging function with distinct error-code constants for each failure. Shape strongly suggests a **streaming audio format parser** (multi-track header, buffer sizing) — possible BGM/CD-audio streaming, separate from the SCSP sound-effect queue. |
| 0x0600b716 | `Stream_SetCallback` | stores a callback pointer into the stream struct (+0x68), then re-runs `Res_FixupPointers` |
| 0x0600b076 | `Stream_PumpUntilIdle` | busy-loop calling a pump callback while a stream-state flag is non-zero |
| 0x0600b0bc | `Stream_Close` | waits for idle (`Stream_PumpUntilIdle`), runs a finalize callback if state was 4, sets state to 5 (closed) |

**Running total: 57 functions renamed out of 529 in A.BIN.**

### Streaming subsystem: full picture emerges — likely BGM/CD-audio streaming engine

Followed the callers of `Stream_InitFromHeader`/`Stream_Close` upward and found
the top-level driver:

| address | renamed to | what it does |
|---|---|---|
| 0x0600c4f4 | `Stream_Update` | the master per-tick state machine (states 0-5): re-inits on state 2, walks the track table processing entries, pumps until idle and fills any gap, checks buffer fill level against a threshold, handles underrun, and transitions to a linked next stream (`Stream_LinkNext`) when the current one finishes — **this is the top-level "advance the audio stream by one tick" entry point** |
| 0x0600b780 | `Stream_LinkNext` | compares two stream headers for format compatibility (sample layout, channel count, etc.); if compatible, copies playback state from the finishing stream into the next one and marks it ready (state 4) — enables **gapless/seamless track transitions** |
| 0x0600c0f4 | `Stream_FillSilence` | fills the output buffer with silence (splits by channel using a byte flag at header+0x25, mono vs. stereo) — buffer-underrun fallback |
| 0x0600b984 | `Stream_HandleUnderrun` | records a high-water-mark stat and resets write-position state after an underrun is detected |

**Conclusion**: this whole cluster (`Stream_InitFromHeader`, `Stream_Update`,
`Stream_LinkNext`, `Stream_Close`, `Stream_FillSilence`, `Stream_HandleUnderrun`,
plus the `RingBuf_*` helpers) is a complete streaming audio engine — most
likely CD-audio or ADPCM BGM streaming with gapless track-to-track transitions,
underrun/silence handling, and buffer-fill telemetry. Entirely separate from
the SCSP sound-effect command queue (`Snd_Cmd*`/`Snd_ChannelScheduler`) traced
earlier — Shinobi Legions has (at least) two independent audio pipelines: one
for short sound effects (fixed 8-channel scheduler) and one for streamed music.

**Running total: 61 functions renamed out of 529 in A.BIN.**

Two more from `Stream_Update`'s track-processing branch — much denser, lower
confidence than the rest of this cluster (heavy bitfield/offset arithmetic,
didn't fully verify every branch):

- `0x0600bcba` → `Stream_CopyTrackSamples` — copies/deinterleaves audio sample
  data for one track entry into the ring buffer (mono vs. stereo split via the
  same +0x25 format flag byte, handles wraparound).
- `0x0600c292` → `Stream_PrepareNextChunk` — advances to the next track-table
  entry and kicks off decoding for it (sets up a decode-state block, calls what
  looks like a decoder-init with format/pointer args).

Treat these two names as working hypotheses, not confirmed — the exact
bit-level behavior wasn't fully traced. Good candidates to revisit if the
streaming engine needs to be precisely reimplemented later.

**Running total: 63 functions renamed out of 529 in A.BIN.**

More streaming-cluster helpers, verified via xref-checked shapes (higher
confidence than the previous two):

| address | renamed to | what it does |
|---|---|---|
| 0x0600c222 | `Stream_CanPrefetchNext` | look-ahead feasibility check: is there a next track-table entry, is it not an end-marker (`-1`), does a flag bit clear, is there buffer space — used by `Stream_PrepareNextChunk` before eagerly advancing |
| 0x0600aed8 | `RingBuf_AdvanceWrite` | write-side mirror of `RingBuf_AdvanceRead` — advances the write position with wraparound, same status-code shape |
| 0x0600ae84 | `RingBuf_ClearWrapTail` | if a write would cross the buffer's wrap boundary, zero-fills the overflow region past the end |
| 0x0600af0e | `RingBuf_AdvanceWriteDeferred` | conditionally calls `RingBuf_AdvanceWrite` or just accumulates a pending byte count, depending on a flag |
| 0x0600af48 | `Stream_MarkNeedsInit` | sets stream state to 2 (needs re-init) — this is what `Stream_LinkNext` calls to prep the next stream before `Stream_Update` picks it up |
| 0x0600af38 | `Stream_GetHeaderPtr` | trivial accessor: returns `struct + 0x34`, the header sub-struct pointer used constantly throughout this cluster |
| 0x0600af32 | `Stream_GetBufferBase` | trivial accessor: returns the struct's first field, used as a base address added to other offsets elsewhere — reasonably confident but not 100% |

Left `0x0600af40` (a similar trivial one-field accessor) unnamed — couldn't
pin down what field it exposes without more digging, didn't want to guess.

**Running total: 70 functions renamed out of 529 in A.BIN.**

### New cluster: hardware timer / elapsed-time queries (0x0600c700-0x0600c900)

| address | renamed to | what it does |
|---|---|---|
| 0x0600c720 | `Sys_ReadRolloverCounter` | reads a 16-bit counter that can roll over, advances a base pointer by a fixed delta on wraparound |
| 0x0600c768 | `Sys_CaptureTimestamp` | atomic wrapper (raises IRQ level like `Sys_RaiseIrqLevel`) that captures `Sys_ReadRolloverCounter`'s value into a struct field |
| 0x0600c7ac | `Sys_GetElapsedTime` | reads the counter again and subtracts the captured timestamp — straightforward "time since capture" |
| 0x0600c8a6 | `Sys_QueryTimerState` | state-dispatched (states -1/0/1/2/3/4/5) elapsed-time query in different units; references `PTR_VDP2_TVSTAT` (the VDP2 TV status register, i.e. reads the hardware VBlank bit) confirming this is tied to real display timing, not just a software tick. Lower confidence — genuinely complex, multiple unit-conversion branches not individually verified. |

Left `0x0600c7f6` unnamed — related (calls `Sys_GetElapsedTime`, same state-check
shape) but its exact purpose wasn't clear enough to name with confidence.

**Working theory**: this is a general hardware-timer utility used for
frame-rate-independent timing, plausibly what feeds `Fx_StepInterpolate3Axis`'s
step calculations (interpolation speed independent of frame rate would need
exactly this kind of elapsed-time query). Not confirmed via a direct call
chain yet — worth checking if `Fx_*` functions call into this cluster.

**Running total: 74 functions renamed out of 529 in A.BIN.**

### Confirmed: timer cluster is part of the streaming engine, up to 32 concurrent streams

Checked who calls `Sys_CaptureTimestamp` — both callers are streaming-cluster
functions, confirming the "worth checking" hypothesis from above:

| address | renamed to | what it does |
|---|---|---|
| 0x0600b61e | `Stream_BeginPlayback` | checks a buffer-fill threshold, then captures a timestamp (`Sys_CaptureTimestamp`) and sets stream state to 4 (ready/playing) — the actual "start" of playback |
| 0x0600e16a | `Stream_RestampGroup` | iterates up to **32 stream slots** (`Stream_GetSlotIfActive`), and for each active stream (state 4) whose group-flag matches a bitmask parameter, increments a counter and re-captures its timestamp — a batch resync operation across a group of streams |
| 0x0600b94e | `Stream_GetSlotIfActive` | given a slot index (0-31), checks an allocation bitmask; if set, returns a pointer into a 32-entry array of stream instance pointers, else null |

**Confirmed architecture**: the streaming engine supports up to 32 concurrent
stream instances (a slot pool with an allocation bitmask, same pattern as
`Msg_InitSlotPool`'s 32-slot pool, though a separate pool — worth confirming
they're not literally the same array). `Stream_RestampGroup`'s "resync a group
by flag" operation suggests streams can be tagged into groups (e.g. all BGM
tracks vs. all ambient loops) and paused/resynced together.

**Running total: 77 functions renamed out of 529 in A.BIN.**

Two more, closing out the streaming cluster's lifecycle functions:

- `0x0600b748` → `Stream_SetVolume` — sets a field at +0x2c (moderate
  confidence it's volume/pan), and if the stream is currently playing (state
  4), immediately applies it via a callback with sentinel args.
- `0x0600b0e6` → `Stream_StopAndReset` — pumps until idle, does mono/stereo
  buffer-boundary padding (same +0x25 format-flag check seen throughout),
  then sets state back to 1 (idle) — stop-and-recycle rather than a full
  `Stream_Close`.

Checked for the slot pool's allocator (who sets the bitmask
`Stream_GetSlotIfActive` reads) — no writer found in A.BIN, same dead-end
pattern as other cross-cutting state. Not chasing further for now.

**The streaming subsystem is now comprehensively mapped**: init from header,
per-tick update, track prefetch/copy, playback start/stop/close, volume,
group resync, silence fallback, underrun handling, and the 32-slot pool —
roughly 25 functions across `Stream_*`/`RingBuf_*`/`Sys_*` (timer). Good
stopping point for this cluster; future work here should focus on precisely
verifying `Stream_CopyTrackSamples`/`Stream_PrepareNextChunk` (flagged lower
confidence earlier) rather than finding more functions.

**Running total: 79 functions renamed out of 529 in A.BIN.**

### Searched for pad/controller input — not in A.BIN

Checked several likely spots for SMPC pad-reading code:

- The 3 remaining unidentified boot table entries (`0x06005840`, `0x060057f0`,
  `0x06005b0a`) are real code, but read as VDP1/VDP2/SCU hardware init (the
  first repeats the `4/8/0x10/0x20` mode constants seen throughout the
  object/sprite system — likely sprite priority setup, not input).
- Boot table entry 5 (`0x0602524c`) — checked the raw file bytes directly:
  **all zero**. This init slot is unused in this build, not a Ghidra miss.
- No pad/controller-related strings (`list_strings` — same set as before, no
  new hits) and no imports (raw binary, expected).

**Conclusion**: pad input reading isn't in `A.BIN`. Given the confirmed
architecture (A.BIN = boot + SGL plumbing, `DIRECTOR.PPB`/`SHINOBI.PPB` =
actual game logic overlays), input handling is very likely inside
`DIRECTOR.PPB`. Finding it is blocked on getting a reliable disassembly of
that file — see the "Paused" note above. This is a concrete reason to
prioritize unblocking `DIRECTOR.PPB` (targeted function-by-function
disassembly) in a future session, since it's needed for both game logic *and*
a playable PC port.

### New cluster: VDP1 (sprite/polygon engine) init, 0x06028000-0x06028800

Systematic sweep resumed. Sampled 8 functions in this block — confirmed VDP1
hardware register cluster (Ghidra already had the register names: `FBCR`,
`PTMR`, `EWDR`, `EWLR`, `EWRR`, `TVMR`):

- **`Vdp1_Init`** (was `FUN_060280ac`) — the orchestrator: calls the other
  three below in sequence, then writes `FBCR`/`PTMR`/`EWDR`/`EWLR`/`EWRR`
  directly. High confidence — this is VDP1 startup.
- **`Vdp1_SetDisplayMode`** (was `FUN_06028178`) — writes `TVMR`, looks up a
  4-entry table by an index param (0-7, resolution/timing presets), sets a
  couple of derived flags. High confidence.
- **`Vdp1_SetEraseWindow`** (was `FUN_06028258`) — direct writer for the
  `EWDR`/`EWLR`/`EWRR` erase-window registers from x1/y1/x2/y2 + a
  fill-color param, with resolution-dependent bit shifts. High confidence.
- **`Vdp1_EraseFrameBuffer`** (was `FUN_06028570`) — thin wrapper: calls
  `Vdp1_SetEraseWindow` (through a function pointer, confirmed by matching
  the exact `(color,0,0,W-1,H-1)` call shape) with full-screen bounds. High
  confidence.
- **Left unnamed (real code, purpose genuinely unclear, not guessing)**:
  `0x0602834c` (trivial 2-way flag setter feeding VDP1 config — maybe
  interlace/double-density toggle), `0x06028368` (large orchestrator that
  computes several offsets into what looks like a display-list/workspace
  struct and calls `0x06028792` + `Vdp1_EraseFrameBuffer` — plausibly a
  "begin frame" setup, but the struct layout isn't pinned down), `0x06028528`
  (trivial register-set + callback), `0x06028792` (loops zeroing N 10-byte
  entries in an array, then conditionally relinks a list head — looks like a
  free-list reset for sprite/command entries).
- **Verdict for the PC port**: this whole cluster is VDP1-hardware-specific
  (framebuffer/erase-window register writes) — not portable as-is. On PC,
  frame clearing is just `SDL_RenderClear`; no action needed here beyond
  documentation.

**Running total: 83 functions renamed out of 529 in A.BIN.**

### New cluster: VDP1 command-list builder, 0x06028800-0x06029600

Sampled 8 more functions right after the VDP1 init cluster. This is a
different, very concrete layer: functions that write fixed 0x20-byte (32
word... actually 16-word) records into a command-list buffer, each starting
with a small integer "code" that matches the real VDP1 hardware command
format (`CMDCTRL`/`COMDT` field) byte-for-byte:

- **`Vdp1_EmitLocalCoordCmd`** (was `FUN_06028b08`) — code `10` (Local
  Coordinate command), 2 words (X,Y). High confidence — exact match to the
  documented VDP1 command format.
- **`Vdp1_EmitSystemClipCmd`** (was `FUN_06028b54`) — code `9` (System
  Clipping Coordinates), 2 words at the right offsets (XC,YC). High
  confidence.
- **`Vdp1_EmitUserClipCmd`** (was `FUN_06028ba8`) — code `8` (User Clipping
  Coordinates), 4 words (X1,Y1,X2,Y2) at the documented offsets. High
  confidence.
- **`Vdp1_EmitPolygonCmd`** (was `FUN_06028d54`) — code `4` (Polygon draw),
  sets CMDPMOD/CMDCOLR, fills 4 vertices (quad) via a callback, sets the
  Gouraud table pointer field. High confidence.
- **`Vdp1_FlushCommandList`** (was `FUN_06028ac4`) — guarded by a "dirty"
  flag; if set, calls a function pointer (likely the actual VDP1 submit/DMA
  kick) and advances the write cursor by one slot (`0x20` bytes, matching
  the command size above). Medium confidence — the callback itself wasn't
  traced.
- **`Vdp1_InitBackgroundQuad`** (was `FUN_0602894c`) — builds a persistent
  full-screen quad (local-coord + polygon commands back to back) once, then
  on later calls just patches one field. Reads like initializing a
  border/background polygon. Medium-high confidence.
- **`Vdp1_LoadTextureEntry`** (was `FUN_060288a8`) — copies a texture/pattern
  record (address + 2 attribute words) from an indexed table into active
  state, advances a "current char address" cursor by `0x20`. Medium
  confidence — matches the shape of SGL's texture-table lookup but the
  struct fields aren't independently confirmed.
- **Left unnamed**: `0x060293ec` — takes 3 char params, toggles several
  attribute-flag bits (0x10/0x20/0x80/0xc0) from small enums, and calls a
  function pointer with 0/1 only when a "current frame" value changes
  (looks like a double-buffer flip trigger). Too speculative to name with
  confidence — real code, VDP1/SGL attribute-combiner territory, not
  chasing further right now.
- **Verdict for the PC port**: none of this is portable as data-format — a
  PC renderer builds its own draw calls — but the `Emit*Cmd` functions are
  useful as a spec: they confirm the exact hardware polygon/clip/coordinate
  command layout, which is handy if we ever want to interpret command lists
  recorded from the original game for reference.

**Running total: 90 functions renamed out of 529 in A.BIN.**

### Big find: Vdp2_Init, and a second transform-dispatcher twin

Continued the sweep past the VDP1 clusters, into 0x06029500-0x0602ae50:

- **`Vdp2_Init`** (was `FUN_0602a688`) — zeroes a whole family of VDP2 state
  structs and writes `PTR_VDP2_TVMD_0602a820` directly (Ghidra already had
  the VDP2 TVMD register name) — confirms this is VDP2 (background/scroll
  processor) startup, the sibling of the earlier `Vdp1_Init`. Ends by
  raising the *same* ready-flag as `Obj_SetReadyFlag` (`0602a8bc`) — so VDP2
  init is itself one of the things that "readies" a frame. High confidence.
- **`Obj_SetTransformParamSecondary`** (was `FUN_0602ab1c`) — structurally a
  twin of `Obj_SetTransformParam`: same mode dispatch (1/2/4/8), writes a
  *different* pair of offsets (`+0x4c`/`+0x50` vs. the original's
  `+0x44`/`+0x48`) on a related-but-different struct, with an extra
  min/max-clamp pre-pass via two callbacks before storing. Reads like a
  second data channel per object slot (maybe velocity/rotation next to
  position). Medium-high confidence on the "twin dispatcher" shape, low
  confidence on what the second channel actually represents.
- **`Sys_MemCopyWords`** (was `FUN_0602adc8`) — confirmed: plain 16-bit-unit
  copy loop (`param_3` is a byte count, copies `param_3/2` words). Distinct
  from the existing `Sys_MemCopy` (byte-oriented). High confidence.
- **`Gfx_FlushDirtyRegions`** (was `FUN_0602ac48`) — calls `Sys_MemCopyWords`
  ~7 times with fixed sizes against several dirty-flag-guarded regions —
  looks like flushing several small dirty buffers (palette/CRAM-sized
  chunks, given the `0x28`/`0x48`/`0x40`/`0x10`/`0x20`-byte sizes) out to
  their destinations each frame. Medium confidence.
- **`Gfx_SyncAndFlush`** (was `FUN_0602ad2e`) — small state-dispatcher (1/2)
  that conditionally does two more `Sys_MemCopyWords` calls then always
  calls `Gfx_FlushDirtyRegions`. Medium confidence.
- **Left unnamed** (real code, not confident enough to name): `0x060295a0`
  and `0x06029af8` (both small "zero a fixed-size struct" resetters —
  plausible attribute/object record resets, no strong evidence either way),
  `0x060295c4` (a large ~200-line attribute-flag combiner, clear structural
  cousin of the unnamed `0x060293ec` from the VDP1 cluster — same family of
  functions, still too speculative to name precisely), `0x0602a660` (copies
  8 words into a fixed table offset, likely VDP2-palette-related but not
  confirmed).

**Running total: 95 functions renamed out of 529 in A.BIN.**

### VDP2 scroll/priority register cluster, 0x0602ae48-0x0602b344

Continued past `Vdp2_Init` into a dense run of VDP2 register bitfield
writers (per-scroll-plane setters — VDP2 has 4+ scroll screens, each with
its own set of character/priority/scroll registers, which is why these
functions are so repetitive).

- **`Vdp2_FlushDirtyRegions`** (was `FUN_0602ae48`) — checks a dirty-flag
  byte bit by bit (0x80/0x40/0x20/0x10/8/4), and for each set bit calls
  `Sys_MemCopyWords` with a fixed size then a callback — structurally
  identical to `Gfx_FlushDirtyRegions` from the VDP1 cluster, just the VDP2
  sibling. Medium-high confidence.
- **`Vdp2_SetPriority`** (was `FUN_0602b254`) — sets two 3-ish-bit priority
  fields into one register at nibble-aligned offsets (`<<0xc`, `<<8`) —
  matches the shape of VDP2's paired-plane priority registers (e.g.
  PRINA/PRINB hold two screens' priorities each). Medium confidence.
- **`Vdp2_ResetScrollState`** (was `FUN_0602b2a0`) — calls one named helper
  (`FUN_0602bf6c`) plus 5 function pointers, then zeroes an 8-word array and
  two more fields. Reads like a scroll-state reset entry point. Medium
  confidence.
- **Left unnamed** (real code, genuinely too dense/speculative to map
  precisely without register-level ground truth): `0x0602af54`,
  `0x0602afd4`, `0x0602b344` — large bitfield combiners writing 3-8 VDP2
  registers each via masked shifts, gated by per-plane callback thunks
  (probably a "wait for safe VDP2 write window" gate repeated per plane).
  `0x0602b2f2` — trivial one-line bitfield getter, no strong hypothesis for
  what it reads. Not chasing these further; VDP2 register-level work isn't
  needed for the PC port anyway (SDL2 handles blending/layers directly).

**Running total: 98 functions renamed out of 529 in A.BIN.**

### Sampled 0x06031400-0x06031ba4 — real code, too complex to name safely yet

Switched away from VDP2 registers to the sound-adjacent 0x06031400 block
(near `Snd_LookupSfxDef`), hoping for more PC-portable material. Found
something different and bigger instead:

- `0x06031794`/`0x06031830` — a matched pair of range-wrapping functions
  (multi-branch bounds check, subtract-and-negate on overflow). Called
  together on the same value from the function below, always as a pair —
  plausible sine/cosine-table-style companions (angle or coordinate
  wrapping without hardware divide — SH-2 has no integer divider), but no
  table reference was confirmed, so not naming them yet.
- `0x06031ba4` — a genuinely large (~200 line) function containing repeated
  `a*d - b*c` / `a*c + b*d` patterns (2D rotation/cross-product math) across
  three coordinate pairs, followed by per-element loops that call a
  comparison callback and write pass/fail results into a short array —
  reads like a **visibility/clipping test over a rotated bounding volume**
  (screen-space cull for polygon or sprite commands). Architecturally
  interesting (would explain how the game decides what's on/off screen) but
  far too dense to safely assign real names to its pieces in one pass.
- `0x060315f8`/`0x06031588` — index-based (not pointer-based) linked-list
  operations using `-1` as a sentinel, similar in spirit to `Msg_InitSlotPool`
  but on a different table. Plausible object/particle slot allocator, not
  confirmed.

**Not renaming any of these this round** — real code, but confidently
naming would mean guessing at semantics we haven't verified. Flagging as a
candidate for a future *dedicated* session (worth using more budget on,
given the "visibility test" function looks architecturally significant),
rather than folding it into a quick sweep pass.

Total unchanged: **98 functions renamed out of 529 in A.BIN.**

## Back to DIRECTOR.PPB: the unblock (2026-08-05)

Resumed the targeted approach per plan. Instead of fighting Ghidra's broken
auto-disassembly (still ~10-15% clean, unchanged), used `tools/sh2dis.py`
directly against `extracted/DIRECTOR.PPB` with `--base 0x06040000` to
hand-decode raw bytes, bypassing Ghidra's function-boundary heuristics
entirely. **This works reliably** — every instruction decoded this way
matches Ghidra's own (previously confirmed) disassembly of
`Director_EntryDispatch` byte-for-byte. This is the way forward for this
file: decode with the script, confirm structure by hand, only then create
functions/rename in Ghidra.

### `Director_EntryDispatch`'s real structure, fully decoded

Ghidra's decompiler garbles this into nonsense (`while (*psVar2 = in_r2, ...)`,
already known from earlier). The raw disassembly shows it's **not** a
computed jump table — it's a linear chain of 8 `CMP/EQ #n,R0` / `BT` pairs
(one per state value 0-7), which is why the decompiler chokes on it (SH-2
decompilers generally handle computed jump tables better than long branch
chains with a shared fall-through tail):

```
06040062: CMP/EQ #0,R0   06040064: BT 0x6040018   (case 0)
06040066: CMP/EQ #1,R0   06040068: BT 0x6040020   (case 1)
0604006C: CMP/EQ #2,R0   0604006E: BT 0x6040040   (case 2)
06040070: CMP/EQ #3,R0   06040072: BT 0x6040084   (case 3)
06040074: CMP/EQ #4,R0   06040076: BT 0x604004A   (case 4)
06040078: CMP/EQ #5,R0   0604007A: BT 0x6040050   (case 5)
0604007C: CMP/EQ #6,R0   0604007E: BT 0x6040056   (case 6)
06040080: CMP/EQ #7,R0   06040082: BT 0x604005C   (case 7)
06040084: ADD #36,R15 / LDS.L @R15+,PR / RTS       (epilogue)
```

Two concrete, confirmed findings:

- **State 3 is a true no-op.** Its `BT` target (`0x6040084`) is the
  function's own epilogue — state 3 does nothing but return. A real "idle,
  nothing to do this frame" case.
- **State 0 aliases into state 6's test, not its own handler.** State 0's
  target (`0x6040018`) is just `BRA 0x604007E; NOP` — and `0x604007E` is
  literally the mid-chain `BT 0x6040056` instruction for case 6. Jumping
  straight to a bare `BT` re-uses whatever T-flag was left by the *previous*
  `CMP/EQ` (state 0's own, which was true), so this branch is always taken,
  landing on **case 6's handler** every time. This confirms and explains the
  earlier "trivial thunk" finding from the previous session — it's not a
  handler at all, it's state 0 quietly reusing state 6's logic. Real, but
  probably not meaningful to port distinctly (state 0 == state 6 in effect).

Cases 1, 2, 4, 5, 6, 7 have real handler bodies starting at their `BT`
targets — not yet decoded (case 1 opens with a `JSR` through a literal-pool
function pointer at `0x6040090`, worth resuming here next).

**Open question, possibly explains the bulk-disassembly failures**: the
literal pool value at `0x06040088` (loaded into R14 as "the state variable's
address") is raw bytes `00 0B 6E F6` — not a plausible RAM address in any
Saturn memory region. Either this is a placeholder patched by a relocation
step at overlay-load time (would explain why blind disassembly chokes on
so many literal-pool-adjacent regions — relocatable slots aren't valid
addresses/instructions until patched), or the file-offset-to-VA mapping
has a wrinkle we haven't found yet for pool data specifically (vs. code,
which decodes correctly). Not resolved — flagging for next session, since
if it's relocation, that changes how we should read literal pools
file-wide.

### The dispatcher is almost entirely empty — only state 1 does real work

Decoded all 6 remaining handler bodies. Surprising, well-evidenced result:

- **States 4, 5, and 6 are single-instruction stubs**, each just
  `BRA 0x604005E` — which itself is `BRA 0x6040082`, landing back on the
  chain's *own* `CMP/EQ #7,R0` test. Since the original state (4, 5, or 6)
  never equals 7, that test always fails and falls straight through to the
  epilogue. Three states, zero effect.
- **State 7**'s target (`0x604005C`) is a lone `ADD #1,R2` (bumping a
  register nobody reads afterward) then falling into the same `BRA
  0x6040082` tail described above. Effectively also a no-op.
- **State 2**'s target (`0x6040040`) is a `NOP` followed by `BRA
  0x604005C` — i.e. it jumps straight into state 7's do-nothing tail.
  Also effectively empty. (The `MOV.L`/`JSR` pair sitting just before it at
  `0x604003C-0x603E` is dead code from this dispatcher's point of view —
  nothing branches to it — so either it's an unrelated function Ghidra's
  function-boundary confusion glued on here, or it's reachable some other
  way we haven't traced.)
- Combined with the already-confirmed state 0 (aliases into state 6, which
  is now *also* confirmed empty) and state 3 (direct-to-epilogue): **states
  0, 2, 3, 4, 5, 6, and 7 are all no-ops.** Only **state 1** does anything.
- **State 1** (`0x6040020`) is the one real path: a direct `JSR` through a
  function pointer loaded from a literal-pool slot, followed by an
  *indexed* function-pointer call — load an index value, multiply by 4,
  add to a base, dereference, `JSR`. This is a genuine "call
  handler-for-current-index" pattern, architecturally the kind of thing a
  per-object-type or per-input-state dispatch would look like.

**Blocked on**: the literal-pool values these calls depend on (e.g. the
pool slot at `0x6040088` used earlier, and the ones at `0x6040090`/
`0x6040094` for state 1) decode to implausible addresses when read as
big-endian 32-bit values straight off disc (`0x000B6EF6`, `0x52680604`,
`0xA6CC0602` — none fall in any real Saturn memory region). Two competing
explanations, neither confirmed: (a) these are relocation placeholders
patched at load time — plausible, since `Res_FixupPointers` in A.BIN is a
confirmed real "fix up embedded relative offsets into absolute pointers"
routine (though used there for the *streaming audio* format, not overlay
loading) — but no such fixup call was found in `Stage_ResetAndLoadDirector`'s
traced sequence before it jumps into `DIRECTOR.PPB`, which argues against
this unless the overlay self-relocates internally; or (b) there's still an
alignment/boundary mistake specific to this region that hasn't been caught.
The unresolved "`+0` vs `+6` entry point" question from earlier (does
execution really start at the 6-byte header, or right after it?) is
probably related and could be the same root cause.

**Bottom line for this session**: `Director_EntryDispatch` itself is mostly
a dead end for finding gameplay/input logic — 7 of 8 states do nothing.
State 1's indexed call is the one lead worth chasing, but it's gated on
resolving the pool-value mystery first. Input handling is very likely
elsewhere entirely — either a different, not-yet-found dispatcher in
`DIRECTOR.PPB`, or in `SHINOBI.PPB` (not yet touched at all).

### Promising new lead in SHINOBI.PPB: a scattered-value dispatcher (message/event codes?)

Scanned `SHINOBI.PPB` for function prologues (`4F22` = `STS.L PR,@-R15`)
directly via byte search — found 40+ candidate function starts beyond the
entry dispatcher already sampled, all real code (unlike blind bulk
disassembly, this only flags addresses that begin with a genuine prologue
byte pattern, which is a much better hit rate).

Spot-checked one (`0x06040868`) and it's a different animal from
`Director_EntryDispatch`: reads the *same* shared state-pointer cell used
by the entry dispatcher (`@0x60408DC`, `EXTU.W`-extended), then tests it
against a chain of `CMP/EQ #imm,R0` / `BT` pairs — but this time the
immediates are **scattered, non-sequential values** (`0x15`, `0x2C`,
`0x4A`, `0x3E`, ...) instead of `0,1,2,3...`. That shape — testing a value
against a set of specific constants rather than counting up from zero — is
much more consistent with **message/event-code handling** (e.g. "if this
is message type 0x15, do X") than a simple linear state machine. This is a
meaningfully different, and more promising, lead than the entry dispatcher.

**Blocked on tooling, not analysis**: getting a clean Ghidra decompile of
this (rather than continuing hand-disassembly) requires importing
`SHINOBI.PPB` into Ghidra as a second raw-binary program, the same way
`DIRECTOR.PPB` was set up (base `0x06040000`, SH-2 big-endian) — not yet
done. Flagging as the natural next step before going further by hand.

### SHINOBI.PPB imported into Ghidra — same wall, now confirmed by a second independent tool

Imported `SHINOBI.PPB` as a raw binary (SH-2 big-endian, base `0x06040000`).
Bulk "select-all + disassemble + Auto Analyze" reproduced the exact same
failure mode as `DIRECTOR.PPB`: the entry dispatcher at `0x06040006` isn't
even recognized (function list starts at `0x060402bd`), and boundaries
past that point are cascaded/misaligned versus the true ones found by the
clean prologue-byte scan.

Targeted approach instead: manually cleared/disassembled/created a function
right at `0x06040868` (the scattered-value dispatcher). This time it
worked — Ghidra now shows a real function there, confirming the same
struct-pointer reads seen by hand (`iRam060408d4`, `puRam060408cc`,
matching the raw `sh2dis.py` trace exactly) — **but it still hits "bad
instruction" at the same point the manual read got stuck** (right around
the `EXTU.W`/`AND`/`TST` sequence before the `CMP/EQ` chain).

**This is the important result**: two independent, differently-built tools
(a from-scratch minimal Python disassembler, and Ghidra's mature SH-2
module) now agree, on multiple separate functions across both `.PPB`
files, that something breaks at consistent, structurally-similar points —
always shortly after a state-cell read, near the same kind of `FFFF`/odd
filler words. That's strong evidence this isn't a tooling bug in either
disassembler; it's a genuine property of how these files were built —
almost certainly either an SGL-toolchain-specific instruction/relocation
encoding neither tool models, or literal relocation placeholder bytes that
are only valid after a patch step we haven't located.

**Conclusion for this thread**: we've extracted real, solid, structural
knowledge (both dispatchers' shapes, the shared state cell, `Scu_*`/CD-FS
characterization of A.BIN, and now cross-tool confirmation of exactly where
and how the `.PPB` files resist static analysis) — but fully cracking the
literal-pool/relocation format is blocked on information we don't have
(Sega/SGL toolchain documentation for this overlay format), not on more
manual effort. Recommending this thread stay parked (task #14) until either
that documentation surfaces or a different technique presents itself,
rather than continuing to spend sessions on byte-level trial and error that
keeps landing on the same wall.

### Cross-referenced against sotn-decomp (Saturn port of Symphony of the Night) — found the likely real bug

The user pointed out we're probably not the first to hit this — checked the
connected `sotn-decomp-repo` folder, which includes a Saturn port target
(`config/saturn/*.prg.yaml`, `tools/saturn-splitter`, SH-2 disassembly via
Ghidra+GhidraMCP, documented in their own `docs/DECOMP_LOG.md`). Genuinely
useful cross-project findings:

- **Their exact technique matches ours** — raw binary import into Ghidra
  with a manually-set base address equal to the overlay's real load address
  (`0x060A5000` for `ALUCARD.PRG`, their equivalent of our `.PPB` files) —
  and for them, **auto-analysis correctly finds functions** afterward. That
  rules out "raw-binary-import + Ghidra" as inherently unreliable for
  Saturn overlays in general — it works fine for a same-shaped file in a
  sibling project.
- **Their per-overlay header is much bigger than ours**: `ALUCARD.PRG`'s
  segment config marks offset `0x0-0x5F` (96 bytes) as `data`, with real
  code only starting at `0x60`. We've been assuming a **6-byte** header for
  `DIRECTOR.PPB`/`SHINOBI.PPB` based on a 2-header-word + `FFFF`-sentinel
  pattern that looked plausible but was never independently confirmed
  against a working reference.
- Their toolchain is confirmed **GNU `sh-elf-gcc`** (Cygnus port, run under
  DOS emulation for the original build), not Hitachi's proprietary `SHC` —
  worth keeping in mind if code-generation patterns ever need comparing,
  since our disassembly troubles could plausibly be compiler-specific too.

**New working hypothesis, not yet tested**: our 6-byte header guess is very
likely **wrong/too short** — real code (and the real, valid literal pool)
probably starts later, and everything we've decoded from `+6` onward
(including `Director_EntryDispatch` itself) may actually still be sitting
inside a longer header/data region that *happens* to decode into
plausible-looking SH-2 instructions for a while before hitting genuine
garbage. This would cleanly explain every symptom so far: implausible pool
values, consistent bad-instruction cascades at scattered points, and two
independent disassemblers agreeing (because they're both correctly
decoding bytes that just aren't meant to be code). **Next step**: determine
the real header length for `DIRECTOR.PPB`/`SHINOBI.PPB` — compare file
sizes/structure against `ALUCARD.PRG`'s known-good 96-byte layout, or look
for a length/size field near the start of the header that would tell us
where code actually begins.

**Tested and NOT confirmed**: dumped `DIRECTOR.PPB`'s first 256 bytes as
4-byte-aligned big-endian words and checked every one against the
`0x06000000-0x07ffffff` (Work RAM High) range, the way `ALUCARD.PRG`'s
header reads cleanly as a table of absolute addresses. **Zero hits** —
`DIRECTOR.PPB` does not have an `ALUCARD.PRG`-style leading pointer table.
Different game, different publisher, evidently a different overlay
convention. The "6-byte header, code at +6" reading stays the
best-supported one (independently re-confirmed: the very first `STS.L
PR,@-R15` byte pattern found by a fresh scan is at `0x06040008`, exactly
where `Director_EntryDispatch`'s second instruction should be).

**One more data point, inconclusive**: re-checked the case-1 pool value
with non-4-byte-aligned offsets out of curiosity — reading from
`0x6040090-2` instead of the "correct" (instruction-computed, 4-aligned)
`0x6040090` happens to produce a plausible-looking `0x06025268` instead of
garbage. But SH-2's `MOV.L @(disp,PC)` addressing mode is defined to always
target a 4-byte-aligned effective address, so an unaligned "lucky" read
isn't a legitimate fix — more likely coincidence than signal, but noted in
case a pattern emerges later.

**Parking this specific pointer mystery.** At this point we've tested and
ruled out: a script bug (Ghidra agrees byte-for-byte), `Res_FixupPointers`
relocation, the LWRAM-range theory, the Boot-ROM-exclusion logic, a
base+offset reading, an `ALUCARD.PRG`-style header table, and alignment
sensitivity. What we've solidly gained instead: both dispatchers' full
control-flow shape, the shared state cell, confirmation the disassembly
trouble is a genuine file-format property (not a tooling bug, cross-checked
against two disassemblers and a sibling decompilation project). Further
progress on the pointer itself most likely needs either different
information (real toolchain docs) or a lucky find, not more manual
byte-staring — future sessions should default to cataloguing *other*
self-contained functions in these files (via the prologue-byte-scan +
targeted Ghidra function creation technique, which works reliably) rather
than re-attacking this one value.

### Actually — a new, better-supported hypothesis surfaces immediately

Tried the "catalogue a fresh function" pivot right away: targeted-created
`0x06040266` in Ghidra. It breaks on the **very first instruction after the
prologue** — `STS.L PR,@-R15` decodes clean, then the next word (`FE7F`) is
"bad instruction" to both Ghidra and `sh2dis.py`. `0xFE7F` has top nibble
`0xF` — the same "reserved" opcode space (`0xF000-0xFFFF`) as the `FFFF`
anomalies seen repeatedly throughout both dispatchers. This is now the
**third independent location** (two dispatcher tails plus this fresh,
unrelated function) where a clean, valid instruction is immediately
followed by an `0xF`-prefixed word that neither disassembler can decode —
too consistent to be coincidental misalignment.

Base SH-2 (the Saturn's SH7604, no FPU) leaves the entire `0xF000-0xFFFF`
opcode space undefined/reserved. **New hypothesis**: these aren't garbage
or misalignment at all — they may be **deliberate illegal-instruction traps**
used as a fast software-interrupt/syscall mechanism (a known real-world
pattern on other platforms, e.g. calling into BIOS/SGL runtime code, or
Master/Slave SH-2 cross-communication, via an intentionally-illegal opcode
that the exception handler recognizes and dispatches on). This would
explain why dispatcher branches sometimes land *directly* on one of these
words (a real jump target, not skippable padding) and why two independently
different disassemblers agree it's not valid inline code — because it
isn't meant to be interpreted as a normal instruction at all, on either
tool's terms.

**Not confirmed, but the most promising lead yet** — worth checking next
time: (1) do these `0xF`xxx values follow any pattern (fixed set of
values, or do the low bits look like an operand/index)? (2) does A.BIN's
own boot code install an illegal-instruction exception handler anywhere
(would be a strong confirming signal)? Recommending this angle over
continuing the pointer-value archaeology — it might explain *all* the
disassembly trouble at once rather than one pool value at a time.

**Checked A.BIN, then checked the SH-2 ISA itself — the trap hypothesis
weakens.** `list_strings` on A.BIN found no VBR/exception-handler-related
text (expected — that plumbing likely lives in the shared Saturn BIOS,
which isn't part of A.BIN and isn't something we can inspect here). Did
turn up a genuinely nice find though: a library version string,
`` `$ver1.28 94/12/29SATURN(S) master ``, plus two leftover debug/test
strings (`"Hi! Come come everybody."`, `"Guu... I'm sleepinggguuu..."`) —
noted for flavor, not directly useful here.

More importantly: SH-2 has a **dedicated, documented trap instruction**,
`TRAPA #imm8`, encoded as `1100 0011 iiii iiii` (fixed top byte `0xC3`).
That's the real, spec-compliant way SH-2 code issues a software
interrupt/syscall — a game wouldn't need to abuse undefined `0xF`-prefixed
opcode space for that when a proper instruction already exists for it.
**This weakens (doesn't fully kill) the illegal-instruction-trap
hypothesis** — worth keeping in mind, but shouldn't be treated as
confirmed.

**Session wrap for this thread**: across this session we've tested,
individually, a script bug, `Res_FixupPointers` relocation, an LWRAM
address range, Boot-ROM exclusion, base+offset addressing, an
`ALUCARD.PRG`-style header table, alignment sensitivity, and now an
illegal-instruction-trap mechanism — none confirmed, several actively
ruled out. What stands regardless: both dispatchers' control-flow shapes
are solid and independently cross-checked, the disassembly trouble is
provably a real file-format property (not a tool bug), and we now know
`0xF`-prefixed words specifically (not just "corruption" generally) are
the recurring failure signature worth recognizing on sight. Genuinely
parking this now — it's earned a rest, not for lack of trying.

**Next steps for a future session**: (1) resolve the pool-value mystery —
check whether `SHINOBI.PPB` has an analogous slot at the same relative
offset with a *different* value (would support relocation) or whether
there's separate self-relocating init code before `Director_EntryDispatch`
runs; (2) once resolved, chase state 1's indexed call table; (3) consider
sampling `SHINOBI.PPB` with the same raw `sh2dis.py` approach in parallel,
since it's the file A.BIN never references directly and may be where the
real per-frame game logic (including input) actually lives.

### SHINOBI.PPB sampled too — same shallow-dispatcher architecture

Ran the same `sh2dis.py` approach cold against `extracted/SHINOBI.PPB`
(same 6-byte-header shape, same base `0x06040000` assumption). Found an
entry dispatcher at `0x06040006` structurally identical in spirit to
`Director_EntryDispatch` — an 8-way `CMP/EQ`+`BT` chain over a state value
— and **the same pattern holds**: states 2, 3, 4, and 5 all branch to the
exact same shared landing spot, states 0 and 1 alias into other states'
tails the same way state 0 did in `DIRECTOR.PPB`, and only one or two
states carry a real `JSR` through a pool-loaded function pointer. Cross-file
confirmation that **this "mostly-empty 8-state per-frame dispatcher" is a
deliberate, repeated pattern** in both overlays, not a one-off.

**Correction to the pool-value pessimism above**: on reflection, values
like `0x000B6EF6` aren't actually implausible — the Saturn's Low Work RAM
(LWRAM) region is a full 1MB (`0x00000000`-`0x000FFFFF`), and `0x000B6EF6`
falls comfortably inside it. `SHINOBI.PPB`'s analogous state-pointer pool
slot resolves to `0x00000602` — a *very* plausible fixed low-RAM address
for a shared state variable both overlays read (would make sense: A.BIN or
whichever overlay is active writes the "current director/game state" to one
fixed low-RAM cell). Retracting the "implausible address" framing — the
real blocker is just that `list_segments` on the raw-binary-imported
`DIRECTOR.PPB`/`SHINOBI.PPB` Ghidra programs only defines the `0x06040000+`
range, not `0x00000000+` LWRAM, so nothing here can be double-checked
against Ghidra directly. A LWRAM segment would need to be added manually
(or cross-referenced against A.BIN's own memory map, which does span low
addresses) to confirm reads/writes to `0x602` and similar cells.

**Where this leaves things**: both overlay files share one fixed low-RAM
state cell and a near-empty dispatcher built around it. The real gameplay
logic (state 1's `JSR` targets in both files) is still unresolved, but now
better understood — this is a solid stopping point for a session; picking
this back up should start with adding a low-RAM segment/overlay in Ghidra
(or just tracing low addresses like `0x602` through A.BIN, which already
has the full memory map) to see who else touches that state cell.

### Traced the state cell in A.BIN — retracting the LWRAM theory, settling on base+offset

Checked A.BIN's own memory map (`list_segments`, full Saturn map — this
program has all of it, unlike the raw-imported `.PPB` files): the address
`0x00000602` falls inside **`Boot_ROM` (`0x00000000-0x000FFFFE`)**, not
work RAM as guessed above — retracting that. Boot ROM is read-only, so a
value the dispatcher needs to *read as mutable per-frame state* can't
actually live there as a bare absolute address. `get_xrefs_to(0x06040602)`
in A.BIN (the base+offset interpretation) returned nothing either, but
that's expected — A.BIN wouldn't reference an address *inside* an overlay
it hasn't loaded yet by literal.

Also checked `Boot_And_MainLoop`'s actual call into the overlay entry point
(`(*(code*)0x06040000)()`, called bare, no arguments in R4/R5) — so the
overlay does **not** receive its own load base as a parameter, which eliminates one clean way this could resolve itself at runtime.

**Working conclusion** (logical, not yet directly confirmed): since the
literal absolute value can't be right (lands in read-only ROM), and no
relocation-fixup call was found in the load path, the most sensible reading
left is that these pool "addresses" are meant to be added to the overlay's
own fixed load base (`0x06040000`) by convention — i.e. `0x06040000 +
0x602 = 0x06040602` (squarely in `Work_RAM_High`, writable) — baked in by
whatever tool built the `.PPB` file, on the assumption it always loads at
that exact fixed address (no runtime relocation needed since the load
address never varies). Checked a couple of Saturn memory-map references
online (retroreversing.com, Yabause wiki) — they confirm the *generic*
memory regions (Boot ROM at 0x0, Work RAM High at 0x06000000+) but don't
document a specific low-ROM "peripheral status" address, so that avenue
didn't pan out either.

**Not chasing further right now** — this has gone deep for one session.
The concrete, load-bearing result stands regardless of which pool-address
theory is right: **`Director_EntryDispatch` and its `SHINOBI.PPB`
counterpart are both mostly-empty dispatchers with exactly one real path
(state 1) gated behind an indexed function-pointer call.** That's the
actual next target, not the address theory.

### State 1's pointer confirmed unresolvable as-is; `Res_FixupPointers` ruled out

Tried the targeted-function approach on state 1's block directly: created
a function at `0x06040020` in Ghidra (DIRECTOR.PPB). Ghidra's own analyzer
— independently of the manual `sh2dis.py` reading — resolved the exact
same call: `(*DAT_06040090)()`, with `DAT_06040090 = 0x52680604`, then hit
"bad instruction" immediately after (same wall as the manual read). Two
independent tools agreeing rules out a script bug on our side — the value
genuinely is `0x52680604` in the file, and it genuinely isn't a valid
address to call through, by any tool's reckoning.

### Back to A.BIN sweep: confirms/extends the CD-ROM-driver verdict, one real find

Sampled two fresh ranges. `0x06036000-0x06036c00` matches the existing
"CD-ROM/file driver, not game logic" verdict exactly — generic
buffer-position/limit state machine with a type-indexed function-pointer
dispatch table, same shape as a stream/codec abstraction. Not renaming
these (low value, matches an already-documented pattern, no new
information).

`0x06039600-0x0603a300` turned out to be the same CD-ROM territory too —
most sampled functions (`0x06039674`, `0x06039a64`, `0x06039c1a`,
`0x06039e32`) build a small fixed-size buffer starting with a distinct
"command code" byte (`0x61`, `0x75`, `0x11`, `0x44`) then hand it to a
function pointer — textbook Saturn CD-block command-packet constructors
(one function per CDC command opcode). Consistent with the existing
verdict, not renaming individually (many near-identical trivial wrappers,
low value).

One real find in the same range, though:

- **`Scu_ConfigureDmaChannel`** (was `FUN_0603a15c`) — writes directly into
  `SCU_D0R`/`SCU_D0W`/`SCU_D0C` (Ghidra already had these SCU DMA register
  names), indexed by a channel number (`param_2 * 0x20`, matching the
  32-byte stride between DMA channel register blocks). High confidence —
  this is genuine SCU DMA channel configuration, sitting in the middle of
  otherwise-CD-driver code (makes sense: CD reads commonly DMA straight
  into work RAM).

**Running total: 99 functions renamed out of 529 in A.BIN.**

### Sweep conclusion: the rest of A.BIN's tail is CD-ROM/filesystem driver boilerplate

Sampled a third range, `0x06032800-0x06034e00`, to see if the CD-driver
verdict held or if this was a one-off. It holds, and clearly: small
command-buffer builders (same "opcode byte + call through function
pointer" shape as the `0x06039600` cluster), a consistent nested-struct
access pattern at `param_1+0x10` / `+0x6c` (looks like a file/device
handle's cached position or config sub-record), a seek-like function with
POSIX-errno-style negative return codes (`-11`, `-14`), and a
cached-position read/seek dispatcher. All generic filesystem/CD-block
driver plumbing, same family as everything from `0x06035000` onward.

**Conclusion for the sweep**: the stretch from roughly `0x06032800` through
`0x0603a28c` — the large majority of A.BIN's remaining unnamed functions —
is one continuous CD-ROM/filesystem driver library (not hand-written game
code, likely a Sega-provided or licensed CD-FS component). Three
independent samples across this range (this session and earlier) all
landed on the same pattern with zero exceptions. **Deprioritizing this
entire range from the sweep** — further sampling here is very unlikely to
turn up gameplay logic, and it's not useful for the PC port either (the
port already does plain `fopen`/`fread`, no need to replicate Saturn's CD
block driver). Combined with the earlier-confirmed VDP1/VDP2/SGL-internals
clusters (`0x06028000-0x0602c000` roughly) and the sound/streaming/object
clusters already fully mapped, **A.BIN's overall shape is now well
understood**: boot + hardware plumbing + a large licensed CD-FS driver,
with no gameplay logic — consistent with the project's core architectural
finding that gameplay lives in `DIRECTOR.PPB`/`SHINOBI.PPB`, not `A.BIN`.
Remaining unswept pockets are small (a few hundred bytes here and there
between confirmed clusters); worth a final targeted pass someday but not
high-value.

**Running total: 99 functions renamed out of 529 in A.BIN** (unchanged —
this pass was about characterizing territory, not naming individual
generic wrappers).

Checked the one remaining live hypothesis: does `Res_FixupPointers` (A.BIN's
confirmed relocation-fixup routine) get called anywhere in the
`DIRECTOR.PPB` load path, the way it's used for streaming-audio headers?
`get_xrefs_to(Res_FixupPointers)` in A.BIN returns exactly two callers —
**`Stream_InitFromHeader` and `Stream_SetCallback`, both streaming-audio-only.**
Nothing in `Load_DirectorPPB`/`Stage_ResetAndLoadDirector`'s traced call
chain touches it. **This hypothesis is ruled out** — A.BIN does not
relocate the overlay's internal pointers before jumping in.

**Where this leaves it**: whatever fixes up these pool values (if anything
does) must happen either inside `DIRECTOR.PPB`/`SHINOBI.PPB`'s own code
(self-relocation logic we haven't located — would need to be very early,
before `Director_EntryDispatch` is ever reached) or the pool values aren't
addresses being called through at all and the decompile's `(*DAT_...)()`
reading is itself misleading (possible if Ghidra/our manual read is
misinterpreting the instruction stream here the same way both did for the
"FFFF" gaps elsewhere in these dispatchers). Genuinely unresolved. This is
a good place to stop this specific thread — it's hit a real wall, not a
lack of effort, and further progress likely needs a different technique
(e.g. checking if any *other* address in either `.PPB` file's early bytes
looks like self-relocation/init code we haven't identified yet) rather than
more manual byte-staring at this exact spot.
