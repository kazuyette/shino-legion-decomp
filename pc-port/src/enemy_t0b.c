/* Enemy type 0x000B (stage 8-1): bird (crow tengu). Loader 0x060A28AC, handler
 * 0x060A2A86, init 0x060A2918, decision 0x060A37E0, aim 0x060A3904 (Atan8 + 0x80),
 * velocity 0x060A3940 (+0x30 = cos(a) * v, +0x34 = -sin(a) * v; the handler then
 * negates +0x30), face test 0x060A399C, death overlays 0x060A39FC / 0x060A3B2C (anim 8),
 * feather spawner 0x060A3BFC (projectile 9, 0x060A3C60). 1 HP, 300 points.
 * Modes (+0x4E): 6 = flies in far away on the background layer (drawn relative to the
 * layer scroll 0x060EA5E8/EC, act 9, 3 px/frame, invulnerable) until 352 px away, then
 * re-enters at the screen edge (mode 3); 0 = decide; 1 = dive at Sho (act 4: speed up to
 * 0x500, act 5 when level with Sho and facing him: pull up, act 0 glide); 2 = swoop
 * through a random point 100-163 px past Sho (+0x4C/+0x50 target, angle +0x54 steered
 * 1/frame) until 100x80 px from it, then mode 8 = brake; 3 = turn (acts 1/3/0xC);
 * 4 = hurt (act 6, 3 feathers); 5 = falling (act 10 cut in two / 7 blown), then 7 =
 * dead: waits until its home is 512 px off screen and starts over (+4 bit 0x20). */
#include "enemy_more.h"
#include "anim.h"
#include "sprite.h"
#include "player_ext.h"
#include "fixmath.h"
#include "fx.h"

#define TB_DEFS 0x060C6F30u
#define TB_SCR  0x060E9C60u
#define BG_X    0x060EA5E8u
#define BG_Y    0x060EA5ECu

static uint32_t rnd_div(uint32_t d) { return ((uint32_t)sub_0602DDD2() >> 8) / d; }   /* A.BIN 0x0600A55C */

void EnemyB_Load(void)                                      /* 0x060A28AC */
{
    Enemy_RegisterChars(0xB, 0x060E960Cu);
    int16_t s = (int16_t)Enemy_FindSlot(0xB);
    uint16_t b = R16(0x060A9084u + 2u * (uint32_t)s);
    Pal_Copy((b & 0xFFF) << 4, 16, 0x060E9978u);
    Pal_Copy((uint16_t)(b + 1) * 16, 16, 0x060E9998u);
}

static void BirdB_Init(void)                                /* 0x060A2918 */
{
    uint32_t o = O;
    W16(o + 6, 9);
    Anim_Start(o + 0x58, TB_DEFS, TB_SCR, CHAR_BASE(SLOT), RS16(o + 6));
    AND16(o + 2, 0xFFFE);
    W16(o + 0x16, 1);
    for (uint32_t k = 0x48; k <= 0x56; k += 2) W16(o + k, 0);
    W16(o + 0x52, 0xFFFF);
    OR16(o + 2, 0x100);
    OR16(o + 4, 1);
    OR16(o + 2, 8);
    W16(o + 0x4E, 6);
    if (!(R16(o + 4) & 0x20)) {
        W16(o + 0x1E, (uint16_t)(R32(o + 8) >> 16));
        W16(o + 0x20, (uint16_t)(R32(o + 0xC) >> 16));
    } else {                                                        /* respawn at home */
        W32(o + 8, (uint32_t)((int32_t)RS16(o + 0x1E) << 16));
        W32(o + 0xC, (uint32_t)((int32_t)RS16(o + 0x20) << 16));
    }
    AND16(o + 2, 0x7FFF);
    AND16(o + 4, 0xFFEF);
    AND16(o + 4, 0xFFBF);
    if (RS32(PL + 8) - RS32(o + 8) > 0) { OR16(o + 4, 0x10); OR16(o + 2, 0x8000); }
}

static int BirdB_Face(void)                                 /* 0x060A399C */
{
    uint32_t o = O;
    uint16_t old = R16(o + 2);
    AND16(o + 2, 0x7FFF);
    if (RS32(PL + 8) < RS32(o + 8)) OR16(o + 2, 0x8000);
    return (R16(o + 2) & 0x8000) != (old & 0x8000);
}

