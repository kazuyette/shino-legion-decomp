/* DIRECTOR scenes 5/6: continue / game over (0x06042D18, `braf` table 0x06042D94, 20 steps)
 * and scene 7: staff roll (0x060432C4) -- work package D6, ported line by line from
 * DIRECTOR.dec.bin 0x06042D18-0x060435BE (docs/director.md "Scene 5 / 6", "Scene 7").
 *
 * Continue (continues left): CNTCOUNT.CPK countdown with "CREDIT-n" drawn over it; Start
 * between timer 50 and 519 stops it, continues - 1, plays CONTINUE.CPK from the LWRAM copy
 * (movie id 0x100E), reloads SHOU.CHR and restarts act 1 (Continue_Restart, scene 2). If the
 * countdown runs out, its last frame stays up with HIGH SCORE / SCORE (step 18).
 * No continue left: GAMEOVER.CPK, then the same score screen. Score screen: text fades in
 * (colour calc 3 -> 0, 32 frames), Start jumps to 599, at 600 fade to black -> scene 0.
 * Staff roll: STAFFROL.PBC lines scroll up 0.5 px/frame until the end marker of the script
 * 0x06043E00 is on screen, 900 frames hold (Start skips after 120), fade -> scene 0. */
#include "director_int.h"
#include "movie.h"

#define V_SR_SCROLL  0x060DD028u    /* s32 staff roll scroll, 16.16 */
#define V_SR_END     0x060DD02Cu    /* s32 end marker reached */
#define SR_SCRIPT    0x06043E00u    /* {u16 char, u16 x, s16 dy, 0} ..., char < 0 = end */

/* the sprite fields DIRECTOR writes before a plain Sprite_Draw (+0x18 untouched) */
static void Dir_Sprite(int chr, int x, int y)
{
    uint32_t s = SPR_WORK;
    W16(s + 6, 0); W16(s + 8, 0);
    W16(s + 2, (uint32_t)x); W16(s + 4, (uint32_t)y);
    W16(s + 0x10, 0); W16(s + 0x0A, 0);
    W16(s + 0x0C, 0); W16(s + 0x0E, 0);
    W16(s, (uint32_t)chr);
    Sprite_Draw(s);                                  /* 0x060068C0 */
}

/* 0x0604302E: "CREDIT-" (char 10) and the continues left as a digit char (0-9) */
static void Continue_DrawCredits(void)
{
    Dir_Sprite(10, 176, 16);
    Dir_Sprite(R16(V_CONTINUES), 288, 16);
}

/* 0x06042DC2 */
static void Continue_Init(void)
{
    Dir_SetFadeOffsets(0);
    Font_Init(0x200, 64, 0x8000);
    Lz_DecompressRam(0x0608742Cu, VDP1_VRAM_BASE + 0x4740u);   /* digits 0-9 + "CREDIT-" */
    Char_RegisterList(0, 0, 0x060873D0u);            /* 11 chars */
    Pal_Copy(0, 16, 0x060873B0u);
    W16(D_TIMER, 0);
    Dir_NextStep();
    if (RS16(V_CONTINUES) == 0) Dir_SetStep(16);     /* no continue: game over */
}

/* 0x06042E58 */
static void Continue_StartCountdown(void)
{
    Res_Request(1, 0, 0, 0xFFFFFFFFu, LWRAM_BASE, 0x06043DACu);   /* "CONTINUE.CPK" -> LWRAM */
    Movie_SetPlaylistRam(0x06043DA8u, 0x100, 0x58800, 3);       /* {13 CNTCOUNT, -1} */
    W16(D_TIMER, 0);
    Dir_NextStep();
}

