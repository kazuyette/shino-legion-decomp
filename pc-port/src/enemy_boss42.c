/* Stage 4-2 boss (enemy type 0x0020), its energy orb (projectile 0x20) and lightning bolt
 * (projectile 0x21), ported from SHINOBI.PPB (0x06084420-0x06086F00).
 *
 * The boss is a sorcerer that teleports between the ledges (0x1025, objhandlers_d.c) sliding
 * down the screen. It shares the enemy slot conventions of enemy.c (current enemy O, slot
 * SLOT) and keeps per-body state in a small block at 0x060F81D8, indexed by the body being
 * updated (0x060F8228: 0 = the boss, 1 = its clone, spawned when its HP drops below 17):
 *   0x060F81D8[i] flags: 0x001 split done, 0x004/0x008/0x040 ledge too low (teleport),
 *     0x010 orb out, 0x020 hurt, 0x080 the boss died (the clone takes over), 0x100 one body
 *     dying, 0x200 not touching Sho, 0x400 keep the queued action, 0x800 one body dead
 *   0x060F81DC[i] ledge slot it stands on      0x060F81E0+0x20*i aura animation
 *   0x060F8220/24[i] saved next action / count  0x060F822C[i] orb angle
 *   0x060F8230/38[i] body X/Y                   0x060F8240/48[i] orb target X/Y
 *   0x060F8254/5C[i] orb drift X/Y              0x060F8264[i] last random choice
 *   0x060F8268 action forced on the clone       0x060F826A clone HP
 *   0x060F826E[i] object slot                   0x060F8272 bolt cool-down
 *   0x060F822A/50/52/6C indices in the attack-count tables 0x060BD006/1C/3C/50.
 * Object fields: +6 action, +0x16 HP (0x060BD0B8[difficulty] = 36/44/52), +0x48 queued
 * action, +0x4C hit flag, +0x4E repeat count, +0x50 clone flag, +0x52 teleport result,
 * +0x54 placed on its ledge, +0x56 died first (does not end the fight).
 * Actions (anims of 0x060BC5DC/0x060E2400): 0 intro (starts the ledges when it ends),
 * 1 stand / decide, 2 -> 4 teleport to another ledge, 5 -> 6 cast the orb, 0xC -> 0xD
 * -> 0xE call lightning on Sho's ledge, 0xB hurt, 0x10 -> 0x11 split, 0x12 dying,
 * 0x13 death flash object (subtype 2: fades out and posts "stage clear"), 0x14 gone.
 * Hitting it: sword (Enemy_CheckPlayerHits, no shurikens) or its own orb sent back with
 * the sword (4 damage). */
#include "enemy.h"
#include "enemy_int.h"
#include "objects.h"
#include "objhandlers.h"
#include "objhandlers_d.h"
#include "fx.h"
#include "fade.h"
#include "anim.h"
#include "sprite.h"
#include "fixmath.h"
#include "player_ext.h"
#include "ram.h"
#include <string.h>

#define BI            RS16(0x060F8228u)
#define BFL(i)        (0x060F81D8u + 2u * (uint32_t)(i))
#define BLEDGE(i)     (0x060F81DCu + 2u * (uint32_t)(i))
#define BAURA(i)      (0x060F81E0u + 0x20u * (uint32_t)(i))
#define BSAVE_NEXT(i) (0x060F8220u + 2u * (uint32_t)(i))
#define BSAVE_CNT(i)  (0x060F8224u + 2u * (uint32_t)(i))
#define IDX5          0x060F822Au
#define ORB_ANG(i)    (0x060F822Cu + 2u * (uint32_t)(i))
#define BX(i)         (0x060F8230u + 4u * (uint32_t)(i))
#define BY(i)         (0x060F8238u + 4u * (uint32_t)(i))
#define ORB_TX(i)     (0x060F8240u + 4u * (uint32_t)(i))
#define ORB_TY(i)     (0x060F8248u + 4u * (uint32_t)(i))
#define IDX2          0x060F8250u
#define IDX12         0x060F8252u
#define ORB_DX(i)     (0x060F8254u + 4u * (uint32_t)(i))
#define ORB_DY(i)     (0x060F825Cu + 4u * (uint32_t)(i))
#define LAST_CHOICE(i) (0x060F8264u + 2u * (uint32_t)(i))
#define SYNC          0x060F8268u
#define CLONE_HP      0x060F826Au
#define IDXL          0x060F826Cu
#define BSLOT(i)      (0x060F826Eu + 2u * (uint32_t)(i))
#define CLONE_SLOT    0x060F8270u
#define BOLT_WAIT     0x060F8272u
#define WORLD_TOP     RS32(0x060EA61Cu)       /* stage control block C+4 */
#define DEFS          0x060BC5DCu
#define SCRIPTS       0x060E2400u
#define VBLANKS       0x060143EAu
#define PX            RS32(0x060EA2BCu)
#define PY            RS32(0x060EA2C0u)

static uint32_t slot_addr(int s) { return OBJ_TABLE + (uint32_t)(int16_t)(s * (int)OBJ_SIZE); }
static int32_t iabs(int32_t v) { return v < 0 ? -v : v; }
static void flag_or(int i, uint16_t m)  { OR16(BFL(i), m); }
static void flag_and(int i, uint16_t m) { AND16(BFL(i), m); }
static int16_t hi16(int32_t v) { return (int16_t)((uint32_t)v >> 16); }

/* ------------------------------------------------------------------ loader */

void Enemy20_Load(void)                                         /* 0x06084420 */
{
    Enemy_RegisterChars(0x20, 0x060E1AE4u);
    int16_t b = COLOR_BANK(Enemy_FindSlot(0x20));
    Pal_Copy((uint16_t)b * 16, 0x100, 0x060E1E90u);            /* 16 palettes */
    Pal_Copy((uint16_t)(b + 16) * 16, 16, SHADOW_CRAM + (uint32_t)(b + 13) * 32);   /* hurt flash x2 */
    Pal_Copy((uint16_t)(b + 17) * 16, 16, SHADOW_CRAM + (uint32_t)(b + 13) * 32);
    Pal_Copy((uint16_t)(b + 18) * 16, 16, 0x060E20B0u);         /* reflected orb */
}

