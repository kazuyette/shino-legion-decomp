/* Soldier (type 0x0001) subtypes 5, 6 and 9, ported from SHINOBI.PPB.
 *  5 (0x0608E3E6, init 0x0608E36C): soldier hiding under the water/floor line (world
 *    bottom 0x060EA624 + 100 px, action 0x13) until Sho comes within 250 px; leaps out
 *    (action 0x19, vy 0xD6000, splash effect 0xE via 0x0605B04C) for 12 rising frames,
 *    then throws in the air (0xB). Killed -> 100 points and back into hiding at home
 *    (+0x50/+0x52 px, no item any more); re-armed (+4 = 1) once Sho is 320 px away.
 *  6 (0x0608E8C4, init 0x0608F3F0): 2-HP rifleman with its own decision code (zone
 *    test inlined): shoots standing (0x14, muzzle 34,-47) / crouched (0x15, 34,-19) or
 *    jumps (0x16, vy 0x80000) and throws while falling (0x17, aimed, 25,-43); dodges a
 *    shuriken by a random jump. Shots inherit its own X speed (not on stage 8).
 *    Palette bank +2, cut-down overlay bank +3. Killed -> 200 points.
 *  9 (0x060921CC, init 0x06092768): soldier on a lower floor that can jump up to Sho's
 *    level (action 4, two hops of 6 and 11 px/frame, +0x4E counter) and throw in the
 *    air; when it lands where it took off it drops back through the floors (+4 bit 0 =
 *    no map collision until within 200 px of home +0x52). Off screen it returns home.
 * The common soldier code is in enemy.c (Soldier_*). */
#include "enemy_int.h"
#include "enemy_soldier_sub.h"
#include "anim.h"
#include "collision.h"
#include "player_ext.h"
#include "fx.h"

void Obj_Integrate(uint32_t o);      /* physics.c, 0x060452A4 */

static int32_t iabs(int32_t v) { return v < 0 ? -v : v; }

/* |x - camX - 160| / |y - camY - 160| (16.16), as computed by the originals */
static int32_t dist_x(uint32_t o)
{
    int32_t d = RS32(o + 8) - RS32(CAM_X) + (int32_t)0xFF600000;
    if (d < 0) d = RS32(CAM_X) - RS32(o + 8) + 0x00A00000;
    return d;
}
static int32_t dist_y(uint32_t o)
{
    int32_t d = RS32(o + 0xC) - RS32(CAM_Y) + (int32_t)0xFF600000;
    if (d < 0) d = RS32(CAM_Y) - RS32(o + 0xC) + 0x00A00000;
    return d;
}
static int far_away(uint32_t o) { return dist_x(o) >= 0x01000000 || dist_y(o) >= 0x01000000; }

/* 0x06092DA4 (local copy, also in enemy_soldier_c.c) */
static void hide_check(void)
{
    uint32_t o = O;
    if (R16(o + 6) == 0x13) OR16(o + 0x58, 0x4000);
    else if (R16(o + 6) == 0x11 && R16(o + 0x5A) == 0) OR16(o + 0x58, 0x4000);
    else AND16(o + 0x58, 0xBFFF);
}

/* ------------------------------------------------------------ subtype 5 */

static void Soldier05_Init(void)                             /* 0x0608E36C */
{
    uint32_t o = O;
    W16(o + 6, 0x13);
    Anim_Start(o + 0x58, 0x060BFFB0u, 0x060E43D4u, CHAR_BASE(SLOT), RS16(o + 6));
    W32(o + 0xC, (uint32_t)(RS32(0x060EA624u) + 0x00640000));
    AND16(o + 2, 0xFFFE);
    W16(o + 0x16, 1);
    W16(o + 4, 1);
    W16(o + 0x14, 0);
    W16(o + 0x50, R32(o + 8) >> 16);
    W16(o + 0x52, R32(o + 0xC) >> 16);
}

