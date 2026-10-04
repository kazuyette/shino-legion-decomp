/* Enemy type 0x000A (stage 8-1): stone giant. Loader 0x060A1B2C, handler 0x060A1CC8,
 * init 0x060A1BF0, decision 0x060A25AA, face Sho 0x060A266A, cut-down overlays
 * 0x060A26BA (anim 7, sliding off) / 0x060A27DC (anim 8, legs).
 * 5 HP, 800 points. It stands where it was placed (the sprite is drawn at its home
 * position +0x1E/+0x20) and decides from the distance (0x060A25AA, 8-px units):
 * |dy| < 5 and |dx| < 12 -> mode 2 (sweep, act 4: rubble at its feet), |dx| < 40 ->
 * mode 1 (stomp: act 1 -> 2 (x rand % 4) -> 3, the stomp drops 2-3 rocks 0x1048 from the
 * ceiling 64 px left of it and throws rubble 88 px ahead, shaking the scenery
 * (C+0x20 0x060EA638 |= 0x80) until the anim posts event 2), else mode 0 (idle).
 * Hurt: act 5 (mode 3) -> act 0xB/0xA/0xC (mode 5: 3 more stomps, flashing banks +2..+5);
 * dead: act 9 (cut, sword kinds 0-3: two overlays, 30 frames) or 6 (blown up), then
 * Enemy_DeathBurst + 800.
 * Fields: +0x14 wait, +0x48 counter, +0x4E mode, +0x50 repeats, +0x52 attack kind that hurt it.
 * Original quirk kept: the sweep's rubble (0x060A1FEA) is spawned at x + r15 (the stack
 * pointer, 0x0600xxxx) instead of x, i.e. ~1536 px to the right of the giant. */
#include "enemy_more.h"
#include "anim.h"
#include "sprite.h"
#include "collision.h"
#include "player_ext.h"
#include "objhandlers_f.h"

#define TA_DEFS 0x060C6288u
#define TA_SCR  0x060E95D8u
#define SHAKE   0x060EA638u                /* stage control C+0x20, bit 0x80 = shake */
#define ORIG_SP 0x06001F80                 /* approximate r15 inside the handler (see above) */

static uint32_t rnd_div(uint32_t d) { return ((uint32_t)sub_0602DDD2() >> 8) / d; }   /* A.BIN 0x0600A55C */

void EnemyA_Load(void)                                      /* 0x060A1B2C */
{
    Enemy_RegisterChars(10, 0x060E920Cu);
    int16_t s = (int16_t)Enemy_FindSlot(10);
    uint16_t b = R16(0x060A9084u + 2u * (uint32_t)s);
    Pal_Copy((b & 0xFFF) << 4, 16, 0x060E93B8u);
    for (uint32_t k = 1; k < 6; k++) Pal_Copy((uint16_t)(b + k) * 16, 16, 0x060E93B8u + k * 0x20);
}

static void GiantA_Init(void)                               /* 0x060A1BF0 */
{
    uint32_t o = O;
    W16(o + 6, 0);
    Anim_Start(o + 0x58, TA_DEFS, TA_SCR, CHAR_BASE(SLOT), RS16(o + 6));
    AND16(o + 2, 0xFFFE);
    W16(o + 0x16, 5);
    for (uint32_t k = 0x48; k <= 0x56; k += 2) W16(o + k, 0);
    W16(o + 0x52, 0xFFFF);
    W16(o + 0x1E, (uint16_t)(R32(o + 8) >> 16));
    W16(o + 0x20, (uint16_t)(R32(o + 0xC) >> 16));
}

static void GiantA_Decide(void)                             /* 0x060A25AA */
{
    uint32_t o = O;
    uint16_t dx = (uint16_t)(EM_Abs(RS32(o + 8) - RS32(PL + 8)) >> 19);
    uint16_t dy = (uint16_t)(EM_Abs(RS32(o + 0xC) - RS32(PL + 0xC)) >> 19);
    if (dy < 5 && dx < 12) { W16(o + 0x4E, 2); return; }
    if (dx < 0x28) { W16(o + 0x4E, 1); return; }
    if (dx < 0x200) W16(o + 0x4E, 0);
}

static int GiantA_Face(void)                                /* 0x060A266A */
{
    uint32_t o = O;
    uint16_t old = R16(o + 2);
    AND16(o + 2, 0x7FFF);
    if (RS32(PL + 8) < RS32(o + 8)) OR16(o + 2, 0x8000);
    return (R16(o + 2) & 0x8000) != (old & 0x8000);
}

