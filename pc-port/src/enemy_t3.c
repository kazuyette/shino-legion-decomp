/* Enemy type 0x0003 (stage 1-1): a leaping ninja. It stands facing Sho, then
 * (think timer +0x14) picks a move from the distance to Sho and the map around it:
 * walk towards / back off, hop on a ledge (jump height from the distance), drop
 * down to a lower floor, a high jump, or a leaping slash when Sho is close and
 * attacking. 3 HP; cut down = fall + explosion overlays, 200 points.
 * Ported from SHINOBI 0x0609E12C (loader) / 0x0609E258 (handler) and helpers.
 * Fields: +0x48 death timer, +0x4A jump height (air frames), +0x4E chosen move
 * (0 idle, 1 hop, 2 walk on, 3 back off, 4 leap attack, 5 hurt, 6 dying, 7 high
 * jump, 8 drop down), +0x50 repeat count, +0x52 Sho's attack kind on the last hit
 * (0xFFFF none). Actions +6: 0 stand, 1 walk, 2 crouch, 3 leap, 4 drop, 5 air,
 * 6 hurt, 7 cut down, 10 burning, 11 land, 12 walk back, 13/14/15 high jump. */
#include "enemy_int.h"
#include "anim.h"
#include "sprite.h"
#include "collision.h"
#include "player_ext.h"
#include "fx.h"

void Obj_Integrate(uint32_t o);      /* physics.c, 0x060452A4 */

static int32_t iabs(int32_t v) { return v < 0 ? -v : v; }
/* A.BIN 0x0600A55C (unsigned division) applied to the random generator */
static uint32_t rnd_div10(void) { return ((uint32_t)sub_0602DDD2() >> 8) / 10u; }
/* |a| >> 19 as the u16 the original keeps ("tiles" of 8 px) */
static uint16_t tiles(int32_t d) { return (uint16_t)((uint32_t)(d >> 3) >> 16); }

/* 0x0609E12C */
void Enemy3_Load(void)
{
    Enemy_RegisterChars(3, 0x060E6B88u);
    int16_t s = (int16_t)Enemy_FindSlot(3);
    uint16_t b = R16(0x060A9084u + 2u * (uint32_t)s);
    Pal_Copy((uint16_t)(b << 4), 16, 0x060E6D9Cu);
    Pal_Copy((uint16_t)((b + 1) << 4), 16, 0x060E6DBCu);
}

/* 0x0609E198 */
static void Ninja3_Init(void)
{
    int16_t s = (int16_t)Enemy_FindSlot(3);
    uint32_t o = O;
    W16(o + 6, 0);
    Anim_Start(o + 0x58, 0x060C4C9Cu, 0x060E6FC8u, CHAR_BASE(s), RS16(o + 6));
    AND16(o + 2, 0xFFFE);
    W16(o + 0x16, 3);
    for (uint32_t k = 0x48; k <= 0x56; k += 2) W16(o + k, 0);
    W16(o + 0x4E, 0);
    W16(o + 0x52, 0xFFFF);
}

/* 0x0609EEA0: face Sho; 1 if the facing changed. */
static int Ninja3_Face(void)
{
    uint32_t o = O;
    uint16_t old = R16(o + 2) & 0x8000;
    AND16(o + 2, 0x7FFF);
    if (RS32(PL + 8) < RS32(o + 8)) OR16(o + 2, 0x8000);
    return (R16(o + 2) & 0x8000) != old;
}

/* 0x0609F2C0: map attributes around the ninja (layer 0x060EA588).
 * 1 = ceiling type 5 above, 4 = wall left, 2 = wall right, 8 = floor type 2 below. */
static uint16_t Ninja3_Probe(void)
{
    uint32_t o = O;
    uint16_t r = 0;
    if ((Map_GetAttr(RS32(o + 8), RS32(o + 0xC) + (int32_t)0xFFC00000, LAYER_DESC) & 0xF0) == 0x50) r = 1;
    if ((Map_GetAttr(RS32(o + 8) + (int32_t)0xFFB00000, RS32(o + 0xC), LAYER_DESC) & 0xF0) == 0x10) r |= 4;
    if ((Map_GetAttr(RS32(o + 8) + 0x00500000, RS32(o + 0xC), LAYER_DESC) & 0xF0) == 0x10) r |= 2;
    if ((Map_GetAttr(RS32(o + 8), RS32(o + 0xC) + 0x00040000, LAYER_DESC) & 0xF0) == 0x20) r |= 8;
    return r;
}

