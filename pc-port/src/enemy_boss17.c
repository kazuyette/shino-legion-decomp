/* Enemy type 0x0017: stage 8-2 boss (index 15, file STAGE8_2), a jointed giant built from
 * up to 7 sprite segments (head/arms/mouth...) around the body sprite.
 * Loader 0x06076024 (0x50 chars of 0x060DEB78, six 16-colour palettes 0x060DEDF8 / F58 /
 * F78 / F38 / F98 / FB8 at banks +0..+5, bank +6 = bank +0 darkened by 4), handler
 * 0x0607619A, init 0x06076D4C (fixed home 0x528 / 0x530, facing left, Sho's input locked
 * for the intro), action setter 0x0607841A, move pick 0x06078348 (facing left: rand % 3 ->
 * laser 0x1001 / low laser 0x1003 / dash 9; facing right: rand % 2 -> dash 9 / grab 0xF;
 * never the same twice except 9), combat 0x06077F7A, draw 0x06078004 (segments 0x060F6938,
 * 16 bytes {x, y, angle, anim, priority}; targets 0x060F69D8, previous 0x060F6898, parent
 * 0x060F6A78, offsets 0x060F6A8C / 0x060F6AB4, count 0x060F6ADC; joint placement 0x06079C70,
 * per-action layout 0x06079D24, easing 0x06077D60 over 0x060F6894 frames, angle step
 * A.BIN-style signed divide 0x060A51DC), palette 0x0607A3AA (0x060DEFD8[+0x48], blinking
 * 8/9, 0x060F6ADE = current), bubbles 0x0607A4E4 (3 anim states 0x060F6AE0, anim 0x22),
 * dash overlay 0x0607A784 (0x060F6B48), afterimages 0x0607A934 (8 records of 0x1E4 bytes
 * at 0x060F6B6C, counts 0x060F7A8C), death break-up 0x06079AE4 (0x060B795E poses) /
 * 0x0607AC34 (effect 0x12) / 0x0607ACB6 (36 explosions), hurt flash A.BIN-side shared
 * 0x0604990A (Enemy_HurtFlash here). HP 0x060B7958[difficulty]; boss bar 0x06061B58.
 * Actions: 0 idle (intro: 8 palette steps then flags4 bit 1 = bubbles), 1/2 turn, 9 dash
 * (to the screen edge, then 10 run, 0xB turn back), 0xF..0x13 / 0x24 / 0x25 grab arm
 * (0x10 stretch: scale 0x060F7A9C; 0x11 reach with projectile 0x17 (rotating beam);
 * 0x24 swing sending shards 0x103C; 0x25 recover), 0x1001 / 0x1003 laser (projectile 0x16
 * from the mouth, +0x50 shots), 0x1002 close the mouth, 0x26 dying (explosions), 0x19
 * falling apart (25 poses), 0x27 gone -> fade to white, stage state |= 10.
 * Projectiles 0x15 (0x060775E4 hand hit box), 0x16 (0x06076ED8 laser: grows 1.5/frame,
 * line test against Sho's boxes, breaks on walls: shards / rocks 0x0605E65C -> projectile
 * 8 (0x0605E0E0, ported here) from the ceiling), 0x17 (0x060776FC rotating beam); effects
 * 0x12 (0x06077B24 explosion -> 0x13) and 0x13 (0x06077C4C smoke). */
#include "enemy_more.h"
#include "anim.h"
#include "sprite.h"
#include "objects.h"
#include "collision.h"
#include "player_ext.h"
#include "fixmath.h"
#include "fade.h"
#include "hud.h"
#include "fx.h"
#include "sfx.h"
#include "objhandlers_f.h"
#include "objhandlers_d.h"

#define B17_DEFS 0x060B5D98u
#define B17_SCR  0x060DF284u
#define SG(i)    (0x060F6938u + 16u * (uint32_t)(i))     /* segments {x, y, angle, anim, prio} */
#define TG(i)    (0x060F69D8u + 16u * (uint32_t)(i))     /* targets */
#define PV(i)    (0x060F6898u + 16u * (uint32_t)(i))     /* previous frame */
#define LINK(i)  (0x060F6A78u + 2u * (uint32_t)(i))      /* s16 parent (-1: placed directly) */
#define OFX(i)   (0x060F6A8Cu + 4u * (uint32_t)(i))
#define OFY(i)   (0x060F6AB4u + 4u * (uint32_t)(i))
#define NSEG     0x060F6ADCu
#define K_PAL    0x060F6ADEu
#define K_STEPS  0x060F6894u     /* s16 easing frames left */
#define K_BUB    0x060F6AE0u     /* 3 (4 reset) anim states 0x20 */
#define K_BUBT   0x060F6B40u
#define K_BUBV   0x060F6B42u
#define K_IDLE   0x060F6B44u
#define K_OVL    0x060F6B48u     /* dash / stretch overlay anim */
#define K_LAST   0x060F6B68u
#define K_AIMG   0x060F6B6Au
#define K_REC(k) (0x060F6B6Cu + 0x1E4u * (uint32_t)(k))  /* afterimage records */
#define K_RECN(k) (0x060F7A8Cu + 2u * (uint32_t)(k))
#define K_SCALE  0x060F7A9Cu     /* s32 arm stretch */
#define K_FRAME  0x060F7AA0u
#define K_SWING  0x060F7AA2u
#define K_PALT   0x060F7AA4u
#define K_HP     0x060F7AA6u
#define LOC      SCRATCH_ANIM    /* the original's on-stack anim state */
#define PROJ(i)  (PROJECTILES + (uint32_t)((int16_t)(i) * 0x40))
#define C_FLAGS  0x060EA638u     /* stage control (0x80: screen shaking) */
#define VBL      R16(0x060143EAu)

static int16_t smod(int32_t a, int32_t b) { return (int16_t)(b ? a % b : 0); }   /* A.BIN 0x0600A604 */
static int16_t bank(void) { return COLOR_BANK(SLOT); }

/* ---------------------------------------------------------------- 0x0604990A */
/* Hurt flash shared by the bosses (enemy code 0x0604990A): state 0x060F2068 (frame),
 * 0x060F2064 colour bank, 0x060F2066 colour count. mode 0 reset (bank 0x50, 256 colours),
 * 1 start, 2 white (keep == 0) or copy `keep`, 3 restore from `pal`, 4 tick for n frames:
 * white / restored every other frame; 1 while running, 2 when it ends. */
int Enemy_HurtFlash(int16_t mode, int16_t n, uint32_t keep, uint32_t pal)
{
    if (mode == 0) { W16(0x060F2068u, 0); W16(0x060F2064u, 0x50); W16(0x060F2066u, 0x100); return 0; }
    if (mode == 1) { W16(0x060F2068u, 1); return 0; }
    if (mode == 2) {
        if (keep == 0) {
            uint32_t c = (uint32_t)R16(0x060F2064u) * 0x20u + SHADOW_CRAM;
            for (int16_t k = RS16(0x060F2066u); k != 0; k--, c += 2) W16(c, 0x7FFF);
            W16(0x060250A8u, 1);
        } else Pal_Copy((R16(0x060F2064u) & 0xFFF) << 4, RS16(0x060F2066u), keep);
        return 0;
    }
    if (mode == 3) { Pal_Copy((R16(0x060F2064u) & 0xFFF) << 4, RS16(0x060F2066u), pal); return 0; }
    if (mode != 4) return 0;
    int16_t f = RS16(0x060F2068u);
    if (f == 0) return 0;
    if (f <= n) {
        int16_t c = (int16_t)(f + 1);
        W16(0x060F2068u, (uint16_t)c);
        if ((((int32_t)f - 1) & 2) == 0) { if ((((int32_t)c - 2) & 1) == 0) Enemy_HurtFlash(2, n, keep, pal); }
        else if ((((int32_t)c - 2) & 1) == 0) Enemy_HurtFlash(3, n, keep, pal);
        return 1;
    }
    W16(0x060F2068u, 0);
    Enemy_HurtFlash(3, n, keep, pal);
    return 2;
}

/* ---------------------------------------------------------------- segments */

static void seg_cpy(uint32_t d, uint32_t s)                 /* x, y, angle */
{
    W32(d, R32(s)); W32(d + 4, R32(s + 4)); W16(d + 8, R16(s + 8));
}

/* 0x06079C70: dst = joint at (a, b) of `prev` (copied first, as the original passes it by
 * value), turned by prev's angle; a is mirrored with the boss. */
static void seg_place(uint32_t dst, uint32_t prev, int32_t a, int32_t b)
{
    int32_t px = RS32(prev), py = RS32(prev + 4);
    int16_t ang = RS16(prev + 8);
    int32_t s = Sin8(ang), c = Cos8(ang);
    int32_t t = FixMul(a, c), u = FixMul(b, s);
    if (R16(O + 2) & 0x8000) t = -t;
    W32(dst, (uint32_t)(t + px + u));
    t = FixMul(a, -s); u = FixMul(b, c);
    if (R16(O + 2) & 0x8000) t = -t;
    W32(dst + 4, (uint32_t)(t + py + u));
}

static int16_t sdiv16(int16_t a, int16_t b)                 /* SHINOBI 0x060A51DC */
{
    if (b == 0) { W32(0x06027348u, 0x44E); return 0; }
    return (int16_t)(a / b);
}