/* the stomp (mode 1 act 3, mode 5 act 10): event 1 = impact, event 2 = shaking over */
static void GiantA_Stomp(void)
{
    uint32_t o = O;
    if ((R16(o + 0x58) & 3) == 1) {
        sub_06007CDE(0x217);
        sub_06007CDE(0x10E);
        ObjF_RockfallSpawn(RS32(o + 8), RS32(0x060EA61Cu) + (int32_t)0xFFC00000);
        int32_t d = (R16(o + 2) & 0x8000) ? (int32_t)0xFFA80000 : 0x00580000;
        ObjF_Rubble2Spawn(RS32(o + 8) + d, RS32(o + 0xC), 2);
        OR16(o + 4, 1);
        AND16(o + 0x58, 0xFFFE);
    }
    if (R16(o + 4) & 1) OR16(SHAKE, 0x80);
    if ((R16(o + 0x58) & 3) == 2) {
        AND16(o + 4, 0xFFFE);
        AND16(SHAKE, 0xFF7F);
        AND16(o + 0x58, 0xFFFD);
    }
}

/* 0x060A26BA / 0x060A27DC: the two halves of the cut-down giant */
static void GiantA_CutDraw(int16_t anim, int top)
{
    uint32_t o = O;
    int16_t s = (int16_t)Enemy_FindSlot(10);
    Anim_Start(SCRATCH_ANIM, TA_DEFS, TA_SCR, CHAR_BASE(s), anim);
    if (top) {
        int32_t d = RS16(o + 0x48) * 0x4000;
        int32_t x = (R16(o + 2) & 0x8000) ? RS32(o + 8) + d : RS32(o + 8) - d;
        W16(SPR + 2, (uint32_t)((x - RS32(CAM_X)) >> 16));
        W16(SPR + 4, (uint32_t)((RS32(o + 0xC) + d - RS32(CAM_Y)) >> 16));
    } else {
        W16(SPR + 2, (uint16_t)(RS16(o + 0x1E) - (int16_t)(R32(CAM_X) >> 16)));
        W16(SPR + 4, (uint16_t)(RS16(o + 0x20) - (int16_t)(R32(CAM_Y) >> 16)));
    }
    W16(SPR + 0x10, (R16(o + 2) & 0x8000) | 0x200);
    W32(SPR + 0x18, 0xFFFFFFFFu);
    W16(SPR + 0x0A, (uint16_t)COLOR_BANK(s));
    W16(SPR + 0x0C, 0);
    W16(SPR + 0x0E, (uint16_t)((RS16(o + 0x48) >> 3) & 3));
    Anim_Tick(SCRATCH_ANIM);
}

static void GiantA_Die(uint32_t o)
{
    Enemy_DeathBurst(o);
    Score_Add(800);                                                 /* 0x06062B38 */
    W16(o, 0xFFFF);
}

