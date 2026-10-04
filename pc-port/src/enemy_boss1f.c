/* Enemy type 0x001F: stage 9-2 final boss, Kazuma (index 17). Loader 0x0607E490 (0xD2
 * chars of 0x060E0730, 10 palettes at banks 0x50/0x51/0x58..0x5F, opens KAZUMA2.PBC
 * 0x060BC5B4), handler 0x0607E840, init 0x06080664 (home 0x520 / 0x4C0, HP 0x300 = the
 * whole bar, VDP2 colour-calculation shadow set up), setter 0x06080C00, decision
 * 0x06080A0E (distance bands 60/80/120/160/240 px x (rand >> 4) % 101 through the
 * {threshold, if-true, condition, if-false} tables 0x060BC35C..0x060BC548, walker
 * 0x06080B7E, conditions 0x06081AA0 always / 0x06081AA4 not mid-combo / 0x06081A24 room
 * behind / 0x06081ABA orb attack ready), reaction 0x06081730 (Sho close in the air ->
 * guard 10, Sho high and not slashing -> jump 0x3EC, projectile coming -> 0x3F1 block,
 * Sho near -> 0x20 back-flip / 0x3ED, Sho dashing from afar -> 0x3F2), wall test
 * 0x060819A8, damage by action 0x060812BC, sword-flash overlay 0x060812D2 (tables
 * 0x060BC23A/252/276/296/2C4 by frame), palette 0x06081AF4 (by +0x4E: 0x060E10F4..),
 * phase graphics 0x0607E5F4 (orbs 3 / 4, files 0x060E1AD8[]), death graphics 0x0607E768.
 * Hits: each one drops an orb 0x104C (0x0605F4BE, index 0x060F81D2) next to the hit and
 * steps the phase (0x060842F8 Boss1F_OrbTaken, objhandlers_f4.c); collected orbs fly back
 * (0x0605FE48, action 0x17) and trigger the specials by +0x4E: < 3 0x3F7 (leaping sword
 * dives 0x06081C64), < 5 0x3F8 (storm 0x06082110: fades out, lightning shaking the
 * screen, NBG2 priority 4 / colour ratio, pendulum flames = projectile 0x1B via 0x06082794
 * / 0x060828EC), < 8 0x3F9 (fire dragon 0x06082FC8 = projectile 0x1C head + body).
 * Actions (0x060F7AE0) of the original: 0 stand, 2 walk, 6 fall, 7 land, 8 / 0x3E9-0x3EB
 * sword combos, 0xE shoot (projectile 0x18), 0x10 / 0x3EE / 0x3EF / 0x3F0 jumps (0x3F0
 * fires an 8-way fan), 0x12 dash, 0x14 / 0x3F2 leap at Sho, 0x17 orb call, 0x18 hurt,
 * 0x19 / 0x1A back jump, 0x1B death (fx 0x14 explosions 0x060841E4, then fade to white,
 * stage state |= 10; the 9-2 clear leads to the ending through the tally / DIRECTOR
 * scenes), 0x20 back-flip, 0x3E8 intro wait, 0x3F1 block, 0x3F3..0x3F6 dash slashes.
 * Projectiles 0x18 (0x06080868 spinning blade), 0x1B (0x06082AE0 pendulum flame: swings
 * from its pivot, rotated 320 px arm), 0x1C (0x060833AC dragon: head records its path in
 * the ring 0x060F7B30 (0x8C x {x, y, angle, flags}), body parts replay it, sparks 0x104F
 * via 0x06084124); effect 0x14 (0x060841E4 explosion). Afterimages through 0x06049C10
 * with the rotation record (EM_TrailEx). */
#include "enemy_more.h"
#include "anim.h"
#include "sprite.h"
#include "objects.h"
#include "objhandlers.h"
#include "collision.h"
#include "player_ext.h"
#include "fixmath.h"
#include "resource.h"
#include "fade.h"
#include "hud.h"
#include "fx.h"
#include "objhandlers_d.h"

#define B1F_DEFS 0x060BA5D0u
#define B1F_SCR  0x060E19D0u
#define K_ORBS   0x060F7AD4u     /* s16 orbs collected (0x104C) */
#define K_GFX    0x060F7AD6u     /* s16 phase graphics pending */
#define K_BX     0x060F7AD8u     /* s32 boss x / y for the orbs */
#define K_BY     0x060F7ADCu
#define K_ACT    0x060F7AE0u     /* s16 action */
#define K_SUB    0x060F7AE2u     /* s16 step of the specials */
#define K_RATIO  0x060F7AE4u     /* s16 NBG colour-calculation ratio */
#define K_TIM    0x060F7AE6u
#define K_SHK    0x060F7AE8u     /* u16 lightning shake index */
#define K_SHKT   0x060F7AEAu
#define K_PEND   0x060F7AECu     /* u16 pendulum start-angle index */
#define K_PENDX  0x060F7AEEu     /* u16 pendulum x index */
#define K_CNT    0x060F7AF0u
#define K_PENDON 0x060F7B2Cu     /* s16 a pendulum flame exists (set by it, cleared here) */
#define K_DRAGON 0x060F7B2Eu     /* s16 the dragon exists */
#define HIST(i)  (0x060F7B30u + 12u * (uint32_t)(i))   /* dragon path ring */
#define K_HHEAD  0x060F81C0u
#define K_HCNT   0x060F81C2u
#define K_DIR    0x060F81CCu
#define K_DANG   0x060F81CEu
#define K_DSEGS  0x060F81D0u
#define K_ORBIX  0x060F81D2u
#define PROJ(i)  (PROJECTILES + (uint32_t)((int16_t)(i) * 0x40))
#define OBJ(i)   (OBJ_TABLE + (uint32_t)(int16_t)((int16_t)(i) * 0x78))
#define VBL      R16(0x060143EAu)
#define LEFT     RS32(0x060EA618u)
#define TOP      RS32(0x060EA61Cu)
#define RIGHT    RS32(0x060EA620u)
#define BOTTOM   RS32(0x060EA624u)

void Boss1F_OrbTaken(uint32_t b);                           /* 0x060842F8, objhandlers_f4.c */

static int16_t smod(int32_t a, int32_t b) { return (int16_t)(b ? a % b : 0); }   /* A.BIN 0x0600A604 */
static int32_t sdiv(int32_t a, int32_t b) { return b ? a / b : 0; }              /* A.BIN 0x060092BC */
static int32_t dxabs(uint32_t o) { return EM_Abs(RS32(o + 8) - RS32(PL + 8)); }
static int right_facing(uint32_t o) { return !(R16(o + 2) & 0x8000); }
static void face(uint32_t o)
{
    if (RS32(o + 8) < RS32(PL + 8)) AND16(o + 2, 0x7FFF); else OR16(o + 2, 0x8000);
}
static int32_t centre_ofs(uint32_t o) { return ((LEFT + RIGHT + (int32_t)0xF8000000) >> 1) - RS32(o + 8) + 0x4000000; }
/* sprite anchor shift between the standing and the spinning frames (23 px) */
static void shift23(uint32_t o, int fwd)
{
    int32_t d = right_facing(o) ? 0x170000 : -0x170000;
    W32(o + 8, (uint32_t)(RS32(o + 8) + (fwd ? d : -d)));
}

/* A.BIN 0x06009254: set a bit field of the VDP2 shadow "changed" word 0x0603B4AC */
static void vdp2_dirty(uint16_t field)
{
    int w = field & 0xFF, pos = 32 - w - (field >> 8);
    uint32_t m = ((1u << w) - 1u) << pos;
    W32(0x0603B4ACu, (R32(0x0603B4ACu) & ~m) | ((1u << pos) & m));
}

/* ---------------------------------------------------------------- loaders */

static void reg_chars(int base, uint32_t tbl, uint32_t n, uint32_t off)   /* 8-byte entries */
{
    for (uint32_t i = 0; i < n; i++) {
        uint32_t r = tbl + i * 8;
        Char_Register((base + (int)i) & 0xFFFF, Ram_Ptr(r + 6)[0] * 8, Ram_Ptr(r + 7)[0], RS16(r + 4), off);
        off += (uint32_t)R16(r + 2) * 32;
    }
}

void Enemy1F_Load(void)                                     /* 0x0607E490 */
{
    int16_t s = (int16_t)Enemy_FindSlot(0x1F);
    reg_chars(CHAR_BASE(s), 0x060E0730u, 0xD2, R32(0x060A9070u + 4u * (uint32_t)s));
    static const struct { int dst; uint32_t src; } p[10] = {
        { 0x500, 0x060E10F4u }, { 0x510, 0x060E12B4u }, { 0x580, 0x060E1194u }, { 0x590, 0x060E1394u },
        { 0x5A0, 0x060E1374u }, { 0x5B0, 0x060E13B4u }, { 0x5C0, 0x060E1354u }, { 0x5D0, 0x060E1334u },
        { 0x5E0, 0x060E13D4u }, { 0x5F0, 0x060E1174u } };
    for (int k = 0; k < 10; k++) Pal_Copy(p[k].dst, 0x10, p[k].src);
    Res_Request(2, 0, 0, 0xFFFFFFFFu, 0, 0x060BC5B4u);
}

static void Kazuma_PhaseGfx(void)                           /* 0x0607E5F4 */
{
    if (RS16(K_PENDON) != 0) { W16(K_GFX, 1); return; }
    int16_t n = RS16(K_ORBS);
    if (n >= 5) { W16(K_GFX, 0); return; }
    int16_t s = (int16_t)Enemy_FindSlot(0x1F);
    uint32_t t;
    if (n == 3) t = 0x060E1094u;
    else { t = 0x060E10C0u; Pal_Copy(0x600, 0x80, 0x060E11B4u); }
    uint32_t cnt = R32(t);
    uint32_t off = R32(0x060A9070u + 4u * (uint32_t)s) + 0x4F980;
    Res_Request(1, 1, 0, 0xFFFFFFFFu, R32(0x060A9070u + 4u * (uint32_t)s) + 0x25C540C0u, R32(0x060E1AD8u + 4u * (uint32_t)(n - 3)));
    reg_chars(CHAR_BASE(s) + 0xD2, t + 4, cnt, off);
    Res_Request(2, 0, 0, 0xFFFFFFFFu, 0, R32(0x060E1AD8u + 4u * (uint32_t)(n - 2)));
}

