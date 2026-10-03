/* Stage "system" objects 0x1002 (item crate) and 0x1003 (trap door), ported from
 * SHINOBI.PPB. Style and field conventions: see objhandlers.c / objhandlers.h. */
#include "objhandlers.h"
#include "objects.h"
#include "enemy.h"
#include "fx.h"
#include "anim.h"
#include "sprite.h"
#include "collision.h"
#include "fixmath.h"
#include "player_ext.h"
#include "ram.h"

#define CUR       R32(OBJ_CUR)
#define PL        0x060EA2B4u           /* player object */
#define CAM_X     0x060252A0u
#define CAM_Y     0x060252A4u
#define FROZEN    (R16(0x0602529Eu) & 0x8000)
#define SPR       0x060250ACu
#define MAP_CELL  0x060EA654u           /* u32: address of the map word found by Map_GetPN (0 = none) */

/* Descriptor of the object collision layer (0x060EA64A, low byte) — same as the
 * static layer_desc() of collision.c, re-implemented here. */
static uint32_t coll_desc(void)
{
    return LAYER_DESC + (((uint32_t)(uint8_t)R16(G_COLL_LAYER) * 0x30) & 0xFF);
}

/* *(u16 *)*(0x060EA654) = v. When Map_GetPN found no cell the pointer is 0 and the
 * Saturn write lands in the BIOS ROM (ignored); on PC it would hit emulated RAM. */
static void cell_store(uint16_t v)
{
    uint32_t a = R32(MAP_CELL);
    if (a) W16(a, v);
}

/* 0x06046B14: re-send the map cell under (x,y) to the VDP2 name table of the layer
 * (desc +0x24 table base, +0x2C char base), 2-word pattern-name format, 32x32 cells.
 * PC: the renderer draws straight from the map pages and the name table is never
 * set up (desc +0x24 = 0), so the write is skipped then. */
static void Map_PutCell(int32_t x, int32_t y, uint32_t desc)
{
    uint32_t pn = Map_GetPN(x, y, desc);
    uint32_t tbl = R32(desc + 0x24);
    uint32_t col = (uint32_t)((int16_t)((uint32_t)(x + (int32_t)0xFC000000) >> 20)) & 0x1F;
    uint32_t row = (uint32_t)((int16_t)((uint32_t)(y + (int32_t)0xFC000000) >> 20)) & 0x1F;
    uint32_t v = (((pn & 0x0FFE) << 2) & 0x7FFE) | (pn & 0x2000) << 16 | (pn & 0x8000) << 15 |
                 (pn & 0x4000) << 17 | (pn & 1) << 28 | (pn & 0x1000) << 8 | R32(desc + 0x2C);
    if (!tbl) return;
    W32(tbl + row * 0x80 + col * 4, v);
}

/* 0x0605817C: carry the enemies (types < 0x1000 with a loaded slot) standing on
 * `self`: Obj_CarryContact 2x8 box, then move those standing (+0x10 & 0x2000) by
 * (dx, -dy). Static here (also used by other handlers in SHINOBI). */
static void Obj_CarryEnemies(uint32_t self, int32_t dx, int32_t dy)
{
    for (int16_t s = 0; s < OBJ_SLOTS; s++) {
        uint32_t e = OBJ_TABLE + (uint32_t)(int16_t)(s * (int)OBJ_SIZE);
        if (R16(e) == 0xFFFF) continue;
        if (R16(e) >= 0x1000) continue;
        if (Enemy_FindSlot((int16_t)R16(e)) == -1) continue;
        if ((int16_t)Obj_CarryContact(self, e, 2, 8) == 0) continue;
        if (!(R16(e + 0x10) & 0x2000)) continue;
        W32(e + 8, R32(e + 8) + (uint32_t)dx);
        W32(e + 0xC, R32(e + 0xC) - (uint32_t)dy);
    }
}

/* ---- 0x1002: item crate ----
 * A 32x32 block written into the collision map as cells 0x3F8/0x3FA/0x3FC/0x3FE
 * (the original cells are saved at +0x48..+0x4E). Falls when there is no floor
 * under it, breaks into 6 pieces under Sho's sword and drops item +0x18.
 * +6 state: 0 in the map, 1-3 falling (sprite), 4-6 breaking. +0x34 fall speed. */

/* 0x0604C5B2: save the 4 map cells under the crate and write the crate cells
 * (VDP2 refresh only when within 0xC0 px of the screen centre). */