/* 0x0609F22C: hop towards Sho, height from the horizontal distance. */
static void Ninja3_Hop(void)
{
    uint32_t o = O;
    int32_t d = RS32(PL + 8) - RS32(o + 8);
    if (d < 0) d = RS32(o + 8) - RS32(PL + 8);
    int32_t v = (int16_t)((uint32_t)(d + (int32_t)0xFFF00000) >> 16);
    v = v / 2;                                                       /* addc + shar */
    W16(o + 0x4A, (uint16_t)(int16_t)(v / 4 - 6));                  /* 0x060092BC */
    if (RS16(o + 0x4A) < 2) W16(o + 0x4A, 2);
    if (RS16(o + 0x4A) > 12) W16(o + 0x4A, 12);
    W16(o + 0x50, 0);
    W16(o + 0x4E, 1);
    W16(o + 6, 2);
}

/* 0x0609F188: when level with Sho and 15-19 tiles away, sometimes hop at once. */
static void Ninja3_MaybeHop(void)
{
    uint32_t o = O;
    int32_t d = RS32(o + 0xC) - RS32(PL + 0xC);
    if (d < 0) d = RS32(PL + 0xC) - RS32(o + 0xC);
    if (tiles(d) > 13) return;
    if (rnd_div10() & 1) return;
    d = RS32(o + 8) - RS32(PL + 8);
    if (d < 0) d = RS32(PL + 8) - RS32(o + 8);
    uint16_t dx = tiles(d);
    if (dx >= 15 && dx < 20) {
        Ninja3_Face();
        Ninja3_Hop();
    }
}

/* 0x0609ECA0: choose the next move (+0x4E). */
static void Ninja3_Decide(void)
{
    uint32_t o = O;
    int32_t d = RS32(o + 8) - RS32(PL + 8);
    if (d < 0) d = RS32(PL + 8) - RS32(o + 8);
    uint16_t dx = tiles(d);
    d = RS32(o + 0xC) - RS32(PL + 0xC);
    if (d < 0) d = RS32(PL + 0xC) - RS32(o + 0xC);
    uint16_t dy = tiles(d);
    uint16_t m = Ninja3_Probe();
    if (dx < 20 && RS32(o + 0xC) - RS32(PL + 0xC) < (int32_t)0xFFC00000 && (m & 8)) {
        W16(o + 0x4E, 8);                                          /* Sho well below: drop */
        return;
    }
    if (dy > 10) {
        W16(o + 0x4E, (dx < 20 && !(m & 1)) ? 7 : 0);
        return;
    }
    if (dx < 8) {
        W16(o + 0x4E, 4);
        if (R16(o + 2) & 0x8000) { if (m & 4) W16(o + 0x4E, 1); }
        else if (m & 2) W16(o + 0x4E, 1);
    } else if (dx >= 8 && dx < 15) {
        W16(o + 0x4E, 3);
    } else if (dx >= 15 && dx < 20) {
        W16(o + 0x4E, (rnd_div10() & 1) ? 1 : 3);
    } else if (dx >= 20 && dx < 40) {
        W16(o + 0x4E, 2);
        if (R16(o + 2) & 0x8000) { if (m & 2) W16(o + 0x4E, 0); }
        else if (m & 4) W16(o + 0x4E, 0);
    } else if (dx >= 40 && dx < 0x200) {
        W16(o + 0x4E, 0);
    }
}

/* 0x0609EEF0: walk at `v` (16.16, mirrored when facing left). 1 = turned round, stopped. */
static int Ninja3_Walk(int32_t v)
{
    uint32_t o = O;
    if ((uint16_t)Ninja3_Face()) {
        W16(o + 0x4E, 0);
        W16(o + 6, 0);
        W32(o + 0x30, 0);
        if (!(R16(o + 2) & 0x4000)) Ninja3_MaybeHop();
        return 1;
    }
    if (R16(o + 2) & 0x8000) v = -v;
    W32(o + 0x30, (uint32_t)v);
    if (R16(o + 0x5A) == 8) W32(o + 0x30, 0);
    if (!(R16(o + 2) & 0x4000)) Ninja3_MaybeHop();
    return 0;
}

