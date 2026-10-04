/* Stage 4-2 objects 0x1025-0x1028 (ledges, beams, glow, lanterns), ported from SHINOBI.PPB.
 * See objhandlers_d.h for the field layout. Style: objhandlers.c. */
#include "objhandlers_d.h"
#include "objhandlers.h"
#include "objects.h"
#include "enemy.h"
#include "enemy_int.h"
#include "fx.h"
#include "anim.h"
#include "sprite.h"
#include "player_ext.h"
#include "ram.h"

#undef O
#define CUR      R32(OBJ_CUR)
#define C        0x060EA618u            /* stage control block: +4 world top, +0xC world bottom */
#define YMIN     RS32(C + 4)
#define YMAX     RS32(C + 0xC)
#define VBLANKS  0x060143EAu
#define DEFS     0x060AA95Cu            /* stage 4-2 object sprite defs */
#define SCRIPTS  0x060D26E8u

static uint32_t slot_addr(int s) { return OBJ_TABLE + (uint32_t)(int16_t)(s * (int)OBJ_SIZE); }

/* A.BIN 0x06006140 */
static int step_to(int a, int b, int s)
{
    int d = a - b;
    if (d < 0) d = -d;
    if (d < s) return b;
    return a < b ? a + s : a - s;
}

int Pal_StepTowards(int dst, int n, uint32_t src, int step)          /* A.BIN 0x06006000 */
{
    int done = 1;
    uint32_t d = SHADOW_CRAM + 2u * (uint32_t)(uint16_t)dst;
    for (; n > 0; n--, d += 2, src += 2) {
        uint16_t a = R16(d), b = R16(src);
        int r = step_to(a & 31, b & 31, step);
        int g = step_to((a >> 5) & 31, (b >> 5) & 31, step);
        int bl = step_to((a >> 10) & 31, (b >> 10) & 31, step);
        W16(d, (uint32_t)(((bl << 10) & 0x7C00) | ((g << 5) & 0x03E0) | (r & 31)));
        if (R16(d) != (R16(src) & 0x7FFF)) done = 0;
    }
    W16(0x060250A8u, 1);
    return done;
}

static int32_t speed(uint16_t cls)
{
    return cls == 0 ? 0x2000 : cls == 1 ? 0x6000 : cls == 2 ? 0x4000 : 0;
}

static void draw(uint32_t o, uint16_t ctrl, uint32_t prio, uint16_t bank, uint16_t pmod)
{
    W16(SPR + 0x02, (uint32_t)(int16_t)((uint32_t)(RS32(o + 8) - 0x04000000) >> 16));
    W16(SPR + 0x04, (uint32_t)(int16_t)((uint32_t)(RS32(o + 0xC) - 0x04000000) >> 16));
    W16(SPR + 0x10, ctrl);
    W32(SPR + 0x18, prio);
    W16(SPR + 0x0A, bank);
    W16(SPR + 0x0C, 0);
    W16(SPR + 0x0E, pmod);
    Anim_Tick(o + 0x58);
}

/* ---- 0x1026: background beam ---- */
void ObjHandler_1026(void)
{
    uint32_t o = CUR;
    if (R16(o + 2) & 1) {
        W16(o + 0x1C, 0x8000);
        W16(o + 6, (uint32_t)(3 + (R16(o + 0x12) & 3)));
        Anim_Start(o + 0x58, DEFS, SCRIPTS, 0x10, RS16(o + 6));
        AND16(o + 2, 0xFFFE);
        W16(o + 0x48, 0);
    }
    if (!(R16(0x0602529Eu) & 0x8000) && R16(o + 0x48) != 0) {
        W32(o + 0xC, (uint32_t)(RS32(o + 0xC) + speed((R16(o + 0x12) & 0xC) >> 2)));
        if (RS32(o + 0xC) > YMAX + 0x900000) W32(o + 0xC, (uint32_t)(RS32(o + 0xC) - 0x02000000));
    }
    int32_t d = RS32(o + 0xC) - YMIN + (int32_t)0xFF900000;
    if (d < 0) d = YMIN - RS32(o + 0xC) + 0x700000;
    if (d >= 0x01200000) return;
    draw(o, 0x0200, 0xFFFFFF80u, 0, 0);
}

