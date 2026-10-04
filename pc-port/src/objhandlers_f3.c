/* Stage 5-1 / 5-2 objects 0x103D-0x1044 (files STAGE5_1 / STAGE5_2, stage indices 8 / 9),
 * ported from SHINOBI.PPB. 5-1 sprite defs 0x060AAC02 / scripts 0x060D27C4, 5-2 0x060AB18C /
 * 0x060D2814. See objhandlers_f.h. */
#include "objhandlers_f_int.h"
#include "collision.h"

#define DEFS51  0x060AAC02u
#define SCR51   0x060D27C4u
#define DEFS52  0x060AB18Cu
#define SCR52   0x060D2814u
#define CART_SCROLL 0x060F2288u     /* u32: camera target X offset the cart asks for (0x060EA564 follows it) */
#define CART_DX     0x060F228Cu     /* u32: last cart speed given to Sho */

/* 0x06057E2C: track type under (x, y) from the map character (layer 0x060EA588):
 * 0x10 flat, 0x20 slope down (H-flipped tiles), 0x30 slope up, 0 none. */
static uint16_t Track_Attr(int32_t x, int32_t y)
{
    uint32_t v = Map_GetPN(x, y, LAYER_DESC);
    v = (((int32_t)(v & 0xFFFF) >> 1) & 0x1FF) | (v & 0x8000);
    if (v == 1) return 0x10;
    if (v == 0x75 || v == 0xD3 || v == 0xF3 || v == 0x132 || v == 0x133 || v == 0x152 || v == 0x153) return 0x30;
    if (v == 0x156 || v == 0x4001 || v == 0x4156) return 0x10;
    if (v == 0x8075 || v == 0x80D3 || v == 0x80F3 || v == 0x8132 || v == 0x8133 || v == 0x8152 || v == 0x8153) return 0x20;
    return 0;
}

/* 0x06057F10: the player's cart crossing x = 0x0D80 / 0x3840 / 0x5D80 (layer px) brings a cart
 * full of enemies (0x103D flags 2, 256 px behind it) with soldiers (type 1) / type 0x24. */
static int Cart_Ambush(uint32_t o)
{
    int16_t k = -1;
    if (RS32(o + 0x28) < 0x0D800000 && 0x0D7FFFFF < RS32(o + 8)) k = 0;
    if (RS32(o + 0x28) < 0x38400000 && 0x383FFFFF < RS32(o + 8)) k = 1;
    if (RS32(o + 0x28) < 0x5D800000 && 0x5D7FFFFF < RS32(o + 8)) k = 2;
    if (k == -1) return -1;
    int32_t x = RS32(o + 8), cx = x - 0x1000000, y = RS32(o + 0xC);
    if (k == 0) {
        Obj_Register(0x103D, cx, y, 2, 0xFFFF);
        y -= 0x180000;
        Obj_Register(1, x - 0x1280000, y, 0, 0xFFFF);
        Obj_Register(1, cx, y, 6, 0xFFFF);
        Obj_Register(1, x - 0xD80000, y, 0, 0xFFFF);
    }
    if (k == 1) {
        Obj_Register(0x103D, cx, y, 2, 0xFFFF);
        y -= 0x180000;
        Obj_Register(1, x - 0x1180000, y, 6, 0xFFFF);
        Obj_Register(1, x - 0xE80000, y, 6, 0xFFFF);
    }
    if (k == 2) {
        Obj_Register(0x103D, cx, y, 2, 0xFFFF);
        y -= 0x180000;
        Obj_Register(0x24, x - 0x1240000, y, 0, 0xFFFF);
        Obj_Register(0x24, x - 0x10C0000, y, 0, 0xFFFF);
        Obj_Register(0x24, x - 0xF40000, y, 0, 0xFFFF);
        return Obj_Register(0x24, x - 0xDC0000, y, 0, 0xFFFF);
    }
    return k;
}

/* 0x060580A4: spawn list 0x060AAD9C {x, type, dx, dy, flags, param, vy, splash} (8 x s16,
 * ended by -1): when the cart passes x, spawn type at the cart + (dx, dy) with +0x34 = vy,
 * plus a splash 0x1041 there if `splash` & 1. */