static void Soldier05_Home(uint32_t o)        /* killed: 100 points, back into hiding */
{
    Score_Add(100);
    Enemy_DeathBurst(o);
    W32(o + 8, (uint32_t)((int32_t)RS16(o + 0x50) << 16));
    W32(o + 0xC, (uint32_t)((int32_t)RS16(o + 0x52) << 16));
    W16(o + 0x16, 1);
    W16(o + 6, 0x13);
    W16(o + 4, 0);
    W16(o + 0x14, 0);
    W16(o + 0x18, 0xFFFF);
}

void Soldier_Sub05(void)                                     /* 0x0608E3E6 */
{
    uint32_t o = O;
    if (far_away(o) && !(R16(o + 2) & 8) && !(R16(o + 2) & 0x4000)) return;
    if (R16(o + 2) & 1) Soldier05_Init();
    if (R16(o + 6) == 0x13 && iabs(RS32(o + 8) - RS32(PL + 8)) > 0x01400000 && dist_y(o) < 0x01000000) {
        W32(o + 0xC, (uint32_t)(RS32(0x060EA624u) + 0x00640000));    /* re-armed */
        W16(o + 4, 1);
        return;
    }
    if (!FROZEN) {
        int r = Soldier_CheckHits();
        int16_t a = RS16(o + 6);
        if ((R16(o + 2) & 8) && a != 0x13 && a != 0x11 && a != 0x12 && a != 0xE && a != 0xF && a != 0x19 && a != 0xD)
            Soldier_OnHit(r, o + 0x4C, o + 0x54, o + 0x56);
        if (!(R16(o + 0x58) & 0x8000)) {
            if (RS16(o + 6) == 0x13) {
                if (!(R16(o + 4) & 1)) return;
                if (iabs(RS32(o + 8) - RS32(PL + 8)) >= 0x00FA0000) return;
                W16(o + 6, 0x19);                                     /* leap out */
                W16(o + 0x14, 0);
                Obj_Spawn(0xE, RS32(o + 8), RS32(o + 0xC) + 0x00200000, 0, 0);   /* 0x0605B04C: splash */
                AND16(o + 2, 0x7FFF);
                if (RS32(PL + 8) < RS32(o + 8)) OR16(o + 2, 0x8000);
                W32(o + 0x34, 0x000D6000u);
            } else if (RS16(o + 6) == 0x19) {
                if (R16(o + 0x58) & 1) {
                    if (R16(o + 0x14) < 0xC) { W16(o + 0x14, R16(o + 0x14) + 1); Obj_Integrate(o); }
                    else {
                        W16(o + 6, 0xB);
                        AND16(o + 0x58, 0xFFFE);
                        AND16(o + 2, 0xFFF7);
                        W32(o + 0x30, 0);
                    }
                }
            } else Soldier_Think(o + 0x4A);
        } else if (RS16(o + 6) == 0xE) {
            Soldier05_Home(o);
        } else if (Soldier_AnimDone(RS16(o + 0x4A))) return;
        if (RS16(o + 6) != 0x13 && RS16(o + 6) != 0x19) Soldier_Physics();
        Enemy_AnimSwitch();
    }
    if ((R16(o + 0x58) & 3) == 1 && RS16(o + 6) == 0xF) {
        Anim_Start(SCRATCH_ANIM, 0x060BFFB0u, 0x060E43D4u, CHAR_BASE(SLOT), 0x10);
        Enemy_DrawOverlay(SCRATCH_ANIM, (int16_t)(COLOR_BANK(SLOT) + 1), o + 0x4C, o + 0x54, o + 0x56);
        if (R16(SCRATCH_ANIM) & 0x8000) { Soldier05_Home(o); Enemy_AnimSwitch(); }
    }
    hide_check();
    Soldier_Draw(R16(o + 0x56));
}

/* ------------------------------------------------------------ subtype 6 */

static void Soldier06_Init(void)                             /* 0x0608F3F0 */
{
    uint32_t o = O;
    W16(o + 0x48, 0);
    W16(o + 6, 0);
    W16(o + 0x14, 10);
    Anim_Start(o + 0x58, 0x060BFFB0u, 0x060E43D4u, CHAR_BASE(SLOT), RS16(o + 6));
    AND16(o + 2, 0xFFFE);
    W16(o + 0x16, 2);
    W16(o + 4, 0);
    AND16(o + 2, 0x7FFF);
    if (RS32(PL + 8) < RS32(o + 8)) OR16(o + 2, 0x8000);
}

