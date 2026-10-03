/* Soldier (type 0x0001) subtypes 4, 7 and 8 (see enemy_soldier_sub.c), ported from SHINOBI.PPB.
 *  4 → 0x06090D72  waits hidden (action 0x13) and drops in from the top of the screen when Sho
 *                  comes within 236 px; killed, it goes back to its spot (+0x50/+0x52, px)
 *                  hidden again and re-arms once Sho has moved 320 px away (no item the
 *                  next times). On stage index 9 it is a plain soldier that dies for good.
 *                  +4 bit 0 = armed. Death overlay anim state at +0x4C/+0x54/+0x56.
 *  7 → 0x0609126C  invisible "squad leader": when Sho comes near it spawns 4 subtype-8
 *                  soldiers 32 px apart (child slot+1 in +0x48..+0x4E, items from table
 *                  0x060C08D0[+0x18] (10 bytes per entry)) and orders them to shoot one at a
 *                  time, starting with the one nearest Sho. When the squad is wiped out it
 *                  waits for Sho to go 320 px away and spawns a new squad (without items)
 *                  when he comes back. +4 state (0 watch, 1 ordering, 2 squad dead,
 *                  3 re-arm), +0x14 timer, +0x50 current child, +0x52 order given.
 *  8 → 0x06091DE8  squad member: hidden (0x13), then drops in from the top of the screen;
 *                  behaves like the default soldier but only attacks (Soldier_Decide) when
 *                  its leader writes 9 into +0x4C. +0x48 = leader's slot index.
 *                  NOTE: subtype 8 is not dispatched by enemy_soldier_sub.c yet. */
#include "enemy_int.h"
#include "enemy_soldier_sub.h"
#include "anim.h"
#include "fx.h"           /* Projectile_InBox 0x060612C2 */
#include "objects.h"      /* Obj_Register 0x06048B64, OBJ_TABLE */
#include "player_ext.h"   /* sub_0602DDD2 = random */

void Soldier_Sub08(void);

static int32_t iabs32(int32_t v) { return v < 0 ? -v : v; }

/* same off-screen test as EnemyHandler_0001 0x0608FB9C (Y limit = X limit = lim) */
static int far_from_camera(uint32_t o, int32_t xlim)
{
    int32_t d = RS32(o + 8) - RS32(CAM_X) + (int32_t)0xFF600000;
    if (d < 0) d = RS32(CAM_X) - RS32(o + 8) + 0x00A00000;
    if (d >= xlim) return 1;
    d = RS32(o + 0xC) - RS32(CAM_Y) + (int32_t)0xFF600000;
    if (d < 0) d = RS32(CAM_Y) - RS32(o + 0xC) + 0x00A00000;
    return d >= 0x01000000;
}

/* 0x06092DA4: no auto-draw while hidden (0x13) or at the first frame of action 0x11. */
static void Soldier_HideCheck(void)
{
    uint32_t o = O;
    if (R16(o + 6) == 0x13) OR16(o + 0x58, 0x4000);
    else if (R16(o + 6) == 0x11 && R16(o + 0x5A) == 0) OR16(o + 0x58, 0x4000);
    else AND16(o + 0x58, 0xBFFF);
}

/* ------------------------------------------------------------ subtype 4 */

static void Sub04_Init(void)                                 /* 0x06091192 */
{
    uint32_t o = O;
    W16(o + 6, STAGE == 9 ? 0 : 0x13);
    Anim_Start(o + 0x58, 0x060BFFB0u, 0x060E43D4u, CHAR_BASE(SLOT), RS16(o + 6));
    AND16(o + 2, 0xFFFE);
    W16(o + 0x16, 1);
    W16(o + 4, 1);
    W16(o + 0x50, R32(o + 8) >> 16);                                /* home position (px) */
    W16(o + 0x52, R32(o + 0xC) >> 16);
    if (RS32(o + 8) < RS32(PL + 8)) AND16(o + 2, 0x7FFF); else OR16(o + 2, 0x8000);
}

/* killed: back home, hidden and disarmed, no more item */
static void Sub04_Respawn(uint32_t o)
{
    W32(o + 8, (uint32_t)((int32_t)RS16(o + 0x50) * 0x10000));
    W32(o + 0xC, (uint32_t)((int32_t)RS16(o + 0x52) * 0x10000));
    W16(o + 0x16, 1);
    W16(o + 6, 0x13);
    W16(o + 4, 0);
    W16(o + 0x18, 0xFFFF);
}

