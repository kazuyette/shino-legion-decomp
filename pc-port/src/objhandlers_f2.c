/* Stage 4-1 objects 0x1029-0x1030 and 0x1037 (file STAGE4_1, stage index 6), ported from
 * SHINOBI.PPB. Sprite defs 0x060AA6CC, scripts 0x060D25D4, chars 0x10+. See objhandlers_f.h. */
#include "objhandlers_f_int.h"
#include "collision.h"

#define DEFS41  0x060AA6CCu
#define SCR41   0x060D25D4u
#define F41(o)  f_anim_cur(o, DEFS41, SCR41)

/* near-screen test on a position kept in +0x4A/+0x4C (px) */
static int vis_px(uint32_t o, int16_t px, int16_t py, int force)
{
    int32_t d = px * 0x10000 - CX + (int32_t)0xFF600000;
    if (d < 0) d = CX - px * 0x10000 + 0xA00000;
    if (!(d < 0x01000000 || force)) return 0;
    d = py * 0x10000 - CY + (int32_t)0xFF600000;
    if (d < 0) d = CY - py * 0x10000 + 0xA00000;
    (void)o;
    return d < 0x01000000 || force;
}
static int sword_cut(uint32_t o)        /* Sho's sword (not attack kinds 4/5) on body box 0 */
{
    if (R16(o + 2) & 8) return 0;
    if ((int16_t)Hitbox_Test(o, 0, PL, 2) == -1) return 0;
    int16_t k = (int16_t)Player_AttackKind();
    return k != 4 && k != 5;
}
static void scratch_draw(int anim, int16_t x, int16_t y, uint16_t ctrl, uint16_t pmod, uint16_t bank, uint32_t prio)
{
    Anim_Start(SCRATCH_ANIM, DEFS41, SCR41, 0x10, anim);
    W16(SPR + 2, (uint16_t)x); W16(SPR + 4, (uint16_t)y);
    f_spr(ctrl, pmod, bank, 0, prio);
    Anim_Tick(SCRATCH_ANIM);
}

/* ---- 0x1029 (0x06052ECC): hanging lantern. The sword cuts it (sound 0x122, spark): item +0x18
 * with an explosion part, the cut-off top (anim 2, 0x06053308) falls left spinning for 57
 * frames, the base (anim 1, 0x060533C0) stays 336 frames then blinks; the lantern (anim 0)
 * grows to scale 1 (+0x14 << 8) and shrinks back. +0x4A/+0x4C spawn position (px). */
static void Lantern41_Top(uint32_t o)                   /* 0x06053308 */
{
    Anim_Start(SCRATCH_ANIM, DEFS41, SCR41, 0x10, 2);
    W16(SPR + 2, (uint32_t)((RS32(o + 8) - CX) >> 16));
    W16(SPR + 4, (uint32_t)((RS32(o + 0xC) - CY - 0x160000) >> 16));
    f_spr(0x0600, (uint16_t)(RS16(o + 0x4E) >> 4), 0x20, 0, 0xFFFFFF7Eu);
    W16(SPR + 0x14, (uint32_t)(RS16(o + 0x4E) * 0xF));
    if (RS16(o + 0x4E) & 1) Anim_Tick(SCRATCH_ANIM);
}
static void Lantern41_Base(uint32_t o)                  /* 0x060533C0 */
{
    Anim_Start(SCRATCH_ANIM, DEFS41, SCR41, 0x10, 1);
    W16(SPR + 2, (uint32_t)((RS16(o + 0x4A) * 0x10000 - CX) >> 16));
    W16(SPR + 4, (uint32_t)((RS16(o + 0x4C) * 0x10000 - CY) >> 16));
    f_spr(0x0200, 0, 0x20, 0, 0xFFFFFF7Eu);
    if (RS16(o + 0x52) < 0x41 || ((RS16(o + 0x52) + 1) & 1)) Anim_Tick(SCRATCH_ANIM);
}
void ObjHandler_1029(void)
{
    uint32_t o = CUR;
    if (R16(o + 2) & 1) {
        f_anim(o, DEFS41, SCR41, 3);
        AND16(o + 2, 0xFFFE);
        W16(o + 0x14, 0); W16(o + 0x48, 0);
        W16(o + 0x4A, R32(o + 8) >> 16); W16(o + 0x4C, R32(o + 0xC) >> 16);
        W16(o + 0x4E, 0); W16(o + 0x50, 0); W16(o + 0x52, 0);
        OR16(o + 0x1C, 0x8000); OR16(o + 0x12, 0x4000);
    }
    int f = RS16(o + 0x48) != 0;
    if (!vis_px(o, RS16(o + 0x4A), RS16(o + 0x4C), f)) return;
    if (!FROZEN) {
        if (sword_cut(o)) {
            SFX(0x122);
            Obj_Spawn(2, RS32(HIT_POINT), RS32(HIT_POINT + 4), 0, 0);
            OR16(o + 2, 8);
            W16(o + 0x48, 1); W16(o + 0x50, 1);
            f_anim(o, DEFS41, SCR41, 0);
            if (RS16(o + 0x18) != -1) {
                Obj_Spawn(6, RS16(o + 0x4A) << 16, RS16(o + 0x4C) << 16, 0, 0);
                Item_Drop(RS16(o + 0x18), RS16(o + 0x4A) << 16, RS16(o + 0x4C) << 16);
            }
        } else {
            if (RS16(o + 0x48) == 1) {
                W32(o + 8, (uint32_t)(RS32(o + 8) + RS16(o + 0x4E) * -0x800));
                W32(o + 0xC, (uint32_t)(RS32(o + 0xC) - (RS16(o + 0x4E) * -0x2000 + 0x40000)));
                if (RS16(o + 0x4E) == 0x38) W16(o + 0x48, 2);
                W16(o + 0x4E, R16(o + 0x4E) + 1);
            }
            if (RS16(o + 0x50) == 1) {
                if (RS16(o + 0x52) == 0x150) W16(o + 0x50, 2); else W16(o + 0x52, R16(o + 0x52) + 1);
                W16(o + 0x14, R16(o + 0x14) + 1);
            }
            if (RS16(o + 0x50) == 2) {
                if (R16(o + 0x14) < 0x60) W16(o + 0x50, 3); else W16(o + 0x14, R16(o + 0x14) - 1);
            }
        }
    }
    if (RS16(o + 0x48) == 1) Lantern41_Top(o);
    if (RS16(o + 0x50) == 1) Lantern41_Base(o);
    W16(SPR + 2, (uint32_t)((RS16(o + 0x4A) * 0x10000 - CX) >> 16));
    W16(SPR + 4, (uint32_t)((RS16(o + 0x4C) * 0x10000 - CY) >> 16));
    f_spr(0x0200, 0, 0x20, 0, 0xFFFFFF7Eu);
    if (RS16(o + 0x50) != 0) {
        W16(SPR + 0x10, 0x1A00);
        uint32_t s = (uint32_t)R16(o + 0x14) << 8;
        if (s > 0xFFFF) s = 0x10000;
        W32(SPR + 0x1C, s); W32(SPR + 0x20, s);
    }
    if (RS16(o) != -1 && (RS16(o + 0x50) == 0 || (VBL & 1))) Anim_Tick(o + 0x58);
}

