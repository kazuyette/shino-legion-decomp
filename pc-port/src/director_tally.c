/* DIRECTOR scene 8: stage clear tally (0x06042840) -- work package D5.
 *
 * STUB: step 0 (StgClear_Init 0x0604288C: font, STGCLEAR.PBC, chars, palette, fade-in,
 * stage-clear tune) is the real code; steps 1/2 only draw the background and the banner,
 * wait for Start (or 480 frames) and fade out to scene 4. To do: StgClear_Tally 0x0604295C,
 * StgClear_DrawBg 0x06042AA8 (stage digit), StgClear_DrawText 0x06042B5C, Score_Add
 * 0x06042C98, Lives_Add 0x06042C6C (docs/director.md "Scene 8"). */
#include "director_int.h"

/* 0x0604288C */
static void StgClear_Init(void)
{
    Font_Init(0x200, 64, 0x58800);
    Res_Request(1, 1, 0, 0xFFFFFFFFu, VDP1_VRAM_BASE + 0x4740u, 0x06043D54u);   /* "STGCLEAR.PBC" */
    Char_RegisterList(0, 0, 0x06087354u);            /* 11 chars */
    Pal_Copy(0, 32, 0x06087314u);
    Dir_SetFadeOffsets(0x01000000u);
    Fade_SetupDuration(16, 1, 1);
    Fade_StartIn();
    W16(D_TIMER, 0);
    Snd_StopAll();
    Snd_Play(1);                                     /* stage clear tune */
    Dir_DispOn();
    Dir_NextStep();
}

/* STUB of StgClear_DrawBg 0x06042AA8 / StgClear_DrawText 0x06042B5C */
static void StgClear_Draw_Stub(void)
{
    Dir_DrawChar(10, 0, 0, 0, 0);                    /* background RGB 320x224 */
    Dir_DrawChar(9, 16, 24, 0, 0);                   /* "STAGE CLEAR" banner */
    Font_DrawText("SCORE", 0, 88, 112);
    Font_DrawNumber(RS32(V_SCORE), 0, 184, 112, 8, 0);
}

/* STUB of StgClear_Tally 0x0604295C */
static void StgClear_Tally_Stub(void)
{
    StgClear_Draw_Stub();
    W16(D_TIMER, R16(D_TIMER) + 1u);
    if ((Dir_Pressed() & 0x0F00) && RS16(D_TIMER) < 480) W16(D_TIMER, 480);
    if (RS16(D_TIMER) >= 480) {
        Fade_SetTarget(0x100, 0x100, 0x100);
        Fade_SetDefaultSpeed(0x100000);
        Fade_StartOut();
        Dir_NextStep();
    }
}

/* 0x06042A44 */
static void StgClear_Exit(void)
{
    StgClear_Draw_Stub();                            /* 0x06042AA8 + 0x06042B5C */
    if (R16(FADE_STATE) != 0) return;
    Dir_SetFadeOffsets(0);
    Dir_DispOff();
    Dir_SetScene(4);                                 /* story movie of the next stage */
}

/* 0x06042840 */
void Scene8_StageClear(void)
{
    switch (Dir_Step()) {
    case 0: StgClear_Init(); break;
    case 1: StgClear_Tally_Stub(); break;
    case 2: StgClear_Exit(); break;
    default: break;
    }
}