static void Cart_SpawnList(uint32_t o)
{
    for (uint32_t p = 0x060AAD9Cu; RS16(p) != -1; p += 16) {
        int32_t px = RS16(p) << 16;
        if (RS32(o + 0x28) - 0x04000000 < px && px <= RS32(o + 8) - 0x04000000) {
            int32_t x = RS32(o + 8) + RS16(p + 4) * 0x10000;
            int16_t s = (int16_t)Obj_Register(R16(p + 2), x, RS32(o + 0xC) + RS16(p + 6) * 0x10000, R16(p + 8), R16(p + 10));
            if (s != -1) W32(f_slot(s) + 0x34, (uint32_t)(RS16(p + 12) << 16));
            if (R16(p + 14) & 1) Obj_Register(0x1041, x, 0, 1, 0xFFFF);
        }
    }
}

/* ---- 0x103D (0x06057464): mine cart (5-1). Parts: 3 body, 2 tilted body (+0x4C >> 8),
 * 5 wheels (rolling), 1 wheel hubs, 4 top. flags & 3: 0 Sho's cart (spawns the ambushes and
 * the list above), 1 cart Sho can't start, 2 enemy cart (already running), 3 wreck (falls,
 * spins, splashes 0x1041 at the water line y = 0x3D0). Running (+4 & 1, started by Sho
 * landing on it, sound 0x211): follows the track (Track_Attr under the front wheel adjusts
 * the tilt, under the centre the speed +0x48: flat -> towards 3 px/frame (enemy cart: scripted
 * 4.5 then slower, camera offset 0x060F2288), down slope -0x222, up +0x666 per frame,
 * velocity +0x30/+0x34 = speed x (0.95, +-0.32)); off the track it falls (0x5000/frame, max 7)
 * and at x 0x2E00..0x3000 it becomes a wreck. Sho on it is carried (PL+0x38/+0x3C). Freed
 * below world bottom + 128 or 1024 px behind the camera. */