/* 0x0609EF86: burning: flame overlay (anim 8) drifting away with the death timer. */
static void Ninja3_DrawFlame(void)
{
    int16_t s = (int16_t)Enemy_FindSlot(3);
    uint32_t o = O;
    Anim_Start(SCRATCH_ANIM, 0x060C4C9Cu, 0x060E6FC8u, CHAR_BASE(s), 8);
    int32_t h = RS16(o + 0x48);
    int32_t off = (int32_t)((uint32_t)h << 11) + (int32_t)((uint32_t)h << 12);
    int32_t x = (R16(o + 2) & 0x8000) ? RS32(o + 8) + off : RS32(o + 8) - off;
    int32_t y = RS32(o + 0xC) + off;
    W16(SPR + 2, (uint32_t)((x - RS32(CAM_X)) >> 16));
    W16(SPR + 4, (uint32_t)((y - RS32(CAM_Y)) >> 16));
    W16(SPR + 0x10, (R16(o + 2) & 0x8000) | 0x0200);
    W32(SPR + 0x18, 0xFFFFFFFFu);
    W16(SPR + 0x0A, (uint32_t)COLOR_BANK(s));
    W16(SPR + 0x0C, 0);
    W16(SPR + 0x0E, (uint16_t)(RS16(o + 0x48) >> 4));
    Anim_Tick(SCRATCH_ANIM);
}

/* 0x0609F0BC: burning: second overlay (anim 9) on the body. */
static void Ninja3_DrawBurn(void)
{
    int16_t s = (int16_t)Enemy_FindSlot(3);
    uint32_t o = O;
    Anim_Start(SCRATCH_ANIM2, 0x060C4C9Cu, 0x060E6FC8u, CHAR_BASE(s), 9);
    W16(SPR + 2, (uint32_t)((RS32(o + 8) - RS32(CAM_X)) >> 16));
    W16(SPR + 4, (uint32_t)((RS32(o + 0xC) - RS32(CAM_Y)) >> 16));
    W16(SPR + 0x10, (R16(o + 2) & 0x8000) | 0x0200);
    W32(SPR + 0x18, 0xFFFFFFFFu);
    W16(SPR + 0x0A, (uint32_t)COLOR_BANK(s));
    W16(SPR + 0x0C, 0);
    W16(SPR + 0x0E, (uint16_t)(RS16(o + 0x48) >> 4));
    Anim_Tick(SCRATCH_ANIM2);
}

