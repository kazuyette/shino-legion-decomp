/* Stage objects 0x1007-0x1011 (stage idx 13, 2, 3) and effects 8/9, ported from
 * SHINOBI.PPB. See objhandlers_e.h for what they are. Style: objhandlers_d.c. */
#include "objhandlers_e.h"
#include "objhandlers.h"
#include "objhandlers_c.h"
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
#define VBLANKS  0x060143EAu
#define C        0x060EA618u            /* stage control block (+0xC world bottom) */
#define L1_X     0x060EA5B8u            /* layer 1 descriptor +0/+4: its scroll position */
#define L1_Y     0x060EA5BCu
#define DEFS13   0x060A985Eu            /* sprite defs / anim scripts per stage */
#define SCR13    0x060D1CA4u
#define DEFS2    0x060A9AB2u
#define SCR2     0x060D1E2Cu
#define DEFS3    0x060A9E3Au
#define SCR3     0x060D1EE0u

static uint32_t slot_addr(int s) { return OBJ_TABLE + (uint32_t)(int16_t)(s * (int)OBJ_SIZE); }
static int16_t  scr_x(uint32_t o) { return (int16_t)((uint32_t)(RS32(o + 8) - RS32(CAM_X)) >> 16); }
static int16_t  scr_y(uint32_t o) { return (int16_t)((uint32_t)(RS32(o + 0xC) - RS32(CAM_Y)) >> 16); }
/* |x - camX - 160| / |y - camY - 160| (16.16), the usual activity tests. */
static int32_t dx160(uint32_t o)
{
    int32_t d = RS32(o + 8) - RS32(CAM_X) + (int32_t)0xFF600000;
    return d < 0 ? RS32(CAM_X) - RS32(o + 8) + 0xA00000 : d;
}
static int32_t dy160(uint32_t o)
{
    int32_t d = RS32(o + 0xC) - RS32(CAM_Y) + (int32_t)0xFF600000;
    return d < 0 ? RS32(CAM_Y) - RS32(o + 0xC) + 0xA00000 : d;
}
static void spr(int16_t x, int16_t y, uint16_t ctrl, uint32_t prio, uint16_t bank, uint16_t pri, uint16_t cc)
{
    W16(SPR + 0x02, (uint16_t)x);
    W16(SPR + 0x04, (uint16_t)y);
    W16(SPR + 0x10, ctrl);
    W32(SPR + 0x18, prio);
    W16(SPR + 0x0A, bank);
    W16(SPR + 0x0C, pri);
    W16(SPR + 0x0E, cc);
}
static void start(uint32_t o, uint32_t defs, uint32_t scripts, int anim)
{
    W16(o + 6, (uint16_t)anim);
    Anim_Start(o + 0x58, defs, scripts, 0x10, RS16(o + 6));
    AND16(o + 2, 0xFFFE);
}
/* Sho's sword (not a ninjutsu/shuriken attack kind 4/5) on hitbox kind 0 of o. */
static int sword_cut(uint32_t o)
{
    if (Hitbox_Test(o, 0, PL, 2) == 0xFFFF) return 0;
    int16_t k = (int16_t)Player_AttackKind();
    return k != 4 && k != 5;
}

/* ================================================================ A.BIN 0x060067C2 */
uint16_t SprList_Step(uint32_t tbl, uint32_t idx, uint32_t timer)
{
    for (int guard = 0; guard < 64; guard++) {
        uint32_t e = tbl + (uint32_t)R16(idx) * 10u;
        uint16_t c = R16(e);
        if (c >= 0x8000) {
            if (c == 0xFFFF) { W16(SPR, 0xFFFF); return 0xFFFF; }
            if (c != 0x8000) break;                 /* PC guard: the original spins forever */
            W16(idx, R16(e + 2));                   /* goto */
            W16(timer, 0);
            continue;
        }
        W16(SPR + 0x00, c);
        W16(SPR + 0x06, R16(e + 2));
        W16(SPR + 0x08, R16(e + 4));
        W16(SPR + 0x0E, R16(e + 8));
        if (!FROZEN) {
            uint16_t t = R16(timer);
            W16(timer, (uint16_t)(t + 1));
            if (!(R16(e + 6) > t)) { W16(idx, R16(idx) + 1); W16(timer, 0); }
        }
        return R16(SPR);
    }
    return R16(SPR);
}

/* ================================================================ effects 8 / 9 */
/* 0x0604E180. Effect +2 = spawn flags | 1 (Obj_Spawn never clears bit 0 for these), so
 * only the odd trajectories 1/3/5/7 occur. The trajectories without a case (type 8: 5-7,
 * type 9: 7) use whatever r12/r11 held in the caller 0x06061FA0: 0x8000 and 64. */
