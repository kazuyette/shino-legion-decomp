/* DIRECTOR.PPB core (work package D1), ported from DIRECTOR.dec.bin at 0x06040000.
 * See docs/director.md and director_int.h. */
#include "director.h"
#include "director_int.h"
#include "movie.h"
#include <stdio.h>

void Dir_DrawChar(int chr, int x, int y, int bank, int ccalc)
{
    uint32_t s = SPR_WORK;
    W16(s + 6, 0); W16(s + 8, 0);
    W16(s + 2, (uint32_t)x); W16(s + 4, (uint32_t)y);
    W16(s + 0x10, 0); W32(s + 0x18, 0);
    W16(s + 0x0A, (uint32_t)bank); W16(s + 0x0C, 0); W16(s + 0x0E, (uint32_t)ccalc);
    W16(s, (uint32_t)chr);
    Sprite_Draw(s);
}

/* ------------------------------------------------------------------ entry */

/* 0x06040000 */
void Director_Entry(void)
{
    switch (R16(G_DIR_MODE)) {
    case 0: Director_Init(); break;                  /* 0x06040094 */
    case 1: Director_RunScene(); break;              /* 0x0604027A inlined */
    case 2:                                          /* 0x060402CC inlined */
        Load_ShinobiPPB();                           /* 0x06004E6A: queued, loads next frame */
        W16(G_DIR_MODE, 0);
        break;
    case 3: break;                                   /* idle */
    case 4: case 5: case 6: case 7:
        W16(G_DIR_MODE, R16(G_DIR_MODE) + 1u);
        break;
    default: break;
    }
}

/* 0x06040094 */
void Director_Init(void)
{
    /* colour offset A = 0 (each write preceded by 0x06009254: VDP2 shadow dirty) */
    W16(0x0603B49Eu, 0); W16(0x0603B4A0u, 0); W16(0x0603B4A2u, 0);
    Fade_Reset();                                    /* 0x06006F56 */
    /* SclConfig display enable of NBG0..NBG3 off: DIRECTOR draws only VDP1 sprites */
    Ram_Ptr(0x06020D08u)[0] = 0; Ram_Ptr(0x06020D08u + 0x4C)[0] = 0;
    Ram_Ptr(0x06020D08u + 0x98)[0] = 0; Ram_Ptr(0x06020D08u + 0xE4)[0] = 0;
    /* 0x0602B344(0,0), 0x0602B344(0x100,0): VDP2 colour-calculation rates (not emulated) */
    W16(0x0603B46Cu, (R16(0x0603B46Cu) & 0x0FFF) | 0x2000);          /* SPCTL */
    W16(0x0603B46Cu, R16(0x0603B46Cu) & 0xF0FF);
    W16(0x0603B46Cu, (R16(0x0603B46Cu) & 0xFFF0) | 3);              /* sprite type 3 */
    W16(0x0603B46Cu, (R16(0x0603B46Cu) & 0xFFDF) | 0x20);           /* palette + RGB sprites */
    /* CCRSA..CCRSD: sprite colour-calc ratios {0, 8, 16, 24, 31, 31, 31, 31} */
    W16(0x0603B48Au, R16(0x0603B48Au) & 0xFF00);
    W16(0x0603B48Au, (R16(0x0603B48Au) & 0x00FF) | 0x0800);
    W16(0x0603B48Cu, (R16(0x0603B48Cu) & 0xFF00) | 0x10);
    W16(0x0603B48Cu, (R16(0x0603B48Cu) & 0x00FF) | 0x1800);
    W16(0x0603B48Eu, (R16(0x0603B48Eu) & 0xFF00) | 31);
    W16(0x0603B48Eu, (R16(0x0603B48Eu) & 0x00FF) | 0x1F00);
    W16(0x0603B490u, (R16(0x0603B490u) & 0xFF00) | 31);
    W16(0x0603B490u, (R16(0x0603B490u) & 0x00FF) | 0x1F00);
    W32(SPR_ZOOM, 0x10000);                          /* global sprite zoom 1.0 */
    W32(D_STEP, 0);
    Vdp2_DisplayOff();                               /* 0x06005BB4 */
    W16(G_DIR_MODE, R16(G_DIR_MODE) + 1u);
}

/* 0x0604027A: the scene table 0x060435C0 is copied to the stack and indexed with
 * *0x060252CC; the handlers are matched by their original address. */