/* ---- 0x1030 (0x06053470): paper screen (anim 0xD). Cut by the sword: 6 sparks (0x0605381C),
 * item +0x18 (flags & 1 = 0). Flags & 1: an enemy hides behind it and bursts out when Sho is
 * within 16 px vertically / 160 px horizontally: type 2 (flags & 8) or 4 (flags & 2/4; flags
 * & 4 -> enemy flags 1), param +0x18. The tear (anim 0xE, distorted quad 0x06053908) grows
 * for 24 frames. */
static void Screen_Sparks(uint32_t o)                   /* 0x0605381C */
{
    static const int32_t d[6][2] = { { -0x240000, -0x140000 }, { -0x120000, -0x170000 }, { -0x40000, -0x1A0000 },
                                     { 0x40000, -0x1D0000 }, { 0x120000, -0x200000 }, { 0x240000, -0x240000 } };
    for (int i = 0; i < 6; i++) Obj_Spawn(2, RS32(o + 8) + d[i][0], RS32(o + 0xC) + d[i][1], 0, i * 2);
}
static void Screen_Tear(uint32_t o)                     /* 0x06053908 */
{
    int16_t t = RS16(o + 0x14);
    Anim_Start(SCRATCH_ANIM, DEFS41, SCR41, 0x10, 0xE);
    int16_t dx = (int16_t)((uint32_t)(RS32(o + 8) - CX - 0x280000) >> 16);
    int16_t cy = (int16_t)((uint32_t)(RS32(o + 0xC) - CY - 0x160000) >> 16);
    W16(SPR + 0x30, (uint16_t)dx);                     /* D.x */
    W16(SPR + 0x2E, (uint16_t)cy);                     /* C.y */
    W16(SPR + 0x24, (uint16_t)(dx - t));               /* A */
    W16(SPR + 0x26, (uint16_t)(cy + t * 2 - 0x2E));
    W16(SPR + 0x28, (uint16_t)(t + dx + 0x47));        /* B */
    W16(SPR + 0x2A, (uint16_t)(t + cy - 0x2E));
    W16(SPR + 0x2C, (uint16_t)(dx + 0x47));            /* C.x */
    W16(SPR + 0x32, (uint16_t)cy);                     /* D.y */
    f_spr(0x0204, R16(o + 0x14) >> 3, 0x20, 0, 0xFFFFFF82u);
    if (!(R16(o + 0x14) & 1)) Anim_Tick(SCRATCH_ANIM);
}
void ObjHandler_1030(void)
{
    uint32_t o = CUR;
    if (R16(o + 2) & 1) {
        f_anim(o, DEFS41, SCR41, 0xD);
        AND16(o + 2, 0xFFFE);
        W16(o + 0x14, 0); W16(o + 0x48, 0);
        W16(o + 0x4A, R32(o + 8) >> 16); W16(o + 0x4C, R32(o + 0xC) >> 16);
        OR16(o + 0x1C, 0x8000); OR16(o + 0x12, 0x4000);
    }
    if (!vis_px(o, RS16(o + 0x4A), RS16(o + 0x4C), RS16(o + 0x48) != 0)) return;
    if (!FROZEN) {
        if (sword_cut(o)) {
            SFX(0x122);
            Screen_Sparks(o);
            OR16(o + 2, 8);
            W16(o + 0x48, 1);
            if (!(R16(o + 0x12) & 1) && RS16(o + 0x18) != -1) {
                Obj_Spawn(6, RS16(o + 0x4A) << 16, RS16(o + 0x4C) << 16, 0, 0);
                Item_Drop(RS16(o + 0x18), RS16(o + 0x4A) << 16, RS16(o + 0x4C) << 16);
            }
        }
        if (RS16(o + 0x48) == 0 && (R16(o + 0x12) & 1)) {
            int32_t x = RS16(o + 0x4A) * 0x10000, y = RS16(o + 0x4C) * 0x10000;
            if (f_abs(y - PY) < 0x100000 && f_abs(x - PX) < 0xA00000) {
                W16(o + 0x48, 1);
                OR16(o + 2, 8);
                uint16_t type = 2;   /* the original leaves it uninitialised without flags 2/4/8 (never placed so) */
                if (R16(o + 0x12) & 2) type = 4;
                uint16_t fl = (R16(o + 0x12) & 4) != 0;
                if (fl) type = 4;
                if (R16(o + 0x12) & 8) type = 2;
                SFX(0x122);
                Obj_Register(type, x, y, fl, (uint16_t)RS16(o + 0x18));
                Screen_Sparks(o);
            }
        }
        if (RS16(o + 0x48) == 1 && R16(o + 0x14) < 0x80) W16(o + 0x14, R16(o + 0x14) + 1);
    }
    if (R16(o + 0x14) < 0x18) Screen_Tear(o);
    W16(SPR + 2, (uint32_t)((RS16(o + 0x4A) * 0x10000 - CX) >> 16));
    W16(SPR + 4, (uint32_t)((RS16(o + 0x4C) * 0x10000 - CY) >> 16));
    f_spr(0x0200, 0, 0x20, 0, 0xFFFFFF81u);
    if (RS16(o) != -1) Anim_Tick(o + 0x58);
}

