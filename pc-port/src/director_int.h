#ifndef DIRECTOR_INT_H
#define DIRECTOR_INT_H
/* Internal interface of the DIRECTOR.PPB port (docs/director.md). One file per work package:
 *   director.c          D1  entry, init, scene dispatch, logo (scene 0/3), stage start
 *                           (scene 2), story movie (scene 4), new game / launch / continue
 *                           variable setup, cheat sequence recorder
 *   director_font.c     D3  font
 *   director_title.c    D4  scene 1 (title, menu, options, sound test)        -- stub
 *   director_tally.c    D5  scene 8 (stage clear tally), Score_Add/Lives_Add  -- stub
 *   director_gameover.c D6  scenes 5/6 (continue, game over), 7 (staff roll)  -- stub
 *   movie.c / cpk.h     D7  movie wrapper (ported) / CPK player backend (movie_stub.c)
 * DIRECTOR's data (tables, strings, char lists, palettes, LZ images) is used in place in
 * the emulated RAM image of DIRECTOR.dec.bin, at the original addresses. Every scene is a
 * state machine on D_STEP; scenes change with Dir_SetScene(). Each handler must keep the
 * original address in its comment. */
#include <stdint.h>
#include "ram.h"
#include "boot.h"
#include "sprite.h"
#include "fade.h"
#include "resource.h"
#include "player_ext.h"

/* ---- shared RAM (docs/director.md §6, §4.2) ---- */
#define D_STEP        0x06087DACu   /* u32 scene step */
#define D_FONT_CHAR   0x06087DB0u   /* u16 font char base (0x200) */
#define D_FONT_BANK   0x06087DB2u   /* u16 font colour bank (64) */
#define D_TIMER       0x06087DB4u   /* s16 scene timer */
#define D_SCENE_TABLE 0x060435C0u   /* 9 x u32 scene handlers */
#define PAD1_HELD     0x0601C4A0u
#define PAD1_PRESSED  0x0601C4A4u
#define FADE_OFS      0x06025100u   /* 3 x s32 current colour offsets (16.16, 9-bit) */
#define FRAME_COUNT   0x060143EAu   /* u16 VBlank counter */

#define V_STAGE       0x0602526Au   /* s16 stage index (0 = game 1-1 ... 17 = 9-2, -1 = end) */
#define V_CHEATS      0x0602529Cu
#define V_STAGE_STATE 0x0602529Eu
#define V_HP_MAX      0x060252B0u
#define V_ONEUP_TOKENS   0x060252B2u
#define V_SHUR_INIT   0x060252B4u
#define V_SHURIKENS   0x060252B6u
#define V_LIVES       0x060252B8u
#define V_MAGIC       0x060252BAu
#define V_MAGIC_AVAIL 0x060252BEu
#define V_CONTROL     0x060252C0u
#define V_CONTINUES   0x060252C2u
#define V_DIFFICULTY  0x060252C4u
#define V_SCORE       0x060252C8u   /* u32 */
#define V_BONUS_LIFE  0x060252D0u   /* u32 */
#define V_BONUS_SHUR  0x060252D4u   /* u32 */
#define V_BONUS_DMG   0x060252D8u   /* u32 */
#define V_NOT_FIRST   0x060252DCu   /* s16: 0 = Sho drops into the stage */
#define V_RESET_ACT   0x060252E0u   /* s32 soft-reset action */
#define V_HISCORE     0x06000214u
#define V_LOGO_SEEN   0x06014404u
#define V_DEMO        0x06014408u   /* demo counter / flags */
#define V_INVINCIBLE  0x06014414u   /* 0x4465268F = cheat */

#define T_HP_MAX      0x06014424u   /* s16[3] per difficulty (A.BIN) */
#define T_LIVES       0x0601442Au
#define T_CONTINUES   0x06014430u

/* title / menu work (D4) */
#define D_MENU_CURSOR 0x060DCF8Eu   /* bit 0 GAME START/OPTIONS, 0x80 confirmed */
#define D_MENU_BOB    0x060DCF90u
#define D_TITLE_IDLE  0x060DCF92u
#define D_PRESS_START 0x060DCF94u   /* u32 */
#define D_CHEAT_COUNT 0x060DCFA4u   /* u32 */
#define D_CHEAT_BUF   0x060DCFA8u   /* u16[64] */