static void BirdB_Flip(uint32_t o) { if (!(R16(o + 2) & 0x8000)) OR16(o + 2, 0x8000); else AND16(o + 2, 0x7FFF); }

static void BirdB_Decide(void)                              /* 0x060A37E0 */
{
    uint32_t o = O;
    uint16_t dx = (uint16_t)(EM_Abs(RS32(o + 8) - RS32(PL + 8)) >> 19);
    uint16_t dy = (uint16_t)(EM_Abs(RS32(o + 0xC) - RS32(PL + 0xC)) >> 19);
    if (!BirdB_Face()) {
        if (RS32(o + 0xC) - RS32(PL + 0xC) < 0 && dx > 1 && dx < 0x32 && dy > 9 && dy < 0x32) W16(o + 0x4E, 1);
        else W16(o + 0x4E, 2);
    } else {
        BirdB_Flip(o);                                              /* turn first (mode 3) */
        W16(o + 0x4E, 3);
        W16(o + 6, 1);
        OR16(o + 4, 4);
        W32(o + 0x30, 0);
        W32(o + 0x34, 0);
    }
}

static int16_t BirdB_Aim(int32_t x, int32_t y)              /* 0x060A3904 */
{
    uint32_t o = O;
    return (int16_t)(Atan8((int16_t)(R32(o + 8) >> 16), (int16_t)(R32(o + 0xC) >> 16),
                           (int16_t)((uint32_t)x >> 16), (int16_t)((uint32_t)y >> 16)) + 0x80);
}

static void BirdB_Vel(int16_t a, int32_t v)                 /* 0x060A3940 */
{
    uint32_t o = O;
    W32(o + 0x30, (uint32_t)FixMul(Cos8(a), v));
    W32(o + 0x34, (uint32_t)FixMul(Sin8(a), -v));
}

static void BirdB_Feather(int32_t x, int32_t y, uint16_t kind)   /* 0x060A3BFC */
{
    int s = Projectile_Spawn(9, x, y, 0, 0);
    if ((int16_t)s == -1) return;
    uint32_t p = PROJECTILES + (uint32_t)((int16_t)s * 0x40);
    OR16(p + 2, kind);
    W16(p + 0x14, 0x80);
}

/* steer the heading +0x54 by `step` (towards 0xB0 facing right, 0x50 facing left) */
static void BirdB_Steer(uint32_t o, int16_t step)
{
    if (!(R16(o + 2) & 0x8000)) { if (RS16(o + 0x54) < 0xB0) W16(o + 0x54, (uint16_t)(RS16(o + 0x54) + step)); }
    else if ((((uint32_t)(int32_t)RS16(o + 0x54) + 0x80u) & 0xFF) > 0x50) W16(o + 0x54, (uint16_t)(RS16(o + 0x54) - step));
}

static void BirdB_Turn(uint32_t o)                          /* -> mode 3, act 1 */
{
    W16(o + 0x4E, 3);
    W16(o + 6, 1);
    W32(o + 0x30, 0);
    W32(o + 0x34, 0);
}

static void BirdB_Dead(uint32_t o)
{
    W16(o + 0x4E, 7);
    Enemy_DeathBurst(o);
    Score_Add(300);                                                 /* 0x06062B38 */
    OR16(o + 4, 0x20);
    W16(o + 0x18, 0xFFFF);
}

/* 0x060A39FC / 0x060A3B2C: the two halves of the cut bird (anim 8, frames 0 / 1) */
static void BirdB_CutDraw(int top)
{
    uint32_t o = O;
    int16_t s = (int16_t)Enemy_FindSlot(0xB);
    Anim_Start(SCRATCH_ANIM, TB_DEFS, TB_SCR, CHAR_BASE(s), 8);
    if (top) {
        int32_t d = RS16(o + 0x48) * 0x1800;
        int32_t x = (R16(o + 2) & 0x8000) ? RS32(o + 8) + d : RS32(o + 8) - d;
        W16(SPR + 2, (uint32_t)((x - RS32(CAM_X)) >> 16));
        W16(SPR + 4, (uint32_t)((RS32(o + 0xC) + d - RS32(CAM_Y)) >> 16));
    } else {
        W16(SCRATCH_ANIM + 2, 1);
        W16(SPR + 2, (uint32_t)((RS32(o + 8) - RS32(CAM_X)) >> 16));
        W16(SPR + 4, (uint32_t)((RS32(o + 0xC) - RS32(CAM_Y)) >> 16));
    }
    W16(SPR + 0x10, (R16(o + 2) & 0x8000) | 0x200);
    W32(SPR + 0x18, 0xFFFFFFFFu);
    W16(SPR + 0x0A, (uint16_t)COLOR_BANK(s));
    W16(SPR + 0x0C, 0);
    W16(SPR + 0x0E, (uint16_t)((RS16(o + 0x48) >> 4) & 3));
    Anim_Tick(SCRATCH_ANIM);
}

