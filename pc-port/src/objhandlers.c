/* Stage object handlers, ported from SHINOBI.PPB. */
#include "objhandlers.h"
#include "objects.h"
#include "ram.h"
#include "fixmath.h"
#include "anim.h"
#include "player.h"
#include "objhandlers_d.h"
#include "objhandlers_f.h"
#include "objhandlers_e.h"
#include <stdio.h>

#define CUR      R32(OBJ_CUR)
#define PL       0x060EA2B4u           /* player object */
#define CAM_X    0x060252A0u
#define CAM_Y    0x060252A4u
#define FROZEN   (R16(0x0602529Eu) & 0x8000)
#define SPR      0x060250ACu

static uint32_t slot_addr(int s) { return OBJ_TABLE + (uint32_t)(int16_t)(s * (int)OBJ_SIZE); }
static int32_t iabs(int32_t v) { return v < 0 ? -v : v; }

void Obj_Push(uint32_t o, int32_t dx, int32_t dy)
{
    int32_t px = RS32(o + 0x40), py = RS32(o + 0x44);
    if (dx != 0 && px != 0) {
        if ((px ^ dx) & (int32_t)0x80000000) OR16(o + 0x10, 0x0400);    /* pushed from both sides */
        else if (!(iabs(dx) > iabs(px))) dx = 0;
    }
    if (dy != 0 && py != 0) {
        if ((py ^ dy) & (int32_t)0x80000000) OR16(o + 0x10, 0x0400);
        else if (!(iabs(dy) > iabs(py))) dy = 0;
    }
    W32(o + 0x40, (uint32_t)(px + dx));
    W32(o + 0x44, (uint32_t)(py + dy));
}

int Rect_Overlap(const ObjRect *a, const ObjRect *b)
{
    if (((a->l - b->r) & (int32_t)0x80000000) == ((a->r - b->l) & (int32_t)0x80000000)) return 0;
    if (((a->t - b->b) & (int32_t)0x80000000) == ((a->b - b->t) & (int32_t)0x80000000)) return 0;
    return -1;
}

/* d = smallest push that separates a from b (one axis only, sign = away from b). */
int Rect_Intersect(const ObjRect *a, const ObjRect *b, int32_t d[2])
{
    if (!Rect_Overlap(a, b)) return 0;
    d[0] = (a->r < b->r ? a->r : b->r) - (b->l < a->l ? a->l : b->l);
    d[1] = (a->b < b->b ? a->b : b->b) - (b->t < a->t ? a->t : b->t);
    if (d[1] < d[0]) d[0] = 0; else d[1] = 0;
    if (a->l < b->l) d[0] = -d[0];
    if (a->t < b->t) d[1] = -d[1];
    return -1;
}

/* Hitbox of the current object from its animation frame (+0x68 -> {?, l, t, r, b}). */
static void obj_box(uint32_t o, uint16_t mode, ObjRect *r)
{
    uint32_t hb = R32(o + 0x68);
    int32_t x = RS32(o + 8), y = RS32(o + 0xC);
    if (!(mode & 0x8000)) { r->l = x + RS16(hb + 2) * 0x10000; r->r = x + RS16(hb + 6) * 0x10000; }
    else                  { r->l = x - RS16(hb + 6) * 0x10000; r->r = x - RS16(hb + 2) * 0x10000; }
    if (!(mode & 2))      { r->t = y + RS16(hb + 4) * 0x10000; r->b = y + RS16(hb + 8) * 0x10000; }
    else                  { r->t = y - RS16(hb + 8) * 0x10000; r->b = y - RS16(hb + 4) * 0x10000; }
    r->b -= 2; r->t -= 2;
}

/* Player vs current object. mode: 0x8000 mirror X, 2 mirror Y, 1 one-way top,
 * 4 detect stomp, 8 touch only (no push). Returns 0 none, 1 touch, 2 player on top, 3 stomp. */