static void Kazuma_DeathGfx(void)                           /* 0x0607E768 */
{
    int16_t s = (int16_t)Enemy_FindSlot(0x1F);
    uint32_t off = R32(0x060A9070u + 4u * (uint32_t)s);
    Res_Request(1, 1, 0, 0xFFFFFFFFu, off + 0x25C04740u, 0x060BC350u);
    reg_chars(CHAR_BASE(s), 0x060E0DC4u, 0x5A, off);
}

/* 0x0605F4BE: an orb (0x104C, index k) at (x, y) unless already collected */
static void orb_spawn(int32_t x, int32_t y, int16_t k)
{
    uint16_t bit = (uint16_t)(1u << (k & 31));
    if (!(R16(0x060F22C4u) & bit)) Obj_Register(0x104C, x, y, (uint16_t)k, 0xFFFF);
}

static void orbs_call(void)                                 /* 0x0605FE48 */
{
    W16(0x060EA426u, 0xFFFF);
    W16(0x060EA630u, 0); W16(0x060EA632u, 0); W16(0x060EA634u, 0);
    for (int16_t s = 0; s < 0xC0; s++)
        if (R16(OBJ(s)) == 0x104C && R16(OBJ(s) + 0x48) == 0) W16(OBJ(s) + 0x48, 2);
}

/* ---------------------------------------------------------------- setter 0x06080C00 */

static void K_Set(int16_t a)
{
    uint32_t o = O;
    for (;;) {
        switch (a) {
        case 0: W16(o + 6, 0); W16(K_ACT, 0); W16(o + 0x14, 6); return;
        case 8: sub_06007CDE(0x229); /* fall through */
        case 0xE:
            AND16(o + 4, 0xFFBF);
            face(o);
            W16(o + 6, (uint16_t)a); W16(K_ACT, (uint16_t)a);
            W16(o + 0x48, (uint16_t)(smod((int32_t)(((uint32_t)sub_0602DDD2() >> 6) & 0x7FFF), 5) + 1));
            return;
        case 0x10: AND16(o + 4, 0xFFBF); W16(o + 6, 0x10); W16(K_ACT, 0x10); W16(o + 0x4A, 0); return;
        case 0x17:
            if (RS16(o + 0x16) > 0) { W16(o + 6, 0x17); W16(K_ACT, 0x17); orbs_call(); return; }
            a = 0;
            continue;
        case 0x1B: W32(o + 0x30, 0); W32(o + 0x34, 0); W16(o + 6, 0x1B); W16(K_ACT, 0x1B); return;
        case 0x20:
            face(o);
            W16(o + 6, 0x20); W16(K_ACT, 0x20);
            if (right_facing(o)) {
                if (EM_Abs(RS32(o + 8) - LEFT) < 0x780000) {
                    W32(o + 8, (uint32_t)(RS32(o + 8) - 0x170000));
                    W32(o + 0x30, 0x30000); W32(o + 0x34, 0xD0000); OR16(o + 4, 0x20);
                } else { W32(o + 0x30, 0xFFFA8000u); W32(o + 0x34, 0x30000); }
            } else {
                if (EM_Abs(RS32(o + 8) - RIGHT) < 0x780000) {
                    W32(o + 8, (uint32_t)(RS32(o + 8) + 0x170000));
                    W32(o + 0x30, 0xFFFD0000u); W32(o + 0x34, 0xD0000); OR16(o + 4, 0x20);
                } else { W32(o + 0x30, 0x58000); W32(o + 0x34, 0x30000); }
            }
            sub_06007CDE(0x126);
            OR16(o + 2, 0x4000);
            return;
        case 1000: AND16(o + 4, 0xFFBF); K_Set(0); W16(K_ACT, 1000); W16(o + 0x14, 0x3C); return;
        case 0x3E9: case 0x3EA: case 0x3EB: case 0x3EC:
            AND16(o + 4, 0xFFBF);
            sub_06007CDE(0x229);
            face(o);
            if (a == 0x3E9) K_Set(10);
            else if (a == 0x3EA) K_Set(0xC);
            else W16(o + 6, a == 0x3EB ? 8 : 5);
            W16(K_ACT, (uint16_t)a);
            return;
        case 0x3ED: AND16(o + 4, 0xFFBF); W16(o + 6, 1); W16(K_ACT, 0x3ED); return;
        case 0x3EE: case 0x3EF:
            AND16(o + 4, 0xFFBF); sub_06007CDE(0x229);
            W16(o + 6, 5); W16(K_ACT, (uint16_t)a); OR16(o + 4, 4);
            return;
        case 0x3F0: case 0x3F2: AND16(o + 4, 0xFFBF); W16(o + 6, 5); W16(K_ACT, (uint16_t)a); return;
        case 0x3F1: W16(o + 6, 0xC); W16(K_ACT, 0x3F1); OR16(o + 2, 4); return;
        case 0x3F3: AND16(o + 4, 0xFFBF); W16(o + 6, 3); W16(K_ACT, 0x3F3); return;
        case 0x3F4: case 0x3F5: case 0x3F6:
            AND16(o + 4, 0xFFBF); W16(o + 6, 3); W16(K_ACT, (uint16_t)a); OR16(o + 4, 4);
            return;
        case 0x3F7: case 0x3F8: case 0x3F9: {
            int32_t c = centre_ofs(o);
            if (EM_Abs(c) > 0x500000) { if (c < 1) OR16(o + 2, 0x8000); else AND16(o + 2, 0x7FFF); }
            W16(o + 6, 5); W16(K_ACT, (uint16_t)a); W16(K_SUB, 0);
            return;
        }
        default: W16(o + 6, (uint16_t)a); W16(K_ACT, (uint16_t)a); return;
        }
    }
}

/* ---------------------------------------------------------------- decision */

static int K_Wall(void)                                     /* 0x060819A8: 1 = room behind */
{
    uint32_t o = O;
    int32_t b = right_facing(o) ? LEFT : RIGHT;
    return EM_Abs(RS32(o + 8) - b) >= 0x780000;
}

static int K_Cond(uint32_t fn)                              /* the table conditions */
{
    uint32_t o = O;
    switch (fn) {
    case 0x06081AA0u: return 1;
    case 0x06081AA4u: return !(R16(o + 4) & 4);
    case 0x06081A24u: { int32_t b = right_facing(o) ? RIGHT : LEFT; return EM_Abs(RS32(o + 8) - b) >= 0x780000; }
    case 0x06081ABAu: return RS16(o + 0x16) > 0 && RS16(K_PENDON) == 0 && !(R16(o + 4) & 0x40);
    default: return 0;
    }
}

static int16_t K_Table(uint32_t t, int16_t r)               /* 0x06080B7E */
{
    int16_t k = 0;
    while (r <= RS16(t + 12u * (uint32_t)k)) k++;
    uint32_t e = t + 12u * (uint32_t)k;
    return K_Cond(R32(e + 4)) ? RS16(e + 2) : RS16(e + 8);
}

static int16_t K_Decide(void)                               /* 0x06080A0E */
{
    uint32_t o = O;
    face(o);
    int16_t r = smod((int32_t)((uint32_t)sub_0602DDD2() >> 4), 101);
    int32_t d = dxabs(o);
    int16_t a;
    if (d < 0x200000) a = K_Wall() ? 0x19 : 0x3EF;
    else if ((R16(o + 4) & 8) && K_Cond(0x06081ABAu)) { AND16(o + 4, 0xFFF7); a = 0x17; }
    else if (d < 0x3C0000) a = K_Table(0x060BC35Cu, r);
    else if (d < 0x500000) a = K_Table(0x060BC3B0u, r);
    else if (d < 0x780000) a = K_Table(0x060BC3F8u, r);
    else if (d < 0xA00000) a = K_Table(0x060BC458u, r);
    else if (d < 0xF00000) a = K_Table(0x060BC4C4u, r);
    else a = K_Table(0x060BC548u, r);
    AND16(o + 4, 0xFFFB);
    return a;
}

static int K_React(void)                                    /* 0x06081730 */
{
    uint32_t o = O;
    if (R16(PL + 2) & 0x4000) {
        if (dxabs(o) < 0x3C0000 && EM_Abs(RS32(o + 0xC) - RS32(PL + 0xC)) < 0x500000) {
            W32(o + 0x30, 0); K_Set(10); return 1;
        }
        if (EM_Abs(RS32(o + 0xC) - RS32(PL + 0xC)) > 0x8C0000 && Player_AttackKind() != 3 && RS16(0x060EA2BAu) != 6) {
            face(o); W32(o + 0x30, 0); K_Set(0x3EC); return 1;
        }
    }
    int32_t d = right_facing(o) ? 0x800000 : -0x800000;
    if (Projectile_InBox(RS32(o + 8) + d, RS32(o + 0xC) - 0x400000, (d >> 8) + RS32(o + 8), RS32(o + 0xC))) {
        W32(o + 0x30, 0); K_Set(0x3F1); return 1;
    }
    if (dxabs(o) < 0x570000 && (Player_AttackKind() != 6 || RS16(0x060EA2BAu) == 6)) {
        if ((RS16(0x060EA2BAu) == 6 || Player_AttackKind() == 3) && (VBL & 1)) { W32(o + 0x30, 0); K_Set(0x3ED); }
        else K_Set(0x20);
        return 1;
    }
    if (RS16(0x060EA2BAu) == 0x3B) {
        int32_t dx = RS32(PL + 8) - RS32(o + 8);
        if (EM_Abs(dx) > 0x500000 && EM_Abs(dx) < 0xA00000) { W32(o + 0x30, 0); face(o); K_Set(0x3F2); }
    }
    return 0;
}

/* ---------------------------------------------------------------- the specials */

static void spec_leap(uint32_t o)                           /* step 0 of 0x3F7..0x3F9 */
{
    if ((R16(o + 0x58) & 3) != 1) return;
    AND16(o + 0x58, 0xFFFC);
    int32_t c = centre_ofs(o);
    if (EM_Abs(c) > 0x500000) { W32(o + 0x34, 0xB0000); W32(o + 0x30, (uint32_t)sdiv(c, 70)); W16(K_SUB, 1); }
    else { W32(o + 0x34, 0x40000); W32(o + 0x30, (uint32_t)sdiv(c, 24)); W16(K_SUB, 2); }
    sub_06007CDE(0x126);
}
static void spec_spin(uint32_t o, int timer)               /* step 1: spinning flight, land */
{
    if (R16(o + 6) == 0xF) {
        W16(o + 0x4A, (uint16_t)(RS16(o + 0x4A) + (right_facing(o) ? -0xF : 0xF)));
        if (!(R16(o + 2) & 0x4000)) {
            W16(o + 0x4A, 0); W32(o + 0x30, 0);
            shift23(o, 0);
            W16(o + 6, 7); W16(K_SUB, 3);
            if (timer) W16(o + 0x14, 10);
        }
    } else if (RS32(o + 0x34) < 0x40000) { shift23(o, 1); W16(o + 6, 0xF); }
}
static void spec_land(uint32_t o, int timer)                /* step 2 */
{
    if (!(R16(o + 2) & 0x4000)) {
        W32(o + 0x30, 0); W16(o + 6, 7);
        if (timer) W16(o + 0x14, 10);
        W16(K_SUB, 3);
    }
}