void EnemyHandler_000B(void)                                /* 0x060A2A86 */
{
    uint32_t o = O;
    int16_t s = (int16_t)Enemy_FindSlot(0xB);
    if (R16(o + 2) & 1) BirdB_Init();
    if (RS16(o + 0x4E) == 7) {                                      /* dead: respawn when far */
        int32_t d = RS16(o + 0x1E) * 0x10000 - RS32(CAM_X) + (int32_t)0xFF600000;
        if (d < 0) d = RS32(CAM_X) - RS16(o + 0x1E) * 0x10000 + 0x00A00000;
        if (d > 0x02000000) OR16(o + 2, 1);
    }
    int32_t d = RS32(o + 8) - RS32(CAM_X) + (int32_t)0xFF600000;
    if (d < 0) d = RS32(CAM_X) - RS32(o + 8) + 0x00A00000;
    if (d <= 0x01800000) {
        if (RS16(o + 0x4E) == 6 && !(R16(o + 4) & 0x40)) {
            OR16(o + 4, 0x40);
            W32(o + 8, (R16(o + 4) & 0x10) ? R32(BG_X) + 0x015F0000u : R32(BG_X));
        }
    } else if (!(R16(o + 2) & 8)) {
        if (d > 0x02000000) return;
    } else if (RS16(o + 0x4E) == 6 && !(R16(o + 4) & 0x40)) return;
    if (RS16(o + 0x4E) == 7) return;
    if (FROZEN) goto draw;

    if (!(R16(PL + 2) & 8) && (int16_t)Enemy_AttackPlayer(1, 0, 0) == 3) sub_06007CDE(0x10C);
    if (!(R16(o + 2) & 8)) {
        W16(o + 0x52, 0xFFFF);
        if ((int16_t)Enemy_CheckPlayerHits(0xC000) == 2) W16(o + 0x52, (uint16_t)Player_AttackKind());
    }
    {
        int16_t m = RS16(o + 0x4E);
        if (m != 4 && m != 5 && m != 7 && m != 6 && (R16(o + 2) & 8)) {   /* hurt: feathers */
            W16(o + 6, 6);
            W16(o + 0x4E, 4);
            W32(o + 0x30, 0);
            W32(o + 0x34, 0);
            AND16(o + 4, 0xFFFD);
            AND16(o + 4, 0xFFFE);
            BirdB_Feather(RS32(o + 8) + 0x100000, RS32(o + 0xC) - 0x100000, (uint16_t)(rnd_div(7) & 7));
            BirdB_Feather(RS32(o + 8) - 0x100000, RS32(o + 0xC) + 0x100000, (uint16_t)(rnd_div(7) & 7));
            BirdB_Feather(RS32(o + 8), RS32(o + 0xC), (uint16_t)(rnd_div(7) & 7));
            if (RS16(o + 0x16) > 0) sub_06007CDE(0x207);
        }
    }
    if (RS16(o + 0x4E) == 0) {
        BirdB_Decide();
        int16_t m = RS16(o + 0x4E);
        if (m == 1) {                                               /* dive at Sho */
            W16(o + 6, 4);
            W16(o + 0x4A, 0x400);
            W16(o + 0x4C, (uint16_t)(R32(PL + 8) >> 16));
            W16(o + 0x50, (uint16_t)((uint32_t)(RS32(PL + 0xC) - 0x200000) >> 16));
            OR16(o + 4, 1);
            W16(o + 0x54, (uint16_t)(BirdB_Aim(RS16(o + 0x4C) << 16, RS16(o + 0x50) << 16) & 0xFF));
        } else if (m == 2) {                                        /* swoop past him */
            W16(o + 6, 0);
            W16(o + 0x4A, 0x380);
            AND16(o + 4, 0xFFFE);
            OR16(o + 4, 8);
            int32_t a = (int32_t)(rnd_div(7) & 0x3F) + 100;
            int32_t b = (int32_t)(rnd_div(7) & 0x3F);
            if (R16(o + 2) & 0x8000) a = -a;
            if (RS32(o + 0xC) - RS32(PL + 0xC) > 0x200000) b = -0x40 - b;
            W16(o + 0x4C, (uint16_t)((uint32_t)(a * 0x10000 + RS32(PL + 8)) >> 16));
            W16(o + 0x50, (uint16_t)((uint32_t)((b - 0x50) * 0x10000 + RS32(PL + 0xC)) >> 16));
            W16(o + 0x54, (uint16_t)(BirdB_Aim(RS16(o + 0x4C) << 16, RS16(o + 0x50) << 16) & 0xFF));
        } else if (m == 3) {
            BirdB_Turn(o);
            OR16(o + 4, 4);
        }
    }

    if (RS16(o + 0x4E) == 1) {
        if (R16(o + 6) == 4) {
            if (RS32(PL + 0xC) > RS32(o + 0xC) - 0x300000 && RS32(PL + 0xC) < RS32(o + 0xC) + 0x100000 &&
                ((!(R16(o + 2) & 0x8000) && RS32(PL + 8) < RS32(o + 8)) ||
                 ((R16(o + 2) & 0x8000) && RS32(o + 8) < RS32(PL + 8)))) {
                W16(o + 6, 5);                                      /* level with him: pull up */
                W16(o + 0x4C, (uint16_t)(R32(o + 8) >> 16));
                W16(o + 0x50, (uint16_t)(R32(o + 0xC) >> 16));
                goto integrate;
            }
            if (RS16(o + 0x4A) < 0x500) W16(o + 0x4A, (uint16_t)(RS16(o + 0x4A) + 0x80));
        }
        if (R16(o + 6) == 5) {
            BirdB_Steer(o, 4);
            W16(o + 0x54, R16(o + 0x54) & 0xFF);
            if (RS16(o + 0x4A) > 0x300) W16(o + 0x4A, (uint16_t)(RS16(o + 0x4A) - 0x40));
        }
        if (R16(o + 6) == 0) {
            BirdB_Steer(o, 4);
            if (RS16(o + 0x4A) > 0x200) W16(o + 0x4A, (uint16_t)(RS16(o + 0x4A) - 0x40));
            W16(o + 0x54, R16(o + 0x54) & 0xFF);
        }
        BirdB_Vel(RS16(o + 0x54), RS16(o + 0x4A) << 8);
        if (R16(o + 6) == 0) { int32_t v = RS32(o + 0x30); W32(o + 0x30, (uint32_t)((v + (v < 0)) >> 1)); }
        W32(o + 0x30, (uint32_t)-RS32(o + 0x30));
    }
    if (RS16(o + 0x4E) == 2) {
        if (!(R16(o + 4) & 8)) {
            BirdB_Steer(o, 1);
            W16(o + 0x54, R16(o + 0x54) & 0xFF);
            if (RS16(o + 0x4A) > 0x300) W16(o + 0x4A, (uint16_t)(RS16(o + 0x4A) - 0x20));
            if (EM_Abs(RS16(o + 0x4C) * 0x10000 - RS32(o + 8)) > 0x640000 ||
                EM_Abs(RS16(o + 0x50) * 0x10000 - RS32(o + 0xC)) > 0x500000)
                W16(o + 0x4E, 8);
            BirdB_Vel(RS16(o + 0x54), RS16(o + 0x4A) << 8);
            W16(o + 0x48, R16(o + 0x48) + 1);
        } else {
            if (RS16(o + 0x4A) < 0x600) W16(o + 0x4A, (uint16_t)(RS16(o + 0x4A) + 0x140));
            if (RS32(o + 8) - 0x400000 < RS32(PL + 8) && RS32(PL + 8) < RS32(o + 8) + 0x400000) {
                AND16(o + 4, 0xFFF7);
                W16(o + 0x4C, (uint16_t)(R32(o + 8) >> 16));
                W16(o + 0x50, (uint16_t)(R32(o + 0xC) >> 16));
            }
            BirdB_Vel(RS16(o + 0x54), RS16(o + 0x4A) << 8);
        }
        W32(o + 0x30, (uint32_t)-RS32(o + 0x30));
    }
    if (RS16(o + 0x4E) == 6) {                                      /* far away, on the background */
        if (R16(o + 6) == 9) {
            W32(o + 8, (uint32_t)(RS32(o + 8) + ((R16(o + 4) & 0x10) ? -0x30000 : 0x30000)));
            if (EM_Abs(RS32(BG_X) - RS32(o + 8)) < 0x1600000) W16(o + 0x48, R16(o + 0x48) + 2);
            else {                                                  /* comes to the front */
                W16(o + 0x4E, 3);
                W16(o + 6, 1);
                W32(o + 0x30, 0);
                W32(o + 0x34, 0);
                if (!(R16(o + 4) & 0x10)) W32(o + 8, (uint32_t)(RS32(CAM_X) + 0x1E00000));
                else { OR16(o + 2, 0x8000); W32(o + 8, (uint32_t)(RS32(CAM_X) - 0xA00000)); }
                W32(o + 0xC, (uint32_t)((int32_t)RS16(o + 0x20) << 16));
                AND16(o + 2, 0xFFF7);
                goto integrate;
            }
        }
    }
    if (RS16(o + 0x4E) == 5) {                                      /* falling */
        if (R16(o + 6) == 10) {
            if (RS16(o + 0x48) > 0x3E) { BirdB_Dead(o); return; }
            W16(o + 0x48, R16(o + 0x48) + 1);
        }
        W32(o + 0x34, (uint32_t)(RS32(o + 0x34) - 0x5000));
    }
    if (RS16(o + 0x4E) == 8) {                                      /* brake */
        W16(o + 0x4A, (uint16_t)(RS16(o + 0x4A) - 0x20));
        if (RS16(o + 0x4A) > 0) {
            BirdB_Vel(RS16(o + 0x54), RS16(o + 0x4A) << 8);
            W32(o + 0x30, (uint32_t)-RS32(o + 0x30));
        } else {
            W16(o + 0x4E, 3);
            W16(o + 6, 1);
            W16(o + 0x48, 0);
            OR16(o + 4, 4);
            W32(o + 0x30, 0);
            W32(o + 0x34, 0);
            goto integrate;
        }
    }
    if (R16(o + 0x58) & 0x8000) {                                   /* anim over */
        AND16(o + 0x58, 0x7FFF);
        W16(o + 0x14, 0);
        int16_t m = RS16(o + 0x4E), a = RS16(o + 6);
        if (m == 1 && a == 4) {
            W16(o + 6, 5);
            W16(o + 0x4C, (uint16_t)(R32(o + 8) >> 16));
            W16(o + 0x50, (uint16_t)(R32(o + 0xC) >> 16));
        } else if (m == 1 && a == 5) W16(o + 6, 0);
        else if (m == 1 && a == 0) BirdB_Turn(o);
        else if (m == 2) {
            if ((R16(o + 4) & 2) && (R16(o + 4) & 4)) { AND16(o + 4, 0xFFFD); BirdB_Turn(o); }
            else W16(o + 0x22, 0xFFFF);
        } else if (m == 5) { BirdB_Dead(o); return; }
        else if (m == 4) {
            if (RS16(o + 0x16) < 1) {
                W16(o + 0x48, 0);
                sub_06007CDE(0x207);
                W16(o + 0x4E, 5);
                int16_t k = RS16(o + 0x52);
                W16(o + 6, (k == 0 || k == 1 || k == 2 || k == 3) ? 10 : 7);
            } else {
                W16(o + 0x4E, 2);
                W16(o + 6, 0);
                AND16(o + 4, 0xFFFE);
                AND16(o + 2, 0xFFF7);
            }
        } else if (m == 3) {
            if (a == 1) {
                if (!BirdB_Face()) W16(o + 6, 0xC);
                else { BirdB_Flip(o); W16(o + 6, 3); }
            } else if (a == 3) {
                if (!BirdB_Face()) { BirdB_Flip(o); W16(o + 6, 3); W16(o + 0x22, 0); }
                else W16(o + 6, 0xC);
            } else if (a == 2) W16(o + 6, 0xC);
            else if (a == 0xC) {
                W16(o + 0x4E, 0);
                W16(o + 0x4A, 0);
                AND16(o + 4, 0xFFFB);
                OR16(o + 4, 1);
            }
        }
    }
integrate:
    Obj_Integrate(o);
    Enemy_AnimSwitch();
draw:
    if (R16(o + 6) == 10) { BirdB_CutDraw(1); BirdB_CutDraw(0); }
    W16(SPR + 0x0E, 0);
    W16(SPR + 0x0C, 1);
    W32(SPR + 0x18, 0x81);
    if (RS16(o + 0x4E) == 6) {
        W16(SPR + 0x0C, 0xFFFF);
        W16(SPR + 4, (uint32_t)((RS16(o + 0x20) * 0x10000 - RS32(BG_Y)) >> 16));
        W16(SPR + 2, (uint32_t)((RS32(o + 8) - RS32(BG_X)) >> 16));
        W32(SPR + 0x18, 0xFFFFFF7Fu);
    } else {
        W16(SPR + 2, (uint32_t)((RS32(o + 8) - RS32(CAM_X)) >> 16));
        W16(SPR + 4, (uint32_t)((RS32(o + 0xC) - RS32(CAM_Y)) >> 16));
    }
    W16(SPR + 0x10, (R16(o + 2) & 0x8000) | 0x200);
    W16(SPR + 0x0A, (uint16_t)COLOR_BANK(s));
    if (R16(o + 6) == 10) {
        W16(SPR + 0x0A, (uint16_t)(COLOR_BANK(s) + 1));
        W16(SPR + 0x0E, (uint16_t)(RS16(o + 0x48) >> 4));
    }
    if (R16(o) != 0xFFFF) Anim_Tick(o + 0x58);
}

