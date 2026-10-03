#ifndef ANIM_H
#define ANIM_H
#include <stdint.h>

/* Animation state (u16 array in RAM, e.g. player +0x58), A.BIN layout:
 *  [0] flags: 0x8000 finished (FF00), 0x4000 no auto-draw, 0x2000 paused,
 *      0x80 use loop point, 0x40/0x20 set by frame bits 15/14, low 4 = events
 *  [1] script index   [2..3] script ptr   [4..5] sprite-def table
 *  [6..7] part list   [8..9] data after parts (hitboxes)   [10] part count
 *  [11] char base     [12] anim id (0xFFFF = none)   [13] frame timer
 *  [14] loop point
 * Script = 4-byte entries {u16 a, u16 b}:
 *  a < 0xF000: frame. bit15 -> flags|=0x40, bit14 -> flags|=0x20,
 *              low 12 bits = sprite def; b = duration (ticks)
 *  F900 nop, FA00 sound(b), FB00 loop point=b, FC00 flags|=b,
 *  FD00 events=b&15, FE00 goto b, FF00 end (flags|=0x8000, hold),
 *  EFFF = empty frame (no parts). */
#define ANIM_SPRITE 0x060250ACu     /* shared sprite command struct */

void Anim_Start(uint32_t st, uint32_t defTbl, uint32_t scriptTbl, int charBase, int anim);  /* 0x0600648C */
void Anim_Tick(uint32_t st);                                                               /* 0x060064C0 */
#endif
