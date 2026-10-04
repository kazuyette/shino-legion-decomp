/* DIRECTOR scene 1: title / main menu / options / sound test (0x06040E76, `braf` table
 * 0x06040F14, 18 steps) -- work package D4. Ported line by line from DIRECTOR.dec.bin.
 *
 *   step 0  0x06040F3E Title_NewGameDefaults (director.c)
 *   step 1  0x0604102C Title_Step1_LoadPbc        step 2 0x06040E86 (one frame)
 *   step 3  0x06041078 Title_Step3_Setup          step 4 0x06041184 Title_Step4_Loop
 *   step 5  0x060412D4 Title_Launch (director.c)  step 6 0x06041744 Options_Init
 *   step 7  0x06041804 Options_Update             step 8 0x060418B8 Options_WaitFadeOut
 *   step 9  0x060418E4 Options_Exit (-> step 3)   steps 10-15: nothing
 *   step 16 0x06040ED8 (inline: sound test init)  step 17 0x0604231C SoundTest_Update
 *
 * Sprite struct 0x060250AC (SPR_WORK): +0 char, +2/+4 x/y, +6/+8 offsets, +0x0A colour bank,
 * +0x0C priority register field, +0x0E colour-calc ratio register (2 = ~50 %: dims the
 * unselected menu items; needs VDP2 sprite colour calculation in the renderer), +0x10 flags,
 * +0x18 z. */
#include "director_int.h"
#include "audio.h"
#include <stdio.h>

/* RAM private to scene 1 (docs/director.md §6) */
#define D_PAD_REPEAT  0x060DCF8Cu   /* u16 Pad_GetDirRepeat hold counter */
#define D_OPT_MUSIC   0x060DCF98u   /* s16 options MUSIC 0..23 (music test table 0x0604391C) */
#define D_OPT_SE      0x060DCF9Au   /* s16 options S.E. 0..39 (plays 0x100 + n) */
#define D_SND_BANK    0x060DCF9Cu   /* s16 loaded .SND bank (index in 0x06087180) */
#define D_CDDA_LEVEL  0x060DCF9Eu   /* s16 CD-DA fade level 6..0 */
#define D_CDDA_SUB    0x060DCFA0u   /* s16 CD-DA fade sub-timer */
#define V_AUDIO_MONO  0x06025246u   /* s16 AUDIO option: 0 stereo, 1 mono */
#define SNDDRV_PTR    0x0603BBB8u   /* u32 -> sound driver work area (+3 = mono flag) */
#define SNDT_BANK     0x060872ECu   /* u16 sound test BANK */
#define SNDT_SEQ      0x060872EEu   /* u16 sound test SEQ */
#define SNDT_ROW      0x060872F0u   /* u16 sound test row (0 file, 1 bank, 2 seq) */

#define T_DIFF_NAMES  0x0608726Cu   /* char *[3]  EASY NORMAL HARD */
#define T_ACTION_NAMES 0x06087278u  /* char *[3]  SHURIKEN KATANA JUMP */
#define T_MUSIC_NAMES 0x06087284u   /* char *[24] */
#define T_AUDIO_NAMES 0x060872E4u   /* char *[2]  STEREO MONO */
#define T_SND_FILES   0x06087180u   /* char *[10] STAGE7.SND ... KAZUMA.SND */
#define T_CDDA_FILES  0x060871A8u   /* char *[2]  TITLE.PCM ENDING.PCM */
#define T_MUSIC_TEST  0x0604391Cu   /* 24 x {u32 type (1 = CD-DA), u32 seq, u32 bank, u32 0} */
#define T_BOB         0x0604387Au   /* s16[24] title cursor bob */
#define T_OPT_ROWS    0x060438FAu   /* s16[7]  options row y */
#define T_BTN_LAYOUT  0x0600F290u   /* A.BIN: 6 x {u16 shuriken, katana, jump button, 0} */

