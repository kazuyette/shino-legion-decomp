#include "boot.h"
#include "sound.h"
#include "object.h"
#include "mode.h"
#include "resource.h"
#include "sys.h"
#include <stdio.h>

static unsigned char s_director_buffer[256 * 1024];

void Boot_Init(void)
{
    printf("[boot] init\n");
    Obj_InitLinkedLists();
    Reset_ObjectAndChannelTables();
    /* "assets" is the extracted disc file tree, see README for layout. */
    Load_DirectorPPB("assets", s_director_buffer, sizeof(s_director_buffer));
}

/* The 4 fixed object "slots" processed every frame in the real main loop,
 * identified by their mode bitflags (docs/jump_table_functions.md, "Big
 * find: the real per-frame main loop body"). Slots 1-2 get an extra
 * param-copy step in the original (FUN_060042ac, SGL-internals territory,
 * not ported here) that slots 3-4 skip. */
static const int kObjectSlotModes[4] = {4, 8, 0x10, 0x20};

void Boot_RunFrame(void)
{
    Sys_SignalStopAndWaitAck();

    /* NOTE: the real Boot_And_MainLoop calls Stage_ResetAndLoadDirector()
     * here every single frame, but that function internally gates almost
     * all of its work behind a CD/peripheral-ready check and only does
     * real work on an actual stage transition (see docs). We don't have
     * that gating condition ported yet, so we deliberately do NOT call it
     * from here — call it explicitly when a real stage-transition event
     * fires, to avoid spamming a reload every frame on PC. */

    for (int i = 0; i < 4; i++) {
        int mode = kObjectSlotModes[i];
        Obj_ConsumeFlagAndStore(mode);
        Obj_SetTransformParam(mode, 0, 0); /* TODO: real per-slot data once DIRECTOR.PPB is readable */
        Obj_SetReadyFlag();
    }

    Snd_ChannelScheduler();
}

void Stage_ResetAndLoadDirector(void)
{
    printf("[boot] stage reset\n");
    Sys_SignalStopAndWaitAck();
    Snd_CmdStopAll();
    Res_CloseIfOpen();
    Mode_EnterIdle();
    Reset_ObjectAndChannelTables();
    Load_DirectorPPB("assets", s_director_buffer, sizeof(s_director_buffer));
}
