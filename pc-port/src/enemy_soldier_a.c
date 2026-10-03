/* Soldier (type 0x0001) subtypes 2 and 3/11 (see enemy_soldier_sub.c), ported from SHINOBI.PPB.
 *  2    → 0x0608FE38  appears (action 0x11) when Sho comes within 200 px of its spot, shoots
 *                     3 times, vanishes (0x12) and comes back 0x2D0 frames later; killed, it
 *                     also respawns at its origin (+0x50/+0x52) after the same delay.
 *  3/11 → 0x060904C0  walks in from 80 px aside (+0x1E = target x), shoots 3 times, walks
 *                     back out (action 7/8) and waits 0xF0 frames before coming back.
 * Extra fields: +0x1E target x (px), +0x4C/+0x54/+0x56 death-overlay anim state,
 * +0x4E shots left, +0x50/+0x52 origin (px). */
#include "enemy_int.h"
#include "enemy_soldier_sub.h"
#include "anim.h"
#include "player.h"      /* Obj_Integrate  0x060452A4 */
#include "collision.h"   /* Obj_CollideMap 0x06045340 */

static int32_t iabs32(int32_t v) { return v < 0 ? -v : v; }

/* far = 1 when the enemy is 160 + 96 px or more away from the camera origin on either axis
 * (same test as EnemyHandler_0001 0x0608FB9C). */
static int far_from_camera(uint32_t o)
{
    int32_t d = RS32(o + 8) - RS32(CAM_X) + (int32_t)0xFF600000;
    if (d < 0) d = RS32(CAM_X) - RS32(o + 8) + 0x00A00000;
    if (d >= 0x01000000) return 1;
    d = RS32(o + 0xC) - RS32(CAM_Y) + (int32_t)0xFF600000;
    if (d < 0) d = RS32(CAM_Y) - RS32(o + 0xC) + 0x00A00000;
    return d >= 0x01000000;
}

/* 0x06092DA4: hide the sprite (anim flag 0x4000 "no auto-draw") while waiting (0x13) or
 * on the first frame of appearing (0x11). Not ported elsewhere: local copy. */
static void Soldier_SetHidden(void)
{
    uint32_t o = O;
    if (RS16(o + 6) == 0x13) { OR16(o + 0x58, 0x4000); return; }
    if (RS16(o + 6) == 0x11 && R16(o + 0x5A) == 0) { OR16(o + 0x58, 0x4000); return; }
    AND16(o + 0x58, 0xBFFF);
}

/* back to the origin, waiting (0x13) for 0x060C08A0 (= 0x2D0) frames */
static void sub02_reset(uint32_t o)
{
    W32(o + 8, (uint32_t)(RS16(o + 0x50) * 0x10000));
    W32(o + 0xC, (uint32_t)(RS16(o + 0x52) * 0x10000));
    W16(o + 0x16, 1);
    W16(o + 6, 0x13);
    W16(o + 4, 0);
    W16(o + 0x14, R16(0x060C08A0u));
}

static void Soldier_Sub02Init(void)                          /* 0x060903E0 */
{
    uint32_t o = O;
    W16(o + 6, STAGE == 9 ? 0x11 : 0x13);
    Anim_Start(o + 0x58, 0x060BFFB0u, 0x060E43D4u, CHAR_BASE(SLOT), RS16(o + 6));
    AND16(o + 2, 0xFFFE);
    W16(o + 0x16, 1);
    W16(o + 4, 1);
    W16(o + 0x14, 0);
    W16(o + 0x50, R32(o + 8) >> 16);
    W16(o + 0x52, R32(o + 0xC) >> 16);
    if (RS32(o + 8) < RS32(0x060EA2BCu)) AND16(o + 2, 0x7FFF);
    else OR16(o + 2, 0x8000);
}