static int K_EdgeStop(void)                                 /* 0x060820C4 */
{
    uint32_t o = O;
    if (right_facing(o)) { if (RIGHT - 0x180000 <= RS32(o + 8)) { W32(o + 8, (uint32_t)(RIGHT - 0x180000)); return 1; } }
    else if (RS32(o + 8) <= LEFT + 0x180000) { W32(o + 8, (uint32_t)(LEFT + 0x180000)); return 1; }
    return 0;
}

static void K_SpecialSword(void)                            /* 0x06081C64 (action 0x3F7) */
{
    uint32_t o = O;
    switch (RS16(K_SUB)) {
    case 0: spec_leap(o); break;
    case 1: spec_spin(o, 1); break;
    case 2: spec_land(o, 1); break;
    case 3:
        if (R16(o + 0x58) & 0x8000) {
            W16(o + 0x14, R16(o + 0x14) - 1);
            if (RS16(o + 0x14) < 1) {
                face(o);
                W16(o + 6, 5); W32(o + 0x34, 0x30000);
                W16(K_SUB, R16(K_SUB) + 1);
                sub_06007CDE(0x126); sub_06007CDE(0x228);
            }
        }
        break;
    case 4: case 6: case 8:
        if (R16(o + 6) == 0x10) W16(o + 0x4A, (uint16_t)(RS16(o + 0x4A) + (right_facing(o) ? -0xF : 0xF)));
        W32(o + 0x30, right_facing(o) ? 0x50000u : 0xFFFB0000u);
        if (RS32(o + 0x34) < 0x10000) {
            if (R16(o + 6) == 5) { shift23(o, 1); W32(o + 0xC, (uint32_t)(RS32(o + 0xC) - 0x2B0000)); }
            W16(o + 6, 0x10);
        }
        if (RS32(o + 0x34) < 0x5000) W32(o + 0x34, 0x5000);
        if (K_EdgeStop()) {
            shift23(o, 0);
            W16(o + 0x4A, 0); W32(o + 0x30, 0); W32(o + 0x34, 0);
            W32(o + 0xC, (uint32_t)(RS32(o + 0xC) + 0x2B0000));
            W16(o + 6, 6);
            W16(K_SUB, R16(K_SUB) + 1);
            W16(o + 0x14, 0x14);
        }
        break;
    case 5: case 7: case 9:
        if (!(R16(o + 2) & 0x4000)) {
            if (R16(o + 6) == 0) {
                W16(o + 0x14, R16(o + 0x14) - 1);
                if (RS16(o + 0x14) < 1) {
                    W16(K_SUB, R16(K_SUB) + 1);
                    if (RS16(K_SUB) < 9) {
                        W16(o + 6, 5); W32(o + 0x34, 0x30000);
                        sub_06007CDE(0x126); sub_06007CDE(0x228);
                    }
                }
            } else {
                W16(o + 6, 7);
                if (R16(o + 0x58) & 0x8000) { W16(o + 6, 0); W16(o + 2, R16(o + 2) ^ 0x8000); }
            }
        }
        break;
    case 10:
        W16(o + 0x50, 0);
        AND16(o + 4, 0xFFFD); AND16(o + 4, 0xFFFE);
        AND16(o + 0x1C, 0x3FFF); AND16(o + 0x12, 0xBFFF);
        W16(o + 6, 0); W16(K_ACT, 0); W16(o + 0x14, 6);
        break;
    default: break;
    }
}

static void shake_apply(void)                               /* lightning shake tables 0x060BC2E4 / 0x060BC2F0 */
{
    int16_t k = RS16(0x060BC304u + 2u * (uint32_t)(int16_t)R16(K_SHK));
    uint32_t x = R32(0x060BC2E4u + 4u * (uint32_t)k), y = R32(0x060BC2F0u + 4u * (uint32_t)k);
    W32(0x060EA5E8u, x); W32(0x060EA5ECu, y); W32(0x060EA5F8u, x); W32(0x060EA5FCu, y);
}

static void K_Storm(void)                                   /* 0x06082794 */
{
    uint32_t o = O;
    W16(K_SHKT, R16(K_SHKT) - 1);
    if (RS16(K_SHKT) < 1) {
        if (R16(K_SHK) < 3) W16(K_SHK, R16(K_SHK) + 1); else W16(K_SHK, 0);
        W16(K_SHKT, R16(0x060BC2FCu + 2u * (uint32_t)(int16_t)R16(K_SHK)));
        if (RS16(0x060BC304u + 2u * (uint32_t)(int16_t)R16(K_SHK)) == 2) sub_06007CDE(0x20C);
    }
    shake_apply();
    if (RS16(K_SUB) < 0xD && smod(VBL, 30) == 0) {
        int16_t s = (int16_t)Projectile_Spawn(0x1B, RS32(o + 8) + RS32(0x060BC318u + 4u * (uint32_t)(int16_t)R16(K_PENDX)),
                                              RS32(o + 0xC) - 0x460000, 0, 0);
        if (R16(K_PENDX) < 5) W16(K_PENDX, R16(K_PENDX) + 1); else W16(K_PENDX, 0);
        if (s != -1) { W16(PROJ(s) + 0x10, 1); W32(PROJ(s) + 0x1C, ((uint32_t)sub_0602DDD2() >> 8) & 1); }
    }
}

static int K_StormBolt(void)                                /* 0x060828EC: 1 when the bolts are done */
{
    W16(K_SHKT, R16(K_SHKT) - 1);
    if (RS16(K_SHKT) < 1) {
        if (R16(K_SHK) < 3) W16(K_SHK, R16(K_SHK) + 1); else W16(K_SHK, 0);
        W16(K_SHKT, R16(0x060BC330u + 2u * (uint32_t)(int16_t)R16(K_SHK)));
        if (RS16(0x060BC304u + 2u * (uint32_t)(int16_t)R16(K_SHK)) == 2) {
            int16_t s = (int16_t)Projectile_Spawn(0x1B, ((LEFT + RIGHT + (int32_t)0xF8000000) >> 1) + 0x4000000,
                                                  RS32(O + 0xC) - 0x640000, 0, 0);
            if (s != -1) W32(PROJ(s) + 0x1C, (RS16(K_SUB) == 7 || RS16(K_SUB) == 0xB) ? 0 : 1);
            W16(K_CNT, R16(K_CNT) - 1);
            sub_06007CDE(0x20C);
        }
    }
    shake_apply();
    return RS16(K_CNT) < 1;
}

static void ember(uint32_t o)                               /* 0x06082A28 */
{
    uint16_t f = VBL & 1;
    int32_t d = (int32_t)((((uint32_t)sub_0602DDD2() >> 8) & 0xF) * 0x10000) + 0x1C0000;
    if (!right_facing(o)) d = -d;
    int16_t s = (int16_t)Obj_Register(0x104E, RS32(o + 8) + d, RS32(o + 0xC) - 0x140000, (uint16_t)(f + 1), 0xFFFF);
    if (s != -1) OR16(OBJ(s) + 2, R16(o + 2) & 0x8000);
}

