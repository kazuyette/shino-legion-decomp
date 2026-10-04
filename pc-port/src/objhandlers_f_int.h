#ifndef OBJHANDLERS_F_INT_H
#define OBJHANDLERS_F_INT_H
/* Private helpers shared by objhandlers_f*.c (stage objects 0x1020-0x1050). Everything
 * works on the current object CUR = *(0x060F21F0), like the originals. */
#include <stdint.h>
#include "objhandlers.h"
#include "objhandlers_d.h"
#include "objhandlers_f.h"
#include "objects.h"
#include "enemy.h"
#include "enemy_int.h"
#include "fx.h"
#include "anim.h"
#include "sprite.h"
#include "fixmath.h"
#include "player_ext.h"
#include "ram.h"

#undef O
#define CUR      R32(OBJ_CUR)
#define CUR_IDX  R16(OBJ_CUR_IDX)
#define CX       RS32(CAM_X)
#define CY       RS32(CAM_Y)
#define PX       RS32(0x060EA2BCu)             /* Sho's position (PL + 8 / + 0xC) */
#define PY       RS32(0x060EA2C0u)
#define VBL      R16(0x060143EAu)              /* vblank counter */
#define FRAMES   R16(0x060F4CCCu)              /* stage frame counter */
#define STAGE_C  0x060EA618u                   /* stage control block: +0 world left?, +4 top, +8 right, +0xC bottom */
#define SFX(n)   sub_06007CDE(n)

static inline uint32_t f_slot(int s) { return OBJ_TABLE + (uint32_t)(int16_t)(s * (int)OBJ_SIZE); }
static inline int32_t  f_abs(int32_t v) { return v < 0 ? -v : v; }
/* |x - camX - 160 px| (the "is it near the screen" test of every handler) */
static inline int32_t  f_scr_dx(uint32_t o)
{
    int32_t d = RS32(o + 8) - CX + (int32_t)0xFF600000;
    if (d < 0) d = CX - RS32(o + 8) + 0x00A00000;
    return d;
}
static inline int32_t  f_scr_dy(uint32_t o)
{
    int32_t d = RS32(o + 0xC) - CY + (int32_t)0xFF600000;
    if (d < 0) d = CY - RS32(o + 0xC) + 0x00A00000;
    return d;
}
/* A.BIN 0x0602DDD2 random, >> 8 (what the callers keep) */
static inline uint32_t f_rnd(void) { return (uint32_t)sub_0602DDD2() >> 8; }
/* screen position from the world position and the camera */
static inline void f_spr_cam(uint32_t o)
{
    W16(SPR + 0x02, (uint32_t)((RS32(o + 8) - CX) >> 16));
    W16(SPR + 0x04, (uint32_t)((RS32(o + 0xC) - CY) >> 16));
}
/* common sprite fields: +0x0E colour calc, +0x10 flags, +0x0A bank, +0x0C priority reg, +0x18 draw prio */
static inline void f_spr(uint16_t ctrl, uint16_t pmod, uint16_t bank, uint16_t preg, uint32_t prio)
{
    W16(SPR + 0x0E, pmod); W16(SPR + 0x10, ctrl); W16(SPR + 0x0A, bank);
    W16(SPR + 0x0C, preg); W32(SPR + 0x18, prio);
}
static inline void f_blink(uint32_t o, uint16_t mask)
{
    if (VBL & mask) OR16(o + 0x58, 0x4000); else AND16(o + 0x58, 0xBFFF);
}
/* spawn-time anim start used by every handler */
static inline void f_anim(uint32_t o, uint32_t defs, uint32_t scripts, int anim)
{
    W16(o + 6, (uint32_t)anim);
    Anim_Start(o + 0x58, defs, scripts, 0x10, RS16(o + 6));
}
static inline void f_anim_cur(uint32_t o, uint32_t defs, uint32_t scripts)
{
    Anim_Start(o + 0x58, defs, scripts, 0x10, RS16(o + 6));
}

/* A.BIN 0x0600A604: signed modulo r1 % r0 */
static inline int32_t f_smod(int32_t a, int32_t b) { return b ? a % b : 0; }

/* Shared by the f files (objhandlers_f.c) */
int  ObjF_SwordClang(uint32_t o);                   /* 0x0606078C */
int  ObjF_SwordHurtsPlayer(int16_t dmg);            /* 0x060601BA */
int  ObjF_BreakParts(uint32_t o);                   /* 0x0606081A: 6 explosion parts, returns +0x18 */
void ObjF_CarryEnemies(uint32_t self, int32_t dx, int32_t dy);  /* 0x0605817C */
void ObjF_BlockShurikens(uint32_t o);               /* 0x0604B7AC */
uint16_t ObjF_ProjHitbox(uint32_t o, int16_t ko, uint32_t p, int16_t kp);  /* 0x06060E8E */
#endif
