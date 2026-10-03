#include "collision.h"
#include "objhandlers.h"
#include "ram.h"

static uint32_t layer_desc(int16_t layer) { return LAYER_DESC + (uint32_t)(((uint8_t)layer * 0x30) & 0xFF); }

/* 0x06046F12: pattern-name word under (x,y) in layer space (16.16, 16px tiles,
 * 32x32-tile pages of 0x800 bytes, page layout {u8 w, u8 h, u8 ids[]}). */
uint16_t Map_GetPN(int32_t x, int32_t y, uint32_t desc)
{
    uint32_t cx = ((uint32_t)(x + (int32_t)0xFC000000) >> 20) & 0x7FF;
    uint32_t cy = ((uint32_t)(y + (int32_t)0xFC000000) >> 20) & 0x7FF;
    W32(0x060EA654u, 0);
    uint32_t lay = R32(desc + 0x20);
    if (!lay) return 0;
    int w = (int8_t)Ram_Ptr(lay)[0], h = (int8_t)Ram_Ptr(lay)[1];
    if ((int)cx < w << 5 && (int)cy < h << 5) {
        uint8_t pid = Ram_Ptr(lay)[2 + (uint16_t)((cx >> 5) + w * (cy >> 5))];
        if (pid != 0xFF) {
            uint32_t a = R32(desc + 0x1C) + (uint32_t)pid * 0x800 + (cy & 31) * 0x40 + (cx & 31) * 2;
            W32(0x060EA654u, a);
            return R16(a);
        }
    }
    return 0;
}

/* 0x06047066: is the 8x8 quadrant under (x,y) solid for this shape? */
static uint16_t quadrant_solid(uint32_t x, uint32_t y, uint16_t shape)
{
    return R16(0x060A8AE4u + 2u * shape) &
           R16(0x060A8B04u + 2u * (((x & 0x80000u) >> 19) | ((y & 0x80000u) >> 18)));
}

/* 0x06046FE2: collision attribute (u16) under (x,y), 0xFFFF if empty. */
uint16_t Map_GetAttr(int32_t x, int32_t y, uint32_t desc)
{
    if (R32(desc + 0x28)) {
        x = x + RS32(desc + 0x10) + (int32_t)0xFC000000;
        y = y + (int32_t)0xFC000000 + RS32(desc + 0x14);
        uint16_t pn = Map_GetPN(x, y, desc);
        int idx = (int)(0x0FFF & pn) >> 1;
        if (idx < 0x200) {
            uint16_t a = R16(R32(desc + 0x28) + (uint32_t)idx * 2);
            if ((a & 0xF) && quadrant_solid((uint32_t)x, (uint32_t)y, a & 0xF)) return a;
        }
    }
    return 0xFFFF;
}

/* 0x06045B88: attribute -> side flags (1 ceiling, 2 floor, 4 wall-left, 8 wall-right,
 * 0x10/0x20 surface specials). Type table @0x060CA700. */
uint16_t Coll_AttrFlags(uint16_t attr)
{
    if (attr == 0xFFFF) return 0;
    return R16(0x060CA700u + ((attr >> 4) & 7) * 2u);
}

/* 0x06045670: probe the 4 edges of a box (x = centre, y = feet, w in 8px/2
 * units, h in 8px units). skip bits: 1 top, 2 bottom, 4 left, 8 right. */