/* ---- 0x102F (0x06053A28): sliding double door (anim 10 right leaf, 0x06053F44 left leaf anim
 * 0xB + (flags & 1)). When Sho comes within 16 px vertically / 240 px horizontally it opens
 * (sound 0x21B, 20 frames x 2 px), releases an enemy (type 4 with flags 2/4 (flags & 4 ->
 * enemy flags 1), type 2 with flags 8, param +0x18 then -1) and stays open 32 frames, closes,
 * then waits 320 frames; it also waits 320 frames after its enemy is gone. */
static void Door_Left(uint32_t o)                       /* 0x06053F44 */
{
    scratch_draw((R16(o + 0x12) & 1) + 0xB, (int16_t)((uint32_t)(RS32(o + 8) - CX + RS16(o + 0x4A) * -0x10000) >> 16),
                 (int16_t)((uint32_t)(RS32(o + 0xC) - CY) >> 16), 0x0200, 0, 0x20, 0xFFFFFF80u);
}
void ObjHandler_102F(void)
{
    uint32_t o = CUR;
    if (R16(o + 2) & 1) {
        f_anim(o, DEFS41, SCR41, 10);
        AND16(o + 2, 0xFFFE);
        W16(o + 0x14, 0); W16(o + 0x48, 0); W16(o + 0x4A, 0); W16(o + 0x4C, 0xFFFF);
        OR16(o + 0x1C, 0x8000); OR16(o + 0x12, 0x4000);
    }
    if (RS16(o + 0x4C) != -1 && RS16(f_slot(RS16(o + 0x4C))) == -1) {
        if (RS16(o + 0x48) == 0 || RS16(o + 0x48) == 4) { W16(o + 0x48, 4); W16(o + 0x14, 0x140); }
        W16(o + 0x4C, 0xFFFF);
    }
    if (f_scr_dx(o) > 0xFFFFFF && RS16(o + 0x48) == 0) return;
    if (f_scr_dy(o) > 0xFFFFFF && RS16(o + 0x48) == 0) return;
    if (FROZEN) goto draw;
    if (RS16(o + 0x48) == 0 && f_abs(RS32(o + 0xC) - PY) < 0x100000 && f_abs(RS32(o + 8) - PX) < 0xF00000 &&
        (RS16(o + 0x4C) == -1 || RS16(f_slot(RS16(o + 0x4C))) == -1)) {
        W16(o + 0x48, 1); W16(o + 0x14, 0x14); W16(o + 0x4A, 0);
        uint16_t type = 4, fl;
        if (R16(o + 0x12) & 2) type = 4;
        fl = (R16(o + 0x12) & 4) != 0;
        if (fl) type = 4;
        if (R16(o + 0x12) & 8) type = 2;
        W16(o + 0x4C, (uint32_t)Obj_Register(type, RS32(o + 8), RS32(o + 0xC), fl, (uint16_t)RS16(o + 0x18)));
        SFX(0x21B);
        goto draw;
    }
    {
        int16_t s = RS16(o + 0x48);
        if (s == 1) {
            if (RS16(o + 0x14) == 0) { W16(o + 0x48, 2); W16(o + 0x14, 0x20); W16(o + 0x18, 0xFFFF); goto draw; }
            W16(o + 0x14, R16(o + 0x14) - 1);
            W16(o + 0x4A, R16(o + 0x4A) + 2);
        }
        if (RS16(o + 0x48) == 2) {
            if (RS16(o + 0x14) == 0) { W16(o + 0x48, 3); W16(o + 0x14, 0x14); goto draw; }
            W16(o + 0x14, R16(o + 0x14) - 1);
        }
        if (RS16(o + 0x48) == 3) {
            if (RS16(o + 0x14) == 0) { W16(o + 0x48, 4); W16(o + 0x14, 0x140); goto draw; }
            W16(o + 0x14, R16(o + 0x14) - 1);
            W16(o + 0x4A, R16(o + 0x4A) - 2);
        }
        if (RS16(o + 0x48) == 4) {
            if (RS16(o + 0x14) == 0) W16(o + 0x48, 0); else W16(o + 0x14, R16(o + 0x14) - 1);
        }
    }
draw:
    Door_Left(o);
    W16(SPR + 2, (uint32_t)((RS32(o + 8) - CX + RS16(o + 0x4A) * 0x10000) >> 16));
    W16(SPR + 4, (uint32_t)((RS32(o + 0xC) - CY) >> 16));
    f_spr(0x0200, 0, 0x20, 0, 0xFFFFFF80u);
    if (RS16(o) != -1) Anim_Tick(o + 0x58);
}

/* ---- 0x102E (0x06053FF4): row of 4 spears / pistons (anim 9; flags & 0x10: hanging, anim 0x10,
 * V-flipped), spaced 16 px (flags & 2: 64 px), towards the left with flags & 1 (then only
 * active with Sho within 384 px). Each cycles phases 0 (48 frames down), 1 (16 frames rising
 * 4 px a frame), 2 (48 / with flags & 8 112 frames up 64 px), 3 (16 frames retracting;
 * sound 0x105 for hanging ones on screen); flags & 4 offsets the timers. Each one's weapon box
 * hurts Sho (0x060601BA, 1). Timers +0x48..+0x4E, phases bytes +0x50..+0x53. */