void ObjHandler_103D(void)
{
    uint32_t o = CUR;
    if ((R16(o + 0x12) & 3) != 3) {
        if (R16(o + 2) & 1) {
            f_anim(o, DEFS51, SCR51, 0);
            AND16(o + 2, 0xFFFE);
            W16(o + 0x14, 0); W16(o + 4, 0);
            W16(o + 0x4C, 0); W16(o + 0x4E, 0);
            W32(o + 0x48, 0); W32(o + 0x34, 0);
            W16(o + 0x50, 0x10);
            W16(o + 0x1C, 0x4000);
            if ((R16(o + 0x12) & 3) == 2) OR16(o + 4, 1);
        }
        if (!(f_scr_dx(o) < 0x1800001 || (R16(o + 4) & 1))) return;
        if (RS32(STAGE_C + 0xC) + 0x800000 < RS32(o + 0xC) || CX - RS32(o + 8) > 0x4000000) { W16(o, 0xFFFF); return; }
        if (!FROZEN) {
            W32(o + 0x28, R32(o + 8));
            W32(o + 0x2C, R32(o + 0xC));
            uint32_t sy = R32(o + 0xC);
            W32(o + 0xC, (uint32_t)(RS32(o + 0xC) + RS16(o + 0x4E) * -0x10000));
            int16_t c = (int16_t)Obj_PlayerContact(0);
            if (c == 2 && (R16(o + 0x12) & 3) != 1) {
                if (!(R16(o + 4) & 1)) SFX(0x211);
                OR16(o + 4, 1);
            }
            W32(o + 0xC, sy);
            int32_t dyc = 0;
            if (R16(o + 4) & 1) {
                int a = RS16(o + 0x4C) >> 8;
                int32_t cx = FixMul(Cos8(a), 0x200000), sn = FixMul(Sin8(a), 0x200000);
                uint16_t t = Track_Attr(RS32(o + 8) + cx, RS32(o + 0xC) - sn + 0xC0000);
                int16_t tl;
#define TILT(lo_cond, lo_add, hi_cond, hi_add) \
                tl = RS16(o + 0x4C); if (lo_cond) W16(o + 0x4C, (uint16_t)(tl + (lo_add))); \
                tl = RS16(o + 0x4C); if (hi_cond) W16(o + 0x4C, (uint16_t)(tl + (hi_add)));
                if (t == 0)    { TILT(tl < -0x800, 0x80, -0x800 < tl, -0x80) }
                if (t == 0x10) { TILT(tl < 0, 0x80, 0 < tl, -0x80) }
                if (t == 0x20) { TILT(tl < 0xD00, 0x80, 0xD00 < tl, -0x80) }
                if (t == 0x30) { TILT(-0xD00 < tl, -0x80, tl < -0xD00, 0x80) }
#undef TILT
                int16_t old = RS16(o + 0x4E);
                int8_t hb = (int8_t)(R16(o + 0x4C) >> 8);
                W16(o + 0x4E, (uint16_t)((RS16(o + 0x4C) >> 8) < 0 ? -hb : hb));
                dyc = (RS16(o + 0x4E) - old) * 0x10000;
                if (c == 2) W32(PL + 0x3C, R32(PL + 0x3C) + (uint32_t)dyc);
                uint16_t t2 = Track_Attr(RS32(o + 8), RS32(o + 0xC) + 0x80000);
                W16(o + 0x14, R16(o + 0x14) + 1);
                if (t2 == 0x10) {
                    int32_t d = 0;
                    if (RS32(o + 0x48) != 0x30000) {
                        int32_t e = 0x30000 - RS32(o + 0x48);
                        d = e;
                        if (f_abs(e) > 0xCCC) d = e < 0 ? -0xCCC : 0xCCC;
                    }
                    if ((R16(o + 0x12) & 3) == 2) {
                        if (R16(o + 0x14) < 0x46) { W32(o + 0x48, 0x48000); d = 0; W32(CART_SCROLL, 0xBE0000); }
                        int32_t xx = RS32(o + 8) - 0x04000000;
                        if (xx < 0x58000001 || xx > 0x5FFFFFFF) {
                            if (R16(o + 0x14) > 600) { W32(CART_SCROLL, 0x6E0000); d = -0x199; }
                        } else if (R16(o + 0x14) > 0xF0) { W32(CART_SCROLL, 0x6E0000); d = -0x199; }
                    }
                    W32(o + 0x48, (uint32_t)(RS32(o + 0x48) + d));
                }
                if (t2 == 0x20) W32(o + 0x48, (uint32_t)(RS32(o + 0x48) - 0x222));
                if (t2 == 0x30) W32(o + 0x48, (uint32_t)(RS32(o + 0x48) + 0x666));
                switch (t2 & 0xF0) {
                case 0:
                    W32(o + 0x34, (uint32_t)(RS32(o + 0x34) + 0x5000));
                    if (RS32(o + 0x34) > 0x70000) W32(o + 0x34, 0x70000);
                    if (0x2E000000 < RS32(o + 8) - 0x04000000 && RS32(o + 8) - 0x04000000 < 0x30000000) {
                        W16(o + 0x12, (R16(o + 0x12) & 0xFFFC) | 3);
                        W16(o + 0x14, 0);
                    }
                    break;
                case 0x10:
                    W32(o + 0x30, R32(o + 0x48));
                    if (RS32(o + 0x34) != 0) {
                        W32(o + 0x34, (uint32_t)-(RS32(o + 0x34) >> 2));
                        if (f_abs(RS32(o + 0x34)) < 0x10000) W32(o + 0x34, 0);
                    }
                    W32(o + 0xC, R32(o + 0xC) & 0xFFF80000u);
                    if (!(R16(o + 0x50) & 0xF0)) { SFX(0x20E); SFX(0x21A); }   /* landed */
                    break;
                case 0x20:
                    W32(o + 0x30, (uint32_t)FixMul(RS32(o + 0x48), 0xF2DC));
                    W32(o + 0x34, (uint32_t)-FixMul(RS32(o + 0x48), 0x50F4));
                    break;
                case 0x30:
                    W32(o + 0x30, (uint32_t)FixMul(RS32(o + 0x48), 0xF2DC));
                    W32(o + 0x34, (uint32_t)FixMul(RS32(o + 0x48), 0x50F4));
                    break;
                }
                W32(o + 8, R32(o + 8) + R32(o + 0x30));
                W32(o + 0xC, R32(o + 0xC) + R32(o + 0x34));
                if (c == 2) {
                    W32(PL + 0x3C, R32(PL + 0x3C) - R32(o + 0x34));
                    if (RS32(PL + 0x38) == 0) { W32(PL + 0x38, R32(o + 0x30)); W32(CART_DX, R32(o + 0x30)); }
                }
                W16(o + 0x50, t2);
            }
            sy = R32(o + 0xC);
            W32(o + 0xC, (uint32_t)(RS32(o + 0xC) + RS16(o + 0x4E) * -0x10000));
            ObjF_CarryEnemies(o, RS32(o + 0x30), -dyc - RS32(o + 0x34));
            W32(o + 0xC, sy);
            if ((R16(o + 0x12) & 3) == 0) { Cart_Ambush(o); Cart_SpawnList(o); }
        }
        int32_t dx = RS32(o + 8) - CX, dy = RS32(o + 0xC) - CY;
        f_spr(0x0200, 0, 0, 0, 0x80);
        W16(SPR + 2, (uint32_t)dx >> 16); W16(SPR + 4, (uint32_t)dy >> 16);
        W16(o + 0x5A, 3); Anim_Tick(o + 0x58);
        W16(SPR + 0x10, 0x0600); W16(SPR + 0x14, (uint32_t)(RS16(o + 0x4C) >> 8));
        W16(SPR + 2, (uint32_t)dx >> 16); W16(SPR + 4, (uint32_t)dy >> 16);
        W16(o + 0x5A, 2); Anim_Tick(o + 0x58);
        W16(SPR + 0x10, 0x0600);
        W16(SPR + 0x14, (uint32_t)(((R32(o + 8) >> 16) & 0x3F) * -4));
        int a = RS16(o + 0x4C) >> 8;
        int32_t cx = FixMul(Cos8(a), 0x200000), sn = FixMul(Sin8(a), 0x200000);
        W16(SPR + 2, (uint32_t)(dx + cx) >> 16); W16(SPR + 4, (uint32_t)(dy - sn) >> 16);
        W16(o + 0x5A, 5); Anim_Tick(o + 0x58);
        W16(SPR + 2, (uint32_t)(dx - cx) >> 16); W16(SPR + 4, (uint32_t)(dy + sn) >> 16);
        Anim_Tick(o + 0x58);
        W16(SPR + 0x10, 0x0200);
        W16(SPR + 2, (uint32_t)(dx + cx) >> 16); W16(SPR + 4, (uint32_t)(dy - sn) >> 16);
        W16(o + 0x5A, 1); Anim_Tick(o + 0x58);
        W16(SPR + 2, (uint32_t)(dx - cx) >> 16); W16(SPR + 4, (uint32_t)(dy + sn) >> 16);
        Anim_Tick(o + 0x58);
        W16(SPR + 0x10, 0x0200);
        W16(SPR + 4, (uint32_t)(dy + (RS16(o + 0x4E) + 1) * -0x10000) >> 16);
        W16(SPR + 2, (uint32_t)dx >> 16);
        W16(o + 0x5A, 4); Anim_Tick(o + 0x58);
        return;
    }
    /* wreck */
    if (RS32(STAGE_C + 0xC) + 0x800000 < RS32(o + 0xC) || CX - RS32(o + 8) > 0x4000000) { W16(o, 0xFFFF); return; }
    if (!FROZEN) {
        W32(o + 0x28, R32(o + 8));
        W32(o + 0x2C, R32(o + 0xC));
        W32(o + 0x34, (uint32_t)(RS32(o + 0x34) + 0x5000));
        if (RS32(o + 0x34) > 0x70000) W32(o + 0x34, 0x70000);
        W32(o + 0xC, R32(o + 0xC) + R32(o + 0x34));
        W32(o + 8, R32(o + 8) + R32(o + 0x30));
        W16(o + 0x14, R16(o + 0x14) - 1);       /* (the original's clamp at -64 never triggers) */
        if (RS32(o + 0x2C) - 0x04000000 < 0x3D00000 && 0x3CFFFFF < RS32(o + 0xC) - 0x04000000) {
            Obj_Register(0x1041, RS32(o + 8) + 0x200000, 0, 1, 0xFFFF);
            Obj_Register(0x1041, RS32(o + 8), 0, 1, 0xFFFF);
        }
    }
    f_spr(0x0600, 0, 0, 0, 0x80);
    W16(SPR + 2, (uint32_t)((RS32(o + 8) - CX) >> 16));
    W16(SPR + 4, (uint32_t)(((RS32(o + 0xC) - CY) >> 16) + 8));
    W16(SPR + 0x14, R16(o + 0x14));
    W16(o + 0x5A, 0);
    Anim_Tick(o + 0x58);
}

