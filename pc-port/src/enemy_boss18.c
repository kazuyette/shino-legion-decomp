/* Enemy type 0x0018: stage 1-2 boss, the giant Hannya mask (index 1, file STAGE7_2).
 * Loader 0x0609FFF4 (chars 0x060E8468, 256-colour palette 0x060E8A78, opens HANNYA2.PBC),
 * handler 0x060A00EA. Subtype 0 = the mask, 1 = death flame (0x060A1A44, anim 10, bank
 * +0xE), 2 = falling fragment (0x060A1974, anim 0xB, rotated). Projectiles 0x10 (fire
 * pillar 0x060A152C) and 0x11 (fireball 0x060A1354, burst = projectile 5).
 * State block 0x060F832C (see M_* below). HP 0x060C6172[difficulty]; boss bar 0x06061B58.
 * Intro (0): fades in (palette 0x060E8878 by Pal_StepTowards), 4 appearing frames.
 * 1: drifts sideways on a sine wave (0x060A103C picks the wave: 0x060C6230[rand & 3] px
 *    amplitude) or picks a move from 0x060C6178/0x060C6198[rand >> 8 & 0xF]: 2 tilt and
 *    spit two aimed fireballs (0x060A10E8), 3 (unused), 4 charge (spins, flies at Sho
 *    at 3 px/frame bouncing off the arena 0x060A1300, then back home), 5 turn round,
 *    6 rain of fire pillars (0x060C61B8 / 0x060C6218 columns, one gap at +0x48).
 * Hurt: white flash 33 frames (palette 0x060E8878); dead: 7 = flames + fragments, then
 * fade to white 0x060A00A8 and stage state |= 10 (clear). Sho's input is locked
 * (PL+4 |= 0x400) during the intro and after the death. */
#include "enemy_more.h"
#include "anim.h"
#include "sprite.h"
#include "objects.h"
#include "player_ext.h"
#include "fixmath.h"
#include "resource.h"
#include "fade.h"
#include "hud.h"
#include "objhandlers_d.h"
#include "fx.h"

#define B18_DEFS 0x060C54B8u
#define B18_SCR  0x060E91DCu
#define M_FRAME  0x060F832Cu   /* s16 appearing frame (4 = hidden .. 0) */
#define M_ANGLE  0x060F832Eu   /* s16 sprite rotation */
#define M_STEP   0x060F8330u   /* s16 step of the current action */
#define M_HOMEY  0x060F8334u   /* s32 home Y */
#define M_WSPD   0x060F8338u   /* s32 wave phase speed */
#define M_WPH    0x060F833Cu   /* s32 wave phase */
#define M_FLASH  0x060F8340u   /* s16 hurt flash counter */
#define M_NEXT   0x060F8342u   /* u16 next move (0x8000 = turn first) */
#define M_HIT    0x060F8344u   /* s16 hit since the last pick */
#define PROJ(i)  (PROJECTILES + (uint32_t)((int16_t)(i) * 0x40))

uint16_t Enemy18_Load(void)                                 /* 0x0609FFF4 */
{
    Enemy_RegisterChars(0x18, 0x060E8468u);
    int16_t s = (int16_t)Enemy_FindSlot(0x18);
    Pal_Copy((R16(0x060A9084u + 2u * (uint32_t)s) & 0xFFF) << 4, 0x100, 0x060E8A78u);
    return (uint16_t)Res_Request(2, 0, 0, 0xFFFFFFFFu, 0, 0x060C6224u);   /* "HANNYA2.PBC" */
}

static int pal_dst(void) { return (R16(0x060A9084u + 2u * (uint32_t)SLOT) & 0xFFF) << 4; }

static void Hannya_Init(void)                               /* 0x060A0F3C */
{
    uint32_t o = O;
    W16(o + 6, 0);
    Anim_Start(o + 0x58, B18_DEFS, B18_SCR, CHAR_BASE(SLOT), RS16(o + 6));
    Anim_Tick(o + 0x58);
    AND16(o + 2, 0xFFFE);
    OR16(o + 2, 0x8000);
    W16(o + 0x16, R16(0x060C6172u + 2u * (uint32_t)RS16(0x060252C4u)));
    W16(o + 0x14, 0);
    W16(M_FRAME, 4); W16(M_ANGLE, 0); W16(M_STEP, 0);
    W32(M_HOMEY, R32(o + 0xC)); W32(M_WSPD, 0); W32(M_WPH, 0);
    W16(M_FLASH, 0); W16(M_NEXT, 0); W16(M_HIT, 0);
    OR16(PL + 4, 0x400);
}