/* 0x0609E33C-0x0609EB3E: combat + AI. 1 = the object was deleted (return at once). */
static int Ninja3_Think(void)
{
    uint32_t o = O;
    if (R16(o + 6) == 0) Ninja3_Face();
    if (!(R16(PL + 2) & 8) &&
        !(R16(o + 6) == 5 && iabs(RS32(PL + 8) - RS32(o + 8)) < 0x00180000)) {
        if ((int16_t)Enemy_AttackPlayer(1, 0, 0xC000) == 3) sub_06007CDE(0x10C);
    }
    if (!(R16(o + 2) & 8)) {
        W16(o + 0x52, 0xFFFF);
        if ((int16_t)Enemy_CheckPlayerHits(0xC000) == 2) W16(o + 0x52, (uint16_t)Player_AttackKind());
    }
    if (RS16(o + 0x4E) != 5 && RS16(o + 0x4E) != 6 && (R16(o + 2) & 8)) {   /* hit */
        W16(o + 6, 6);
        W16(o + 0x4E, 5);
        W32(o + 0x30, 0);
        W16(o + 0x50, 0);
        W32(o + 0x34, 0x00030000u);
        AND16(o + 4, 0xFFFE);
        if (RS16(o + 0x16) > 0) sub_06007CDE(0x206);
    }
    if (R16(o + 6) == 0) {                                             /* standing */
        AND16(o + 4, 0xFFFE);
        if (R16(o + 0x14) == 0) {
            Ninja3_Decide();
            int16_t m = RS16(o + 0x4E);
            if (m == 1 || m == 2 || m == 3 || m == 4 || m == 7 || m == 8) W16(o + 0x14, 8);
        } else {
            W16(o + 0x14, R16(o + 0x14) - 1);
            if (R16(o + 0x14) == 0) {
                switch (RS16(o + 0x4E)) {
                case 1:
                    if (Ninja3_Probe() & 1) W16(o + 6, 0); else Ninja3_Hop();
                    break;
                case 2: W16(o + 0x50, 0); W16(o + 0x4E, 2); W16(o + 6, 1); break;
                case 3: W16(o + 0x50, 0); W16(o + 0x4E, 3); W16(o + 6, 12); break;
                case 4:
                    W16(o + 0x48, 0); W16(o + 0x50, 0); W16(o + 0x4E, 4); W16(o + 6, 3);
                    W32(o + 0x34, 0x00040000u);
                    sub_06007CDE(0x126);
                    break;
                case 7: W16(o + 0x50, 0); W16(o + 0x4E, 7); W16(o + 6, 14); break;
                case 8:
                    W16(o + 0x50, 0); W16(o + 0x4E, 8); W16(o + 6, 4);
                    W32(o + 0xC, (uint32_t)(RS32(o + 0xC) + 0x00100000));
                    W32(o + 0x34, 0xFFFE0000u);
                    return 0;
                }
            }
        }
    }
    /* 0x0609E5CC */
    if ((R16(o + 2) & 0x4000) && (R16(o + 6) == 1 || R16(o + 6) == 12)) {   /* walked off a ledge */
        W16(o + 0x4E, 8);
        W16(o + 6, 4);
        W32(o + 0x30, 0);
    }
    if (RS16(o + 0x4E) == 8 && R16(o + 6) == 4 && (R16(o + 0x58) & 1) && (R16(o + 2) & 0x10)) {
        W16(o + 0x5A, R16(o + 0x5A) + 1);
        AND16(o + 0x58, 0xFFFE);
    }
    if (RS16(o + 0x4E) == 7) {                                         /* high jump */
        if (R16(o + 6) == 14) {
            if ((R16(o + 0x58) & 3) == 1) {
                W32(o + 0x34, 0x000A0000u);
                sub_06007CDE(0x126);
                AND16(o + 0x58, 0xFFFE);
            }
            if ((R16(o + 0x58) & 3) == 2) {
                W16(o + 6, 15);
                AND16(o + 0x58, 0xFFFD);
                return 0;
            }
        }
        if (R16(o + 6) == 15) {
            W32(o + 0x30, 0x8000u);
            if (R16(o + 2) & 0x8000) W32(o + 0x30, 0xFFFF8000u);
            if (RS32(o + 0x34) < 0) W16(o + 6, 13);
            W16(o + 0x48, R16(o + 0x48) + 1);
        }
        if (R16(o + 6) == 13 && (R16(o + 0x58) & 1) && (R16(o + 2) & 0x10)) {
            W16(o + 0x5A, R16(o + 0x5A) + 1);
            AND16(o + 0x58, 0xFFFE);
            W32(o + 0x30, 0);
        }
    }
    if (R16(o + 6) == 0 && iabs(RS32(o + 8) - RS32(PL + 8)) < 0x00780000 &&
        iabs(RS32(o + 0xC) - RS32(PL + 0xC)) < 0x00500000 && (int16_t)Player_AttackKind() != 6) {
        W16(o + 0x48, 0); W16(o + 0x50, 0); W16(o + 0x4E, 4); W16(o + 6, 3);   /* counter-leap */
        W32(o + 0x34, 0x00040000u);
        sub_06007CDE(0x126);
        return 0;
    }
    if (RS16(o + 0x4E) == 1) {                                         /* hop */
        if (R16(o + 6) == 2 && (R16(o + 0x58) & 1)) {
            W32(o + 0x34, 0x00020000u);
            AND16(o + 0x58, 0xFFFE);
            sub_06007CDE(0x126);
            OR16(o + 4, 1);
        }
        if (R16(o + 4) & 1) {
            if ((int32_t)R16(o + 0x1A) >= RS16(o + 0x4A)) { W16(o + 6, 5); AND16(o + 4, 0xFFFE); }
            else W32(o + 0x34, 0x00020000u);
        }
        if (R16(o + 6) == 5 && (R16(o + 0x58) & 1) && (R16(o + 2) & 0x10)) {
            AND16(o + 0x58, 0xFFFE);
            W16(o + 6, 11);
            W32(o + 0x30, 0);
            AND16(o + 4, 0xFFFE);
            return 0;
        }
        if (R16(o + 6) == 11) {
            W32(o + 0x30, 0x4000u);
            if (R16(o + 2) & 0x8000) W32(o + 0x30, 0xFFFFC000u);
        }
        if (R16(o + 4) & 1) {
            W32(o + 0x30, 0x00060000u);
            if (R16(o + 2) & 0x8000) W32(o + 0x30, 0xFFFA0000u);
        }
    }
    if (RS16(o + 0x4E) == 2 && R16(o + 6) == 1 && Ninja3_Walk(0x00014000)) return 0;
    if (RS16(o + 0x4E) == 3 && R16(o + 6) == 12 && Ninja3_Walk((int32_t)0xFFFEC000)) return 0;
    if (RS16(o + 0x4E) == 4 && R16(o + 6) == 3) {                       /* leap attack */
        if (R16(o + 0x58) & 1) {
            W32(o + 0x30, 0xFFFC8000u);
            if (R16(o + 2) & 0x8000) W32(o + 0x30, 0x00038000u);
        }
        if (R16(o + 2) & 0x10) {
            W16(o + 0x5A, R16(o + 0x5A) + 1);
            AND16(o + 0x58, 0xFFFE);
            W32(o + 0x30, 0);
        }
    }
    if (R16(o + 6) == 6 && (R16(o + 2) & 0x4000)) {                      /* knocked back */
        W32(o + 0x30, 0xFFFF0000u);
        if (R16(o + 2) & 0x8000) W32(o + 0x30, 0x00010000u);
    }
    if (R16(o + 6) == 10) {                                             /* burning */
        if (RS16(o + 0x48) >= 63) {
            Enemy_DeathBurst(o);
            Score_Add(200);
            W16(o, 0xFFFF);
            return 1;
        }
        W16(o + 0x48, R16(o + 0x48) + 1);
    }
    if (!(R16(o + 0x58) & 0x8000)) return 0;
    /* animation finished */
    W32(o + 0x30, 0);
    W16(o + 0x14, 0);
    AND16(o + 0x58, 0x7FFF);
    if (RS16(o + 0x4E) == 6) {
        Enemy_DeathBurst(o);
        Score_Add(200);
        W16(o, 0xFFFF);
        return 1;
    }
    if (R16(o + 6) == 6) {
        if (R16(o + 2) & 0x4000) { W16(o + 0x22, 0); return 0; }       /* still in the air */
        if (RS16(o + 0x16) <= 0) {                                      /* dead */
            W16(o + 0x48, 0);
            W16(o + 0x4E, 6);
            sub_06007CDE(0x206);
            int16_t k = RS16(o + 0x52);
            W16(o + 6, (k == 0 || k == 1 || k == 2 || k == 3) ? 10 : 7);
            return 0;
        }
        AND16(o + 2, 0xFFF7);
    }
    if (RS16(o + 0x50) == 0) {
        W16(o + 6, 0);
        W16(o + 0x4E, 0);
        Ninja3_MaybeHop();
    } else {
        W16(o + 0x22, 0);
        W16(o + 0x50, R16(o + 0x50) - 1);
    }
    return 0;
}