static void K_SpecialStorm(void)                            /* 0x06082110 (action 0x3F8) */
{
    uint32_t o = O;
    int16_t st = RS16(K_SUB);
    if ((uint16_t)st >= 0x13) return;
    switch (st) {
    case 0:
        if ((R16(o + 0x58) & 3) == 1) {
            W16(K_TIM, 5);
            AND16(o + 0x58, 0xFFFC);
            int32_t c = centre_ofs(o);
            if (EM_Abs(c) > 0x500000) { W32(o + 0x34, 0xB0000); W32(o + 0x30, (uint32_t)sdiv(c, 70)); W16(K_SUB, 1); }
            else { W32(o + 0x34, 0x40000); W32(o + 0x30, (uint32_t)sdiv(c, 24)); W16(K_SUB, 2); }
            sub_06007CDE(0x126);
        }
        break;
    case 1: spec_spin(o, 0); break;
    case 2: spec_land(o, 0); break;
    case 3: if (R16(o + 0x58) & 0x8000) { W16(o + 6, 1); AND16(o + 4, 0xFFFE); W16(K_SUB, R16(K_SUB) + 1); } break;
    case 4: if (R16(o + 0x58) & 0x8000) { W16(o + 6, 0x15); W16(K_SUB, R16(K_SUB) + 1); W16(o + 0x14, 0x4B); } break;
    case 5:
        if (R16(o + 0x5A) > 7 && R16(o + 0x5A) < 0xC) { ember(o); ember(o); }
        if (R16(o + 0x58) & 0x8000) {
            W16(K_TIM, R16(K_TIM) - 1);
            if (RS16(K_TIM) < 1) {
                W16(K_TIM, 5);
                if (RS16(o + 0x50) < 4) W16(o + 0x50, R16(o + 0x50) + 1);
                if (RS16(o + 0x50) == 4) { W16(K_TIM, 0x14); W16(o + 0x14, R16(o + 0x14) + 4); }
            }
        }
        if (R16(o + 0x14) != 0) W16(o + 0x14, R16(o + 0x14) - 1);
        if (R16(o + 0x14) == 0 && RS16(o + 0x50) == 4) {
            vdp2_dirty(0x0001);
            W16(0x0603B478u, (R16(0x0603B478u) & 0xFFFB) | 4);
            vdp2_dirty(0x0401);
            W16(0x0603B494u, (R16(0x0603B494u) & 0xFF00) | 0x1F);
            W16(K_RATIO, 0x1F);
            W16(0x060EA646u, 4);
            Scl_SetPriority(0x10, 4);
            W16(0x060F4D46u, 1);                            /* 0x0606EDD4 */
            W16(o + 0x14, 5);
            W16(K_SHK, 0); W16(K_SHKT, 10);
            W16(K_TIM, 0x96);
            W16(K_SUB, R16(K_SUB) + 1);
        }
        if (RS16(o + 0x50) == 4) {
            int16_t k = RS16(0x060BC304u + 2u * (uint32_t)(int16_t)R16(K_SHK));
            W32(0x060EA5F8u, R32(0x060BC2E4u + 4u * (uint32_t)k));
            W32(0x060EA5ECu, R32(0x060BC2F0u + 4u * (uint32_t)k));
            W32(0x060EA5FCu, R32(0x060EA5ECu));
        }
        break;
    case 6:
        W16(o + 0x14, R16(o + 0x14) - 1);
        if (R16(o + 0x14) == 0) {
            W16(o + 0x14, 5);
            W16(K_RATIO, R16(K_RATIO) - 1);
            vdp2_dirty(0x0401);
            W16(0x0603B494u, (R16(0x0603B494u) & 0xFF00) | (R16(K_RATIO) & 0xFF));
            if (RS16(K_RATIO) < 0x11) W16(K_RATIO, 0x10);
        }
        /* fall through */
    case 8: case 10: case 12:
        K_Storm();
        W16(K_TIM, R16(K_TIM) - 1);
        if (RS16(K_TIM) < 1) { W16(K_SUB, R16(K_SUB) + 1); W16(K_CNT, 1); }
        break;
    case 7: case 9: case 11: case 13:
        if (K_StormBolt() == 1) { W16(K_TIM, 0x96); W16(K_SUB, R16(K_SUB) + 1); }
        break;
    case 0xE:
        K_Storm();
        if (RS16(K_PENDON) == 0) {
            vdp2_dirty(0x0001);
            W16(0x0603B478u, (R16(0x0603B478u) & 0xFFFB) | 4);
            vdp2_dirty(0x0401);
            W16(0x0603B494u, (R16(0x0603B494u) & 0xFF00) | (R16(K_RATIO) & 0xFF));
            W16(o + 0x14, 5);
            W16(K_SUB, R16(K_SUB) + 1);
        }
        break;
    case 0xF:
        K_Storm();
        W16(o + 0x14, R16(o + 0x14) - 1);
        if (R16(o + 0x14) == 0) {
            W16(K_RATIO, R16(K_RATIO) + 1);
            if (RS16(K_RATIO) > 0x1E) { W16(K_SUB, R16(K_SUB) + 1); W16(K_TIM, 5); }
            W16(o + 0x14, 5);
        }
        vdp2_dirty(0x0401);
        W16(0x0603B494u, (R16(0x0603B494u) & 0xFF00) | (R16(K_RATIO) & 0xFF));
        break;
    case 0x10:
        W16(0x060F4D46u, 0);                                /* 0x0606EDDC */
        vdp2_dirty(0x0001);
        W16(0x0603B478u, R16(0x0603B478u) & 0xFFFB);
        W16(0x060EA646u, 2);
        Scl_SetPriority(0x10, 2);
        W16(K_SUB, R16(K_SUB) + 1);
        break;
    case 0x11:
        W16(K_TIM, R16(K_TIM) - 1);
        if (RS16(K_TIM) < 1) {
            W16(o + 0x50, R16(o + 0x50) - 1);
            if (R16(o + 0x50) == 0) W16(K_SUB, R16(K_SUB) + 1);
            W16(K_TIM, 5);
        }
        break;
    case 0x12:
        W16(o + 0x50, 0);
        AND16(o + 4, 0xFFFD); AND16(o + 0x1C, 0x3FFF); AND16(o + 0x12, 0xBFFF);
        K_Set(0x22);
        break;
    }
}

static void K_SpecialDragon(void)                           /* 0x06082FC8 (action 0x3F9) */
{
    uint32_t o = O;
    switch (RS16(K_SUB)) {
    case 0: spec_leap(o); break;
    case 1: spec_spin(o, 0); break;
    case 2: spec_land(o, 0); break;
    case 3: if (R16(o + 0x58) & 0x8000) { W16(o + 6, 1); AND16(o + 4, 0xFFFE); W16(K_SUB, R16(K_SUB) + 1); } break;
    case 4:
        if (R16(o + 0x58) & 0x8000) {
            W16(o + 6, 0x15); W16(K_SUB, R16(K_SUB) + 1);
            W16(K_HHEAD, 0); W16(K_HCNT, 0); W16(K_TIM, 5); W16(K_DSEGS, 0);
        }
        break;
    case 5:
        if (R16(o + 0x5A) >= 9) {
            int32_t d = 0x500000; int16_t ang = 0;
            if (!right_facing(o)) { d = -0x500000; ang = 0x80; }
            int16_t s = (int16_t)Projectile_Spawn(0x1C, RS32(o + 8) + d, RS32(o + 0xC) - 0x1E0000, ang, 0);
            if (s != -1) {
                sub_06007CDE(0x113);
                W16(PROJ(s) + 0x10, 1);
                if (right_facing(o)) AND16(PROJ(s) + 4, 0x7FFF); else OR16(PROJ(s) + 4, 0x8000);
            }
            W16(K_SUB, R16(K_SUB) + 1);
        }
        break;
    case 6: if (RS16(K_DRAGON) == 0) { W16(K_TIM, 5); W16(K_SUB, R16(K_SUB) + 1); } break;
    case 7:
        W16(K_TIM, R16(K_TIM) - 1);
        if (RS16(K_TIM) < 1) {
            W16(o + 0x50, R16(o + 0x50) - 1);
            if (R16(o + 0x50) == 0) W16(K_SUB, R16(K_SUB) + 1);
            W16(K_TIM, 5);
        }
        break;
    case 8:
        AND16(o + 4, 0xFFFD); AND16(o + 0x1C, 0x3FFF); AND16(o + 0x12, 0xBFFF);
        K_Set(0x22);
        break;
    default: break;
    }
}

/* ---------------------------------------------------------------- draw helpers */

static void K_Flash(void)                                   /* 0x060812D2: sword flash overlay */
{
    uint32_t o = O;
    W16(SPR + 2, (uint32_t)((RS32(o + 8) - RS32(CAM_X)) >> 16));
    W16(SPR + 4, (uint32_t)((RS32(o + 0xC) - RS32(CAM_Y)) >> 16));
    W16(SPR + 0x10, (R16(o + 2) & 0x8000) | 0x200);
    W32(SPR + 0x18, 1);
    W16(SPR + 0x0A, (uint16_t)(COLOR_BANK(SLOT) + 1));
    W16(SPR + 0x0C, 0);
    W16(SPR + 0x0E, R16(o + 0x50));
    uint16_t a = R16(o + 6);
    int anim; uint32_t t; int stop_on_end = 1;
    switch (a) {
    case 8: anim = 9; t = 0x060BC23Au; break;
    case 10: anim = 0xB; t = 0x060BC252u; break;
    case 0xC: anim = 0xD; t = 0x060BC276u; break;
    case 0x10: anim = 0x11; t = 0; break;
    case 0x12: anim = 0x13; t = 0x060BC296u; break;
    case 0x15: anim = 0x16; t = 0x060BC2C4u; break;
    default: return;
    }
    (void)stop_on_end;
    Anim_Start(SCRATCH_ANIM, B1F_DEFS, B1F_SCR, CHAR_BASE(SLOT), anim);
    if (t) {
        W16(SCRATCH_ANIM + 2, R16(t + 2u * R16(o + 0x5A)));
        if (RS16(SCRATCH_ANIM + 2) == 0 && R16(o + 0x72) == 0) sub_06007CDE(0x127);
    } else {
        W16(SPR + 0x10, R16(SPR + 0x10) | 0x400);
        W16(SPR + 0x14, R16(o + 0x4A));
    }
    if (R16(SPR + 0x0E) > 3) { W16(SPR + 0x0E, 3); OR16(SCRATCH_ANIM, 0x4000); }
    if (t && RS16(t + 2u * R16(o + 0x5A)) == -1) return;
    Anim_Tick(SCRATCH_ANIM);
}

static void K_Palette(void)                                 /* 0x06081AF4 */
{
    uint32_t o = O;
    uint32_t a = 0x060E10F4u, b;
    int16_t ph = RS16(o + 0x4E);
    if (ph < 3) { b = 0x060E12D4u; a = 0x060E1114u; }
    else if (ph < 5) { b = 0x060E12F4u; a = 0x060E1134u; }
    else if (ph < 8) { b = 0x060E1314u; a = 0x060E1154u; }
    else b = 0x060E12B4u;
    int dst = (R16(0x060A9084u + 2u * (uint32_t)SLOT) & 0xFFF) << 4;
    int dst1 = (RS16(0x060A9084u + 2u * (uint32_t)SLOT) + 1) * 0x10;
    if (!(R16(o + 4) & 2)) {
        if (RS16(o + 0x4C) != 2 && Pal_StepTowards(dst, 0x10, 0x060E10F4u, 1)) {
            W16(o + 0x4C, 2);
            Pal_Copy(dst1, 0x10, 0x060E12B4u);
        }
    } else if (RS16(o + 0x4C) == 0) {
        if (Pal_StepTowards(dst, 0x10, a, 1)) { Pal_Copy(dst1, 0x10, b); W16(o + 0x4C, 1); }
    } else if (Pal_StepTowards(dst, 0x10, 0x060E10F4u, 1) & 0xFFFF) W16(o + 0x4C, 0);
}

/* ---------------------------------------------------------------- init / handler */

static void K_Init(void)                                    /* 0x06080664 */
{
    uint32_t o = O;
    W32(o + 8, 0x05200000u); W32(o + 0xC, 0x04C00000u);
    K_Set(1000);
    Anim_Start(o + 0x58, B1F_DEFS, B1F_SCR, CHAR_BASE(SLOT), RS16(o + 6));
    AND16(o + 2, 0xFFFE);
    W16(o + 4, 0);
    W16(o + 0x16, 0x300);
    W16(o + 0x14, 0);
    W32(o + 0x30, 0); W32(o + 0x34, 0);
    OR16(o + 2, 0x8000);
    for (uint32_t k = 0x48; k <= 0x52; k += 2) W16(o + k, 0);
    EM_TrailReset();
    vdp2_dirty(0x0001); W16(0x0603B478u, R16(0x0603B478u) & 0xFDFF);
    vdp2_dirty(0x0001); W16(0x0603B478u, R16(0x0603B478u) & 0xFEFF);
    vdp2_dirty(0x0001); W16(0x0603B478u, (R16(0x0603B478u) & 0xFFFB) | 4);
    vdp2_dirty(0x0001); W16(0x0603B47Au, R16(0x0603B47Au) & 0xFFCF);
    vdp2_dirty(0x0401); W16(0x0603B494u, R16(0x0603B494u) & 0xFF00);
    W16(K_PEND, 0); W16(K_PENDX, 0); W16(K_GFX, 0);
    W16(0x060F81D4u, 0x300);
    W16(0x060F81D6u, (uint16_t)(0x300 - RS16(0x060BC338u + 2u * (uint32_t)RS16(0x060252C4u))));
    W16(K_ORBIX, 0);
}