/* 0x060A103C: near a wall -> 1 (turn); else a new sine drift (0 = drift) */
static int Hannya_Wave(void)
{
    uint32_t o = O;
    if (!(R16(o + 2) & 0x8000)) { if (0x05700000 - RS32(o + 8) < 0x00500000) return 1; }
    else if (RS32(o + 8) - 0x04600000 < 0x00500000) return 1;
    uint32_t r = (uint32_t)sub_0602DDD2();
    int32_t a = FixDiv((int32_t)RS16(0x060C6230u + 2u * ((r >> 8) & 3)) << 16, 0x20000);
    int32_t v = FixDiv(0x00800000, a);
    if (RS32(M_WSPD) >= 0) v = -v;
    W32(M_WSPD, (uint32_t)v);
    W32(M_WPH, 0);
    return 0;
}

static void Hannya_Spit(void)                               /* 0x060A10E8: two aimed fireballs */
{
    uint32_t o = O;
    static const int32_t dx[2] = { 0x1A0000, 0x200000 }, dy[2] = { (int32_t)0xFFBF0000, (int32_t)0xFFF80000 };
    static const int32_t oy[2] = { -0x210000, -0x280000 };
    for (int k = 0; k < 2; k++) {
        int32_t d = (R16(o + 2) & 0x8000) ? -dx[k] : dx[k];
        int ang = Atan8((int16_t)((uint32_t)(RS32(o + 8) + d) >> 16), (int16_t)((uint32_t)(RS32(o + 0xC) + dy[k]) >> 16),
                        (int16_t)((uint32_t)RS32(PL + 8) >> 16), (int16_t)((uint32_t)(RS32(PL + 0xC) - 0x200000) >> 16));   /* 0x060498BE */
        int s = Projectile_Spawn(0x11, RS32(o + 8) + d, RS32(o + 0xC) + oy[k], (int16_t)ang, 0x50000);
        if ((int16_t)s >= 0) {
            uint32_t p = PROJ(s);
            OR16(p + 4, R16(o + 2) & 0x8000);
            W16(p + 0x14, 0);
            W16(p + 0x12, 1);
        }
    }
    sub_06007CDE(0x125);
}

/* 0x060A122A: charge velocity, 3 px/frame: 0 at Sho, 1 back home (x 0x4C0), 2 bounce */
static void Hannya_Aim(int16_t mode)
{
    uint32_t o = O;
    int16_t tx, ty;
    if (mode == 1) { tx = 0x4C0; ty = (int16_t)(R32(M_HOMEY) >> 16); }
    else { tx = (int16_t)(R32(PL + 8) >> 16); ty = (int16_t)((uint32_t)(RS32(PL + 0xC) - 0x200000) >> 16); }
    int a = Atan8((int16_t)(R32(o + 8) >> 16), (int16_t)(R32(o + 0xC) >> 16), tx, ty);
    if (mode == 2 && !(R16(PL + 2) & 0x4000)) a = RS32(o + 0x30) < 0 ? 0x59 : 0x19;
    W32(o + 0x30, (uint32_t)FixMul(Cos8(a), 0x30000));
    W32(o + 0x34, (uint32_t)FixMul(Sin8(a), 0x30000));
}

static int Hannya_Arena(void)                               /* 0x060A1300 */
{
    uint32_t o = O;
    if (RS32(o + 8) < 0x040F0000 || RS32(o + 8) > 0x05D10000 || RS32(o + 0xC) < 0x04400000) return 1;
    if (RS32(o + 0xC) > 0x04E00000) return 2;
    return 0;
}

