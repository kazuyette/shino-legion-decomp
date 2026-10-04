/* DIRECTOR scene 8: stage clear tally (0x06042840) -- work package D5, ported line by line
 * from DIRECTOR.dec.bin 0x06042840-0x06042D16 (docs/director.md "Scene 8").
 *
 * The bonuses are prepared before the hand-off: scene 2 step 0 seeds LIFE 0 / NO SHURIKEN
 * 5000 / NO DAMAGE 1 at every stage start, SHINOBI's tally (0x0606BA08) then writes
 * LIFE = HP x 1000, NO DAMAGE = 10000 or 0 and clears NO SHURIKEN when a shuriken was used.
 * Here: 160 frames of display, then 100 points per frame move from every positive bonus
 * into the score (Dir_ScoreAdd: hi-score, extra life every 30000/60000/90000 points); once
 * the three bonuses are all 0, A/B/C/Start jumps the timer to 480; past 480 the screen fades
 * to black (0x80000/frame) and the next scene is 4 (story movie of the next stage). */
#include "director_int.h"

#define S_LIFE_BONUS  0x06043D78u   /* "LIFE BONUS" */
#define S_HIGH_SCORE  0x06043D64u   /* "HIGH SCORE" */
#define S_SCORE       0x06043D70u   /* "SCORE" */
#define S_NO_SHURIKEN 0x06043D84u   /* "NO SHURIKEN BONUS" */
#define S_NO_DAMAGE   0x06043D98u   /* "NO DAMAGE BONUS" */
#define T_EXTEND      0x06014418u   /* u32[3] extra life every n points (30000/60000/90000) */

/* the sprite fields DIRECTOR writes before a plain Sprite_Draw (+0x18 untouched) */
static void StgClear_Sprite(int chr, int x, int y, int bank)
{
    uint32_t s = SPR_WORK;
    W16(s + 6, 0); W16(s + 8, 0);
    W16(s + 2, (uint32_t)x); W16(s + 4, (uint32_t)y);
    W16(s + 0x10, 0); W16(s + 0x0A, (uint32_t)bank);
    W16(s + 0x0C, 0); W16(s + 0x0E, 0);
    W16(s, (uint32_t)chr);
    Sprite_Draw(s);                                  /* 0x060068C0 */
}

/* 0x06042C6C: lives += n, clamped to 0..99; returns the change actually applied */
static int Dir_LivesAdd(int16_t n)
{
    int16_t old = RS16(V_LIVES);
    W16(V_LIVES, (uint32_t)(int32_t)(RS16(V_LIVES) + n));
    if (RS16(V_LIVES) > 99) W16(V_LIVES, 99);
    if (RS16(V_LIVES) < 0) W16(V_LIVES, 0);
    return (int16_t)(RS16(V_LIVES) - old);
}

/* 0x06042C98: DIRECTOR's own copy of SHINOBI's Score_Add 0x06062B38 (no "game mode 7"
 * test around the extra-life sound) */
static void Dir_ScoreAdd(int32_t n)
{
    int32_t old = RS32(V_SCORE);
    W32(V_SCORE, (uint32_t)old + (uint32_t)n);
    if (RS32(V_SCORE) > 99999990) W32(V_SCORE, 99999990u);
    if (R32(V_HISCORE) < R32(V_SCORE)) W32(V_HISCORE, R32(V_SCORE));    /* cmp/hs: unsigned */
    int32_t every = RS32(T_EXTEND + 4u * (uint32_t)(int32_t)RS16(V_DIFFICULTY));
    if (every == 0) return;                          /* PC guard (sdiv 0x060092BC by 0) */
    if (old / every >= RS32(V_SCORE) / every) return;
    Dir_LivesAdd(1);
    Snd_Play(0x107);                                 /* extra life */
}

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

/* 0x06042AA8: background, stage number, banner */
static void StgClear_DrawBg(void)
{
    StgClear_Sprite(10, 0, 0, 0);                    /* background RGB 320x224 */
    int16_t st = RS16(V_STAGE);                      /* already the next stage (SHINOBI ++) */
    int16_t d = (int16_t)((st >> 1) - 1);
    if (st == -1) d = 8;                             /* game finished: "9" */
    StgClear_Sprite((uint16_t)d, 128, 8, 1);         /* digit 1-9 (chars 0-8), colour bank 1 */
    StgClear_Sprite(9, 16, 24, 0);                   /* "STAGE CLEAR" banner */
}

/* 0x06042B5C: labels + numbers (8 digits with zeros for the scores, 5 for the bonuses) */
static void StgClear_DrawText(void)
{
    uint32_t s = SPR_WORK;
    W16(s + 0x10, 0); W32(s + 0x18, 0); W16(s + 0x0C, 0); W16(s + 0x0E, 0);
    Font_DrawString(S_HIGH_SCORE, 0, 48, 88);
    Font_DrawNumber(RS32(V_HISCORE), 0, 208, 88, 8, 0);
    Font_DrawString(S_SCORE, 0, 88, 112);
    Font_DrawNumber(RS32(V_SCORE), 0, 208, 112, 8, 0);
    Font_DrawString(S_LIFE_BONUS, 0, 48, 144);
    Font_DrawNumber(RS32(V_BONUS_LIFE), 0, 232, 144, 5, 0);
    Font_DrawString(S_NO_SHURIKEN, 0, 48, 168);
    Font_DrawNumber(RS32(V_BONUS_SHUR), 0, 232, 168, 5, 0);
    Font_DrawString(S_NO_DAMAGE, 0, 48, 192);
    Font_DrawNumber(RS32(V_BONUS_DMG), 0, 232, 192, 5, 0);
}

/* 0x0604295C */
static void StgClear_Tally(void)
{
    StgClear_DrawBg();
    StgClear_DrawText();
    W16(D_TIMER, R16(D_TIMER) + 1u);
    if (RS16(D_TIMER) <= 160) return;
    if (RS32(V_BONUS_LIFE) > 0) { Dir_ScoreAdd(100); W32(V_BONUS_LIFE, R32(V_BONUS_LIFE) - 100u); }
    if (RS32(V_BONUS_SHUR) > 0) { Dir_ScoreAdd(100); W32(V_BONUS_SHUR, R32(V_BONUS_SHUR) - 100u); }
    if (RS32(V_BONUS_DMG) > 0)  { Dir_ScoreAdd(100); W32(V_BONUS_DMG, R32(V_BONUS_DMG) - 100u); }
    if (RS16(D_TIMER) > 480) {
        Fade_SetTarget(0x100, 0x100, 0x100);         /* to black */
        Fade_SetDefaultSpeed(0x80000);
        Fade_StartOut();
        W16(D_TIMER, 0);
        Dir_NextStep();
        return;
    }
    if ((R32(V_BONUS_LIFE) | R32(V_BONUS_SHUR) | R32(V_BONUS_DMG)) == 0 && (Dir_Pressed() & 0x0F00))
        W16(D_TIMER, 480);                           /* A/B/C/Start: leave next frame */
}

/* 0x06042A44 */
static void StgClear_Exit(void)
{
    StgClear_DrawBg();
    StgClear_DrawText();
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
    case 1: StgClear_Tally(); break;
    case 2: StgClear_Exit(); break;
    default: break;
    }
}