void Fx_Shard(uint32_t e)
{
    SprList_Step(0x060A9E26u, e + 0x12, e + 0x2A);
    if (!FROZEN) {
        uint16_t t = R16(e + 6);
        if (R16(e) == 8) {
            int32_t vx = 0x8000, vy = 0x40;
            switch (R16(e + 2) & 7) {
            case 0: vx = 0x6000;  vy = (int32_t)(t * 0x1200u) - 0xA000;  break;
            case 1: vx = -0x6400; vy = (int32_t)(t * 0x1600u) - 0x1E400; break;
            case 2: vx = 0x3800;  vy = (int32_t)(t * 0x1200u) - 0x1C800; break;
            case 3: vx = -0xA000; vy = (int32_t)(t * 0x1400u) - 0x1A000; break;
            case 4: vx = 0x8000;  vy = (int32_t)(t * 0x1200u) - 0xC000;  break;
            }
            if (t > 0x40) W16(e, 0xFFFF);
            if (t > 0x20) {
                vx += FixMul(0x8000, Sin8(t & 0xFF));
                vy += FixMul(0x2000, Cos8(t & 0xFF));
            }
            W32(e + 8, (uint32_t)(RS32(e + 8) + vx));
            W32(e + 0xC, (uint32_t)(RS32(e + 0xC) + vy));
            W16(e + 6, (uint16_t)(t + 1));
        } else if (R16(e) == 9) {
            int32_t ax = 0x8000, ay = 0x40;
            switch (R16(e + 2) & 7) {
            case 0: ax = 0x20000; ay = 0x10000; break;
            case 1: ax = 0x1A000; ay = 0x11000; break;
            case 2: ax = 0x18000; ay = 0x13000; break;
            case 3: ax = 0x19000; ay = 0x12000; break;
            case 4: ax = 0x18800; ay = 0x10000; break;
            case 5: ax = 0x17000; ay = 0x15000; break;
            case 6: ax = 0x16000; ay = 0x11000; break;
            }
            W32(e + 8, (uint32_t)(RS32(e + 8) + ax + FixMul(0x10000, Sin8(t & 0x7F))));
            W32(e + 0xC, (uint32_t)(RS32(e + 0xC) + ay + FixMul(0x800, Cos8(t & 0x7F))));
            W16(e + 6, (uint16_t)(t + 1));
            int32_t y = RS32(e + 0xC), x = RS32(e + 8);
            if (y > RS32(CAM_Y) + 0xFA0000 || y < RS32(CAM_Y) - 0x320000) W16(e, 0xFFFF);
            if (x > RS32(CAM_X) + 0x1900000 || x < RS32(CAM_X) - 0x320000) W16(e, 0xFFFF);
        }
    }
    W16(SPR + 0x02, (uint32_t)((RS32(e + 8) - RS32(CAM_X)) >> 16));
    W16(SPR + 0x04, (uint32_t)((RS32(e + 0xC) - RS32(CAM_Y)) >> 16));
    W16(SPR + 0x00, R16(SPR) + 0x1C);
    W16(SPR + 0x0A, 0);
    W16(SPR + 0x10, 0x0600);
    W16(SPR + 0x0C, 0);
    W16(SPR + 0x0E, 0);
    W16(SPR + 0x14, 0);
    W32(SPR + 0x18, 0x81);
    uint16_t t = R16(e + 6);
    if (R16(e) == 8) {
        W16(SPR + 0x14, (R16(e + 2) & 1) ? t : (uint16_t)-t);
        W16(SPR + 0x0E, t > 0x30 ? 3 : t > 0x20 ? 2 : t > 0x10 ? 1 : 0);
    } else if (R16(e) == 9) {
        W16(SPR + 0x14, t);
    }
    if (R16(e) != 0xFFFF) Sprite_Draw(SPR);
}

/* ================================================================ stage 13 */
/* 0x0604CE18: the glow overlay (anim 6) of 0x1007, frame 0/1 every 2 ticks. */
static void Glow1007_Draw(uint32_t o)
{
    Anim_Start(SCRATCH_ANIM, DEFS13, SCR13, 0x10, 6);
    W16(SCRATCH_ANIM + 2, (R16(o + 0x14) & 2) >> 1);
    spr((int16_t)((uint32_t)(RS32(o + 8) - RS32(L1_X)) >> 16),
        (int16_t)((int16_t)((uint32_t)(RS32(o + 0xC) - RS32(L1_Y)) >> 16) - 0x12),
        0x0200, 0xFFFFFF80u, 0, 0xFFFF, 2);
    Anim_Tick(SCRATCH_ANIM);
}