static void Hannya_Flames(void)                             /* 0x060A1A44 */
{
    uint32_t o = O;
    for (uint32_t k = 0; k < 10; k++) {
        uint32_t t = 0x060C6238u + k * 8;
        int s = Obj_Register(0x18, RS32(o + 8) + RS16(t) * 0x10000, RS32(o + 0xC) + RS16(t + 2) * 0x10000, 1, 0xFFFF);
        if ((int16_t)s >= 0) W16(OBJ_TABLE + (uint32_t)(int16_t)(s * 0x78) + 0x48, R16(t + 4));
    }
}

static void Hannya_Fragment(void)                           /* 0x060A1974 */
{
    uint32_t o = O;
    uint32_t r = (uint32_t)sub_0602DDD2();
    int a = (r >> 8) & 0xFF;
    int32_t vx = FixMul(Cos8(a), 0x20000), vy = FixMul(Sin8(a), 0x20000);
    int16_t d = (int16_t)((((uint32_t)sub_0602DDD2() >> 8) & 0x1F) + 7);
    uint32_t r3 = (uint32_t)sub_0602DDD2();
    int s = Obj_Register(0x18, d * vx + RS32(o + 8), d * -vy + RS32(o + 0xC), (uint16_t)(((r3 >> 8) & 3) * 4 + 2), 0xFFFF);
    if ((int16_t)s >= 0) {
        uint32_t f = OBJ_TABLE + (uint32_t)(int16_t)(s * 0x78);
        W32(f + 0x30, (uint32_t)vx);
        W32(f + 0x34, (uint32_t)-vy);
    }
}

static void Hannya_StopShots(void)                          /* 0x060A1ACE */
{
    for (int k = 0; k < 0x40; k++) {
        uint16_t t = R16(PROJ(k));
        if (t == 0x10 || t == 0x11) W16(PROJ(k) + 0x12, 0);
    }
}

static void Hannya_Dead(void)                               /* 0x060A00A8 */
{
    Fade_SetTarget(0x100, 0x100, 0x100);
    Fade_SetEndLayerMask(0);
    Fade_SetNextSpeed(0x40000, 1);
    Fade_StartOut();
    W16(0x0602529Eu, (R16(0x0602529Eu) & 0xFFF8) | 10);
    W16(O, 0xFFFF);
}

static void Hannya_Flame(uint32_t o)                        /* subtype 1 */
{
    if (R16(o + 2) & 1) {
        W16(o + 6, 10);
        Anim_Start(o + 0x58, B18_DEFS, B18_SCR, CHAR_BASE(SLOT), RS16(o + 6));
        AND16(o + 2, 0xFFFE);
        W16(o + 0x14, 0);
    }
    if (!FROZEN) {
        int16_t d = RS16(o + 0x48);
        W16(o + 0x48, (uint16_t)(d - 1));
        if (d > 0) return;
        if (R16(o + 0x58) & 0x8000) { W16(o, 0xFFFF); return; }
    }
    W16(SPR + 0x0A, (uint16_t)(COLOR_BANK(SLOT) + 0xE));
    W16(SPR + 2, (uint32_t)((RS32(o + 8) - RS32(CAM_X)) >> 16));
    W16(SPR + 4, (uint32_t)((RS32(o + 0xC) - RS32(CAM_Y)) >> 16));
    W16(SPR + 0x10, 0x200);
    W16(SPR + 0x0C, 0);
    W32(SPR + 0x18, 0x80);
    W16(SPR + 0x0E, R16(o + 0x58) & 3);
    Anim_Tick(o + 0x58);
}

