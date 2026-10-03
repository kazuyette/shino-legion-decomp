/* Stage system objects 0x1000 / 0x1001 (SHINOBI.PPB), see objhandlers.c.
 *
 * 0x1000 (0x06049F0C): breakable container. One sword cut (not a ninjutsu/shuriken
 *   attack kind 4/5) splits it in two halves that fly apart, fall to the floor,
 *   bounce once and fade; it drops item +0x18 and a puff (effect 6).
 *   +0x12 & 0x1F = orientation 0/1/2 (halves stacked / side by side, frames 0/2/4).
 *   First half = the object (+8/+0xC, +0x30/+0x34 velocity), second half at
 *   +0x48/+0x4C; +0x50/+0x51 (bytes) rotation of each half; +0x14 timer.
 * 0x1001 (0x0604A67C): 7-piece breakable structure (stone statue/lantern) on the
 *   ground. +0x12 & 0xF = piece index; piece 0 is the master and owns the 6 pieces in
 *   the following 6 object slots (placed consecutively by the stage list, piece k at
 *   master + k * 0x78). It is solid for Sho, stops enemy shurikens, and each sword hit
 *   (counter +6) knocks off the next piece(s) in the order 0x060A97CC; piece 4 bursts
 *   into 12 fragments, piece 5 explodes and drops the master's item (+0x18); once the
 *   counter reaches 6 the remaining core only throws sparks when hit. */
#include "objhandlers.h"
#include "objects.h"
#include "ram.h"
#include "fixmath.h"
#include "anim.h"
#include "sprite.h"
#include "fx.h"
#include "player_ext.h"
#include "enemy_int.h"

#define CUR      R32(OBJ_CUR)

static int32_t iabs(int32_t v) { return v < 0 ? -v : v; }
static uint8_t R8(uint32_t a) { return Ram_Ptr(a)[0]; }
static void    W8(uint32_t a, uint32_t v) { Ram_Ptr(a)[0] = (uint8_t)v; }
/* A.BIN 0x060092BC: signed division r1 / r0 (truncates). */
static int32_t sdiv(int32_t a, int32_t b) { return b ? a / b : 0; }

/* |x - camX - 160| (16.16), the usual activity test of the object handlers. */
static int32_t screen_dist(uint32_t o)
{
    int32_t d = (int32_t)(R32(o + 8) - R32(CAM_X) + 0xFF600000u);
    if (d < 0) d = (int32_t)(0x00A00000u + (R32(CAM_X) - R32(o + 8)));
    return d;
}
/* y - 0x04000000 (layer coordinates), compared signed by the floor tests. */
static int32_t layer_y(uint32_t a) { return (int32_t)(R32(a) + 0xFC000000u); }

/* DUPLICATE of the static Proj_Hitbox in fx.c (0x06060E8E): object hitboxes
 * (kind ko) vs projectile hitboxes (kind kp), through a temporary object view. */
static uint16_t Proj_Hitbox(uint32_t o, int16_t ko, uint32_t p, int16_t kp)
{
    if (iabs(RS32(o + 8) - RS32(p + 8)) > 0x02000000) return 0xFFFF;
    if (iabs(RS32(o + 0xC) - RS32(p + 0xC)) > 0x02000000) return 0xFFFF;
    static const uint32_t TMP = 0x060FFE00u;
    W32(TMP + 8, R32(p + 8)); W32(TMP + 0xC, R32(p + 0xC));
    W16(TMP + 2, R16(p + 4) & 0x8000); W32(TMP + 0x68, R32(p + 0x30));
    return Hitbox_Test(o, ko, TMP, kp);
}

/* DUPLICATE of the static Proj_Bounce in fx.c (0x0606137C). */
static void Proj_Bounce(uint32_t e)
{
    W16(e + 6, 0);
    OR16(e + 4, 0x4000);
    int32_t v = RS32(e + 0x18); W32(e + 0x18, (uint32_t)-((v + (v < 0)) >> 1));
    v = RS32(e + 0x1C);         W32(e + 0x1C, (uint32_t)-((v + (v < 0)) >> 1));
}