static void K_Dying(uint32_t o)                             /* face Sho, stop, lock his input, 0x1B */
{
    face(o);
    W16(o + 0x4A, 0); W16(o + 0x50, 0); W32(o + 0x30, 0);
    AND16(o + 4, 0xFFDE);
    OR16(PL + 4, 0x400);
    K_Set(0x1B);
    Enemy_AnimSwitch();
}

static void K_Explode(uint32_t o)                           /* action 0x1B: fx 0x14 every 22 frames */
{
    if (smod(VBL, 22) != 0) return;
    uint16_t f = R16(o + 0x5A);
    if (f < 0x2D) {
        uint32_t ry = (uint32_t)sub_0602DDD2(), rx = (uint32_t)sub_0602DDD2();
        Obj_Spawn(0x14, RS32(o + 8) - 0x100000 + (int32_t)(rx & 0x1FFFFF), RS32(o + 0xC) - (int32_t)(ry & 0x3FFFFF), 0, 0);
    } else if (f < 0x37) {
        uint32_t ry = (uint32_t)sub_0602DDD2(), rx = (uint32_t)sub_0602DDD2();
        Obj_Spawn(0x14, RS32(o + 8) - 0x100000 + (int32_t)(rx & 0xFFFFF), RS32(o + 0xC) - (int32_t)(ry & 0x1FFFFF), 0, 0);
    } else {
        int32_t dx = (int32_t)(((uint32_t)sub_0602DDD2() >> 8) & 0x3FFFFF);
        if (!right_facing(o)) dx = -dx;
        uint32_t ry = (uint32_t)sub_0602DDD2();
        Obj_Spawn(0x14, RS32(o + 8) + dx, RS32(o + 0xC) - (int32_t)(ry & 0xFFFFF), 0, 0);
    }
}

static int K_Running(uint32_t o)                            /* anim still running: per action */
{
    switch (RS16(K_ACT)) {
    case 0:
        face(o);
        if (RS32(o + 0x34) < 0) K_Set(6);
        else if (!K_React()) {
            if (R16(o + 0x14) == 0) K_Set(K_Decide());
            else W16(o + 0x14, R16(o + 0x14) - 1);
        }
        break;
    case 2:
        face(o);
        if (!K_React()) {
            if (dxabs(o) < 0x3C0000) { W32(o + 0x30, 0); K_Set(K_Decide()); }
            else W32(o + 0x30, right_facing(o) ? 0x16000u : 0xFFFEA000u);
        }
        break;
    case 6: if (!(R16(o + 2) & 0x4000)) K_Set(7); break;
    case 7: K_React(); break;
    case 0xE:
        if ((R16(o + 0x58) & 3) == 1) {
            AND16(o + 0x58, 0xFFFC);
            int32_t dx = 0x2E0000; int16_t ang = 0;
            if (!right_facing(o)) { dx = (int32_t)0xFFD20000; ang = 0x80; }
            Projectile_Spawn(0x18, RS32(o + 8) + dx, RS32(o + 0xC) - 0x2E0000, ang, 0x50000);
            sub_06007CDE(0x100);
        } else if (R16(o + 0x5A) < 3) K_React();
        break;
    case 0x10:
        W16(o + 0x4A, (uint16_t)(RS16(o + 0x4A) + (right_facing(o) ? -0xF : 0xF)));
        if (RS32(o + 0xC) > 0x4ADFFFF) {
            W16(o + 0x4A, 0); W32(o + 0x30, 0);
            face(o);
            shift23(o, 0);
            K_Set(K_Wall() ? 0x19 : 0x3EE);
        }
        break;
    case 0x12:
        AND16(o + 4, 0xFFFE);
        W32(o + 0x30, 0);
        if ((R16(o + 0x58) & 3) == 1) { OR16(o + 4, 1); W32(o + 0x30, right_facing(o) ? 0x78000u : 0xFFF88000u); }
        break;
    case 0x14:
        if (!(R16(o + 2) & 0x4000)) { W16(o + 0x4A, 0); W32(o + 0x30, 0); shift23(o, 0); K_Set(6); }
        break;
    case 0x17:
        W16(o + 0x50, 1);
        if ((R16(o + 0x58) & 3) == 1) {
            sub_06007CDE(0x10F);
            AND16(o + 0x58, 0xFFFC);
            OR16(o + 4, 2 | 0x40 | 1);
            OR16(o + 0x12, 0x4000);
        }
        break;
    case 0x18: if (!(R16(o + 2) & 0x4000) && RS32(o + 0x34) < 1) W32(o + 0x30, 0); break;
    case 0x19:
        if ((R16(o + 0x58) & 3) == 1) {
            AND16(o + 0x58, 0xFFFC); OR16(o + 0x58, 2);
            W32(o + 0x34, 0x40000);
            W32(o + 0x30, right_facing(o) ? 0xFFFC0000u : 0x40000u);
            sub_06007CDE(0x126);
        } else if ((R16(o + 0x58) & 3) == 2 && !(R16(o + 2) & 0x4000)) { W32(o + 0x30, 0); K_Set(0x1A); }
        break;
    case 0x1B:
        if (!(R16(o + 2) & 0x4000) && RS32(o + 0x34) < 1) W32(o + 0x30, 0);
        K_Explode(o);
        if ((R16(o + 0x58) & 3) == 1) { AND16(o + 0x58, 0xFFFC); Kazuma_DeathGfx(); }
        if ((R16(o + 0x58) & 3) == 2) { AND16(o + 0x58, 0xFFFC); sub_06007CDE(0x22B); }
        break;
    case 0x20:
        if ((R16(o + 0x58) & 3) == 0) OR16(o + 4, 1); else AND16(o + 4, 0xFFFE);
        if (RS32(o + 0x34) > 0x50000 && RS32(o + 0xC) < 0x4800000) W32(o + 0x34, R32(o + 0x34) - 0x5000);
        if (!(R16(o + 4) & 0x20) || RS32(o + 0x34) > 0x20000) {
            if (!(R16(o + 2) & 0x4000)) { W32(o + 0x30, 0); AND16(o + 4, 0xFFFE); K_Set(0x21); }
        } else {
            AND16(o + 4, 0xFFDF);
            shift23(o, 1);
            AND16(o + 4, 0xFFFE);
            K_Set(0x3F0);
            W16(o + 6, 0xF);
        }
        break;
    case 1000:
        if (R16(o + 0x14) != 0) W16(o + 0x14, R16(o + 0x14) - 1);
        else K_Set(0);
        break;
    case 0x3EC:
        if ((R16(o + 0x58) & 3) == 1) {
            AND16(o + 0x58, 0xFFFC); OR16(o + 0x58, 2);
            W32(o + 0x34, 0xC0000);
            W32(o + 0x30, (uint32_t)sdiv(RS32(PL + 8) - RS32(o + 8), 76));
            sub_06007CDE(0x126);
        } else if ((R16(o + 0x58) & 3) == 2 && RS32(o + 0x34) < 0x68000) { shift23(o, 1); K_Set(0x10); }
        break;
    case 0x3EE: case 0x3EF:
        if (R16(o + 6) == 0xF) {
            W16(o + 0x4A, (uint16_t)(RS16(o + 0x4A) + (right_facing(o) ? -0xF : 0xF)));
            if (RS32(o + 0xC) > 0x4ADFFFF) { W16(o + 0x4A, 0); W32(o + 0x30, 0); shift23(o, 0); K_Set(7); }
        } else if ((R16(o + 0x58) & 3) == 1) {
            AND16(o + 0x58, 0xFFFC); OR16(o + 0x58, 2);
            W32(o + 0x34, 0xB0000);
            sub_06007CDE(0x126);
            int32_t d = RS16(K_ACT) == 0x3EE ? (right_facing(o) ? 0x8F0000 : (int32_t)0xFF710000)
                                            : (right_facing(o) ? 0x1070000 : (int32_t)0xFEF90000);
            W32(o + 0x30, (uint32_t)sdiv(d, 70));
        } else if ((R16(o + 0x58) & 3) == 2 && RS32(o + 0x34) < 0x40000) { shift23(o, 1); W16(o + 6, 0xF); }
        break;
    case 0x3F0:
        if (R16(o + 6) == 0xF) {
            W16(o + 0x4A, (uint16_t)(RS16(o + 0x4A) + (right_facing(o) ? -0xF : 0xF)));
            if ((R16(o + 0x58) & 3) == 0) {
                OR16(o + 0x58, 1);
                int a = Atan8((int16_t)(R32(o + 8) >> 16), (int16_t)(R32(o + 0xC) >> 16), (int16_t)(R32(PL + 8) >> 16), (int16_t)(R32(PL + 0xC) >> 16));
                int16_t ang = (int16_t)(a - 0x20);
                for (int k = 0; k < 8; k++, ang = (int16_t)(ang + 8))
                    Projectile_Spawn(0x18, RS32(o + 8) + FixMul(0x100000, Cos8(ang)), RS32(o + 0xC) - FixMul(0x100000, Sin8(ang)), ang, 0x50000);
                sub_06007CDE(0x101);
            }
            if (RS32(o + 0xC) > 0x4ADFFFF) { W16(o + 0x4A, 0); W32(o + 0x30, 0); shift23(o, 0); K_Set(7); }
        } else if ((R16(o + 0x58) & 3) == 1) {
            AND16(o + 0x58, 0xFFFC); OR16(o + 0x58, 2);
            W32(o + 0x34, 0xB0000); W32(o + 0x30, 0);
            sub_06007CDE(0x126);
        } else if ((R16(o + 0x58) & 3) == 2 && RS32(o + 0x34) < 0x40000) { shift23(o, 1); W16(o + 6, 0xF); }
        break;
    case 0x3F2:
        if ((R16(o + 0x58) & 3) == 1) {
            sub_06007CDE(0x228);
            AND16(o + 0x58, 0xFFFC);
            W32(o + 0x30, right_facing(o) ? 0x40000u : 0xFFFC0000u);
            int32_t n = sdiv(EM_Abs(RS32(PL + 8) - RS32(o + 8)), EM_Abs(RS32(o + 0x30))) + 4;
            W32(o + 0x34, (uint32_t)((n >> 1) * 0x5000));
            int32_t s = 0, acc = 0;
            while (acc < 0x4C00000 - RS32(PL + 0xC)) { s += 0x5000; acc += s; }
            W32(o + 0x34, (uint32_t)(RS32(o + 0x34) + s));
            shift23(o, 1);
            if (RS32(o + 0x34) > 0x88000) W32(o + 0x34, 0x88000);
            K_Set(0x14);
            sub_06007CDE(0x126);
        }
        break;
    case 0x3F3:
        if ((R16(o + 0x58) & 3) == 1 && !K_React()) {
            if (smod(VBL, 7) == 0) sub_06007CDE(0x112);
            W32(o + 0x30, right_facing(o) ? 0x34000u : 0xFFFCC000u);
            if (dxabs(o) < 0x3C0000) { W32(o + 0x30, 0); K_Set(8); }
        }
        break;
    case 0x3F4: case 0x3F5: case 0x3F6:
        if (R16(o + 6) == 3 && !K_React() && (R16(o + 0x58) & 3) == 1) {
            if (smod(VBL, 7) == 0) sub_06007CDE(0x112);
            int32_t lim = RS16(K_ACT) == 0x3F4 ? 0x3C0000 : RS16(K_ACT) == 0x3F5 ? 0x500000 : 0x780000;
            W32(o + 0x30, right_facing(o) ? 0x34000u : 0xFFFCC000u);
            if (dxabs(o) < lim) { W32(o + 0x30, 0); W16(o + 6, 4); }
        }
        break;
    case 0x3F7: K_SpecialSword(); break;
    case 0x3F8: K_SpecialStorm(); break;
    case 0x3F9: K_SpecialDragon(); break;
    default: break;
    }
    return 0;
}