void Soldier_Sub02(void)                                     /* 0x0608FE38 */
{
    uint32_t o = O;
    if (R16(o + 2) & 1) Soldier_Sub02Init();
    if (far_from_camera(o)) {
        if (!(R16(o + 2) & 0x4008)) return;                  /* off screen: asleep */
        if (RS16(o + 6) == 0x13) return;
    }
    if (!FROZEN) {
        int r = Soldier_CheckHits();
        int16_t a = RS16(o + 6);
        if ((R16(o + 2) & 8) && a != 0x13 && a != 0x11 && a != 0x12 && a != 0x0E && a != 0x0F && a != 0x0D)
            Soldier_OnHit(r, o + 0x4C, o + 0x54, o + 0x56);
        if (R16(o + 0x58) & 0x8000) {                        /* animation finished */
            a = RS16(o + 6);
            if (a == 0x0E) {                                 /* knocked away */
                if (STAGE == 9) {
                    if (Soldier_AnimDone(RS16(o + 0x4A))) return;
                } else {
                    Score_Add(100);
                    Enemy_DeathBurst(o);
                    sub02_reset(o);
                    W16(o + 0x18, 0xFFFF);                   /* no item drop next time */
                }
            } else if (a == 0x11) {                          /* appeared */
                W16(o + 6, (R16(o + 2) & 0x0800) ? 2 : 0);
                AND16(o + 2, 0xFFF7);
                W16(o + 0x14, STAGE == 9 ? 60 : 20);
            } else if (a == 0x12) {                          /* vanished */
                sub02_reset(o);
            } else {
                if (Soldier_AnimDone(RS16(o + 0x4A))) return;
            }
        } else {
            a = RS16(o + 6);
            if (a == 0 || a == 2) {
                if (STAGE == 9 || RS16(o + 0x4E) != 0) Soldier_Think(o + 0x4A);
                else W16(o + 6, 0x12);                       /* out of shots: vanish */
            } else if (a == 9 || a == 0x0A || a == 0x0B) {
                if (R16(o + 0x58) & 3) W16(o + 0x4E, R16(o + 0x4E) - 1);
                Soldier_Think(o + 0x4A);
            } else if (a == 0x13) {                          /* waiting */
                if (!(R16(o + 4) & 1)) {
                    int16_t t = RS16(o + 0x14);
                    W16(o + 0x14, (uint32_t)(t - 1));
                    if (t > 0) return;
                    W16(o + 0x14, 0);
                    W16(o + 4, 1);
                    return;
                }
                if (iabs32(RS32(o + 8) - RS32(PL + 8)) >= 0x00C80000) return;
                W16(o + 6, 0x11);                            /* Sho within 200 px: appear */
                W16(o + 0x4E, 3);
                if (RS32(o + 8) < RS32(PL + 8)) AND16(o + 2, 0x7FFF);
                else OR16(o + 2, 0x8000);
            } else {
                Soldier_Think(o + 0x4A);
            }
        }
        a = RS16(o + 6);
        if (a != 0x11 && a != 0x13) Soldier_Physics();
        else if (RS16(o + 6) == 0x11) { Obj_Integrate(o); Obj_CollideMap(o, 2, 8); }
        Enemy_AnimSwitch();
    }
    if (STAGE == 9) {
        if (Soldier_Dying(o + 0x4C, o + 0x54, o + 0x56)) return;
    } else if ((R16(o + 0x58) & 3) == 1 && RS16(o + 6) == 0x0F) {   /* cut down: explosion overlay */
        Anim_Start(SCRATCH_ANIM, 0x060BFFB0u, 0x060E43D4u, CHAR_BASE(SLOT), 0x10);
        Enemy_DrawOverlay(SCRATCH_ANIM, (int16_t)(COLOR_BANK(SLOT) + 1), o + 0x4C, o + 0x54, o + 0x56);
        if (R16(SCRATCH_ANIM) & 0x8000) {
            Score_Add(100);
            Enemy_DeathBurst(o);
            sub02_reset(o);
            W16(o + 0x18, 0xFFFF);
            Enemy_AnimSwitch();
        }
    }
    Soldier_SetHidden();
    Soldier_Draw(R16(o + 0x56));
}

/* ------------------------------------------------------------ subtype 3 / 11 */

