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
mode selector and `Msg_InitSlotPool`'s slot arrays. Now that we know
`DIRECTOR.PPB` is a separate dynamically-loaded overlay (see above), this
pattern makes sense: these cells are plausibly written *from DIRECTOR.PPB*,
invisible to static analysis of A.BIN alone. Worth remembering once
`DIRECTOR.PPB` analysis is usable again — don't re-chase these dead ends
purely within A.BIN.

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