/* ------------------------------------------------------------------ helpers */

static void Boss_Init(void)                                     /* 0x0608587C */
{
    uint32_t o = O;
    int sub = R16(o + 0x12) & 0x1F;
    if (sub == 0) {
        W16(o + 6, 0);
        Anim_Start(o + 0x58, DEFS, SCRIPTS, CHAR_BASE(SLOT), RS16(o + 6));
        Anim_Start(BAURA(sub), DEFS, SCRIPTS, CHAR_BASE(SLOT), 3);
        OR16(o + 0x58, 0x4000);
        OR16(BAURA(sub), 0x4000);
        AND16(o + 2, 0xFFFE);
        W16(o + 0x16, R16(0x060BD0B8u + 2u * (uint32_t)RS16(0x060252C4u)));
        W16(o + 0x14, 0); W32(o + 0x30, 0); W32(o + 0x34, 0);
        W16(o + 0x48, 0); W16(o + 0x4C, 0); W16(o + 0x50, 0);
        W16(o + 0x52, 0); W16(o + 0x54, 0); W16(o + 0x56, 0);
        W16(BFL(0), 0); W16(BFL(1), 0);
        W16(BLEDGE(0), 0xFFFF); W16(BLEDGE(1), 0xFFFF);
        W16(IDX5, 0); W16(IDX2, 0); W16(IDX12, 0); W16(IDXL, 0);
        OR16(PL + 4, 0x0400);                                   /* Sho invulnerable during the intro */
        W16(BSLOT(0), R16(ENEMY_CUR_IDX));
    } else {                                                    /* the clone */
        W16(o + 6, 0x11);
        Anim_Start(o + 0x58, DEFS, SCRIPTS, CHAR_BASE(SLOT), RS16(o + 6));
        Anim_Start(BAURA(sub), DEFS, SCRIPTS, CHAR_BASE(SLOT), 3);
        AND16(o + 2, 0xFFFE);
        W16(o + 0x16, R16(CLONE_HP));
        W16(o + 0x14, 0); W32(o + 0x30, 0); W32(o + 0x34, 0);
        W16(o + 0x48, 0); W16(o + 0x4C, 0); W16(o + 0x50, 1);
        W16(CLONE_SLOT, R16(ENEMY_CUR_IDX));
    }
    OR16(o + 2, 0x0100);
}

/* 0x06085AA6: a ledge to teleport to (moving, in the landing band, not Sho's...), the
 * n-th candidate from table 0x060BD050; mode 1: on screen only, 2: not the current one. */
static int Boss_FindLedge(int16_t mode)
{
    int16_t n = RS16(0x060BD050u + 2u * (uint32_t)RS16(IDXL));
    if (R16(IDXL) < 10) W16(IDXL, R16(IDXL) + 1); else W16(IDXL, 0);
    int16_t found = -1, last = -1;
    for (int16_t s = 0; s < 0xC0; s++) {
        uint32_t l = slot_addr(s);
        if (R16(l) != 0x1025 || R16(l + 0x4E) != 0 || R16(l + 0x4A) == 0) continue;
        int16_t cand = s;                                       /* any free ledge in the band */
        if ((mode == 0 || WORLD_TOP <= RS32(l + 0xC)) && (mode != 2 || s != RS16(BLEDGE(BI)))) {
            int16_t other = BI != 0 ? RS16(BLEDGE(0)) : RS16(BLEDGE(1));
            uint16_t ofl = BI != 0 ? R16(BFL(0)) : R16(BFL(1));
            if ((ofl & 8) || s != other) {                      /* not where the other body is */
                if (n == 0) return s;
                n--;
                found = s;
            } else cand = last;
        }
        last = cand;
    }
    return found == -1 ? last : found;
}

/* 0x06085C26: the ledge Sho stands on (mode 0: only if it is not low), or -1. */
static int Boss_LedgeWithSho(int16_t mode)
{
    for (int16_t s = 0; s < 0xC0; s++) {
        uint32_t l = slot_addr(s);
        if (R16(l) != 0x1025 || R16(l + 0x4E) == 0) continue;
        if (R16(l + 0x4C) != 0 && mode == 0) return -1;
        return s;
    }
    return -1;
}

/* 0x06085C8E: stand on ledge s; 1 when that ledge is low (time to leave). */
static int Boss_StandOn(int16_t s)
{
    uint32_t o = O, l = slot_addr(s);
    W32(o + 8, (uint32_t)(RS32(l + 8) + 0x300000));
    W32(o + 0xC, (uint32_t)(RS32(l + 0xC) + 0x100000));
    return R16(l + 0x4C) != 0;
}

/* 0x06086C9C: queue an attack and how many times to repeat it. */
static void Boss_Queue(int16_t act)
{
    uint32_t o = O;
    if (act == 2) {
        W16(o + 0x48, 2);
        W16(o + 0x4E, R16(0x060BD01Cu + 2u * (uint32_t)RS16(IDX2)));
        W16(IDX2, R16(IDX2) < 15 ? R16(IDX2) + 1 : 0);
    } else if (act == 5) {
        W16(o + 0x48, 5);
        W16(o + 0x4E, R16(0x060BD006u + 2u * (uint32_t)RS16(IDX5)));
        W16(IDX5, R16(IDX5) < 10 ? R16(IDX5) + 1 : 0);
    } else if (act == 0xC) {
        W16(o + 0x48, 0xC);
        W16(o + 0x4E, R16(0x060BD03Cu + 2u * (uint32_t)RS16(IDX12)));
        W16(IDX12, R16(IDX12) < 9 ? R16(IDX12) + 1 : 0);
    }
}

/* 0x06086D90: next attack: 40% orb, 30% lightning / 30% teleport (alternating). */
static int16_t Boss_Choose(void)
{
    uint32_t r = (uint32_t)sub_0602DDD2();
    int lit = 0;
    for (int16_t s = 0; s < 0xC0; s++) {
        uint32_t l = slot_addr(s);
        if (R16(l) == 0x1025 && R16(l + 0x50) != 0) { lit = 1; break; }
    }
    uint32_t v = r % 101u;                                      /* A.BIN 0x0600CEF4 */
    int16_t a, last = RS16(LAST_CHOICE(BI));
    if (v > 60) a = 5;
    else if (v > 30) a = (last == 0xC || lit) ? 5 : 0xC;
    else a = (last != 2) ? 2 : (lit ? 5 : 0xC);
    W16(LAST_CHOICE(BI), (uint16_t)a);
    return a;
}