/* 0x0606078C (second argument unused): Sho's sword hits an indestructible object:
 * spark (effect 5) + "clang" once per swing (player +4 |= 0x20). Returns 1 if hit. */
static int Obj_SwordClang(uint32_t o)
{
    if (R16(PL + 4) & 0x20) return 0;
    if (Hitbox_Test(o, 0, PL, 2) == 0xFFFF) return 0;
    OR16(PL + 4, 0x20);
    Obj_Spawn(5, RS32(HIT_POINT), RS32(HIT_POINT + 4), R16(PL + 2) & 0x8000, 0);
    sub_06007CDE(0x105);
    return 1;
}

/* 0x0604B7AC: enemy shurikens (type 1, hostile, not already bouncing) touching
 * object o bounce off it. */
static void Obj_BlockShurikens(uint32_t o)
{
    for (uint32_t p = PROJECTILES; p < PROJECTILES + 0x1000u; p += 0x40) {
        if (R16(p) != 1) continue;
        if ((R16(p + 4) & 0x6000) != 0x2000) continue;
        if (Proj_Hitbox(o, 0, p, 2) == 0xFFFF) continue;
        Proj_Bounce(p);
    }
}

/* ======================================================= 0x1000 container */
void ObjHandler_1000(void)                                          /* 0x06049F0C */
{
    uint32_t o = CUR, h = o + 0x48;
    if (R16(o + 2) & 1) {                                           /* spawn */
        W16(o + 6, 0);
        Anim_Start(o + 0x58, 0x060A9400u, 0x060D1B18u, 0x10, RS16(o + 6));
        AND16(o + 2, 0xFFFE);
        W16(o + 0x14, 0);
        W16(o + 0x1C, 0x8000);
        W8(h + 8, 0);
        W8(h + 9, 0);
        switch (R16(o + 0x12) & 0x1F) {
        case 0: W32(h, R32(o + 8));              W32(h + 4, R32(o + 0xC) + 0x00100000u); break;
        case 1: W32(h, R32(o + 8) + 0x00100000u); W32(h + 4, R32(o + 0xC)); W16(o + 0x5A, 2); break;
        case 2: W32(h, R32(o + 8) + 0x00300000u); W32(h + 4, R32(o + 0xC)); W16(o + 0x5A, 4); break;
        }
    }
    if (screen_dist(o) > 0x01800000 && !(R16(o + 2) & 8)) return;

    if (!FROZEN) {
        int16_t k;
        if (!(R16(o + 2) & 8) && Hitbox_Test(o, 0, PL, 2) != 0xFFFF &&
            (k = (int16_t)Player_AttackKind()) != 4 && k != 5) {    /* cut by the sword */
            Obj_Spawn(2, RS32(HIT_POINT), RS32(HIT_POINT + 4), 0, 0);
            OR16(o + 2, 8);
            sub_06007CDE(0x104);
            switch (R16(o + 0x12) & 0x1F) {
            case 0:
            case 2:
                W32(o + 0x34, 0);
                Item_Drop(RS16(o + 0x18), RS32(o + 8) + 0x00300000, RS32(o + 0xC) + 0x00100000);
                Obj_Spawn(6, RS32(o + 8) + 0x00300000, RS32(o + 0xC) + 0x00100000, 0, 0);
                break;
            case 1:
                W32(o + 0x34, 0);
                Item_Drop(RS16(o + 0x18), RS32(o + 8) + 0x00100000, RS32(o + 0xC) + 0x00300000);
                Obj_Spawn(6, RS32(o + 8) + 0x00100000, RS32(o + 0xC) + 0x00300000, 0, 0);
                break;
            }
        }
        if (R16(o + 2) & 8) {                                       /* halves flying apart */
            W32(o + 8, R32(o + 8) - R32(o + 0x30));
            W32(o + 0xC, R32(o + 0xC) + R32(o + 0x34));
            W32(h, R32(h) + R32(o + 0x30));
            W32(h + 4, R32(h + 4) + R32(o + 0x34));
            W32(o + 0x30, R32(o + 0x30) - 0x1800);
            if (RS32(o + 0x30) < 0) W32(o + 0x30, 0);
            W32(o + 0x34, R32(o + 0x34) + 0x5000);
            if (RS32(o + 0x34) > 0x00080000) W32(o + 0x34, 0x00080000);
            /* floors (A, B), landing height, bounce speed, spin per frame */
            int32_t fa, fb, land; uint32_t bvy; int spin;
            switch (R16(o + 0x12) & 0x1F) {
            case 0:  fa = 0x01A40000; fb = 0x01A40000; land = 0x01A00000; bvy = 0xFFFB0000u; spin = 2; break;
            case 1:  fa = 0x01B40000; fb = 0x01740000; land = 0x01700000; bvy = 0xFFFD0000u; spin = 1; break;
            case 2:  fa = 0x01C40000; fb = 0x01C40000; land = 0x01A00000; bvy = 0xFFFD0000u; spin = 1; break;
            default: goto draw;
            }
            if (layer_y(o + 0xC) > fa) W32(o + 0xC, (uint32_t)fa + 0x04000000u);
            if (layer_y(h + 4) > fb)   W32(h + 4, (uint32_t)fb + 0x04000000u);
            if (R16(o + 0x14) == 0) {                               /* falling */
                W8(h + 8, R8(h + 8) + 1);
                if (R8(h + 8) > 0x40) W8(h + 8, 0x40);
                W8(h + 9, R8(h + 8));
                if (layer_y(o + 0xC) > land) {                      /* hit the floor: bounce */
                    W32(o + 0x30, 0x00030000);
                    W32(o + 0x34, bvy);
                    W16(o + 0x14, 1);
                }
            } else {
                W8(h + 8, R8(h + 8) + spin);
                W8(h + 9, R8(h + 9) - spin);
                W16(o + 0x14, R16(o + 0x14) + 1);
            }
            if (R16(o + 0x14) >= 0x20) W16(o, 0xFFFF);
        }
    }
draw:
    if (R16(o) == 0xFFFF) return;
    OR16(o + 0x58, 0x4000);                                         /* we draw the parts */
    Anim_Tick(o + 0x58);
    W16(SPR + 0x0E, (R16(o + 0x14) >> 3) & 3);
    W16(SPR + 0x10, 0x0600);
    switch (R16(o + 0x12) & 0x1F) {
    case 0:
        W16(SPR + 6, 0xFFD0); W16(SPR + 8, 0xFFF0); W16(SPR + 0x0A, 0x1E);
        break;
    case 1:
        W16(SPR + 6, 0xFFF0); W16(SPR + 8, 0xFFD0); W16(SPR + 0x0A, 0x1F);
        OR16(SPR + 0x10, 0x1000);
        W32(SPR + 0x1C, (uint32_t)FixDiv((int32_t)((0x20u - R16(o + 0x14)) << 16), 0x00200000));
        break;
    case 2:
        W16(SPR + 6, 0xFFD0); W16(SPR + 8, 0xFFF0); W16(SPR + 0x0A, 0x1E);
        OR16(SPR + 0x10, 0x1000);
        W32(SPR + 0x1C, (uint32_t)FixDiv((int32_t)((0x20u - R16(o + 0x14)) << 16), 0x00200000));
        break;
    }
    W16(SPR + 2, ((R32(o + 8) - R32(CAM_X)) >> 16) - R16(SPR + 6));
    W16(SPR + 4, ((R32(o + 0xC) - R32(CAM_Y)) >> 16) - R16(SPR + 8));
    W32(SPR + 0x18, 0xFFFFFF80u);
    W16(SPR + 0x0C, 0);
    W16(SPR + 0x14, R8(h + 8));
    Sprite_Draw(SPR);                                               /* first half */
    switch (R16(o + 0x12) & 0x1F) {
    case 0:
        W16(SPR + 6, 0xFFD0); W16(SPR + 8, 0);
        break;
    case 1:
        W16(SPR + 6, 0); W16(SPR + 8, 0xFFD0);
        OR16(SPR + 0x10, 0x1000);
        W32(SPR + 0x1C, (uint32_t)FixDiv((int32_t)((0x40u - R16(o + 0x14)) << 16), 0x00400000));
        break;
    case 2:
        W16(SPR + 6, 0); W16(SPR + 8, 0xFFF0);
        OR16(SPR + 0x10, 0x1000);
        W32(SPR + 0x1C, (uint32_t)FixDiv((int32_t)((0x40u - R16(o + 0x14)) << 16), 0x00400000));
        break;
    }
    W16(SPR + 2, ((R32(h) - R32(CAM_X)) >> 16) - R16(SPR + 6));
    W16(SPR + 4, ((R32(h + 4) - R32(CAM_Y)) >> 16) - R16(SPR + 8));
    W16(SPR + 0, R16(SPR + 0) + 1);                                 /* next character */
    W16(SPR + 0x14, R8(h + 9));
    Sprite_Draw(SPR);                                               /* second half */
}