/* anim ended: per action. 1 = the boss is gone */
static int K_Ended(uint32_t o)
{
    switch (RS16(K_ACT)) {
    case 8: case 0x3EB: case 0x3E9: case 0x3EA: {
        W16(o + 0x48, R16(o + 0x48) - 1);
        int16_t nx = RS16(K_ACT) == 0x3E9 ? 0x3EA : RS16(K_ACT) == 0x3EA ? 0x3EB : 0x3E9;
        if (R16(o + 0x48) != 0 && dxabs(o) < 0x3C0000) K_Set(nx); else K_Set(0);
        break;
    }
    case 0xE:
        W16(o + 0x48, R16(o + 0x48) - 1);
        if (R16(o + 0x48) == 0) K_Set(0);
        else {
            uint16_t want = RS32(o + 8) < RS32(PL + 8) ? 0 : 0x8000;
            if ((R16(o + 2) & 0x8000) == want) { AND16(o + 0x58, 0x7FFF); W16(o + 0x5A, 0); W16(o + 0x72, 0); }
            else K_Set(0);
        }
        break;
    case 0x17:
        if (RS16(o + 0x4E) < 3) K_Set(0x3F7);
        else if (RS16(o + 0x4E) < 5) K_Set(0x3F8);
        else if (RS16(o + 0x4E) < 8) K_Set(0x3F9);
        else K_Set(0);
        break;
    case 0x18:
        W16(o + 0x4A, 0); W16(o + 0x50, 0); W32(o + 0x30, 0);
        AND16(o + 4, 0xFFFE);
        if (RS16(o + 0x16) < 1) { face(o); OR16(PL + 4, 0x400); K_Set(0x1B); }
        else { W32(o + 0x30, 0); K_Set(0); W16(o + 0x52, 0x1E); OR16(o + 4, 0x80); }
        break;
    case 0x1B:
        if (R16(FADE_STATE) == 0 && Ram_Ptr(0x06025114u)[0] == 0 && R32(0x06025108u) != 0x01000000u) {
            Fade_SetTarget(0x100, 0x100, 0x100);
            Fade_SetEndLayerMask(0);
            Fade_SetNextSpeed(0x20000, 1);
            Fade_StartOut();
            W16(0x0602529Eu, (R16(0x0602529Eu) & 0xFFF8) | 10);
        }
        if (R32(0x06025108u) == 0x01000000u) { W16(o, 0xFFFF); return 1; }
        break;
    case 0x3ED: K_Set(0x12); break;
    case 0x3F1: AND16(o + 2, 0xFFFB); K_Set(0); break;
    case 0x3F7: K_SpecialSword(); break;
    case 0x3F8: K_SpecialStorm(); break;
    case 0x3F9: K_SpecialDragon(); break;
    default: K_Set(0); break;
    }
    return 0;
}

void EnemyHandler_001F(void)                                /* 0x0607E840 */
{
    uint32_t o = O;
    if (R16(o + 2) & 1) K_Init();
    if (RS16(K_GFX) != 0 && ((RS16(K_ORBS) == 3 && RS16(o + 0x4E) == 3) || (RS16(K_ORBS) == 4 && RS16(o + 0x4E) == 5))) {
        W16(K_GFX, 0);
        Kazuma_PhaseGfx();
    }
    if (!FROZEN) {
        Hud_DrawBossBar(0x300, RS16(o + 0x16));
        if (RS16(o + 0x50) < 4) {
            if (R16(o + 0x50) == 0) AND16(o + 0x1C, 0x3FFF);
            else { AND16(o + 0x1C, 0x7FFF); OR16(o + 0x1C, 0x4000); }
        } else { AND16(o + 0x1C, 0xBFFF); OR16(o + 0x1C, 0x8000); }
        int r = Enemy_AttackPlayer((int16_t)(RS16(K_ACT) == 0x3F7 ? 2 : 1), 0, 0xC000);
        if (r == 3) sub_06007CDE(R16(o + 6) == 0x14 ? 0x10C : 0x104);
        if (!(R16(o + 4) & 2) && !(R16(o + 4) & 0x80) && (RS16(K_ACT) != 0x20 || (R16(o + 0x58) & 3) != 0))
            Enemy_CheckPlayerHits(0xC000);
        if (RS16(o + 0x16) < 1 && R16(o + 6) != 0x1B) K_Dying(o);
        if ((R16(o + 2) & 8) && !(R16(o + 4) & 0x80) && R16(o + 6) != 0x1B && R16(o + 6) != 0x18) {   /* hurt */
            if (RS16(o + 0x16) > 0) sub_06007CDE(0x22A);
            Boss1F_OrbTaken(o);
            if (RS16(K_ORBS) < 6) {
                if (EM_Abs(RS32(HIT_POINT) - RS32(o + 8)) > 0x280000) W32(HIT_POINT, R32(o + 8));
                if (EM_Abs(RS32(HIT_POINT + 4) - RS32(o + 0xC) + 0x320000) > 0x640000) W32(HIT_POINT + 4, (uint32_t)(RS32(o + 0xC) - 0x320000));
                orb_spawn(RS32(HIT_POINT), RS32(HIT_POINT + 4), RS16(K_ORBIX));
            }
            face(o);
            W16(o + 0x4A, 0); W16(o + 0x50, 0); W32(o + 0x30, 0);
            AND16(o + 4, 0xFFDE);
            if (RS16(o + 0x16) < 1) { OR16(PL + 4, 0x400); K_Set(0x1B); }
            else {
                W32(o + 0x34, 0x30000);
                W32(o + 0x30, right_facing(o) ? 0xFFFD8000u : 0x28000u);
                K_Set(0x18);
            }
            Enemy_AnimSwitch();
        }
        if (R16(o + 4) & 0x80) {                                    /* invulnerable after a hit */
            if (R16(o + 0x52) != 0) W16(o + 0x52, R16(o + 0x52) - 1);
            if (RS16(o + 0x52) < 1) {
                if (RS16(o + 0x16) < 1) K_Dying(o);
                else AND16(o + 2, 0xFFF7);
                W16(o + 0x52, 0);
                AND16(o + 4, 0xFF7F);
            }
        }
        if (!(R16(o + 0x58) & 0x8000)) K_Running(o);
        else if (K_Ended(o)) return;
        Obj_Integrate(o);
        EM_ClampX(o);
        Obj_CollideMap(o, 1, 1);
        K_Palette();
        Enemy_AnimSwitch();
    }
    if (R16(o + 6) != 0x1B) {
        const uint32_t EX = 0x060FFE50u;                    /* the original's stack record */
        W32(EX, R16(o + 0x4A) ? 2u : 0u);
        W16(EX + 4, 0);
        W16(EX + 6, R16(o + 0x4A));
        EM_TrailEx(R16(o + 4) & 1, o, EX);
    }
    W16(SPR + 2, (uint32_t)((RS32(o + 8) - RS32(CAM_X)) >> 16));
    W16(SPR + 4, (uint32_t)((RS32(o + 0xC) - RS32(CAM_Y)) >> 16));
    W16(SPR + 0x10, R16(o + 2) & 0x8000);
    if (R16(o + 0x4A) != 0) { W16(SPR + 0x10, R16(SPR + 0x10) | 0x400); W16(SPR + 0x14, R16(o + 0x4A)); }
    W16(SPR + 0x0A, (uint16_t)COLOR_BANK(SLOT));
    W16(SPR + 0x0C, 0);
    W16(SPR + 0x0E, R16(o + 0x50));
    if (R16(SPR + 0x0E) < 4) { Anim_Tick(o + 0x58); K_Flash(); }
    W16(K_PENDON, 0); W16(K_DRAGON, 0);
    W32(K_BX, R32(o + 8)); W32(K_BY, R32(o + 0xC));
}

/* ---------------------------------------------------------------- projectiles / effect */

/* Projectile 0x18 (0x06080868): spinning blade (anim 0x1F), moves by its velocity, hurts
 * Sho (1, mode 0x3800); freed off screen or 16 frames after being knocked back. */