static void Hannya_Piece(uint32_t o)                        /* subtype 2 */
{
    if (R16(o + 2) & 1) {
        W16(o + 6, 0xB);
        Anim_Start(o + 0x58, B18_DEFS, B18_SCR, CHAR_BASE(SLOT), RS16(o + 6));
        AND16(o + 2, 0xFFFE);
        W16(o + 0x14, 0);
    }
    if (!FROZEN) {
        W32(o + 8, (uint32_t)(RS32(o + 8) + RS32(o + 0x30)));
        W32(o + 0xC, (uint32_t)(RS32(o + 0xC) + RS32(o + 0x34)));
        W32(o + 0x30, (uint32_t)FixMul(RS32(o + 0x30), 0xFD00));
        W32(o + 0x34, (uint32_t)(RS32(o + 0x34) + 0x3000));
        if (RS32(o + 0x34) > 0x50000) W32(o + 0x34, 0x50000u);
        uint16_t t = R16(o + 0x14);
        W16(o + 0x14, t + 1);
        if (t > 0x60) { W16(o, 0xFFFF); return; }
    }
    W16(SPR + 0x0A, (uint16_t)COLOR_BANK(SLOT));
    W16(SPR + 2, (uint32_t)((RS32(o + 8) - RS32(CAM_X)) >> 16));
    W16(SPR + 4, (uint32_t)((RS32(o + 0xC) - RS32(CAM_Y)) >> 16));
    W16(SPR + 0x10, 0x600);
    W16(SPR + 0x14, (uint16_t)((R16(o + 0x14) & 0xFF) << 3));
    W16(SPR + 0x0C, 0);
    W32(SPR + 0x18, 0x7F);
    W16(SPR + 0x0E, R16(o + 0x14) < 0x40 ? 0 : (uint16_t)((R16(o + 0x14) - 0x40) >> 3));
    W16(o + 0x5A, (uint16_t)((R16(o + 0x12) & 0xC) >> 2));
    if (!(R16(0x060143EAu) & 1)) Anim_Tick(o + 0x58);
}