void Soldier_Sub04(void)                                     /* 0x06090D72 */
{
    uint32_t o = O;
    if (R16(o + 2) & 1) Sub04_Init();
    if (far_from_camera(o, 0x01000000) && (!(R16(o + 2) & 0x4008) || R16(o + 6) == 0x13)) {
        if (R16(o + 6) != 0x13) W16(o + 6, 0);
        W16(o + 0x14, 3);
        W16(o + 4, 1);
        Enemy_AnimSwitch();
        return;
    }
    if (!FROZEN) {
        int r = Soldier_CheckHits();
        int16_t a = RS16(o + 6);
        if ((R16(o + 2) & 8) && a != 0x13 && a != 0x0E && a != 0x0F && a != 0x0D)
            Soldier_OnHit(r, o + 0x4C, o + 0x54, o + 0x56);
        if (R16(o + 0x58) & 0x8000) {                               /* animation finished */
            if (R16(o + 6) == 0x0E && STAGE != 9) {                 /* knocked away: respawn */
                Score_Add(100);
                Enemy_DeathBurst(o);
                W16(o + 4, 0);
                Sub04_Respawn(o);
            } else if (Soldier_AnimDone(RS16(o + 0x4A))) return;
        } else if (R16(o + 6) == 0x13) {                            /* hidden */
            int32_t d = iabs32(RS32(o + 8) - RS32(PL + 8));
            if (!(R16(o + 4) & 1)) {                                /* re-arm when Sho is far */
                if (d >= RS32(0x060C08CCu)) W16(o + 4, 1);
                return;
            }
            if (d >= RS32(0x060C08C4u)) return;
            W32(o + 0xC, R32(CAM_Y));                               /* drop from the top */
            W32(o + 0x34, R32(0x060C08C8u));
            W16(o + 6, 0);
            AND16(o + 2, 0xFFF7);
        } else {
            Soldier_Think(o + 0x4A);
        }
        if (R16(o + 6) != 0x11 && R16(o + 6) != 0x13) Soldier_Physics();
        Enemy_AnimSwitch();
    }
    if (STAGE == 9) {
        if (Soldier_Dying(o + 0x4C, o + 0x54, o + 0x56)) return;
    } else if ((R16(o + 0x58) & 3) == 1 && R16(o + 6) == 0x0F) {   /* cut down: explosion */
        Anim_Start(SCRATCH_ANIM, 0x060BFFB0u, 0x060E43D4u, CHAR_BASE(SLOT), 0x10);
        Enemy_DrawOverlay(SCRATCH_ANIM, (int16_t)(COLOR_BANK(SLOT) + 1), o + 0x4C, o + 0x54, o + 0x56);
        if (R16(SCRATCH_ANIM) & 0x8000) {
            Score_Add(100);
            Enemy_DeathBurst(o);
            Sub04_Respawn(o);
            Enemy_AnimSwitch();
        }
    }
    Soldier_HideCheck();
    Soldier_Draw(R16(o + 0x56));
}

/* ------------------------------------------------------------ subtype 7 */

#define CHILD(o, i)  ((o) + 0x48u + 2u * (uint32_t)(int16_t)(i))

/* object of child id c (slot + 1), 16-bit offset arithmetic like the original */
static uint32_t child_obj(uint16_t c)
{
    return OBJ_TABLE + (uint32_t)(int32_t)(int16_t)((int32_t)(int16_t)(c - 1) * 0x78);
}

/* still one of my squad members, alive */
static int child_ok(uint32_t c)
{
    return R16(c) == 1 && RS16(c + 0x48) == (int32_t)R16(ENEMY_CUR_IDX) && (R16(c + 0x12) & 0x1F) == 8 &&
           R16(c + 6) != 0x0E && R16(c + 6) != 0x0F;
}

/* spawn squad member k at +dx; returns nothing, records slot+1 (0 = none) */
static void Sub07_Spawn(uint32_t o, int k, int32_t dx, uint16_t param)
{
    int16_t s = (int16_t)Obj_Register(1, RS32(o + 8) + dx, RS32(o + 0xC), 8, param);
    if (s == -1) { W16(CHILD(o, k), 0); return; }
    W16(CHILD(o, k), (uint16_t)(s + 1));
    uint32_t c = OBJ_TABLE + (uint32_t)(int32_t)(int16_t)(s * 0x78);
    W16(c + 0x48, R16(ENEMY_CUR_IDX));
    W16(c + 6, 0x13);
    W32(c + 0x34, 0xFFF80000u);
}