static int32_t spear_dy(uint32_t o, int i)
{
    int ph = Ram_Ptr(o + 0x50 + (uint32_t)i)[0];
    int16_t t = RS16(o + 0x48 + 2u * (uint32_t)i);
    int up = (R16(o + 0x12) & 0x10) != 0;
    if (ph == 1) return up ? (t * -4 + 0x40) * -0x10000 : (t * -4 + 0x40) * 0x10000;
    if (ph == 2) return up ? -0x400000 : 0x400000;
    if (ph == 3) return up ? t * -0x40000 : t * 0x40000;
    return 0;
}
void ObjHandler_102E(void)
{
    uint32_t o = CUR;
    if (R16(o + 2) & 1) {
        OR16(o + 0x12, 0x4000);
        f_anim(o, DEFS41, SCR41, (R16(o + 0x12) & 0x10) ? 0x10 : 9);
        AND16(o + 2, 0xFFFE);
        W16(o + 0x14, 0);
        W16(o + 0x48, 0); W16(o + 0x4A, 4); W16(o + 0x4C, 8); W16(o + 0x4E, 0xC);
        if (R16(o + 0x12) & 4) { W16(o + 0x48, 0x10); W16(o + 0x4A, 0x14); W16(o + 0x4C, 0x18); W16(o + 0x4E, 0x1C); }
        W16(o + 0x50, 0);
        OR16(o + 0x1C, 0x8000);
        OR16(o + 2, 8);
    }
    int32_t step = (R16(o + 0x12) & 2) ? 0x400000 : 0x100000;
    if (!FROZEN) {
        for (uint32_t i = 0; i < 4; i++) {
            uint32_t t = o + 0x48 + 2 * i;
            uint8_t *ph = Ram_Ptr(o + 0x50 + i);
            if (R16(t) == 0) {
                *ph = (uint8_t)((*ph + 1) & 3);
                if (*ph == 0) W16(t, 0x30);
                if (*ph == 1) W16(t, 0x10);
                if (*ph == 2) W16(t, (R16(o + 0x12) & 8) ? 0x70 : 0x30);
                if (*ph == 3) W16(t, 0x10);      /* (the on-screen sound is compiled out: debug flag 0) */
            } else W16(t, R16(t) - 1);
        }
        if (!(R16(o + 0x12) & 1) || (RS32(o + 8) - 0x1800000 < PX && PX < RS32(o + 8) + 0x1800000)) {
            for (int i = 0; i < 4; i++) {
                uint32_t sx = R32(o + 8), sy = R32(o + 0xC);
                if (!(R16(o + 0x12) & 1)) W32(o + 8, (uint32_t)i * (uint32_t)step + sx);
                else                      W32(o + 8, sx - (uint32_t)i * (uint32_t)step);
                W32(o + 0xC, (uint32_t)(RS32(o + 0xC) + spear_dy(o, i)));
                ObjF_SwordHurtsPlayer(1);
                W32(o + 8, sx); W32(o + 0xC, sy);
            }
        }
    }
    if (f_scr_dx(o) >= 0x1800000 || f_scr_dy(o) >= 0x1800000 || RS16(o) == -1) return;
    OR16(o + 0x58, 0x4000);
    Anim_Tick(o + 0x58);
    for (int i = 0; i < 4; i++) {
        int32_t y = RS32(o + 0xC) + spear_dy(o, i);
        int16_t sp = (R16(o + 0x12) & 2) ? 0x40 : 0x10;
        sp = (R16(o + 0x12) & 1) ? (int16_t)-(sp * i) : (int16_t)(sp * i);
        W16(SPR + 2, (uint32_t)((int16_t)((uint32_t)(RS32(o + 8) - CX) >> 16) + sp));
        W16(SPR + 4, (uint32_t)((y - CY) >> 16));
        f_spr((R16(o + 0x12) & 0x10) ? 0x4200 : 0x0200, 0, 0x20, 0, 0x80);
        Sprite_Draw(SPR);
    }
}

/* Enemies standing on / pushed by `self` (0x06056160: Obj_CarryContact + ceiling bump). */
static int carry_contact2(uint32_t self, uint32_t e, int16_t w, int16_t h)
{
    ObjRect a, b;
    int32_t d[2];
    a.l = RS32(e + 8) - (w >> 1) * 0x80000;
    a.r = w * 0x80000 + RS32(e + 8) - (w >> 1) * 0x80000;
    a.t = RS32(e + 0xC) - h * 0x80000;
    a.b = RS32(e + 0xC);
    uint32_t hb = R32(self + 0x68);
    b.l = RS16(hb + 2) * 0x10000 + RS32(self + 8);
    b.r = RS16(hb + 6) * 0x10000 + RS32(self + 8);
    b.t = RS16(hb + 4) * 0x10000 + RS32(self + 0xC) - 2;
    b.b = RS16(hb + 8) * 0x10000 + RS32(self + 0xC) - 2;
    if (!Rect_Intersect(&a, &b, d)) return 0;
    OR16(e + 0x10, 0x1000);
    Obj_Push(e, d[0], -d[1]);
    if (d[0] == 0 && d[1] < 0) OR16(e + 0x10, 0x2000);
    if (d[0] == 0 && d[1] > 0 && !(R16(e + 0x10) & 0x2000) && (R16(e + 2) & 0x4000) && RS32(e + 0x34) >= 0)
        W32(e + 0x34, 0xFFFFFFFFu);
    return 1;
}
/* 0x06056072: enemies on `self` are moved by (dx, -dy); crushed ones (+0x10 & 0x400) die. */
static void carry_enemies2(uint32_t self, int32_t dx, int32_t dy)
{
    for (int16_t s = 0; s < 0xC0; s++) {
        uint32_t e = f_slot(s);
        if (R16(e) == 0xFFFF || R16(e) >= 0x1000) continue;
        if (Enemy_FindSlot((int16_t)R16(e)) == -1) continue;
        if (!carry_contact2(self, e, 2, 8)) continue;
        if (!(R16(e + 0x10) & 0x400)) {
            if (R16(e + 0x10) & 0x2000) {
                W32(e + 8, (uint32_t)(RS32(e + 8) + dx));
                W32(e + 0xC, (uint32_t)(RS32(e + 0xC) - dy));
            }
        } else { W16(e + 0x16, 0); OR16(e + 2, 8); }
    }
}