void Director_RunScene(void)
{
    static uint32_t lastBad = 0xFFFFFFFFu;
    uint32_t scene = R32(G_DIR_SCENE);
    uint32_t fn = scene < 9 ? R32(D_SCENE_TABLE + 4u * scene) : 0;
    switch (fn) {
    case 0x0604059Eu: Scene0_Logo(); break;              /* scenes 0, 3 */
    case 0x06040E76u: Scene1_Title(); break;
    case 0x06042668u: Scene2_StageStart(); break;
    case 0x060407F2u: Scene4_Story(); break;
    case 0x06042D18u: Scene5_ContinueGameOver(); break;  /* scenes 5, 6 */
    case 0x060432C4u: Scene7_StaffRoll(); break;
    case 0x06042840u: Scene8_StageClear(); break;
    default:
        if (scene != lastBad) printf("[director] scene %u: handler %08X unknown\n", (unsigned)scene, (unsigned)fn);
        lastBad = scene;
        break;
    }
}

/* 0x06040596 */
void Director_SetMenuSoundMap(void) { Snd_SetAreaMap(0x060443EEu); }

/* ------------------------------------------------------- scene 0: SEGA logo */

/* 0x060405F6 */
static void Logo_Step0_Init(void)
{
    Dir_DispOff();
    W32(V_RESET_ACT, 1);
    CheatSeq_Reset();
    W16(0x06087DB6u, 0);
    Dir_NextStep();
}

/* 0x06040640 */
static void Logo_Step1_Load(void)
{
    if (R32(V_LOGO_SEEN) == 0) {                     /* first boot: the BIOS showed SEGA */
        W32(D_STEP, R32(D_STEP) + 2);
        W32(V_LOGO_SEEN, 1);
        return;
    }
    Dir_SetFadeOffsets(0x01000000u);
    Fade_SetupDuration(64, 1, 1);                    /* 0x060072AC */
    Fade_StartIn();                                  /* 0x06006FB8 */
    Snd_StopAll();
    Lz_DecompressRam(0x0608466Au, VDP1_VRAM_BASE + 0x4740u);   /* SEGA logo, RGB 320x224 */
    Char_RegisterList(0, 0, 0x060870B0u);
    W16(D_TIMER, 0);
    Dir_DispOn();
    Dir_NextStep();
}

/* 0x060406F4 */
static void Logo_Step2_Show(void)
{
    W16(D_TIMER, R16(D_TIMER) + 1u);
    if (RS16(D_TIMER) >= 240) {
        if (RS16(D_TIMER) == 240) {
            Fade_SetTarget(0x100, 0x100, 0x100);     /* 0x06006F48: to black */
            Fade_SetDefaultSpeed(0x80000);           /* 0x06007234 */
            Fade_StartOut();                         /* 0x06006FDE */
        }
        if (R16(FADE_STATE) == 0) {
            Dir_NextStep();
            Dir_DispOff();
        }
    }
    Dir_DrawChar(0, 0, 0, 0, 0);
}

/* 0x0604077A */
static void Logo_Step3_End(void)
{
    Dir_SetFadeOffsets(0);
    W32(V_RESET_ACT, 1);
    Dir_NextStep();
}

/* 0x0604059E */
void Scene0_Logo(void)
{
    switch (Dir_Step()) {
    case 0: Logo_Step0_Init(); break;
    case 1: Logo_Step1_Load(); break;
    case 2: Logo_Step2_Show(); break;
    case 3: Logo_Step3_End(); break;
    case 4:                                          /* 0x060405C4 */
        Dir_SetScene(1);
        Dir_DispOff();
        break;
    default: break;
    }
}

/* -------------------------------------------------- new game / launch / continue */

/* 0x06040F3E (title step 0) */
void Title_NewGameDefaults(void)
{
    Dir_SetFadeOffsets(0);
    W16(D_MENU_CURSOR, 0);
    W16(D_MENU_BOB, 0);
    W16(D_TIMER, 0);
    Dir_NextStep();
    W32(V_RESET_ACT, 1);
    W16(V_CHEATS, 0);
    W16(V_STAGE, 0);
    W16(V_ONEUP_TOKENS, 0);
    uint32_t d = 2u * R16(V_DIFFICULTY);
    W16(V_HP_MAX, R16(T_HP_MAX + d));
    W16(V_LIVES, R16(T_LIVES + d));
    W16(V_CONTINUES, R16(T_CONTINUES + d));
    W16(V_MAGIC, 0);
    W32(V_SCORE, 0);
    W16(V_NOT_FIRST, 0);
    W16(V_SHURIKENS, R16(V_SHUR_INIT));
    W16(V_MAGIC_AVAIL, 0);
    W16(V_STAGE_STATE, 0);
}