static void Crate_PlaceCells(int32_t x, int32_t y)
{
    uint32_t o = CUR, e = o + 0x48;
    int32_t d = RS32(o + 8) - RS32(CAM_X) + (int32_t)0xFF600000;
    if (d < 0) d = RS32(CAM_X) - RS32(o + 8) + 0x00A00000;
    int16_t vis = 1;
    if (d > 0x00C00000) vis = 0;
    uint32_t desc = coll_desc();
    int32_t x2 = x + 0x00100000, y2 = y + 0x00100000;
    W16(e + 0, Map_GetPN(x, y, desc));   cell_store(0x03F8); if (vis) Map_PutCell(x, y, desc);
    W16(e + 2, Map_GetPN(x2, y, desc));  cell_store(0x03FA); if (vis) Map_PutCell(x2, y, desc);
    W16(e + 4, Map_GetPN(x, y2, desc));  cell_store(0x03FC); if (vis) Map_PutCell(x, y2, desc);
    W16(e + 6, Map_GetPN(x2, y2, desc)); cell_store(0x03FE); if (vis) Map_PutCell(x2, y2, desc);
}

/* 0x0604C6E8: put the saved map cells back. */
static void Crate_RestoreCells(int32_t x, int32_t y)
{
    uint32_t e = CUR + 0x48;
    uint32_t desc = coll_desc();
    int32_t x2 = x + 0x00100000, y2 = y + 0x00100000;
    Map_GetPN(x, y, desc);   cell_store(R16(e + 0)); Map_PutCell(x, y, desc);
    Map_GetPN(x2, y, desc);  cell_store(R16(e + 2)); Map_PutCell(x2, y, desc);
    Map_GetPN(x, y2, desc);  cell_store(R16(e + 4)); Map_PutCell(x, y2, desc);
    Map_GetPN(x2, y2, desc); cell_store(R16(e + 6)); Map_PutCell(x2, y2, desc);
}