/* ---- 0x102D (0x060546FC): lift (anim 7; flags & 0x10 anim 8). Carries the enemies within
 * 32 x 96 px; Sho's sword clangs on it. Without flags & 8: starts once its guard (enemy type 7
 * with the same flags & 3) is gone: after 32 frames, with Sho on it (within 256 px left, 96 px
 * vertically) it locks Sho's input (+4 |= 0x400), rises 96 px (sound 0x211), waits until Sho
 * walks 112 px right (+4 |= 0x10 meanwhile), goes back down and unlocks. With flags & 8: falls
 * 80 px (after 72 frames) when Sho approaches its left side, raised back by the 0x102C switch
 * (+0x48 = 3). */
void ObjHandler_102D(void)
{
    uint32_t o = CUR;
    if (R16(o + 2) & 1) {
        f_anim(o, DEFS41, SCR41, (R16(o + 0x12) & 0x10) ? 8 : 7);
        AND16(o + 2, 0xFFFE);
        W16(o + 0x48, 0); W16(o + 0x1C, 0x4000); W16(o + 0x4A, 0xFFFF); W16(o + 0x14, 0x20);
        if (!(R16(o + 0x12) & 8))
            for (uint16_t s = 0; s < 0xC0; s++)
                if (RS16(f_slot(s)) == 7 && (R16(f_slot(s) + 0x12) & 3) == (R16(o + 0x12) & 3)) W16(o + 0x4A, s);
        OR16(o + 0x12, 0x4000);
    }
    if (!(f_scr_dx(o) < 0x1000000 || RS16(o + 0x48) != 4)) return;
    if (!(f_scr_dy(o) < 0x1000000 || RS16(o + 0x48) != 4)) return;
    if (!FROZEN) {
        Obj_PlayerContact(0);
        ObjF_SwordClang(o);
        for (uint16_t s = 0; s < 0xC0; s++) {
            uint32_t e = f_slot(s);
            uint16_t t = R16(e);
            if (t == 0xFFFF || t >= 0x1001) continue;
            if (f_abs(RS32(o + 8) - RS32(e + 8)) < 0x200000 && f_abs(RS32(o + 0xC) - RS32(e + 0xC)) < 0x600000)
                Obj_CarryContact(o, e, 2, 0xC);
        }
        if (!(R16(o + 0x12) & 8)) {
            if (RS16(o + 0x48) == 0 && RS16(o + 0x4A) != -1 && RS16(f_slot(RS16(o + 0x4A))) == -1) {
                W16(o + 0x48, 1); W16(o + 0x14, 0x20); W16(o + 0x4A, 0xFFFF);
            }
            if (RS16(o + 0x48) == 1) {
                if (RS16(o + 0x14) == 0) {
                    if (RS32(o + 8) - 0x1000000 < PX && f_abs(PY - RS32(o + 0xC)) < 0x600000) {
                        OR16(PL + 4, 0x400);
                        W16(o + 0x48, 2); W16(o + 0x14, 0x60);
                        SFX(0x211);
                    }
                } else W16(o + 0x14, R16(o + 0x14) - 1);
            }
            if (RS16(o + 0x48) == 2) {
                if (RS16(o + 0x14) == 0) {
                    if (PX - RS32(o + 8) > 0x700000) {
                        AND16(PL + 4, 0xFFEF);
                        W16(o + 0x48, 3); W16(o + 0x14, 0x60);
                        SFX(0x211);
                    } else OR16(PL + 4, 0x10);
                } else { W32(o + 0xC, R32(o + 0xC) - 0x10000); W16(o + 0x14, R16(o + 0x14) - 1); }
            }
            if (RS16(o + 0x48) == 3) {
                if (RS16(o + 0x14) == 0) { W16(o + 0x48, 4); AND16(PL + 4, 0xFBFF); }
                else { W32(o + 0xC, R32(o + 0xC) + 0x10000); W16(o + 0x14, R16(o + 0x14) - 1); }
            }
        } else {
            if (RS16(o + 0x48) == 0 && f_abs(PY - RS32(o + 0xC)) < 0x400000 &&
                RS32(o + 8) - 0x400000 < PX && PX < RS32(o + 8) - 0x100000) {
                W16(o + 0x48, 1); W16(o + 0x14, 0x48);
            }
            if (RS16(o + 0x48) == 1) {
                if (RS16(o + 0x14) == 0) { SFX(0x211); W16(o + 0x48, 2); W16(o + 0x14, 0x50); }
                else W16(o + 0x14, R16(o + 0x14) - 1);
            }
            if (RS16(o + 0x48) == 2 && RS16(o + 0x14) != 0) {
                W32(o + 0xC, R32(o + 0xC) + 0x10000); W16(o + 0x14, R16(o + 0x14) - 1);
            }
            if (RS16(o + 0x48) == 3) {
                if (RS16(o + 0x14) == 0) W16(o + 0x48, 0);
                else { W32(o + 0xC, R32(o + 0xC) - 0x10000); W16(o + 0x14, R16(o + 0x14) - 1); }
            }
        }
    }
    int16_t sx = (int16_t)((uint32_t)(RS32(o + 8) - CX) >> 16);
    if (RS16(o + 0x48) == 2 || RS16(o + 0x48) == 3) sx = (int16_t)(sx + ((R16(o + 0x14) & 1) ? 1 : -1));
    W16(SPR + 2, (uint16_t)sx);
    W16(SPR + 4, (uint32_t)((RS32(o + 0xC) - CY) >> 16));
    f_spr(0x0200, 0, 0x20, 0, 0x81);
    if (RS16(o) != -1) Anim_Tick(o + 0x58);
}

/* ---- 0x102C (0x06054CA4): floor switch (anim 6) of the flags-8 lift: Sho stands on it ->
 * 104 frames sinking (colour-calc steps), sound 0x211, hidden until Sho is 192 px above it,
 * then 104 frames back and the lift is sent up (+0x48 = 3, 80 frames). */