/* ---- SBL sound driver calls only used here (SCSP not emulated: logging stubs, D9/D10) ---- */
static void Snd_StopSeq(int slot)                               /* 0x06030B24 SND_StopSeq */
{
    printf("[snd] 06030B24 stop seq slot %d\n", slot);
}
static void Snd_StartSeq(int slot, int bank, int song, int prio) /* 0x06030A82 SND_StartSeq */
{
    printf("[snd] 06030A82 start seq slot %d bank %d song %d prio %d\n", slot, bank, song, prio);
}
static void Snd_SetAllSeqVolume(int vol, int rate)              /* 0x06007DBA: 0x06008154 on the 8 slots */
{
    printf("[snd] 06007DBA all seq volume %d rate %d\n", vol, rate);
}
/* SCSP sound RAM / 68000 error words read by the sound test (0x25A00416 u16, 0x25A00420 u32):
 * not emulated (the RAM image would alias them onto Work RAM), read as 0. */
static uint32_t SndRam_Read(uint32_t a) { (void)a; return 0; }

/* sprite struct fill without +0x18 (the functions that leave it alone) */
static void Spr_Fill(int chr, int x, int y, int bank, int ccalc)
{
    uint32_t s = SPR_WORK;
    W16(s + 6, 0); W16(s + 8, 0);
    W16(s + 2, (uint32_t)x); W16(s + 4, (uint32_t)y);
    W16(s + 0x10, 0);
    W16(s + 0x0A, (uint32_t)bank); W16(s + 0x0C, 0); W16(s + 0x0E, (uint32_t)ccalc);
    W16(s, (uint32_t)chr);
}
static inline void Spr_SetCCalc(int c) { W16(SPR_WORK + 0x0E, (uint32_t)c); }

static void Fade_OutToBlack(int32_t speed)
{
    Fade_SetTarget(0x100, 0x100, 0x100);             /* 0x06006F48 */
    Fade_SetDefaultSpeed(speed);                     /* 0x06007234 */
    Fade_StartOut();                                 /* 0x06006FDE */
}

/* ------------------------------------------------------------------ title */

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

/* 0x060416F4 (also inlined at 0x060411CC / 0x06041532) */
static void Music_FadeReset(void)
{
    W16(D_CDDA_LEVEL, 6);
    W16(D_CDDA_SUB, 0);
}

/* 0x06041702: CD-DA level 6 -> 0, one step every 5 frames */
static void Music_FadeStep(void)
{
    W16(D_CDDA_SUB, R16(D_CDDA_SUB) + 1u);
    if (RS16(D_CDDA_SUB) <= 4) return;
    W16(D_CDDA_SUB, 0);
    if (RS16(D_CDDA_LEVEL) <= 0) return;
    W16(D_CDDA_LEVEL, R16(D_CDDA_LEVEL) - 1u);
    Snd_SetCdDaLevel(R16(D_CDDA_LEVEL) & 0xFF, R16(D_CDDA_LEVEL) & 0xFF);
}

/* 0x060415A8: shuriken (char 2) bobbing left of the selected item */
static void Title_DrawCursor(void)
{
    uint32_t i = (uint32_t)(int32_t)RS16(D_MENU_BOB) % 24u;   /* 0x0600CEF4: unsigned modulo */
    uint32_t s = SPR_WORK;
    W16(s + 2, (uint32_t)(RS16(T_BOB + 2u * i) + 88));
    W16(s + 4, 152);
    if ((R16(D_MENU_CURSOR) & 1) == 1) W16(s + 4, 172);
    W16(s + 6, 0); W16(s + 8, 0);
    W16(s + 0x10, 0); W16(s + 0x0A, 0); W16(s + 0x0C, 0); W16(s + 0x0E, 0);
    W16(s, 2);
    Sprite_Draw(s);
}

/* 0x06041620: GAME START (char 3) / OPTIONS (char 4), colour bank 1, the unselected one dimmed
 * (colour calc 2); the confirmed one blinks (hidden while timer & 8 == 0) */