/* ---- 0x103E (0x06058238): explosive barrels on the track (anim 1): Sho's running cart
 * (0x103D flags 0) reaching it (0..80 px) -> 5 explosion projectiles (type 5) and both the
 * barrels and the cart are removed. */
void ObjHandler_103E(void)
{
    uint32_t o = CUR;
    if (R16(o + 2) & 1) {
        f_anim(o, DEFS51, SCR51, 1);
        AND16(o + 2, 0xFFFE);
        W16(o + 0x14, 0); W16(o + 0x1C, 0x8000);
    }
    if (f_scr_dx(o) >= 0x1800001) return;
    if (!FROZEN) {
        for (int16_t s = 0; s < 0xC0; s++) {
            uint32_t c = f_slot(s);
            if (RS16(c) == 0x103D && !(R16(c + 0x12) & 3) && (R16(c + 4) & 1) &&
                RS32(c + 8) < RS32(o + 8) && RS32(o + 8) - RS32(c + 8) < 0x500000) {
                int32_t x = RS32(o + 8), y = RS32(o + 0xC);
                Projectile_Spawn(5, x, y + 0x180000, 0, 0);
                Projectile_Spawn(5, x, y - 0x180000, 0, 0);
                Projectile_Spawn(5, x - 0x200000, y, 0, 0);
                Projectile_Spawn(5, x - 0x400000, y + 0x180000, 0, 0);
                Projectile_Spawn(5, x - 0x500000, y - 0x180000, 0, 0);
                W16(o, 0xFFFF);
                W16(c, 0xFFFF);
                return;
            }
        }
    }
    f_spr_cam(o);
    f_spr(0x0200, 0, 0, 0, 0xFFFFFF80u);
    Anim_Tick(o + 0x58);
}

