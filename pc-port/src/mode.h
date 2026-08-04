#ifndef MODE_H
#define MODE_H

/* FUN_060096bc — idle/attract mode table entry: reset a flag, call
 * Idle_InitMessageSystem. */
void Idle_ModeTableEntry(void);

/* FUN_0600aaa4 — zeroes idle/attract-mode state globals and calls sub-inits. */
void Idle_InitMessageSystem(void);

/* FUN_06008cc4 — was misnamed Snd_ResetOnStageChange; actually calls
 * Idle_ModeTableEntry via a fixed literal-pool pointer, then clears a flag.
 * Called from every Stage_ResetAndLoadDirector teardown. */
void Mode_EnterIdle(void);

#endif