/* ======================================================= 0x1001 statue */
#define PIECE_TBL  0x060A9794u   /* 7 x {s16 dx, s16 dy, u16 frame, pad} */
#define HIT_ORDER  0x060A97CCu   /* s16[7]: piece knocked off by hit n */
#define FRAG_TBL   0x060A97DAu   /* 12 x {s16 dx, s16 dy, s16 angle, u16 frame} */

/* common sprite setup of the pieces (bank 0x2D) */
static void piece_sprite(uint32_t o, uint16_t fl)
{
    W16(SPR + 0x0E, 0);
    W16(SPR + 0x10, (R16(o + 2) & 0x8000) | fl);
}
/* `master` = slot of piece 0 */
static void piece_pos(uint32_t o, uint32_t master)
{
    W16(SPR + 0x0A, 0x2D);
    W16(SPR + 2, (R32(o + 8) - R32(CAM_X)) >> 16);
    W16(SPR + 4, (R32(o + 0xC) - R32(CAM_Y)) >> 16);
    if (!(R16(o + 2) & 8)) W16(SPR + 2, R16(SPR + 2) + R32(master + 0x30));   /* shakes with the master */
    W16(SPR + 0x0C, 0);
}
static void piece_angle(uint32_t o)
{
    W16(SPR + 0x14, R16(o + 0x14));
    if (R16(o + 2) & 0x8000) W16(SPR + 0x14, (uint32_t)-RS16(SPR + 0x14));
}
/* bounce on the floor after vy / -2; |vy| < 0.5 px stops */
static void piece_bounce(uint32_t o, uint32_t floorY)
{
    W32(o + 0xC, floorY);
    W32(o + 0x34, (uint32_t)sdiv(RS32(o + 0x34), -2));
    if (iabs(RS32(o + 0x34)) < 0x8000) W32(o + 0x34, 0);
}
static void piece_fall(uint32_t o, int32_t maxv)
{
    W32(o + 0x34, R32(o + 0x34) + 0x5000);
    if (RS32(o + 0x34) > maxv) W32(o + 0x34, (uint32_t)maxv);
    W32(o + 0xC, R32(o + 0xC) + R32(o + 0x34));
}
static void piece_move_x(uint32_t o)
{
    if (R16(o + 2) & 0x8000) W32(o + 8, R32(o + 8) - R32(o + 0x30));
    else                     W32(o + 8, R32(o + 8) + R32(o + 0x30));
}