int Obj_PlayerContact(uint16_t mode)
{
    uint32_t o = CUR;
    int16_t w = RS16(PL + 0x1E), h = RS16(PL + 0x20);
    ObjRect p, b;
    p.l = RS32(PL + 8) - (w >> 1) * 0x80000;
    p.r = w * 0x80000 + RS32(PL + 8) - (w >> 1) * 0x80000;
    p.t = RS32(PL + 0xC) - h * 0x80000;
    p.b = RS32(PL + 0xC);
    obj_box(o, mode, &b);
    int32_t d[2];
    int ret = 0;
    if (!Rect_Intersect(&p, &b, d)) return 0;
    OR16(PL + 0x10, 0x1000);
    OR16(o + 0x10, 0x1000);
    ret = 1;
    if (mode & 8) d[0] = d[1] = 0;
    if (mode & 1) {
        if ((!(R16(o + 0x24) & 0x1000) && (R16(PL + 2) & 0x4000) && d[0] == 0 && d[1] < 0) ||
            ((R16(PL + 0x24) & 0x2000) && d[0] == 0 && d[1] < 0))
            OR16(PL + 0x10, 0x0800);
        else
            d[0] = d[1] = 0;
    }
    Obj_Push(PL, d[0], -d[1]);
    if ((mode & 4) && !(R16(PL + 0x10) & 0x10)) {
        int32_t mid = p.l + ((p.r - p.l) + ((p.r - p.l) < 0)) / 2;
        if (b.l < mid && mid < b.r && b.t < p.t && p.t < b.b) {
            OR16(PL + 0x10, 0x10);
            if (R16(PL + 2) & 0x0400) { OR16(PL + 0x10, 0x80); OR16(o + 0x10, 0x80); ret = 3; }
        }
    }
    if (d[0] == 0 && d[1] < 0 && !(R16(PL + 2) & 0x20)) { OR16(PL + 0x10, 0x2000); ret = 2; }
    if (d[0] == 0 && d[1] > 0 && !(R16(PL + 0x10) & 0x2000) && (R16(PL + 2) & 0x4000) && RS32(PL + 0x34) >= 0)
        W32(PL + 0x34, 0xFFFFFFFFu);                    /* bumped a ceiling: start falling */
    /* (debug cheat: hitbox outlines via 0x06028CA2, not ported) */
    return ret;
}

/* Something (an enemy) standing on / pushed by `self`. Box of `other`: w*8 x h*8 above its feet. */
int Obj_CarryContact(uint32_t self, uint32_t other, int w, int h)
{
    ObjRect a, b;
    int32_t d[2];
    a.l = RS32(other + 8) - ((int16_t)w >> 1) * 0x80000;
    a.r = RS32(other + 8) + (int16_t)w * 0x80000 - ((int16_t)w >> 1) * 0x80000;
    a.t = RS32(other + 0xC) - (int16_t)h * 0x80000;
    a.b = RS32(other + 0xC);
    uint32_t hb = R32(self + 0x68);
    b.l = RS16(hb + 2) * 0x10000 + RS32(self + 8);
    b.r = RS16(hb + 6) * 0x10000 + RS32(self + 8);
    b.t = RS16(hb + 4) * 0x10000 + RS32(self + 0xC) - 2;
    b.b = RS16(hb + 8) * 0x10000 + RS32(self + 0xC) - 2;
    if (!Rect_Intersect(&a, &b, d)) return 0;
    OR16(other + 0x10, 0x1000);
    Obj_Push(other, d[0], -d[1]);
    if (d[0] == 0 && d[1] < 0) OR16(other + 0x10, 0x2000);
    return 1;
}

/* ---- 0x1004: floating platform (bobs on the water, tilts and sinks under Sho) ----
 * +0x48 bob amplitude, +0x4C tilt amplitude (16.16, damped x0.625 each cycle),
 * +0x50 rest Y (px), +0x52 last player contact, +0x54 carrying flag, +0x56 angle. */