/* ---- 0x103F (0x06058444): 5-1 controller: drops the stage's items (table 0x060AB0B8 {kind,
 * x, y}), keeps the cart's speed on Sho while he jumps (0x060586EA: PL+0x38 = 0x060F228C;
 * also moves the enemy shurikens with it), moves the camera offset 0x060EA564 1 px/frame
 * towards 0x060F2288, and shows the warning sign (anim 2 at screen 256,112, 128 frames) when
 * Sho passes one of the x in 0x060AB0AE. */
static void Cart_CarrySho(void)                         /* 0x060586EA */
{
    if (!(R16(PL + 2) & 0x4000)) { if (R16(PL + 2) & 0x10) W32(CART_DX, 0); }
    else if (RS32(PL + 0x38) == 0) W32(PL + 0x38, R32(CART_DX));
    if (FROZEN) return;
    for (uint32_t p = PROJECTILES; p < PROJECTILES + 0x40u * 0x40u; p += 0x40)
        if (R16(p) == 1) W32(p + 8, R32(p + 8) + R32(PL + 0x38));
}
void ObjHandler_103F(void)
{
    uint32_t o = CUR;
    if (R16(o + 2) & 1) {
        f_anim(o, DEFS51, SCR51, 2);
        AND16(o + 2, 0xFFFE);
        W16(o + 0x14, 0); W16(o + 0x1C, 0x8000);
        for (uint32_t p = 0x060AB0B8u; RS16(p) != -1; p += 6)        /* 0x060586B0 */
            Item_Drop(RS16(p), RS16(p + 2) * 0x10000 + 0x04000000, RS16(p + 4) * 0x10000 + 0x04000000);
        W32(CART_DX, 0);
        W32(CART_SCROLL, 0x6E0000);
    }
    Cart_CarrySho();
    if (!FROZEN) {
        int16_t t = (int16_t)(R32(CART_SCROLL) >> 16);
        if (t < (int16_t)(R32(0x060EA564u) >> 16)) W32(0x060EA564u, R32(0x060EA564u) - 0x10000);
        if ((int16_t)(R32(0x060EA564u) >> 16) < t) W32(0x060EA564u, R32(0x060EA564u) + 0x10000);
        if (RS16(o + 0x14) == 0)
            for (uint32_t p = 0x060AB0AEu; RS16(p) != -1; p += 2) {
                int32_t x = RS16(p) << 16;
                if (RS32(PL + 0x28) - 0x04000000 < x && x <= PX - 0x04000000) W16(o + 0x14, 1);
            }
        if (RS16(o + 0x14) != 0) {
            uint16_t v = R16(o + 0x14);
            W16(o + 0x14, v + 1);
            if (v > 0x80) W16(o + 0x14, 0);
        }
    }
    if (RS16(o + 0x14) == 0) return;
    W16(SPR + 2, 0x100); W16(SPR + 4, 0x70);
    f_spr(0x0200, 0, 0x20, 0, 0x82);
    Anim_Tick(o + 0x58);
}