static void Statue_Master(uint32_t o)                               /* 0x0604A6D8 */
{
    if (R16(o + 2) & 1) {                                           /* set up all 7 pieces */
        int32_t x0 = RS32(o + 8), y0 = RS32(o + 0xC);
        uint16_t mir = (R16(o + 0x12) & 0x10) ? 0x8000 : 0;
        for (int16_t i = 0; i < 7; i++) {
            uint32_t p = o + (uint32_t)(i * 0x78), t = PIECE_TBL + (uint32_t)i * 8;
            W16(p + 6, 1);
            Anim_Start(p + 0x58, 0x060A9400u, 0x060D1B18u, 0x10, RS16(p + 6));
            if (mir & 0x8000) W32(p + 8, (uint32_t)(RS16(t) * 0x10000 + x0));
            else              W32(p + 8, (uint32_t)(x0 - RS16(t) * 0x10000));
            W32(p + 0xC, (uint32_t)(y0 - RS16(t + 2) * 0x10000));
            W16(p + 0x5A, R16(t + 4));
            AND16(p + 2, 0xFFFE);
            OR16(p + 2, mir);
            W16(p + 0x14, 0);
            W16(p + 6, 0);
            W16(p + 0x1C, 0x4000);
            W16(p + 0x18, R16(o + 0x18));
            OR16(p + 0x12, 0x4000);
        }
    }
    if (screen_dist(o) > 0x02000000) return;
    if (!FROZEN) {
        Obj_PlayerContact(R16(o + 2) & 0x8000);                     /* solid */
        if (R16(o + 6) >= 6) Obj_SwordClang(o);
        Obj_BlockShurikens(o);
        if (R16(o + 6) < 6 && !(R16(o + 2) & 8) && Hitbox_Test(o, 0, PL, 2) != 0xFFFF) {
            Obj_Spawn(2, RS32(HIT_POINT), RS32(HIT_POINT + 4), 0, 0);
            OR16(o + 2, 8);
            W16(o + 0x14, 0);
            sub_06007CDE(0x104);
            W16(o + 6, R16(o + 6) + 1);
            OR16(o + (uint32_t)(RS16(HIT_ORDER + 2u * R16(o + 6)) * 0x78) + 2, 8);
            if (RS16(HIT_ORDER + 2u * R16(o + 6)) == 1) {           /* piece 1 falls with piece 2 */
                W16(o + 6, R16(o + 6) + 1);
                OR16(o + (uint32_t)(RS16(HIT_ORDER + 2u * R16(o + 6)) * 0x78) + 2, 8);
            }
            if (RS16(HIT_ORDER + 2u * R16(o + 6)) == 3) W16(o + 0x5A, 1);   /* damaged core */
        }
        W32(o + 0x30, 0);
        if (R16(o + 2) & 8) {                                       /* shake for 16 frames */
            W16(o + 0x14, R16(o + 0x14) + 1);
            W32(o + 0x30, (R16(o + 0x14) >> 2) & 1);
            if (R16(o + 0x14) > 0x10) { AND16(o + 2, 0xFFF7); W16(o + 0x14, 0); }
        }
    }
    W16(SPR + 0x0E, 0);
    W16(SPR + 0x10, (R16(o + 2) & 0x8000) | 0x0200);
    W16(SPR + 0x0A, 0x2D);
    W16(SPR + 2, (R32(o + 8) - R32(CAM_X)) >> 16);
    W16(SPR + 4, (R32(o + 0xC) - R32(CAM_Y)) >> 16);
    W16(SPR + 2, R16(SPR + 2) + R32(o + 0x30));
    W16(SPR + 0x0C, 0);
    W32(SPR + 0x18, 0xFFFFFF80u);
    Anim_Tick(o + 0x58);
}