/* 0x0609E258 */
void EnemyHandler_0003(void)
{
    int16_t s = (int16_t)Enemy_FindSlot(3);
    uint32_t o = O;
    if (R16(o + 2) & 1) Ninja3_Init();
    int32_t d = RS32(o + 8) - RS32(CAM_X) + (int32_t)0xFF600000;
    if (d < 0) d = RS32(CAM_X) - RS32(o + 8) + 0x00A00000;
    if (d > 0x01000000 && !(R16(o + 2) & 8)) return;                   /* off screen: asleep */
    d = RS32(o + 0xC) - RS32(CAM_Y) + (int32_t)0xFF600000;
    if (d < 0) d = RS32(CAM_Y) - RS32(o + 0xC) + 0x00A00000;
    if (d > 0x01000000 && !(R16(o + 2) & 8)) return;
    if (!FROZEN) {
        if (Ninja3_Think()) return;
        /* 0x0609EB40 */
        Obj_Integrate(o);
        if (RS32(0x060EA618u) - RS32(o + 8) + 0x00200000 > 0) W32(o + 8, (uint32_t)(RS32(0x060EA618u) + 0x00200000));
        if (RS32(0x060EA620u) - RS32(o + 8) + (int32_t)0xFFE00000 < 0) W32(o + 8, (uint32_t)(RS32(0x060EA620u) + (int32_t)0xFFE00000));
        Obj_CollideMap(o, 3, 6);
        Enemy_AnimSwitch();
    }
    /* 0x0609EB8E */
    if (R16(o + 6) == 10) { Ninja3_DrawFlame(); Ninja3_DrawBurn(); }
    W16(SPR + 0x0E, 0);
    if (R16(o + 6) == 7) W16(SPR + 0x0E, R16(o + 0x58) & 3);
    if (R16(o + 6) == 10) W16(SPR + 0x0E, (uint16_t)(RS16(o + 0x48) >> 4));
    W16(SPR + 2, (uint32_t)((RS32(o + 8) - RS32(CAM_X)) >> 16));
    W16(SPR + 4, (uint32_t)((RS32(o + 0xC) - RS32(CAM_Y)) >> 16));
    if (R16(o + 6) == 10) W16(SPR + 4, R16(SPR + 4) - 37);
    W16(SPR + 0x10, R16(o + 2) & 0x8000);
    W16(SPR + 0x0A, R16(o + 6) == 10 ? (uint16_t)(R16(0x060A9084u + 2u * (uint32_t)s) + 1) : (uint16_t)COLOR_BANK(s));
    W16(SPR + 0x0C, 0);
    W32(SPR + 0x18, 0);
    if (R16(o) != 0xFFFF) Anim_Tick(o + 0x58);
}