/* ---- 0x1040 (0x060570FC): water surface (anim 3, bank 0xF) drawn as 5 tiles of 128 px across
 * the screen once the camera is below y 0x2E0, stretched vertically by a cosine of the frame
 * counter, every other tile shifted 64 px every 64 frames, blinking priority. */
void ObjHandler_1040(void)
{
    uint32_t o = CUR;
    if (R16(o + 2) & 1) {
        f_anim(o, DEFS51, SCR51, 3);
        AND16(o + 2, 0xFFFE);
        W16(o + 0x14, 0); W16(o + 0x1C, 0x8000);
    }
    if (CY - 0x04000000 < 0x2E00000) return;
    W32(o + 8, ((R32(CAM_X) + 0xFB800000u) & 0xFF800000u) + 0x04000000u);
    for (int i = 0; i < 5; i++) {
        W16(SPR + 0x0E, 0); W16(SPR + 0x10, 0x0A00);
        W32(SPR + 0x20, (uint32_t)(FixMul(Cos8((FRAMES & 0x7F) << 1), 0xC000) + 0x10000));
        W16(SPR + 0x0A, 0xF);
        int32_t off = (int32_t)(((FRAMES << 1) + 0x80) & 0x100) / 4;
        W16(SPR + 2, (uint32_t)((off * 0x10000 + RS32(o + 8) - CX) >> 16));
        W16(SPR + 4, (uint32_t)((RS32(o + 0xC) - CY) >> 16));
        W16(SPR + 0x0C, 1); W16(SPR + 0x14, 0);
        W32(SPR + 0x18, (VBL & 1) ? 0xFFFFFF00u : 0xFF);
        Anim_Tick(o + 0x58);
        W32(o + 8, R32(o + 8) + 0x800000);
    }
}

/* ---- 0x1041 (0x06057298): water splash (anim 4) at y 0x7FE (world): flags & 1 one-shot (spawned
 * by carts / the spawn list), else follows Sho crossing the surface. */
void ObjHandler_1041(void)
{
    uint32_t o = CUR;
    if (R16(o + 2) & 1) {
        W16(o + 6, 4);
        AND16(o + 2, 0xFFFE);
        W16(o + 0x14, 0); W16(o + 0x1C, 0x8000);
        if ((R16(o + 0x12) & 1) == 1) {
            W32(o + 0xC, 0x07FE0000);
            f_anim_cur(o, DEFS51, SCR51);
            W16(o + 0x14, 1);
        }
    }
    if (!FROZEN) {
        if (R16(o + 0x14) == 0 &&
            ((0x07FE0000 < PY && RS32(PL + 0x2C) < 0x07FE0000) || (PY < 0x07FE0000 && 0x07FE0000 < RS32(PL + 0x2C)))) {
            W32(o + 8, (uint32_t)PX);
            W32(o + 0xC, 0x07FE0000);
            f_anim_cur(o, DEFS51, SCR51);
            W16(o + 0x14, 1);
        }
        if (R16(o + 0x58) & 0x8000) {
            W16(o + 0x14, 0);
            if ((R16(o + 0x12) & 1) == 1) { W16(o, 0xFFFF); return; }
        }
    }
    if (R16(o + 0x14) == 0) return;
    f_spr_cam(o);
    f_spr(0x0200, 0, 0x1F, 1, (VBL & 1) ? 0xFFFFFF00u : 0xFF);
    Anim_Tick(o + 0x58);
}

/* ---- 0x1042 (0x06056F34): far background element (anim 5, bank 0x2F) scrolled with layer 2
 * (rates 0x060EA600/0x060EA602, 8.8, wrap 2048 px); flags & 1: also cycles palette 0x2F0
 * (16 colours) between 0x060D2714 and 0x060D2734. */