static void Sub07_Init(void)                                 /* 0x06091B5C */
{
    uint32_t o = O;
    AND16(o + 2, 0x7FFF);
    if (RS32(PL + 8) < RS32(o + 8)) OR16(o + 2, 0x8000);
    OR16(o + 2, 8);
    W16(o + 6, 0x13);
    Anim_Start(o + 0x58, 0x060BFFB0u, 0x060E43D4u, CHAR_BASE(SLOT), RS16(o + 6));
    AND16(o + 2, 0xFFFE);
    W16(o + 4, 0);
    W16(o + 0x50, 0);
    W16(o + 0x52, 0);
    uint16_t k = R16(o + 0x18);
    if (k == 0xFFFF) k = 8;
    W16(o + 0x18, 0xFFFF);
    uint32_t t = 0x060C08D0u + (uint32_t)k * 10;                    /* items of the 4 members */
    Sub07_Spawn(o, 0, 0x00000000, R16(t));
    Sub07_Spawn(o, 1, 0x00200000, R16(t + 2));
    Sub07_Spawn(o, 2, 0x00400000, R16(t + 4));
    Sub07_Spawn(o, 3, 0x00600000, R16(t + 6));
    W16(o + 0x14, 0);
}

/* next member in Sho's direction; 1 = end of the row (back to state 0) */
static int Sub07_Next(uint32_t o)
{
    if (R16(o + 2) & 0x8000) {
        if (RS16(o + 0x50) >= 3) { W16(o + 4, 0); W16(o + 0x14, 60); return 1; }
        W16(o + 0x50, R16(o + 0x50) + 1);
    } else {
        if (RS16(o + 0x50) < 1) { W16(o + 4, 0); W16(o + 0x14, 60); return 1; }
        W16(o + 0x50, R16(o + 0x50) - 1);
    }
    return 0;
}

static void Sub07_Watch(uint32_t o)                          /* state 0 */
{
    int found = 0;
    for (int16_t i = 0; i < 4; i++) {
        if (R16(CHILD(o, i)) == 0) continue;
        if (child_ok(child_obj(R16(CHILD(o, i))))) found = 1;
        else W16(CHILD(o, i), 0);
    }
    if (!found) W16(o + 4, 2);                                      /* squad wiped out */
    else {                                                          /* face Sho from the squad's centre */
        int16_t first = -1, last = -1;
        for (int16_t i = 0; i < 4; i++)
            if (R16(CHILD(o, i)) != 0) { last = i; if (first == -1) first = i; }
        int32_t cx = RS32(CAM_X);
        int32_t d = (int32_t)((uint32_t)RS32(child_obj(R16(CHILD(o, first))) + 8) - (uint32_t)cx +
                              (uint32_t)RS32(child_obj(R16(CHILD(o, last))) + 8) - (uint32_t)cx);
        int32_t mid = ((d + (d < 0)) >> 1) + cx;
        if (RS32(PL + 8) >= mid) AND16(o + 2, 0x7FFF); else OR16(o + 2, 0x8000);
    }
    if (RS16(o + 0x14) < 1) {                                       /* time to attack */
        W16(o + 0x14, 60);
        W16(o + 4, 1);
        W16(o + 0x50, (R16(o + 2) & 0x8000) ? 0 : 3);
        W16(o + 0x52, 0);
        return;
    }
    if (R16(o + 0x14) > 60) W16(o + 0x14, 1);
    int idle = 1;
    for (int16_t i = 0; i < 4; i++) {
        if (R16(CHILD(o, i)) == 0) continue;
        uint16_t a = R16(child_obj(R16(CHILD(o, i))) + 6);
        if (a != 0 && a != 2 && a != 0x13 && a != 6 && a != 0x0D && a != 0x0E && a != 0x0F) idle = 0;
    }
    if (idle) W16(o + 0x14, R16(o + 0x14) - 1);
}

static void Sub07_Order(uint32_t o)                          /* state 1 */
{
    if (RS16(o + 0x52) == 0) {                                      /* order the next member */
        for (;;) {
            uint16_t id = R16(CHILD(o, RS16(o + 0x50)));
            if (id != 0 && child_ok(child_obj(id))) {
                W16(child_obj(id) + 0x4C, 9);
                W16(o + 0x52, 1);
                return;
            }
            W16(CHILD(o, RS16(o + 0x50)), 0);
            if (Sub07_Next(o)) return;
        }
    }
    uint32_t c = child_obj(R16(CHILD(o, RS16(o + 0x50))));
    if (child_ok(c)) {
        if (RS16(c + 0x4C) != 9) { W16(c + 0x4C, 9); W16(o + 0x52, 1); return; }
        uint16_t a = R16(c + 6);
        if (!((a == 9 || a == 0x0B || a == 0x0A) && (R16(c + 0x58) & 3))) return;   /* not fired yet */
        W16(c + 0x4C, 0xFFFF);
    } else {
        W16(CHILD(o, RS16(o + 0x50)), 0);
    }
    if (Sub07_Next(o)) return;
    W16(o + 0x52, 0);
}

