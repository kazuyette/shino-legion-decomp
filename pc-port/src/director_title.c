/* DIRECTOR scene 1: title / main menu / options / sound test (0x06040E76, `braf` table
 * 0x06040F14, 18 steps) -- work package D4.
 *
 * STUB: steps 0, 1, 2, 3 and 5 are the real code (0, 5 are the D1 variable setup in
 * director.c); step 4 is a simplified title loop (background + Start = GAME START,
 * idle 2280 frames = attract demo) without the menu, PRESS START, cursor, CD-DA fade;
 * options / sound test (6-17) are not there. Replace step 4 and add 6-17
 * (docs/director.md §1.2 "Scene 1"). */
#include "director_int.h"

/* 0x0604102C */
static void Title_Step1_LoadPbc(void)
{
    Res_Request(1, 1, 0, 0xFFFFFFFFu, VDP1_VRAM_BASE + 0x4740u, 0x06043840u);   /* "TITLE.PBC" */
    W32(D_PRESS_START, 1);
    Dir_NextStep();
}

/* 0x06041078 */
static void Title_Step3_Setup(void)
{
    Char_RegisterList(0, 0, 0x06087230u);            /* 7 chars */
    Pal_Copy(0, 64, 0x060871B0u);
    Director_SetMenuSoundMap();                      /* 0x06040596 */
    CheatSeq_Reset();
    Res_Request(2, 0, 0, 0xFFFFFFFFu, 0, 0x06043B88u);   /* "TITLE.PCM" = CD-DA track 2 */
    Snd_SetCdDaLevel(6, 6);
    Snd_SetCdDaPan(31, 15);
    Snd_SetMasterVolume(15);
    W16(D_MENU_CURSOR, 0);
    W16(D_MENU_BOB, 0);
    W16(D_TIMER, 0);
    W16(D_TITLE_IDLE, 0);
    Dir_DispOn();
    W32(V_DEMO, R32(V_DEMO) & 0xFF);
    Dir_NextStep();
    Dir_SetFadeOffsets(0x01000000u);
    Fade_SetupDuration(16, 1, 1);
    Fade_StartIn();
    Dir_DispOn();
}

/* 0x06041428 */
static void Title_DrawBg(void) { Dir_DrawChar(0, 0, 0, 0, 0); }

/* STUB of 0x06041184 */
static void Title_Step4_Loop_Stub(void)
{
    Title_DrawBg();
    CheatSeq_Record();
    if (!(R16(D_MENU_CURSOR) & 0x80)) {
        if ((Dir_Pressed() & 0x0800) && R16(FADE_STATE) == 0) {
            Snd_Play(0x108);
            W32(V_RESET_ACT, 0);
            W16(D_MENU_CURSOR, 0x80);                /* GAME START confirmed */
            Fade_SetTarget(0x100, 0x100, 0x100);
            Fade_SetDefaultSpeed(0x40000);
            Fade_StartOut();
            return;
        }
        W16(D_TITLE_IDLE, R16(D_TITLE_IDLE) + 1u);
        if (RS16(D_TITLE_IDLE) == 2280) {            /* 38 s idle: attract demo */
            Fade_SetTarget(0x100, 0x100, 0x100);
            Fade_SetDefaultSpeed(0x40000);
            Fade_StartOut();
        }
        if (RS16(D_TITLE_IDLE) < 2280) return;
    }
    if (R16(FADE_STATE) == 0) {
        Dir_DispOff();
        Dir_NextStep();
    }
}

/* 0x06040E76 */
void Scene1_Title(void)
{
    switch (Dir_Step()) {
    case 0: Title_NewGameDefaults(); break;          /* 0x06040F3E */
    case 1: Title_Step1_LoadPbc(); break;
    case 2: Dir_NextStep(); break;                   /* 0x06040E86: one frame */
    case 3: Title_Step3_Setup(); break;
    case 4: Title_Step4_Loop_Stub(); break;
    case 5: Title_Launch(); break;                   /* 0x060412D4 */
    case 6:                                          /* STUB: options -> back to the title */
        Dir_SetStep(3);
        break;
    default: break;
    }
}