void ObjHandler_1042(void)
{
    uint32_t o = CUR;
    if (R16(o + 2) & 1) {
        f_anim(o, DEFS51, SCR51, 5);
        AND16(o + 2, 0xFFFE);
        W16(o + 0x14, 0); W16(o + 0x1C, 0x8000);
    }
    int32_t bx = (int32_t)((uint32_t)FixMul(CX - 0x04000000, (int32_t)((uint32_t)R16(0x060EA600u) << 8)) & 0x7FFFFFF) + 0x04000000;
    int32_t by = (int32_t)((uint32_t)FixMul(CY - 0x04000000, (int32_t)((uint32_t)R16(0x060EA602u) << 8)) & 0x7FFFFFF) + 0x04000000;
    if (!FROZEN && (R16(o + 0x12) & 1)) {
        if (RS16(o + 0x14) == 0) { if (Pal_StepTowards(0x2F0, 0x10, 0x060D2714u, 1)) W16(o + 0x14, 1); }
        else if (Pal_StepTowards(0x2F0, 0x10, 0x060D2734u, 1)) W16(o + 0x14, 0);
    }
    int32_t d = RS32(o + 8) - bx + (int32_t)0xFF600000;
    if (d < 0) d = bx - RS32(o + 8) + 0xA00000;
    if (d >= 0x1000001) return;
    W16(SPR + 2, (uint32_t)((RS32(o + 8) - bx) >> 16));
    W16(SPR + 4, (uint32_t)((RS32(o + 0xC) - by) >> 16));
    f_spr(0x0200, 0, 0x2F, 0xFFFF, 0xFFFFFF7Fu);
    Anim_Tick(o + 0x58);
}

/* ---- 0x1043 (0x06058774): 5-2 elevator (anim 0, cage anim 2 in the global state 0x060F2290).
 * +0x48 (set by the stage hook 0x0606DDBC): 0 stopped, 1 descending (accelerating to 16 px/frame,
 * Sho follows via PL+0x3C), 2 rising 0.75 px/frame (checkpoint 1 starts so: 0x0606E184). Stops at the
 * bottom y 0x2F80 (0x0606E154, sound 0x21A) and at the top y 0x04C0 (0x0606E1B8). While moving
 * up it spawns the shaft's enemies from table 0x060AB318 (0x18 bytes: trigger y, skip y, x, y,
 * type, flags, param; 0x67 entries), keeping at most 6 (0x060F22B0), carried on it and freed
 * 88 px below the world bottom; with +4 & 1 the world top/bottom (C+4/C+0xC) track it. Camera X
 * is snapped to whole pixels. Sho locked (+4 |= 0x10) at start / at the top. */