/* Projectile 9 (0x060A3C60): feather (anim 0xD, rotated by +0x14) drifting down in a
 * swaying arc; +2 & 7 picks the sway amplitudes (vy base 1 px/frame + r13 * trig,
 * vx = r12 * trig with r12 = base + ((rand >> 8) / 7 & 0xF000)), +2 & 1 cos/sin.
 * Harmless; freed when its anim ends. */
void Proj_Feather09(uint32_t e)
{
    static const int32_t amp_y[8] = { 0x12000, 0x10000, 0x13000, 0x10000, 0x14800, 0x12800, 0x13200, 0x14300 };
    static const int32_t amp_x[8] = { 0xA000, 0x8000, 0x10000, 0xC000, 0x13000, 0x11000, 0x15000, 0x1C000 };
    int16_t s = (int16_t)Enemy_FindSlot(0xB);
    if (R16(e + 4) & 1) {
        Anim_Start(e + 0x20, TB_DEFS, TB_SCR, CHAR_BASE(s), 0xD);
        AND16(e + 4, 0xFFFE);
    }
    if (!FROZEN) {
        W32(e + 0x18, 0);
        W32(e + 0x1C, 0);
        uint16_t k = R16(e + 2) & 7;
        int32_t ry = amp_y[k];
        int32_t rx = (int32_t)(rnd_div(7) & 0xF000) + amp_x[k];
        W32(e + 0x1C, (uint32_t)(RS32(e + 0x1C) + 0x10000));
        int32_t t = (R16(e + 2) & 1) ? Sin8(R16(e + 0x14) & 0xFF) : Cos8(R16(e + 0x14) & 0xFF);
        if (!(R16(e + 0x14) & 0x80)) W32(e + 0x1C, (uint32_t)(RS32(e + 0x1C) + FixMul(ry, t)));
        else W32(e + 0x1C, (uint32_t)(RS32(e + 0x1C) - FixMul(ry, t)));
        if (!(R16(e + 0x14) & 0x40)) W32(e + 0x18, (uint32_t)FixMul(rx, t));
        else W32(e + 0x18, (uint32_t)-FixMul(rx, t));
        W32(e + 8, (uint32_t)(RS32(e + 8) + RS32(e + 0x18)));
        W32(e + 0xC, (uint32_t)(RS32(e + 0xC) + RS32(e + 0x1C)));
        W16(e + 0x14, R16(e + 0x14) + 1);
        W16(e + 6, R16(e + 6) + 1);
    }
    W16(SPR + 2, (uint32_t)((RS32(e + 8) - RS32(CAM_X)) >> 16));
    W16(SPR + 4, (uint32_t)((RS32(e + 0xC) - RS32(CAM_Y)) >> 16));
    W16(SPR + 0x0A, (uint16_t)COLOR_BANK(s));
    W16(SPR + 0x0C, 0);
    W16(SPR + 0x0E, R16(e + 0x20) & 3);
    W16(SPR + 0x14, R16(e + 0x14));
    W16(SPR + 0x10, (R16(e + 4) & 0x8000) | 0x600);
    W32(SPR + 0x18, 0x81);
    if (R16(e) != 0xFFFF) Anim_Tick(e + 0x20);
    if (R16(e + 0x20) & 0x8000) W16(e, 0xFFFF);
}