/* ---- 0x1025: ledge ---- */
void ObjHandler_1025(void)
{
    uint32_t o = CUR;
    if (R16(o + 2) & 1) {
        W16(o + 0x1C, 0x8000);
        W16(o + 6, 0);
        Anim_Start(o + 0x58, DEFS, SCRIPTS, 0x10, RS16(o + 6));
        AND16(o + 2, 0xFFFE);
        W16(o + 0x48, 0); W16(o + 0x4A, 0); W16(o + 0x50, 0);
    }
    if (!(R16(0x0602529Eu) & 0x8000)) {
        if (R16(o + 0x48) != 0) {
            W32(o + 0xC, (uint32_t)(RS32(o + 0xC) + speed(R16(o + 0x12) & 3)));
            if (RS32(o + 0xC) > YMAX + 0x02000000) {               /* wrap to the top */
                W32(o + 0xC, (uint32_t)(RS32(o + 0xC) - 0x04000000));
                W16(o + 0x4A, 0); W16(o + 0x4C, 0);
            } else {
                int32_t y = RS32(o + 0xC);
                W16(o + 0x4C, YMIN + 0x00C80000 < y ? 1 : 0);
                W16(o + 0x4A, (YMIN + (int32_t)0xFFD00000 < y && y < YMIN + 0x00A80000) ? 1 : 0);
            }
        }
        int on = 0;
        int32_t y = RS32(o + 0xC);
        if (YMIN + (int32_t)0xFFF00000 < y) {
            int32_t d = RS32(PL + 0xC) - y;
            if (d < 0) d = -d;
            if (d < 0x00640000 && (uint16_t)Obj_PlayerContact(1) == 2) {
                W32(PL + 0x3C, (uint32_t)(RS32(PL + 0x3C) - 0x20000));  /* keep Sho pressed on it */
                on = 1;
            }
        }
        W16(o + 0x4E, (uint32_t)on);
    }
    int32_t d = RS32(o + 0xC) - YMIN + (int32_t)0xFF900000;
    if (d < 0) d = YMIN - RS32(o + 0xC) + 0x700000;
    if (d >= 0x00B00000) return;
    if (RS16(o + 0x50) == 1) {                                  /* glowing */
        if (R16(o + 0x52) == 0) {
            W16(o + 0x52, (uint32_t)Obj_Register(0x1027, RS32(o + 8), RS32(o + 0xC), 0, 0xFFFF));
            W16(o + 0x52, R16(o + 0x52) + 1);
            if (R16(o + 0x52) != 0) W16(slot_addr(RS16(o + 0x52) - 1) + 0x48, R16(OBJ_CUR_IDX));
        }
        if (Pal_StepTowards(0x10, 0x10, 0x060D2620u, 1)) W16(o + 0x50, 2);
    } else if (RS16(o + 0x50) == 3) {
        if (Pal_StepTowards(0x10, 0x10, SHADOW_CRAM, 1)) W16(o + 0x50, 0);
    }
    draw(o, 0x0200, 0xFFFFFF81u, R16(o + 0x50) != 0 ? 1 : 0, 0);
}

/* ---- 0x1027: glow overlay on a ledge ---- */
void ObjHandler_1027(void)
{
    uint32_t o = CUR;
    if (R16(o + 2) & 1) {
        W16(o + 0x1C, 0x8000);
        W16(o + 6, 2);
        Anim_Start(o + 0x58, DEFS, SCRIPTS, 0x10, RS16(o + 6));
        AND16(o + 2, 0xFFFE);
    }
    if (!(R16(0x0602529Eu) & 0x8000)) {
        uint32_t p = slot_addr(RS16(o + 0x48));
        if (R16(o + 0x58) & 0x8000) {                           /* done: release the ledge */
            W16(p + 0x52, 0);
            W16(o, 0xFFFF);
            return;
        }
        W32(o + 8, R32(p + 8));
        W32(o + 0xC, R32(p + 0xC));
    }
    if (R16(VBLANKS) & 1) OR16(o + 0x58, 0x4000); else AND16(o + 0x58, 0xBFFF);
    /* colour-calc mode: the original reads the CURRENT PROJECTILE's anim flags here */
    uint16_t pm = R16(R32(PROJ_CUR) + 0x20) & 3;
    draw(o, 0x0200, 0xFFFFFF82u, 2, pm ? pm : 1);
}

