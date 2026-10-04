/* Stage objects 0x1013-0x101F (stage idx 5, 0, 10) and projectiles 0x24 / 0x25, ported
 * from SHINOBI.PPB. See objhandlers_e.h for what they are. Style: objhandlers_d.c. */
#include "objhandlers_e.h"
#include "objhandlers.h"
#include "objects.h"
#include "enemy.h"
#include "enemy_int.h"
#include "fx.h"
#include "anim.h"
#include "sprite.h"
#include "collision.h"
#include "fixmath.h"
#include "player_ext.h"
#include "ram.h"

#undef O
#define CUR      R32(OBJ_CUR)
#define VBLANKS  0x060143EAu
#define DEFS5    0x060AA4C0u            /* sprite defs / anim scripts per stage */
#define SCR5     0x060D24CCu
#define DEFS0    0x060AC050u
#define SCR0     0x060D29ACu
#define DEFS10   0x060ABCF0u
#define SCR10    0x060D2878u
#define PL_GRAB  0x060EA440u            /* u32: object Sho hangs from (PL + 0x18C) */

static uint32_t slot_addr(int s) { return OBJ_TABLE + (uint32_t)(int16_t)(s * (int)OBJ_SIZE); }
static int32_t  iabs(int32_t v) { return v < 0 ? -v : v; }
static int16_t  scr_x(uint32_t o) { return (int16_t)((uint32_t)(RS32(o + 8) - RS32(CAM_X)) >> 16); }
static int16_t  scr_y(uint32_t o) { return (int16_t)((uint32_t)(RS32(o + 0xC) - RS32(CAM_Y)) >> 16); }
static int32_t dx160(uint32_t o)
{
    int32_t d = RS32(o + 8) - RS32(CAM_X) + (int32_t)0xFF600000;
    return d < 0 ? RS32(CAM_X) - RS32(o + 8) + 0xA00000 : d;
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
static int sword_cut(uint32_t o)
{
    if (Hitbox_Test(o, 0, PL, 2) == 0xFFFF) return 0;
    int16_t k = (int16_t)Player_AttackKind();
    return k != 4 && k != 5;
}
/* A.BIN 0x060092BC: signed division r1 / r0. */
static int32_t sdiv(int32_t a, int32_t b) { return b ? a / b : 0; }
/* the 0x0602DDD2 random generator through A.BIN 0x0600A55C (unsigned r1 / r0) */
static uint32_t rnd_div(uint32_t d) { return (uint32_t)sub_0602DDD2() / d; }

/* DUPLICATE of the static Obj_SwordClang in objhandlers_a.c (0x0606078C). */
static int Obj_SwordClang(uint32_t o)
{
    if (R16(PL + 4) & 0x20) return 0;
    if (Hitbox_Test(o, 0, PL, 2) == 0xFFFF) return 0;
    OR16(PL + 4, 0x20);
    Obj_Spawn(5, RS32(HIT_POINT), RS32(HIT_POINT + 4), R16(PL + 2) & 0x8000, 0);
    sub_06007CDE(0x105);
    return 1;
}
/* 0x060601BA: the current object's attack boxes (kind 2) hurt Sho (body kind 0). */
static int Obj_HurtPlayer(int16_t dmg)
{
    uint32_t o = CUR;
    if (R16(R32(0x060EA41Cu)) != 0 || (R16(PL + 2) & 8)) return 0;
    if (R16(PL + 4) & 0x0410) return 0;
    if (Hitbox_Test(o, 2, PL, 0) == 0xFFFF) return 0;
    Obj_Spawn(2, RS32(HIT_POINT), RS32(HIT_POINT + 4), 0, 0);
    Player_TakeDamage(dmg);
    return 1;
}
/* 0x0606081A: 6 explosion parts around o (offsets / delays / flags table 0x060AC964). */
static void Obj_Burst6(uint32_t o)
{
    for (uint32_t i = 0; i < 6; i++) {
        uint32_t e = 0x060AC964u + i * 8;
        Obj_Spawn(6, RS16(e) * 0x10000 + RS32(o + 8), RS16(e + 2) * 0x10000 + RS32(o + 0xC),
                  RS16(e + 6), RS16(e + 4));
    }
}

/* ================================================================ stage 5 */
/* 0x06052B98: a falling leaf object 0x104B (random +0x4C frame, +0x48 phase). */
static int Leaf104B_Spawn(int32_t x, int32_t y, int16_t flags, uint16_t mirror)
{
    int s = Obj_Register(0x104B, x, y, (uint16_t)flags, 0xFFFF);
    if ((int16_t)s < 0) return -1;
    uint32_t e = slot_addr(s);
    W16(e + 0x4C, (rnd_div(7) >> 5) & 0xF);
    OR16(e + 2, mirror);
    W16(e + 0x48, (rnd_div(7) >> 5) & 0x1F);
    return 0;
}

/* 0x1013: hanging plant (anim 7 + (flags & 1), mirrored if flags & 2), waits 128 frames.
 * Sho (and the enemy of type 0x1A found at spawn, +0x4E) brushing through restarts the
 * anim at frame 1 (Sho) / 10 (enemy); a sword cut by Sho drops 2 leaves (0x104B), a hit by
 * the enemy while it is in action 3 drops 5; 64 frames between cuts. */
void ObjHandler_1013(void)
{
    uint32_t o = CUR;
    if (R16(o + 2) & 1) {
        W16(o + 6, (R16(o + 0x12) & 1) + 7);
        if (R16(o + 0x12) & 2) OR16(o + 2, 0x8000);
        Anim_Start(o + 0x58, DEFS5, SCR5, 0x10, RS16(o + 6));
        AND16(o + 2, 0xFFFE);
        OR16(o + 0x1C, 0x8000);
        W16(o + 0x48, 0);
        W16(o + 0x4A, 0x40);
        W16(o + 0x4C, 0x80);
        W16(o + 0x4E, 0xFFFF);
        W16(o + 0x50, 0);
        for (uint16_t s = 0; s < OBJ_SLOTS; s++)
            if (R16(slot_addr(s)) == 0x1A) W16(o + 0x4E, s);
        OR16(o + 0x12, 0x4000);
    }
    if (!FROZEN) {
        if (R16(o + 0x4C) == 0) {
            uint32_t who[2] = { PL, RS16(o + 0x4E) != -1 ? slot_addr(RS16(o + 0x4E)) : 0 };
            for (uint16_t i = 0; i < 2; i++) {
                uint32_t w = who[i];
                /* PC: without a type-0x1A enemy the original reads an uninitialised stack
                 * word for the second actor; only the cooldown step is kept for it. */
                if (w && !(R16(o + 2) & 8) && Hitbox_Test(o, 0, w, 2) != 0xFFFF) {
                    int16_t k = (int16_t)Player_AttackKind();
                    if (k == 4 || k == 5) AND16(o + 2, 0xFFF7);
                    else {
                        int32_t x = RS32(o + 8), y = RS32(o + 0xC);
                        uint16_t m = R16(w + 2) & 0x8000;
                        if (i == 0) {
                            sub_06007CDE(0x20A);
                            Leaf104B_Spawn(x, y - 0x220000, 0, m);
                            Leaf104B_Spawn(x - 0x80000, y - 0x1E0000, 0, m);
                        }
                        if (i == 1 && R16(w + 6) == 3) {
                            sub_06007CDE(0x20A);
                            Leaf104B_Spawn(x, y - 0x280000, 1, m);
                            Leaf104B_Spawn(x + 0x80000, y - 0x200000, 1, m);
                            Leaf104B_Spawn(x - 0x80000, y - 0x1C0000, 1, m);
                            Leaf104B_Spawn(x + 0x100000, y - 0x1E0000, 1, m);
                            Leaf104B_Spawn(x - 0x100000, y - 0x220000, 1, m);
                        }
                        OR16(o + 2, 8);
                    }
                }
                if (R16(o + 2) & 8) {
                    if (R16(o + 0x4A) == 0) { W16(o + 0x4A, 0x40); AND16(o + 2, 0xFFF7); }
                    else W16(o + 0x4A, R16(o + 0x4A) - 1);
                }
                if (!w) continue;
                int32_t x = RS32(o + 8), y = RS32(o + 0xC), wx = RS32(w + 8), wy = RS32(w + 0xC);
                int in = x - 0x100000 < wx && wx < x + 0x100000 && y - 0x200000 < wy && wy < y + 0x100000;
                if (in) {
                    if (R16(o + 0x48) == 0 && R16(o + 0x5A) == 0) {
                        W16(o + 0x5A, i == 0 ? 1 : 10);
                        W16(o + 0x48, 1);
                        W16(o + 0x50, i + 2);
                    }
                } else if (RS16(o + 0x50) == i + 2) {
                    W16(o + 0x48, 0);
                    W16(o + 0x50, 0);
                }
            }
        } else W16(o + 0x4C, R16(o + 0x4C) - 1);
    }
    W16(o + 0x16, 0x7FFF);
    spr(scr_x(o), scr_y(o), 0x0200, 0x81, 0, 0, 0);
    if (R16(o) != 0xFFFF) Anim_Tick(o + 0x58);
}

/* 0x060523CC: the lamp's glow (anim 2); the draw priority is OR-ed into the previous one. */
static void Lamp1014_Glow(uint32_t o)
{
    Anim_Start(SCRATCH_ANIM, DEFS5, SCR5, 0x10, 2);
    W16(SPR + 0x10, 0x0200);
    W32(SPR + 0x18, R32(SPR + 0x18) | 0xFFFFFF82u);
    W16(SPR + 0x02, (uint16_t)scr_x(o));
    W16(SPR + 0x04, (uint16_t)scr_y(o));
    W16(SPR + 0x0A, 0); W16(SPR + 0x0C, 0); W16(SPR + 0x0E, 0);
    Anim_Tick(SCRATCH_ANIM);
}
/* 0x1014: lamp (anim 1 started at script position (flags & 0x1F) * 4), glow on odd frames;
 * when its anim posts event 1 it stays hidden for 16 frames. */
void ObjHandler_1014(void)
{
    uint32_t o = CUR;
    if (R16(o + 2) & 1) {
        W16(o + 6, 1);
        Anim_Start(o + 0x58, DEFS5, SCR5, 0x10, RS16(o + 6));
        W16(o + 0x5A, (R16(o + 0x12) & 0x1F) << 2);
        AND16(o + 2, 0xFFFE);
        W16(o + 0x48, 0x10);
        OR16(o + 2, 8);
        OR16(o + 0x12, 0x4000);
    }
    if (!FROZEN) {
        if (R16(o + 0x14) & 1) Lamp1014_Glow(o);
        W16(o + 0x14, R16(o + 0x14) + 1);
        if (R16(o + 0x58) & 1) {
            if (R16(o + 0x48) != 0) { W16(o + 0x48, R16(o + 0x48) - 1); return; }
            AND16(o + 0x58, 0xFFFE);
            W16(o + 0x48, 0x10);
        }
    }
    W16(o + 0x16, 0x7FFF);
    spr(scr_x(o), scr_y(o), 0x0200, 0xFFFFFF81u, 0, 0, 0);
    uint16_t f = R16(o + 0x5A);
    if (f < 3) W16(SPR + 0x0E, f & 3);
    /* original: if ((f > 9) < 3) -> always true */
    W16(SPR + 0x0E, (uint16_t)((10 - f) & 3));
    if (R16(o) != 0xFFFF) Anim_Tick(o + 0x58);
}

/* 0x1015: spark of a fountain (anim 3 + (flags & 3)). The placement Y encodes the launch
 * angle (y - 0x582 px, 8-bit) and it starts from y = 0x582 px; speed class (flags >> 2) & 3
 * sets the speeds and how far along its path it starts (8/12/16/18 steps). After 64 frames
 * it flies, slowing horizontally, falling faster, growing to 1.875x, rotating; freed at
 * the end of its anim or outside x 0x41E..0x5B1 / below y 0x5D3. */
void ObjHandler_1015(void)
{
    uint32_t o = CUR;
    if (R16(o + 2) & 1) {
        OR16(o + 0x1C, 0x8000);
        W16(o + 6, (R16(o + 0x12) & 3) + 3);
        Anim_Start(o + 0x58, DEFS5, SCR5, 0x10, RS16(o + 6));
        AND16(o + 2, 0xFFFE);
        W16(o + 0x48, 0); W16(o + 0x4A, 0); W16(o + 0x4C, 0x40);
        int16_t ang = (int16_t)((uint32_t)(RS32(o + 0xC) - 0x05820000) >> 16);
        W32(o + 0xC, 0x05820000u);
        int k = (R16(o + 0x12) & 0xC) >> 2;
        static const uint16_t sx[4] = { 0x3100, 0x3300, 0x3400, 0x3200 };
        static const uint16_t sy[4] = { 0x3100, 0x3180, 0x3200, 0x3380 };
        static const int16_t steps[4] = { 8, 12, 16, 18 };
        W16(o + 0x4E, sx[k]);
        W16(o + 0x50, sy[k]);
        W32(o + 0x30, (uint32_t)FixMul(Cos8(ang), RS16(o + 0x4E)));
        if (ang == 0x40 || ang == 0xC0) W32(o + 0x30, 0);
        W32(o + 0x34, (uint32_t)FixMul(Sin8(ang), RS16(o + 0x50)));
        int16_t i = 0;
        for (; i < steps[k]; i++) {
            W32(o + 8, (uint32_t)(RS32(o + 8) + RS32(o + 0x30) * i));
            W32(o + 0xC, (uint32_t)(RS32(o + 0xC) + RS32(o + 0x34) * i));
        }
        W16(o + 0x14, (uint16_t)i);
        if (RS32(o + 0x30) < 0) OR16(o + 2, 0x8000);
        W16(o + 0x4E, 0);
        OR16(o + 2, 8);
        OR16(o + 0x12, 0x4000);
        return;
    }
    if (!FROZEN) {
        if (RS16(o + 0x4C) > 0) { W16(o + 0x4C, R16(o + 0x4C) - 1); return; }
        if (RS16(o + 0x48) < 0x120) W16(o + 0x48, (uint16_t)(R16(o + 0x14) << 3));
        if (RS16(o + 0x4A) < 0x120) W16(o + 0x4A, (uint16_t)(R16(o + 0x14) << 3));
        int32_t n = RS16(o + 0x4E);
        if (RS32(o + 0x30) != 0 && (R16(o + 2) & 0x8000)) {
            if (n * 0x13 + RS32(o + 0x30) < 0) W32(o + 0x30, (uint32_t)(RS32(o + 0x30) + n * 7));
            else if (RS32(o + 0x30) - n * 0x13 > 0) W32(o + 0x30, (uint32_t)(RS32(o + 0x30) - n * 7));
        }
        uint32_t t = R16(o + 0x14);
        W32(o + 8, R32(o + 8) + R32(o + 0x30) * t);
        W32(o + 0xC, R32(o + 0xC) + R32(o + 0x34) * t);
        if (n > 3) W32(o + 0xC, (uint32_t)(RS32(o + 0xC) + (n - 3) * 0x5000));
        W16(o + 0x4E, R16(o + 0x4E) + 1);
        W16(o + 0x14, R16(o + 0x14) + 1);
    }
    if (RS16(o + 0x4C) > 0) return;
    if (RS32(o + 0xC) > 0x05D30000 || RS32(o + 8) > 0x05B10000 || RS32(o + 8) < 0x041E0000) {
        W16(o, 0xFFFF);
        return;
    }
    spr(scr_x(o), scr_y(o), (R16(o + 2) & 0x8000) | 0x1E00, (uint32_t)(3 - (R16(o + 0x12) & 3)), 0, 1, 2);
    W16(SPR + 0x14, (uint16_t)(R16(o + 0x14) * 3));
    W32(SPR + 0x1C, (uint32_t)(RS16(o + 0x48) * 0x10 + 0xC000));
    W32(SPR + 0x20, (uint32_t)(RS16(o + 0x4A) * 0x10 + 0xC000));
    if (R16(o) != 0xFFFF) Anim_Tick(o + 0x58);
    if (R16(o + 0x58) & 0x8000) W16(o, 0xFFFF);
}

/* ================================================================ stage 0 */
/* 0x1016: cloud (anim 0, or 0xD if flags & 1) drifting left 5/16 px per frame on a 1/4
 * parallax plane, wrapping over 256 px; Y on a 1/16 plane. */
void ObjHandler_1016(void)
{
    uint32_t o = CUR;
    int32_t p = FixMul(RS32(CAM_X) - 0x04000000, 0x4000);
    if (R16(o + 2) & 1) {
        start(o, DEFS0, SCR0, (R16(o + 0x12) & 1) ? 0xD : 0);
        W16(o + 0x14, 0);
        W32(o + 8, (uint32_t)(p + ((R16(o + 0x12) & 1) ? 0x08400000 : 0x08C00000)));
    }
    if (!FROZEN) W32(o + 8, R32(o + 8) - 0x5000);
    if ((p + 0x08000000) - RS32(o + 8) > 0x1000000) W32(o + 8, (uint32_t)(p + 0x0A800000));
    W16(SPR + 0x02, (uint32_t)((RS32(o + 8) - (p + 0x08000000)) >> 16));
    uint32_t q = (uint32_t)FixMul((int32_t)((R32(CAM_Y) & 0xFFFF0000u) + 0xFC000000u), 0x1000);
    W16(SPR + 0x04, (uint32_t)((RS32(o + 0xC) - (int32_t)(q & 0xFFFF0000u) - 0x04000000) >> 16));
    W16(SPR + 0x10, 0x0200);
    W32(SPR + 0x18, 0xFFFFFF7Fu);
    W16(SPR + 0x0A, 0x20);
    W16(SPR + 0x0C, 0xFFFF);
    W16(SPR + 0x0E, 1);
    if (R16(o) != 0xFFFF) Anim_Tick(o + 0x58);
}

/* 0x0605B664: the lid of 0x1017 (anim 2): blinks / spins with -(+0x34) while bouncing. */
static void Lid1017_Draw(uint32_t o)
{
    Anim_Start(SCRATCH_ANIM, DEFS0, SCR0, 0x10, 2);
    W16(SPR + 0x14, 0);
    int16_t s = RS16(o + 0x48);
    uint16_t t = R16(o + 0x14);
    if (s == 3) W16(SCRATCH_ANIM + 2, (t & 1) ? 0 : 1);
    else if (s >= 4 && s <= 6) {
        W16(SCRATCH_ANIM + 2, s == 4 ? (t & 4) != 0 : s == 5 ? (t & 8) != 0 : 1);
        W16(SPR + 0x14, (uint32_t)(-RS32(o + 0x34)) >> 16);
    }
    W16(SPR + 0x10, 0x0600);
    W16(SPR + 0x02, (uint16_t)scr_x(o));
    W16(SPR + 0x04, (uint16_t)scr_y(o));
    W32(SPR + 0x18, R32(SPR + 0x18) | 0xFFFFFF80u);
    W16(SPR + 0x0A, 0x20); W16(SPR + 0x0C, 0); W16(SPR + 0x0E, 0);
    Anim_Tick(SCRATCH_ANIM);
}
/* 0x1017: stone lantern (anim 1 at the spawn point +0x4A/+0x4C; the lid is the object,
 * 18 px higher). A sword cut (spark, item +0x18 with a puff) knocks the lid off:
 * 1 flies up-left 16 frames, 2 hangs 8, 3 falls until it hits the map (layer 0),
 * 4 rocks back (sound), 5 rests 73 frames, 6 done (idle off screen). */
void ObjHandler_1017(void)
{
    uint32_t o = CUR;
    if (R16(o + 2) & 1) {
        W16(o + 6, 1);
        Anim_Start(o + 0x58, DEFS0, SCR0, 0x10, RS16(o + 6));
        OR16(o + 0x1C, 0x8000);
        AND16(o + 2, 0xFFFE);
        W16(o + 0x48, 0);
        W16(o + 0x4A, R32(o + 8) >> 16);
        W16(o + 0x4C, R32(o + 0xC) >> 16);
        W32(o + 0xC, R32(o + 0xC) - 0x120000);
        W32(o + 0x30, 0);
        W16(o + 0x14, 0);
        OR16(o + 0x12, 0x4000);
    }
    if (dx160(o) > 0x1000000 && (R16(o + 0x48) == 0 || R16(o + 0x48) == 6)) return;
    if (R16(o + 0x48) == 0) AND16(o + 2, 0xFFF7);
    if (!FROZEN) {
        if (!(R16(o + 2) & 8) && sword_cut(o) && R16(o + 0x48) == 0) {
            sub_06007CDE(0x122);
            Obj_Spawn(2, RS32(HIT_POINT), RS32(HIT_POINT + 4), 0, 0);
            OR16(o + 2, 8);
            W16(o + 0x48, 1);
            W32(o + 0x34, 0);
            W16(o + 0x14, 0);
            if (RS16(o + 0x18) != -1) {
                Obj_Spawn(6, RS32(o + 8), RS32(o + 0xC) - 0x100000, 0, 0);
                Item_Drop(RS16(o + 0x18), RS32(o + 8), RS32(o + 0xC) - 0x100000);
            }
        }
        uint32_t t = R16(o + 0x14);
        switch (RS16(o + 0x48)) {
        case 1:
            if (t < 0x10) {
                W32(o + 0x30, R32(o + 0x30) - t * 0x80);
                W32(o + 0x34, R32(o + 0x34) + t * 0x80);
                W16(o + 0x14, t + 1);
                W32(o + 8, R32(o + 8) + R32(o + 0x30));
                W32(o + 0xC, R32(o + 0xC) + R32(o + 0x34));
            } else { W16(o + 0x14, 0); W16(o + 0x48, 2); }
            break;
        case 2:
            if (t > 7) { W16(o + 0x14, 0); W16(o + 0x48, 3); W32(o + 0x30, 0); W32(o + 0x34, 0); }
            else W16(o + 0x14, t + 1);
            break;
        case 3:
            W32(o + 0x30, R32(o + 0x30) - t * 0x40);
            W32(o + 0x34, R32(o + 0x34) + t * 0x40);
            if (Map_GetAttr(RS32(o + 8), RS32(o + 0xC), LAYER_DESC) == 0xFFFF) {
                W16(o + 0x14, t + 1);
                W32(o + 8, R32(o + 8) + R32(o + 0x30));
                W32(o + 0xC, R32(o + 0xC) + R32(o + 0x34));
            } else {
                W16(o + 0x14, 0); W16(o + 0x48, 4); W32(o + 0x30, 0); W32(o + 0x34, 0);
                sub_06007CDE(0x213);
            }
            break;
        case 4:
            if (RS32(o + 0x34) > 0x1FFFFF) { sub_06007CDE(0x213); W16(o + 0x14, 0); W16(o + 0x48, 5); }
            else {
                W32(o + 0x30, R32(o + 0x30) + t * 0x80);
                W32(o + 0x34, R32(o + 0x34) + R32(o + 0x30));
                W16(o + 0x14, t + 1);
            }
            break;
        case 5:
            if (t < 0x49) W16(o + 0x14, t + 1);
            else { W16(o + 0x14, 0); W16(o + 0x48, 6); }
            break;
        }
    }
    if (R16(o + 0x48) == 1) OR16(o + 2, 8);
    Lid1017_Draw(o);
    spr((int16_t)((uint32_t)(RS16(o + 0x4A) * 0x10000 - RS32(CAM_X)) >> 16),
        (int16_t)((uint32_t)(RS16(o + 0x4C) * 0x10000 - RS32(CAM_Y)) >> 16), 0x0200, 0xFFFFFF7Fu, 0x20, 0, 0);
    if (R16(o) != 0xFFFF) Anim_Tick(o + 0x58);
}

/* 0x1018: background decor (anim 3) on the 1/4 x 1/16 parallax planes. */
void ObjHandler_1018(void)
{
    uint32_t o = CUR;
    if (R16(o + 2) & 1) {
        start(o, DEFS0, SCR0, 3);
        W16(o + 0x14, 0);
    }
    int32_t px = FixMul((int32_t)((R32(CAM_X) + 0xFC000000u) & 0xFFFF0000u), 0x4000);
    int32_t py = FixMul((int32_t)((R32(CAM_Y) + 0xFC000000u) & 0xFFFF0000u), 0x1000);
    spr((int16_t)((uint32_t)(RS32(o + 8) - px - 0x04000000) >> 16),
        (int16_t)((uint32_t)(RS32(o + 0xC) - py - 0x04000000) >> 16), 0x0200, 0xFFFFFF76u, 0x20, 0xFFFF, 0);
    if (R16(o) != 0xFFFF) Anim_Tick(o + 0x58);
}

/* 0x1019: solid block (anim 4): Obj_PlayerContact(0), the sword clangs on it. */
void ObjHandler_1019(void)
{
    uint32_t o = CUR;
    if (R16(o + 2) & 1) {
        start(o, DEFS0, SCR0, 4);
        W16(o + 0x14, 0);
        W16(o + 0x16, 0x7FFF);
        W16(o + 0x1C, 0x4000);
        OR16(o + 0x12, 0x4000);
    }
    if (dx160(o) >= 0x1000000) return;
    if (!FROZEN) {
        Obj_PlayerContact(0);
        Obj_SwordClang(o);
    }
    W16(o + 0x16, 0x7FFF);
    AND16(o + 2, 0xFFF7);
    spr(scr_x(o), scr_y(o), 0x0200, 0x8A, 0, 1, 0);
    if (R16(o) != 0xFFFF) Anim_Tick(o + 0x58);
}

/* 0x0605B1AE: a rock (projectile 0x25) with trajectory v. */
static int Rock_Spawn(int32_t x, int32_t y, uint16_t v)
{
    int s = Projectile_Spawn(0x25, x, y, 0, 0);
    if ((int16_t)s == -1) return -1;
    OR16(PROJECTILES + (uint32_t)(int16_t)s * 0x40 + 2, v);
    return 0;
}
/* 0x0605C7D0: the pillar's base (anim 5) at +0x54 / +0x56 px. */
static void Pillar_Base(uint32_t o)
{
    Anim_Start(SCRATCH_ANIM, DEFS0, SCR0, 0x10, 5);
    spr((int16_t)((uint32_t)(RS16(o + 0x54) * 0x10000 - RS32(CAM_X)) >> 16),
        (int16_t)((uint32_t)(RS16(o + 0x56) * 0x10000 - RS32(CAM_Y)) >> 16), 0x0200, 0xFFFFFF80u, 0, 0, 0);
    Anim_Tick(SCRATCH_ANIM);
}
/* 0x101A: rock pillar in 3 parts, flags & 3: 0 top (anim 5), 1 middle (anim 9, 53 px up,
 * draws the base, +0x4A = slot of the top above it), 2 upper (anim 8, 174 px up).
 * +0x48: 1 standing, 0 thrown (two hops left then rocks 64 deg, +0x4E 0..4), 4 tilted,
 * 5 -> becomes the falling top (anim 10), 6 falls spinning, 7 bounces, 8 landed.
 * A sword cut on parts 1/2: sound, item +0x18, 4 rocks (projectile 0x25) at the hit point;
 * part 1 sends the top (5 = fall from its own height), part 2 falls (6). */
void ObjHandler_101A(void)
{
    uint32_t o = CUR;
    uint16_t f = R16(o + 0x12) & 3;
    if (R16(o + 2) & 1) {
        uint16_t a = f + 5;
        if (f == 1) a = 9;
        if (f == 2) a = 8;
        start(o, DEFS0, SCR0, a);
        W16(o + 0x14, 0);
        W16(o + 0x1C, 0x8000);
        W16(o + 0x48, 1);
        W16(o + 0x4A, 0);
        W16(o + 0x4C, 0x48);
        W16(o + 0x4E, 0);
        W16(o + 0x50, 0);
        W16(o + 0x52, 0);
        W16(o + 0x54, R32(o + 8) >> 16);
        W16(o + 0x56, 0x5BF);
        W16(o + 0x16, 0x7FFF);
        W16(o + 0x14, 0);
        if (f == 1) {
            W32(o + 0xC, R32(o + 0xC) - 0x350000);
            for (uint16_t s = 0; s < OBJ_SLOTS; s++) {
                uint32_t p = slot_addr(s);
                if (R16(p) == 0x101A && R32(p + 8) == R32(o + 8) && (R16(p + 0x12) & 3) == 2) W16(o + 0x4A, s);
            }
        }
        if (f == 2) W32(o + 0xC, R32(o + 0xC) - 0xAE0000);
        OR16(o + 0x12, 0x4000);
    }
    if (dx160(o) > 0x1000000 && (R16(o + 0x48) == 1 || R16(o + 0x48) == 8)) return;
    if (R16(o + 0x48) == 1) AND16(o + 2, 0xFFF7);
    if (!FROZEN) {
        if (R16(o + 0x48) == 5) {
            W16(o + 6, 10);
            Anim_Start(o + 0x58, DEFS0, SCR0, 0x10, 10);
            W16(o + 0x48, 0);
        }
        if ((f == 1 || f == 2) && !(R16(o + 2) & 8) && sword_cut(o) && R16(o + 0x48) == 1) {
            sub_06007CDE(0x21C);
            W16(o + 0x48, 0);
            OR16(o + 2, 8);
            if (RS16(o + 0x18) != -1) {
                Obj_Spawn(6, RS32(o + 8), RS32(o + 0xC) - 0x200000, 0, 0);
                Item_Drop(RS16(o + 0x18), RS32(o + 8), RS32(o + 0xC) - 0x200000);
            }
            int32_t hx = RS32(HIT_POINT), hy = RS32(HIT_POINT + 4);
            Rock_Spawn(hx, hy, (uint16_t)(sub_0602DDD2() & 7));
            Rock_Spawn(hx + 0x140000, hy - 0x90000, (uint16_t)(sub_0602DDD2() & 7));
            Rock_Spawn(hx - 0x100000, hy + 0x50000, (uint16_t)(sub_0602DDD2() & 7));
            Rock_Spawn(hx - 0x120000, hy - 0xC0000, (uint16_t)(sub_0602DDD2() & 7));
            if (f == 1) {
                uint32_t p = slot_addr(RS16(o + 0x4A));
                if (R16(p + 0x50) == 0) {
                    W16(p + 0x48, 5);
                    W32(p + 0xC, R32(o + 0xC));
                    OR16(p + 2, 8);
                }
            }
            if (f == 2) { W16(o + 0x50, 1); W16(o + 0x48, 6); W16(o + 0x14, 0); }
        }
        uint32_t t = R16(o + 0x14);
        if (R16(o + 0x48) == 7) {
            W32(o + 0xC, R32(o + 0xC) - (t * 0xFFFFE000u + 0x20000u));
            if (t > 0x10 && Map_GetAttr(RS32(o + 8), RS32(o + 0xC), LAYER_DESC) != 0xFFFF) W16(o + 0x48, 8);
            W16(o + 0x14, R16(o + 0x14) + 1);
        }
        t = R16(o + 0x14);
        if (R16(o + 0x48) == 6) {
            W32(o + 0xC, R32(o + 0xC) - (t * 0xFFFFE000u + 0x20000u));
            if (RS32(o + 0x30) < 0x400000) W32(o + 0x30, R32(o + 0x30) + t * 0x800);
            if (t > 0x10 && Map_GetAttr(RS32(o + 8), RS32(o + 0xC), LAYER_DESC) != 0xFFFF) {
                sub_06007CDE(0x20A);
                W16(o + 0x48, 7);
                W16(o + 0x14, 0);
            }
            W16(o + 0x14, R16(o + 0x14) + 1);
        }
        if (R16(o + 0x48) == 0) {
            t = R16(o + 0x14);
            if (R16(o + 0x4E) == 0) {
                W32(o + 8, R32(o + 8) - t * 0x380);
                W32(o + 0xC, R32(o + 0xC) - (t * 0xFFFFE000u + 0x18000u));
            }
            if (R16(o + 0x4E) == 1) {
                W32(o + 8, R32(o + 8) - t * 0x300);
                W32(o + 0xC, R32(o + 0xC) - (t * 0xFFFFE000u + 0x20000u));
            }
            W16(o + 0x4C, R16(o + 0x4C) - 1);
            W16(o + 0x14, R16(o + 0x14) + 1);
            if (R16(o + 0x14) > 0x10 && Map_GetAttr(RS32(o + 8), RS32(o + 0xC), LAYER_DESC) != 0xFFFF) {
                sub_06007CDE(0x21C);
                if (R16(o + 0x4E) == 0) { W16(o + 0x4C, 0x50); W16(o + 0x14, 1); }
                if (R16(o + 0x4E) == 1) { W16(o + 0x48, 2); W16(o + 0x14, 0); }
                W16(o + 0x4E, R16(o + 0x4E) + 1);
            }
        }
        if (R16(o + 0x4E) == 2) {
            t = R16(o + 0x14);
            W16(o + 0x52, (uint16_t)(int16_t)sdiv((int32_t)(t * t), 0x200));
            W16(o + 0x14, t + 2);
            if (RS16(o + 0x52) > 0x40) {
                sub_06007CDE(0x20A);
                W16(o + 0x4E, 3);
                W16(o + 0x48, 4);
                W16(o + 0x14, 0);
            }
        }
        if (R16(o + 0x4E) == 3) {
            t = R16(o + 0x14);
            W32(o + 0xC, R32(o + 0xC) - (t * 0xFFFFF000u + 0x8000u));
            if ((t << 12) < 0x8001) W16(o + 0x14, t + 1);
            else if (Map_GetAttr(RS32(o + 8), RS32(o + 0xC), LAYER_DESC) != 0xFFFF) W16(o + 0x4E, 4);
        }
    }
    if (R16(o + 0x48) == 1) AND16(o + 2, 0xFFF7);
    if (R16(o + 6) == 9) Pillar_Base(o);
    spr(scr_x(o), scr_y(o), 0x0200, R16(o + 0x48) != 1 ? 0xFFFFFF8Au : 0xFFFFFF80u, 0, 0, 0);
    if (R16(o + 0x4E) == 2) {
        W16(SPR + 0x10, 0x0600);
        W16(SPR + 0x02, R16(SPR + 0x02) + ((R16(o + 0x14) & 0x10) ? 1 : -1));
        W16(SPR + 0x14, (uint16_t)-RS16(o + 0x52));
    }
    if (R16(o + 0x48) == 4) {
        OR16(SPR + 0x10, 0x0400);
        W16(SPR + 0x14, (uint16_t)-RS16(o + 0x52));
    }
    if (R16(o + 0x48) == 6 || R16(o + 0x48) == 7 || R16(o + 0x48) == 8) {
        OR16(SPR + 0x10, 0x0400);
        W16(SPR + 0x14, (uint16_t)-(int16_t)(R32(o + 0x30) >> 16));
    }
    if (R16(o) != 0xFFFF) Anim_Tick(o + 0x58);
}

/* Projectile 0x25 (0x0605B226): rock from 0x101A (anim 0xB + (+2 & 1)): falls 1 px/frame
 * plus a swing by trajectory +2 & 7 (amplitudes per case), spins with +0x14; damage 1 to
 * Sho (Projectile_Collide(1, 0x8000)), freed on a hit or at the end of its anim. */
void Proj_Rock(uint32_t e)
{
    int32_t ay = 0x18000, ax = 0x8000;
    if (R16(e + 4) & 1) {
        Anim_Start(e + 0x20, DEFS0, SCR0, 0x10, (R16(e + 2) & 1) + 0xB);
        AND16(e + 4, 0xFFFE);
        sub_06007CDE(0x20A);
    }
    if (!FROZEN) {
        W32(e + 0x18, 0);
        W32(e + 0x1C, 0);
        switch (R16(e + 2) & 7) {
        case 0: ay = 0x12000; ax = 0xA000; break;
        case 1: ay = 0x10000; break;
        case 2: ay = 0x14000; ax = 0x10000; break;
        case 3: ax = 0xC000; break;
        case 4: ax = 0x13000; ay = 0x20000; break;
        case 5: ax = 0x11000; ay = 0x16000; break;
        case 6: ax = 0x15000; break;
        case 7: ax = 0x1C000; ay = 0x20000; break;
        }
        if ((int16_t)Projectile_Collide(1, 0x8000) == 1) W16(e, 0xFFFF);
        W32(e + 0x1C, R32(e + 0x1C) + 0x10000);
        int32_t tr = (R16(e + 2) & 1) ? Sin8(R16(e + 0x14) & 0xFF) : Cos8(R16(e + 0x14) & 0xFF);
        if (!(R16(e + 0x14) & 0x80)) W32(e + 0x1C, (uint32_t)(RS32(e + 0x1C) + FixMul(ay, tr)));
        else                         W32(e + 0x1C, (uint32_t)(RS32(e + 0x1C) - FixMul(ay, tr)));
        if (!(R16(e + 0x14) & 0x40)) W32(e + 0x18, (uint32_t)FixMul(ax, tr));
        else                         W32(e + 0x18, (uint32_t)-FixMul(ax, tr));
        W32(e + 8, R32(e + 8) + R32(e + 0x18));
        W32(e + 0xC, R32(e + 0xC) + R32(e + 0x1C));
        W16(e + 0x14, R16(e + 0x14) + 1);
        W16(e + 6, R16(e + 6) + 1);
    }
    spr((int16_t)((uint32_t)(RS32(e + 8) - RS32(CAM_X)) >> 16), (int16_t)((uint32_t)(RS32(e + 0xC) - RS32(CAM_Y)) >> 16),
        (R16(e + 4) & 0x8000) | 0x0600, 0xFFFFFF81u, 0, 0, 0);
    W16(SPR + 0x14, R16(e + 0x14));
    if (R16(e) != 0xFFFF) Anim_Tick(e + 0x20);
    if (R16(e + 0x20) & 0x8000) W16(e, 0xFFFF);
}

/* ================================================================ stage 10 */
/* 0x060498BE: 8-bit angle from o (+ (dx, dy), dx mirrored) to Sho's chest (32 px up). */
static int Obj_AimAtPlayer(uint32_t o, int32_t dx, int32_t dy)
{
    if (R16(o + 2) & 0x8000) dx = -dx;
    return Atan8((int16_t)((uint32_t)(RS32(o + 8) + dx) >> 16), (int16_t)((uint32_t)(RS32(o + 0xC) + dy) >> 16),
                 (int16_t)(R32(PL + 8) >> 16), (int16_t)((uint32_t)(RS32(PL + 0xC) - 0x200000) >> 16));
}

/* 0x101B: cannon turret (mirrored if flags & 1; body anim 0, barrel frame 1 rotated by
 * +0x48 >> 6, mount frame 2/3). Solid (Obj_PlayerContact), sword clangs, the barrel hurts
 * Sho while firing (+0x4C != 0). +0x50: 0 wait until Sho is within 240 px, 1 aim for a
 * random 60..187 frames (angle clamped to -20..0x88, +0x54 rate), 2 fire: at +0x4C == 8 a
 * ball (projectile 0x24, 4 px/frame) and a flash (effect 0xF); up to 3 shots (40 frames).
 * The original clears +2 & 8 just before testing it, so its "destroyed" branch (HP +0x16,
 * explosion, item) never runs; kept for reference. */
void ObjHandler_101B(void)
{
    uint32_t o = CUR;
    if (R16(o + 2) & 1) {
        if (R16(o + 0x12) & 1) OR16(o + 2, 0x8000);
        start(o, DEFS10, SCR10, 0);
        W16(o + 0x14, 0);
        W16(o + 0x16, 8);
        W16(o + 0x1C, 0x4000);
        W16(o + 0x48, (R16(o + 2) & 0x8000) ? 0x2000 : 0);
        for (uint32_t k = 0x4A; k <= 0x54; k += 2) W16(o + k, 0);
    }
    if (dx160(o) > 0x1800000) return;
    if (!FROZEN) {
        Anim_Start(o + 0x58, DEFS10, SCR10, 0x10, 0);          /* hitboxes of frame 0 */
        OR16(o + 0x58, 0x4000);
        W16(o + 0x5A, 0);
        Anim_Tick(o + 0x58);
        AND16(o + 0x58, 0xBFFF);
        Obj_PlayerContact(R16(o + 2) & 0x8000);
        Obj_SwordClang(o);
        if (R16(o + 0x4C) != 0) Obj_HurtPlayer(1);
        AND16(o + 2, 0xFFF7);
        if (!(R16(o + 2) & 8)) {
            int16_t st = RS16(o + 0x50);
            if (st == 0) {
                W16(o + 0x4C, 0);
                W16(o + 0x4E, 0);
                if (iabs(RS32(PL + 8) - RS32(o + 8)) < 0xF00000) {
                    W16(o + 0x4E, (uint16_t)((((uint32_t)sub_0602DDD2() >> 8) & 0x7F) + 0x3C));
                    W16(o + 0x50, R16(o + 0x50) + 1);
                    W16(o + 0x54, 0);
                }
            } else if (st == 1) {
                W16(o + 0x4E, R16(o + 0x4E) - 1);
                if (RS16(o + 0x4E) < 1) {
                    W16(o + 0x4C, 0x20);
                    W16(o + 0x52, 0);
                    W16(o + 0x50, R16(o + 0x50) + 1);
                } else {
                    int16_t a = (int16_t)(Obj_AimAtPlayer(o, (int32_t)0xFFEC0000, (int32_t)0xFFD50000) & 0xFF);
                    if (a >= 0xC0) a = (int16_t)(a - 0x100);
                    if (a > 0x88) a = 0x88;
                    if (a < -0x14) a = -0x14;
                    int16_t cur = (int16_t)(RS16(o + 0x48) >> 6);
                    if (cur == a) W16(o + 0x54, 0);
                    if (cur < a) { W16(o + 0x54, R16(o + 0x54) + 2); if (RS16(o + 0x54) > 0x40) W16(o + 0x54, 0x40); }
                    cur = (int16_t)(RS16(o + 0x48) >> 6);
                    if (a < cur) { W16(o + 0x54, R16(o + 0x54) - 2); if (RS16(o + 0x54) < -0x40) W16(o + 0x54, 0xFFC0); }
                    W16(o + 0x48, R16(o + 0x48) + R16(o + 0x54));
                }
            } else if (st == 2) {
                W16(o + 0x4C, R16(o + 0x4C) - 1);
                if (R16(o + 0x4C) == 8) {                       /* fire */
                    int16_t a = (int16_t)(RS16(o + 0x48) >> 6);
                    int16_t sx = (int16_t)((int16_t)(R32(o + 8) >> 16) + ((R16(o + 2) & 0x8000) ? 0x14 : -0x14) +
                                           (int16_t)((uint32_t)(Cos8(a) * 0x2C) >> 16));
                    int16_t sy = (int16_t)((int16_t)(R32(o + 0xC) >> 16) - 0x2B - (int16_t)((uint32_t)(Sin8(a) * 0x2C) >> 16));
                    Projectile_Spawn(0x24, sx << 16, sy << 16, a, 0x40000);
                    Obj_Spawn(0xF, (int16_t)(sx + (int16_t)((uint32_t)Cos8(a) >> 12)) << 16,
                              (int16_t)(sy - (int16_t)((uint32_t)Sin8(a) >> 12)) << 16, 0, 0);
                    sub_06007CDE(0x124);
                }
                if (RS16(o + 0x4C) < 1) {
                    if (RS16(o + 0x52) < 2 && ((((uint32_t)sub_0602DDD2() >> 8) & 1) == 0)) {
                        W16(o + 0x4C, 0x28);
                        W16(o + 0x52, R16(o + 0x52) + 1);
                    } else { W16(o + 0x4C, 0); W16(o + 0x50, 0); }
                }
            }
        } else {                                                /* unreachable in the original */
            W16(o + 0x50, 0);
            if (RS16(o + 0x4A) < 0x10) W16(o + 0x4A, R16(o + 0x4A) + 1);
            else if (RS16(o + 0x16) < 1) {
                Obj_Burst6(o);
                Item_Drop(RS16(o + 0x18), RS32(o + 8), RS32(o + 0xC));
                W16(o, 0xFFFF);
                return;
            } else { AND16(o + 2, 0xFFF7); W16(o + 0x4A, 0); }
        }
    }
    uint16_t bank = 0x20;
    if (R16(o + 0x4C) == 0 || !(R16(VBLANKS) & 1)) Anim_Start(o + 0x58, DEFS10, SCR10, 0x10, 0);
    else { Anim_Start(o + 0x58, DEFS10, SCR10, 0x10, 1); bank = 0x2B; }
    spr(scr_x(o), scr_y(o), (R16(o + 2) & 0x8000) | 0x0200, 0x80, bank, 0, 0);
    W16(o + 0x5A, 0);
    Anim_Tick(o + 0x58);                                        /* body */
    int16_t bx = (int16_t)(RS16(SPR + 0x02) + ((R16(o + 2) & 0x8000) ? 0x14 : -0x14));
    int16_t by = (int16_t)(RS16(SPR + 0x04) - 0x2B);
    W16(SPR + 0x02, (uint16_t)bx);
    W16(SPR + 0x04, (uint16_t)by);
    if (RS16(o + 0x4C) < 8 && R16(o + 0x4C) != 0) {             /* recoil */
        int a = ((RS16(o + 0x48) >> 6) + 0x80) & 0xFF;
        W16(SPR + 0x02, (uint16_t)(RS16(SPR + 0x02) + (int16_t)((uint32_t)(Cos8(a) * 3) >> 16)));
        W16(SPR + 0x04, (uint16_t)(RS16(SPR + 0x04) - (int16_t)((uint32_t)(Sin8(a) * 3) >> 16)));
    }
    W16(SPR + 0x10, 0x0600);
    W16(SPR + 0x14, (uint16_t)(RS16(o + 0x48) >> 6));
    W32(SPR + 0x18, 0x80);
    W16(o + 0x5A, 1);
    Anim_Tick(o + 0x58);                                        /* barrel */
    W16(SPR + 0x10, (R16(o + 2) & 0x8000) | 0x0200);
    W32(SPR + 0x18, 0x80);
    W16(SPR + 0x02, (uint16_t)bx);
    W16(SPR + 0x04, (uint16_t)by);
    W16(o + 0x5A, R16(o + 0x4C) != 0 ? 3 : 2);
    Anim_Tick(o + 0x58);                                        /* mount */
}

/* Projectile 0x24 (0x06059700): cannon ball (anim 2), straight line, freed 128 px off
 * screen; Projectile_Collide(1, 0x9000) -> freed + projectile 5 at its place. */
void Proj_CannonBall(uint32_t e)
{
    if (R16(e + 4) & 1) {
        Anim_Start(e + 0x20, DEFS10, SCR10, 0x10, 2);
        AND16(e + 4, 0xFFFE);
    }
    if (!FROZEN) {
        W32(e + 8, R32(e + 8) + R32(e + 0x18));
        W32(e + 0xC, R32(e + 0xC) - R32(e + 0x1C));
        int32_t dx = RS32(e + 8) - RS32(CAM_X), dy = RS32(e + 0xC) - RS32(CAM_Y);
        if (dx < -0x800000 || dx > 0x1C00000 || dy < -0x800000 || dy > 0x1C00000) { W16(e, 0xFFFF); return; }
        W16(e + 0x12, 0x7F);
        if ((int16_t)Projectile_Collide(1, 0x9000) != 0) {
            W16(e, 0xFFFF);
            Projectile_Spawn(5, RS32(e + 8), RS32(e + 0xC), 0, 0);
            return;
        }
    }
    spr((int16_t)((uint32_t)(RS32(e + 8) - RS32(CAM_X)) >> 16), (int16_t)((uint32_t)(RS32(e + 0xC) - RS32(CAM_Y)) >> 16),
        0x0200, 0x7F, 0x20, 0, 0);
    Anim_Tick(e + 0x20);
}

/* 0x101C: crate (anim 3): any hit (+2 & 8 from the sword here, or a shuriken) bursts it
 * (6 explosion parts, sound) and drops item +0x18. */
void ObjHandler_101C(void)
{
    uint32_t o = CUR;
    if (R16(o + 2) & 1) {
        start(o, DEFS10, SCR10, 3);
        W16(o + 0x14, 0);
        W16(o + 0x1C, 0x8000);
    }
    if (dx160(o) > 0x1800000) return;
    if (!FROZEN) {
        if (!(R16(o + 2) & 8) && Hitbox_Test(o, 0, PL, 2) != 0xFFFF) OR16(o + 2, 8);
        if (R16(o + 2) & 8) {
            Obj_Burst6(o);
            sub_06007CDE(0x115);
            Item_Drop(RS16(o + 0x18), RS32(o + 8), RS32(o + 0xC));
            W16(o, 0xFFFF);
            return;
        }
    }
    spr(scr_x(o), scr_y(o), 0x0200, 0xFFFFFF80u, 0x10, 0, 0);
    Anim_Tick(o + 0x58);
}

/* 0x101D: background decor (anim 4, frame flags & 0x1F) scrolling left by the speed
 * table 0x060ABF16[flags & 0x1F] on a 1/8 parallax plane (wraps over 256 px). */
void ObjHandler_101D(void)
{
    uint32_t o = CUR;
    if (R16(o + 2) & 1) {
        start(o, DEFS10, SCR10, 4);
        W16(o + 0x14, 0);
        W16(o + 0x1C, 0x8000);
    }
    int32_t p = FixMul(RS32(CAM_X) - 0x04000000, 0x2000);
    if (!FROZEN) {
        W32(o + 8, R32(o + 8) - R16(0x060ABF16u + 2u * (R16(o + 0x12) & 0x1F)));
        if ((p + 0x04000000) - RS32(o + 8) > 0x1000000) W32(o + 8, (uint32_t)(p + 0x06000000));
    }
    spr((int16_t)((uint32_t)(RS32(o + 8) - (p + 0x04000000)) >> 16),
        (int16_t)((uint32_t)(RS32(o + 0xC) - RS32(LAYER_DESC + 4)) >> 16), 0x0200, 0xFFFFFF01u, 0x20, 0xFFFF, 2);
    W16(o + 0x5A, R16(o + 0x12) & 0x1F);
    Anim_Tick(o + 0x58);
}

/* 0x060459A0: does Sho's sword (his hitboxes of kind 2, union) touch a map cell of
 * `layer` whose collision type (0x060CA700[(attr >> 4) & 7]) has bits 0xC? Scans the
 * bottom edge of the sword box in 8 px steps; pt = the cell position found. */
static int Player_SwordMapHit(int16_t layer, int32_t pt[2])
{
    uint32_t hb = R32(PL + 0x68);
    if (layer < 0 || hb == 0 || RS16(hb) == -1) return 0;
    int16_t l = 0x7FFF, t = 0x7FFF, r = -0x8000, b = -0x8000, n = 0;
    for (int16_t i = 0; i < 0xB && RS16(hb) != -1; i++, hb += 10) {
        if (RS16(hb) != 2) continue;
        n++;
        if (RS16(hb + 2) < l) l = RS16(hb + 2);
        if (RS16(hb + 4) < t) t = RS16(hb + 4);
        if (RS16(hb + 6) > r) r = RS16(hb + 6);
        if (RS16(hb + 8) > b) b = RS16(hb + 8);
    }
    (void)t;
    if (n == 0) return 0;
    pt[1] = b * 0x10000 + RS32(PL + 0xC);
    int16_t cnt = (int16_t)sdiv(iabs(r - l) + 4, 8);
    if (cnt == 0) return 0;
    pt[0] = (R16(PL + 2) & 0x8000) ? RS32(PL + 8) - r * 0x10000 : RS32(PL + 8) + l * 0x10000;
    uint32_t desc = LAYER_DESC + (((uint32_t)(uint8_t)layer * 0x30) & 0xFF);
    for (; cnt > 0; cnt--) {
        uint16_t a = Map_GetAttr(pt[0], pt[1], desc);
        uint16_t ty = a == 0xFFFF ? 0 : R16(0x060CA700u + 2u * ((a >> 4) & 7));   /* 0x06045B88 */
        if (ty & 0xC) return 1;
        pt[0] += 0x80000;
    }
    return 0;
}
/* 0x0605A16C: 8 debris objects (0x101E flags 1) around pt, table 0x060ABF22
 * {angle, dx, dy, frame}. */
static void Debris101E_Spawn(const int32_t pt[2])
{
    for (int16_t i = 0; i < 8; i++) {
        uint32_t e = 0x060ABF22u + (uint32_t)i * 8;
        int s = Obj_Register(0x101E, RS16(e + 2) * 0x10000 + pt[0], RS16(e + 4) * 0x10000 + pt[1], 1, 0xFFFF);
        if ((int16_t)s < 0) continue;
        uint32_t p = slot_addr(s);
        W16(p + 0x48, R16(e));
        W16(p + 0x4A, ((uint16_t)s & 0xF) << 4);
        W16(p + 0x4C, R16(e + 6));
    }
}
/* 0x101E: flags & 1 = 0: watcher: Sho's sword on a breakable cell of layer 2 throws 8
 * debris (sound, 32 frames cooldown; not with attack kinds 4/5/6). The map is unchanged.
 * flags & 1: debris (anim 5, frame +0x4C): flies at angle +0x48 2 px/frame, bobbing
 * (sin +0x4A x 1.375), spins, fades after 64 frames, freed after 96. */
void ObjHandler_101E(void)
{
    uint32_t o = CUR;
    if (R16(o + 2) & 1) {
        start(o, DEFS10, SCR10, 5);
        W16(o + 0x14, 0);
        W16(o + 0x1C, 0x8000);
    }
    if (!FROZEN) {
        if (!(R16(o + 0x12) & 1)) {
            if (R16(o + 0x14) != 0) { W16(o + 0x14, R16(o + 0x14) - 1); return; }
            int16_t k = (int16_t)Player_AttackKind();
            if (k == 4 || k == 5 || k == 6) return;
            int32_t pt[2];
            if ((int16_t)Player_SwordMapHit(2, pt) == 0) return;
            Debris101E_Spawn(pt);
            W16(o + 0x14, 0x20);
            sub_06007CDE(0x20A);
            return;
        }
        W32(o + 8, (uint32_t)(RS32(o + 8) + FixMul(Cos8(RS16(o + 0x48)), 0x20000)));
        W32(o + 0xC, (uint32_t)(RS32(o + 0xC) - FixMul(Sin8(RS16(o + 0x48)), 0x20000)));
        W32(o + 0xC, (uint32_t)(RS32(o + 0xC) + FixMul(Sin8(RS16(o + 0x4A)), 0x16000)));
        W16(o + 0x4A, R16(o + 0x4A) + 2);
        W16(o + 0x14, R16(o + 0x14) + 1);
        if (R16(o + 0x14) > 0x5F) { W16(o, 0xFFFF); return; }
    }
    W16(SPR + 0x02, (uint16_t)scr_x(o));
    W16(SPR + 0x04, (uint16_t)scr_y(o));
    W16(SPR + 0x10, 0x0600);
    W16(SPR + 0x14, R16(o + 0x4A));
    W16(SPR + 0x0E, R16(o + 0x14) < 0x40 ? 0 : (uint16_t)((R16(o + 0x14) - 0x40) >> 3));
    W16(SPR + 0x0A, 0x10);
    W32(SPR + 0x18, 0xFFFFFF7Fu);
    W16(SPR + 0x0C, 0xFFFF);
    W16(o + 0x5A, R16(o + 0x4C));
    Anim_Tick(o + 0x58);
}

/* 0x101F: rope (anim 6) swinging +-43 (8-bit angle; speed 0x100 or 0x134 per frame if
 * flags & 1). When Sho's top (y - 72 px) is within 32 px of the rope end (96 px below the
 * pivot) it sets PL+0x10 |= 0x10; if he is jumping (PL+2 & 0x400) he grabs it:
 * 0x060EA440 = rope, PL+0x10 |= 0x90, PL+4 |= 0x100, +0x10 |= 0x80, and PL+0x38/+0x3C get
 * the pull towards the end (clamped to 6 px). */
void ObjHandler_101F(void)
{
    uint32_t o = CUR;
    if (R16(o + 2) & 1) {
        start(o, DEFS10, SCR10, 6);
        W16(o + 0x14, 0);
        W16(o + 0x1C, 0x8000);
        W16(o + 0x48, 0);
    }
    if (!FROZEN) W16(o + 0x14, R16(o + 0x14) + ((R16(o + 0x12) & 1) ? 0x134 : 0x100));
    if (dx160(o) > 0x1800000) return;
    int16_t ang = (int16_t)((uint32_t)(Sin8(R16(o + 0x14) >> 8) * 0x2B) >> 16);
    W16(o + 0x48, (uint16_t)ang);
    if (!FROZEN && !(R16(PL + 0x10) & 0x10)) {
        int32_t ex = RS32(o + 8) - FixMul(0x600000, Sin8(ang));
        int32_t ey = RS32(o + 0xC) + FixMul(0x600000, Cos8(ang));
        if (iabs(RS32(PL + 8) - ex) < 0x200000 && iabs(RS32(PL + 0xC) - ey - 0x480000) < 0x200000) {
            OR16(PL + 0x10, 0x10);
            if (R16(PL + 2) & 0x0400) {
                W32(PL_GRAB, o);
                OR16(PL + 0x10, 0x90);
                OR16(PL + 4, 0x100);
                OR16(o + 0x10, 0x80);
                int32_t d = ex - RS32(PL + 8);
                if (iabs(d) > 0x60000) d = d < 0 ? -0x60000 : 0x60000;
                W32(PL + 0x38, (uint32_t)(RS32(PL + 0x38) + d));
                d = ey - RS32(PL + 0xC) + 0x480000;
                if (iabs(d) > 0x60000) d = d < 0 ? -0x60000 : 0x60000;
                W32(PL + 0x3C, (uint32_t)(RS32(PL + 0x3C) - d));
            }
        }
    }
    spr(scr_x(o), scr_y(o), 0x0600, 0x80, 0x10, 0, 0);
    W16(SPR + 0x14, (uint16_t)-ang);
    Anim_Tick(o + 0x58);
}