void Proj_KazumaBlade18(uint32_t e)
{
    int16_t sl = (int16_t)Enemy_FindSlot(0x1F);
    if (R16(e + 4) & 1) {
        Anim_Start(e + 0x20, B1F_DEFS, B1F_SCR, CHAR_BASE(sl), 0x1F);
        AND16(e + 4, 0xFFFE);
        W16(e + 0x14, 0); W16(e + 6, 0);
    }
    if (!FROZEN) {
        W32(e + 8, R32(e + 8) + R32(e + 0x18));
        W32(e + 0xC, R32(e + 0xC) - R32(e + 0x1C));
        W16(e + 0x14, R16(e + 0x14) + 8);
        Projectile_Collide(1, 0x3800);                          /* 0x060812BC(0x1F) = 1 */
        int32_t dx = RS32(e + 8) - RS32(CAM_X), dy = RS32(e + 0xC) - RS32(CAM_Y);
        if (dx < -0x800000 || dx > 0x1C00000 || dy < -0x800000 || dy > 0x1C00000) W16(e, 0xFFFF);
        if (R16(e + 4) & 0x4000) { W16(e + 6, R16(e + 6) + 1); if (R16(e + 6) > 0xF) W16(e, 0xFFFF); }
    }
    if (R16(e) == 0xFFFF) return;
    W16(SPR + 2, (uint32_t)((RS32(e + 8) - RS32(CAM_X)) >> 16));
    W16(SPR + 4, (uint32_t)((RS32(e + 0xC) - RS32(CAM_Y)) >> 16));
    W16(SPR + 0x0A, (uint16_t)COLOR_BANK(sl));
    W16(SPR + 0x10, 0x600);
    W32(SPR + 0x18, 0xFFFFFFFFu);
    W16(SPR + 0x14, R16(e + 0x14));
    W16(SPR + 0x0C, 0); W16(SPR + 0x0E, 0);
    Anim_Tick(e + 0x20);
}

/* Projectile 0x1B (0x06082AE0): a flame swinging from its pivot (x, y stay the pivot;
 * the flame point is computed every frame): +0x10 == 0 a 320 px pendulum (angle 8.8,
 * speed +0x18 up to 0xF0, turning at 0x2800 / 0x5800, hurts 2), else a 20 px one
 * (start angle 0x060BC30C[], harmless, falls to y 0x490); the pivot sinks 0.42 px/frame.
 * Under ninjutsu 6 it fades out in 7 frames. Sets 0x060F7B2C every frame. */
void Proj_KazumaFlame1B(uint32_t e)
{
    W16(K_PENDON, 1);
    if (R16(e + 4) & 1) {
        Anim_Start(e + 0x20, 0x060BC16Eu, 0x060E1A9Cu, 0x132, 0);
        AND16(e + 4, 0xFFFE);
        if (R16(e + 0x10) == 0) W16(e + 0x14, 0x4000);
        else {
            W16(e + 0x14, R16(0x060BC30Cu + 2u * (uint32_t)(int16_t)R16(K_PEND)));
            if (R16(K_PEND) < 4) W16(K_PEND, R16(K_PEND) + 1); else W16(K_PEND, 0);
        }
        W32(e + 0x18, 0);
        W16(e + 6, 7);
    }
    int32_t px = RS32(e + 8), py = RS32(e + 0xC);
    if (!FROZEN) py += 0x6C00;
    uint16_t a = R16(e + 0x14);
    if (R16(e + 0x10) == 0) {
        int32_t x = FixMul(0x1400000, Cos8(a >> 8));
        x += FixMul(FixMul(0x1400000, Cos8((a >> 8) + 1)) - x, (int32_t)((a & 0xFF) << 8));
        W32(e + 8, (uint32_t)(x + px));
        int32_t y = FixMul(0x1400000, Sin8(a >> 8));
        y += FixMul(FixMul(0x1400000, Sin8((a >> 8) + 1)) - y, (int32_t)((a & 0xFF) << 8));
        W32(e + 0xC, (uint32_t)(y + py - 0x1400000));
        if (!FROZEN) {
            W16(e + 0x14, (uint16_t)(R16(e + 0x14) + (uint16_t)R32(e + 0x18)));
            if (RS32(e + 0x1C) == 0) {
                W32(e + 0x18, R32(e + 0x18) + 10);
                if (RS32(e + 0x18) > 0xF0) W32(e + 0x18, 0xF0);
                if (R16(e + 0x14) > 0x57FF) W32(e + 0x1C, 1);
            } else {
                W32(e + 0x18, R32(e + 0x18) - 10);
                if (RS32(e + 0x18) < -0xF0) W32(e + 0x18, 0xFFFFFF10u);
                if (R16(e + 0x14) < 0x2801) W32(e + 0x1C, 0);
            }
            W16(e + 0x12, 0x7FFF);
            Projectile_Collide(2, 0xC000);
            if (BOTTOM + 0x140000 <= RS32(e + 0xC)) { W16(e, 0xFFFF); return; }
        }
    } else {
        W32(e + 8, (uint32_t)(FixMul(0x140000, Cos8(a >> 8)) + px));
        W32(e + 0xC, (uint32_t)(FixMul(0x140000, Sin8(a >> 8)) + py - 0x140000));
        if (!FROZEN) {
            W16(e + 0x14, (uint16_t)(R16(e + 0x14) + (uint16_t)R32(e + 0x18)));
            if (RS32(e + 0x1C) == 0) {
                W32(e + 0x18, R32(e + 0x18) + 2);
                if (RS32(e + 0x18) > 0x100) W32(e + 0x18, 0x100);
                if (R16(e + 0x14) > 0x7FFE) W32(e + 0x1C, 1);
            } else {
                W32(e + 0x18, R32(e + 0x18) - 2);
                if (RS32(e + 0x18) < -0x100) W32(e + 0x18, 0xFFFFFF00u);
                if (R16(e + 0x14) < 2) W32(e + 0x1C, 0);
            }
            if (RS32(e + 0xC) > 0x48FFFFF) { W16(e, 0xFFFF); return; }
        }
    }
    if (R16(e) != 0xFFFF) {
        W16(SPR + 2, (uint32_t)((RS32(e + 8) - RS32(CAM_X)) >> 16));
        W16(SPR + 4, (uint32_t)((RS32(e + 0xC) - RS32(CAM_Y)) >> 16));
        W16(SPR + 0x0A, 0x20);
        W16(SPR + 0x10, 0x600);
        if (R16(e + 0x10) != 0) { W16(SPR + 0x10, 0x1E00); W32(SPR + 0x1C, 0x6000); W32(SPR + 0x20, 0x6000); }
        W16(SPR + 0x14, (uint16_t)(0x140 - (R16(e + 0x14) >> 8)));
        W32(SPR + 0x18, 0x85);
        W16(SPR + 0x0C, 1);
        if (R16(e + 0x10) == 0) W16(SPR + 0x0E, R16(e + 0x20) & 3);
        else W16(SPR + 0x0E, RS32(e + 0xC) > 0x47FFFFF ? 3 : 2);
        if (FROZEN && (R16(0x060252BAu) & 0x8000) && (R16(0x060252BAu) & 0xF) == 6) {
            W16(SPR + 0x0E, (uint16_t)(3 - sdiv(R16(e + 6), 4)));
            W16(e + 6, R16(e + 6) - 1);
            if (R16(e + 6) == 0) { W16(e, 0xFFFF); return; }
        }
        Anim_Tick(e + 0x20);
    }
    W32(e + 8, (uint32_t)px);
    W32(e + 0xC, (uint32_t)py);
}

static int16_t hist_wrap(int16_t a, int16_t b, int16_t n)  /* 0x060840FA */
{
    int16_t i = (int16_t)(a + b);
    while (n <= i) i = (int16_t)(i - n);
    return i;
}

static void spark(int32_t x, int32_t y, int16_t ang)        /* 0x06084124 */
{
    uint32_t r1 = ((uint32_t)sub_0602DDD2() >> 8) & 0x1FFFFF;
    uint32_t r2 = ((uint32_t)sub_0602DDD2() >> 8) & 0x1FFFFF;
    int32_t c = -FixMul(0x28000, Cos8(ang));
    int32_t s = FixMul(0x28000, Sin8(ang));
    uint16_t f = (uint16_t)(((uint32_t)sub_0602DDD2() >> 8) & 1);
    int16_t k = (int16_t)Obj_Register(0x104F, x + (int32_t)r1, y - (int32_t)r2 + 0x100000, f, 0xFFFF);
    if (k != -1) { W32(OBJ(k) + 0x30, (uint32_t)c); W32(OBJ(k) + 0x34, (uint32_t)s); }
}

static void dragon_trail(void)                              /* body parts dropped by the head */
{
    int16_t c = RS16(K_HCNT);
    if (c >= 0x8C || c == 0 || smod(c, 4) != 0 || sdiv(c, 4) > 32) return;
    uint32_t h = HIST(RS16(K_HHEAD));
    int16_t s = (int16_t)Projectile_Spawn(0x1C, RS32(h), RS32(h + 4), (int16_t)R16(h + 8), 0);
    if (s == -1) return;
    W16(K_DSEGS, R16(K_DSEGS) + 1);
    OR16(PROJ(s) + 4, R16(HIST(RS16(K_HHEAD)) + 0xA) & 0x8000);
    W16(PROJ(s) + 6, (uint16_t)sdiv(RS16(K_HCNT), 4));
}

static void dragon_fade_boss(void)
{
    W16(K_TIM, R16(K_TIM) - 1);
    if (RS16(K_TIM) >= 1) return;
    W16(K_TIM, 5);
    for (int16_t s = 0; s < 0xC0; s++) {
        if (R16(OBJ(s)) != 0x1F) continue;
        if (RS16(OBJ(s) + 0x50) < 4) W16(OBJ(s) + 0x50, R16(OBJ(s) + 0x50) + 1);
        if (RS16(OBJ(s) + 0x50) == 4) W16(K_TIM, 10000);
        break;
    }
}

/* Projectile 0x1C (0x060833AC): the fire dragon. Head (+0x10 != 0, anim 2 of 0x060BC1BE,
 * chars 0x132): crosses the arena (3.5 px/frame), then three dives from the far side
 * aimed at Sho (angle clamped to 0x8E..0xF2, swaying +-0x34) while the boss fades out
 * (+0x50 -> 4); records its path into the ring. Body parts (anim 3) replay the ring 4
 * entries apart per index (+6), dropping sparks; parts of index < 27 hurt Sho (2, 20
 * frames' grace, 48 px box). Fading in over 48 frames. Sets 0x060F7B2E every frame. */