/* 0x060412D4 (title step 5) */
void Title_Launch(void)
{
    Dir_SetFadeOffsets(0);
    Res_CloseIfOpen();                               /* 0x060048D8 */
    uint32_t d = 2u * R16(V_DIFFICULTY);
    W16(V_HP_MAX, R16(T_HP_MAX + d));
    W16(V_LIVES, R16(T_LIVES + d));
    W16(V_CONTINUES, R16(T_CONTINUES + d));
    uint16_t cur = R16(D_MENU_CURSOR);
    if (!(cur & 0x80)) {                             /* idle: attract demo */
        W16(G_DIR_MODE, R16(G_DIR_MODE) + 1u);       /* -> Load_ShinobiPPB */
        W32(V_RESET_ACT, 1);
        Res_Request(1, 0, 0, 0xFFFFFFFFu, LWRAM_BASE, 0x06043870u);   /* "SHOU.CHR" */
        W16(V_NOT_FIRST, 1);
        W32(V_DEMO, R32(V_DEMO) & 0xFF);
        W16(V_STAGE, (uint32_t)(int16_t)(R32(V_DEMO) & 0xFF));
        W32(V_DEMO, R32(V_DEMO) + 2);
        if (RS32(V_DEMO) > 9) W32(V_DEMO, 0);
        W32(V_DEMO, R32(V_DEMO) | 0x8000);
    } else if (cur & 1) {                            /* OPTIONS */
        Dir_NextStep();
    } else {                                         /* GAME START */
        Dir_SetScene(2);
        W16(V_STAGE, 0);
        if (CheatSeq_Match(0x0604384Au)) {           /* Start A B C B A Start */
            W16(V_HP_MAX, 1); W16(V_LIVES, 0); W16(V_CONTINUES, 0);
        }
        if (CheatSeq_Match(0x0604385Cu)) {           /* Start A Z B Y C X Start */
            W16(V_LIVES, 99); W16(V_CONTINUES, 9);
        }
    }
}

/* 0x06042FE4 (continue step 5) */
void Continue_Restart(void)
{
    W16(V_SHURIKENS, R16(V_SHUR_INIT));
    W16(V_LIVES, R16(T_LIVES + 2u * R16(V_DIFFICULTY)));
    W16(V_MAGIC_AVAIL, 0);
    W16(V_ONEUP_TOKENS, 0);
    W16(V_STAGE, R16(V_STAGE) & 0xFFFE);             /* back to act 1 */
    W16(V_STAGE_STATE, 0);
    Dir_SetScene(2);
    Dir_DispOff();
}

/* ------------------------------------------------------------------ cheats */

/* 0x0604223C */
void CheatSeq_Reset(void)
{
    W32(D_CHEAT_COUNT, 0);
    for (uint32_t a = D_CHEAT_BUF; a < D_CHEAT_BUF + 0x80; a += 2) W16(a, 0);
}

/* 0x0604225E */
void CheatSeq_Record(void)
{
    if (RS32(D_CHEAT_COUNT) >= 64) return;
    uint16_t p = Dir_Pressed() & 0xFF70;
    if (!p) return;
    W16(D_CHEAT_BUF + 2u * R32(D_CHEAT_COUNT), p);
    W32(D_CHEAT_COUNT, R32(D_CHEAT_COUNT) + 1);
}

/* 0x060422B6 */
int CheatSeq_Match(uint32_t seq)
{
    for (uint32_t i = 0; i < 64; i++) {
        uint16_t w = R16(seq + 2 * i);
        if (w == 0xFFFF) break;
        if (R16(D_CHEAT_BUF + 2 * i) != w) return 0;
    }
    return 1;
}

/* ------------------------------------------------------ scene 2: stage start */

/* 0x06042692 */
static void StgStart_Step0_InitBonus(void)
{
    Dir_SetFadeOffsets(0);
    W32(V_BONUS_LIFE, 0);
    W32(V_BONUS_SHUR, 5000);
    W32(V_BONUS_DMG, 1);
    W16(D_TIMER, 0);
    Dir_NextStep();
}