/* 0x1007: lamp on the background layer 1 (anim flags & 1), glow on odd frames. */
void ObjHandler_1007(void)
{
    uint32_t o = CUR;
    if (R16(o + 2) & 1) {
        start(o, DEFS13, SCR13, R16(o + 0x12) & 1);
        W16(o + 0x48, 0);
        W16(o + 0x14, 0);
        W16(o + 0x1C, 0x8000);
        OR16(o + 0x12, 0x4000);
    }
    if (!FROZEN) {
        W16(o + 0x14, R16(o + 0x14) + 1);
        W16(o + 0x48, (R16(o + 0x14) & 4) ? 1 : 0);
    }
    if (R16(o + 0x14) & 1) Glow1007_Draw(o);
    spr((int16_t)((uint32_t)(RS32(o + 8) - RS32(L1_X)) >> 16),
        (int16_t)((uint32_t)(RS32(o + 0xC) - RS32(L1_Y)) >> 16), 0x0200, 0xFFFFFF80u, 0, 0xFFFF, 0);
    if (R16(o) == 0xFFFF) return;
    Anim_Tick(o + 0x58);
}

/* 0x0604D188: base left at the spawn point (anim 7). */
static void Lantern1008_Base(uint32_t o)
{
    Anim_Start(SCRATCH_ANIM, DEFS13, SCR13, 0x10, 7);
    spr((int16_t)((uint32_t)(RS16(o + 0x4C) * 0x10000 - RS32(CAM_X)) >> 16),
        (int16_t)((uint32_t)(RS16(o + 0x4E) * 0x10000 - RS32(CAM_Y)) >> 16), 0x0200, 0x81, 0, 0, 0);
    Anim_Tick(SCRATCH_ANIM);
}
/* 0x0604D218: the top (anim 8), spinning and blinking out once cut. */
static void Lantern1008_Top(uint32_t o)
{
    Anim_Start(SCRATCH_ANIM, DEFS13, SCR13, 0x10, 8);
    W16(SPR + 0x02, (uint16_t)scr_x(o));
    W16(SPR + 0x04, (uint16_t)(scr_y(o) - 0x20));
    W16(SPR + 0x10, 0x0200);
    if (R16(o + 0x48) != 0) {
        W16(SPR + 0x14, (uint16_t)(R16(o + 0x14) << 3));
        W16(SPR + 0x10, 0x0600);
    }
    W32(SPR + 0x18, 0x81);
    W16(SPR + 0x0A, 0); W16(SPR + 0x0C, 0); W16(SPR + 0x0E, 0);
    if (R16(o + 0x48) == 0) Anim_Tick(SCRATCH_ANIM);
    else if (R16(o + 0x14) & 1) {
        W16(SPR + 0x0E, R16(o + 0x14) >> 2);
        Anim_Tick(SCRATCH_ANIM);
    }
}
/* 0x1008: lantern (flame anim 2 + (flags & 3), drawn on odd frames). Sho's sword knocks
 * the top off towards Sho's side (+0x52 = 1 when he is on the left; 48 frames, accelerating,
 * rising and spinning, blinking out), +0x48 0 intact / 1 flying / 2 gone. */
void ObjHandler_1008(void)
{
    uint32_t o = CUR;
    if (R16(o + 2) & 1) {
        start(o, DEFS13, SCR13, (R16(o + 0x12) & 3) + 2);
        W16(o + 0x48, 0);
        W16(o + 0x4A, 0x30);
        W16(o + 0x4C, R32(o + 8) >> 16);
        W16(o + 0x4E, R32(o + 0xC) >> 16);
        W16(o + 0x50, 0);
        W16(o + 0x52, 0);
        W16(o + 0x14, 0);
        W16(o + 0x1C, 0x8000);
    }
    if (R16(o + 0x48) == 0) AND16(o + 2, 0xFFF7);
    if (!FROZEN) {
        W16(o + 0x50, R16(o + 0x50) + 1);
        if (!(R16(o + 2) & 8) && sword_cut(o)) {
            sub_06007CDE(0x122);
            Obj_Spawn(2, RS32(HIT_POINT), RS32(HIT_POINT + 4), 0, 0);
            OR16(o + 2, 8);
            if (RS32(PL + 8) - RS32(o + 8) < 0) W16(o + 0x52, 1);
            W16(o + 0x48, 1);
        }
        if (R16(o + 0x48) == 1) {
            W16(o + 0x14, R16(o + 0x14) + 1);
            uint32_t t = R16(o + 0x14);
            if (R16(o + 0x52) == 0) W32(o + 8, R32(o + 8) + t * 0x800u);
            else                    W32(o + 8, R32(o + 8) - t * 0x800u);
            W32(o + 0xC, R32(o + 0xC) - (t * 0xFFFFE000u + 0x40000u));
            W16(o + 0x4A, R16(o + 0x4A) - 1);
            W16(SPR + 0x14, (uint16_t)(t * 15));
            if (R16(o + 0x4A) == 0) W16(o + 0x48, 2);
        }
    }
    if (R16(o + 0x48) != 0) OR16(o + 2, 8);
    Lantern1008_Base(o);
    Lantern1008_Top(o);
    spr(scr_x(o), scr_y(o), 0x0200, 0x81, 0, 0, 0);
    if (R16(o) != 0xFFFF && R16(o + 0x48) == 0 && (R16(o + 0x50) & 1)) Anim_Tick(o + 0x58);
}