/* shot from the muzzle (dx mirrored, dy) if on screen, with the soldier's own X speed */
static void shoot06(uint32_t o, int32_t dx, int32_t dy, int16_t angle, int32_t add)
{
    int32_t x = RS32(o + 8) + dx, y = RS32(o + 0xC) + dy;
    if (RS32(CAM_X) < x && x < RS32(CAM_X) + 0x01400000 && RS32(CAM_Y) < y && y < RS32(CAM_Y) + 0x00E00000) {
        int s = Projectile_Spawn(1, x, y, angle, RS32(0x060C085Cu + 4u * (uint32_t)STAGE));
        sub_06007CDE(0x125);
        if ((int16_t)s != -1) {
            uint32_t p = PROJECTILES + (uint32_t)(int16_t)s * 0x40;
            W32(p + 0x18, (uint32_t)(RS32(p + 0x18) + add));
        }
    } else if (R16(o + 6) != 0x17) { W16(o + 0x14, 10); W16(o + 6, 0); }
}

void Soldier_Sub06(void)                                     /* 0x0608E8C4 */
{
    uint32_t o = O;
    if (far_away(o) && !(R16(o + 2) & 8) && R16(o + 6) != 0x14 && R16(o + 6) != 0x15) return;
    if (R16(o + 2) & 1) Soldier06_Init();
    int16_t zone = 0;
    uint16_t ovl = 0;
    if (!FROZEN) {
        if ((int16_t)Enemy_AttackPlayer(1, 0, 0xC000) != 0) {
            if (!(R16(o + 2) & 0x8000)) OR16(PL + 2, 0x8000); else AND16(PL + 2, 0x7FFF);
        }
        int16_t r = (int16_t)Enemy_CheckPlayerHits(0xC000);
        W32(o + 0x30, 0);
        int16_t a = RS16(o + 6);
        if ((R16(o + 2) & 8) && a != 0xD && a != 0xE && a != 0xF) {
            int16_t k = (r == 2) ? (int16_t)Player_AttackKind() : 4;
            if (RS16(o + 0x16) < 1) {
                if (k == 4 || k == 5) { W16(o + 6, 0xE); sub_06007CDE(0x205); }
                else {
                    W16(o + 6, 0xF);
                    Anim_Start(SCRATCH_ANIM, 0x060BFFB0u, 0x060E43D4u, CHAR_BASE(SLOT), 0x10);
                    W16(o + 0x52, R16(SCRATCH_ANIM + 2));
                    W16(o + 0x54, R16(SCRATCH_ANIM + 26));
                    W16(o + 0x56, R16(SCRATCH_ANIM));
                    sub_06007CDE(0x205);
                }
            } else { sub_06007CDE(0x205); W16(o + 6, 0xD); }
            Enemy_AnimSwitch();
        }
        if (!(R16(o + 0x58) & 0x8000)) {
            a = RS16(o + 6);
            if (a == 0 || a == 2) {
                AND16(o + 2, 0x7FFF);
                if (RS32(PL + 8) < RS32(o + 8)) OR16(o + 2, 0x8000);
                if (RS32(o + 0x34) != 0) W16(o + 6, 5);                  /* falling */
                else {
                    int32_t d = (R16(o + 2) & 0x8000) ? (int32_t)0xFF800000 : 0x00800000;
                    if (!Projectile_InBox(RS32(o + 8) + d, RS32(o + 0xC) + (int32_t)0xFFC00000, RS32(o + 8), RS32(o + 0xC)))
                        AND16(o + 4, 0x7FFF);
                    else if (!(R16(o + 4) & 0x8000)) {                    /* a shuriken: maybe jump */
                        OR16(o + 4, 0x8000);
                        if (!(sub_0602DDD2() & 1)) { W16(o + 6, 0x16); goto physics; }
                    }
                    int16_t t = (int16_t)(RS16(o + 0x14) - 1);
                    W16(o + 0x14, (uint16_t)t);
                    if (t == 0) {
                        W16(o + 0x14, 0xB4);
                        int32_t y = RS32(o + 0xC), sy = RS32(PL + 0xC), h = RS16(PL + 0x20) * -0x80000;
                        if (iabs(RS32(PL + 8) - RS32(o + 8)) < 0x00500000 && iabs(sy - y) <= 0x004FFFFF) zone = 1;
                        else if (sy <= y - 0x2F0000 || y - 0x2F0000 <= sy + h)
                            zone = (y - 0x130000 < sy && sy + h < y - 0x130000) ? 2 : 1;
                        if (zone == 0) {                                  /* shoot standing */
                            if (!(R16(o + 2) & 0x0800)) W16(o + 6, 0x14);
                            else { W16(o + 0x4A, 0x14); W16(o + 6, 3); }
                        } else if (zone == 1) {                           /* jump */
                            if (!(R16(o + 2) & 0x0800)) W16(o + 6, 0x16);
                            else { W16(o + 0x4A, 0x16); W16(o + 6, 3); }
                        } else {                                          /* shoot crouched */
                            if (!(R16(o + 2) & 0x0800)) { W16(o + 0x4A, 0x15); W16(o + 6, 1); }
                            else W16(o + 6, 0x15);
                        }
                    }
                }
            } else if (a == 5) {
                if (!(R16(o + 2) & 0x4000)) W16(o + 6, 6);
            } else if (a == 0x14 || a == 0x15) {
                if ((R16(o + 0x58) & 3) == 1) {
                    AND16(o + 0x58, 0xFFFC);
                    int32_t dy = (a == 0x14) ? (int32_t)0xFFD10000 : (int32_t)0xFFED0000;
                    int32_t add = RS32(o + 8) - RS32(o + 0x28);
                    if (STAGE == 8) add = 0;
                    if (!(R16(o + 2) & 0x8000)) shoot06(o, 0x00220000, dy, 0, add);
                    else shoot06(o, (int32_t)0xFFDE0000, dy, 0x80, add);
                }
            } else if (a == 0x16) {
                if ((R16(o + 0x58) & 3) == 1) {
                    AND16(o + 0x58, 0xFFFC); OR16(o + 0x58, 2);
                    W32(o + 0x34, 0x00080000u);
                } else if ((R16(o + 0x58) & 3) == 2 && RS32(o + 0x34) < 0x78001) W16(o + 6, 0x17);
            } else if (a == 0x17) {
                if ((R16(o + 0x58) & 3) == 1) {
                    AND16(o + 0x58, 0xFFFC);
                    int32_t dx = (R16(o + 2) & 0x8000) ? (int32_t)0xFFE70000 : 0x00190000;
                    int16_t ang = (int16_t)Atan8((int16_t)((uint32_t)(RS32(o + 8) + dx) >> 16),
                                                 (int16_t)((uint32_t)(RS32(o + 0xC) + (int32_t)0xFFD50000) >> 16),
                                                 (int16_t)((uint32_t)RS32(PL + 8) >> 16),
                                                 (int16_t)((uint32_t)(RS32(PL + 0xC) + (int32_t)0xFFE00000) >> 16));
                    int32_t x = RS32(o + 8) + dx, y = RS32(o + 0xC) + (int32_t)0xFFD50000;
                    if (RS32(CAM_X) < x && x < RS32(CAM_X) + 0x01400000 && RS32(CAM_Y) < y && y < RS32(CAM_Y) + 0x00E00000) {
                        int32_t add = RS32(o + 8) - RS32(o + 0x28);
                        int s = Projectile_Spawn(1, x, y, ang, RS32(0x060C085Cu + 4u * (uint32_t)STAGE));
                        sub_06007CDE(0x125);
                        if ((int16_t)s != -1) {
                            uint32_t p = PROJECTILES + (uint32_t)(int16_t)s * 0x40;
                            W32(p + 0x18, (uint32_t)(RS32(p + 0x18) + add));
                        }
                    }
                } else if ((R16(o + 0x58) & 3) == 2 && !(R16(o + 2) & 0x4000)) W16(o + 6, 0x18);
            }
        } else {
            switch (RS16(o + 6)) {
            case 1: W16(o + 6, R16(o + 0x4A)); OR16(o + 2, 0x0800); break;
            case 3: W16(o + 6, R16(o + 0x4A)); AND16(o + 2, 0xF7FF); break;
            case 0xD: AND16(o + 2, 0xFFF7); W16(o + 6, (R16(o + 2) & 0x0800) ? 2 : 0); break;
            case 0xE:
                Enemy_DeathBurst(o); AND16(o + 2, 0xFFF7); Score_Add(200); W16(o, 0xFFFF);
                return;
            case 6: case 0x14: case 0x15: case 0x18:
                W16(o + 6, (R16(o + 2) & 0x0800) ? 2 : 0);
                break;
            }
        }
physics:
        Obj_Integrate(o);
        Obj_CollideMap(o, 3, 10);
        Enemy_AnimSwitch();
    }
    if ((R16(o + 0x58) & 3) != 0 && RS16(o + 6) == 0xF) {
        Anim_Start(SCRATCH_ANIM, 0x060BFFB0u, 0x060E43D4u, CHAR_BASE(SLOT), 0x10);
        Enemy_DrawOverlay(SCRATCH_ANIM, (int16_t)(COLOR_BANK(SLOT) + 3), o + 0x52, o + 0x54, o + 0x56);
        ovl = R16(SCRATCH_ANIM);
        if (ovl & 0x8000) {
            Enemy_DeathBurst(o); AND16(o + 2, 0xFFF7); Score_Add(200); W16(o, 0xFFFF);
            return;
        }
    }
    W16(SPR + 2, (uint32_t)((RS32(o + 8) - RS32(CAM_X)) >> 16));
    W16(SPR + 4, (uint32_t)((RS32(o + 0xC) - RS32(CAM_Y)) >> 16));
    W16(SPR + 0x10, R16(o + 2) & 0x8000);
    W16(SPR + 0x0A, (uint16_t)(COLOR_BANK(SLOT) + 2));
    W16(SPR + 0x0C, 0);
    if (RS16(o + 6) == 0xE) W16(SPR + 0x0E, R16(o + 0x58) & 3);
    else if (RS16(o + 6) == 0xF && (R16(o + 0x58) & 3) != 0) W16(SPR + 0x0E, ovl & 3);
    else W16(SPR + 0x0E, 0);
    Anim_Tick(o + 0x58);
}