static void Statue_Piece1(uint32_t o)                               /* 0x0604AA90 */
{
    if (screen_dist(o) > 0x02000000) return;
    if (!FROZEN && (R16(o + 2) & 8)) {
        piece_fall(o, 0x00080000);
        W32(o + 0x30, 0);
        if (layer_y(o + 0xC) < 0x01CE0000) {
            W16(o + 0x14, R16(o + 0x14) - 1);
            W32(o + 0x30, 0x00010000);
        } else {
            piece_bounce(o, 0x05CE0000u);
            if (R16(o + 0x14) > 0xFFC0) {
                W16(o + 0x14, R16(o + 0x14) - 4);
                AND16(o + 0x14, 0xFFFE);
                W32(o + 0x30, 0x8000);
            }
        }
        piece_move_x(o);
    }
    piece_sprite(o, 0x1600);
    W32(SPR + 0x1C, (uint32_t)FixDiv((int32_t)(((uint32_t)R16(o + 0x14) + 0x70) << 16), 0x00700000));
    piece_pos(o, o - 0x78);
    piece_angle(o);
    W32(SPR + 0x18, 0xFFFFFF7Fu);
    Anim_Tick(o + 0x58);
}

static void Statue_Piece2(uint32_t o)                               /* 0x0604ACBC */
{
    if (screen_dist(o) > 0x02000000) return;
    if (!FROZEN && (R16(o + 2) & 8)) {
        piece_fall(o, 0x00080000);
        W32(o + 0x30, 0);
        if (layer_y(o + 0xC) < 0x01CE0000) {
            W16(o + 0x14, R16(o + 0x14) + 1);
            W32(o + 0x30, 0xFFFF8000u);                             /* mov.w #0x8000: sign-extended */
        } else {
            piece_bounce(o, 0x05CE0000u);
            if (R16(o + 0x14) < 0x40) {
                W16(o + 0x14, R16(o + 0x14) + 4);
                AND16(o + 0x14, 0xFFFE);
                W32(o + 0x30, 0xFFFF0000u);
            }
        }
        piece_move_x(o);
    }
    piece_sprite(o, 0x1600);
    W32(SPR + 0x1C, (uint32_t)FixDiv((int32_t)((0x50u - R16(o + 0x14)) << 16), 0x00500000));
    piece_pos(o, o - 0xF0);
    piece_angle(o);
    W32(SPR + 0x18, 0xFFFFFF80u);
    Anim_Tick(o + 0x58);
}