/* ================================================================ stage 2 */
/* 0x0604E12C: one leaf effect (8, or 9 when p & 8) with trajectory p & 7. */
static int Leaf_Spawn(int32_t x, int32_t y, uint16_t p)
{
    return Obj_Spawn((p & 8) ? 9 : 8, x, y, p & 7, 0) < 0 ? -1 : 0;
}

/* 0x1009: bush (anim flags & 7; half width 16 px for anims 0-2, 64 for 5, else 32).
 * Walking into it restarts the anim at frame 1 (rustle); the sword throws leaves
 * (effects 8) on the side opposite to the cut, then 128 frames of cooldown. */
void ObjHandler_1009(void)
{
    uint32_t o = CUR;
    int32_t w = 0x200000;
    if (R16(o + 2) & 1) {
        start(o, DEFS2, SCR2, R16(o + 0x12) & 7);
        W16(o + 0x14, 0);
        W16(o + 0x48, 1);
        W16(o + 0x4A, 0);
        W16(o + 0x4C, 0x80);
        OR16(o + 0x1C, 0x8000);
        if (R16(o + 0x12) & 8) OR16(o + 2, 0x8000);
        OR16(o + 0x12, 0x4000);
    }
    if (dx160(o) > 0x2000000 || dy160(o) > 0x2000000) return;
    if (R16(o + 0x4A) == 0) AND16(o + 2, 0xFFF7);
    if (!FROZEN) {
        int32_t x = RS32(o + 8), hx, hy;
        if (R16(o + 6) < 3) w = 0x100000;
        if (R16(o + 6) == 5) w = 0x400000;
        if (R16(o + 0x4A) == 0 && !(R16(o + 2) & 8) && sword_cut(o)) {
            sub_06007CDE(0x20A);
            hx = RS32(HIT_POINT); hy = RS32(HIT_POINT + 4);
            if (w == 0x100000) {
                Leaf_Spawn(x, hy, 0);
                Leaf_Spawn(x - 0x100000, hy, 1);
                Leaf_Spawn(x + 0x100000, hy, 2);
                Leaf_Spawn(x - 0x200000, hy, 3);
                Leaf_Spawn(x + 0x200000, hy, 4);
            } else {
                int32_t s = w == 0x200000 ? 0x100000 : 0x200000;      /* cut zone half width */
                int32_t f = w == 0x200000 ? 0x280000 : 0x300000;      /* farthest leaf */
                if (x - s < hx) {
                    Leaf_Spawn(x + 0x100000, hy, 0);
                    Leaf_Spawn(x + 0x200000, hy, 2);
                    Leaf_Spawn(x + f, hy, 4);
                }
                if (hx < x + s) {
                    Leaf_Spawn(x - 0x100000, hy, 0);
                    Leaf_Spawn(x - 0x200000, hy, 1);
                    Leaf_Spawn(x - f, hy, 3);
                }
            }
            W16(o + 0x4A, 1);
        }
        if (R16(o + 0x4A) != 0) {
            if (R16(o + 0x4C) == 0) { W16(o + 0x4A, 0); W16(o + 0x4C, 0x80); }
            else W16(o + 0x4C, R16(o + 0x4C) - 1);
        }
        int32_t px = RS32(PL + 8), py = RS32(PL + 0xC), y = RS32(o + 0xC);
        if (R16(o + 0x48) != 0 && x - w - 0x200000 < px && px < x + w + 0x200000 &&
            y - 0x100000 < py && py < y + 0x100000 && !(R16(PL + 2) & 0x4000)) {
            W16(o + 0x5A, 1);                                   /* rustle */
            W16(o + 0x48, 0);
        }
        if (R16(o + 0x48) == 0) {
            if (px < x - w - 0x200000 || x + w + 0x200000 < px) W16(o + 0x48, 1);
            if (R16(PL + 2) & 0x4000) W16(o + 0x48, 1);
        }
    }
    spr(scr_x(o), scr_y(o), (R16(o + 2) & 0x8000) | 0x0200, 0x81, 0, 1, 0);
    if (R16(o) != 0xFFFF) Anim_Tick(o + 0x58);
}

