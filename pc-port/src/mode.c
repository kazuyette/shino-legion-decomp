#include "mode.h"
#include <stdio.h>

void Idle_InitMessageSystem(void)
{
    printf("[mode] enter idle/attract mode\n");
    /* TODO: port FUN_0600aaa4 sub-inits once identified. */
}

void Idle_ModeTableEntry(void)
{
    Idle_InitMessageSystem();
}

void Mode_EnterIdle(void)
{
    Idle_ModeTableEntry();
}
