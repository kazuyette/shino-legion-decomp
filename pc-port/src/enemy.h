#ifndef ENEMY_H
#define ENEMY_H
#include <stdint.h>

/* Enemies = stage objects with type < 0x1000 (SHINOBI handler table 0x060A9090[type]).
 * Each stage has 4 enemy "slots" (0x060A91B8[stage*5 + slot] = type) that select the
 * character bank (0x060A9064[slot]), VRAM area (0x060A9070[slot]) and colour bank
 * (0x060A9084[slot]) the type uses.
 * Enemy object fields: +2 flags2 (0x8000 facing left, 0x4000 airborne, 0x0800 crouched,
 * 0x0010 just landed, 0x0008 hit/invulnerable, 0x0001 spawn), +4 flags4 (0x8000 dodging),
 * +6 action, +0x14 think timer, +0x16 HP, +0x1A air counter, +0x22 previous action,
 * +0x4A next action after a posture change, +0x52/+0x54/+0x56 death overlay anim state,
 * +0x58 animation (+0x68 hitboxes {kind, l, t, r, b} x n, 0xFFFF-terminated;
 * kinds: 0 body, 1 guard, 2 attack, 3 weak spot).
 * Effects (sparks, explosions): 64 x 0x30 at 0x060F33D4, created by Obj_Spawn.
 * Projectiles: 64 x 0x40 at 0x060F23D0. */
#define ENEMY_CUR      0x060F205Cu   /* u32 enemy being updated */
#define ENEMY_CUR_IDX  0x060F2060u
#define ENEMY_CUR_SLOT 0x060F2062u   /* its type's slot (0..4) */
#define HIT_POINT      0x0602527Cu   /* {x, y} centre of the last hitbox overlap */
#define EFFECTS        0x060F33D4u
#define PROJECTILES    0x060F23D0u
#define SCORE          0x060252C8u
#define LIVES          0x060252B8u

int      Enemy_FindSlot(int16_t type);                                 /* 0x06048D00 */
void     Enemy_RegisterChars(int16_t type, uint32_t table);            /* 0x06049AF4 */
void     Enemy_LoadTypes(void);                                        /* 0x06048C8A */
void     Obj_UpdateEnemies(void);                                      /* 0x06048954 */
void     Enemy_ResetTables(void);                                      /* PC: effects/projectiles empty */

uint16_t Hitbox_Test(uint32_t a, int16_t ka, uint32_t b, int16_t kb);  /* 0x06048DB2 */
void     Enemy_AnimSwitch(void);                                       /* 0x06048D3C */
void     Enemy_DeathBurst(uint32_t o);                                 /* 0x06049256 */
void     Score_Add(int32_t pts);                                       /* 0x06062B38 */
int      Lives_Add(int16_t n);                                         /* 0x06062B0C */
int      Projectile_InBox(int32_t x0, int32_t y0, int32_t x1, int32_t y1); /* 0x060612C2 */
#endif