/* 0x100A / 0x100B: decor (anims 7 / 8), drawn within 256 px of the screen centre. */
static void Decor_Stage2(int anim)
{
    uint32_t o = CUR;
    if (R16(o + 2) & 1) {
        start(o, DEFS2, SCR2, anim);
        W16(o + 0x14, 0);
        OR16(o + 0x12, 0x4000);
    }
    if (dx160(o) > 0x1000000 || dy160(o) > 0x1000000) return;
    spr(scr_x(o), scr_y(o), 0x0200, 0x81, 0, 1, 0);
    if (R16(o) != 0xFFFF) Anim_Tick(o + 0x58);
}
void ObjHandler_100A(void) { Decor_Stage2(7); }
void ObjHandler_100B(void) { Decor_Stage2(8); }

/* 0x0604E5B8: enemies of type 0x23 standing on / pushed by `self` (2x8 box): crushed
 * ones (+0x10 & 0x400) die (HP 0, +2 |= 8), standing ones move by (dx, -dy). */
static void Carry_Type23(uint32_t self, int32_t dx, int32_t dy)
{
    for (int16_t s = 0; s < OBJ_SLOTS; s++) {
        uint32_t e = slot_addr(s);
        uint16_t t = R16(e);
        if (t == 0xFFFF || t >= 0x1000) continue;
        if (Enemy_FindSlot((int16_t)t) == -1) continue;
        if (t != 0x23) continue;
        if ((int16_t)Obj_CarryContact(self, e, 2, 8) == 0) continue;
        Obj_Push(self, 0, 0);
        if (R16(e + 0x10) & 0x0400) { W16(e + 0x16, 0); OR16(e + 2, 8); }
        else if (R16(e + 0x10) & 0x2000) {
            W32(e + 8, R32(e + 8) + (uint32_t)dx);
            W32(e + 0xC, R32(e + 0xC) - (uint32_t)dy);
        }
    }
}

/* 0x100C: gate (anim 9, solid). Placement flags & 3 = the checkpoint (0x060EC658) at which
 * it slides open: 8 px/frame to the right for 8 frames, moving the world bottom C+0xC up
 * from 0x0E44 (cp 1) / 0x0644 (cp 2) px, then clears its flag bit. No freeze test. */
void ObjHandler_100C(void)
{
    uint32_t o = CUR;
    if (R16(o + 2) & 1) {
        start(o, DEFS2, SCR2, 9);
        W16(o + 0x14, 0x40);
        W16(o + 0x48, 0);
        W16(o + 0x1C, 0x4000);
        OR16(o + 0x12, 0x4000);
    }
    if (dx160(o) > 0x1800000 || dy160(o) > 0x1800000) return;
    Obj_PlayerContact(0);
    for (int cp = 1; cp <= 2; cp++) {
        if ((R16(o + 0x12) & 3) != cp || RS16(G_CHECKPOINT) != cp) continue;
        if (R16(o + 0x14) == 0) AND16(o + 0x12, cp == 1 ? 0xFFFE : 0xFFFD);
        else {
            if (R16(o + 0x48) == 0) {
                sub_06007CDE(0x219);
                W32(C + 0xC, cp == 1 ? 0x0E440000u : 0x06440000u);
                W16(o + 0x48, 1);
            }
            W16(o + 0x14, R16(o + 0x14) - 8);
            W32(o + 8, R32(o + 8) + 0x80000u);
            W32(C + 0xC, R32(C + 0xC) - 0x80000u);
        }
    }
    Carry_Type23(o, 0, 0);
    spr(scr_x(o), (int16_t)(scr_y(o) + 1), 0x0200, 0x81, 0, 0, 0);
    if (R16(o) != 0xFFFF) Anim_Tick(o + 0x58);
}

/* 0x100D: crumbling block (anim 10 + (flags & 1), mirrored too). One-way top + stomp
 * contact while +0x48 != 0; standing on it (2) or stomping (3) -> shakes 64 frames
 * (+0x4C rocking), then falls (+0x14 frames, y += t/8 px) fading out, freed after 65. */