/* subtype 2: death flash (0x06086E68) */
static void Boss_DeathFlash(void)
{
    uint32_t o = O;
    if (R16(o + 2) & 1) {
        W16(o + 6, 0x13);
        Anim_Start(o + 0x58, DEFS, SCRIPTS, CHAR_BASE(SLOT), RS16(o + 6));
        AND16(o + 2, 0xFFFE);
    }
    if (R16(o + 0x58) & 0x8000) {                               /* stage clear */
        Fade_SetTarget(0x100, 0x100, 0x100);
        Fade_SetEndLayerMask(0);
        Fade_SetNextSpeed(0x20000, 1);
        Fade_StartOut();
        W16(0x0602529Eu, (R16(0x0602529Eu) & 0xFFF8) | 10);
        W16(o, 0xFFFF);
        return;
    }
    W16(SPR + 2, (uint32_t)hi16(RS32(o + 8) - RS32(CAM_X)));
    W16(SPR + 4, (uint32_t)hi16(RS32(o + 0xC) - RS32(CAM_Y)));
    W16(SPR + 0x10, 0x0200);
    W32(SPR + 0x18, (R16(VBLANKS) & 1) ? 1u : 0xFFFFFFFFu);
    W16(SPR + 0x0A, (uint32_t)(COLOR_BANK(SLOT) + RS16(o + 0x48) + 0x10));
    W16(SPR + 0x0C, 0);
    Anim_Tick(o + 0x58);
}

static void set_all_moving(uint16_t v)
{
    for (int16_t s = 0; s < 0xC0; s++) {
        uint32_t l = slot_addr(s);
        if (R16(l) == 0x1025 || R16(l) == 0x1026) W16(l + 0x48, v);
        else if (R16(l) == 0x1028) W16(l + 0x4A, v);
    }
}

/* ------------------------------------------------------------- the handler */