void EnemyHandler_0018(void)                                /* 0x060A00EA */
{
    uint32_t o = O;
    if ((R16(o + 0x12) & 3) == 1) { Hannya_Flame(o); return; }
    if ((R16(o + 0x12) & 3) == 2) { Hannya_Piece(o); return; }
    if (R16(o + 2) & 1) Hannya_Init();
    Hud_DrawBossBar(RS16(0x060C6172u + 2u * (uint32_t)RS16(0x060252C4u)), RS16(o + 0x16));
    if (FROZEN) goto draw;
    if (!(R16(PL + 2) & 8) && !(R16(o + 2) & 8) && (R16(o + 6) != 4 || (RS16(M_STEP) != 1 && RS16(M_STEP) != 5)))
        Enemy_AttackPlayer(1, 0, 0);
    if (!(R16(o + 2) & 8) && R16(o + 6) != 6) Enemy_CheckPlayerHits(0x8000);
    if ((R16(o + 2) & 8) && R16(o + 6) != 7) {                      /* hurt: white flash */
        if (!(R16(0x060143EAu) & 1)) {
            uint32_t c = (uint32_t)R16(0x060A9084u + 2u * (uint32_t)SLOT) * 0x20u + SHADOW_CRAM;
            for (int k = 0; k < 0x100; k++) W16(c + 2u * (uint32_t)k, 0x7FFF);
        } else Pal_Copy(pal_dst(), 0x100, 0x060E8878u);
        W16(M_HIT, 1);
        int16_t f = RS16(M_FLASH);
        W16(M_FLASH, (uint16_t)(f + 1));
        if (f > 0x20) {
            AND16(o + 2, 0xFFF7);
            Pal_Copy(pal_dst(), 0x100, 0x060E8878u);
            W16(M_FLASH, 0);
            if (RS16(o + 0x16) < 1) {                               /* dead */
                W16(M_ANGLE, 0);
                OR16(o + 2, 8);
                W16(o + 6, 7);
                Enemy_AnimSwitch();
                Enemy_RegisterChars(0x18, 0x060E8684u);             /* 0x060A0040: break-up graphics */
                Res_Request(1, 1, 0, 0xFFFFFFFFu, R32(0x060A9070u + 4u * (uint32_t)SLOT) + 0x25C04740u, 0x060C6224u);
                Hannya_StopShots();
                OR16(PL + 4, 0x400);
            }
        }
    }
    switch (RS16(o + 6)) {
    case 0:                                                         /* intro */
        switch (RS16(M_STEP)) {
        case 0: W16(o + 0x14, R16(o + 0x14) + 1); if (R16(o + 0x14) > 0x40) { W16(M_STEP, 1); W16(o + 0x14, 0); } break;
        case 1:
            W16(o + 0x14, R16(o + 0x14) + 1);
            if ((R16(o + 0x14) & 0xF) == 0) W16(M_FRAME, R16(M_FRAME) - 1);
            if (RS16(M_FRAME) == 0) { W16(M_STEP, 2); W16(o + 0x14, 0); }
            break;
        case 2: W16(o + 0x14, R16(o + 0x14) + 1); if (R16(o + 0x14) > 0x30) { W16(M_STEP, 3); W16(o + 0x14, 0); } break;
        case 3: {
            Pal_StepTowards(pal_dst(), 0x100, 0x060E8878u, 1);
            uint16_t t = R16(o + 0x14);
            W16(o + 0x14, t + 1);
            if (t > 0x20) { W16(o + 0x14, 0); W16(M_STEP, 1); W16(o + 6, 1); Hannya_Wave(); AND16(PL + 4, 0xFBFF); }
            break;
        }
        }
        break;
    case 1:                                                         /* drift / pick */
        if (RS16(M_STEP) == 0) {
            if (R16(M_NEXT) == 0) {
                uint32_t r = (uint32_t)sub_0602DDD2();
                if (RS16(0x060C6178u + 2u * ((r >> 8) & 0xF)) == 0 && RS16(M_HIT) == 0) W16(M_NEXT, 1);
                else { r = (uint32_t)sub_0602DDD2(); W16(M_NEXT, R16(0x060C6198u + 2u * ((r >> 8) & 0xF))); W16(M_HIT, 0); }
            }
            if ((R16(M_NEXT) & 7) == 1) {
                if (Hannya_Wave() == 0) { W16(M_STEP, 1); goto drift; }
                if (R16(o + 0x5A) == 0) W16(o + 6, 5);
            } else if (R16(o + 0x5A) == 0) {
                int32_t d = RS32(PL + 8) - RS32(o + 8);
                if (RS16(M_NEXT) < 1 || ((d >= 0 || (R16(o + 2) & 0x8000)) && (d < 1 || !(R16(o + 2) & 0x8000)))) {
                    W16(M_STEP, 0);
                    W16(o + 0x14, 0);
                    uint16_t m = R16(M_NEXT) & 7;
                    if (m == 2) W16(o + 6, 2);
                    else if (m == 3) { W16(o + 6, 4); W16(o + 0x48, 0); W16(o + 0x4A, 0); }
                    else if (m == 4) W16(o + 6, 6);
                } else { W16(M_NEXT, R16(M_NEXT) | 0x8000); W16(o + 6, 5); }   /* turn to face Sho first */
            }
            break;
        }
    drift:
        W32(M_WPH, (uint32_t)(RS32(M_WPH) + RS32(M_WSPD)));
        if (EM_Abs(RS32(M_WPH)) > 0x00800000) { W16(M_STEP, 0); W16(M_NEXT, 0); }
        W32(o + 8, (uint32_t)(RS32(o + 8) + ((R16(o + 2) & 0x8000) ? -0x20000 : 0x20000)));
        W32(o + 0xC, (uint32_t)(RS32(M_HOMEY) - FixMul(0x180000, Sin8((R32(M_WPH) >> 16) & 0xFF))));
        break;
    case 2:                                                         /* tilt and spit */
        if (RS16(M_STEP) == 0) {
            if (R16(o + 0x58) & 1) {
                W16(M_ANGLE, (uint16_t)(RS16(M_ANGLE) + ((R16(o + 2) & 0x8000) ? -1 : 1)));
                if (EM_Abs(RS16(M_ANGLE)) > 0x2F) W16(M_STEP, 1);
            }
        } else if (RS16(M_STEP) == 1) {
            W16(M_ANGLE, (uint16_t)(RS16(M_ANGLE) + ((R16(o + 2) & 0x8000) ? 8 : -8)));
            if (R16(M_ANGLE) == 0) { W16(o + 0x5A, R16(o + 0x5A) + 1); Hannya_Spit(); }
            if (EM_Abs(RS16(M_ANGLE)) > 0x12F) W16(M_STEP, 2);
        } else if (RS16(M_STEP) == 2) {
            uint16_t t = R16(o + 0x14);
            W16(o + 0x14, t + 1);
            if (t > 0x10) { W16(M_STEP, 0); W16(o + 0x14, 0); W16(o + 6, 3); W16(M_ANGLE, 0); }
        }
        break;
    case 3:
        W16(M_ANGLE, 0);
        if (R16(o + 0x58) & 0x8000) { W16(M_STEP, 0); W16(o + 6, 1); W16(M_NEXT, 1); }
        break;
    case 4: {                                                       /* charge */
        int16_t st = RS16(M_STEP);
        if (st == 0) { if (R16(o + 0x58) & 1) W16(M_STEP, 1); }
        else if (st == 1) {
            W16(o + 0x48, (uint16_t)(RS16(o + 0x48) + ((R16(o + 2) & 0x8000) ? 0x40 : -0x40)));
            W16(o + 0x4A, (uint16_t)(RS16(o + 0x4A) + RS16(o + 0x48)));
            W16(M_ANGLE, (uint16_t)(RS16(o + 0x4A) >> 8));
            if (EM_Abs(RS16(o + 0x48)) > 0xFFF) { Hannya_Aim(0); W16(M_STEP, 2); W16(o + 0x4C, 0); W16(o + 0x14, 0); }
        } else if (st == 2) {
            W16(o + 0x4A, (uint16_t)(RS16(o + 0x4A) + RS16(o + 0x48)));
            W16(M_ANGLE, (uint16_t)(RS16(o + 0x4A) >> 8));
            uint16_t t = R16(o + 0x14);
            W16(o + 0x14, t + 1);
            if (t > 0x20) { W16(M_STEP, 3); W16(o + 0x14, 0); }
        } else if (st == 3) {
            W16(o + 0x14, R16(o + 0x14) + 1);
            W16(o + 0x4A, (uint16_t)(RS16(o + 0x4A) + RS16(o + 0x48)));
            W16(M_ANGLE, (uint16_t)(RS16(o + 0x4A) >> 8));
            W32(o + 8, (uint32_t)(RS32(o + 8) + RS32(o + 0x30)));
            W32(o + 0xC, (uint32_t)(RS32(o + 0xC) - RS32(o + 0x34)));
            int16_t w = (int16_t)Hannya_Arena();
            if (w != 0) {
                sub_06007CDE(0x114);
                if (R16(o + 0x14) > 0x3C) { Hannya_Aim(1); W16(o + 0x4C, 1); w = 0; }
                if (w == 1) Hannya_Aim(0);
                if (w == 2) Hannya_Aim(2);
            }
            if (R16(o + 0x4C) != 0 && EM_Abs(RS32(o + 0xC) - RS32(M_HOMEY)) < 0x90000) {
                W16(M_STEP, 4); W16(o + 0x4C, 0); W16(o + 0x14, 0);
            }
        } else if (st == 4) {
            W16(o + 0x4A, (uint16_t)(RS16(o + 0x4A) + RS16(o + 0x48)));
            W16(M_ANGLE, (uint16_t)(RS16(o + 0x4A) >> 8));
            if (EM_Abs(RS16(o + 0x4A)) < 0x1001) W16(M_STEP, 5);
        } else if (st == 5) {
            int16_t y = (int16_t)(R32(o + 0xC) >> 16), hy = (int16_t)(R32(M_HOMEY) >> 16);
            if (y < hy) W32(o + 0xC, R32(o + 0xC) + 0x10000);
            if (hy < y) W32(o + 0xC, R32(o + 0xC) - 0x10000);
            if (EM_Abs(RS16(o + 0x48)) > 0x1FF)
                W16(o + 0x48, (uint16_t)(RS16(o + 0x48) + ((R16(o + 2) & 0x8000) ? -0x40 : 0x40)));
            W16(o + 0x4A, (uint16_t)(RS16(o + 0x4A) + RS16(o + 0x48)));
            W16(M_ANGLE, (uint16_t)(RS16(o + 0x4A) >> 8));
            if (EM_Abs(RS16(o + 0x4A)) < 0x201) { W16(M_STEP, 6); W16(o + 0x5A, R16(o + 0x5A) + 1); W16(M_ANGLE, 0); }
        } else if (st == 6 && (R16(o + 0x58) & 0x8000)) {
            W16(M_ANGLE, 0); W16(M_STEP, 0); W16(o + 6, 1); W16(M_NEXT, 1);
            W16(o + 0x48, 0); W16(o + 0x4A, 0); W16(o + 0x4C, 0); W16(o + 0x14, 0);
        }
        break;
    }
    case 5:                                                         /* turn round */
        if (R16(o + 0x58) & 0x8000) { W16(o + 2, R16(o + 2) ^ 0x8000); W16(M_STEP, 0); W16(o + 6, 1); }
        break;
    case 6: {                                                       /* rain of fire */
        int16_t st = RS16(M_STEP);
        if (st == 0) {
            Pal_StepTowards(pal_dst(), 0x100, 0x060E8C78u, 1);
            uint16_t t = R16(o + 0x14);
            W16(o + 0x14, t + 1);
            if (t > 0x60) { W16(M_STEP, 1); W16(o + 0x48, 0); }
            if (R16(o + 0x14) == 1) sub_06007CDE(0x21E);
        } else if (st == 1) {
            int16_t k = (int16_t)EM_Sdiv((int16_t)((uint32_t)(RS32(o + 8) - 0x04080000) >> 16), 75);
            W16(o + 0x4E, (uint16_t)(k * 75 - (int16_t)((uint32_t)(RS32(o + 8) - 0x04000000) >> 16) + 45));
            if (k < 0) k = 0;
            if (k > 5) k = 5;
            W16(o + 0x48, (uint16_t)k);                             /* the safe column */
            W16(o + 0x4A, (uint16_t)((((uint32_t)sub_0602DDD2() >> 8) & 7) * 6));
            W16(M_STEP, 2); W16(o + 0x14, 0); W16(o + 0x4C, 0);
        } else if (st == 2) {
            if (R16(o + 0x14) == 0) {
                int16_t col, xo, base;
                do {
                    if (RS16(o + 0x4C) > 5) { W16(M_STEP, 3); W16(o + 0x48, 0); W16(o + 0x4A, 0); W16(o + 0x4C, 0); goto done; }
                    col = RS16(0x060C61B8u + 2u * (uint32_t)RS16(o + 0x4A));
                    xo = RS16(0x060C6218u + 2u * (uint32_t)col);
                    base = RS16(o + 0x4E);
                    W16(o + 0x4A, R16(o + 0x4A) + 1);
                    W16(o + 0x4C, R16(o + 0x4C) + 1);
                } while (RS16(o + 0x48) == col);
                int s = Projectile_Spawn(0x10, xo * 0x10000 + 0x04000000 - base * 0x10000, 0x05200000, 0, 0);
                if ((int16_t)s != -1) W16(PROJ(s) + 0x12, 1);
                W16(o + 0x14, 6);
            }
            W16(o + 0x14, R16(o + 0x14) - 1);
        } else if (st == 3) {
            uint16_t t = R16(o + 0x14);
            W16(o + 0x14, t + 1);
            if (t > 0x70) { W16(M_STEP, 4); W16(o + 0x14, 0); }
        } else if (st == 4) {
            Pal_StepTowards(pal_dst(), 0x100, 0x060E8878u, 1);
            uint16_t t = R16(o + 0x14);
            W16(o + 0x14, t + 1);
            if (t > 0x20) { W16(M_STEP, 5); W16(o + 0x48, 0); }
        } else if (st == 5) { W16(M_STEP, 0); W16(o + 6, 1); W16(M_NEXT, 1); W16(o + 0x14, 0); }
        break;
    }
    case 7:                                                         /* dying */
        if (!(R16(0x060143EAu) & 1)) Pal_StepTowards(pal_dst(), 0x100, 0x060E8A78u, 1);
        if (R16(o + 0x58) & 1) { AND16(o + 0x58, 0xFFFE); Hannya_Flames(); }
        if (R16(o + 0x58) & 2) { Hannya_Fragment(); Hannya_Fragment(); }
        if (R16(o + 0x58) & 0x8000) { Hannya_Dead(); return; }
        break;
    }
done:
    Enemy_AnimSwitch();
draw:
    if (RS16(M_FRAME) < 4) {
        W16(SPR + 0x14, R16(M_ANGLE) & 0xFF);
        W16(SPR + 0x0E, R16(M_FRAME));
        W16(SPR + 2, (uint32_t)((RS32(o + 8) - RS32(CAM_X)) >> 16));
        W16(SPR + 4, (uint32_t)((RS32(o + 0xC) - RS32(CAM_Y)) >> 16));
        W16(SPR + 0x10, (R16(o + 2) & 0x8000) | 0x400);
        W16(SPR + 0x0A, (uint16_t)COLOR_BANK(SLOT));
        W16(SPR + 0x0C, 0);
        W32(SPR + 0x18, 0);
        Anim_Tick(o + 0x58);
    }
}