static void Title_DrawMenuItems(void)
{
    Spr_Fill(3, 136, 152, 1, 2);
    if (!(R16(D_MENU_CURSOR) & 1)) Spr_SetCCalc(0);
    if (R16(D_MENU_CURSOR) != 0x80 || (R16(D_TIMER) & 8)) Sprite_Draw(SPR_WORK);
    Spr_Fill(4, 136, 172, 1, 2);
    if ((R16(D_MENU_CURSOR) & 1) == 1) Spr_SetCCalc(0);
    if (R16(D_MENU_CURSOR) != 0x81 || (R16(D_TIMER) & 8)) Sprite_Draw(SPR_WORK);
}

/* 0x06041458 */
static void Title_MenuUpdate(void)
{
    if (R16(FADE_STATE) != 1 && !(R16(D_MENU_CURSOR) & 0x80)) {
        W16(D_MENU_BOB, R16(D_MENU_BOB) + 1u);
        uint16_t p = Dir_Pressed();
        if (p & 0x3000) {                            /* Up / Down */
            W16(D_MENU_CURSOR, R16(D_MENU_CURSOR) ^ 1u);
            W16(D_MENU_BOB, 0);
            Snd_Play(0x10C);
        }
        if (p & 0x0800) {                            /* Start: confirm */
            W16(D_MENU_CURSOR, R16(D_MENU_CURSOR) | 0x80u);
            W16(D_MENU_BOB, 0);
            W16(D_TIMER, 0);
            Snd_Play(0x108);
        }
    }
    Title_DrawCursor();
    Title_DrawMenuItems();
    if (!(R16(D_MENU_CURSOR) & 0x80)) return;
    W16(D_TIMER, R16(D_TIMER) + 1u);
    if (RS16(D_TIMER) < 64) return;
    if (RS16(D_TIMER) == 64) {
        Fade_OutToBlack(0x80000);
        Music_FadeReset();
    }
    Music_FadeStep();
    if (R16(FADE_STATE) == 0) {
        Dir_NextStep();
        Vdp2_DisplayOff();                           /* 0x06005BB4 */
    }
}

/* 0x06041184 */
static void Title_Step4_Loop(void)
{
    Title_DrawBg();
    CheatSeq_Record();
    if (!(R16(D_MENU_CURSOR) & 0x80)) {
        W16(D_TITLE_IDLE, R16(D_TITLE_IDLE) + 1u);
        if (RS16(D_TITLE_IDLE) >= 2280) {            /* 38 s without a choice: attract demo */
            if (RS16(D_TITLE_IDLE) == 2280) {
                Fade_OutToBlack(0x40000);
                Music_FadeReset();
            }
            Music_FadeStep();
            if (R16(FADE_STATE) == 0) {
                Dir_NextStep();
                Vdp2_DisplayOff();
            }
            return;
        }
    }
    if (R32(D_PRESS_START) == 0) {
        Title_MenuUpdate();
        return;
    }
    /* "PRESS START BUTTON" phase */
    if ((Dir_Pressed() & 0x0800) && R16(FADE_STATE) != 1) {
        W32(D_PRESS_START, 0);
        Snd_Play(0x108);
        W32(V_RESET_ACT, 0);
    }
    if (R16(FRAME_COUNT) & 0x10) Dir_DrawChar(6, 85, 164, 3, 0);   /* blinking, colour bank 3 */
}

/* ------------------------------------------------------------------ options */

/* 0x06042018: Up/Down/Left/Right on press, or while held after 20 frames every 4 frames */
static uint16_t Pad_GetDirRepeat(void)
{
    static const uint16_t dir[4] = { 0x1000, 0x2000, 0x4000, 0x8000 };
    for (int i = 0; i < 4; i++)
        if (Dir_Pressed() & dir[i]) {
            W16(D_PAD_REPEAT, 0);
            return dir[i];
        }
    for (int i = 0; i < 4; i++)
        if (Dir_Held() & dir[i]) {
            if (R16(D_PAD_REPEAT) > 20 && !(R16(FRAME_COUNT) & 3)) return dir[i];
            W16(D_PAD_REPEAT, R16(D_PAD_REPEAT) + 1u);
        }
    return 0;
}