void Soldier_Sub07(void)                                     /* 0x0609126C */
{
    uint32_t o = O;
    if ((R16(o + 2) & 1) && iabs32(RS32(o + 0xC) - RS32(PL + 0xC)) < 0x00540000 &&
        iabs32(RS32(o + 8) - RS32(PL + 8)) < 0x00EC0000)
        Sub07_Init();
    if (R16(o + 2) & 1) return;
    if (far_from_camera(o, 0x01180000) && R16(o + 4) != 2) return;
    if (FROZEN) return;
    switch (R16(o + 4)) {
    case 0: Sub07_Watch(o); break;
    case 1: Sub07_Order(o); break;
    case 2:                                                         /* wait until Sho is far */
        if (iabs32(RS32(o + 8) - RS32(PL + 8)) > 0x01400000) W16(o + 4, 3);
        break;
    case 3:                                                         /* new squad when he is back */
        if (iabs32(RS32(o + 0xC) - RS32(PL + 0xC)) < 0x00540000 && iabs32(RS32(o + 8) - RS32(PL + 8)) < 0x00EC0000) {
            Sub07_Spawn(o, 0, 0x00000000, 0xFFFF);
            Sub07_Spawn(o, 1, 0x00200000, 0xFFFF);
            Sub07_Spawn(o, 2, 0x00400000, 0xFFFF);
            Sub07_Spawn(o, 3, 0x00600000, 0xFFFF);
            W16(o + 4, 0);
            W16(o + 0x14, 0);
        }
        break;
    }
}

/* ------------------------------------------------------------ subtype 8 */

static void Sub08_Init(void)                                 /* 0x06092128 */
{
    uint32_t o = O;
    AND16(o + 2, 0x7FFF);
    if (RS32(PL + 8) < RS32(o + 8)) OR16(o + 2, 0x8000);
    W16(o + 6, 0x13);
    Anim_Start(o + 0x58, 0x060BFFB0u, 0x060E43D4u, CHAR_BASE(SLOT), RS16(o + 6));
    AND16(o + 2, 0xFFFE);
    W16(o + 0x16, 1);
    W16(o + 4, 0x13);
    W16(o + 0x4C, 0xFFFF);
}

void Soldier_Sub08(void)                                     /* 0x06091DE8 */
{
    uint32_t o = O;
    if (far_from_camera(o, 0x01000000) && (!(R16(o + 2) & 0x4008) || R16(o + 6) == 0x13)) {
        if (R16(o + 6) != 0x13) W16(o + 6, 0);
        if (RS16(o + 0x14) < 3) W16(o + 0x14, 3);
        W16(o + 0x4C, 0xFFFF);                                      /* order cancelled */
        Enemy_AnimSwitch();
        return;
    }
    if (R16(o + 2) & 1) Sub08_Init();
    if (!FROZEN) {
        int r = Soldier_CheckHits();
        int16_t a = RS16(o + 6);
        if ((R16(o + 2) & 8) && a != 0x13 && a != 0x0D && a != 0x0E && a != 0x0F) {
            Soldier_OnHit(r, o + 0x52, o + 0x54, o + 0x56);
        } else if (R16(o + 0x58) & 0x8000) {
            if (Soldier_AnimDone(RS16(o + 0x4A))) return;
        } else if (a == 0 || a == 2) {
            AND16(o + 2, 0x7FFF);
            if (RS32(PL + 8) < RS32(o + 8)) OR16(o + 2, 0x8000);   /* face Sho */
            if (RS32(o + 0x34) != 0) W16(o + 6, 5);                 /* falling */
            else {
                int32_t dx = (R16(o + 2) & 0x8000) ? (int32_t)0xFF800000 : 0x00800000;
                int32_t x = RS32(o + 8), y = RS32(o + 0xC);
                if (!(uint16_t)Projectile_InBox(x + dx, y + (int32_t)0xFFC00000, x, y)) AND16(o + 4, 0x7FFF);
                else if (!(R16(o + 4) & 0x8000)) {                  /* dodge a shuriken (1 in 2) */
                    OR16(o + 4, 0x8000);
                    if (!(sub_0602DDD2() & 1)) { W16(o + 6, 4); goto move; }
                }
                if (RS16(o + 0x4C) == 9) Soldier_Decide(o + 0x4A);  /* ordered to attack */
            }
        } else if (a == 0x13) {                                     /* drop from the top */
            W16(o + 0x14, 10);
            W32(o + 0xC, (uint32_t)(RS32(CAM_Y) + (int32_t)0xFFEC0000));
            W16(o + 6, 0);
            AND16(o + 2, 0xFFF7);
        } else {
            Soldier_Think(o + 0x4A);
        }
move:
        if (R16(o + 6) != 0x13) Soldier_Physics();
        Enemy_AnimSwitch();
    }
    if (Soldier_Dying(o + 0x52, o + 0x54, o + 0x56)) return;
    Soldier_HideCheck();
    Soldier_Draw(R16(o + 0x56));
}