static void Seg_Ease(void)                                  /* 0x06077D60 */
{
    if (FROZEN) return;
    for (int16_t i = 0; i < RS16(NSEG); i++) {
        int16_t l = RS16(LINK(i));
        if (l == -1) continue;
        int32_t rx = 0, ry = 0; int16_t ra = 0;
        if (RS16(K_STEPS) > 0) {
            const uint32_t PAR = 0x060FFE60u;               /* PC scratch: the parent copy */
            seg_cpy(PAR, SG(l));
            const uint32_t RES = 0x060FFE70u;
            seg_place(RES, PAR, RS32(OFX(i)), RS32(OFY(i)));   /* angle of the parent copy */
            rx = RS32(RES); ry = RS32(RES + 4);
            if (RS16(SG(i) + 8) == RS16(TG(i) + 8)) ra = RS16(SG(i) + 8);
            else {
                int16_t d0 = (int16_t)((R16(TG(i) + 8) & 0xFF) - (R16(SG(i) + 8) & 0xFF));
                int16_t c[3] = { d0, (int16_t)(d0 - 0x100), (int16_t)(d0 + 0x100) };
                int16_t best = 0x100;
                for (int k = 0; k < 3; k++) if (EM_Abs(c[k]) < EM_Abs(best)) best = c[k];
                ra = (int16_t)(RS16(SG(i) + 8) + sdiv16(best, RS16(K_STEPS)));
            }
        }
        if (RS16(K_STEPS) < 1) { rx = RS32(TG(i)); ry = RS32(TG(i) + 4); ra = RS16(TG(i) + 8); }
        seg_cpy(PV(i), SG(i));
        W32(SG(i), (uint32_t)rx); W32(SG(i) + 4, (uint32_t)ry); W16(SG(i) + 8, (uint16_t)ra);
    }
    if (RS16(K_STEPS) > 0) W16(K_STEPS, R16(K_STEPS) - 1);
}

/* 0x06079D24: segments that follow the body's frame (tables by +0x5A) */
static int32_t arm_len(void)
{
    int32_t l = 0x2D0000;
    if (FixMul(RS32(K_SCALE), 0x80000) + 0x1C0000 > 0x2D0000) l = FixMul(RS32(K_SCALE), 0x80000) + 0x1C0000;
    return l;
}
static void tg_from_sg(int n4same)
{
    if (RS16(K_STEPS) >= 1) return;
    seg_cpy(TG(1), SG(1)); seg_cpy(TG(2), SG(2)); seg_cpy(TG(3), SG(3));
    if (n4same) { W32(TG(4), R32(SG(2))); W32(TG(4) + 4, R32(SG(2) + 4)); W16(TG(4) + 8, R16(SG(1) + 8)); }
    else seg_cpy(TG(4), SG(4));
}
static void Seg_Layout(void)
{
    uint32_t o = O;
    uint16_t a = R16(o + 6), f = R16(o + 0x5A);
    switch (a) {
    case 9: case 10: case 11: {
        uint32_t t = a == 9 ? 0x060B7810u : a == 10 ? 0x060B7848u : 0x060B7860u;
        int32_t dx = RS32(t + 8u * f);
        W32(SG(1), (uint32_t)((R16(o + 2) & 0x8000) ? RS32(o + 8) - dx : RS32(o + 8) + dx));
        W32(SG(1) + 4, (uint32_t)(RS32(o + 0xC) + RS32(t + 8u * f + 4)));
        seg_place(SG(2), SG(1), (int32_t)0xFFF70000, (int32_t)0xFFD80000);
        seg_cpy(SG(3), SG(2));
        seg_place(SG(4), SG(3), 0x2D0000, 0);
        tg_from_sg(0);
        break;
    }
    case 0xF: case 0x13: {
        uint32_t t = a == 0xF ? 0x060B78A0u : 0x060B7900u;
        W32(SG(1), (uint32_t)(RS32(o + 8) + RS32(t + 8u * f)));
        W32(SG(1) + 4, (uint32_t)(RS32(o + 0xC) + RS32(t + 8u * f + 4)));
        W32(SG(3), (uint32_t)(RS32(o + 8) + RS32(t + 8u * f)));
        W32(SG(3) + 4, (uint32_t)(RS32(o + 0xC) + RS32(t + 8u * f + 4)));
        seg_place(SG(2), SG(1), 0x2D0000, 0);
        W32(SG(4), R32(SG(2))); W32(SG(4) + 4, R32(SG(2) + 4)); W16(SG(4) + 8, R16(SG(1) + 8));
        tg_from_sg(1);
        break;
    }
    case 0x10:
        seg_place(SG(4), SG(1), 0x1C0000, (int32_t)0xFFFF0000);
        seg_place(SG(2), SG(1), arm_len(), 0);
        if (RS16(o + 0x4E) != -1) { W32(PROJ(RS16(o + 0x4E)) + 8, R32(SG(2))); W32(PROJ(RS16(o + 0x4E)) + 0xC, R32(SG(2) + 4)); }
        break;
    case 0x11: case 0x12: case 0x24: case 0x25:
        W32(SG(1), (uint32_t)(RS32(o + 8) - 0x50000)); W32(SG(1) + 4, (uint32_t)(RS32(o + 0xC) - 0x4F0000));
        W32(SG(3), (uint32_t)(RS32(o + 8) - 0x50000)); W32(SG(3) + 4, (uint32_t)(RS32(o + 0xC) - 0x4F0000));
        seg_place(SG(4), SG(1), 0x1C0000, (int32_t)0xFFFF0000);
        W16(SG(4) + 8, R16(SG(1) + 8));
        seg_place(SG(2), SG(1), arm_len(), 0);
        if (RS16(o + 0x4E) != -1) { W32(PROJ(RS16(o + 0x4E)) + 8, R32(SG(2))); W32(PROJ(RS16(o + 0x4E)) + 0xC, R32(SG(2) + 4)); }
        if (RS16(o + 0x54) != -1) {
            uint32_t p = PROJ(RS16(o + 0x54));
            W32(p + 8, R32(SG(1))); W32(p + 0xC, R32(SG(1) + 4)); W16(p + 0x14, R16(SG(1) + 8));
        }
        break;
    default: break;
    }
}

/* the 7 resting joint offsets (0x06079AE4 / 0x0607841A(0x19)) */
static const int32_t JX[7] = { 0, 0x40000, 0x30000, (int32_t)0xFFFE0000, 0x10000, (int32_t)0xFFF60000, (int32_t)0xFFF60000 };
static const int32_t JY[7] = { 0, (int32_t)0xFFF40000, (int32_t)0xFFE30000, (int32_t)0xFFE10000, (int32_t)0xFFE50000, (int32_t)0xFFF30000, (int32_t)0xFFF30000 };

static void Seg_DeathPose(int16_t p)                        /* 0x06079AE4 */
{
    uint32_t t = 0x060B795Eu + 16u * (uint32_t)p;
    W16(K_STEPS, R16(t));
    int neg = (R16(O + 2) & 0x8000) != 0;
    W16(SG(0) + 8, (uint16_t)(neg ? -RS16(t + 2) : RS16(t + 2)));
    for (int i = 1; i < 7; i++) W16(TG(i) + 8, (uint16_t)(neg ? -RS16(t + 2 + 2u * i) : RS16(t + 2 + 2u * i)));
    seg_place(TG(1), SG(0), JX[1], JY[1]);
    for (int i = 2; i <= 4; i++) seg_place(TG(i), TG(i - 1), JX[i], JY[i]);
    seg_place(TG(5), TG(4), JX[5], JY[5]);
    seg_place(TG(6), TG(4), JX[6], JY[6]);
}

/* ---------------------------------------------------------------- setter 0x0607841A */
static void set_tg_ofs(void)
{
    W32(OFX(1), 0xFFFA0000u); W32(OFY(1), 0xFF910000u);
    W32(OFX(2), 0xFFFA0000u); W32(OFY(2), 0xFF910000u);
    W32(OFX(3), 0x2D0000u);   W32(OFY(3), 0);
}
static void seg_set(uint32_t s, int16_t ang, int16_t anim, int16_t prio)
{
    W16(s + 8, (uint16_t)ang); W16(s + 0xA, (uint16_t)anim); W16(s + 0xC, (uint16_t)prio);
}
static void tg_xy(int i) { W32(TG(i), R32(SG(i))); W32(TG(i) + 4, R32(SG(i) + 4)); }
static void aimg_clear(void) { for (int k = 0; k < 8; k++) W16(K_RECN(k), 0); }
static void laser_aim(int low)
{
    int a = Atan8((int16_t)(R32(SG(1)) >> 16), (int16_t)(R32(SG(1) + 4) >> 16), (int16_t)(R32(PL + 8) >> 16),
                  low ? 0x410 : (int16_t)((uint32_t)(RS32(PL + 0xC) - 0x200000) >> 16));
    W16(TG(1) + 8, (uint16_t)(a - 0x80));
    set_tg_ofs();
}