/* 0x06042EB0 */
static void Continue_Countdown(void)
{
    Movie_Update();
    Continue_DrawCredits();
    if (RS32(MOVIE_STATE) < 0) {                     /* countdown over: score screen */
        Movie_DrawFrame();
        Dir_SetStep(18);
        W16(D_TIMER, 0);
    }
    W16(D_TIMER, R16(D_TIMER) + 1u);
    if (RS16(D_TIMER) >= 50 && RS16(D_TIMER) < 520 && (Dir_Pressed() & 0x0800)) {
        Movie_Stop();
        Dir_NextStep();
        W16(V_CONTINUES, R16(V_CONTINUES) - 1u);
    }
}

/* 0x06042F40 */
static void Continue_StartClip(void)
{
    Movie_DrawFrame();
    Continue_DrawCredits();
    Movie_SetPlaylistRam(0x06043DBAu, 0x100, 0x58800, 3);       /* {0x100E = CONTINUE.CPK in LWRAM, -1} */
    Dir_NextStep();
}

/* 0x06042F64 */
static void Continue_PlayClip(void)
{
    if (RS32(MOVIE_STATE) != 2) Movie_DrawFrame();
    Movie_Update();
    Continue_DrawCredits();
    if (RS32(MOVIE_STATE) >= 0) return;
    W16(D_TIMER, 0);
    Movie_DrawFrame();
    Res_Request(1, 0, 0, 0xFFFFFFFFu, LWRAM_BASE, 0x06043DC0u);   /* "SHOU.CHR" (overwritten) */
    Dir_NextStep();
}

/* 0x06042D46 (inline in the dispatcher; same code out of line at 0x060430D8) */
static void GameOver_StartMovie(void)
{
    Movie_SetPlaylistRam(0x06043DCAu, 0x100, 0x58800, 3);       /* {16 GAMEOVER, -1} */
    Dir_NextStep();
}

/* 0x060430F2 */
static void GameOver_PlayMovie(void)
{
    Movie_Update();
    if (RS32(MOVIE_STATE) >= 0) return;
    Movie_DrawFrame();                               /* keep the last frame */
    Dir_NextStep();
    W16(D_TIMER, 0);
}

/* 0x06043140 */
static void GameOver_ShowScore(void)
{
    Movie_DrawFrame();
    uint32_t s = SPR_WORK;
    W16(s + 0x10, 0); W32(s + 0x18, 0); W16(s + 0x0C, 0); W16(s + 0x0E, 0);
    if (RS16(D_TIMER) < 32)                          /* text fade-in: colour calc 3,2,1,0 */
        W16(s + 0x0E, (uint16_t)(3 - (RS16(D_TIMER) >> 3)));
    Font_DrawString(0x06043DD0u, 0, 80, 160);        /* "HIGH SCORE" */
    Font_DrawNumber(RS32(V_HISCORE), 0, 184, 160, 8, 0);
    Font_DrawString(0x06043DDCu, 0, 120, 184);       /* "SCORE" */
    Font_DrawNumber(RS32(V_SCORE), 0, 184, 184, 8, 0);
    if (RS16(D_TIMER) < 600 && (Dir_Pressed() & 0x0800)) W16(D_TIMER, 599);
    W16(D_TIMER, R16(D_TIMER) + 1u);
    if (RS16(D_TIMER) < 600) return;
    if (RS16(D_TIMER) == 600) {
        Fade_SetTarget(0x100, 0x100, 0x100);         /* to black */
        Fade_SetDefaultSpeed(0x80000);
        Fade_StartOut();
    }
    if (R16(FADE_STATE) != 0) return;
    W16(D_TIMER, 0);
    Dir_NextStep();
}

/* 0x0604327E */
static void GameOver_Exit(void)
{
    Dir_SetFadeOffsets(0);
    Dir_DispOff();
    Dir_SetScene(0);                                 /* SEGA logo -> title */
}