void ObjHandler_100D(void)
{
    uint32_t o = CUR;
    if (R16(o + 2) & 1) {
        start(o, DEFS2, SCR2, (R16(o + 0x12) & 1) + 10);
        W16(o + 0x14, 0);
        W16(o + 0x48, 1);
        W16(o + 0x4A, 0x40);
        W16(o + 0x4C, 0);
        OR16(o + 0x1C, 0x8000);
        if (R16(o + 0x12) & 1) OR16(o + 2, 0x8000);
        OR16(o + 0x12, 0x4000);
    }
    if ((dx160(o) > 0x2000000 || dy160(o) > 0x2000000) && R16(o + 0x48) == 1) return;
    if (!FROZEN) {
        int16_t c = (int16_t)RS32(o + 8);       /* r5 = X when no contact test is made */
        if (R16(o + 0x48) != 0) c = (int16_t)Obj_PlayerContact(5);
        if (c == 2 || c == 3) W16(o + 0x48, 2);
        if (R16(o + 0x48) == 2) {
            if (R16(o + 0x4A) == 0) {
                W16(o + 0x48, 0);
                sub_06007CDE(0x20A);
                sub_06007CDE(0x10C);
            }
            W16(o + 0x4A, R16(o + 0x4A) - 1);
            W16(o + 0x4C, (R16(o + 0x4A) & 0x10) ? R16(o + 0x4C) + 1 : R16(o + 0x4C) - 1);
        }
        if (R16(o + 0x48) == 0) {
            if (R16(o + 0x14) < 0x41) {
                W16(o + 0x14, R16(o + 0x14) + 1);
                W32(o + 0xC, R32(o + 0xC) + R16(o + 0x14) * 0x2000u);
            } else W16(o, 0xFFFF);
        }
    }
    W16(SPR + 0x02, (uint16_t)scr_x(o));
    W16(SPR + 0x04, (uint16_t)scr_y(o));
    uint16_t ctrl = (R16(o + 2) & 0x8000) | 0x0200;
    W16(SPR + 0x14, 0);
    if (R16(o + 0x48) == 2) {
        int16_t a = (int16_t)(FixMul(0x80000, Sin8(RS16(o + 0x4C))) >> 16);
        W16(SPR + 0x14, (uint16_t)((ctrl & 0x8000) ? a : -a));
    }
    W16(SPR + 0x10, ctrl | 0x0400);
    W32(SPR + 0x18, 0x81);
    W16(SPR + 0x0A, 0);
    W16(SPR + 0x0C, 0xFFFF);
    uint16_t t = R16(o + 0x14);
    W16(SPR + 0x0E, t > 0x38 ? 3 : t > 0x30 ? 2 : t > 0x20 ? 1 : 0);
    if (R16(o + 0x48) == 0) {
        int16_t a = (int16_t)(FixMul(0x400000, Sin8(RS16(o + 0x14))) >> 16);
        uint16_t v = (uint16_t)((ctrl & 0x8000) ? a : -a);
        W16(SPR + 0x14, (uint16_t)(v >> 1));
    }
    if (R16(o) == 0xFFFF) return;
    Anim_Tick(o + 0x58);
}

/* ================================================================ stage 3 (boss room) */
#define G_TIMER_L  0x060F21F6u
#define G_TIMER_R  0x060F21F8u
#define G_ALIVE    0x060F21FAu
#define G_21FC     0x060F21FCu
#define G_IDX_L    0x060F21FEu
#define G_IDX_R    0x060F2200u
#define G_MOUTH_L  0x060F2204u          /* anim states (0x20 bytes each) */
#define G_MOUTH_R  0x060F2224u
#define G_STATE_L  0x060F2244u
#define G_STATE_R  0x060F2246u
#define G_SCALE_L  0x060F2248u
#define G_SCALE_R  0x060F224Cu

/* 0x0604ED92: drop a group of debris pieces (0x100F flags 1) from above the screen:
 * side, count, offsets and phases from the cyclic tables 0x060AA0D8..0x060AA110. */
static void Debris_Drop(uint32_t o)
{
    int16_t s = RS16(o + 0x48);
    int left = RS16(0x060AA0D8u + 2u * (uint32_t)s) == 0;
    W16(o + 0x48, (uint32_t)(int32_t)s < 7 ? s + 1 : 0);
    int32_t x0 = RS32(CAM_X) + (left ? 0x640000 : 0xDC0000), y0 = RS32(CAM_Y), dx = 0, dy = 0;
    s = RS16(o + 0x50);
    int16_t n = RS16(0x060AA0E8u + 2u * (uint32_t)s);
    W16(o + 0x50, (uint32_t)(int32_t)s < 8 ? s + 1 : 0);
    for (; n != 0; n--) {
        int k = Obj_Register(0x100F, x0 + dx, y0 + dy, 1, 0xFFFF);
        s = RS16(o + 0x4A);
        dx = RS16(0x060AA0FAu + 2u * (uint32_t)s) << 16;
        W16(o + 0x4A, (uint32_t)(int32_t)s < 4 ? s + 1 : 0);
        s = RS16(o + 0x4C);
        dy = RS16(0x060AA104u + 2u * (uint32_t)s) << 16;
        W16(o + 0x4C, (uint32_t)(int32_t)s < 5 ? s + 1 : 0);
        if ((int16_t)k != -1) {
            uint32_t p = slot_addr(k);
            if (!left) OR16(p + 2, 0x8000);
            W16(p + 0x48, R16(0x060AA110u + 2u * (uint32_t)RS16(o + 0x4E)));
            W16(o + 0x4E, R16(o + 0x4E) < 4 ? R16(o + 0x4E) + 1 : 0);
        }
    }
}