static void B17_Set(int16_t a)
{
    uint32_t o = O;
    switch ((uint16_t)a) {
    case 0:
        W16(o + 6, 0);
        W16(K_IDLE, 0); W16(K_STEPS, 0);
        W32(SG(0), R32(o + 8)); W32(SG(0) + 4, R32(o + 0xC));
        W16(SG(0) + 8, 0); W16(SG(0) + 0xC, 0); W16(TG(0) + 8, 0);
        W16(LINK(0), 0xFFFF);
        W32(TG(0), R32(SG(0))); W32(TG(0) + 4, R32(SG(0) + 4));
        W16(NSEG, 4);
        if (!(R16(o + 2) & 0x8000)) {
            W32(SG(1), (uint32_t)(RS32(o + 8) - 0x60000)); W32(SG(1) + 4, (uint32_t)(RS32(o + 0xC) - 0x6F0000));
            seg_set(SG(1), 0xE0, 3, 1); W16(TG(1) + 8, 0xE0); W16(LINK(1), 0);
            W16(SG(0) + 0xA, 0);
            tg_xy(1);
            seg_place(SG(2), SG(1), 0x2D0000, 0);
            seg_set(SG(2), 0, 5, 2); tg_xy(2); W16(TG(2) + 8, 0); W16(LINK(2), 1);
            W32(SG(3), (uint32_t)(RS32(o + 8) - 0x60000)); W32(SG(3) + 4, (uint32_t)(RS32(o + 0xC) - 0x6F0000));
            seg_set(SG(3), 0xE0, 6, -1); W16(TG(3) + 8, 0xE0); W16(LINK(3), 0); tg_xy(3);
        } else {
            W32(SG(1), (uint32_t)(RS32(o + 8) + 0x60000)); W32(SG(1) + 4, (uint32_t)(RS32(o + 0xC) - 0x6F0000));
            seg_set(SG(1), 0x20, 6, 2); W16(TG(1) + 8, 0x20); W16(LINK(1), 0);
            W32(SG(2), (uint32_t)(RS32(o + 8) + 0x60000)); W32(SG(2) + 4, (uint32_t)(RS32(o + 0xC) - 0x6F0000));
            seg_set(SG(2), 0x20, 3, -1); W16(TG(2) + 8, 0x20); W16(LINK(2), 0);
            W16(SG(0) + 0xA, 0);
            tg_xy(1); tg_xy(2);
            seg_place(SG(3), SG(2), 0x2D0000, 0);
            seg_set(SG(3), 0, 5, -2); tg_xy(3); W16(TG(3) + 8, 0); W16(LINK(3), 2);
        }
        break;
    case 1: case 2:
        sub_06007CDE(0x210);
        if (a == 2) AND16(o + 2, 0x7FFF);
        W16(o + 6, (uint16_t)a);
        W16(NSEG, 1); W16(K_STEPS, 0); W16(LINK(0), 0xFFFF); W16(SG(0) + 0xA, (uint16_t)a);
        break;
    case 9:
        if (!(R16(o + 2) & 0x8000)) { if (RS32(PL + 8) < RS32(o + 8)) { B17_Set(1); W16(o + 0x52, 9); return; } }
        else if (RS32(o + 8) < RS32(PL + 8)) { B17_Set(2); W16(o + 0x52, 9); return; }
        sub_06007CDE(0x211);
        W16(o + 6, 9);
        W16(NSEG, 5); W16(K_STEPS, 0xF); W16(LINK(0), 0xFFFF);
        seg_set(SG(1), 0, 0xC, -1);
        tg_xy(1); W16(LINK(1), 0);
        if (!(R16(o + 2) & 0x8000)) {
            W16(TG(1) + 8, 0xF0); W16(SG(2) + 8, 0xE0); W16(SG(2) + 0xC, 0xFFFE);
            W16(SG(3) + 8, 0xE0); W16(SG(3) + 0xC, 2); W16(SG(4) + 0xC, 3);
        } else {
            W16(TG(1) + 8, 0x10); W16(SG(2) + 8, 0x20); W16(SG(2) + 0xC, 2);
            W16(SG(3) + 8, 0x20); W16(SG(3) + 0xC, 0xFFFE); W16(SG(4) + 0xC, 0xFFFD);
        }
        W16(LINK(4), 3); W16(LINK(3), 1); W16(LINK(2), 1);
        W16(TG(4) + 8, 0); tg_xy(4);
        W16(TG(3) + 8, 0); tg_xy(3);
        W16(TG(2) + 8, 0); tg_xy(2);
        W16(SG(4) + 0xA, 5); W16(SG(4) + 8, 0); W16(SG(3) + 0xA, 3); W16(SG(2) + 0xA, 6);
        W16(K_AIMG, 0);
        W16(SG(0) + 0xA, 9);
        aimg_clear();
        break;
    case 10:
        W16(o + 6, 10); W16(SG(0) + 0xA, 10);
        sub_06007CDE(0x113);
        break;
    case 0xB:
        sub_06007CDE(0x20E);
        W16(o + 6, 0xB);
        W16(TG(1) + 8, 0); W16(K_STEPS, 0x14);
        if (!(R16(o + 2) & 0x8000)) { W16(TG(2) + 8, 0xE0); W16(TG(3) + 8, 0xE0); }
        else { W16(TG(2) + 8, 0x20); W16(TG(3) + 8, 0x20); }
        W16(SG(0) + 0xA, 0xB);
        break;
    case 0xF:
        sub_06007CDE(0x20E); sub_06007CDE(0x211);
        W16(o + 6, 0xF);
        W16(NSEG, 5);
        W16(o + 0x50, (uint16_t)(smod((int32_t)(((uint32_t)sub_0602DDD2() >> 6) & 0x7FFF), 5) + 1));
        W32(K_SCALE, 0x10000); W16(K_STEPS, 0x19);
        W32(SG(0), R32(o + 8)); W32(SG(0) + 4, R32(o + 0xC));
        W16(TG(0) + 8, R16(SG(0) + 8)); W16(LINK(0), 0xFFFF);
        seg_set(SG(1), 0xE0, 3, 1); tg_xy(1); W16(TG(1) + 8, 0x40); W16(LINK(1), 0);
        seg_set(SG(2), 0, 5, 2); tg_xy(2); W16(TG(2) + 8, 0); W16(LINK(2), 1);
        W16(SG(3) + 8, 0xE0); W16(SG(3) + 0xA, 6); W16(SG(3) + 0xC, 0xFFFF);
        tg_xy(3); W16(TG(3) + 8, 0xE0); W16(LINK(3), 0);
        seg_set(SG(4), 0xE0, 4, 0); tg_xy(4); W16(TG(4) + 8, 0xE0); W16(LINK(4), 1);
        W16(SG(0) + 0xA, 0xF);
        W32(TG(0), R32(SG(0))); W32(TG(0) + 4, R32(SG(0) + 4));
        break;
    case 0x10:
        W16(o + 6, 0x10); W16(SG(0) + 0xA, 0x10);
        if (RS16(o + 0x4E) == -1) W16(o + 0x4E, (uint16_t)Projectile_Spawn(0x15, 0, 0, 0, 0));
        if (RS16(o + 0x4E) != -1) { W32(PROJ(RS16(o + 0x4E)) + 8, R32(SG(2))); W32(PROJ(RS16(o + 0x4E)) + 0xC, R32(SG(2) + 4)); }
        break;
    case 0x11:
        sub_06007CDE(0x20E);
        W16(o + 6, 0x11); W16(K_AIMG, 0); W16(SG(0) + 0xA, 0x11);
        aimg_clear();
        break;
    case 0x12: W16(o + 6, 0x12); W16(SG(0) + 0xA, 0x12); break;
    case 0x13:
        sub_06007CDE(0x211);
        W16(o + 6, 0x13); W16(K_STEPS, 0x1E); W16(TG(1) + 8, 0xE0); W16(SG(0) + 0xA, 0x13);
        break;
    case 0x19: {
        static const int32_t ex[13] = { 0, 0x120000, -0x160000, 0xC0000, -0xE0000, 0, 0, 0x120000, -0x140000, 0x160000, -0x180000, 0x180000, -0x180000 };
        static const int32_t ey[13] = { -0x10000, -0x80000, -0x100000, -0x1C0000, -0x220000, -0x2E0000, -0x390000, -0x400000, -0x4B0000, -0x560000, -0x600000, -0x6E0000, -0x800000 };
        static const int ed[13] = { 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2 };
        W16(o + 6, 0x19);
        AND16(o + 4, 0xFFFD);
        for (int k = 0; k < 13; k++) Obj_Spawn(0, RS32(o + 8) + ex[k], RS32(o + 0xC) + ey[k], 0, ed[k]);
        sub_06007CDE(0x209);
        if (RS16(o + 0x4E) != -1) { W16(PROJ(RS16(o + 0x4E)), 0xFFFF); W16(o + 0x4E, 0xFFFF); }
        if (RS16(o + 0x54) != -1) { W16(PROJ(RS16(o + 0x54)), 0xFFFF); W16(o + 0x54, 0xFFFF); }
        W16(NSEG, 7);
        W16(o + 0x4C, 0); W16(o + 0x56, 0);
        W32(SG(0), R32(o + 8)); W32(SG(0) + 4, R32(o + 0xC));
        W16(LINK(0), 0xFFFF);
        W16(SG(1) + 0xC, 0); W16(SG(1) + 0xA, 0x18); W16(LINK(1), 0);
        W16(SG(2) + 0xC, 0xFFFF); W16(SG(2) + 0xA, 0x17); W16(LINK(2), 1);
        W16(SG(3) + 0xC, 0xFFFE); W16(SG(3) + 0xA, 0x16); W16(LINK(3), 2);
        W16(SG(4) + 0xC, 0); W16(SG(4) + 0xA, 0x15); W16(LINK(4), 3);
        W32(SG(5) + 4, (uint32_t)(RS32(SG(4) + 4) - 0xD0000)); W16(SG(5) + 0xA, 0x1A); W16(LINK(5), 4);
        W32(SG(6) + 4, (uint32_t)(RS32(SG(4) + 4) - 0xD0000)); W16(SG(6) + 0xA, 0x1B); W16(LINK(6), 4);
        int16_t p = RS16(o + 0x56);
        W16(SG(0) + 0xA, 0x19);
        W16(o + 0x56, (uint16_t)(p + 1));
        Seg_DeathPose(p);
        if (!(R16(o + 2) & 0x8000)) { W16(SG(5) + 8, 0xE0); W16(SG(5) + 0xC, 0); W16(SG(6) + 8, 0xE0); W16(SG(6) + 0xC, 0xFFFD); }
        else { W16(SG(5) + 8, 0x20); W16(SG(5) + 0xC, 0xFFFD); W16(SG(6) + 8, 0x20); W16(SG(6) + 0xC, 0); }
        W16(SG(4) + 8, 0); W16(SG(3) + 8, 0); W16(SG(2) + 8, 0); W16(SG(1) + 8, 0);
        for (int i = 1; i < 7; i++) { W32(OFX(i), (uint32_t)JX[i]); W32(OFY(i), (uint32_t)JY[i]); }
        for (int i = 1; i <= 4; i++) seg_place(SG(i), SG(i - 1), JX[i], JY[i]);
        seg_place(SG(5), SG(4), JX[5], JY[5]);
        seg_place(SG(6), SG(4), JX[6], JY[6]);
        break;
    }
    case 0x24:
        sub_06007CDE(0x20E);
        W16(o + 6, 0x24); W16(K_STEPS, 0);
        W16(SG(1) + 8, R16(SG(1) + 8) & 0xFF);
        W32(TG(1), R32(SG(1))); W32(TG(1) + 4, R32(SG(1) + 4));
        W16(SG(5) + 8, 0);
        W16(TG(1) + 8, R16(SG(1) + 8));
        W16(K_SWING, RS32(o + 8) < RS32(PL + 8) ? 0 : 0x8000);
        W16(SG(0) + 0xA, 0x24);
        break;
    case 0x25: sub_06007CDE(0x20E); W16(o + 6, 0x25); W16(SG(0) + 0xA, 0x25); break;
    case 0x26:
        W16(K_FRAME, R16(o + 0x5A));
        for (int16_t i = 0; i < RS16(NSEG); i++) { W16(LINK(i), 0xFFFF); seg_cpy(SG(i), PV(i)); }
        W16(SG(0) + 0xA, R16(o + 6));
        W16(SG(0) + 0xC, 0);
        W32(SG(0), R32(o + 8)); W32(SG(0) + 4, R32(o + 0xC)); W16(SG(0) + 8, 0);
        W16(o + 6, 0x26);
        Obj_Spawn(0, RS32(SG(0)), RS32(SG(0) + 4), 0, 0);
        break;
    case 0x27: W16(o + 6, 0x27); W16(NSEG, 0); break;
    case 0x1000: B17_Set(0); OR16(o + 4, 1); break;
    case 0x1001: case 0x1003:
        sub_06007CDE(0x210);
        B17_Set(0);
        W16(o + 0x4A, 0x1001); W16(o + 0x4C, 0);
        if (RS16(o + 0x52) != a) {
            W16(o + 0x50, (uint16_t)(smod((int32_t)(((uint32_t)sub_0602DDD2() >> 6) & 0x7FFF), 5) + 1));
            W16(o + 0x52, (uint16_t)a);
        }
        W16(K_STEPS, 0x14);
        laser_aim(a == 0x1003);
        OR16(o + 4, 4);
        break;
    case 0x1002:
        sub_06007CDE(0x210);
        W16(o + 0x4A, 0x1002); W16(K_STEPS, 0x14); W16(NSEG, 4);
        W16(o + 6, 0);
        W32(SG(0), R32(o + 8)); W32(SG(0) + 4, R32(o + 0xC));
        W16(SG(0) + 8, 0); W16(SG(0) + 0xA, 0); W16(SG(0) + 0xC, 0); W16(TG(0) + 8, 0);
        W16(LINK(0), 0xFFFF);
        W32(TG(1), (uint32_t)(RS32(o + 8) + 0x60000)); W32(TG(1) + 4, (uint32_t)(RS32(o + 0xC) - 0x6F0000));
        seg_set(TG(1), 0x20, 6, 1); W16(LINK(1), 0);
        W32(TG(2), (uint32_t)(RS32(o + 8) + 0x60000)); W32(TG(2) + 4, (uint32_t)(RS32(o + 0xC) - 0x6F0000));
        seg_set(TG(2), 0x20, 3, -1); W16(LINK(2), 0);
        W32(TG(0), R32(SG(0))); W32(TG(0) + 4, R32(SG(0) + 4));
        seg_place(SG(3), TG(2), 0x2D0000, 0);
        seg_set(TG(3), 0, 5, -2); W16(LINK(3), 2);
        set_tg_ofs();
        break;
    default: break;
    }
}