/* 0x06041744 */
static void Options_Init(void)
{
    Font_Init(0x200, 64, 0x58800);                   /* 0x06040318 */
    Dir_SetFadeOffsets(0x01000000u);
    Fade_SetupDuration(16, 1, 1);
    Fade_StartIn();
    W16(D_SND_BANK, 0);
    Res_Request(1, 2, 0, 0xFFFFFFFFu, 0, R32(T_SND_FILES));   /* STAGE7.SND (bank 0) */
    CheatSeq_Reset();
    W16(D_MENU_CURSOR, 0);
    W16(D_OPT_SE, 0);
    W16(D_OPT_MUSIC, 0);
    W16(D_PAD_REPEAT, 0);
    Dir_DispOn();
    Dir_NextStep();
}

/* 0x06041958: options background (char 1) + "OPTIONS" title (char 5, colour bank 2) */
static void Options_DrawBg(void)
{
    Spr_Fill(1, 0, 0, 0, 0);
    Sprite_Draw(SPR_WORK);
    Spr_Fill(5, 104, 24, 2, 0);
    Sprite_Draw(SPR_WORK);
}

/* 0x060419C4: rows LEVEL / SHURIKENS / CONTROL TYPE (+ A: B: C: layout) / MUSIC / S.E. /
 * AUDIO / EXIT; every row but the cursor's is dimmed (colour calc 2) */
static void Options_DrawItems(void)
{
    uint32_t s = SPR_WORK;
    int16_t cur;
    W16(s + 0x10, 0); W32(s + 0x18, 0); W16(s + 0x0C, 0);
    Spr_SetCCalc(2);
    if (RS16(D_MENU_CURSOR) == 0) Spr_SetCCalc(0);
    Font_DrawString(0x060438ACu, 0, 48, 56);                                   /* LEVEL */
    Font_DrawString(R32(T_DIFF_NAMES + 4u * (uint32_t)(int32_t)RS16(V_DIFFICULTY)), 0, 160, 56);
    Spr_SetCCalc(2);
    if (RS16(D_MENU_CURSOR) == 1) Spr_SetCCalc(0);
    Font_DrawString(0x060438B4u, 0, 48, 72);                                   /* SHURIKENS */
    Font_DrawNumber(RS16(V_SHUR_INIT), 0, 160, 72, 3, 1);
    Spr_SetCCalc(2);
    if (RS16(D_MENU_CURSOR) == 2) Spr_SetCCalc(0);
    Font_DrawString(0x060438C0u, 0, 48, 88);                                   /* CONTROL TYPE */
    Font_DrawNumber(RS16(V_CONTROL) + 1, 0, 160, 88, 1, 1);
    Font_DrawString(0x060438D0u, 0, 184, 88);                                  /* A: */
    Font_DrawString(0x060438D4u, 0, 184, 104);                                 /* B: */
    Font_DrawString(0x060438D8u, 0, 184, 120);                                 /* C: */
    for (int32_t i = 0; i < 3; i++) {                /* action i -> row of its button */
        uint16_t b = R16(T_BTN_LAYOUT + 2u * (uint32_t)(RS16(V_CONTROL) * 4 + i));
        uint16_t row;
        if (b & 0x0400) row = 0;                     /* A */
        else if (b & 0x0100) row = 1;                /* B */
        else if (b & 0x0200) row = 2;                /* C */
        else row = b;                                /* (never: the value itself is used) */
        Font_DrawString(R32(T_ACTION_NAMES + 4u * (uint32_t)i), 0, 200, (int16_t)(row * 16 + 88));
    }
    Spr_SetCCalc(2);
    if (RS16(D_MENU_CURSOR) == 3) Spr_SetCCalc(0);
    Font_DrawString(0x060438DCu, 0, 48, 136);                                  /* MUSIC */
    Font_DrawString(R32(T_MUSIC_NAMES + 4u * (uint32_t)(int32_t)RS16(D_OPT_MUSIC)), 0, 160, 136);
    Spr_SetCCalc(2);
    if (RS16(D_MENU_CURSOR) == 4) Spr_SetCCalc(0);
    Font_DrawString(0x060438E4u, 0, 48, 152);                                  /* S.E. */
    Font_DrawNumber(RS16(D_OPT_SE), 0, 160, 152, 2, 0);
    Spr_SetCCalc(2);
    if (RS16(D_MENU_CURSOR) == 5) Spr_SetCCalc(0);
    Font_DrawString(0x060438ECu, 0, 48, 168);                                  /* AUDIO */
    Font_DrawString(R32(T_AUDIO_NAMES + 4u * (uint32_t)(int32_t)RS16(V_AUDIO_MONO)), 0, 160, 168);
    Spr_SetCCalc(2);
    cur = RS16(D_MENU_CURSOR);
    if (cur == 6) Spr_SetCCalc(0);
    Font_DrawString(0x060438F4u, 0, 48, 184);                                  /* EXIT */
}