void ObjHandler_1004(void)
{
    uint32_t o = CUR, e = o + 0x48;
    if (R16(o + 2) & 1) {                                   /* spawn */
        W16(o + 6, 3);
        Anim_Start(o + 0x58, 0x060A9400u, 0x060D1B18u, 0x10, RS16(o + 6));
        AND16(o + 2, 0xFFFE);
        W16(o + 0x14, 0);
        W16(o + 0x1C, 0x4000);
        W16(e + 8, (uint32_t)(R32(o + 0xC) >> 16));
        W16(o + 0x1E, 0xFFFF);
        if (R16(o + 0x12) & 0x10)                           /* carries an enemy (type 1) */
            W16(o + 0x1E, (uint32_t)Obj_Register(1, RS32(o + 8), RS32(o + 0xC) + (int32_t)0xFFE80000, 0x0C,
                                                  (uint16_t)RS16(o + 0x18)));
    }
    int32_t dist = RS32(o + 8) - RS32(CAM_X) + (int32_t)0xFF600000;     /* |x - screen centre| */
    if (dist < 0) dist = 0x00A00000 + (RS32(CAM_X) - RS32(o + 8));
    if (dist > 0x01800000) { W32(e, 0); W32(e + 4, 0); return; }      /* off screen: idle */

    if (!FROZEN) {
        int16_t on = (int16_t)Obj_PlayerContact(0), carry = 0;
        if (RS16(o + 0x1E) != -1)
            carry = (int16_t)Obj_CarryContact(o, slot_addr(RS16(o + 0x1E)), 4, 8);
        W32(o + 0x3C, 0);
        if ((on == 2 && RS16(e + 10) != 2) || (on != 2 && RS16(e + 10) == 2) || RS16(e + 12) != carry) {
            W16(o + 0x14, 0);                               /* landing / leaving: new splash */
            W32(e, 0x00040000);
            W32(e + 4, 0x00030000);
        }
        W16(e + 10, (uint16_t)on);
        W16(e + 12, (uint16_t)carry);
        W32(o + 0x2C, R32(o + 0xC));
        int32_t amp = RS32(e);
        W32(o + 0x3C, (uint32_t)(FixMul(Sin8((R16(o + 0x14) << 2) & 0xFF), amp) + (amp + (amp < 0)) / 2));
        W16(e + 14, (uint32_t)(int16_t)(FixMul(Sin8((R16(o + 0x14) << 3) & 0xFF), RS32(e + 4)) >> 16));
        W32(o + 0xC, (uint32_t)(RS16(e + 8) * 0x10000 + RS32(o + 0x3C)));
        W16(o + 0x14, R16(o + 0x14) + 1);
        if ((int)(R16(o + 0x14) << 2) >= 0x100) {           /* one cycle: damp */
            int32_t v = FixMul(RS32(e), 0xA000);     W32(e, (uint32_t)(v < 0x8000 ? 0 : v));
            v = FixMul(RS32(e + 4), 0xA000);          W32(e + 4, (uint32_t)(v < 0x8000 ? 0 : v));
            W16(o + 0x14, 0);
        }
        /* idle swell: phase from the distance to x=0x780, time = frame counter */
        int32_t u = 0x07800000 - RS32(o + 8);
        if (u < 0) u = RS32(o + 8) + (int32_t)0xF8800000;
        int16_t ph = (int16_t)(((uint32_t)u << 1) >> 16) & 0xFF;
        int t = R16(0x060F4CCCu) & 0xFF;
        int32_t a = FixMul(Cos8(ph), 0x30000);
        W32(o + 0xC, (uint32_t)(RS32(o + 0xC) - FixMul(Cos8(t), a)));
        a = FixMul(Sin8(ph), 0x20000);
        W16(e + 14, (uint32_t)(RS16(e + 14) - (int16_t)(FixMul(Cos8(t), a) >> 16)));
        if (on == 2) {                                      /* Sho's weight: sink and tilt */
            W32(o + 0xC, R32(o + 0xC) + 0x30000);
            int32_t k = FixDiv(RS32(o + 8) - RS32(PL + 8), 0x300000);
            if (k > 0x10000) k = 0x10000;
            W16(e + 14, (uint32_t)(RS16(e + 14) + (int16_t)(FixMul(k, 0x20000) >> 16)));
        }
        if (carry) W32(o + 0xC, R32(o + 0xC) + 0x20000);
        if (on == 2)                                        /* carry Sho with the platform */
            W32(PL + 0x3C, (uint32_t)(RS32(PL + 0x3C) - (RS32(o + 0xC) - RS32(o + 0x2C))));
        if (carry) {
            uint32_t c = slot_addr(RS16(o + 0x1E));
            W32(c + 0x3C, (uint32_t)(RS32(c + 0x3C) - (RS32(o + 0xC) - RS32(o + 0x2C))));
        }
    }
    W16(SPR + 0x0E, 0);
    W16(SPR + 0x10, 0x0600);                                /* rotate */
    W16(SPR + 0x02, (uint32_t)((RS32(o + 8) - RS32(CAM_X)) >> 16));
    W16(SPR + 0x04, (uint32_t)((RS32(o + 0xC) - RS32(CAM_Y)) >> 16));
    W16(SPR + 0x0C, 0);
    W16(SPR + 0x14, R16(e + 14));
    if (R16(o + 0x12) & 1) {                                /* second part (frame 1) */
        W16(SPR + 0x0A, 0x2F);
        W32(SPR + 0x18, 0xFFFFFF80u);
        W16(o + 0x5A, 1);
        Anim_Tick(o + 0x58);
    }
    W16(SPR + 0x0A, 0x2E);
    W32(SPR + 0x18, 0x80);
    W16(o + 0x5A, 0);
    Anim_Tick(o + 0x58);
}