static int16_t B17_Pick(void)                               /* 0x06078348 */
{
    int16_t r = 0;
    do {
        uint32_t v = (uint32_t)sub_0602DDD2() >> 5;
        if (R16(O + 2) & 0x8000) {
            int16_t m = smod((int32_t)v, 3);
            if (m == 0) r = 0x1001; else if (m == 1) r = 0x1003; else if (m == 2) r = 9;
        } else {
            int16_t m = smod((int32_t)v, 2);
            if (m == 0) r = 9; else if (m == 1) r = 0xF;
        }
    } while (r == RS16(K_LAST) && r != 9);
    W16(K_LAST, (uint16_t)r);
    return r;
}

static void B17_Combat(void)                                /* 0x06077F7A */
{
    uint32_t o = O;
    if (RS16(o + 6) == 0x26) { OR16(o + 0x12, 0x4000); return; }
    if (Enemy_AttackPlayer(1, 1, 0xC000)) EM_PlayerFaceBack(o);
    Enemy_CheckPlayerHits(0x8000);
}

static int16_t seg_bank(int16_t anim)                       /* 0x0607ABE8 */
{
    if (anim == 5 && (R16(O + 2) & 0x8000) && !(R16(O + 2) & 8)) return (int16_t)(bank() + 6);
    return bank();
}

static void seg_draw(int16_t i, int16_t shake, int first_frame)
{
    uint32_t s = SG(i);
    Anim_Start(LOC, B17_DEFS, B17_SCR, CHAR_BASE(SLOT), RS16(s + 0xA));
    if (first_frame && i == 0) W16(LOC + 2, R16(K_FRAME));
    W16(SPR + 2, (uint16_t)((int16_t)((uint32_t)(RS32(s) - RS32(CAM_X)) >> 16) + shake));
    W16(SPR + 4, (uint32_t)((RS32(s + 4) - RS32(CAM_Y)) >> 16));
    W16(SPR + 0x0A, (uint16_t)seg_bank(RS16(s + 0xA)));
    W16(SPR + 0x0C, 0);
    W16(SPR + 0x10, 0x200);
    if (RS16(s + 0xA) != 5) W16(SPR + 0x10, (R16(O + 2) & 0x8000) | 0x200);
    W32(SPR + 0x18, (uint32_t)(int32_t)RS16(s + 0xC));
    if (RS16(s + 8) != 0) { OR16(SPR + 0x10, 0x400); W16(SPR + 0x14, R16(s + 8)); }
    if (RS16(s + 0xA) == 4) { OR16(SPR + 0x10, 0x1000); W32(SPR + 0x1C, R32(K_SCALE)); }
    W16(SPR + 0x0E, 0);
    Anim_Tick(LOC);
}

static void B17_Draw(void)                                  /* 0x06078004 */
{
    uint32_t o = O;
    W16(SPR + 2, (uint32_t)((RS32(o + 8) - RS32(CAM_X)) >> 16));
    W16(SPR + 4, (uint32_t)((RS32(o + 0xC) - RS32(CAM_Y)) >> 16));
    W16(SPR + 0x0A, (uint16_t)bank());
    W16(SPR + 0x0C, 0);
    W16(SPR + 0x10, (R16(o + 2) & 0x8000) | 0x200);
    W32(SPR + 0x18, 0);
    W16(SPR + 0x0E, 0);
    Anim_Tick(o + 0x58);
    Seg_Layout();
    if (R16(o + 6) == 0x26) {
        int16_t sh = (int16_t)(VBL & 2);
        for (int16_t i = 0; i < RS16(NSEG); i++) seg_draw(i, sh, 1);
    } else {
        for (int16_t i = 0; i < RS16(NSEG); i++)
            if (R16(o + 6) != R16(SG(i) + 0xA) || R16(o + 6) == 0x19) seg_draw(i, 0, 0);
    }
}

static void B17_Palette(int16_t p)                          /* 0x0607A3AA */
{
    uint32_t o = O;
    if (RS16(K_PAL) != p) {
        int16_t a = RS16(o + 6);
        int st = (a == 0x26 || a == 0x19 || a == 0x27 || (R16(o + 4) & 1)) ? 1 : 4;
        if (Pal_StepTowards((bank() & 0xFFF) << 4, 0x10, R32(0x060DEFD8u + 4u * (uint32_t)p), st)) W16(K_PAL, (uint16_t)p);
        Pal_SetFaded(((bank() + 6) * 0x10) & 0xFFFF, 0x10, (uint32_t)(uint16_t)bank() * 0x20u + SHADOW_CRAM, -0x38, -0x20, -0x20);
    }
    uint16_t a = R16(o + 6);
    if (a != 0x26 && a != 0x19 && a != 0x27 && !(R16(o + 4) & 1)) {
        W16(K_PALT, R16(K_PALT) + 1);
        if (RS16(K_PALT) > 3) { W16(K_PALT, 0); W16(o + 0x48, RS16(o + 0x48) == 8 ? 9 : 8); }
    }
}

static void B17_Bubbles(void)                               /* 0x0607A4E4 */
{
    uint32_t o = O;
    int16_t a = RS16(o + 6);
    if (!FROZEN && a != 0x10 && a != 0x11 && a != 10 && a != 9) {
        W16(K_BUBT, R16(K_BUBT) + 1);
        if (RS16(K_BUBT) > 0xE) {
            W16(K_BUBT, 0);
            for (int k = 0; k < 3; k++) {
                uint32_t b = K_BUB + 0x20u * (uint32_t)k;
                if (RS16(b + 0x18) != -1) continue;
                Anim_Start(b, B17_DEFS, B17_SCR, CHAR_BASE(SLOT), 0x22);
                OR16(b, R16(K_BUBV));
                if (R16(K_BUBV) < 2) W16(K_BUBV, R16(K_BUBV) + 1); else W16(K_BUBV, 0);
                break;
            }
        }
    }
    for (int k = 0; k < 3; k++) {
        uint32_t b = K_BUB + 0x20u * (uint32_t)k;
        if (RS16(b + 0x18) == -1) continue;
        if (R16(b) & 0x8000) { Anim_Reset(b); continue; }
        uint32_t t = 0x060B77CCu + 4u * (R16(b) & 3);
        int16_t dx = (R16(o + 2) & 0x8000) ? RS16(t) : (int16_t)-RS16(t);
        W16(SPR + 2, (uint16_t)((int16_t)((uint32_t)(RS32(o + 8) - RS32(CAM_X)) >> 16) + dx));
        W16(SPR + 4, (uint16_t)((int16_t)((uint32_t)(RS32(o + 0xC) - RS32(CAM_Y)) >> 16) - RS16(t + 2)));
        W16(SPR + 0x0A, (uint16_t)(bank() + 4));
        W16(SPR + 0x0C, 0);
        W16(SPR + 0x10, (R16(o + 2) & 0x8000) | 0x200);
        W32(SPR + 0x18, 1);
        W16(SPR + 0x0E, 1);
        if (!(VBL & 1)) AND16(b, 0xBFFF); else OR16(b, 0x4000);
        Anim_Tick(b);
    }
}