/* 0x100F: flags & 0x1F = 0: spawner (first group after 60 frames, then every 200);
 * else a falling piece (anim 2): drifts at angle +0x4A (0xD0 / 0xB0 mirrored) by 1/2 px,
 * sways (sin +0x48 x 5/16 px), spins with +0x48, freed 224 px below the camera. */
void ObjHandler_100F(void)
{
    uint32_t o = CUR;
    if (R16(o + 2) & 1) {
        W16(o + 0x1C, 0x8000);
        AND16(o + 2, 0xFFFE);
        if ((R16(o + 0x12) & 0x1F) == 0) {
            W16(o + 0x14, 0x3C);
            W16(o + 0x48, 0); W16(o + 0x4A, 0); W16(o + 0x4C, 0); W16(o + 0x4E, 0); W16(o + 0x50, 0);
        } else {
            W16(o + 6, 2);
            Anim_Start(o + 0x58, DEFS3, SCR3, 0x10, RS16(o + 6));
            W16(o + 0x4A, (R16(o + 2) & 0x8000) ? 0xB0 : 0xD0);
        }
    }
    if (!FROZEN) {
        if ((R16(o + 0x12) & 0x1F) == 0) {
            W16(o + 0x14, R16(o + 0x14) - 1);
            if (R16(o + 0x14) == 0) {
                Debris_Drop(o);
                W16(o + 0x14, 200);
            }
            return;
        }
        int32_t vx = FixMul(0x8000, Cos8(RS16(o + 0x4A)));
        int32_t vy = FixMul(0x8000, Sin8(RS16(o + 0x4A)));
        int32_t sw = FixMul(0x5000, Sin8(RS16(o + 0x48)));
        W32(o + 8, (uint32_t)(RS32(o + 8) + vx));
        W32(o + 0xC, (uint32_t)(RS32(o + 0xC) - (vy + sw)));
        W16(o + 0x48, R16(o + 0x48) + 1);
        if (RS16(o + 0x48) > 0x100) W16(o + 0x48, R16(o + 0x48) - 0x100);
        if (RS32(CAM_Y) + 0xE00000 < RS32(o + 0xC)) W16(o, 0xFFFF);
    }
    spr(scr_x(o), scr_y(o), (R16(o + 2) & 0x8000) | 0x0600, 0xFFFFFF81u, 0x20, 0xFFFF, 0);
    W16(SPR + 0x14, R16(o + 0x48));
    Anim_Tick(o + 0x58);
}

/* 0x0604FAC0: both heads destroyed -> 0x100E at the camera, 0x06073308 (0x060F688C = 1,
 * 0x060F688E = 60). */
static void Heads_Defeated(void)
{
    Obj_Register(0x100E, RS32(CAM_X), RS32(CAM_Y), 0, 0xFFFF);
    W16(0x060F688Cu, 1);
    W16(0x060F688Eu, 0x3C);
}
/* 0x0604F9D0: 6 explosion parts + sound. */
static void Head_Explode(uint32_t o)
{
    int32_t x = RS32(o + 8), y = RS32(o + 0xC);
    Obj_Spawn(6, x, y, 0, 0);
    Obj_Spawn(6, x + 0x80000, y - 0x50000, 0, 6);
    Obj_Spawn(6, x - 0x80000, y - 0x30000, 0, 6);
    Obj_Spawn(6, x, y, 0, 0xC);
    Obj_Spawn(6, x + 0xC0000, y - 0x20000, 0, 0xC);
    Obj_Spawn(6, x - 0xC0000, y - 0x50000, 0, 0xC);
    sub_06007CDE(0x115);
}

/* 0x1010 (left, 0x0604EF58) / 0x1011 (right, 0x0604F450): statue head, 12 HP (sword power
 * per hit, 10 flashing frames). Every table-driven interval (0x060AA124 / 0x060AA132) the
 * mouth opens (anim 0xB / 0xC); when open, a glow (anim 0xD, two halves 10 px apart)
 * grows to scale 1.125 and spits a fireball 0x1012 (0x0604F956 / 0x0604F9A4), then shrinks. */
