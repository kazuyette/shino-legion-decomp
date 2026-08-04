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

void Boot_RunFrame(void)
{
    /* TODO: this is where the real per-VBlank game logic from
     * FUN_06004038 goes, once traced further. */
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