/* 0x0604BF3C */
void ObjHandler_1002(void)
{
    uint32_t o = CUR;
    if (R16(o + 2) & 1) {                                   /* spawn */
        W16(o + 6, 4);
        Anim_Start(o + 0x58, 0x060A9400u, 0x060D1B18u, 0x10, RS16(o + 6));
        AND16(o + 2, 0xFFFE);
        W16(o + 0x14, 0);
        W16(o + 0x1C, 0x8000);
        W16(o + 6, 0);
        W32(o + 8, R32(o + 8) & 0xFFF00000u);               /* snap to the 16 px grid */
        W32(o + 0xC, R32(o + 0xC) & 0xFFF00000u);
        Crate_PlaceCells(RS32(o + 8), RS32(o + 0xC));
    }
    int32_t d = RS32(o + 8) - RS32(CAM_X) + (int32_t)0xFF600000;     /* |x - screen centre| */
    if (d < 0) d = 0x00A00000 + (RS32(CAM_X) - RS32(o + 8));
    if (d > 0x02000000 && R16(o + 6) == 0) return;

    if (!FROZEN) {
        switch (R16(o + 6)) {
        case 0:                                             /* in the map */
            if (!(Coll_AttrFlags(Map_GetAttr(RS32(o + 8), RS32(o + 0xC) + 0x00200000, coll_desc())) & 0xF)) {
                W16(o + 6, 1);                              /* nothing below: fall */
            } else if (Hitbox_Test(o, 0, PL, 2) != 0xFFFF) {/* hit by the sword */
                Obj_Spawn(2, RS32(HIT_POINT), RS32(HIT_POINT + 4), 0, 0);
                W16(o + 6, 4);
                W16(o + 0x14, 0);
                sub_06007CDE(0x122);
                Map_GetPN(RS32(o + 8), RS32(o + 0xC), coll_desc());                            cell_store(0);
                Map_GetPN(RS32(o + 8) + 0x00100000, RS32(o + 0xC), coll_desc());               cell_store(0);
                Map_GetPN(RS32(o + 8), RS32(o + 0xC) + 0x00100000, coll_desc());               cell_store(0);
                Map_GetPN(RS32(o + 8) + 0x00100000, RS32(o + 0xC) + 0x00100000, coll_desc());  cell_store(0);
            }
            break;
        case 1:
            W16(o + 6, R16(o + 6) + 1);
            Obj_CarryEnemies(o, 0, 0);
            break;
        case 2:                                             /* leave the map */
            Crate_RestoreCells(RS32(o + 8), RS32(o + 0xC));
            W32(o + 0x34, 0x00010000);
            W16(o + 6, R16(o + 6) + 1);
            Obj_CarryEnemies(o, 0, 0);
            break;
        case 3:                                             /* falling */
            if (Coll_AttrFlags(Map_GetAttr(RS32(o + 8), RS32(o + 0xC) + 0x00200000, coll_desc())) & 0xF) {
                W32(o + 8, R32(o + 8) & 0xFFF00000u);       /* landed: back into the map */
                W32(o + 0xC, R32(o + 0xC) & 0xFFF00000u);
                Crate_PlaceCells(RS32(o + 8), RS32(o + 0xC));
                W32(o + 0x34, 0);
                W16(o + 6, 0);
            } else {
                Obj_PlayerContact(0);
                W32(o + 0x34, R32(o + 0x34) + 0x5000);
                if (RS32(o + 0x34) > 0x000A0000) W32(o + 0x34, 0x000A0000);
                W32(o + 0xC, R32(o + 0xC) + R32(o + 0x34));
                Obj_CarryEnemies(o, 0, 0);
            }
            break;
        case 4:
            W16(o + 6, R16(o + 6) + 1);
            break;
        case 5:                                             /* broken: restore the map, drop the item */
            Crate_RestoreCells(RS32(o + 8), RS32(o + 0xC));
            W16(o + 6, R16(o + 6) + 1);
            W16(o + 0x14, 0);
            Item_Drop(RS16(o + 0x18), RS32(o + 8) + 0x00100000, RS32(o + 0xC) + 0x00200000);
            break;
        case 6:
            W16(o + 0x14, R16(o + 0x14) + 1);
            if (R16(o + 0x14) >= 0x20) { W16(o, 0xFFFF); return; }   /* gone */
            break;
        }
    }
    AND16(o + 0x58, 0xBFFF);
    switch (R16(o + 6)) {
    case 0:                                                 /* drawn by the map: tick only */
        OR16(o + 0x58, 0x4000);
        /* fall through */
    case 1: case 2: case 3:
        W16(SPR + 0x0E, 0);
        W16(SPR + 0x10, 0x0200);
        W16(SPR + 0x02, (uint32_t)(RS32(o + 8) - RS32(CAM_X)) >> 16);
        W16(SPR + 0x04, (uint32_t)(RS32(o + 0xC) - RS32(CAM_Y)) >> 16);
        W16(SPR + 0x0C, 0);
        W16(SPR + 0x0A, 0x0C);
        W32(SPR + 0x18, 0xFFFFFF80u);
        Anim_Tick(o + 0x58);
        break;
    case 4: case 5: case 6:                                 /* 6 flying pieces */
        W16(SPR + 0x10, 0x0600);
        W16(SPR + 0x0C, 0);
        W16(SPR + 0x0A, 0x0C);
        W32(SPR + 0x18, 0xFFFFFF80u);
        for (uint16_t i = 0; i < 6; i++) {
            int16_t ang = RS16(0x060A9852u + 2u * i);
            uint32_t t = R16(o + 0x14);
            int32_t dx = FixMul(Cos8(ang), (int32_t)((t << 2) << 16));
            int32_t dy = -FixMul(Sin8(ang), (int32_t)((t * 3) << 16));
            t = R16(o + 0x14);
            dy += (int32_t)((t * t) << 12);                 /* gravity */
            OR16(o + 0x58, 0x4000);
            W16(o + 0x5A, (uint32_t)(i + 1));
            Anim_Tick(o + 0x58);
            W16(SPR + 0x0E, (uint32_t)((int32_t)R16(o + 0x14) / 8));       /* A.BIN 0x060092BC */
            W16(SPR + 0x02, (uint32_t)(RS32(o + 8) + dx - RS32(CAM_X)) >> 16);
            W16(SPR + 0x04, (uint32_t)(RS32(o + 0xC) + dy - RS32(CAM_Y)) >> 16);
            uint32_t off = 0x060A983Au + 4u * i;
            W16(SPR + 0x02, (uint32_t)(RS16(SPR + 0x02) + (RS16(off) - RS16(SPR + 0x06))));
            W16(SPR + 0x04, (uint32_t)(RS16(SPR + 0x04) + (RS16(off + 2) - RS16(SPR + 0x08))));
            W16(SPR + 0x14, (uint32_t)(R16(o + 0x14) << 1));               /* spin */
            if (ang < 0x40) W16(SPR + 0x14, (uint32_t)(-RS16(SPR + 0x14)));
            Sprite_Draw(SPR);
        }
        break;
    }
}

/* ---- 0x1003: trap door (end of 1-1) ----
 * Two leaves 0x50 px apart, 0x56 px high, drawn as distorted quads. When Sho is
 * within 0x70 px vertically and 0x80 px horizontally of x+0x28, a guard ninja
 * (type 4, subtype (+0x12 & 0x10) ? 1 : 0, item +0x18) is registered at x+0x28
 * and the door opens (+0x14 angle 0 -> 0x3C), stays open 17 frames and closes.
 * Only once: +0x48 = 1 afterwards. +0x4A open-time counter. */