/* ---- 0x1028: lantern ---- */
void ObjHandler_1028(void)
{
    uint32_t o = CUR;
    if (R16(o + 2) & 1) {
        W16(o + 0x1C, 0x8000);
        uint16_t k = R16(o + 0x12) & 0x1C;
        if (k == 0)      { W16(o + 6, 7); W16(o + 0x48, 0xFFFF); }
        else if (k == 4) { W16(o + 6, 9); W16(o + 0x48, 0); W16(o + 0x14, 50); }
        else if (k == 8) { W16(o + 6, 10); W32(o + 0x30, 0); }
        Anim_Start(o + 0x58, DEFS, SCRIPTS, 0x10, RS16(o + 6));
        AND16(o + 2, 0xFFFE);
        W16(o + 0x4A, 0);
    }
    if (!(R16(0x0602529Eu) & 0x8000)) {
        uint16_t a = R16(o + 6);
        if (a == 7 || a == 8) {
            if (R16(o + 0x4A) != 0) W32(o + 0xC, (uint32_t)(RS32(o + 0xC) + speed(R16(o + 0x12) & 3)));
            if (RS32(o + 0xC) > YMAX + 0x800000) {              /* wrap: rebuilt at the top */
                W32(o + 0xC, (uint32_t)(RS32(o + 0xC) - 0x14000000));
                if (R16(o + 6) == 8) {
                    W16(o + 6, 7);
                    Anim_Start(o + 0x58, DEFS, SCRIPTS, 0x10, RS16(o + 6));
                    if (RS16(o + 0x48) != -1) W16(slot_addr(RS16(o + 0x48)), 0xFFFF);
                }
            }
            if (R16(o + 6) == 7 && YMIN + 0x80000 < RS32(o + 0xC) && RS32(o + 0xC) < YMAX + 0x500000 &&
                Hitbox_Test(o, 0, PL, 2) != 0xFFFF) {
                int k = (int16_t)Player_AttackKind();
                if (k != 4 && k != 5 && k != 6) {               /* cut */
                    sub_06007CDE(0x122);
                    Obj_Spawn(2, RS32(HIT_POINT), RS32(HIT_POINT + 4), 0, 0);
                    OR16(o + 2, 8);
                    if (RS16(o + 0x18) != -1) {
                        Obj_Spawn(6, RS32(o + 8), RS32(o + 0xC), 0, 0);
                        Item_Drop(RS16(o + 0x18), RS32(o + 8), RS32(o + 0xC));
                    }
                    W16(o + 6, 8);
                    Anim_Start(o + 0x58, DEFS, SCRIPTS, 0x10, RS16(o + 6));
                    int s = Obj_Register(0x1028, RS32(o + 8), RS32(o + 0xC) + (int32_t)0xFFE10000, 4, 0xFFFF);
                    if (s != -1) {
                        uint32_t p = slot_addr(s);
                        W32(p + 0x34, 0x60000);
                        W32(p + 0x30, RS32(o + 8) < RS32(PL + 8) ? 0xFFFF0000u : 0x00010000u);
                    }
                    W16(o + 0x48, (uint32_t)Obj_Register(0x1028, RS32(o + 8), RS32(o + 0xC), 8, 0xFFFF));
                    if (RS16(o + 0x48) != -1) W16(slot_addr(RS16(o + 0x48)) + 0x48, R16(OBJ_CUR_IDX));
                }
            }
        } else if (a == 9) {                                    /* lid flying off */
            W32(o + 8, (uint32_t)(RS32(o + 8) + RS32(o + 0x30)));
            W32(o + 0xC, (uint32_t)(RS32(o + 0xC) - RS32(o + 0x34)));
            W32(o + 0x34, (uint32_t)(RS32(o + 0x34) - 0x5000));
            W16(o + 0x14, R16(o + 0x14) - 1);
            if (R16(o + 0x14) == 0) W16(o, 0xFFFF);
            else if (R16(o + 0x14) < 25) {
                if (R16(VBLANKS) & 1) OR16(o + 0x58, 0x4000); else AND16(o + 0x58, 0xBFFF);
            }
            if (RS32(o + 0x30) < 0) {
                W16(o + 0x48, R16(o + 0x48) - 4);
                if (RS16(o + 0x48) < -0x100) W16(o + 0x48, R16(o + 0x48) + 0x100);
            } else {
                W16(o + 0x48, R16(o + 0x48) + 4);
                if (RS16(o + 0x48) > 0x100) W16(o + 0x48, R16(o + 0x48) - 0x100);
            }
        } else if (a == 10) {                                   /* flame on the base */
            uint32_t p = slot_addr(RS16(o + 0x48));
            if (R16(p + 6) != 8) { W16(o, 0xFFFF); return; }
            W32(o + 8, R32(p + 8));
            W32(o + 0xC, R32(p + 0xC));
            W32(o + 0x30, (uint32_t)(RS32(o + 0x30) + 0x400));
            if (RS32(o + 0x30) > 0x10000) W32(o + 0x30, 0x10000);
        }
    }
    if (RS32(o + 0xC) > YMAX + 0x400000 || RS32(o + 0xC) < YMIN + (int32_t)0xFFF00000) return;
    uint16_t a = R16(o + 6);
    if (a == 10) {
        if (R16(VBLANKS) & 1) AND16(o + 0x58, 0xBFFF); else OR16(o + 0x58, 0x4000);
    }
    if (a == 9)       { W16(SPR + 0x10, 0x0600); W16(SPR + 0x14, R16(o + 0x48)); }
    else if (a == 10) { W16(SPR + 0x10, 0x1A00); W16(SPR + 0x14, R16(o + 0x48));
                        W32(SPR + 0x1C, R32(o + 0x30)); W32(SPR + 0x20, R32(o + 0x30)); }
    W16(SPR + 0x02, (uint32_t)(int16_t)((uint32_t)(RS32(o + 8) - 0x04000000) >> 16));
    W16(SPR + 0x04, (uint32_t)(int16_t)((uint32_t)(RS32(o + 0xC) - 0x04000000) >> 16));
    if (a != 9 && a != 10) W16(SPR + 0x10, 0x0200);
    W32(SPR + 0x18, 0xFFFFFF81u);
    W16(SPR + 0x0A, a == 10 ? 4 : 3);
    W16(SPR + 0x0C, 0);
    W16(SPR + 0x0E, 0);
    Anim_Tick(o + 0x58);
}