static void Head_Update(int right)
{
    uint32_t o = CUR;
    uint16_t aIdle = right ? 5 : 3, aFlash = right ? 6 : 4;
    uint32_t timer = right ? G_TIMER_R : G_TIMER_L, idx = right ? G_IDX_R : G_IDX_L;
    uint32_t st = right ? G_STATE_R : G_STATE_L, sc = right ? G_SCALE_R : G_SCALE_L;
    uint32_t mouth = right ? G_MOUTH_R : G_MOUTH_L, tbl = right ? 0x060AA132u : 0x060AA124u;
    uint16_t maxi = right ? 5 : 6;
    int32_t mx = right ? -0x10000 : 0x30000, my = right ? -0x1B0000 : -0x150000;
    if (R16(o + 2) & 1) {
        start(o, DEFS3, SCR3, aIdle);
        W16(o + 0x16, 0xC);
        W16(o + 0x48, 0);
        if (!right) {
            W16(G_TIMER_L, 0x78); W16(G_ALIVE, 2); W16(G_21FC, 0); W16(G_IDX_L, 0); W16(G_STATE_L, 0);
        } else {
            W16(G_IDX_R, 0); W16(G_TIMER_R, 0xA0); W16(G_STATE_R, 0);
        }
    }
    if (!FROZEN) {
        if (!(R16(o + 2) & 8) && Hitbox_Test(o, 0, PL, 2) != 0xFFFF) {
            sub_06007CDE(0x104);
            if (!right) OR16(o + 2, 8);
            W16(o + 0x16, (uint16_t)(RS16(o + 0x16) - (int16_t)Player_SwordPower()));
            if (right) OR16(o + 2, 8);
        }
        if ((R16(o + 2) & 8) && R16(o + 0x48) == 0) {
            W16(o + 0x48, 1);
            W16(o + 0x14, 10);
            W16(st, 0);
            if (RS16(o + 0x16) < 1) {
                int found = 0;
                for (int16_t s = 0; s < OBJ_SLOTS; s++)
                    if (R16(slot_addr(s)) == (right ? 0x1010 : 0x1011)) { found = 1; break; }
                if (!found) Heads_Defeated();
                Head_Explode(o);
                W16(G_ALIVE, R16(G_ALIVE) - 1);
                Score_Add(1000);
                W16(o, 0xFFFF);
                return;
            }
        }
        if (R16(o + 0x48) == 0) {
            W16(timer, R16(timer) - 1);
            if (RS16(timer) < 1) {
                W16(st, 1);
                Anim_Start(mouth, DEFS3, SCR3, 0x10, right ? 0xC : 0xB);
                W32(sc, 0x2000);
                W16(timer, R16(tbl + 2u * (uint32_t)RS16(idx)));
                W16(idx, R16(idx) < maxi ? R16(idx) + 1 : 0);
            }
        } else {
            W16(o + 0x14, R16(o + 0x14) - 1);
            if (RS16(o + 0x14) < 1) {
                W16(o + 6, aIdle);
                Anim_Start(o + 0x58, DEFS3, SCR3, 0x10, RS16(o + 6));
                W16(o + 0x48, 0);
                AND16(o + 2, 0xFFF7);
                W16(timer, R16(tbl + 2u * (uint32_t)RS16(idx)));
                W16(idx, R16(idx) < maxi ? R16(idx) + 1 : 0);
            } else {
                W16(o + 6, (R16(VBLANKS) & 1) ? aFlash : aIdle);
                Anim_Start(o + 0x58, DEFS3, SCR3, 0x10, RS16(o + 6));
            }
        }
    }
    if (R16(st) != 0) {
        spr(scr_x(o), scr_y(o), (R16(o + 2) & 0x8000) | 0x0200, 0xFFFFFF83u, 0x10, 0, 0);
        Anim_Tick(mouth);
        if (R16(mouth) & 0x8000) {                              /* mouth open: glow */
            Anim_Start(SCRATCH_ANIM, DEFS3, SCR3, 0x10, 0xD);
            if (R16(VBLANKS) & 1) OR16(SCRATCH_ANIM, 0x4000);
            int16_t gx = (int16_t)((uint32_t)(RS32(o + 8) - RS32(CAM_X) + mx) >> 16);
            spr((int16_t)(gx - 5), (int16_t)((uint32_t)(RS32(o + 0xC) - RS32(CAM_Y) + my) >> 16),
                (R16(o + 2) & 0x8000) | 0x1A00, 0xFFFFFF84u, 0x10, 0, 0);
            W32(SPR + 0x1C, R32(sc));
            W32(SPR + 0x20, R32(sc));
            Anim_Tick(SCRATCH_ANIM);
            W16(SPR + 0x02, (uint16_t)(gx + 5));
            Anim_Tick(SCRATCH_ANIM);
            if (!FROZEN) {
                if (RS16(st) == 1) {
                    W32(sc, R32(sc) + 0x600);
                    if (RS32(sc) > 0x11FFF) {
                        Obj_Register(0x1012, RS32(o + 8) + mx, RS32(o + 0xC) + my, 0, 0xFFFF);
                        sub_06007CDE(0x20D);
                        W16(st, 2);
                    }
                } else {
                    W32(sc, R32(sc) - 0x1800);
                    if (RS32(sc) < 0x2001) W16(st, 0);
                }
            }
        }
    }
    spr(scr_x(o), scr_y(o), (R16(o + 2) & 0x8000) | 0x0200, 0xFFFFFF82u, 0x10, 0, 0);
    Anim_Tick(o + 0x58);
}
void ObjHandler_1010(void) { Head_Update(0); }
void ObjHandler_1011(void) { Head_Update(1); }