/* 0x06041C50: shuriken (char 2) at x 8, row y + 2 */
static void Options_DrawCursor(void)
{
    uint32_t s = SPR_WORK;
    W16(s + 2, 8);
    W16(s + 4, (uint32_t)(RS16(T_OPT_ROWS + 2u * (uint32_t)(int32_t)RS16(D_MENU_CURSOR)) + 2));
    W16(s + 6, 0); W16(s + 8, 0);
    W16(s + 0x10, 0); W16(s + 0x0A, 0); W16(s + 0x0C, 0); W16(s + 0x0E, 0);
    W16(s, 2);
    Sprite_Draw(s);
}

/* 0x06040E18: on AUDIO with MUSIC = 8 and S.E. = 6, presses recorded while LT is held (RT
 * not); the sequence 0x060437E0 toggles the invincibility flag 0x06014414 */
static void Options_InvincibilityCheat(void)
{
    if (RS16(D_OPT_MUSIC) != 8 || RS16(D_OPT_SE) != 6) return;
    if ((Dir_Held() & 0x88) == 0x08) CheatSeq_Record();
    if (!CheatSeq_Match(0x060437E0u)) return;
    if (R32(V_INVINCIBLE) == 0x4465268Fu) W32(V_INVINCIBLE, 0);
    else W32(V_INVINCIBLE, 0x4465268Fu);
    Snd_Play(0x108);
    CheatSeq_Reset();
}

/* 0x0604214A: C on MUSIC. Table 0x0604391C: type 1 = CD-DA (bank = TITLE/ENDING.PCM),
 * else sequence `seq` of .SND bank `bank` (0xFF = the menu bank, no load) */
static void Options_PlayMusic(void)
{
    Snd_StopAll();
    Res_CloseIfOpen();
    uint32_t e = T_MUSIC_TEST + 16u * (uint32_t)(int32_t)RS16(D_OPT_MUSIC);
    if (R32(e) == 1) {
        Res_Request(2, 0, 0, 0xFFFFFFFFu, 0, R32(T_CDDA_FILES + 4u * R32(e + 8)));
        Snd_SetCdDaLevel(6, 6);
        Snd_SetCdDaPan(31, 15);
        Snd_SetMasterVolume(15);
        return;
    }
    uint32_t bank = R32(e + 8);
    if (bank != (uint32_t)(int32_t)RS16(D_SND_BANK) && bank != 0xFF) {
        Res_Request(1, 2, 0, 0xFFFFFFFFu, 0, R32(T_SND_FILES + 4u * bank));
        W16(D_SND_BANK, (uint32_t)(int16_t)R32(e + 8));
    }
    Snd_Play((int16_t)R32(e + 4));
}