void ObjHandler_Call(uint16_t type)
{
    static uint8_t warned[0x1000];
    switch (type) {
    case 0x1000: ObjHandler_1000(); break;
    case 0x1001: ObjHandler_1001(); break;
    case 0x1002: ObjHandler_1002(); break;
    case 0x1003: ObjHandler_1003(); break;
    case 0x1004: ObjHandler_1004(); break;
    case 0x1005: ObjHandler_1005(); break;
    case 0x1006: ObjHandler_1006(); break;
    case 0x1007: ObjHandler_1007(); break;   /* objhandlers_e.c / objhandlers_e2.c */
    case 0x1008: ObjHandler_1008(); break;
    case 0x1009: ObjHandler_1009(); break;
    case 0x100A: ObjHandler_100A(); break;
    case 0x100B: ObjHandler_100B(); break;
    case 0x100C: ObjHandler_100C(); break;
    case 0x100D: ObjHandler_100D(); break;
    case 0x100F: ObjHandler_100F(); break;
    case 0x1010: ObjHandler_1010(); break;
    case 0x1011: ObjHandler_1011(); break;
    case 0x1013: ObjHandler_1013(); break;
    case 0x1014: ObjHandler_1014(); break;
    case 0x1015: ObjHandler_1015(); break;
    case 0x1016: ObjHandler_1016(); break;
    case 0x1017: ObjHandler_1017(); break;
    case 0x1018: ObjHandler_1018(); break;
    case 0x1019: ObjHandler_1019(); break;
    case 0x101A: ObjHandler_101A(); break;
    case 0x101B: ObjHandler_101B(); break;
    case 0x101C: ObjHandler_101C(); break;
    case 0x101D: ObjHandler_101D(); break;
    case 0x101E: ObjHandler_101E(); break;
    case 0x101F: ObjHandler_101F(); break;
    case 0x1025: ObjHandler_1025(); break;   /* objhandlers_d.c (stage 4-2) */
    case 0x1026: ObjHandler_1026(); break;
    case 0x1027: ObjHandler_1027(); break;
    case 0x1028: ObjHandler_1028(); break;
    default:
        if (ObjHandlerF_Call(type)) break;  /* objhandlers_f*.c: 0x1020-0x1050 */
        if (!warned[type & 0xFFF]) {
            warned[type & 0xFFF] = 1;
            printf("[obj] handler %04X (0x%08X) not ported\n", type, R32(0x060AC820u + 4u * (type - 0x1000u)));
        }
        break;
    }
}