void EnemyHandler_0020(void)                                     /* 0x06084560 */
{
    uint32_t o = O;
    uint16_t sub = R16(o + 0x12) & 0x1F;
    if (sub < 2) W16(0x060F8228u, sub);
    else if (sub == 2) { Boss_DeathFlash(); return; }
    int i = BI;
    W32(BX(i), R32(o + 8));
    W32(BY(i), R32(o + 0xC));
    if (R16(o + 2) & 1) Boss_Init();
    if (BI == 0) W16(SYNC, 0xFFFF);
    else if (RS16(SYNC) != -1) {                                /* the clone copies the boss */
        int16_t a = RS16(o + 6);
        if (a != 4 && a != 2 && a != 0xB && a != 0x12) {
            int16_t s = RS16(SYNC);
            if (RS32(o + 0xC) < WORLD_TOP + 0x460000 && (s == 5 || s == 6)) s = 1;
            W16(o + 6, (uint16_t)s);
            Enemy_AnimSwitch();
        }
    }
    i = BI;
    if (!FROZEN) {
        if (!(R16(BFL(i)) & 0x200)) Enemy_AttackPlayer(0, 1, 0xC000);
        Enemy_CheckPlayerHits(0xA000);
        flag_and(i, 0xFDFF);
        int16_t a = RS16(o + 6);
        if ((R16(o + 2) & 8) && a != 0xB && a != 0x12 && a != 0x14) {   /* hurt */
            W16(o + 0x4C, 1);
            if ((a == 5 || a == 6) && RS16(o + 0x4E) > 2) {
                flag_or(i, 0x400);
                W16(o + 0x48, 5);
                W16(o + 0x4E, R16(o + 0x4E) - 1);
            } else flag_and(i, 0xFBFF);
            W16(o + 6, 0xB);
            flag_or(i, 0x20);
            if (RS16(o + 0x16) < 1) OR16(o + 0x58, 0x8000);
            else sub_06007CDE(0x205);
        }
        if (RS16(o + 0x16) < 0) W16(o + 0x16, 0);
        if (RS16(o + 6) != 5 && RS16(o + 6) != 6) {
            if (RS32(o + 8) < PX) AND16(o + 2, 0x7FFF); else OR16(o + 2, 0x8000);
        }
        if (!(R16(o + 0x58) & 0x8000)) {                         /* during the animation */
            switch (R16(o + 6)) {
            case 0:
                Boss_StandOn(RS16(BLEDGE(i)));
                if ((R16(o + 0x58) & 3) == 3 || R16(o + 0x54) != 0) {
                    if (R16(o + 0x54) == 0) {                   /* appear on the first ledge */
                        int16_t s = 0;
                        for (; s < 0xC0; s++)
                            if (R16(slot_addr(s)) == 0x1025 && (R16(slot_addr(s) + 0x12) & 4) == 4) break;
                        W16(o + 0x54, 1);
                        W16(BLEDGE(i), (uint16_t)s);
                        W32(o + 8, (uint32_t)(RS32(slot_addr(s) + 8) + 0x300000));
                        W32(o + 0xC, (uint32_t)(RS32(slot_addr(s) + 0xC) + 0x100000));
                        AND16(o + 0x58, 0xFFFC);
                        AND16(o + 0x58, 0xBFFF);
                        AND16(BAURA(i), 0xBFFF);
                    }
                    if ((R16(o + 0x58) & 3) == 0) W16(o + 0x54, 0);
                } else if ((R16(o + 0x58) & 0xF) == 4) {
                    AND16(o + 0x58, 0xFFF0);
                    W16(o + 0x52, 1);
                }
                break;
            case 1:
                if (RS32(o + 0xC) < WORLD_TOP + 0x460000) {     /* carried to the top */
                    if (BI == 0) {
                        if (RS16(BOLT_WAIT) < 1) {
                            int lit = 0;
                            for (int16_t s = 0; s < 0xC0; s++)
                                if (R16(slot_addr(s)) == 0x1025 && R16(slot_addr(s) + 0x50) != 0) { lit = 1; break; }
                            if (lit) W16(BOLT_WAIT, 0);
                            else { Boss_Queue(0xC); W16(o + 6, 0xC); W16(BOLT_WAIT, 10); }
                            break;
                        }
                        W16(o + 0x5A, 0);
                        W16(o + 0x72, 0);
                        W16(BOLT_WAIT, R16(BOLT_WAIT) - 1);
                    } else {
                        Boss_Queue(2); W16(o + 6, 2); W16(o + 0x4E, 1);
                    }
                }
                if (Boss_StandOn(RS16(BLEDGE(BI))) && RS16(o + 0x48) != 2) {
                    flag_or(BI, 0x4C);
                    if (BI != 0) flag_or(0, 0x4C);
                }
                break;
            case 2: case 0xB: case 0x10: case 0x11: case 0x14:
                if (RS16(o + 6) == 2 && (R16(o + 0x58) & 3)) flag_or(BI, 0x200);
                Boss_StandOn(RS16(BLEDGE(BI)));
                break;
            case 4:                                             /* teleporting */
                flag_or(BI, 0x200);
                if ((R16(o + 0x58) & 3) == 3 || R16(o + 0x54) != 0) {
                    if (R16(o + 0x54) == 0) {
                        int16_t s = (int16_t)Boss_FindLedge(2);
                        if (s == -1) {                          /* nowhere to go: try again */
                            Boss_StandOn(RS16(BLEDGE(BI)));
                            Anim_Start(o + 0x58, DEFS, SCRIPTS, CHAR_BASE(SLOT), RS16(o + 6));
                            OR16(o + 0x58, 0x4000);
                            W16(o + 0x52, 2);
                            break;
                        }
                        Boss_StandOn(RS16(BLEDGE(BI)));
                        W16(o + 0x52, 1);
                        W32(o + 8, (uint32_t)(RS32(slot_addr(s) + 8) + 0x300000));
                        W32(o + 0xC, (uint32_t)(RS32(slot_addr(s) + 0xC) + 0x100000));
                        W16(BLEDGE(BI), (uint16_t)s);
                        AND16(BAURA(BI), 0xBFFF);
                        AND16(o + 0x58, 0xBFFF);
                        W16(o + 0x54, 1);
                    } else Boss_StandOn(RS16(BLEDGE(BI)));
                    if ((R16(o + 0x58) & 3) == 0) {             /* arrived */
                        W16(o + 0x54, 0);
                        W16(o + 0x4E, R16(o + 0x4E) - 1);
                        if (RS16(o + 0x4E) < 1) {
                            uint16_t f = R16(BFL(BI));
                            if (!(f & 4) || (f & 0x40)) {
                                W16(o + 6, 1);
                                flag_and(BI, 0xFBFF);
                            } else {                            /* resume the interrupted attack */
                                flag_and(BI, 0xFFFB);
                                flag_or(BI, 0x400);
                                W16(o + 6, 1);
                                W16(o + 0x48, R16(BSAVE_NEXT(BI)));
                                W16(o + 0x4E, R16(BSAVE_CNT(BI)));
                            }
                            if (BI == 0) W16(SYNC, R16(o + 6));
                        } else {
                            W16(o + 6, 1);
                            W16(o + 0x48, 2);
                            flag_or(BI, 0x400);
                        }
                    }
                }
                break;
            case 5:                                             /* cast the orb */
                if (Boss_StandOn(RS16(BLEDGE(BI)))) {
                    flag_or(BI, 0x4C);
                    if (BI != 0) flag_or(0, 0x4C);
                }
                if ((R16(o + 0x58) & 3) == 1) {
                    if (!(R16(BFL(BI)) & 0x10)) {
                        AND16(o + 0x58, 0xFFFC);
                        if (RS32(o + 8) < PX) AND16(o + 2, 0x7FFF); else OR16(o + 2, 0x8000);
                        flag_or(BI, 0x10);
                        W16(BSLOT(BI), R16(ENEMY_CUR_IDX));
                        Projectile_Spawn(0x20, RS32(o + 8), RS32(o + 0xC), (int16_t)(BI | (R16(o + 2) & 0x8000)), 0);
                    } else W16(o + 0x72, 0);
                } else if ((R16(o + 0x58) & 3) == 2 && !(R16(BFL(BI)) & 0x10)) {
                    W16(o + 6, 6);
                }
                break;
            case 6:
                if (Boss_StandOn(RS16(BLEDGE(BI))) && RS16(o + 0x48) != 2) {
                    flag_or(BI, 0x4C);
                    if (BI != 0) flag_or(0, 0x4C);
                }
                break;
            case 0xC: {                                         /* lightning */
                Boss_StandOn(RS16(BLEDGE(BI)));
                if ((R16(o + 0x58) & 3) == 1) {
                    int16_t s = (int16_t)Boss_LedgeWithSho(1);
                    if (s != -1) {
                        AND16(o + 0x58, 0xFFFC);
                        if (BI == 0)
                            Projectile_Spawn(0x21, RS32(slot_addr(s) + 8) + 0x300000, RS32(slot_addr(s) + 0xC) + 0x100000, s, 0);
                    }
                }
                break;
            }
            case 0xD:
                Boss_StandOn(RS16(BLEDGE(BI)));
                break;
            case 0xE:
                if (Boss_StandOn(RS16(BLEDGE(BI)))) {
                    W16(o + 6, 1);
                    W16(o + 0x48, 0xC);
                    flag_or(BI, 0x400);
                    if (BI == 0) W16(SYNC, R16(o + 6));
                }
                break;
            case 0x12:                                          /* dying */
                Boss_StandOn(RS16(BLEDGE(BI)));
                if ((R16(BFL(BI)) & 0x100) && R16(o + 0x56) == 0) {
                    if (R16(o + 0x52) != 3) {                   /* the last one: stop everything */
                        set_all_moving(0);
                        W16(o + 0x52, 3);
                        W32(0x060F4D44u, 1);                    /* 0x0606DC5C: stage hook 0x0606D99E: */
                        W32(0x060F4D4Cu, 0x50);                 /* NBG1 wave out after 80 frames (longs) */
                        W16(PL + 0x1C, 0x8000);
                        OR16(PL + 4, 0x0400);
                    }
                    if ((R16(o + 0x58) & 3) == 1) {
                        W16(o + 6, 0x14);
                        int s = Obj_Register(0x20, RS32(o + 8), RS32(o + 0xC), 2, 0xFFFF);
                        if (s != -1) W16(slot_addr(s) + 0x48, (uint16_t)BI);
                    }
                }
                break;
            default: break;
            }
        } else {                                                /* animation finished */
            switch (R16(o + 6)) {
            case 0:                                             /* intro over: the climb starts */
                W16(o + 6, 1);
                AND16(PL + 4, 0xFBFF);
                sub_06007CDE(0x211);
                set_all_moving(1);
                break;
            case 1:
                if (!(R16(BFL(BI)) & 0x400)) Boss_Queue(Boss_Choose());
                if (BI != 0 && (R16(BFL(1)) & 0x80)) {          /* the boss died: the clone takes over */
                    W16(o + 0x50, 1);
                    AND16(o + 0x12, 0xFFE0);
                    W16(BLEDGE(0), R16(BLEDGE(1)));
                    W16(BFL(0), R16(BFL(1)));
                    memmove(Ram_Ptr(BAURA(0)), Ram_Ptr(BAURA(1)), 0x20);   /* A.BIN 0x0600A6CC */
                    W16(0x060F8228u, 0);
                }
                if (!(R16(BFL(BI)) & 1) && BI == 0 && RS16(o + 0x16) < 0x11) W16(o + 0x48, 0x10);
                if (!(R16(BFL(BI ^ 1)) & 0x20)) {
                    if (R16(BFL(BI)) & 0x40) {
                        flag_and(BI, 0xFFBF);
                        W16(BSAVE_NEXT(BI), R16(o + 0x48));
                        W16(BSAVE_CNT(BI), R16(o + 0x4E));
                    }
                    if (R16(BFL(BI)) & 8) {
                        flag_and(BI, 0xFFF7);
                        W16(o + 0x48, 2);
                        W16(o + 0x4E, 1);
                    }
                    if (RS16(o + 0x48) == 0xC) {
                        for (int16_t s = 0; s < 0xC0; s++)
                            if (R16(slot_addr(s)) == 0x1025 && R16(slot_addr(s) + 0x50) != 0) {
                                W16(o + 0x48, 2); W16(o + 0x4E, 1); break;
                            }
                    }
                    W16(o + 6, R16(o + 0x48));
                    if (BI == 0) W16(SYNC, R16(o + 0x48));
                } else Boss_StandOn(RS16(BLEDGE(BI)));
                break;
            case 2:
                W16(BOLT_WAIT, 0);
                OR16(o + 0x58, 0x4000);
                OR16(BAURA(BI), 0x4000);
                W16(o + 6, 4);
                flag_or(BI, 0x200);
                break;
            case 6:                                             /* orb thrown: again? */
            case 0xD: {                                         /* lightning called: again? */
                int16_t again = RS16(o + 6) == 6 ? 5 : 0xE;
                W16(o + 0x4E, R16(o + 0x4E) - 1);
                W16(o + 6, 1);
                if (RS16(o + 0x4E) == 0) flag_and(BI, 0xFBFF);
                else { W16(o + 0x48, (uint16_t)again); flag_or(BI, 0x400); }
                break;
            }
            case 0xB:                                           /* hurt anim over */
                if (RS16(o + 0x16) < 1) {
                    W16(o + 6, 0x12);
                    if (!(R16(BFL(0)) & 0x100)) {
                        flag_or(0, 0x100); flag_or(1, 0x100);
                        W16(o + 0x56, 1);
                    }
                } else {
                    int16_t b = COLOR_BANK(SLOT);
                    Pal_Copy((uint16_t)(BI + b + 0x10) * 16, 16, SHADOW_CRAM + (uint32_t)(b + 13) * 32);
                    flag_and(BI, 0xFFDF);
                    AND16(o + 2, 0xFFF7);
                    W16(o + 6, 1);
                }
                break;
            case 0xC:
                W16(o + 6, 0xD);
                break;
            case 0xE:
                W16(o + 6, 1);
                W16(o + 0x48, 0xC);
                flag_or(BI, 0x400);
                break;
            case 0x10: {                                        /* split in two */
                sub_06007CDE(0x10D);
                W16(o + 0x16, 0x10);
                W16(0x060F8228u, 1);
                int16_t s = (int16_t)Boss_FindLedge(1);
                W16(0x060F8228u, 0);
                if (s != -1) {
                    W16(o + 0x16, (uint16_t)(RS16(o + 0x16) >> 1));
                    W16(CLONE_HP, R16(o + 0x16));
                    Obj_Register(0x20, RS32(slot_addr(s) + 8) + 0x300000, RS32(slot_addr(s) + 0xC) + 0x100000, 1, 0xFFFF);
                    flag_or(0, 1); flag_or(1, 1);
                    W16(BLEDGE(1), (uint16_t)s);
                    W16(o + 6, 0x11);
                }
                break;
            }
            case 0x11:
                W16(o + 6, 1);
                W16(o + 0x48, 5);
                W16(o + 0x4E, R16(0x060BD006u + 2u * (uint32_t)RS16(IDX5)));
                W16(IDX5, R16(IDX5) < 10 ? R16(IDX5) + 1 : 0);
                flag_or(BI, 0x400);
                break;
            case 0x12: {                                        /* dead */
                int16_t b = COLOR_BANK(SLOT);
                Pal_Copy((uint16_t)(BI + b + 0x10) * 16, 16, SHADOW_CRAM + (uint32_t)(b + 13) * 32);
                flag_and(BI, 0xFFDF);
                if (BI == 0) { flag_or(0, 0x80); flag_or(1, 0x80); }
                flag_or(0, 0x800); flag_or(1, 0x800);
                W16(o, 0xFFFF);
                return;
            }
            case 0x14:
                W16(o, 0xFFFF);
                return;
            default: break;
            }
        }
    }
    /* draw: aura (scaled x1.5, flickering) then the body */
    Enemy_AnimSwitch();
    i = BI;
    if (RS16(o + 6) == 4) {
        if (R16(o + 0x54) == 0) { OR16(BAURA(i), 0x4000); OR16(o + 0x58, 0x4000); }
        else { AND16(BAURA(i), 0xBFFF); AND16(o + 0x58, 0xBFFF); }
        if (R16(o + 0x58) & 3) {
            if (RS16(o + 0x52) == 2) { OR16(BAURA(i), 0x4000); OR16(o + 0x58, 0x4000); }
            else { AND16(BAURA(i), 0xBFFF); AND16(o + 0x58, 0xBFFF); }
        }
    } else { AND16(BAURA(i), 0xBFFF); AND16(o + 0x58, 0xBFFF); }
    int16_t bank = COLOR_BANK(SLOT);
    W16(SPR + 2, (uint32_t)hi16(RS32(o + 8) - RS32(CAM_X)));
    W16(SPR + 4, (uint32_t)hi16(RS32(o + 0xC) - RS32(CAM_Y)));
    W16(SPR + 0x10, (R16(o + 2) & 0x8000) | 0x1200);
    W32(SPR + 0x18, (R16(VBLANKS) & 1) ? 0xFFFFFFFFu : 1u);
    W32(SPR + 0x1C, 0x18000);                                  /* (only X scale is set) */
    if (!(R16(o + 2) & 8)) W16(SPR + 0x0A, (uint32_t)(bank + 0xD));
    else {                                                      /* hurt: flash fades in */
        if (!(R16(BFL(i)) & 0x800)) Pal_StepTowards((bank + i + 0x10) * 0x10, 0x10, 0x060E2090u, 1);
        else {
            Pal_StepTowards((bank + 0x10) * 0x10, 0x10, 0x060E2090u, 1);
            Pal_StepTowards((bank + 0x11) * 0x10, 0x10, 0x060E2090u, 1);
        }
        W16(SPR + 0x0A, (uint32_t)(bank + i + 0x10));
    }
    W16(SPR + 0x0C, 0);
    int16_t a = RS16(o + 6);
    W16(SPR + 0x0E, (a == 0 || a == 2 || a == 4 || a == 0x12) ? (R16(o + 0x58) & 3) : 0);
    Anim_Tick(BAURA(i));
    W16(SPR + 2, (uint32_t)hi16(RS32(o + 8) - RS32(CAM_X)));
    W16(SPR + 4, (uint32_t)hi16(RS32(o + 0xC) - RS32(CAM_Y)));
    W16(SPR + 0x10, (R16(o + 2) & 0x8000) | 0x0200);
    W32(SPR + 0x18, 0);
    W16(SPR + 0x0A, (uint32_t)bank);
    W16(SPR + 0x0C, 0);
    Anim_Tick(o + 0x58);
}