/* 0x060426E4: {STGSTRTn, -1}, not skippable (flags 3) */
static void StgStart_Step1_StartMovie(void)
{
    int16_t list[2];
    list[0] = RS16(0x06043D40u + 2u * (uint32_t)(int32_t)(RS16(V_STAGE) >> 1));
    list[1] = -1;
    Movie_SetPlaylist(list, 0x100, 0x58800, 3);
    Dir_NextStep();
}

/* 0x06042734 */
static void StgStart_Step2_PlayMovie(void)
{
    Movie_Update();
    if (RS32(MOVIE_STATE) < 0) {                     /* ended: hold the last frame */
        Movie_DrawFrame();
        if (RS16(D_TIMER) < 120) W16(D_TIMER, R16(D_TIMER) + 1u);
    }
    if (RS16(D_TIMER) < 120 && (Dir_Pressed() & 0x0800)) W16(D_TIMER, 120);
    if (RS16(D_TIMER) < 120) return;
    if (RS16(D_TIMER) == 120) {
        Fade_SetTarget(0x100, 0x100, 0x100);
        Fade_SetDefaultSpeed(0x100000);
        Fade_StartOut();
        W16(D_TIMER, R16(D_TIMER) + 1u);
    }
    if (R16(FADE_STATE) == 0) Dir_NextStep();
}

/* 0x060427F0 */
static void StgStart_Step3_LaunchStage(void)
{
    Movie_Stop();                                    /* 0x06040AD6 */
    Dir_SetFadeOffsets(0);
    Dir_DispOff();
    if (RS16(V_STAGE) == 0 && RS16(V_NOT_FIRST) == 0) {
        Dir_SetScene(4);                             /* TOUJYOU before 1-1 */
    } else {
        W16(G_DIR_MODE, R16(G_DIR_MODE) + 1u);       /* -> SHINOBI.PPB */
    }
}

/* 0x06042668 */
void Scene2_StageStart(void)
{
    switch (Dir_Step()) {
    case 0: StgStart_Step0_InitBonus(); break;
    case 1: StgStart_Step1_StartMovie(); break;
    case 2: StgStart_Step2_PlayMovie(); break;
    case 3: StgStart_Step3_LaunchStage(); break;
    default: break;
    }
}

/* ------------------------------------------------------- scene 4: story movie */

/* 0x06040824 */
static void Story_Step0_PreloadShou(void)
{
    if (RS16(V_STAGE) == 0)
        Res_Request(1, 0, 0, 0xFFFFFFFFu, LWRAM_BASE, 0x06043614u);   /* "SHOU.CHR" */
    Dir_NextStep();
}

/* 0x06040850: playlists 0x0604361E (4 x s16 each): k0 TOUJYOU, k1-6 DEMO_1..6, k7 DEMO_8,
 * k8 DEMO_9, k9 DEMO_10 (stage 17 = before 9-2), k10 ENDING (stage -1) */
static void Story_Step1_StartMovie(void)
{
    int16_t st = RS16(V_STAGE);
    int16_t k = (int16_t)(st >> 1);
    uint16_t flags = 0;
    if (st == 17) k = 9;
    if (st == -1) { k = 10; flags = 2; }
    if (st == 0) flags = 1;
    Movie_SetPlaylistRam(0x0604361Eu + 8u * (uint32_t)(int32_t)k, 0x100, 0x58800, flags);
    Dir_NextStep();
}

/* 0x060408C4 */
static void Story_Step2_PlayMovie(void)
{
    Movie_Update();
    if (RS32(MOVIE_STATE) >= 0) return;
    Dir_DispOff();
    int16_t st = RS16(V_STAGE);
    if (st == 17 || st == 0) W16(G_DIR_MODE, R16(G_DIR_MODE) + 1u);   /* -> SHINOBI */
    else if (st == -1) Dir_SetScene(7);                                /* staff roll */
    else Dir_SetScene(2);                                              /* STGSTRTn */
}

/* 0x060407F2 */
void Scene4_Story(void)
{
    switch (Dir_Step()) {
    case 0: Story_Step0_PreloadShou(); break;
    case 1: Story_Step1_StartMovie(); break;
    case 2: Story_Step2_PlayMovie(); break;
    default: break;
    }
}