static void B17_Overlay(void)                               /* 0x0607A784 */
{
    uint32_t o = O;
    if (R16(K_OVL) & 0x8000) { Anim_Reset(K_OVL); return; }
    W16(SPR + 2, (uint32_t)((RS32(o + 8) - RS32(CAM_X)) >> 16));
    W16(SPR + 4, (uint32_t)((RS32(o + 0xC) - RS32(CAM_Y)) >> 16));
    W16(SPR + 0x0A, (uint16_t)(bank() + 2));
    W16(SPR + 0x0C, 0);
    W16(SPR + 0x10, (RS32(o + 0x30) < 0 ? 0x8000 : 0) | 0x200);
    W16(SPR + 0x0E, 1);
    W32(SPR + 0x18, (VBL & 1) ? 4u : 0xFFFFFFFCu);
    Anim_Tick(K_OVL);
}

static void copy_mem(uint32_t d, uint32_t s, uint32_t n) { for (uint32_t k = 0; k < n; k += 2) W16(d + k, R16(s + k)); }

static void B17_Afterimages(void)                           /* 0x0607A934 */
{
    uint32_t o = O;
    if (!FROZEN) {
        W16(K_AIMG, R16(K_AIMG) + 1);
        if (RS16(K_AIMG) > 3) {
            W16(K_AIMG, 0);
            for (int k = 7; k != 0; k--) { copy_mem(K_REC(k), K_REC(k - 1), 0x1E4); W16(K_RECN(k), R16(K_RECN(k - 1))); }
            if (R16(o + 6) == 10 || R16(o + 6) == 0x11) {
                W16(K_RECN(0), R16(NSEG));
                for (int16_t i = 0; i < RS16(NSEG); i++) copy_mem(K_REC(0) + 0xA4 + 16u * (uint32_t)i, SG(i), 16);
                W16(K_REC(0) + 0xA4 + 0xA, R16(o + 6));
                W32(K_REC(0) + 0xA4, R32(o + 8)); W32(K_REC(0) + 0xA8, R32(o + 0xC));
                W16(K_REC(0), R16(o + 0x5A));
                W16(K_REC(0) + 0x14C, R16(o + 2));
            } else W16(K_RECN(0), 0);
        }
    }
    for (int16_t k = 0; k < 8; k++) {
        if (RS16(K_RECN(k)) == 0) continue;
        uint32_t r = K_REC(k) + 0xA4;
        for (int16_t j = 0; j < RS16(K_RECN(k)); j++) {
            uint32_t s = r + 16u * (uint32_t)j;
            int16_t an = RS16(s + 0xA);
            if (an == 5 || an == 4) continue;
            Anim_Start(LOC, B17_DEFS, B17_SCR, CHAR_BASE(SLOT), an);
            if (j == 0) W16(LOC + 2, R16(K_REC(k)));
            W16(SPR + 2, (uint32_t)((RS32(s) - RS32(CAM_X)) >> 16));
            W16(SPR + 4, (uint32_t)((RS32(s + 4) - RS32(CAM_Y)) >> 16));
            W16(SPR + 0x0A, (uint16_t)bank());
            W16(SPR + 0x0C, 0);
            uint16_t fl = R16(K_REC(k) + 0x14C) & 0x8000;
            W16(SPR + 0x10, fl | 0x200);
            W32(SPR + 0x18, (uint32_t)((int32_t)RS16(s + 0xC) - k * 5 - 8));
            if (RS16(s + 8) != 0) { W16(SPR + 0x10, fl | 0x600); W16(SPR + 0x14, R16(s + 8)); }
            W16(SPR + 0x0E, (uint16_t)(k >> 1));
            Anim_Tick(LOC);
        }
    }
}

/* 0x0607A888: shard 0x103C at (x + dx, y + dy) thrown outwards */
static void B17_Shard(int32_t x, int32_t y, int32_t dx, int32_t dy)
{
    int16_t s = (int16_t)Obj_Register(0x103C, x + dx, y + dy, 0, 0xFFFF);
    int16_t hx = (int16_t)((uint32_t)dx >> 16);
    int a = Atan8(0, 0, hx, (int16_t)((uint32_t)dy >> 16));
    if (s == -1) return;
    uint32_t p = OBJ_TABLE + (uint32_t)(int16_t)(s * 0x78);
    W32(p + 0x30, (uint32_t)((int32_t)hx << 13));
    W32(p + 0x34, (uint32_t)(FixMul(0x20000, Sin8(a)) + 0x40000));
}

static void B17_Fx12(int32_t dx, int32_t dy)                /* 0x0607AC34 */
{
    uint32_t o = O;
    if (dx == -1 && dy == 0) return;
    Obj_Spawn(0x12, (R16(o + 2) & 0x8000) ? RS32(o + 8) - dx : RS32(o + 8) + dx, RS32(o + 0xC) + dy, 0, 0);
}

static void B17_Blowup(void)                                /* 0x0607ACB6 */
{
    uint32_t o = O;
    for (uint32_t k = 0; k < 0x24; k++) {
        uint32_t t = 0x060B7B98u + 12u * k;
        int32_t dx = RS32(t);
        Obj_Spawn(0, (R16(o + 2) & 0x8000) ? RS32(o + 8) - dx : RS32(o + 8) + dx, RS32(o + 0xC) + RS32(t + 4), 0, RS16(t + 8));
    }
    sub_06007CDE(0x209);
    sub_06007CDE(0x217);
}

/* ---------------------------------------------------------------- loader / init / handler */

void Enemy17_Load(void)                                     /* 0x06076024 */
{
    int16_t s = (int16_t)Enemy_FindSlot(0x17);
    int base = (uint16_t)CHAR_BASE(s);
    uint32_t off = R32(0x060A9070u + 4u * (uint32_t)s);
    for (int i = 0; i < 0x50; i++) {
        uint32_t r = 0x060DEB78u + (uint32_t)i * 8;
        Char_Register((base + i) & 0xFFFF, Ram_Ptr(r + 6)[0] * 8, Ram_Ptr(r + 7)[0], RS16(r + 4), off);
        off += (uint32_t)R16(r + 2) * 32;
    }
    uint16_t b = R16(0x060A9084u + 2u * (uint32_t)s);
    Pal_Copy((b & 0xFFF) << 4, 0x10, 0x060DEDF8u);
    Pal_Copy((b + 1) * 0x10, 0x10, 0x060DEF58u);
    Pal_Copy((b + 2) * 0x10, 0x10, 0x060DEF78u);
    Pal_Copy((b + 3) * 0x10, 0x10, 0x060DEF38u);
    Pal_Copy((b + 4) * 0x10, 0x10, 0x060DEF98u);
    Pal_Copy((b + 5) * 0x10, 0x10, 0x060DEFB8u);
    Pal_SetFaded((b + 6) * 0x10, 0x10, 0x060DEDF8u, -4, -4, -4);
}

static void B17_Init(void)                                  /* 0x06076D4C */
{
    uint32_t o = O;
    Enemy_HurtFlash(0, 0, 0, 0);
    W16(0x060F2066u, 0x10);
    W32(o + 8, 0x05280000u); W32(o + 0xC, 0x05300000u);
    OR16(o + 2, 0x8000);
    W16(o + 4, 0);
    B17_Set(0x1000);
    Anim_Start(o + 0x58, B17_DEFS, B17_SCR, CHAR_BASE(SLOT), RS16(o + 6));
    AND16(o + 2, 0xFFFE);
    OR16(o + 2, 0x100);
    W16(o + 0x16, R16(0x060B7958u + 2u * (uint32_t)RS16(0x060252C4u)));
    W16(o + 0x14, 0); W16(o + 0x48, 0); W16(o + 0x4A, 0); W16(o + 0x4C, 0);
    W16(o + 0x4E, 0xFFFF); W16(o + 0x50, 0); W16(o + 0x52, 0); W16(o + 0x54, 0xFFFF);
    OR16(PL + 4, 0x400);
    W16(K_PAL, 100);
    for (int16_t k = 0; k < 4; k++) Anim_Reset(K_BUB + 0x20u * (uint32_t)k);
    W16(K_BUBT, 0); W16(K_BUBV, 0); W16(K_IDLE, 0);
    W16(K_LAST, 0xFFFF);
    W32(K_SCALE, 0x10000);
    W16(K_PALT, 0);
}