/* Projectile 0x10 (0x060A152C): fire pillar (anim 9, bank +0xF); hurts while its anim
 * flag 4 is set; freed at the anim end or when +0x12 is cleared (boss dead). */
void Proj_FirePillar10(uint32_t e)
{
    int16_t s = (int16_t)Enemy_FindSlot(0x18);
    if (R16(e + 4) & 1) {
        Anim_Start(e + 0x20, B18_DEFS, B18_SCR, CHAR_BASE(s), 9);
        AND16(e + 4, 0xFFFE);
    }
    if (!FROZEN) {
        if (R16(e + 0x20) & 4) Projectile_Collide(1, 0xC100);
        if ((R16(e + 0x20) & 0x8000) || RS16(e + 0x12) < 1) { W16(e, 0xFFFF); return; }
    }
    W16(SPR + 0x0A, (uint16_t)(COLOR_BANK(s) + 0xF));
    W16(SPR + 2, (uint32_t)((RS32(e + 8) - RS32(CAM_X)) >> 16));
    W16(SPR + 4, (uint32_t)((RS32(e + 0xC) - RS32(CAM_Y)) >> 16));
    W16(SPR + 0x10, 0xA00);
    W32(SPR + 0x20, (R16(e + 0x20) & 4) ? 0x14000u : 0x10000u);
    W16(SPR + 0x0C, 0);
    W32(SPR + 0x18, (R16(0x060143EAu) & 1) ? 0x80u : 0x7Fu);
    W16(SPR + 0x0E, R16(e + 0x20) & 3);
    Anim_Tick(e + 0x20);
}