uint16_t Coll_ProbeBox(int32_t x, int32_t y, int w, int h, int16_t layer, uint16_t skip)
{
    if (layer < 0) return 0;
    uint32_t d = layer_desc(layer);
    uint32_t hw = ((uint32_t)(w & 0xFFFF) + 1) & 0xFFFE;
    int32_t xl = x - (int32_t)hw * 0x40000;
    int32_t yt = y - (int32_t)(uint16_t)h * 0x80000;
    uint32_t cols = (hw & 0xFFFF) + 1, n = cols;
    uint16_t bottom = 0, top = 0, right = 0, left = 0;
    for (int32_t xi = xl; (int)n > 0; n--, xi += 0x7FFFF) {
        if (!(skip & 2)) bottom |= Coll_AttrFlags(Map_GetAttr(xi, y, d));
        if (!(skip & 1)) {
            uint16_t f = Coll_AttrFlags(Map_GetAttr(xi, yt, d));
            if (n == 1 || n == cols) f &= 0xFFEF;              /* corners ignore 0x10 */
            top |= f;
        }
    }
    for (uint32_t m = (uint16_t)(h + 1); (int)m > 0; m--, yt += 0x7FFFF) {
        if (!(skip & 8)) right |= Coll_AttrFlags(Map_GetAttr((int32_t)hw * 0x40000 + x, yt, d));
        if (!(skip & 4)) left  |= Coll_AttrFlags(Map_GetAttr(xl, yt, d));
    }
    return (uint16_t)((top & 0xFFF1) | (bottom & 0xFFE2) | (right & 0xFFE8) | (left & 0xFFE4));
}

/* 0x0604562C: snap the object out of a wall to the 8px grid. */
void Coll_PushOut(uint32_t obj, uint16_t dir)
{
    uint32_t x = R32(obj + 8), y = R32(obj + 0xC);
    if (dir & 2) y = (y & 0xFFF80000u) - 1;
    if (dir & 1) y = 0x00080000u + (y & 0xFFF80000u);
    if (dir & 8) x = (x & 0xFFF80000u) - 1;
    if (dir & 4) x = 0x00080000u + (x & 0xFFF80000u);
    W32(obj + 8, x);
    W32(obj + 0xC, y);
}

/* 0x060457EC: flags of the tile row just below the feet, w probes 8px apart. */
uint16_t Coll_ProbeFloor(int32_t x, int32_t y, int w, int16_t layer)
{
    if (layer < 0) return 0;
    uint32_t d = layer_desc(layer);
    int32_t yy = (int32_t)(((uint32_t)y & 0xFFF80000u) + 0x80000);
    uint16_t f = 0;
    x += ((w & 0xFFFF) >> 1) * -0x80000;
    for (int i = 0; i < (w & 0xFFFF); i++, x += 0x80000) f |= Coll_AttrFlags(Map_GetAttr(x, yy, d));
    return f;
}

/* 0x0604588E: how many of n probes (8px apart) find a floor under the feet. */
int Coll_CountFloor(int32_t x, int32_t y, int n, int16_t layer)
{
    if (layer < 0) return 0;
    uint32_t d = layer_desc(layer);
    int32_t yy = (int32_t)(((uint32_t)y & 0xFFF80000u) + 0x80000);
    int c = 0;
    x += ((n & 0xFFFF) >> 1) * -0x80000;
    for (int i = 0; i < (n & 0xFFFF); i++, x += 0x80000)
        if (Coll_AttrFlags(Map_GetAttr(x, yy, d)) & 2) c++;
    return c;
}

/* 0x06045340: resolve the object against the map; updates flags10, the
 * airborne flag (flags2 & 0x4000), landing flag 0x10 and the air counter. */