/* -------------------------------------------------------------- projectiles */

/* 0x06060E8E (as fx.c): object hitboxes of kind ko vs the projectile's of kind kp */
static uint16_t proj_hitbox(uint32_t o, int16_t ko, uint32_t p, int16_t kp)
{
    if (iabs(RS32(o + 8) - RS32(p + 8)) > 0x02000000) return 0xFFFF;
    if (iabs(RS32(o + 0xC) - RS32(p + 0xC)) > 0x02000000) return 0xFFFF;
    static const uint32_t TMP = 0x060FFE00u;                    /* PC scratch object view */
    W32(TMP + 8, R32(p + 8)); W32(TMP + 0xC, R32(p + 0xC));
    W16(TMP + 2, R16(p + 4) & 0x8000); W32(TMP + 0x68, R32(p + 0x30));
    return Hitbox_Test(o, ko, TMP, kp);
}

/* 0x06060F64: first object (Sho for hostile projectiles) whose kind-ko boxes touch the
 * projectile's kind-kp boxes; HIT_POINT = overlap centre. */
static uint16_t proj_touch(uint32_t p, int16_t kp, uint32_t *who, int16_t ko)
{
    uint16_t r = 0xFFFF;
    if (R16(p + 4) & 0x4000) return r;
    if (!(R16(p + 4) & 0x2000)) {
        uint32_t o = OBJ_TABLE;
        for (int n = 0xC0; n > 0; n--, o += 0x78) {
            if (R16(o) == 0xFFFF || (R16(o + 2) & 8)) continue;
            r = proj_hitbox(o, ko, p, kp);
            if (r != 0xFFFF) { *who = o; return r; }
        }
        return r;
    }
    if (!(R16(PL + 2) & 8) && R16(R32(0x060EA41Cu)) == 0 && !(R16(PL + 4) & 0x0410)) {
        r = proj_hitbox(PL, ko, p, kp);
        if (r != 0xFFFF) *who = PL;
    }
    return r;
}