void ObjHandler_102C(void)
{
    uint32_t o = CUR;
    if (R16(o + 2) & 1) {
        OR16(o + 0x12, 0x4000);
        f_anim(o, DEFS41, SCR41, 6);
        AND16(o + 2, 0xFFFE);
        W16(o + 0x14, 0); W16(o + 0x48, 0); W16(o + 0x1C, 0x4000);
        for (uint16_t s = 0; s < 0xC0; s++)
            if (R16(f_slot(s)) == 0x102D && (R16(f_slot(s) + 0x12) & 8)) W16(o + 0x4A, s);
    }
    if (f_scr_dx(o) >= 0x1000000 || f_scr_dy(o) >= 0x2000000) return;
    if (!FROZEN) {
        if (RS16(o + 0x48) == 0) {
            if ((int16_t)Obj_PlayerContact(0) == 2) { W16(o + 0x48, 1); W16(o + 0x14, 0x68); }
            ObjF_SwordClang(o);
        }
        if (RS16(o + 0x48) == 1) {
            if (RS16(o + 0x14) == 0) { W16(o + 0x48, 2); SFX(0x211); }
            else {
                if (R16(o + 0x14) > 0x20) { Obj_PlayerContact(0); ObjF_SwordClang(o); }
                W16(o + 0x14, R16(o + 0x14) - 1);
            }
        }
        if (RS16(o + 0x48) == 2 && PY - RS32(o + 0xC) > 0xC00000) { W16(o + 0x48, 3); W16(o + 0x14, 0x68); }
        if (RS16(o + 0x48) == 3) {
            if (RS16(o + 0x14) == 0) {
                W16(o + 0x48, 0);
                uint32_t l = f_slot(RS16(o + 0x4A));
                W16(l + 0x48, 3); W16(l + 0x14, 0x50);
                SFX(0x211);
            } else W16(o + 0x14, R16(o + 0x14) - 1);
        }
    }
    W16(SPR + 2, (uint32_t)((RS32(o + 8) - CX) >> 16));
    W16(SPR + 4, ((R32(o + 0xC) & 0xFFFF0000u) - (R32(CAM_Y) & 0xFFFF0000u)) >> 16);
    f_spr(0x0200, 0, 0x20, 1, 0x81);
    if (RS16(o + 0x48) == 1) W16(SPR + 0x0E, (uint32_t)(3 - (R16(o + 0x14) >> 5)));
    if (RS16(o + 0x48) == 3) W16(SPR + 0x0E, R16(o + 0x14) >> 5);
    if (RS16(o) != -1 && RS16(o + 0x48) != 2) Anim_Tick(o + 0x58);
}

/* 0x06055D6E: one falling rope segment (0x1037) at (x, y); odd ones mirrored. */
static int RopeSeg_Spawn(int32_t x, int32_t y, uint16_t odd, uint16_t t)
{
    int16_t s = (int16_t)Obj_Register(0x1037, x, y, 0, 0xFFFF);
    if (s < 0) return -1;
    if (odd & 1) OR16(f_slot(s) + 2, 0x8000);
    W16(f_slot(s) + 0x14, t);
    return 0;
}

/* ---- 0x102B (0x06055090): rope (anim 5, 16-px segments; flags & 0x1F 10..15: chain, anim 0x12,
 * +4 |= 1). +0x4A length (px), +0x50 rest length (table by flags & 0x1F), +0x48 state:
 * 0 lowering 1 px a frame (chain: accelerating) until the map is hit 16 px below its end,
 * 1/2 sway (+0x4C up to 8 and back; segments rotate alternately), 3 rewinding to +0x50
 * (sound 0x210), 4 rest 48 frames, 5 just cut (64 frames), 6 idle (also when Sho is dead).
 * The sword cuts it at the hit height (rounded to 16 px): the segments below fall as 0x1037,
 * +0x4E = -1 (the platform 0x102A hanging from it falls too). Its anim hitbox is rewritten to
 * the rope length (shared sprite data, as the original). */