void Proj_KazumaDragon1C(uint32_t e)
{
    W16(K_DRAGON, 1);
    if (R16(e + 4) & 1) {
        Anim_Start(e + 0x20, 0x060BC1BEu, 0x060E1AC8u, 0x132, R16(e + 0x10) ? 2 : 3);
        AND16(e + 4, 0xFFFE);
        if (R16(e + 0x10) != 0) W16(e + 6, 0);
        W32(e + 0x18, 0); W32(e + 0x1C, 0);
    }
    if (!FROZEN) {
        W32(e + 0x1C, R32(e + 0x1C) + 1);
        if (R16(e + 0x10) == 0) {                                   /* body */
            int16_t c = RS16(K_HCNT);
            if (R16(e + 6) == 0) { c = (int16_t)(c - 1); if (c < 0) c = 0; }
            int16_t i = hist_wrap(RS16(K_HHEAD), (int16_t)(c - (int16_t)R16(e + 6) * 4), 0x8C);
            uint32_t h = HIST(i);
            W32(e + 8, R32(h)); W32(e + 0xC, R32(h + 4));
            W16(e + 0x14, R16(h + 8));
            AND16(e + 4, 0x7FFF); OR16(e + 4, R16(h + 0xA) & 0x8000);
            if ((((uint32_t)sub_0602DDD2() >> 7) & 0xFF) == 0xFF && sdiv(R16(e + 6), 9) < 3)
                spark(RS32(e + 8), RS32(e + 0xC), RS16(e + 0x14));
            if (BOTTOM + 0x780000 <= RS32(e + 0xC) || RS32(e + 0xC) <= TOP - 0x780000 ||
                RIGHT + 0x780000 <= RS32(e + 8) || RS32(e + 8) <= LEFT - 0x780000) {
                W16(K_DSEGS, R16(K_DSEGS) - 1);
                W16(e, 0xFFFF);
                return;
            }
        } else {                                                    /* head */
            switch (R16(e + 6)) {
            case 0:
                if (R16(e + 4) & 0x8000) { if (LEFT - 0x640000 < RS32(e + 8)) W32(e + 8, R32(e + 8) - 0x38000); }
                else if (RS32(e + 8) < RIGHT + 0x640000) W32(e + 8, R32(e + 8) + 0x38000);
                if (RS16(K_HCNT) < 0x8C) dragon_trail();
                else if (R16(e + 4) & 0x8000) { if (!(LEFT - 0x640000 < RS32(e + 8))) W16(e + 6, R16(e + 6) + 1); }
                else if (RIGHT + 0x640000 <= RS32(e + 8)) W16(e + 6, R16(e + 6) + 1);
                break;
            case 1: case 4: case 7: {
                dragon_fade_boss();
                dragon_trail();
                if (RS32(PL + 8) < ((LEFT + RIGHT + (int32_t)0xF8000000) >> 1) + 0x4000000) {
                    OR16(e + 4, 0x8000); W32(e + 8, (uint32_t)(RIGHT + 0x3C0000)); W32(e + 0xC, (uint32_t)(TOP + 0x640000));
                } else {
                    AND16(e + 4, 0x7FFF); W32(e + 8, (uint32_t)(LEFT - 0x3C0000)); W32(e + 0xC, (uint32_t)(TOP + 0x640000));
                }
                W32(0x060F81C4u, R32(PL + 8)); W32(0x060F81C8u, R32(PL + 0xC));
                int16_t a = (int16_t)Atan8((int16_t)(R32(e + 8) >> 16), (int16_t)(R32(e + 0xC) >> 16), (int16_t)(R32(PL + 8) >> 16),
                                           (int16_t)((uint32_t)(RS32(PL + 0xC) + 0x200000) >> 16));
                if (a < 0x8E || a > 0xF2) a = (a < 0x40 || a > 0xF2) ? 0xF2 : 0x8E;
                if (EM_Abs(a - 0xC0) < 0x32) a = a < 0xC0 ? 0x8E : 0xF2;
                W16(K_DANG, (uint16_t)a);
                W16(e + 0x14, (uint16_t)a);
                W16(K_DIR, (R16(e + 4) & 0x8000) ? 0xFFFF : 1);
                W16(e + 6, R16(e + 6) + 1);
                break;
            }
            case 2: case 5: case 8:
                dragon_fade_boss();
                dragon_trail();
                if (smod(VBL, 40) == 0) sub_06007CDE(0x113);
                W16(e + 0x14, (uint16_t)(RS16(e + 0x14) + RS16(K_DIR) * 2));
                if (RS16(K_DIR) < 1) { if (RS16(e + 0x14) <= (int16_t)(RS16(K_DANG) - 0x34)) W16(K_DIR, 1); }
                else if (RS16(K_DANG) + 0x34 <= RS16(e + 0x14)) W16(K_DIR, 0xFFFF);
                W32(e + 8, (uint32_t)(RS32(e + 8) + FixMul(0x38000, Cos8(RS16(e + 0x14)))));
                W32(e + 0xC, (uint32_t)(RS32(e + 0xC) - FixMul(0x38000, Sin8(RS16(e + 0x14)))));
                if (BOTTOM + 0x780000 < RS32(e + 0xC) || RS32(e + 0xC) < TOP - 0x780000 ||
                    RIGHT + 0x780000 < RS32(e + 8) || RS32(e + 8) < LEFT - 0x780000) W16(e + 6, R16(e + 6) + 1);
                break;
            case 3: case 6: case 9:
                if (RS16(K_DSEGS) == 0) {
                    W16(K_HCNT, 0); W16(K_HHEAD, 0);
                    if (R16(e + 6) == 9) { W16(e, 0xFFFF); return; }
                    W16(e + 6, R16(e + 6) + 1);
                }
                break;
            default: break;
            }
            int16_t c = RS16(K_HCNT);
            int16_t i = hist_wrap(RS16(K_HHEAD), c, 0x8C);
            uint32_t h = HIST(i);
            W32(h, R32(e + 8)); W32(h + 4, R32(e + 0xC)); W16(h + 8, R16(e + 0x14)); W16(h + 0xA, R16(e + 4));
            int16_t nc = (int16_t)(c + 1);
            if (i == RS16(K_HHEAD) && nc > 1) {
                nc = c;
                if (R16(K_HHEAD) < 0x8B) W16(K_HHEAD, R16(K_HHEAD) + 1); else W16(K_HHEAD, 0);
            }
            W16(K_HCNT, (uint16_t)nc);
        }
        if (sdiv(R16(e + 6), 9) < 3 && TOP < RS32(PL + 0xC)) {
            if (!(R16(PL + 2) & 8) && R32(PL + 0x68) != 0 && R16(R32(0x060EA41Cu)) == 0 && RS32(e + 0x18) == 0) {
                ObjRect me = { RS32(e + 8) - 0x180000, RS32(e + 0xC) - 0x180000, RS32(e + 8) + 0x180000, RS32(e + 0xC) + 0x180000 };
                for (uint32_t b = R32(PL + 0x68); RS16(b) != -1; b += 10) {
                    if (RS16(b) != 0 && RS16(b) != 3) continue;
                    int flip = (R16(PL + 2) & 0x8000) != 0;
                    int32_t L = flip ? RS16(b + 2) * -0x10000 : RS16(b + 2) * 0x10000;
                    int32_t R = flip ? RS16(b + 6) * -0x10000 : RS16(b + 6) * 0x10000;
                    int32_t t0 = RS16(b + 4) * 0x10000 + RS32(PL + 0xC), t1 = RS16(b + 8) * 0x10000 + RS32(PL + 0xC);
                    ObjRect sho = { (L + RS32(PL + 8) < R + RS32(PL + 8) ? L : R) + RS32(PL + 8), t0 < t1 ? t0 : t1,
                                    (R + RS32(PL + 8) < L + RS32(PL + 8) ? L : R) + RS32(PL + 8), t1 < t0 ? t0 : t1 };
                    if (Rect_Overlap(&sho, &me)) { Player_TakeDamage(2); W32(e + 0x18, 0x14); break; }
                }
            } else if (RS32(e + 0x18) != 0) W32(e + 0x18, R32(e + 0x18) - 1);
        }
    }
    W16(SPR + 2, (uint32_t)((RS32(e + 8) - RS32(CAM_X)) >> 16));
    W16(SPR + 4, (uint32_t)((RS32(e + 0xC) - RS32(CAM_Y)) >> 16));
    W16(SPR + 0x0A, 0x60);
    W16(SPR + 0x10, ((R16(e + 4) & 0x8000) ^ 0x8000) | 0x600);
    W16(SPR + 0x14, R16(e + 0x14));
    if (R16(e + 4) & 0x8000) W16(SPR + 0x14, (uint16_t)(R16(SPR + 0x14) - 0x80));
    W32(SPR + 0x18, 0xA8);
    if (R16(e + 0x10) == 0) W32(SPR + 0x18, (uint32_t)(0xA8 - (R16(e + 6) + 1)));
    W16(SPR + 0x0C, 1);
    if (RS32(e + 0x1C) > 48) {
        if (R16(e + 0x10) == 0) { W16(SPR + 0x0E, (uint16_t)sdiv(R16(e + 6), 9)); if (R16(SPR + 0x0E) >= 4) W16(SPR + 0x0E, 3); }
        else W16(SPR + 0x0E, 0);
    } else {
        W16(SPR + 0x0E, (uint16_t)(3 - sdiv(RS32(e + 0x1C), 16)));
        if (R16(e + 0x10) == 0 && R16(SPR + 0x0E) < sdiv(R16(e + 6), 9)) {
            W16(SPR + 0x0E, (uint16_t)sdiv(R16(e + 6), 9));
            if (R16(SPR + 0x0E) >= 4) W16(SPR + 0x0E, 3);
        }
    }
    Anim_Tick(e + 0x20);
}

/* Effect 0x14 (0x060841E4): explosion on the dying boss (anim 0x1D / 0x1E at random,
 * sound 0x209, bank 0x5F, blinking). */
void Fx_KazumaBlast14(uint32_t e)
{
    if (R16(e + 2) & 1) {
        int anim = (((uint32_t)sub_0602DDD2() >> 8) & 1) ? 0x1D : 0x1E;
        Anim_Start(e + 0x10, B1F_DEFS, B1F_SCR, CHAR_BASE(Enemy_FindSlot(0x1F)), anim);
        AND16(e + 2, 0xFFFE);
        sub_06007CDE(0x209);
    }
    if (!(VBL & 1)) AND16(e + 0x10, 0xBFFF); else OR16(e + 0x10, 0x4000);
    W16(SPR + 2, (uint32_t)((RS32(e + 8) - RS32(CAM_X)) >> 16));
    W16(SPR + 4, (uint32_t)((RS32(e + 0xC) - RS32(CAM_Y)) >> 16));
    W16(SPR + 0x10, 0x200);
    W32(SPR + 0x18, 0x81);
    W16(SPR + 0x0A, 0x5F);
    W16(SPR + 0x0C, 0); W16(SPR + 0x0E, 0);
    Anim_Tick(e + 0x10);
    if (R16(e + 0x10) & 0x8000) W16(e, 0xFFFF);
}