/* 0x06041C8E */
static void Options_Input(void)
{
    uint16_t r = Pad_GetDirRepeat();
    if (r & 0x1000) {                                /* Up */
        Snd_Play(0x10C);
        W16(D_MENU_CURSOR, R16(D_MENU_CURSOR) - 1u);
        if (RS16(D_MENU_CURSOR) < 0) W16(D_MENU_CURSOR, 6);
        CheatSeq_Reset();
    }
    if (r & 0x2000) {                                /* Down */
        Snd_Play(0x10C);
        W16(D_MENU_CURSOR, R16(D_MENU_CURSOR) + 1u);
        if (RS16(D_MENU_CURSOR) > 6) W16(D_MENU_CURSOR, 0);
        CheatSeq_Reset();
    }
    int16_t v;
    switch (RS16(D_MENU_CURSOR)) {
    case 0:                                          /* LEVEL 0..2, wraps */
        if (r & 0x8000) { v = RS16(V_DIFFICULTY); W16(V_DIFFICULTY, (uint32_t)(v == 2 ? 0 : v + 1)); }
        if (r & 0x4000) { v = RS16(V_DIFFICULTY); W16(V_DIFFICULTY, (uint32_t)(v != 0 ? v - 1 : 2)); }
        break;
    case 1:                                          /* SHURIKENS 0..100 step 10, wraps */
        if (r & 0x8000) { v = RS16(V_SHUR_INIT); W16(V_SHUR_INIT, (uint32_t)(v >= 100 ? 0 : v + 10)); }
        if (r & 0x4000) { v = RS16(V_SHUR_INIT); W16(V_SHUR_INIT, (uint32_t)(v == 0 ? 100 : v - 10)); }
        if ((Dir_Held() & 0x88) == 0x88) CheatSeq_Record();   /* LT + RT held */
        if (CheatSeq_Match(0x06043908u)) {           /* C A B */
            W16(V_SHUR_INIT, 999);
            Snd_Play(0x108);
            CheatSeq_Reset();
        }
        W16(V_SHURIKENS, R16(V_SHUR_INIT));
        break;
    case 2:                                          /* CONTROL TYPE 0..5, wraps */
        if (r & 0x8000) { v = RS16(V_CONTROL); W16(V_CONTROL, (uint32_t)(v >= 5 ? 0 : v + 1)); }
        if (r & 0x4000) { v = RS16(V_CONTROL); W16(V_CONTROL, (uint32_t)(v != 0 ? v - 1 : 5)); }
        break;
    case 3:                                          /* MUSIC 0..23: C plays, A/B stop */
        if (r & 0x8000) { v = RS16(D_OPT_MUSIC); W16(D_OPT_MUSIC, (uint32_t)(v >= 23 ? 0 : v + 1)); }
        if (r & 0x4000) { v = RS16(D_OPT_MUSIC); W16(D_OPT_MUSIC, (uint32_t)(v != 0 ? v - 1 : 23)); }
        if (Dir_Pressed() & 0x0200) { Options_PlayMusic(); return; }
        if (Dir_Pressed() & 0x0500) { Snd_StopAll(); Res_CloseIfOpen(); return; }
        break;
    case 4:                                          /* S.E. 0..39: C plays 0x100 + n, A/B stop */
        if (r & 0x8000) { v = RS16(D_OPT_SE); W16(D_OPT_SE, (uint32_t)(v >= 39 ? 0 : v + 1)); }
        if (r & 0x4000) { v = RS16(D_OPT_SE); W16(D_OPT_SE, (uint32_t)(v != 0 ? v - 1 : 39)); }
        if (Dir_Pressed() & 0x0200) {
            Snd_StopAll();
            Res_CloseIfOpen();
            Snd_Play((int16_t)(R16(D_OPT_SE) + 0x100));
            return;
        }
        if (Dir_Pressed() & 0x0500) { Snd_StopAll(); Res_CloseIfOpen(); return; }
        break;
    case 5:                                          /* AUDIO: Left/Right toggle */
        if (r & 0xC000) {
            W16(V_AUDIO_MONO, R16(V_AUDIO_MONO) ^ 1u);
            uint32_t drv = R32(SNDDRV_PTR);
            /* PC: the SBL driver area pointer is 0 until the sound init is ported */
            if (drv) Ram_Ptr(drv + 3)[0] = R16(V_AUDIO_MONO) == 0 ? 0 : 0x80;
            Audio_SetMono(R16(V_AUDIO_MONO) != 0);   /* PC: the driver's mono flag */
        }
        Options_InvincibilityCheat();
        return;
    default:                                         /* 6 EXIT: handled by Options_Update */
        break;
    }
}