/* ---- small inline helpers (patterns the original inlines everywhere) ---- */
static inline uint32_t Dir_Step(void) { return R32(D_STEP); }
static inline void Dir_NextStep(void) { W32(D_STEP, R32(D_STEP) + 1); }
static inline void Dir_SetStep(uint32_t s) { W32(D_STEP, s); }
static inline void Dir_SetScene(uint32_t scene) { W32(G_DIR_SCENE, scene); W32(D_STEP, 0); }
/* DIRECTOR writes the TVMD shadow directly (no 0x0603A474 flag, unlike 0x06005BA0) */
static inline void Dir_DispOn(void)  { W16(VDP2_TVMD, R16(VDP2_TVMD) | 0x8000); }
static inline void Dir_DispOff(void) { W16(VDP2_TVMD, R16(VDP2_TVMD) & 0x7FFF); }
/* fade offsets (0 = normal, 0x01000000 = 0x100 = -256 black) + Fade_WriteOffsets 0x060078EC */
static inline void Dir_SetFadeOffsets(uint32_t v)
{
    W32(FADE_OFS, v); W32(FADE_OFS + 4, v); W32(FADE_OFS + 8, v);
    Fade_WriteOffsets();
}
/* "fill the sprite struct 0x060250AC and Sprite_Draw": offsets 0, flags 0, priority 0,
 * colour bank `bank`, priority register field 0, colour calc `ccalc` (e.g. 0x06041428). */
void Dir_DrawChar(int chr, int x, int y, int bank, int ccalc);
static inline uint16_t Dir_Pressed(void) { return R16(PAD1_PRESSED); }
static inline uint16_t Dir_Held(void) { return R16(PAD1_HELD); }

/* ---- sound calls (SCSP / CD-DA not emulated yet: logging stubs, D8/D9) ---- */
#define Snd_Play(id)       sub_06007CDE(id)       /* 0x06007CDE */
#define Snd_StopAll()      sub_06007E00()         /* 0x06007E00 */
void Snd_SetAreaMap(uint32_t map);                /* 0x06007E96 */
void Snd_SetCdDaLevel(int l, int r);              /* 0x0603125C, 0..7 */
void Snd_SetCdDaPan(int l, int r);                /* 0x060312F4 */
void Snd_SetMasterVolume(int v);                  /* 0x06030710 */

/* ---- scene handlers (scene table 0x060435C0) ---- */
void Scene0_Logo(void);               /* 0x0604059E, scenes 0 and 3 (director.c) */
void Scene1_Title(void);              /* 0x06040E76 (director_title.c, D4) */
void Scene2_StageStart(void);         /* 0x06042668 (director.c) */
void Scene4_Story(void);              /* 0x060407F2 (director.c) */
void Scene5_ContinueGameOver(void);   /* 0x06042D18, scenes 5 and 6 (director_gameover.c, D6) */
void Scene7_StaffRoll(void);          /* 0x060432C4 (director_gameover.c, D6) */
void Scene8_StageClear(void);         /* 0x06042840 (director_tally.c, D5) */

/* ---- core helpers (director.c, D1) ---- */
void Director_Init(void);             /* 0x06040094 */
void Director_RunScene(void);         /* 0x0604027A (inlined in the entry) */
void Title_NewGameDefaults(void);     /* 0x06040F3E = title step 0 (does step++) */
void Title_Launch(void);              /* 0x060412D4 = title step 5: demo / options / new game */
void Continue_Restart(void);          /* 0x06042FE4 = continue step 5 -> scene 2 */
void CheatSeq_Reset(void);            /* 0x0604223C */
void CheatSeq_Record(void);           /* 0x0604225E */
int  CheatSeq_Match(uint32_t seq);    /* 0x060422B6: seq = u16 list ending with 0xFFFF */
void Director_SetMenuSoundMap(void);  /* 0x06040596: Snd_SetAreaMap(0x060443EE) */

/* ---- font (director_font.c, D3) ---- */
void Font_Init(int charBase, int bank, uint32_t vramOff);           /* 0x06040318 */
void Font_DrawChar(int c, int bank, int x, int y);                  /* 0x06040368 */
void Font_DrawString(uint32_t str, int bank, int x, int y);         /* 0x060403FA (str in RAM) */
void Font_DrawText(const char *str, int bank, int x, int y);        /* PC: host string */
void Font_DrawNumber(int32_t v, int bank, int x, int y, int digits, int flags);   /* 0x0604042E */
void Font_DrawHex(uint32_t v, int bank, int x, int y, int digits, int flags);     /* 0x060404E8 */

#endif