/* 0x0604C7C0 */
void ObjHandler_1003(void)
{
    uint32_t o = CUR;
    if (R16(o + 2) & 1) {                                   /* spawn */
        AND16(o + 2, 0xFFFE);
        W16(o + 0x14, 0);
        W16(o + 6, 0);
        W16(o + 0x48, 0);
        W16(o + 0x4A, 0);
        W16(o + 0x1C, 0x8000);
    }
    int32_t d = RS32(o + 8) - RS32(CAM_X) + (int32_t)0xFF600000;
    if (d < 0) d = RS32(CAM_X) - RS32(o + 8) + 0x00A00000;
    if (d > 0x01800000) return;

    if (!FROZEN) {
        switch (R16(o + 6)) {
        case 0:                                             /* closed: wait for Sho */
            W16(o + 0x14, 0);
            if (RS16(o + 0x48) != 0) break;
            {
                int32_t dy = RS32(PL + 0xC) - RS32(o + 0xC);
                if (dy < 0) dy = RS32(o + 0xC) - RS32(PL + 0xC);
                if (dy >= 0x00700000) break;
                int32_t dx = RS32(PL + 8) - RS32(o + 8) + (int32_t)0xFFD80000;
                if (dx < 0) dx = -RS32(PL + 8) + RS32(o + 8) + 0x00280000;
                if (dx >= 0x00800000) break;
            }
            if (R16(o + 0x12) & 0x10)
                Obj_Register(4, RS32(o + 8) + 0x00280000, RS32(o + 0xC), 1, (uint16_t)RS16(o + 0x18));
            else
                Obj_Register(4, RS32(o + 8) + 0x00280000, RS32(o + 0xC), 0, (uint16_t)RS16(o + 0x18));
            W16(o + 6, 1);
            break;
        case 1:                                             /* opening */
            W16(o + 0x14, R16(o + 0x14) + 4);
            if (R16(o + 0x14) > 0x38) {
                W16(o + 6, R16(o + 6) + 1);
                W16(o + 0x4A, 0);
            }
            break;
        case 2:                                             /* open */
            W16(o + 0x4A, R16(o + 0x4A) + 1);
            if (RS16(o + 0x4A) > 0x10) W16(o + 6, R16(o + 6) + 1);
            break;
        case 3:                                             /* closing */
            W16(o + 0x14, R16(o + 0x14) - 2);
            if (R16(o + 0x14) == 0) {
                W16(o + 0x14, 0);
                W16(o + 6, 0);
                W16(o + 0x48, 1);
            }
            break;
        }
    }
    /* left leaf: quad TL(+0x24) TR(+0x28) BR(+0x2C) BL(+0x30) */
    W16(SPR + 0x24, (uint32_t)(RS32(o + 8) - RS32(CAM_X)) >> 16);
    W16(SPR + 0x26, (uint32_t)(RS32(o + 0xC) - RS32(CAM_Y) + (int32_t)0xFFAA0000) >> 16);
    W16(SPR + 0x30, (uint32_t)(RS32(o + 8) - RS32(CAM_X)) >> 16);
    W16(SPR + 0x32, (uint32_t)(RS32(o + 0xC) - RS32(CAM_Y)) >> 16);
    int32_t w = FixMul(Cos8(RS16(o + 0x14)), 0x00280000);  /* leaf width */
    int32_t k = FixMul(Sin8(RS16(o + 0x14)), 0x00080000);  /* perspective skew */
    int16_t kh = (int16_t)((uint32_t)k >> 16);
    W16(SPR + 0x28, (uint32_t)(int16_t)(RS16(SPR + 0x24) + (int16_t)((uint32_t)w >> 16)));
    W16(SPR + 0x2A, (uint32_t)(int16_t)(RS16(SPR + 0x26) - kh));
    W16(SPR + 0x2C, R16(SPR + 0x28));
    W16(SPR + 0x2E, (uint32_t)(int16_t)(RS16(SPR + 0x32) + kh));
    W16(SPR + 0x10, 0x0204);                                /* distorted sprite */
    W16(SPR + 0x0A, (R16(o + 0x12) & 3) + 0x0D);
    W16(SPR + 0x0E, 0);
    W16(SPR + 0x0C, 0);
    W32(SPR + 0x18, 0xFFFFFF80u);
    if (R16(o + 6) == 1) W32(SPR + 0x18, 1);
    W16(SPR + 0x00, (R16(o + 0x12) & 3) + 0x26);
    Sprite_Draw(SPR);
    /* right leaf: mirrored, 0x50 px to the right */
    int16_t lw = (int16_t)(RS16(SPR + 0x28) - RS16(SPR + 0x24));
    W16(SPR + 0x24, R16(SPR + 0x24) + 0x50);
    W16(SPR + 0x28, (uint32_t)(int16_t)(RS16(SPR + 0x24) - lw));
    W16(SPR + 0x2C, R16(SPR + 0x28));
    W16(SPR + 0x30, R16(SPR + 0x24));
    Sprite_Draw(SPR);
}