void ObjHandler_102B(void)
{
    uint32_t o = CUR;
    if (R16(o + 2) & 1) {
        static const int16_t len[19] = { 0, 0x80, 0x50, 0xA0, 0x90, 0x60, 0x80, 0x80, 0x140, 0xA0,
                                         0, 0, 0, 0, 0, 0, 0x68, 0x100, 0x50 };
        OR16(o + 0x12, 0x4000);
        f_anim(o, DEFS41, SCR41, 5);
        AND16(o + 2, 0xFFFE);
        W16(o + 0x48, 0); W16(o + 0x4C, 0); W16(o + 0x4E, 0); W16(o + 0x50, 0); W16(o + 0x52, 0); W16(o + 0x14, 0);
        uint16_t k = R16(o + 0x12) & 0x1F;
        if ((k >= 1 && k <= 9) || k >= 0x10) {
            if (k <= 0x12) { W16(o + 0x4A, (uint16_t)len[k]); W16(o + 0x50, (uint16_t)len[k]); }
        }
        if (k == 2 || k == 5 || k == 6 || k == 0x12) W16(o + 0x4E, 0xFFFF);
        if (k == 2 || k == 4 || k == 5 || k == 6 || k == 8 || k == 0x12) W16(o + 0x48, 6);
        if (k > 9 && k < 0x10) {
            f_anim(o, DEFS41, SCR41, 0x12);
            W16(o + 0x4A, 0x20); W16(o + 0x50, 0x20);
            OR16(o + 4, 1);
        }
        OR16(o + 0x1C, 0x8000);
        OR16(o + 0x58, 0x4000);
        Anim_Tick(o + 0x58);
    }
    if (f_scr_dx(o) > 0x10FFFFF || f_scr_dy(o) > 0x27FFFFF) return;
    W16(o + 0x16, 0x7FFF);
    W32(o + 0x30, R32(o + 8));
    W32(o + 0x34, R32(o + 0xC));
    uint32_t hb = R32(o + 0x68);
    W16(hb + 4, 0x10);
    W16(hb + 8, R16(o + 0x4A) - 0x10);
    if (RS16(hb + 8) < 0x11 && !(R16(o + 4) & 1)) {
        W16(hb + 4, 0); W16(hb + 8, 0);
        OR16(o + 2, 8);
        W16(o + 0x4A, 0);
    }
    if (RS16(PL + 0x16) < 1) W16(o + 0x48, 6);
    if (!FROZEN) {
        if (RS16(o + 6) == 5 && sword_cut(o)) {
            Obj_Spawn(2, RS32(HIT_POINT), RS32(HIT_POINT + 4), 0, 0);
            OR16(o + 2, 8);
            if (RS32(HIT_POINT + 4) < RS32(o + 0xC)) W32(HIT_POINT + 4, R32(o + 0xC));
            uint16_t cut = (uint16_t)((int16_t)((uint32_t)(RS32(HIT_POINT + 4) - RS32(o + 0x34)) >> 16) & 0xFFF0);
            if ((int16_t)cut < 0x10) cut = 0;
            W16(o + 0x48, 5); W16(o + 0x4E, 0xFFFF); W16(o + 0x4C, 0x40);
            W32(o + 0x34, R32(o + 0x34) + ((uint32_t)(cut >> 4) << 20) + ((uint32_t)(cut & 15) << 16));
            int16_t i = (int16_t)(cut >> 4), n = (int16_t)(RS16(o + 0x4A) / 16);
            if (i <= 0) i = 0;
            if (n <= 0) { n = 0; W16(o + 0x4A, 0); }
            SFX(0x122);
            while ((int)(uint16_t)i < n) {
                RopeSeg_Spawn(RS32(o + 8), RS32(o + 0x34), (uint16_t)i & 1, 0);
                i++;
                W32(o + 0x34, R32(o + 0x34) + 0x100000);
            }
            if (RS16(o + 0x4A) / 16 == 0) W16(o + 0x4A, 0);
            else W16(o + 0x4A, (uint32_t)((R16(o + 0x4A) & 0xF) + (cut & 0xFFF0)));
            W32(o + 0x30, R32(o + 8));
            W32(o + 0x34, R32(o + 0xC));
        } else {
            int chain = R16(o + 4) & 1;
            if (RS16(o + 0x48) == 0) {
                if (Map_GetAttr(RS32(o + 8), RS32(o + 0xC) + RS16(o + 0x4A) * 0x10000 + 0x100000, LAYER_DESC) != 0xFFFF) {
                    if (chain) W16(o + 0x4A, R16(o + 0x4A) & 0xFFF0);
                    W16(o + 0x48, 1); W16(o + 0x4C, 0); W16(o + 0x14, 0); W16(o + 0x52, 0);
                    if (chain) SFX(0x212);
                    goto draw;
                }
                W16(o + 0x4A, R16(o + 0x4A) + (chain ? R16(o + 0x52) : 1));
                if ((R16(o + 0x14) & 1) && RS16(o + 0x52) < 9) W16(o + 0x52, R16(o + 0x52) + 1);
                W16(o + 0x14, R16(o + 0x14) + 1);
            }
            if (RS16(o + 0x48) == 1) {
                if (RS16(o + 0x4C) == 8) { W16(o + 0x48, 2); W16(o + 0x14, 0); goto draw; }
                if ((R16(o + 0x14) & 3) == 2) W16(o + 0x4C, R16(o + 0x4C) + 1);
                W16(o + 0x14, R16(o + 0x14) + 1);
            }
            if (RS16(o + 0x48) == 2) {
                if (RS16(o + 0x4C) == 0) { W16(o + 0x48, 3); W16(o + 0x14, 0); SFX(0x210); goto draw; }
                if ((R16(o + 0x14) & 3) == 2) W16(o + 0x4C, R16(o + 0x4C) - 1);
                W16(o + 0x14, R16(o + 0x14) + 1);
            }
            if (RS16(o + 0x48) == 3) {
                if (RS16(o + 0x4A) <= RS16(o + 0x50)) { W16(o + 0x48, 4); W16(o + 0x4C, 0x30); W16(o + 0x14, 0); goto draw; }
                W16(o + 0x4A, R16(o + 0x4A) - (chain ? 2 : 1));
                W16(o + 0x14, R16(o + 0x14) + 1);
            }
            if (RS16(o + 0x48) == 4) {
                if (RS16(o + 0x4C) == 0) { W16(o + 0x48, 0); W16(o + 0x14, 0); SFX(0x210); goto draw; }
                W16(o + 0x4C, R16(o + 0x4C) - 1);
                W16(o + 0x14, R16(o + 0x14) + 1);
            }
            if (RS16(o + 0x48) == 5) {
                if (RS16(o + 0x4C) == 0) { W16(o + 0x48, 6); W16(o + 0x14, 0); AND16(o + 2, 0xFFF7); }
                else W16(o + 0x4C, R16(o + 0x4C) - 1);
            }
        }
    }
draw:
    /* 0x06055000: the pulley (anim 0x11) 16 px below the anchor */
    scratch_draw(0x11, (int16_t)((uint32_t)(RS32(o + 8) - CX) >> 16), (int16_t)(((uint32_t)(RS32(o + 0xC) - CY) >> 16) + 0x10),
                 0x0200, 0, 0x20, 0);
    W32(o + 0x34, (uint32_t)(RS32(o + 0x34) + (RS16(o + 0x4A) & 0xF) * 0x10000));
    W16(SPR + 2, (uint32_t)((RS32(o + 0x30) - CX) >> 16));
    W16(SPR + 4, (uint32_t)((RS32(o + 0x34) - CY) >> 16));
    f_spr(0x0200, 0, 0x20, 0, 0xFFFFFF80u);
    if (RS16(o) == -1) return;
    OR16(o + 0x58, 0x4000);
    Anim_Tick(o + 0x58);
    for (uint16_t i = 0; (int)i < RS16(o + 0x4A) / 16; i++) {
        if (CY - 0x400000 <= RS32(o + 0x34) && RS32(o + 0x34) <= CY + 0x1300000) {
            W16(SPR + 4, (uint32_t)((RS32(o + 0x34) - CY) >> 16));
            if (RS16(o + 0x48) == 1 || RS16(o + 0x48) == 2) {
                OR16(SPR + 0x10, 0x400);
                W16(SPR + 0x14, (i & 1) ? R16(o + 0x4C) : (uint32_t)-RS16(o + 0x4C));
            }
            Sprite_Draw(SPR);
        }
        W32(o + 0x34, R32(o + 0x34) + 0x100000);
    }
}