static void Statue_Piece3(uint32_t o)                               /* 0x0604B194 */
{
    if (screen_dist(o) > 0x02000000) return;
    if (!FROZEN) {
        if (screen_dist(o) > 0x02000000) return;
        if (R16(o + 2) & 8) {
            piece_fall(o, 0x00080000);
            W32(o + 0x30, 0);
            if (R16(o + 0x14) == 0) W32(o + 0x34, 0xFFFB0000u);     /* initial hop */
            if (layer_y(o + 0xC) < 0x01C80000) {
                if (R16(o + 0x14) < 0x80) W16(o + 0x14, R16(o + 0x14) + 2);
                W32(o + 0x30, 0xFFFEC000u);
            } else {
                piece_bounce(o, 0x05C80000u);
                if (R16(o + 0x14) < 0x80) {
                    W16(o + 0x14, R16(o + 0x14) + 8);
                    AND16(o + 0x14, 0xFFFE);
                    W32(o + 0x30, 0xFFFF0000u);
                }
            }
            piece_move_x(o);
        }
    }
    piece_sprite(o, 0x0600);
    piece_pos(o, o - 0x168);
    piece_angle(o);
    W32(SPR + 0x18, 0xFFFFFF80u);
    Anim_Tick(o + 0x58);
}

static void Statue_Piece4(uint32_t o)                               /* 0x0604AEF4 */
{
    if (screen_dist(o) > 0x02000000) return;
    if (!FROZEN && (R16(o + 2) & 8)) {
        W16(o + 0x14, R16(o + 0x14) + 1);
        if (R16(o + 0x14) >= 0x10) { W16(o, 0xFFFF); return; }
    }
    W16(SPR + 0x10, (R16(o + 2) & 0x8000) | 0x0600);
    W16(SPR + 0x0A, 0x2D);
    W16(SPR + 0x0C, 0);
    W32(SPR + 0x18, 0xFFFFFF80u);
    if (!(R16(o + 2) & 8)) {
        W16(SPR + 0x0E, 0);
        W16(SPR + 2, (R32(o + 8) - R32(CAM_X)) >> 16);
        W16(SPR + 4, (R32(o + 0xC) - R32(CAM_Y)) >> 16);
        W16(SPR + 0x14, 0);
        Anim_Tick(o + 0x58);
        return;
    }
    for (int16_t i = 0; i < 12; i++) {                              /* burst into 12 fragments */
        uint32_t e = FRAG_TBL + (uint32_t)i * 8;
        uint32_t t = R16(o + 0x14);
        W16(SPR + 0x0E, t >> 2);
        W16(SPR + 0x14, t << 3);
        W16(SPR + 2, (R32(o + 8) - R32(CAM_X)) >> 16);
        W16(SPR + 4, (R32(o + 0xC) - R32(CAM_Y)) >> 16);
        int32_t v = FixMul(Cos8(RS16(e + 4)), (int32_t)((t << 2) << 16)) + RS16(e) * 0x10000;
        if (R16(o + 2) & 0x8000) v = -v;
        W16(SPR + 2, R16(SPR + 2) + ((uint32_t)v >> 16));
        v = (int32_t)((uint32_t)FixMul(Sin8(RS16(e + 4)), (int32_t)((t << 2) << 16)) - ((t * t) << 13));
        W16(SPR + 4, R16(SPR + 4) - ((uint32_t)v >> 16));
        W16(SPR + 4, R16(SPR + 4) + R16(e + 2));
        OR16(SPR + 0x10, 0x1800);
        int32_t s = FixDiv((int32_t)((0x20u - t) << 16), 0x00200000);
        W32(SPR + 0x1C, (uint32_t)s);
        W32(SPR + 0x20, (uint32_t)s);
        W16(o + 0x5A, R16(e + 6));
        Anim_Tick(o + 0x58);
    }
}