void ObjHandler_1043(void)
{
    uint32_t o = CUR;
    if (R16(o + 2) & 1) {
        W16(o + 0x1C, 0x8000);
        f_anim(o, DEFS52, SCR52, 0);
        Anim_Start(0x060F2290u, DEFS52, SCR52, 0x10, 2);
        AND16(o + 2, 0xFFFE);
        W16(o + 0x48, 0); W16(o + 4, 0);
        W32(o + 0x30, 0); W32(o + 0x34, 0);
        OR16(PL + 4, 0x10);
        for (int i = 0; i < 6; i++) W16(0x060F22B0u + 2u * (uint32_t)i, 0xFFFF);
        W16(0x060F22BCu, 0);
        if (RS16(0x060EC658u) == 1) {
            SFX(0x211);
            W16(o + 0x48, 2);
            AND16(PL + 4, 0xFBEF);
            Hook_0606E184();
            W32(o + 0xC, 0x2F800000);
            OR16(o + 4, 1);
        }
    }
    W32(CAM_X, R32(CAM_X) & 0xFFFF0000u);
    if (!FROZEN) {
        int16_t s = RS16(o + 0x48);
        if (s == 0) W32(o + 0x34, 0);
        else if (s == 1) {
            W32(o + 0x34, (uint32_t)(RS32(o + 0x34) - 0x5000));
            if (RS32(o + 0x34) < -0x100000) W32(o + 0x34, 0xFFF00000u);
            W32(PL + 0x3C, (uint32_t)-(RS32(PL + 0x34) - RS32(o + 0x34)));
        } else if (s == 2) W32(o + 0x34, 0xC000);
        W32(o + 0xC, R32(o + 0xC) - R32(o + 0x34));
        if (RS32(o + 0xC) > 0x2F800000) {
            Hook_0606E154();
            W32(o + 0xC, 0x2F800000);
            W16(o + 0x48, 0);
            OR16(o + 4, 1);
            SFX(0x21A);
        }
        if (RS32(o + 0xC) < 0x04C00000) {
            Hook_0606E1B8();
            OR16(PL + 4, 0x10);
            W32(o + 0xC, 0x04C00000);
            W16(o + 0x48, 0);
            SFX(0x21A);
        }
        if (R16(o + 4) & 1) {
            int32_t y = RS32(o + 0xC), t = y - 0xC00000;
            if (f_abs(RS32(STAGE_C + 4) - t) > 0xA0000) W32(STAGE_C + 4, (uint32_t)(y - 0xCA0000));
            if (f_abs(t - RS32(STAGE_C + 4)) < 0x10001) { W32(STAGE_C + 0xC, (uint32_t)(y + 0x200000)); W32(STAGE_C + 4, (uint32_t)t); }
            else if (RS32(STAGE_C + 4) < t) W32(STAGE_C + 4, R32(STAGE_C + 4) + 0x10000);
            else W32(STAGE_C + 4, R32(STAGE_C + 4) - 0x10000);
        }
        if (RS16(o + 0x48) == 2) {
            for (;;) {
                uint32_t e = 0x060AB318u + (uint32_t)(int16_t)(R16(0x060F22BCu) * 0x18);
                if (R16(0x060F22BCu) > 0x66 || RS32(e) + 0x04000000 < RS32(o + 0xC)) goto contact;
                int done = 0;
                for (int i = 0; i < 6; i++) {
                    uint32_t l = 0x060F22B0u + 2u * (uint32_t)i;
                    if (RS16(l) == -1) {
                        W16(l, (uint32_t)Obj_Register(R16(e + 0x10), RS32(e + 8) + 0x04000000, RS32(e + 0xC) + 0x04000000,
                                                      R16(e + 0x12), R16(e + 0x14)));
                        W16(0x060F22BCu, R16(0x060F22BCu) + 1);
                        done = 1;
                        break;
                    }
                }
                if (!done) break;
            }
            if (RS32(0x060AB31Cu + (uint32_t)(int16_t)(R16(0x060F22BCu) * 0x18)) + 0x04000000 <= RS32(o + 0xC))
                W16(0x060F22BCu, R16(0x060F22BCu) + 1);
        }
contact:
        if ((int16_t)Obj_PlayerContact(0) == 2) W32(PL + 0x3C, R32(PL + 0x3C) - R32(o + 0x34));
        for (int i = 0; i < 6; i++) {
            uint32_t l = 0x060F22B0u + 2u * (uint32_t)i;
            if (RS16(l) == -1) continue;
            uint32_t e = f_slot(RS16(l));
            if (RS16(e) == -1) W16(l, 0xFFFF);
            else if (RS32(STAGE_C + 0xC) + 0x580000 < RS32(e + 0xC)) { W16(e, 0xFFFF); W16(l, 0xFFFF); }
            else Obj_CarryContact(o, e, 2, 8);
        }
    }
    f_spr_cam(o);
    f_spr(0x0200, 0, 0, 0, 0x81);
    Anim_Tick(o + 0x58);
    W32(SPR + 0x18, 0xFFFFFF80u);
    Anim_Tick(0x060F2290u);
}

/* ---- 0x1044 (0x06058D38): breakable crate (anim 1): Sho's sword -> sound 0x115, spark,
 * 6 explosion parts (table 0x060ABCC0), item +0x18 32 px lower, gone. */
void ObjHandler_1044(void)
{
    uint32_t o = CUR;
    if (R16(o + 2) & 1) {
        W16(o + 0x1C, 0x8000);
        f_anim(o, DEFS52, SCR52, 1);
        AND16(o + 2, 0xFFFE);
        W16(o + 4, 0); W32(o + 0x30, 0); W32(o + 0x34, 0);
    }
    if (f_scr_dx(o) >= 0x1000000 || f_scr_dy(o) >= 0x1000000) return;
    if (FROZEN || (int16_t)Hitbox_Test(o, 0, PL, 2) == -1) {
        f_spr_cam(o);
        f_spr(0x0200, 0, 0, 0, 0xFFFFFF80u);
        Anim_Tick(o + 0x58);
        return;
    }
    SFX(0x115);
    Obj_Spawn(2, RS32(HIT_POINT), RS32(HIT_POINT + 4), 0, 0);
    OR16(o + 2, 8);
    for (uint32_t i = 0; i < 6; i++) {
        uint32_t t = 0x060ABCC0u + i * 8;
        Obj_Spawn(6, RS32(o + 8) + RS16(t) * 0x10000, RS32(o + 0xC) + RS16(t + 2) * 0x10000, RS16(t + 6), RS16(t + 4));
    }
    if (RS16(o + 0x18) != -1) Item_Drop(RS16(o + 0x18), RS32(o + 8), RS32(o + 0xC) + 0x200000);
    W16(o, 0xFFFF);
}