static void Soldier_Sub03Init(void)                          /* 0x06090CA4 */
{
    uint32_t o = O;
    if (STAGE != 9) W16(o + 6, 0x13);
    else {
        W16(o + 6, 7);
        int32_t x = RS32(o + 8) + (((R16(o + 0x12) & 0x1F) == 3) ? (int32_t)0xFFB00000 : 0x00500000);
        AND16(o + 4, 0xFFFE);
        OR16(o + 4, 2);
        W16(o + 0x1E, (uint32_t)x >> 16);
    }
    Anim_Start(o + 0x58, 0x060BFFB0u, 0x060E43D4u, CHAR_BASE(SLOT), RS16(o + 6));
    AND16(o + 2, 0xFFFE);
    W16(o + 0x16, 1);
    W16(o + 4, 1);
    W16(o + 0x14, 0);
    W16(o + 0x50, R32(o + 8) >> 16);
}

void Soldier_Sub03(void)                                     /* 0x060904C0 */
{
    uint32_t o = O;
    if (R16(o + 2) & 1) Soldier_Sub03Init();
    if (far_from_camera(o)) {
        if (!(R16(o + 2) & 0x4008)) return;
        if (RS16(o + 6) == 0x13) return;
    }
    if (!FROZEN) {
        int r = Soldier_CheckHits();
        W32(o + 0x30, 0);
        int16_t a = RS16(o + 6);
        if ((R16(o + 2) & 8) && a != 0x13 && a != 0x0E && a != 0x0F && a != 7 && a != 8 && a != 0x0D)
            Soldier_OnHit(r, o + 0x4C, o + 0x54, o + 0x56);
        int32_t v = -3;                                      /* r4 left over (used by action 8, anim event 0) */
        if (R16(o + 0x58) & 0x8000) {                        /* animation finished */
            a = RS16(o + 6);
            if (a == 6) {                                    /* landed */
                if ((R16(o + 4) & 4) && STAGE != 9) { AND16(o + 4, 0xFFFB); W16(o + 6, 7); }
                else Soldier_AnimDone(RS16(o + 0x4A));
            } else if (a == 8) {                             /* reached the target x */
                AND16(o + 2, 0xFFF7);
                W32(o + 8, (uint32_t)(RS16(o + 0x1E) * 0x10000));
                W32(o + 0x30, 0);
                if ((R16(o + 4) & 2) || STAGE == 9) {        /* walked in: start shooting */
                    W16(o + 6, 0);
                    W16(o + 0x14, 10);
                    AND16(o + 4, 0xFFFD);
                    W16(o + 0x52, R32(o + 0xC) >> 16);
                } else {                                     /* walked out: wait */
                    OR16(o + 2, 8);
                    W16(o + 6, 0x13);
                    W16(o + 0x14, R16(0x060C08C0u));
                }
            } else {
                if (Soldier_AnimDone(RS16(o + 0x4A))) return;
            }
        } else {
            a = RS16(o + 6);
            if (a == 0 || a == 2) {
                if (STAGE == 9 || RS16(o + 0x4E) != 0) Soldier_Think(o + 0x4A);
                else {                                       /* out of shots: walk back to the origin */
                    W16(o + 6, 7);
                    AND16(o + 4, 0xFFFD);
                    W16(o + 0x1E, R16(o + 0x50));
                    if ((int16_t)(R32(o + 0xC) >> 16) != RS16(o + 0x52)) {
                        OR16(o + 4, 0x0C);
                        W16(o + 6, 5);
                    }
                }
            } else if (a == 7) {                             /* walk towards +0x1E */
                if ((int16_t)(R32(o + 8) >> 16) < RS16(o + 0x1E)) AND16(o + 2, 0x7FFF);
                else OR16(o + 2, 0x8000);
                if (R32(o + 0x34) != 0) { W16(o + 6, 5); OR16(o + 4, 4); }
                else if (R16(o + 0x10) & 8) {                /* wall: hop */
                    AND16(o + 2, 0x7FFF); W16(o + 6, 0x1A); W16(o + 0x14, 12); OR16(o + 4, 4);
                } else if (R16(o + 0x10) & 4) {
                    OR16(o + 2, 0x8000); W16(o + 6, 0x1A); W16(o + 0x14, 12); OR16(o + 4, 4);
                } else {
                    int32_t s = RS32((R16(o + 0x58) & 3) ? 0x060C08A8u : 0x060C08A4u);
                    W32(o + 0x30, (uint32_t)((R16(o + 2) & 0x8000) ? -s : s));
                    int32_t px = (int16_t)(R32(o + 8) >> 16), d = px - RS16(o + 0x1E);
                    if (d < 0) d = RS16(o + 0x1E) - px;
                    if (d < RS16(0x060C08ACu)) W16(o + 6, 8);
                }
            } else if (a == 8) {                             /* braking */
                uint16_t k = R16(o + 0x58) & 3;
                if (k == 1) v = RS32(0x060C08B4u);
                else if (k == 2) v = RS32(0x060C08B8u);
                else if (k == 3) v = RS32(0x060C08BCu);
                W32(o + 0x30, (uint32_t)((R16(o + 2) & 0x8000) ? -v : v));
                int32_t tx = RS16(o + 0x1E) * 0x10000;
                int32_t d = RS32(o + 8) - tx;
                if (d < 0) d = tx - RS32(o + 8);
                if (d < iabs32(RS32(o + 0x30))) W32(o + 8, (uint32_t)(RS16(o + 0x1E) * 0x10000));
            } else if (a == 9 || a == 0x0A || a == 0x0B) {
                if (R16(o + 0x58) & 3) W16(o + 0x4E, R16(o + 0x4E) - 1);
                Soldier_Think(o + 0x4A);
            } else if (a == 0x13) {                          /* waiting */
                if (!(R16(o + 4) & 1)) {
                    uint16_t t = R16(o + 0x14);
                    W16(o + 0x14, t - 1u);
                    if (t != 0) return;
                    W16(o + 0x14, 0);
                    OR16(o + 4, 1);
                    return;
                }
                int32_t x = RS32(o + 8) + (((R16(o + 0x12) & 0x1F) == 3) ? (int32_t)0xFFB00000 : 0x00500000);
                if (iabs32(x - RS32(0x060EA2BCu)) >= 0x00780000) return;
                W16(o + 6, 7);                               /* Sho within 120 px of the target: walk in */
                AND16(o + 4, 0xFFFE);
                OR16(o + 4, 2);
                W16(o + 0x4E, 3);
                W16(o + 0x1E, (uint32_t)x >> 16);
                AND16(o + 2, 0xFFF7);
            } else if (a == 0x1A) {                          /* hop over a wall */
                if ((R16(o + 0x58) & 3) == 1) {
                    W16(o + 0x14, R16(o + 0x14) - 1u);
                    if (R16(o + 0x14) == 0 || (int16_t)(R32(O + 8) >> 16) != (int16_t)(R32(O + 0x28) >> 16)) {
                        AND16(o + 0x58, 0xFFFC);
                        OR16(o + 0x58, 2);
                    } else W32(o + 0x34, 0x00060000u);
                }
                if (R16(o + 2) & 0x4000) {
                    int32_t s = RS32(0x060C08B0u);
                    W32(o + 0x30, (uint32_t)((R16(o + 2) & 0x8000) ? -s : s));
                } else if ((R16(o + 0x58) & 3) == 2) W16(o + 6, 6);
            } else {
                Soldier_Think(o + 0x4A);
            }
        }
        if (RS16(o + 6) != 0x13) {
            a = RS16(o + 6);
            if (a == 8 || a == 7) OR16(o + 2, 0x0100);
            else AND16(o + 2, 0xFEFF);
            Obj_Integrate(o);
            a = RS16(o + 6);
            if (a != 8 && a != 7) {
                if (!(R16(o + 4) & 8)) Obj_CollideMap(o, 2, 8);
                else {                                       /* dropping back down to +0x52 */
                    int32_t py = (int16_t)(R32(o + 0xC) >> 16), d = py - RS16(o + 0x52);
                    if (d < 0) d = RS16(o + 0x52) - py;
                    if (d < 10) { AND16(o + 4, 0xFFF7); Obj_CollideMap(o, 2, 8); }
                }
            }
            a = RS16(o + 6);
            if ((a == 4 || a == 5) && (R16(o + 2) & 0x10)) W16(o + 6, 6);
        }
        Enemy_AnimSwitch();
    }
    if (Soldier_Dying(o + 0x4C, o + 0x54, o + 0x56)) return;
    Soldier_Draw(R16(o + 0x56));
}
