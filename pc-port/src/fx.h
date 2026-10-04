#ifndef FX_H
#define FX_H
#include <stdint.h>

/* Projectiles (64 x 0x40 at 0x060F23D0, current one 0x060F23CC):
 *  +0 type (0 Sho's shuriken, 1 enemy shuriken, 4 Sho's powered shuriken, 0xFFFF free;
 *     handlers SHINOBI 0x060AC994[type]), +4 flags (1 spawn, 0x2000 hostile = hits Sho,
 *  0x1000 deflected (spins), 0x4000 bouncing off / dying, 0x8000 mirrored), +6 dying timer,
 *  +8/+0xC X/Y, +0x12 hit points, +0x14 angle, +0x18/+0x1C velocity (16.16, Y up),
 *  +0x20 animation (+0x30 hitboxes).
 * Effects (64 x 0x30 at 0x060F33D4, current one 0x060F33D0): +0 type, +2 flags,
 *  +4 start delay (frames), +8/+0xC X/Y, +0x10 animation. */
#define PROJ_CUR  0x060F23CCu
#define FX_CUR    0x060F33D0u

void Common_LoadChars(void);                                            /* 0x06040A90 */
void Effects_Clear(void);                                               /* 0x0606210A */
void Projectiles_Clear(void);                                           /* 0x06060904 */
int  Obj_Spawn(int type, int32_t x, int32_t y, int flags, int param);   /* 0x060621B2 */
void Effects_Update(void);                                              /* 0x06061FA0 */
void Fx_Sparkle(uint32_t e);                                            /* 0x06062670: effects 0xA-0xD (fx_b.c) */

int  Projectile_Spawn(int16_t type, int32_t x, int32_t y, int16_t angle, int32_t speed); /* 0x0606098C */
void Projectiles_Update(void);                                          /* 0x0606088C */
int  Projectile_InBox(int32_t x0, int32_t y0, int32_t x1, int32_t y1);  /* 0x060612C2 */
int  Projectile_Collide(int16_t dmg, uint16_t mode);                    /* 0x06060A7C */
int  Atan8(int16_t x1, int16_t y1, int16_t x2, int16_t y2);             /* A.BIN 0x0600635A */

/* Items dropped by enemies / objects (64 x 0x30 at 0x060F3FDC, current one 0x060F4BDC):
 *  +0 kind (low byte: 0/1 shurikens +1/+5, 2 health, 3 full health, 4 1-up token,
 *  5 magic, 6/7 magic type, 8 other), +2 timer (0x8000 = collected), +4/+8 X/Y,
 *  +0xC animation, +0x2C spawn flag. Gone after 10 s (blinking from 7 s). */
#define ITEMS     0x060F3FDCu
#define ITEM_CUR  0x060F4BDCu
void Items_Clear(void);                                                 /* 0x0606294E */
int  Item_Drop(int16_t kind, int32_t x, int32_t y);                     /* 0x06062A14 */
void Items_Update(void);                                                /* 0x06062894 */
#endif