static void Statue_Piece5(uint32_t o)                               /* 0x0604B3F2 */
{
    if (screen_dist(o) > 0x02000000) return;
    if (!FROZEN && (R16(o + 2) & 8)) {                              /* explodes */
        W32(o + 0x30, 1);
        if (R16(o + 2) & 0x8000) W32(o + 0x30, 0xFFFFFFFFu);
        int32_t d = RS32(o + 0x30), x = RS32(o + 8), y = RS32(o + 0xC);
        Obj_Spawn(6, x - d * 0x140000, y, 0, 0);
        Obj_Spawn(6, x + d * 0x140000, y, 0, 4);
        Obj_Spawn(6, x - d * 0x100000, y - 0x100000, 0, 12);
        Obj_Spawn(6, x + d * 0x100000, y - 0x100000, 0, 8);
        Item_Drop(RS16(o + 0x18), x, y + 0x100000);
        W16(o, 0xFFFF);
        sub_06007CDE(0x121);
        return;
    }
    piece_sprite(o, 0x0600);
    piece_pos(o, o - 0x258);
    W16(SPR + 0x14, 0);
    W32(SPR + 0x18, 0xFFFFFF80u);
    if (R16(o - 0x258 + 6) >= 5) W32(SPR + 0x18, 0x80);            /* cracked after the 4th hit */
    Anim_Tick(o + 0x58);
}

static void Statue_Piece6(uint32_t o)                               /* 0x0604B624 */
{
    if (screen_dist(o) > 0x02000000) return;
    if (!FROZEN && (R16(o + 2) & 8)) {
        piece_fall(o, 0x00040000);
        if (layer_y(o + 0xC) < 0x01D00000) {
            W16(o + 0x48, R16(o + 0x48) + 1);                       /* tilts while falling */
        } else {
            W32(o + 0xC, 0x05D00000u);
            if (R16(o + 0x48) != 0) {
                W16(o + 0x48, R16(o + 0x48) - 1);
                AND16(o + 0x48, 0xFFFE);
            }
        }
    }
    piece_sprite(o, 0x0600);
    piece_pos(o, o - 0x2D0);
    W16(SPR + 0x14, R16(o + 0x48));
    W32(SPR + 0x18, 0xFFFFFF80u);
    Anim_Tick(o + 0x58);
}

void ObjHandler_1001(void)                                          /* 0x0604A67C */
{
    uint32_t o = CUR;
    switch (R16(o + 0x12) & 0xF) {
    case 0: Statue_Master(o); break;
    case 1: Statue_Piece1(o); break;
    case 2: Statue_Piece2(o); break;
    case 3: Statue_Piece3(o); break;
    case 4: Statue_Piece4(o); break;
    case 5: Statue_Piece5(o); break;
    case 6: Statue_Piece6(o); break;
    }
}