/* ------------------------------------------------------------ subtype 9 */

static void Soldier09_Init(void)                             /* 0x06092768 */
{
    uint32_t o = O;
    W16(o + 6, 0);
    Anim_Start(o + 0x58, 0x060BFFB0u, 0x060E43D4u, CHAR_BASE(SLOT), RS16(o + 6));
    AND16(o + 2, 0xFFFE);
    W16(o + 0x16, 1);
    W16(o + 0x14, 10);
    W16(o + 0x4E, 2);
    W16(o + 0x52, R32(o + 0xC) >> 16);
    W16(o + 4, 0);
}

void Soldier_Sub09(void)                                     /* 0x060921CC */
{
    uint32_t o = O;
    if (far_away(o) && (!(R16(o + 2) & 0x4008) || RS16(o + 6) == 0x13)) {
        if (R16(o + 2) & 1) return;                              /* never seen yet */
        if (dist_y(o) < 0x01800000) {
            W16(o + 0x4E, 2);
            W16(o + 6, 0);
            if (RS16(o + 0x14) < 3) W16(o + 0x14, 3);
            return;
        }
        W32(o + 0xC, (uint32_t)((int32_t)RS16(o + 0x52) << 16));   /* back home */
        if (dist_y(o) > 0x01000000) return;
        if (RS32(o + 0xC) - RS32(PL + 0xC) > 0x01000000) W32(o + 0xC, (uint32_t)(RS32(o + 0xC) - 0x00800000));
        W16(o + 6, 0x11);
    }
    if (R16(o + 2) & 1) Soldier09_Init();
    if (!FROZEN) {
        int r = Soldier_CheckHits();
        int16_t a = RS16(o + 6);
        if ((R16(o + 2) & 8) && a != 0x11 && a != 0xE && a != 0xF && a != 0xD)
            Soldier_OnHit(r, o + 0x4C, o + 0x54, o + 0x56);
        if (!(R16(o + 0x58) & 0x8000)) {
            a = RS16(o + 6);
            if (a == 0 || a == 2) {
                Soldier_Think(o + 0x4A);
                if ((RS16(o + 6) == 0 || RS16(o + 6) == 2) && iabs(RS32(PL + 8) - RS32(o + 8)) < 0x00600000 &&
                    iabs(RS32(PL + 0xC) - RS32(o + 0xC)) < 0x00500000) {
                    if (!(R16(o + 4) & 4)) {                         /* Sho close: maybe jump up */
                        OR16(o + 4, 4);
                        if (((uint32_t)sub_0602DDD2() >> 9) % 3u == 0) {   /* A.BIN 0x0600CEF4 */
                            OR16(o + 4, 2);
                            W16(o + 0x50, R32(o + 0xC) >> 16);
                            AND16(o + 2, 0xF7FF);
                            W16(o + 6, 4);
                            W16(o + 0x4E, 1);
                        }
                    }
                } else AND16(o + 4, 0xFFFB);
            } else if (a == 4) {                                     /* jump (two hops) */
                if (!(R16(o + 2) & 0x4000) && RS16(o + 0x58) == 2) W16(o + 6, 0);
                if (R16(o + 0x58) & 3) {
                    if (R16(o + 0x1A) < 5) {
                        if ((R16(o + 0x58) & 3) == 1) { AND16(o + 0x58, 0xFFFC); OR16(o + 0x58, 2); }
                        int16_t n = (int16_t)(RS16(o + 0x4E) - 1);
                        W16(o + 0x4E, (uint16_t)n);
                        if (n == 0) { W16(o + 0x4E, 2); W32(o + 0x34, 0x000B0000u); }
                        else W32(o + 0x34, 0x00060000u);
                    } else if (RS32(o + 0x34) < 0x10001) {
                        W16(o + 6, 0xB);
                        AND16(o + 0x58, 0xFFFC);
                    }
                }
            } else Soldier_Think(o + 0x4A);
        } else if (RS16(o + 6) == 0xC) {                            /* landed after the throw */
            int back = 0;
            if (R16(o + 4) & 2) {
                AND16(o + 4, 0xFFFD);
                int16_t y16 = (int16_t)(R32(o + 0xC) >> 16);
                back = RS16(o + 0x50) == y16 && RS16(o + 0x52) > y16;
            }
            if (back) { OR16(o + 4, 1); W16(o + 6, 5); OR16(o + 2, 0x4000); }   /* drop back down */
            else Soldier_AnimDone(RS16(o + 0x4A));
        } else if (RS16(o + 6) == 0x11) {
            W16(o + 6, (R16(o + 2) & 0x0800) ? 2 : 0);
            AND16(o + 2, 0xFFF7);
        } else if (Soldier_AnimDone(RS16(o + 0x4A))) return;
        Obj_Integrate(o);
        if (!(R16(o + 4) & 1)) Obj_CollideMap(o, 2, 8);
        else {
            int32_t d = RS16(o + 0x52) - (int16_t)(R32(o + 0xC) >> 16);
            if (iabs(d) < 0xC9) AND16(o + 4, 0xFFFE);
        }
        if ((RS16(o + 6) == 4 || RS16(o + 6) == 5) && (R16(o + 2) & 0x10)) {
            W32(o + 0xC, R32(o + 0xC) & 0xFFF80000u);
            W16(o + 6, 6);
        }
        Enemy_AnimSwitch();
    }
    if (Soldier_Dying(o + 0x4C, o + 0x54, o + 0x56)) return;
    hide_check();
    Soldier_Draw(R16(o + 0x56));
}