static void proj_anim(uint32_t e, int anim)
{
    Anim_Start(e + 0x20, DEFS, SCRIPTS, CHAR_BASE(Enemy_FindSlot(0x20)), anim);
}

/* Projectile 0x20 (0x06085CD8): energy orb. Grows in the boss's hand (+0x10 & 2 clear,
 * follows the hand offsets 0x060BD068/0x060BD090[frame]), waits 15 frames, then homes on
 * where Sho was (turning 4/256 per frame, 3.75 px/frame). The sword sends it back at
 * 7.5 px/frame: then it hurts the boss (4). +0x10: 1 sent back, 2 thrown, 4/8 bursting,
 * 0x10 waiting, 0x20 locked on. +0x14 = body index | facing. */
void Proj_Boss42Orb(uint32_t e)
{
    int i = R16(e + 0x14) & 0xF;
    if (R16(e + 4) & 1) {
        proj_anim(e, 7);
        AND16(e + 4, 0xFFFE);
        W16(e + 0x10, 0);
        W16(e + 6, 0);
        W16(ORB_ANG(i), (R16(e + 0x14) & 0x8000) ? 0x80 : 0);
        W32(e + 0x18, 0); W32(e + 0x1C, 0);
        W32(ORB_DX(i), 0); W32(ORB_DY(i), 0);
    }
    if (!FROZEN) {
        uint16_t f = R16(e + 0x10);
        if (!(f & 2)) {                                         /* in the boss's hand */
            if ((R16(e + 0x20) & 3) == 1) OR16(e + 0x20, 0x4000); else AND16(e + 0x20, 0xBFFF);
            uint32_t b = slot_addr(RS16(BSLOT(i)));
            if (R16(b + 2) & 8) {                               /* the boss was hit: lost */
                flag_and(i, 0xFFEF);
                if (RS16(b + 0x16) < 1) { flag_and(0, 0xFFEF); flag_and(1, 0xFFEF); }
                W16(e, 0xFFFF);
                return;
            }
            uint32_t fr = R16(e + 0x22);
            int32_t dx = RS32(0x060BD068u + fr * 4);
            W32(e + 8, (uint32_t)((R16(e + 0x14) & 0x8000) ? RS32(BX(i)) - dx : RS32(BX(i)) + dx));
            W32(e + 0xC, (uint32_t)(RS32(BY(i)) + RS32(0x060BD090u + fr * 4)));
        } else if (!(f & 4)) {
            if (f & 8) {                                        /* burst over */
                if (R16(e + 0x20) & 0x8000) { flag_and(i, 0xFFEF); W16(e, 0xFFFF); return; }
            } else if (!(f & 1)) {                              /* thrown */
                if (!(f & 0x10)) {
                    int ang = Atan8(hi16(RS32(e + 8)), hi16(RS32(e + 0xC)), hi16(RS32(ORB_TX(i))), hi16(RS32(ORB_TY(i))));
                    W16(ORB_ANG(i), R16(ORB_ANG(i)) & 0xFF);
                    if (!(R16(e + 0x10) & 0x20)) {              /* turn towards the target */
                        int wrapped = 0;
                        int16_t cur = RS16(ORB_ANG(i));
                        if ((ang < 0x40 || ang > 0xC0) && (cur < 0x40 || cur > 0xC0)) {
                            wrapped = 1;
                            int d = iabs((int8_t)ang - (int8_t)cur);
                            if (d > 4 && iabs(ang - cur + 0x100) > 4 && iabs(ang - cur - 0x100) > 4)
                                W16(ORB_ANG(i), (uint32_t)(cur + ((int8_t)cur < (int8_t)ang ? 4 : -4)));
                            else { OR16(e + 0x10, 0x20); W16(ORB_ANG(i), (uint32_t)ang); }
                        }
                        if (!wrapped) {
                            int d = iabs(ang - cur);
                            if (d < 5) { OR16(e + 0x10, 0x20); W16(ORB_ANG(i), (uint32_t)ang); }
                            else if (d < 0x80) W16(ORB_ANG(i), (uint32_t)(cur + (ang < cur ? -4 : 4)));
                            else W16(ORB_ANG(i), (uint32_t)(cur + (ang < cur ? 4 : -4)));
                        }
                    } else {                                    /* locked: drift */
                        int32_t v = RS32(ORB_DX(i)) + FixMul(0x3000, Cos8(RS16(ORB_ANG(i))));
                        if (v > 0x3C000) v = 0x3C000; else if (v < -0x3C000) v = -0x3C000;
                        W32(ORB_DX(i), (uint32_t)v);
                        v = RS32(ORB_DY(i)) + FixMul(0x3000, Sin8(RS16(ORB_ANG(i))));
                        if (v > 0x3C000) v = 0x3C000; else if (v < -0x3C000) v = -0x3C000;
                        W32(ORB_DY(i), (uint32_t)v);
                    }
                } else {                                        /* waiting, then aim at Sho */
                    W16(e + 6, R16(e + 6) - 1);
                    if (RS16(e + 6) == 0) {
                        if (iabs(RS32(e + 8) - PX) < 0x4C0000 && iabs(RS32(e + 0xC) - PY + 0x200000) < 0x4C0000)
                            W16(e + 6, R16(e + 6) + 1);
                        else {
                            AND16(e + 0x10, 0xFFEF);
                            W32(ORB_TX(i), (uint32_t)PX);
                            W32(ORB_TY(i), (uint32_t)(PY - 0x200000));
                        }
                    }
                }
                W32(e + 0x18, (uint32_t)(FixMul(0x3C000, Cos8(RS16(ORB_ANG(i)))) + RS32(ORB_DX(i))));
                W32(e + 0x1C, (uint32_t)(FixMul(0x3C000, Sin8(RS16(ORB_ANG(i)))) + RS32(ORB_DY(i))));
                W32(e + 8, (uint32_t)(RS32(e + 8) + RS32(e + 0x18)));
                W32(e + 0xC, (uint32_t)(RS32(e + 0xC) - RS32(e + 0x1C)));
                OR16(e + 4, 0x2000);
                uint32_t who = 0;
                if (proj_touch(e, 2, &who, 2) != 0xFFFF) {      /* Sho's sword sends it back */
                    sub_06007CDE(0x104);
                    OR16(e + 0x10, 1);
                    W16(e + 4, R16(e + 4) ^ 0xA000);
                    W16(ORB_ANG(i), (uint32_t)Atan8(hi16(RS32(e + 8)), hi16(RS32(e + 0xC)),
                                                    hi16(RS32(BX(i))), hi16(RS32(BY(i)) - 0x100000)));
                    W32(e + 0x18, (uint32_t)FixMul(0x78000, Cos8(RS16(ORB_ANG(i)))));
                    W32(e + 0x1C, (uint32_t)FixMul(0x78000, Sin8(RS16(ORB_ANG(i)))));
                    W32(ORB_DX(i), 0); W32(ORB_DY(i), 0);
                    Obj_Spawn(3, RS32(HIT_POINT), RS32(HIT_POINT + 4), R16(who + 2) & 0x8000, 0);
                } else if (proj_touch(e, 2, &who, 1) != 0xFFFF) {   /* guarded */
                    sub_06007CDE(0x105);
                    Obj_Spawn(3, RS32(HIT_POINT), RS32(HIT_POINT + 4), R16(who + 2) & 0x8000, 0);
                    proj_anim(e, 9);
                    OR16(e + 0x10, 0xC);
                } else if (proj_touch(e, 2, &who, 0) != 0xFFFF) {   /* Sho hit */
                    sub_06007CDE(0x110);
                    Obj_Spawn(2, RS32(HIT_POINT), RS32(HIT_POINT + 4), 0, 0);
                    Player_KnockDir(HIT_POINT);
                    Player_TakeDamage(1);
                    proj_anim(e, 9);
                    OR16(e + 0x10, 0xC);
                }
            } else {                                            /* sent back: hits the boss */
                W32(e + 8, (uint32_t)(RS32(e + 8) + RS32(e + 0x18)));
                W32(e + 0xC, (uint32_t)(RS32(e + 0xC) - RS32(e + 0x1C)));
                AND16(e + 4, 0xDFFF);
                uint32_t who = 0;
                if (proj_touch(e, 2, &who, 0) != 0xFFFF && R16(who) == 0x20) {
                    sub_06007CDE(0x104);
                    Obj_Spawn(2, RS32(HIT_POINT), RS32(HIT_POINT + 4), 0, 0);
                    OR16(who + 2, 8);
                    W16(who + 0x16, (uint32_t)(RS16(who + 0x16) - 4));
                    proj_anim(e, 9);
                    OR16(e + 0x10, 0xC);
                }
            }
        } else if (R16(e + 0x20) & 0x8000) {                    /* burst: second part */
            AND16(e + 0x10, 0xFFFB);
            proj_anim(e, 10);
        }
        if ((R16(e + 0x10) & 2) &&
            (RS32(e + 8) < RS32(CAM_X) || RS32(CAM_X) + 0x1400000 < RS32(e + 8) ||
             RS32(e + 0xC) < RS32(CAM_Y) - 0xA0000 || RS32(CAM_Y) + 0xE00000 < RS32(e + 0xC))) {
            flag_and(i, 0xFFEF);
            W16(e, 0xFFFF);
            return;
        }
        if (!(R16(e + 0x10) & 2)) W16(e + 6, R16(e + 6) + 1);  /* growing */
    }
    if (R16(e) == 0xFFFF) return;
    uint16_t f = R16(e + 0x10);
    W16(SPR + 2, (uint32_t)hi16(RS32(e + 8) - RS32(CAM_X)));
    W16(SPR + 4, (uint32_t)hi16(RS32(e + 0xC) - RS32(CAM_Y)));
    W16(SPR + 0x0A, (uint32_t)(COLOR_BANK(Enemy_FindSlot(0x20)) + ((f & 1) ? 0x12 : 0xF)));
    W16(SPR + 0x10, 0x1E00);
    W32(SPR + 0x1C, (f & 2) ? 0x10000u : (uint32_t)R16(e + 6) * 0x888u);
    W32(SPR + 0x20, R32(SPR + 0x1C));
    if (!(f & 2)) W16(SPR + 0x14, 0);
    else {
        int32_t x = RS32(e + 8), y = RS32(e + 0xC);
        int a = Atan8(hi16(x), hi16(y), hi16(x + RS32(e + 0x18)), hi16(y - RS32(e + 0x1C)));
        if (R16(e + 0x38) == 8) a += 0x40;
        W16(SPR + 0x14, (uint32_t)a);
    }
    W32(SPR + 0x18, 0xFFFFFFFFu);
    W16(SPR + 0x0C, 0);
    W16(SPR + 0x0E, 0);
    Anim_Tick(e + 0x20);
    if (!(R16(e + 0x10) & 2) && R16(e + 0x38) != 8 && (R16(e + 0x20) & 0x8000)) {   /* full size: throw */
        proj_anim(e, 8);
        OR16(e + 0x10, 0x12);
        W16(e + 6, 0xF);
        sub_06007CDE(0x20D);
    }
}