void Obj_CollideMap(uint32_t o, int w, int h)
{
    int16_t layer = RS16(G_COLL_LAYER);
    W16(o + 0x24, R16(o + 0x10));
    W16(o + 0x10, R16(o + 0x10) & 0x3D00);
    uint16_t hit = Coll_ProbeBox(RS32(o + 8), RS32(o + 0xC), w, h, layer, 0);
    uint16_t allow = 0xF;
    if (R16(o + 2) & 0x4000) {
        uint16_t r = Coll_ProbeBox(RS32(o + 8), RS32(o + 0xC) + (int32_t)0xFFF80000, w, h, layer, 0);
        if ((r & 0xF) == 2) allow = 0xD;
        int32_t off = RS32(layer_desc(layer) + 0x14);
        if ((int32_t)((uint32_t)(RS32(o + 0xC) + off + (int32_t)0xFC000000) & 0xFFF80000u) <=
            (int32_t)((uint32_t)(RS32(o + 0x2C) + off + (int32_t)0xFC000000) & 0xFFF80000u))
            allow &= 0xFFFD;                                    /* not falling into a new row */
    }
    if ((R16(o + 2) & 0x20) && !(R16(o + 2) & 0x4000) && (hit & 0xF) == 2)
        allow &= 0xFFFD;                                        /* dropping through */
    uint16_t used = 0;
    if (hit & allow) {
        uint32_t x0 = R32(o + 8), y0 = R32(o + 0xC);
        uint32_t tbl = (R16(o + 2) & 0x4000) ? 0x060A8A74u : 0x060A8A64u;
        int ok = 0;
        for (int i = 0; i < 8; i++, tbl += 2) {
            used = R16(tbl) & allow;
            Coll_PushOut(o, used);
            uint16_t r = Coll_ProbeBox(RS32(o + 8), RS32(o + 0xC), w, h, layer, (uint16_t)~allow);
            if ((r & 0xF) == 0) { ok = 1; break; }
            W32(o + 8, x0); W32(o + 0xC, y0);
        }
        if (!ok) {                                              /* stuck: revert */
            W16(o + 0x10, R16(o + 0x10) | 0x0400);
            used = 0xF;
            W32(o + 8, R32(o + 0x28));
            W32(o + 0xC, R32(o + 0x2C));
        }
    }
    W16(o + 0x10, R16(o + 0x10) | (hit & 0xFFF0) | used);
    if (!(R16(o + 2) & 0x0800)) {
        if ((R16(o + 0x10) & 0x0400) && !(R16(o + 2) & 0x4408))
            W16(o + 0x10, R16(o + 0x10) | 0x8000);              /* squeezed: force crouch */
    } else if (Coll_ProbeBox(RS32(o + 8), RS32(o + 0xC), w, 9, layer, 0xFFFE) & 1) {
        W16(o + 0x10, R16(o + 0x10) | 0x8000);                  /* can't stand up */
    }
    if ((Coll_ProbeFloor(RS32(o + 8), RS32(o + 0xC), w, layer) & 3) == 2)
        W16(o + 0x10, R16(o + 0x10) | 0x4000);
    if ((R16(o + 2) & 0x4000) && RS32(o + 0x34) > 0 && (R16(o + 0x10) & 1))
        W32(o + 0x34, 0xFFFFFFFFu);                             /* bonk the ceiling */
    W16(o + 2, R16(o + 2) & 0xFFEF);
    if (!(R16(o + 0x10) & 0x2002) || RS32(o + 0x34) >= 0) {
        W16(o + 2, R16(o + 2) | 0x4000);                        /* airborne */
        W16(o + 0x1A, R16(o + 0x1A) + 1);
    } else {
        W32(o + 0x34, 0);
        if (R16(o + 0x1A) > 0x78) W16(o + 2, R16(o + 2) & 0xFDFF);
        W16(o + 0x1A, 0);
        if (R16(o + 2) & 0x4000) {                              /* just landed */
            W16(o + 2, R16(o + 2) & 0xEFFF);
            W16(o + 2, R16(o + 2) | 0x10);
        }
        W16(o + 2, R16(o + 2) & 0xAF7F);
    }
}

/* 0x060451C8: keep the player inside the screen margins (0x060EA618+0x10..0x16). */
void Player_ClampToScreen(void)
{
    const uint32_t c = 0x060EA618u, P = 0x060EA2B4u;
    int32_t d;
    if (RS16(c + 0x16) != 0x7FFF && (d = RS16(c + 0x16) * 0x10000 + RS32(0x060252A0u) - RS32(P + 8)) > 0)
        Obj_Push(P, d, 0);
    if (RS16(c + 0x14) != 0x7FFF && (d = RS16(c + 0x14) * 0x10000 + RS32(0x060252A0u) - RS32(P + 8)) < 0)
        Obj_Push(P, d, 0);
    if (RS16(c + 0x12) != 0x7FFF && (d = RS32(P + 0xC) - RS32(0x060252A4u) + RS16(c + 0x12) * -0x10000) > 0)
        Obj_Push(P, 0, d);
    if (RS16(c + 0x10) != 0x7FFF && (d = RS32(P + 0xC) - RS32(0x060252A4u) + RS16(c + 0x10) * -0x10000) < 0) {
        Obj_Push(P, 0, d);
        W32(P + 0x34, 0xFFFFFFFFu);
    }
}
