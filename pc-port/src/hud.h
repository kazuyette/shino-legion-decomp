#ifndef HUD_H
#define HUD_H
/* SHINOBI.PPB status bar (NBG3 page 0x25E6C000, 64 x 64 cells of 8x8, 2-word pattern names, map
 * built once per stage by Hud_InitNbg3Map 0x06047244) and the HUD sprites, see docs/hud.md.
 * Hud_Update 0x06061A10 runs at the start of every stage frame (game mode 6, 0x0606B90E).
 *
 * Variables shown:
 *   0x060EA2CA s16 player HP (PLAYER + P_HP)     0x060252B0 s16 HP max (life bar scale + ticks)
 *   0x060252B6 s16 shurikens (3 digits)          0x060252B2 s16 1-up tokens 0..9 (1 digit)
 *   0x060252B8 s16 lives (2 digits)              0x060252BE s16 ninjutsu available (icon sprite)
 * Score 0x060252C8 is not on the stage HUD (only on DIRECTOR's tally screen). */
#include <stdint.h>

#define HUD_NBG3_PAGE   0x25E6C000u   /* NBG3 pattern-name page: cell (x,y) at + y*256 + x*4 */
#define HUD_DIGITS_TBL  0x060ACAF0u   /* 12 x {top, bottom} cell: digits 0-9, 10 = blank, 11 */
#define HUD_BAR_TBLS    0x060D2EF4u   /* 4 pointers (colour 0..3) to 9 x {top, bottom}: 0..8/8 full */
#define HUD_LABEL_TBL   0x060ACA48u   /* "ENEMY" label: 5 top cells then 5 bottom cells */
#define HUD_LABEL_TBL17 0x060ACA5Cu   /* stage index 17: last label column {top, bottom} */

void Hud_Update(void);                                    /* 0x06061A10 */
void Hud_DrawLifeBar(void);                               /* 0x06061A56 */
void Hud_DrawHpTicks(void);                               /* 0x06061AC0 */
void Hud_DrawBossBar(int16_t max, int16_t value);         /* 0x06061B58 (boss objects) */
void Hud_DrawBar(int16_t x, int16_t y, int16_t fill, int16_t colour);    /* 0x06061C6E */
void Hud_PutDigit(int16_t x, int16_t y, int16_t digit, uint16_t attr);   /* 0x06061D28 */
/* 0x06061D8E: flags 1 = keep leading zeros, 2 = left-align (trailing blanks) unless 1,
 * 4 = palette 8 for the digits. */
void Hud_DrawNumber(int16_t x, int16_t y, int32_t value, uint16_t digits, uint16_t flags);
void Hud_DrawMagicIcon(void);                             /* 0x060632D8 */
#endif