/* 0x06041804 */
static void Options_Update(void)
{
    Options_DrawBg();
    Options_DrawItems();
    Options_DrawCursor();
    Options_Input();
    if (R32(V_INVINCIBLE) == 0x4465268Fu && (Dir_Held() & 0x0080) && (Dir_Pressed() & 0x0800)) {
        Dir_SetStep(16);                             /* RT + Start with the cheat: sound test */
        return;
    }
    if ((Dir_Pressed() & 0x0800) || ((Dir_Pressed() & 0x0200) && RS16(D_MENU_CURSOR) == 6)) {
        Fade_OutToBlack(0x80000);
        Dir_NextStep();
    }
}

/* 0x060418B8 */
static void Options_WaitFadeOut(void)
{
    Options_DrawBg();
    Options_DrawItems();
    Options_DrawCursor();
    if (R16(FADE_STATE) == 0) {
        Dir_NextStep();
        Vdp2_DisplayOff();                           /* 0x06005BB4 */
    }
}

/* 0x060418E4: back to step 3 (TITLE.PBC is still in VRAM, chars re-registered) */
static void Options_Exit(void)
{
    Dir_SetFadeOffsets(0x01000000u);
    Fade_SetupDuration(16, 1, 1);
    Fade_StartIn();
    Snd_StopAll();
    Res_CloseIfOpen();
    Dir_DispOff();
    Dir_SetStep(3);
}

/* ------------------------------------------------------------------ sound test */

/* 0x06040ED8 (out-of-line copy 0x06042308) */
static void SoundTest_Init(void)
{
    W16(SNDT_ROW, 0);
    Dir_NextStep();
    Res_CloseIfOpen();
}

/* 0x0604231C: rows SOUND FILE NAME (.SND bank 0x060DCF9C), BANK, SEQ; C start / B stop the
 * sequence, Z / Y volume, Start back to the options (step 6) */
