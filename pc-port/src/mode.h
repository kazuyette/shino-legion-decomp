#ifndef MODE_H
#define MODE_H

/* FUN_060096bc — idle/attract mode table entry: reset a flag, call
 * Idle_InitMessageSystem. */
void Idle_ModeTableEntry(void);

/* FUN_0600aaa4 — confirmed: resets a 5-field idle-mode message-system
 * state block to exact constants (0,0,0,1,0 — field 3 is the only one set,
 * likely an "enabled"/"ready" flag; the others aren't named with
 * confidence), then calls Msg_ResetState, Msg_InitSlotPool,
 * Idle_NoOpHook (confirmed genuinely empty on real hardware too),
 * Idle_ClearMsgState, Sys_StrCopy — all reached through function-pointer
 * literals in A.BIN, not individually re-traced. Ported: the state reset,
 * which is the only part with observable effect. */
void Idle_InitMessageSystem(void);

/* FUN_06008cc4 — was misnamed Snd_ResetOnStageChange; actually calls
 * Idle_ModeTableEntry via a fixed literal-pool pointer, then clears a flag.
 * Called from every Stage_ResetAndLoadDirector teardown. */
void Mode_EnterIdle(void);

#endif