/* Projectile 0x21 (0x06086A68): lightning on the ledge +0x14 (Sho's): the ledge glows
 * (+0x50 = 1) for 40 frames, then three 30/8/8-frame strikes that hurt Sho (1) while he
 * stands in them, a last 8-frame fade, and the ledge's glow ends (+0x50 = 3). */
void Proj_Boss42Bolt(uint32_t e)
{
    uint32_t l = slot_addr(RS16(e + 0x14));
    if (R16(e + 4) & 1) {
        proj_anim(e, 0xF);
        AND16(e + 4, 0xFFFE);
        W16(e + 0x10, 0);
        W16(e + 6, 0x28);
        W16(l + 0x50, 1);
    }
    if (!FROZEN) {
        if (R16(e + 0x10) == 0) {
            W16(e + 6, R16(e + 6) - 1);
            if (R16(e + 6) != 0) return;
            W16(e + 0x10, 1);
            W16(e + 6, 0x1E);
        }
        W32(e + 8, (uint32_t)(RS32(l + 8) + 0x300000));
        W32(e + 0xC, (uint32_t)(RS32(l + 0xC) + 0x100000));
        if (R16(e + 0x10) == 4 || R16(e + 0x10) == 0) {
            W16(e + 6, R16(e + 6) - 1);
            if (RS16(e + 6) == 0) { W16(l + 0x50, 3); W16(e, 0xFFFF); return; }
        } else {
            W16(e + 6, R16(e + 6) - 1);
            if (RS16(e + 6) == 0) { W16(e + 0x10, R16(e + 0x10) + 1); W16(e + 6, 8); }
        }
        uint32_t who = 0;
        if (R16(e + 0x10) < 4 && proj_touch(e, 2, &who, 0) != 0xFFFF) {
            Obj_Spawn(2, RS32(HIT_POINT), RS32(HIT_POINT + 4), 0, 0);
            Player_KnockDir(HIT_POINT);
            Player_TakeDamage(1);
        }
    }
    W16(SPR + 2, (uint32_t)hi16(RS32(e + 8) - RS32(CAM_X)));
    W16(SPR + 4, (uint32_t)hi16(RS32(e + 0xC) - RS32(CAM_Y)));
    W16(SPR + 0x0A, (uint32_t)(COLOR_BANK(Enemy_FindSlot(0x20)) + 0xE));
    W16(SPR + 0x10, 0x0200);
    W32(SPR + 0x18, 0xFFFFFFFFu);
    W16(SPR + 0x0C, 0);
    W16(SPR + 0x0E, (uint32_t)(R16(e + 0x10) - 1));
    Anim_Tick(e + 0x20);
}