static void SoundTest_Update(void)
{
    uint16_t r = Pad_GetDirRepeat();
    Options_DrawBg();
    if (r & 0x1000) {
        if (R16(SNDT_ROW) == 0) W16(SNDT_ROW, 2);
        else W16(SNDT_ROW, R16(SNDT_ROW) - 1u);
    }
    if (r & 0x2000) W16(SNDT_ROW, R16(SNDT_ROW) >= 2 ? 0 : R16(SNDT_ROW) + 1u);
    uint16_t loaded = R16(D_SND_BANK);
    switch (R16(SNDT_ROW)) {
    case 0:
        if (r & 0x4000) W16(D_SND_BANK, R16(D_SND_BANK) - 1u);
        if (r & 0x8000) W16(D_SND_BANK, R16(D_SND_BANK) + 1u);
        if (RS16(D_SND_BANK) >= 10) W16(D_SND_BANK, 0);
        if (RS16(D_SND_BANK) < 0) W16(D_SND_BANK, 9);
        break;
    case 1:
        if (r & 0x4000) W16(SNDT_BANK, R16(SNDT_BANK) - 1u);
        if (r & 0x8000) W16(SNDT_BANK, R16(SNDT_BANK) + 1u);
        W16(SNDT_BANK, R16(SNDT_BANK) & 63);
        break;
    case 2:
        if (r & 0x4000) W16(SNDT_SEQ, R16(SNDT_SEQ) - 1u);
        if (r & 0x8000) W16(SNDT_SEQ, R16(SNDT_SEQ) + 1u);
        W16(SNDT_SEQ, R16(SNDT_SEQ) & 63);
        break;
    default: break;
    }
    uint32_t s = SPR_WORK;
    W16(s + 0x10, 0); W32(s + 0x18, 0); W16(s + 0x0C, 0);
    Spr_SetCCalc(0);
    Font_DrawString(0x06043A9Cu, 0, 16, 8);          /* --------- SOUND TEST --------- */
    Spr_SetCCalc(2);
    if (R16(SNDT_ROW) == 0) Spr_SetCCalc(0);
    Font_DrawString(0x06043ABCu, 0, 32, 48);         /* SOUND FILE NAME: */
    Font_DrawString(R32(T_SND_FILES + 4u * (uint32_t)(int32_t)RS16(D_SND_BANK)), 0, 176, 48);
    Spr_SetCCalc(2);
    if (R16(SNDT_ROW) == 1) Spr_SetCCalc(0);
    Font_DrawString(0x06043AD0u, 0, 32, 64);         /* SOUND BANK NO. : */
    Font_DrawNumber(R16(SNDT_BANK), 0, 176, 64, 2, 0);
    Spr_SetCCalc(2);
    if (R16(SNDT_ROW) == 2) Spr_SetCCalc(0);
    Font_DrawString(0x06043AE4u, 0, 32, 80);         /* SOUND SEQ. NO. : */
    Font_DrawNumber(R16(SNDT_SEQ), 0, 176, 80, 2, 0);
    Spr_SetCCalc(0);
    Font_DrawString(0x06043AF8u, 0, 64, 128);        /* DRV-ERR : */
    Font_DrawHex(SndRam_Read(0x25A00416u) & 0xFFFF, 0, 144, 128, 4, 0);
    Font_DrawString(0x06043B04u, 0, 64, 144);        /* 68K-ERR : */
    Font_DrawHex(SndRam_Read(0x25A00420u), 0, 144, 144, 8, 0);
    if ((int32_t)RS16(D_SND_BANK) != (int32_t)loaded)
        Res_Request(1, 2, 0, 0xFFFFFFFFu, 0, R32(T_SND_FILES + 4u * (uint32_t)(int32_t)RS16(D_SND_BANK)));
    if (Dir_Pressed() & 0x0200) {                    /* C: (re)start the sequence */
        Snd_StopSeq(0);
        Snd_StartSeq(0, R16(SNDT_BANK) & 0xFF, R16(SNDT_SEQ) & 0xFF, 0);
    }
    if (Dir_Pressed() & 0x0100) Snd_StopSeq(0);      /* B */
    if (Dir_Pressed() & 0x0010) Snd_SetAllSeqVolume(127, 0x80);   /* Z */
    if (Dir_Pressed() & 0x0020) Snd_SetAllSeqVolume(0, 0x80);     /* Y */
    if (Dir_Pressed() & 0x0800) Dir_SetStep(6);      /* Start: back to the options */
}

/* ------------------------------------------------------------------ dispatch */

/* 0x06040E76 */
void Scene1_Title(void)
{
    switch (Dir_Step()) {
    case 0: Title_NewGameDefaults(); break;          /* 0x06040F3E (director.c) */
    case 1: Title_Step1_LoadPbc(); break;
    case 2: Dir_NextStep(); break;                   /* 0x06040E86: one frame */
    case 3: Title_Step3_Setup(); break;
    case 4: Title_Step4_Loop(); break;
    case 5: Title_Launch(); break;                   /* 0x060412D4 (director.c) */
    case 6: Options_Init(); break;
    case 7: Options_Update(); break;
    case 8: Options_WaitFadeOut(); break;
    case 9: Options_Exit(); break;
    case 16: SoundTest_Init(); break;
    case 17: SoundTest_Update(); break;
    default: break;                                  /* 10-15 */
    }
}