/* 0x06042D18 */
void Scene5_ContinueGameOver(void)
{
    switch (Dir_Step()) {
    case 0:  Continue_Init(); break;
    case 1:  Continue_StartCountdown(); break;
    case 2:  Continue_Countdown(); break;
    case 3:  Continue_StartClip(); break;
    case 4:  Continue_PlayClip(); break;
    case 5:  Continue_Restart(); break;              /* 0x06042FE4 (director.c) */
    case 16: GameOver_StartMovie(); break;
    case 17: GameOver_PlayMovie(); break;
    case 18: GameOver_ShowScore(); break;
    case 19: GameOver_Exit(); break;
    default: break;                                  /* 6-15, >= 20: nothing */
    }
}

/* ------------------------------------------------------------ scene 7: staff roll */

/* 0x060434DC: y accumulates the dy of every entry (16.16); every line is drawn each frame
 * at y - scroll (Sprite_Draw culls). The end marker (char 0x1000) is drawn like the others
 * and sets V_SR_END once scroll + 224 > its y. */
static void StaffRoll_Draw(void)
{
    uint32_t y = 0;
    for (uint32_t i = 0; RS16(SR_SCRIPT + 8u * i) >= 0; i++) {
        uint32_t e = SR_SCRIPT + 8u * i;
        y += (uint32_t)(int32_t)RS16(e + 4) << 16;
        if (R16(e) == 0x1000 && (int32_t)(R32(V_SR_SCROLL) + 0x00E00000u) > (int32_t)y)
            W32(V_SR_END, 1);
        Dir_Sprite(R16(e), R16(e + 2), (int16_t)((y - R32(V_SR_SCROLL)) >> 16));
    }
}

/* 0x060432F4 */
static void StaffRoll_Init(void)
{
    Dir_SetFadeOffsets(0);
    Res_Request(1, 1, 0, 0xFFFFFFFFu, VDP1_VRAM_BASE + 0x4740u, 0x06043DE4u);   /* "STAFFROL.PBC" */
    Char_RegisterList(0, 0, 0x06087BB0u);            /* 63 chars */
    Pal_Copy(0, 16, 0x06087B90u);
    Res_Request(2, 0, 0, 0xFFFFFFFFu, 0, 0x06043DF4u);   /* "ENDING.PCM" = CD-DA track 3 */
    Snd_SetCdDaLevel(6, 6);
    Snd_SetCdDaPan(31, 15);
    Snd_SetMasterVolume(15);
    Dir_DispOn();
    W16(D_TIMER, 0);
    Dir_NextStep();
    W32(V_SR_SCROLL, 0);
    W32(V_SR_END, 0);
}

/* 0x060433CC */
static void StaffRoll_Scroll(void)
{
    StaffRoll_Draw();
    W32(V_SR_SCROLL, R32(V_SR_SCROLL) + 0x8000u);   /* 0.5 px per frame */
    if (R32(V_SR_END) == 0) return;
    Dir_NextStep();
    W16(D_TIMER, 0);
}

/* 0x0604340C */
static void StaffRoll_Hold(void)
{
    StaffRoll_Draw();
    if ((Dir_Pressed() & 0x0800) && RS16(D_TIMER) > 120 && RS16(D_TIMER) < 900)
        W16(D_TIMER, 899);
    W16(D_TIMER, R16(D_TIMER) + 1u);
    if (RS16(D_TIMER) < 900) return;
    if (RS16(D_TIMER) == 900) {
        Fade_SetTarget(0x100, 0x100, 0x100);
        Fade_SetDefaultSpeed(0x80000);
        Fade_StartOut();
    }
    if (R16(FADE_STATE) != 0) return;
    Dir_NextStep();
}

/* 0x060434B0 */
static void StaffRoll_Exit(void)
{
    Dir_SetFadeOffsets(0);
    Dir_DispOff();
    Dir_SetScene(0);
}

/* 0x060432C4 */
void Scene7_StaffRoll(void)
{
    switch (Dir_Step()) {
    case 0: StaffRoll_Init(); break;
    case 1: StaffRoll_Scroll(); break;
    case 2: StaffRoll_Hold(); break;
    case 3: StaffRoll_Exit(); break;
    default: break;
    }
}
