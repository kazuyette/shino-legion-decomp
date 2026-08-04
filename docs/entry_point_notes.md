# A.BIN entry point (0x06004000)

Classic Saturn startup pattern, matches known SGL/BIOS-style boot code:

```
06004000: MOV.L @(0xC,PC),R0 ; R0 = 0x6004010 (vector)
06004002: JSR @R0
06004006: MOV.L @(0x4,PC),R0 ; R0 = 0x600400C (vector)
06004008: JMP @R0            ; jump into main init
```

`0600401C`–`0600402A`: BSS-clear loop (writes 0 from R0 up to R1, incrementing by 4,
looping while T flag clear) — standard C runtime startup zeroing uninitialized data.

`06004038`–`06004046`: `MOV.L Rn,@-R15` × 7 — pushes R8–R14, i.e. a real function
prologue. This is the start of "main" (post-runtime-init).

Next steps: resolve the PC-relative literal pool values (the `.word` entries are
mostly 32-bit addresses split across two 16-bit words — need a pass that merges
`D0/D1/... MOV.L @(disp,PC),Rn` targets and dumps them as a literal pool table),
then walk the JSR targets at 0x6004060/6004068/600406e/6004074/600413c etc. to find
the SGL (Sega Sound/Graphics Library) init calls — Saturn games of this era almost
always link against Sega's SGL, so recognizing SGL_INIT/SGL bootstrapping will
anchor the rest of the disassembly against known library function signatures.

## Function pointer table @ 0x06004118 (10 entries, big-endian 32-bit)

R11–R14 are loaded from this table right after the BSS-clear loop (`DB32/DC33/DD33/DE34`
at 0x600404C–0x6004052), so it's read before any real work happens — almost certainly
a device/subsystem init dispatch table (BIOS-style: VDP1/VDP2/SCU/SCSP/SMPC/CD block
init routines), or the SGL bootstrap's internal callback table.

| address    | value      | file offset |
|------------|------------|-------------|
| 0x06004118 | 0x0602A8C4 | 0x268C4 |
| 0x0600411C | 0x0602A894 | 0x26894 |
| 0x06004120 | 0x0602A8A8 | 0x268A8 |
| 0x06004124 | 0x06029B40 | 0x25B40 |
| 0x06004128 | 0x06005130 | 0x01130 |
| 0x0600412C | 0x0602524C | 0x2124C |
| 0x06004130 | 0x06005840 | 0x01840 |
| 0x06004134 | 0x060057F0 | 0x017F0 |
| 0x06004138 | 0x06005B0A | 0x01B0A |
| 0x0600413C | 0x06004418 | 0x00418 |

All 10 targets fall inside the A.BIN file range (max size 0x3C000), confirming they're
internal function pointers, not hardware register addresses. Next: disassemble each of
these 10 entry points — `tools/sh2dis.py A.BIN <file_offset> 128` — to identify which
is SGL init vs. game-specific setup (SGL's `Slave_Init`/`SGL_INIT` prologues have a
recognizable shape from other decompiled Saturn SGL titles).

Known limitation of `sh2dis.py`: it disassembles linearly and doesn't yet distinguish
code from embedded literal pools, so raw dumps across a literal pool will show garbage
`.word` and misparsed branches — always cross-check `.word`-heavy stretches by hand
(pairs of 16-bit halfwords = one 32-bit pointer) before trusting a MOV.L target.

## 0x06005130: BIOS/library trampoline pattern

```
06005130: MOV.L @(0x18,PC),R3   ; R3 = 0x600514C -> value 0x06000344
06005132: MOV.L @R3,R3          ; R3 = *0x06000344  (dereference vector slot)
06005134: MOV #7,R5
06005136: JMP @R3
06005138: MOV #-1,R4
```

This is the classic Saturn BIOS call idiom: load the address of a fixed low-RAM
jump-table slot, dereference it, jump through it, with R4/R5 as call args (delay-slot
MOV). 0x06000344 sits in the low Work RAM region where the Saturn BIOS (or SGL/SBL)
installs its jump table for user code to call into — so this function is a thin
wrapper around a BIOS/library call, not game logic. Two call sites seen so far pass
(R4=-1, R5=7) and (R4=-4, R5=0) — likely interrupt-level or CD-block priority args.

Same idiom is worth grepping for everywhere (`MOV.L @(d,PC),Rn` immediately followed
by `MOV.L @Rn,Rn` then `JMP @Rn`) — each hit is a BIOS/SGL call boundary and a good
place to stop treating the code as "unknown game logic."