static void B17_Swing(uint32_t o)                           /* action 0x24, anim event 1 */
{
    (void)o;
    int hit = 0;
    W16(SG(1) + 8, (uint16_t)(RS16(SG(1) + 8) + (RS16(SG(5) + 8) >> 5)));
    if (!(R16(K_SWING) & 0x8000)) {
        if (RS16(SG(1) + 8) < -0x1D) {
            for (uint32_t k = 0; k < 9; k++)
                B17_Shard(RS32(PV(2)), RS32(PV(2) + 4), -RS32(0x060B7D48u + 8u * k), RS32(0x060B7D4Cu + 8u * k));
            W16(SG(1) + 8, 0xFFE3); hit = 1;
            sub_06007CDE(0x217);
        } else W16(SG(5) + 8, (uint16_t)((RS16(SG(5) + 8) >> 2) + RS16(SG(5) + 8) - 1));
    } else if ((R16(SG(1) + 8) & 0xFF) < 0x9E) W16(SG(5) + 8, (uint16_t)((RS16(SG(5) + 8) >> 2) + RS16(SG(5) + 8) + 1));
    else {
        for (uint32_t k = 0; k < 9; k++)
            B17_Shard(RS32(PV(2)), RS32(PV(2) + 4), RS32(0x060B7D48u + 8u * k), RS32(0x060B7D4Cu + 8u * k));
        W16(SG(1) + 8, 0x9D); hit = 1;
        sub_06007CDE(0x217);
    }
    seg_cpy(TG(1), SG(1));
    if (hit) { OR16(C_FLAGS, 0x80); B17_Set(0x25); }
}

void EnemyHandler_0017(void)                                /* 0x0607619A */
{
    uint32_t o = O;
    if (R16(o + 2) & 1) B17_Init();
    if (!FROZEN) {
        Hud_DrawBossBar(RS16(0x060B7958u + 2u * (uint32_t)RS16(0x060252C4u)), RS16(o + 0x16));
        Seg_Ease();
        B17_Combat();
        W16(K_HP, R16(o + 0x16));
        if (R16(o + 2) & 8) {                                       /* hurt: white flashes */
            int r = Enemy_HurtFlash(4, 0xB, 0, 0x060DEEF8u);
            if (r == 0) Enemy_HurtFlash(1, 0xB, 0, 0x060DEEF8u);
            else if (r == 2) {
                AND16(o + 2, 0xFFF7);
                if (RS16(o + 0x16) < 1) {                           /* dead */
                    OR16(PL + 4, 0x400);
                    W32(o + 0x30, 0);
                    B17_Set(0x26);
                    Enemy_AnimSwitch();
                    AND16(C_FLAGS, 0xFF7F);
                }
            }
        }
        if (!(R16(o + 0x58) & 0x8000)) {
            switch (RS16(o + 6)) {
            case 0:
                if (!(R16(o + 4) & 4)) {
                    W16(o + 0x14, R16(o + 0x14) + 1);
                    if (!(R16(o + 4) & 1)) {
                        W16(K_IDLE, R16(K_IDLE) + 1);
                        if (RS16(K_IDLE) > 0x1D) {
                            W16(K_IDLE, 0);
                            if (RS16(o + 0x50) < 1 || RS16(o + 0x52) == -1) { W16(o + 0x52, 0xFFFF); B17_Set(B17_Pick()); }
                            else B17_Set(RS16(o + 0x52));
                        }
                    } else if (R16(o + 0x14) > 0x13 && (RS16(o + 0x48) != 0 || R16(o + 0x14) > 0x31)) {   /* intro */
                        W16(o + 0x14, 0);
                        W16(o + 0x48, R16(o + 0x48) + 1);
                        if (RS16(o + 0x48) > 7) { AND16(PL + 4, 0xFBFF); AND16(o + 4, 0xFFFE); OR16(o + 4, 2); }
                    }
                } else if (R16(o + 0x4A) == 0x1001) {                /* laser */
                    if (RS16(K_STEPS) < 1) {
                        int16_t st = RS16(o + 0x4C);
                        if (st == 0) { W16(o + 0x14, R16(o + 0x14) + 1); if (R16(o + 0x14) > 3) W16(o + 0x4C, R16(o + 0x4C) + 1); }
                        else if (st == 1) {
                            int16_t ang = (int16_t)(RS16(SG(1) + 8) + 0x80);
                            seg_place(SG(4), SG(1), 0x230000, (int32_t)0xFFFC0000);
                            W16(o + 0x4E, (uint16_t)Projectile_Spawn(0x16, RS32(SG(4)), RS32(SG(4) + 4), ang, 0));
                            if (RS16(o + 0x4E) == -1) W16(o + 0x4C, 3); else W16(o + 0x4C, R16(o + 0x4C) + 1);
                        } else if (st == 2) { if (R16(PROJ(RS16(o + 0x4E))) != 0x16) W16(o + 0x4C, R16(o + 0x4C) + 1); }
                        else if (st == 3) { W16(o + 0x50, R16(o + 0x50) - 1); B17_Set(0x1002); }
                    } else W16(o + 0x14, 0);
                } else if (R16(o + 0x4A) == 0x1002 && RS16(K_STEPS) < 1) { B17_Set(0); AND16(o + 4, 0xFFFB); }
                break;
            case 10: {                                              /* run to the screen edge */
                int done;
                if (!(R16(o + 2) & 0x8000)) { W32(o + 0x30, 0x32000); done = RS32(CAM_X) + 0x1280000 <= RS32(o + 8); }
                else { W32(o + 0x30, 0xFFFCE000u); done = RS32(o + 8) <= RS32(CAM_X) + 0x180000; }
                if (done) { W32(o + 0x30, 0); Anim_Start(K_OVL, B17_DEFS, B17_SCR, CHAR_BASE(SLOT), 0xE); B17_Set(0xB); }
                break;
            }
            case 0x10:
                W32(K_SCALE, R32(K_SCALE) + 0x2000);
                if (RS32(K_SCALE) > 0x9FFFF) { Anim_Start(K_OVL, B17_DEFS, B17_SCR, CHAR_BASE(SLOT), 0xD); B17_Set(0x11); }
                break;
            case 0x11:
                if ((R16(o + 0x58) & 3) == 1) {
                    AND16(o + 0x58, 0xFFFC);
                    if (RS16(o + 0x54) == -1) W16(o + 0x54, (uint16_t)Projectile_Spawn(0x17, RS32(SG(1)), RS32(SG(1) + 4), 0x40, 0));
                } else if ((R16(o + 0x58) & 3) == 2) {
                    if (EM_Abs(RS32(o + 8) - RS32(PL + 8)) < 0x500000) {
                        W32(o + 0x30, 0);
                        Anim_Start(K_OVL, B17_DEFS, B17_SCR, CHAR_BASE(SLOT), 0xE);
                        B17_Set(0x24);
                    } else W32(o + 0x30, RS32(o + 8) < RS32(PL + 8) ? 0x18000u : 0xFFFE8000u);
                }
                break;
            case 0x12:
                if ((R16(o + 0x58) & 3) == 1) {
                    AND16(o + 0x58, 0xFFFC);
                    if (RS16(o + 0x54) != -1) { W16(PROJ(RS16(o + 0x54)), 0xFFFF); W16(o + 0x54, 0xFFFF); }
                } else if ((R16(o + 0x58) & 3) == 2) {
                    W32(K_SCALE, R32(K_SCALE) - 0x1000);
                    if (RS32(K_SCALE) < 0x10001) {
                        if (RS16(o + 0x4E) != -1) { W16(PROJ(RS16(o + 0x4E)), 0xFFFF); W16(o + 0x4E, 0xFFFF); }
                        B17_Set(0x13);
                    }
                }
                break;
            case 0x19:                                              /* falling apart */
                if (RS16(K_STEPS) < 1) {
                    if (R16(o + 0x48) != 0) W16(o + 0x48, R16(o + 0x48) - 1);
                    if (R16(o + 0x56) < 0x15) B17_Fx12(RS32(0x060B7AF0u + 8u * R16(o + 0x56)), RS32(0x060B7AF4u + 8u * R16(o + 0x56)));
                    if (R16(o + 0x56) < 0x19) Seg_DeathPose(RS16(o + 0x56));
                    W16(o + 0x56, R16(o + 0x56) + 1);
                    if (R16(o + 0x56) == 0x1A) { B17_Blowup(); B17_Set(0x27); }
                }
                break;
            case 0x24: if ((R16(o + 0x58) & 3) == 1) B17_Swing(o); break;
            case 0x25:
                if ((R16(o + 0x58) & 3) == 1) {
                    AND16(o + 0x58, 0xFFFC);
                    AND16(C_FLAGS, 0xFF7F);
                    W16(TG(1) + 8, 0x40);
                    W16(K_STEPS, 0x3C);
                }
                break;
            case 0x26:                                              /* dying: explosions */
                if (smod(VBL, 10) == 0) {
                    sub_06007CDE(((uint32_t)sub_0602DDD2() & 0x8000) ? 0x209 : 0x217);
                    int32_t y = RS32(o + 0xC) - (int32_t)((uint32_t)sub_0602DDD2() & 0x7FFFFF);
                    int32_t x = RS32(o + 8) - 0x100000;
                    x += (int32_t)((uint32_t)sub_0602DDD2() & 0x1FFFFF);
                    Obj_Spawn(0, x, y, 0, 0);
                }
                if (smod(VBL, 30) == 0) W16(o + 0x48, RS16(o + 0x48) == 8 ? 2 : 8);
                break;
            default: break;
            }
        } else {
            switch (RS16(o + 6)) {                                  /* anim ended */
            case 1: OR16(o + 2, 0x8000); B17_Set(RS16(o + 0x52)); break;
            case 2: B17_Set(RS16(o + 0x52)); break;
            case 9: Anim_Start(K_OVL, B17_DEFS, B17_SCR, CHAR_BASE(SLOT), 0xD); B17_Set(10); break;
            case 0xB: W16(o + 0x52, 0); B17_Set((R16(o + 2) & 0x8000) ? 2 : 1); break;
            case 0xF: B17_Set(0x10); break;
            case 0x13: B17_Set(0); break;
            case 0x25:
                W16(o + 0x50, R16(o + 0x50) - 1);
                B17_Set(RS16(o + 0x50) < 1 ? 0x12 : 0x11);
                break;
            case 0x26: B17_Set(0x19); Enemy_AnimSwitch(); OR16(o + 0x58, 0x4000); W16(o + 0x48, 8); break;
            case 0x27:
                W16(o, 0xFFFF);
                Fade_SetTarget(0x100, 0x100, 0x100);
                Fade_SetEndLayerMask(0);
                Fade_SetNextSpeed(0x20000, 1);
                Fade_StartOut();
                W16(0x0602529Eu, (R16(0x0602529Eu) & 0xFFF8) | 10);
                W16(o, 0xFFFF);
                return;
            default: break;
            }
        }
        B17_Palette(RS16(o + 0x48));
        Obj_Integrate(o);
        Enemy_AnimSwitch();
    }
    B17_Draw();
    if (R16(o + 4) & 2) B17_Bubbles();
    if (R16(o + 6) == 10 || R16(o + 6) == 0xB) { B17_Overlay(); B17_Afterimages(); }
}

