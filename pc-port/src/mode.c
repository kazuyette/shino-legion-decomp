#include "mode.h"
#include <stdio.h>

/* Confirmed 5-field state block reset by Idle_InitMessageSystem, exact
 * values from the decompile (0, 0, 0, 1, 0). Field names are our best
 * guess, not individually confirmed. */
typedef struct {
    int field0;
    int field1;
    int field2;
    int enabled; /* the only field set nonzero (1) -- likely "ready"/"enabled" */
    int field4;
} IdleMsgState;

static IdleMsgState s_idle_msg_state;

void Idle_InitMessageSystem(void)
{
    s_idle_msg_state.field0 = 0;
    s_idle_msg_state.field1 = 0;
    s_idle_msg_state.field2 = 0;
    s_idle_msg_state.enabled = 1;
    s_idle_msg_state.field4 = 0;
    /* Msg_ResetState/Msg_InitSlotPool/Idle_ClearMsgState/Sys_StrCopy calls
     * not ported -- reached via function-pointer literals in A.BIN, not
     * individually traced (see mode.h). Idle_NoOpHook is confirmed to do
     * nothing, safely skipped. */
    printf("[mode] enter idle/attract mode\n");
}

void Idle_ModeTableEntry(void)
{
    Idle_InitMessageSystem();
}

void Mode_EnterIdle(void)
{
    Idle_ModeTableEntry();
}