/* ---- 0x102A (0x06055A1C): platform hanging from the 0x102B rope with the same flags & 0x1F
 * (+0x48 = its slot): follows the rope's end (+0x34); once the rope is cut (+0x4E = -1) it
 * falls (gravity 0x5000 per frame) until the map stops it (sound 0x212). Sho standing on it
 * while the rope is idle pushes it 2 px down (and Sho with it); enemies are carried (flags &
 * 0xF outside 11..16). */
void ObjHandler_102A(void)
{
    uint32_t o = CUR;
    if (R16(o + 2) & 1) {
        OR16(o + 0x12, 0x4000);
        f_anim(o, DEFS41, SCR41, 4);
        AND16(o + 2, 0xFFFE);
        for (uint16_t s = 0; s < 0xC0; s++)
            if (R16(f_slot(s)) == 0x102B && (R16(f_slot(s) + 0x12) & 0x1F) == (R16(o + 0x12) & 0x1F)) W16(o + 0x48, s);
        W16(o + 0x14, 0);
    }
    W16(o + 0x16, 0x7FFF);
    AND16(o + 2, 0xFFF7);
    uint32_t r = f_slot(RS16(o + 0x48));
    if (RS16(r + 0x4E) != -1) W32(o + 0xC, R32(r + 0x34));
    int loose = RS16(r + 0x4E) == -1 && RS16(o + 0x4C) == 0;
    if (!(f_scr_dx(o) < 0x1100000 || loose)) return;
    if (!(f_scr_dy(o) < 0x2800000 || loose)) return;
    if (!FROZEN) {
        uint32_t y = R32(o + 0xC);
        W32(o + 0xC, y - 0x20000);
        int16_t c = (int16_t)Obj_PlayerContact(0);
        W32(o + 0xC, y);
        int32_t dy = 0;
        if (RS16(r + 0x4E) == -1) {
            if (Map_GetAttr(RS32(o + 8), RS32(o + 0xC) + 0x100000, LAYER_DESC) == 0xFFFF) {
                W32(o + 0xC, R32(o + 0xC) + (uint32_t)R16(o + 0x14) * 0x5000);
                W16(o + 0x14, R16(o + 0x14) + 1);
            } else {
                if (RS16(o + 0x4C) == 0) { W16(o + 0x4C, 1); SFX(0x212); }
                W32(o + 0xC, R32(o + 0xC) & 0xFFF00000u);
            }
        } else if (c == 2 && RS16(r + 0x48) == 0) {
            dy = (int32_t)0xFFFE0000;
            W32(PL + 0x3C, R32(PL + 0x3C) - 0x20000);
        }
        if ((R16(o + 0x12) & 0xF) < 0xB || (R16(o + 0x12) & 0xF) > 0x10) carry_enemies2(o, 0, dy);
    }
    W16(SPR + 2, (uint32_t)((RS32(o + 8) - CX) >> 16));
    W16(SPR + 4, (uint32_t)(((RS32(o + 0xC) - CY) >> 16) + 1));
    f_spr(0x0200, 0, 0x20, 0, 0xFFFFFF81u);
    if (RS16(o) != -1) Anim_Tick(o + 0x58);
}

/* ---- 0x1037 (0x06055E00): falling rope segment (anim 0xF): drifts randomly ((rand >> 12) & 7
 * x t x 1/256 px), falls 0x5000 x t - 2 px; after 16 frames it lands on the map (sound 0x212,
 * part 1) and keeps sliding off; freed at the end of its animation. */
void ObjHandler_1037(void)
{
    uint32_t o = CUR;
    if (R16(o + 2) & 1) {
        OR16(o + 0x12, 0x4000);
        f_anim(o, DEFS41, SCR41, 0xF);
        AND16(o + 2, 0xFFFE);
        OR16(o + 0x1C, 0x8000);
    }
    if (!FROZEN) {
        int16_t t = RS16(o + 0x4C);
        if (RS16(o + 0x5A) == 0) {
            int32_t r = (int32_t)(((uint32_t)sub_0602DDD2() >> 12) & 7);
            W32(o + 8, (uint32_t)(RS32(o + 8) + r * t * ((R16(o + 2) & 0x8000) ? -0x100 : 0x100)));
            W32(o + 0xC, (uint32_t)(RS32(o + 0xC) - (t * -0x5000 + 0x20000)));
            if (t > 0x10 && Map_GetAttr(RS32(o + 8), RS32(o + 0xC), LAYER_DESC) != 0xFFFF) {
                W16(o + 0x5A, 1); W16(o + 0x4C, 0);
                SFX(0x212);
                goto draw;
            }
        } else {
            W32(o + 8, (uint32_t)(RS32(o + 8) + t * ((R16(o + 2) & 0x8000) ? -0x100 : 0x100)));
            W32(o + 0xC, (uint32_t)(RS32(o + 0xC) - (t * -0x5000 + 0x40000)));
        }
        W16(o + 0x4A, (uint32_t)sub_0602DDD2() >> 12);
        W16(o + 0x4C, R16(o + 0x4C) + 1);
    }
draw:
    W16(SPR + 2, (uint32_t)((RS32(o + 8) - CX) >> 16));
    W16(SPR + 4, (uint32_t)((RS32(o + 0xC) - CY) >> 16));
    f_spr(0x0600, R16(o + 0x58) & 3, 0x20, 0, 0xFFFFFF82u);
    W16(SPR + 0x14, (R16(o + 0x4A) & 0x3F) << 2);
    if (RS16(o) != -1) Anim_Tick(o + 0x58);
    if (R16(o + 0x58) & 0x8000) W16(o, 0xFFFF);
}