/* ---------------------------------------------------------------- projectiles / effects */

/* Sho's kind-0/3 boxes against the segment (x0,y0)-(x1,y1) (inline in 0x06076ED8 /
 * 0x060776FC); `vert`: the laser's extra "vertical beam inside the box" rule. */
static int seg_hits_sho(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int laser)
{
    uint32_t b = R32(PL + 0x68);
    if (!b) return 0;
    for (; RS16(b) != -1; b += 10) {
        if (RS16(b) != 0 && RS16(b) != 3) continue;
        int32_t lo = x1 < x0 ? x1 : x0, hi = x0 < x1 ? x1 : x0;
        int flip = (R16(PL + 2) & 0x8000) != 0;
        int32_t L = flip ? RS16(b + 2) * -0x10000 : RS16(b + 2) * 0x10000;
        int32_t R = flip ? RS16(b + 6) * -0x10000 : RS16(b + 6) * 0x10000;
        int32_t bl = (L + RS32(PL + 8) < R + RS32(PL + 8) ? L : R) + RS32(PL + 8);
        int32_t br = (R + RS32(PL + 8) < L + RS32(PL + 8) ? L : R) + RS32(PL + 8);
        int32_t t0 = RS16(b + 4) * 0x10000 + RS32(PL + 0xC), t1 = RS16(b + 8) * 0x10000 + RS32(PL + 0xC);
        int32_t bt = t0 < t1 ? t0 : t1, bb = t1 < t0 ? t0 : t1;
        int32_t dx = x1 - x0, dy = y1 - y0;
        int32_t a = lo < bl ? bl : lo, c = hi < br ? hi : br;
        int32_t ya, yc;
        if (!laser && dx == 0) {
            int32_t m = y0 < y1 ? y1 : y0;
            if (m < bt) ya = yc = 0;
            else { m = y1 < y0 ? y1 : y0; ya = yc = (bb < m) ? 0 : bt + 0x20000; }
        } else {
            ya = y0 + FixMul(FixDiv(a - x0, dx), dy);
            yc = y0 + FixMul(FixDiv(c - x0, dx), dy);
        }
        if (((bt <= ya || bt <= yc) && (ya <= bb || yc <= bb) && bl <= hi && lo <= br) ||
            (laser && ((uint32_t)dx & 0xFFFF0000u) == 0 && bt < y1 && y1 < bb)) return 1;
    }
    return 0;
}

/* Projectile 0x15 (0x060775E4): the stretching hand's hit box (anim 5, hidden): hurts
 * Sho (1) while the boss lives. */
void Proj_B17Hand15(uint32_t e)
{
    if (R16(e + 4) & 1) {
        Anim_Start(e + 0x20, B17_DEFS, B17_SCR, CHAR_BASE(Enemy_FindSlot(0x17)), 5);
        AND16(e + 4, 0xFFFE);
        W16(e + 0x12, 100);
        OR16(e + 0x20, 0x4000);
    }
    if (!FROZEN && RS16(K_HP) > 0 && (int16_t)Projectile_Collide(1, 0x9000) == 1) W16(e, 0x15);
    W16(SPR + 2, (uint32_t)((RS32(e + 8) - RS32(CAM_X)) >> 16));
    W16(SPR + 4, (uint32_t)((RS32(e + 0xC) - RS32(CAM_Y)) >> 16));
    W16(SPR + 0x10, 0x200);
    W32(SPR + 0x18, 0);
    W16(SPR + 0x0A, (uint16_t)(COLOR_BANK(Enemy_FindSlot(0x17)) + 1));
    W16(SPR + 0x0C, 0);
    W16(SPR + 0x0E, 0);
    Anim_Tick(e + 0x20);
}

static void Fall8_Spawn(int32_t x, int16_t delay)           /* 0x0605E6F4 */
{
    int16_t s = (int16_t)Projectile_Spawn(8, x, 0x04000000, 0, 0);
    if (s == -1) return;
    uint32_t p = PROJ(s);
    W16(p + 6, (uint16_t)delay);
    W16(p + 0x10, R16(0x060AC5BEu + 2u * (uint32_t)RS16(0x060F22BEu)));
    if (R16(0x060F22BEu) < 2) W16(0x060F22BEu, R16(0x060F22BEu) + 1); else W16(0x060F22BEu, 0);
    W32(p + 0x18, 0); W32(p + 0x1C, 0);
}

static void Fall8_Rain(int32_t x)                           /* 0x0605E65C */
{
    int16_t n = RS16(0x060AC6DCu + 2u * (uint32_t)RS16(0x060F22C2u));
    if (R16(0x060F22C2u) < 7) W16(0x060F22C2u, R16(0x060F22C2u) + 1); else W16(0x060F22C2u, 0);
    for (int16_t k = 0; k < n; k++) {
        int16_t i = RS16(0x060F22C0u);
        if (R16(0x060F22C0u) < 0xE) W16(0x060F22C0u, R16(0x060F22C0u) + 1); else W16(0x060F22C0u, 0);
        Fall8_Spawn(x + RS32(0x060AC664u + 8u * (uint32_t)i), RS16(0x060AC668u + 8u * (uint32_t)i));
    }
}

static int any_proj8(void)
{
    for (int k = 0; k < 0x40; k++) if (R16(PROJ(k)) == 8) return 1;
    return 0;
}

/* Projectile 8 (0x0605E0E0): rock falling from the ceiling (anim +0x10 of 0x060AC54C,
 * chars 0x10) after +6 frames; hurts Sho (1, guardable below y 0x500); bursts into shards
 * (0x0605E76C, tables 0x060AC5C4 / 61C / 64C) on a hit (sound 0x217 unless playing) or
 * below the stage bottom; shakes the screen while any is falling. */
void Proj_FallingRock08(uint32_t e)
{
    if (R16(e + 4) & 1) {
        int16_t k = RS16(e + 0x10);
        if (k == 0) W16(e + 0x38, 0); else if (k == 1) W16(e + 0x38, 1); else if (k == 2) W16(e + 0x38, 2);
        Anim_Start(e + 0x20, 0x060AC54Cu, 0x060D2AF4u, 0x10, RS16(e + 0x38));
        AND16(e + 4, 0xFFFE);
        W16(e + 0x12, 1);
    }
    if (R16(e + 6) != 0) { if (!FROZEN) W16(e + 6, R16(e + 6) - 1); return; }
    if (!FROZEN) {
        OR16(C_FLAGS, 0x80);
        W32(e + 0xC, R32(e + 0xC) - R32(e + 0x1C));
        W32(e + 0x1C, R32(e + 0x1C) - 0x5000);
        if (RS32(e + 0x1C) < -0xA0000) W32(e + 0x1C, 0xFFF60000u);
        int16_t r = (int16_t)Projectile_Collide(1, RS32(e + 0xC) > 0x05000000 ? 0 : 0x4000);
        if (R16(e) == 0xFFFF) W16(e, 8);
        if (r != 0) {
            if (Sfx_CountLocked(0x217) == 0) sub_06007CDE(0x217);
            int16_t k = RS16(e + 0x10);
            if (k == 0) for (uint32_t i = 0; i < 0xB; i++) ObjF_ShardSpawn(RS32(0x060AC5C4u + 8u * i), RS32(0x060AC5C8u + 8u * i));
            else if (k == 1) for (uint32_t i = 0; i < 6; i++) ObjF_ShardSpawn(RS32(0x060AC61Cu + 8u * i), RS32(0x060AC620u + 8u * i));
            else if (k == 2) for (uint32_t i = 0; i < 3; i++) ObjF_ShardSpawn(RS32(0x060AC64Cu + 8u * i), RS32(0x060AC650u + 8u * i));
            W16(e, 0xFFFF);
            if (!any_proj8()) AND16(C_FLAGS, 0xFF7F);
            return;
        }
        if (RS32(0x060EA624u) < RS32(e + 0xC)) {
            W16(e, 0xFFFF);
            if (!any_proj8()) AND16(C_FLAGS, 0xFF7F);
            return;
        }
    }
    if (R16(e) == 0xFFFF) return;
    W16(SPR + 2, (uint32_t)((RS32(e + 8) - RS32(CAM_X)) >> 16));
    W16(SPR + 4, (uint32_t)((RS32(e + 0xC) - RS32(CAM_Y)) >> 16));
    W16(SPR + 0x10, 0x200);
    W32(SPR + 0x18, 0xFFFFFFFFu);
    W16(SPR + 0x0A, 0); W16(SPR + 0x0C, 0); W16(SPR + 0x0E, 0);
    Anim_Tick(e + 0x20);
}

/* Projectile 0x16 (0x06076ED8): the mouth laser (anim 7, scaled along X by +0x18,
 * growing 1.5 px/frame after 31 frames, sound 0x20D); +0x10 != 0: impact flash (anim 8,
 * 30 frames, spawned by 0x0607A844). Line test vs Sho (2, then 20 frames' grace);
 * freed off screen; on a wall / floor: rocks from the ceiling (y < 0x480) or 7 shards
 * (0x060B77D8), then the impact flash 8 px off. */