/* Projectile 0x11 (0x060A1354): spinning fireball (anim 8); bursts (projectile 5) on
 * hitting Sho / his guard, or when the boss dies. */
void Proj_Fireball11(uint32_t e)
{
    int16_t s = (int16_t)Enemy_FindSlot(0x18);
    if (R16(e + 4) & 1) {
        Anim_Start(e + 0x20, B18_DEFS, B18_SCR, CHAR_BASE(s), 8);
        AND16(e + 4, 0xFFFE);
        W16(e + 6, 0);
    }
    if (!FROZEN) {
        W16(e + 0x14, (uint16_t)(RS16(e + 0x14) + ((R16(e + 4) & 0x8000) ? 0xC : -0xC)));
        W32(e + 8, (uint32_t)(RS32(e + 8) + RS32(e + 0x18)));
        W32(e + 0xC, (uint32_t)(RS32(e + 0xC) - RS32(e + 0x1C)));
        int32_t dx = RS32(e + 8) - RS32(CAM_X), dy = RS32(e + 0xC) - RS32(CAM_Y);
        if (dx < -0x00800000 || dx > 0x01C00000 || dy < -0x00800000 || dy > 0x01C00000) { W16(e, 0xFFFF); return; }
        if ((int16_t)Projectile_Collide(1, 0x8100) != 0 || RS16(e + 0x12) < 1) {
            W16(e, 0xFFFF);
            Projectile_Spawn(5, RS32(e + 8), RS32(e + 0xC), 0, 0);
            return;
        }
    }
    W16(SPR + 0x0A, (uint16_t)COLOR_BANK(s));
    W16(SPR + 0x14, R16(e + 0x14));
    W16(SPR + 0x0E, 0);
    W16(SPR + 2, (uint32_t)((RS32(e + 8) - RS32(CAM_X)) >> 16));
    W16(SPR + 4, (uint32_t)((RS32(e + 0xC) - RS32(CAM_Y)) >> 16));
    W16(SPR + 0x10, (R16(e + 4) & 0x8000) | 0x600);
    W16(SPR + 0x0C, 0);
    W32(SPR + 0x18, 0x7F);
    Anim_Tick(e + 0x20);
}