void EnemyHandler_000A(void)                                /* 0x060A1CC8 */
{
    uint32_t o = O;
    int16_t s = (int16_t)Enemy_FindSlot(10);
    if (R16(o + 2) & 1) GiantA_Init();
    int32_t d = RS32(o + 8) - RS32(CAM_X) + (int32_t)0xFF600000;
    if (d < 0) d = RS32(CAM_X) - RS32(o + 8) + 0x00A00000;
    if (d > 0x01000000) {
        if (!FROZEN && (R16(o + 4) & 1)) AND16(SHAKE, 0xFF7F);
        if (!(R16(o + 2) & 8)) return;
    }
    d = RS32(o + 0xC) - RS32(CAM_Y) + (int32_t)0xFF600000;
    if (d < 0) d = RS32(CAM_Y) - RS32(o + 0xC) + 0x00A00000;
    if (d > 0x01000000) {
        if (!FROZEN && (R16(o + 4) & 1)) AND16(SHAKE, 0xFF7F);
        if (!(R16(o + 2) & 8)) return;
    }
    if (!FROZEN) {
        if (R16(o + 6) == 0 || RS16(o + 0x4E) == 5) GiantA_Face();
        if (!(R16(PL + 2) & 8) && (int16_t)Enemy_AttackPlayer(2, 0, 0) == 3) sub_06007CDE(0x10C);
        if (!(R16(o + 2) & 8)) {
            W16(o + 0x52, 0xFFFF);
            if ((int16_t)Enemy_CheckPlayerHits(0xC000) == 2) W16(o + 0x52, (uint16_t)Player_AttackKind());
        }
        if (RS16(o + 0x4E) != 4 && RS16(o + 0x4E) != 3 && (R16(o + 2) & 8)) {   /* hurt */
            W16(o + 6, 5);
            W16(o + 0x4E, 3);
            W32(o + 0x30, 0);
            W16(o + 0x50, 0);
            if (RS16(o + 0x16) > 0) sub_06007CDE(0x208);
            AND16(SHAKE, 0xFF7F);
        }
        if (R16(o + 6) == 0) {
            if (R16(o + 0x14) == 0) {
                GiantA_Decide();
                if (RS16(o + 0x4E) == 1) W16(o + 0x14, (uint16_t)((rnd_div(10) & 3) * 8 + 0x10));
                else if (RS16(o + 0x4E) == 2) W16(o + 0x14, (uint16_t)((rnd_div(10) & 3) * 8 + 8));
            } else {
                W16(o + 0x14, R16(o + 0x14) - 1);
                if (R16(o + 0x14) == 0) {
                    if (RS16(o + 0x4E) == 1) { W16(o + 0x50, 0); W16(o + 0x4E, 1); W16(o + 6, 1); }
                    else if (RS16(o + 0x4E) == 2) { W16(o + 0x50, 0); W16(o + 0x4E, 2); W16(o + 6, 4); }
                }
            }
        }
        if (RS16(o + 0x4E) == 2 && R16(o + 6) == 4 && (R16(o + 0x58) & 3) == 1) {   /* sweep */
            sub_06007CDE(0x217);
            ObjF_Rubble2Spawn(RS32(o + 8) + ORIG_SP, RS32(o + 0xC), 2);   /* sic: x + r15 */
            AND16(o + 0x58, 0xFFFE);
        }
        if (RS16(o + 0x4E) == 1 && R16(o + 6) == 3) GiantA_Stomp();
        if (R16(o + 6) == 9) {
            if (RS16(o + 0x48) > 0x1E) { GiantA_Die(o); return; }
            W16(o + 0x48, R16(o + 0x48) + 1);
        }
        if (RS16(o + 0x4E) == 5) {
            if (R16(o + 6) == 0xB && RS16(o + 0x48) < 0xF) W16(o + 0x48, R16(o + 0x48) + 1);
            if (R16(o + 6) == 0xC && RS16(o + 0x48) > 0) W16(o + 0x48, R16(o + 0x48) - 1);
            if (R16(o + 6) == 10) GiantA_Stomp();
        }
        if (R16(o + 0x58) & 0x8000) {                               /* anim over */
            W32(o + 0x30, 0);
            W16(o + 0x14, 0);
            AND16(o + 0x58, 0x7FFF);
            int16_t m = RS16(o + 0x4E), a = RS16(o + 6);
            if (m == 4) { GiantA_Die(o); return; }
            if (m == 1 && a == 1) { W16(o + 0x50, (uint16_t)(rnd_div(10) & 3)); W16(o + 6, 2); }
            else if (m == 1 && a == 2 && R16(o + 0x50) == 0) W16(o + 6, 3);
            else if (m == 3 && a == 5) {
                if (RS16(o + 0x16) < 1) {
                    sub_06007CDE(0x208);
                    W16(o + 0x48, 0);
                    W16(o + 0x4E, 4);
                    int16_t k = RS16(o + 0x52);
                    W16(o + 6, (k == 0 || k == 1 || k == 2 || k == 3) ? 9 : 6);
                } else {
                    W16(o + 0x4E, 5);
                    W16(o + 0x50, 0);
                    W16(o + 6, 0xB);
                    AND16(o + 2, 0xFFF7);
                }
            } else if (m == 5) {
                if (a == 0xB) { W16(o + 6, 10); W16(o + 0x50, 3); }
                else if (a == 0xC) { W16(o + 6, 0); W16(o + 0x4E, 0); }
                else if (a == 10) {
                    if (R16(o + 0x50) == 0) W16(o + 6, 0xC);
                    else { W16(o + 0x22, 0); W16(o + 0x50, R16(o + 0x50) - 1); }
                }
            } else if (R16(o + 0x50) == 0) { W16(o + 6, 0); W16(o + 0x4E, 0); }
            else { W16(o + 0x22, 0); W16(o + 0x50, R16(o + 0x50) - 1); }
        }
        Obj_Integrate(o);
        Obj_CollideMap(o, 1, 1);
        Enemy_AnimSwitch();
    }
    if (R16(o + 6) == 9) { GiantA_CutDraw(7, 1); GiantA_CutDraw(8, 0); }
    W16(SPR + 0x0E, 0);
    W16(SPR + 2, (uint16_t)(RS16(o + 0x1E) - (int16_t)(R32(CAM_X) >> 16)));
    W16(SPR + 4, (uint16_t)(RS16(o + 0x20) - (int16_t)(R32(CAM_Y) >> 16)));
    W16(SPR + 0x10, (R16(o + 2) & 0x8000) | 0x200);
    if (R16(o + 6) == 9) {
        W16(SPR + 0x0A, (uint16_t)(COLOR_BANK(s) + 1));
        W16(SPR + 4, R16(SPR + 4) - 0x34);
    } else W16(SPR + 0x0A, (uint16_t)COLOR_BANK(s));
    if (RS16(o + 0x4E) == 5) W16(SPR + 0x0A, (uint16_t)(COLOR_BANK(s) + ((RS16(o + 0x48) >> 2) & 3) + 2));
    W16(SPR + 0x0C, 0);
    if (R16(o + 6) == 6) W16(SPR + 0x0E, R16(o + 0x58) & 3);
    if (R16(o + 6) == 9) W16(SPR + 0x0E, (uint16_t)((RS16(o + 0x48) >> 3) & 3));
    W32(SPR + 0x18, 0);
    if (R16(o) != 0xFFFF) {
        Anim_Tick(o + 0x58);
        AND16(o + 0x58, 0xBFFF);
    }
}