void Proj_B17Laser16(uint32_t e)
{
    int16_t sl = (int16_t)Enemy_FindSlot(0x17);
    if (R16(e + 0x10) != 0) {
        if (R16(e + 4) & 1) {
            Anim_Start(e + 0x20, B17_DEFS, B17_SCR, CHAR_BASE(sl), 8);
            AND16(e + 4, 0xFFFE);
            W16(e + 6, 0x1E);
        }
        if (!FROZEN) { W16(e + 6, R16(e + 6) - 1); if (RS16(e + 6) < 1) { W16(e, 0xFFFF); return; } }
        W16(SPR + 2, (uint32_t)((RS32(e + 8) - RS32(CAM_X)) >> 16));
        W16(SPR + 4, (uint32_t)((RS32(e + 0xC) - RS32(CAM_Y)) >> 16));
        W16(SPR + 0x10, 0x200);
        W32(SPR + 0x18, 0);
        W16(SPR + 0x0A, (uint16_t)(COLOR_BANK(sl) + 1));
        W16(SPR + 0x0C, 0); W16(SPR + 0x0E, 0);
        Anim_Tick(e + 0x20);
        return;
    }
    if (R16(e + 4) & 1) {
        Anim_Start(e + 0x20, B17_DEFS, B17_SCR, CHAR_BASE(sl), 7);
        AND16(e + 4, 0xFFFE);
        W32(e + 0x18, 0x11000); W32(e + 0x1C, 0);
        W16(e + 6, 0);
    }
    if (!FROZEN) {
        if (RS32(e + 0x1C) < 0x1F) { W32(e + 0x1C, R32(e + 0x1C) + 1); if (RS32(e + 0x1C) > 0x1E) sub_06007CDE(0x20D); }
        else W32(e + 0x18, R32(e + 0x18) + 0x18000);
        int32_t ex = RS32(e + 8) + FixMul(FixMul(Cos8(RS16(e + 0x14)), RS32(e + 0x18)), 0x80000);
        int32_t ey = RS32(e + 0xC) - FixMul(FixMul(Sin8(RS16(e + 0x14)), RS32(e + 0x18)), 0x80000);
        if (ex < RS32(CAM_X) - 0xA00000 || RS32(CAM_X) + 0x1E00000 < ex || ey < RS32(CAM_Y) - 0xA00000 || RS32(CAM_Y) + 0x1800000 < ey) {
            W16(e, 0xFFFF);
            return;
        }
        if (!(R16(PL + 2) & 8) && R16(e + 6) == 0 && R16(R32(0x060EA41Cu)) == 0 && RS16(K_HP) > 0 && R32(PL + 0x68)) {
            if (seg_hits_sho(RS32(e + 8), RS32(e + 0xC), ex, ey, 1)) { Player_TakeDamage(2); W16(e + 6, 0x14); }
        } else if (R16(e + 6) != 0) W16(e + 6, R16(e + 6) - 1);
        uint32_t yg = (uint32_t)ey & 0xFFF80000u;
        uint16_t a = Map_GetAttr(ex, (int32_t)yg, (((uint32_t)R16(0x060EA64Au) & 0xFF) * 0x30u & 0xFF) + 0x060EA588u);
        uint16_t f1 = Coll_AttrFlags(a);
        if (RS16(0x060EA64Cu) >= 0) a = Map_GetAttr(ex, (int32_t)yg, (((uint32_t)R16(0x060EA64Cu) & 0xFF) * 0x30u & 0xFF) + 0x060EA588u);
        uint16_t f2 = Coll_AttrFlags(a);
        if ((((f1 & 0xF) | (f2 & 0xF)) & 3) != 0) {
            int32_t dxl = ex - RS32(e + 8), dyg = (int32_t)yg - RS32(e + 0xC);
            int32_t xi = RS32(e + 8) + FixMul(FixDiv(dyg, ey - RS32(e + 0xC)), dxl);
            int32_t off;
            if ((int32_t)yg < 0x04800000) { Fall8_Rain(xi); off = 0x80000; sub_06007CDE(0x122); }
            else {
                for (uint32_t k = 0; k < 7; k++) B17_Shard(xi, (int32_t)yg, RS32(0x060B77D8u + 8u * k), RS32(0x060B77DCu + 8u * k));
                off = -0x80000; sub_06007CDE(0x122);
            }
            int16_t s = (int16_t)Projectile_Spawn(0x16, xi, (int32_t)yg + off, 0, 0);   /* 0x0607A844 */
            if (s != -1) W16(PROJ(s) + 0x10, 1);
            W16(e, 0xFFFF);
            return;
        }
    }
    W16(SPR + 2, (uint32_t)((RS32(e + 8) - RS32(CAM_X)) >> 16));
    W16(SPR + 4, (uint32_t)((RS32(e + 0xC) - RS32(CAM_Y)) >> 16));
    W16(SPR + 0x10, 0x1600);
    W16(SPR + 0x14, R16(e + 0x14));
    W32(SPR + 0x1C, R32(e + 0x18));
    W32(SPR + 0x18, 1);
    W16(SPR + 0x0A, (uint16_t)(COLOR_BANK(sl) + 1));
    W16(SPR + 0x0C, 0); W16(SPR + 0x0E, 0);
    Anim_Tick(e + 0x20);
}

/* Projectile 0x17 (0x060776FC): the rotating beam on the stretched arm (anim 0x14, 94 px,
 * X scale 0.75, angle +0x14 set by the layout), sound 0x123 every 8 frames, line test vs
 * Sho (2, 20 frames' grace). */
void Proj_B17Beam17(uint32_t e)
{
    int16_t sl = (int16_t)Enemy_FindSlot(0x17);
    if (R16(e + 4) & 1) {
        Anim_Start(e + 0x20, B17_DEFS, B17_SCR, CHAR_BASE(sl), 0x14);
        AND16(e + 4, 0xFFFE);
        W16(e + 0x12, 100);
        W16(e + 6, 0);
    }
    if (!FROZEN) {
        int32_t ex = RS32(e + 8) + FixMul(Cos8(RS16(e + 0x14)), 0x5E0000);
        int32_t ey = RS32(e + 0xC) - FixMul(Sin8(RS16(e + 0x14)), 0x5E0000);
        if (smod(VBL, 8) == 0) sub_06007CDE(0x123);
        if (!(R16(PL + 2) & 8) && R16(e + 6) == 0 && R16(R32(0x060EA41Cu)) == 0 && RS16(K_HP) > 0 && R32(PL + 0x68)) {
            if (seg_hits_sho(RS32(e + 8), RS32(e + 0xC), ex, ey, 0)) { Player_TakeDamage(2); W16(e + 6, 0x14); }
        } else if (R16(e + 6) != 0) W16(e + 6, R16(e + 6) - 1);
    }
    W16(SPR + 2, (uint32_t)((RS32(e + 8) - RS32(CAM_X)) >> 16));
    W16(SPR + 4, (uint32_t)((RS32(e + 0xC) - RS32(CAM_Y)) >> 16));
    W16(SPR + 0x10, 0x1600);
    W32(SPR + 0x1C, 0xC000);
    W16(SPR + 0x14, R16(e + 0x14));
    W32(SPR + 0x18, 4);
    W16(SPR + 0x0A, (uint16_t)(COLOR_BANK(sl) + 3));
    W16(SPR + 0x0C, 0); W16(SPR + 0x0E, 0);
    Anim_Tick(e + 0x20);
}

/* Effect 0x12 (0x06077B24): explosion (anim 0x23, sound 0x123), drops smoke 0x13 at its
 * anim event; effect 0x13 (0x06077C4C): smoke (anim 0x22, blinking). */
void Fx_B17Blast12(uint32_t e)
{
    int16_t sl = (int16_t)Enemy_FindSlot(0x17);
    if (R16(e + 2) & 1) {
        Anim_Start(e + 0x10, B17_DEFS, B17_SCR, CHAR_BASE(sl), 0x23);
        AND16(e + 2, 0xFFFE);
        sub_06007CDE(0x123);
    }
    if ((R16(e + 0x10) & 3) == 1) { AND16(e + 0x10, 0xFFFC); Obj_Spawn(0x13, RS32(e + 8), RS32(e + 0xC), 0, 0); }
    W16(SPR + 2, (uint32_t)((RS32(e + 8) - RS32(CAM_X)) >> 16));
    W16(SPR + 4, (uint32_t)((RS32(e + 0xC) - RS32(CAM_Y)) >> 16));
    W16(SPR + 0x10, 0x200);
    W32(SPR + 0x18, 5);
    W16(SPR + 0x0A, (uint16_t)(COLOR_BANK(sl) + 5));
    W16(SPR + 0x0C, 0); W16(SPR + 0x0E, 0);
    Anim_Tick(e + 0x10);
    if (R16(e + 0x10) & 0x8000) W16(e, 0xFFFF);
}

void Fx_B17Smoke13(uint32_t e)
{
    int16_t sl = (int16_t)Enemy_FindSlot(0x17);
    if (R16(e + 2) & 1) {
        Anim_Start(e + 0x10, B17_DEFS, B17_SCR, CHAR_BASE(sl), 0x22);
        AND16(e + 2, 0xFFFE);
    }
    if (!(VBL & 1)) AND16(e + 0x10, 0xBFFF); else OR16(e + 0x10, 0x4000);
    W16(SPR + 2, (uint32_t)((RS32(e + 8) - RS32(CAM_X)) >> 16));
    W16(SPR + 4, (uint32_t)((RS32(e + 0xC) - RS32(CAM_Y)) >> 16));
    W16(SPR + 0x10, 0x200);
    W32(SPR + 0x18, 6);
    W16(SPR + 0x0A, (uint16_t)(COLOR_BANK(sl) + 4));
    W16(SPR + 0x0C, 0); W16(SPR + 0x0E, 0);
    Anim_Tick(e + 0x10);
    if (R16(e + 0x10) & 0x8000) W16(e, 0xFFFF);
}
