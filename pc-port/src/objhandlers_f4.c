/* Stage objects of 6-2 (0x1038-0x103B), 8-1 (0x1045-0x1049), 9-1 (0x1035, 0x1036, 0x1050,
 * 0x104C-0x104F) and the stray ones 0x103C / 0x104B, ported from SHINOBI.PPB. Also the spawn
 * helpers the enemy / projectile code calls (exported in objhandlers_f.h). */
#include "objhandlers_f_int.h"
#include "collision.h"
#include <stdio.h>

#define DEFS62  0x060ABF62u
#define SCR62   0x060D28D8u
#define DEFS81  0x060AC40Au
#define SCR81   0x060D2A5Cu
#define DEFS91  0x060AC6ECu
#define SCR91   0x060D2B20u
#define WORLD_BOTTOM RS32(STAGE_C + 0xC)
#define C_FLAGS  (STAGE_C + 0x20)              /* u16 stage control flags (0x80: scenery shaking) */

/* A.BIN 0x06008154(slot, volume, rate): sequence slot volume fade. The sequencer volume
 * control is not ported (see director_title.c Snd_SetAllSeqVolume): logged once. */
static void Snd_SeqSlotVolume(int slot, int vol, int rate)
{
    static int once;
    if (!once++) printf("[snd] 06008154 seq slot %d volume %d rate %d (not ported)\n", slot, vol, rate);
}

/* A.BIN 0x0600A55C (unsigned division) on the random value >> 8 */
static uint32_t rnd_div(uint32_t d) { return f_rnd() / d; }

/* ======================================================= 6-2: 0x1038-0x103B */

/* 0x0605AF84: 8 fragments (0x1038 flags 1, table 0x060AC010 {angle, dx, dy, part}) around p. */
void Crystal_Burst(int32_t x, int32_t y)                     /* also the 6-2 boss (enemy_boss09.c) */
{
    SFX(0x20A);
    for (int16_t i = 0; i < 8; i++) {
        uint32_t t = 0x060AC010u + (uint32_t)i * 8;
        uint16_t s = (uint16_t)Obj_Register(0x1038, RS16(t + 2) * 0x10000 + x, RS16(t + 4) * 0x10000 + y, 1, 0xFFFF);
        if ((int16_t)s >= 0) {
            uint32_t p = f_slot((int16_t)s);
            W16(p + 0x48, R16(t));
            W16(p + 0x4A, (s & 0xF) << 4);
            W16(p + 0x4C, R16(t + 6));
        }
    }
}

/* ---- 0x1038 (0x0605ACF0): crystal (anim 2, invisible hit zone: the crystal itself is in the
 * map). The sword (attack kinds 4/5 excepted) bursts it into 8 fragments; it can be hit again
 * after 96 frames. Flags & 1: a fragment flying along +0x48 at 2 px/frame with a wobble,
 * spinning (+0x4A), part +0x4C, 63 frames (colour-calc steps every 16). */
void ObjHandler_1038(void)
{
    uint32_t o = CUR;
    if (R16(o + 2) & 1) {
        f_anim(o, DEFS62, SCR62, 2);
        AND16(o + 2, 0xFFFE);
        W16(o + 0x14, 0); W16(o + 0x1C, 0x8000); OR16(o + 0x12, 0x4000);
    }
    if (!FROZEN) {
        if (!(R16(o + 0x12) & 1)) {
            if (R16(o + 0x48) != 0) {
                if (R16(o + 0x14) == 0) { AND16(o + 2, 0xFFF7); W16(o + 0x48, 0); }
                else W16(o + 0x14, R16(o + 0x14) - 1);
            }
            if (R16(o + 0x48) == 0 && !(R16(o + 2) & 8) && (int16_t)Hitbox_Test(o, 0, PL, 2) != -1) {
                int16_t k = (int16_t)Player_AttackKind();
                if (k != 4 && k != 5) {
                    OR16(o + 2, 8);
                    W16(o + 0x14, 0x60);
                    Crystal_Burst(RS32(o + 8), RS32(o + 0xC));
                    W16(o + 0x48, 1);
                }
            }
        } else {
            W32(o + 8, (uint32_t)(RS32(o + 8) + FixMul(Cos8(RS16(o + 0x48)), 0x20000)));
            W32(o + 0xC, (uint32_t)(RS32(o + 0xC) - FixMul(Sin8(RS16(o + 0x48)), 0x20000)));
            W32(o + 0xC, (uint32_t)(RS32(o + 0xC) + FixMul(Sin8(RS16(o + 0x4A)), 0x16000)));
            W16(o + 0x4A, R16(o + 0x4A) + 2);
            W16(o + 0x14, R16(o + 0x14) + 1);
            if (R16(o + 0x14) > 0x3E) { W16(o, 0xFFFF); return; }
        }
    }
    f_spr_cam(o);
    f_spr(0x0600, R16(o + 0x14) >> 4, 0, 0, 0xFFFFFF7Fu);
    W16(SPR + 0x14, R16(o + 0x4A));
    W16(o + 0x5A, R16(o + 0x4C));
    if (!(R16(o + 0x12) & 1)) OR16(o + 0x58, 0x4000);
    Anim_Tick(o + 0x58);
}

/* 0x0605AA34: one piece of rubble (0x103B, part flags & 0xF) thrown up from (x, y): +0x30 =
 * +-(3..6)/16 px, +0x34 = -(8..15.75)/16 px; mirror & 0x8000; `kind` -> +0x48 (draw priority 2
 * when non zero) and +0x4A = (kind & 3) << 2. Also called by enemy code (0x0609DF20). */
int ObjF_RubbleSpawn(int32_t x, int32_t y, uint16_t part, uint16_t kind, uint16_t mirror)
{
    int16_t s = (int16_t)Obj_Register(0x103B, x, y, part & 0xF, 0xFFFF);
    if (s == -1) return -1;
    uint32_t p = f_slot(s);
    OR16(p + 2, mirror & 0x8000);
    W16(p + 0x4A, (uint32_t)((kind & 3) << 2));
    uint32_t v = rnd_div(10) & 3;
    W32(p + 0x30, (mirror & 0x8000) ? (uint32_t)(-0x3000 - (int32_t)v * 0x1000) : v * 0x1000 + 0x3000);
    v = rnd_div(10) & 0x1F;
    W32(p + 0x34, (uint32_t)(-0x80000 - (int32_t)v * 0x4000));
    W16(p + 0x48, kind);
    return s;
}

/* ---- 0x1039 (0x0605A224): breakable wall (anim 0, part flags & 3). Once something sets +0x48
 * (the 6-2 boss) it collapses into 3 rows of rubble (layout by flags & 3, sound 0x122 per row)
 * and disappears. */
void ObjHandler_1039(void)
{
    uint32_t o = CUR;
    if (R16(o + 2) & 1) {
        f_anim(o, DEFS62, SCR62, 0);
        W16(o + 0x5A, R16(o + 0x12) & 3);
        AND16(o + 2, 0xFFFE);
        OR16(o + 0x1C, 0x8000);
        W16(o + 0x48, 0);
        OR16(o + 0x12, 0x4000);
    }
    if (!FROZEN && !(R16(o + 4) & 1) && RS16(o + 0x48) != 0) {
        static const int8_t L0[8][2] = { { -40, 0 }, { -32, 1 }, { -16, 0 }, { -8, 0 }, { 8, 1 }, { 16, 0 }, { 32, 1 }, { 40, 0 } };
        static const int8_t L1[6][2] = { { -12, 1 }, { -8, 1 }, { -4, 0 }, { 4, 0 }, { 8, 0 }, { 12, 1 } };
        static const int8_t L2[6][2] = { { -8, 0 }, { -4, 1 }, { -2, 0 }, { 2, 1 }, { 4, 1 }, { 8, 0 } };
        uint16_t k = R16(o + 0x12) & 3;
        for (uint16_t r = 0; r < 3 && k <= 2; r++) {
            const int8_t (*L)[2] = k == 0 ? L0 : k == 1 ? L1 : L2;
            int n = k == 0 ? 8 : 6;
            int32_t dy = k == 2 ? (int32_t)r * -0x180000 : (int32_t)r * -0x100000;
            SFX(0x122);
            for (int i = 0; i < n; i++)
                ObjF_RubbleSpawn(RS32(o + 8) + L[i][0] * 0x10000, RS32(o + 0xC) + dy, (uint16_t)(r * 2 + L[i][1]), 0,
                                 L[i][0] < 0 ? 0x8000 : 0);
        }
        OR16(o + 4, 1);
        W16(o, 0xFFFF);
    }
    f_spr_cam(o);
    f_spr(0x0200, 0, 0, 0, 0xFFFFFF80u);
    if (RS16(o) != -1) Anim_Tick(o + 0x58);
}

/* 0x0605A688: a piece of debris (0x103A) for the enemy code (0x0609AFE4..): flags & 0xF
 * select the motion, `set` (& 3) the physics table 0x060D28E4 + 4 x 4 x set, `mirror` & 0x8000. */
int ObjF_DebrisSpawn(int32_t x, int32_t y, uint16_t flags, uint16_t set, uint16_t mirror)
{
    int16_t s = (int16_t)Obj_Register(0x103A, x, y, flags & 0xF, 0xFFFF);
    if (s == -1) return -1;
    uint32_t p = f_slot(s);
    OR16(p + 2, mirror & 0x8000);
    W16(p + 0x4A, (uint32_t)((set & 3) << 2));
    return s;
}

/* ---- 0x103A (0x0605A714): debris. flags & 0xF < 4: part (flags & 3) + 2, thrown up (random
 * sin x 5 px) with the accelerations of table 0x060D28E4[+0x4A] (x away from the mirror side),
 * spinning, blinking; else part (flags & 0xC) >> 3, falling straight (t << 11). 64 frames or
 * below the world. */
void ObjHandler_103A(void)
{
    uint32_t o = CUR;
    uint32_t t = 0x060D28E4u + (uint32_t)(RS16(o + 0x4A) * 4);
    int fly = (R16(o + 0x12) & 0xF) < 4;
    if (R16(o + 2) & 1) {
        f_anim(o, DEFS62, SCR62, 1);
        W16(o + 0x5A, fly ? (uint32_t)((R16(o + 0x12) & 3) + 2) : (uint32_t)((R16(o + 0x12) & 0xC) >> 3));
        AND16(o + 2, 0xFFFE);
        OR16(o + 0x1C, 0x8000);
        W16(o + 0x48, 0);
        W32(o + 0x34, R32(t + 4));
        W32(o + 0x34, (uint32_t)-FixMul(0x50000, Sin8((int)(f_rnd() & 0x3F))));
        OR16(o + 0x12, 0x4000);
    }
    if (!FROZEN) {
        if (fly) {
            if (!(R16(o + 2) & 0x8000)) W32(o + 0x30, R32(o + 0x30) - R32(t + 8));
            else                        W32(o + 0x30, R32(o + 0x30) + R32(t + 8));
            W32(o + 0x34, R32(o + 0x34) + R32(t + 0xC));
            W32(o + 8, R32(o + 8) + R32(o + 0x30));
            W32(o + 0xC, R32(o + 0xC) + R32(o + 0x34));
            if (RS16(o + 0x48) < 0x40) W16(o + 0x48, R16(o + 0x48) + 1); else W16(o, 0xFFFF);
            if (WORLD_BOTTOM < RS32(o + 0xC)) W16(o, 0xFFFF);
        } else {
            W32(o + 0x34, (uint32_t)R16(o + 0x14) << 11);
            W32(o + 0xC, R32(o + 0xC) + R32(o + 0x34));
            if (RS16(o + 0x48) < 0x40) W16(o + 0x48, R16(o + 0x48) + 1); else W16(o, 0xFFFF);
        }
        W16(o + 0x14, R16(o + 0x14) + 1);
    }
    f_spr_cam(o);
    f_spr((R16(o + 2) & 0x8000) | 0x600, 0, 0, 0, 0x85);
    int16_t a = fly ? (int16_t)(RS16(o + 0x14) * 4) : RS16(o + 0x14);
    W16(SPR + 0x14, (uint16_t)((R16(o + 2) & 0x8000) ? -a : a));
    if (RS16(o) != -1 && (!fly || (VBL & 1))) Anim_Tick(o + 0x58);
}

/* ---- 0x103B (0x0605AB42): rubble piece (part flags & 7): gravity 0x5000 up to 10 px/frame,
 * spinning, blinking, freed below the world. */
void ObjHandler_103B(void)
{
    uint32_t o = CUR;
    if (R16(o + 2) & 1) {
        f_anim(o, DEFS62, SCR62, 1);
        W16(o + 0x5A, R16(o + 0x12) & 7);
        AND16(o + 2, 0xFFFE);
        OR16(o + 0x1C, 0x8000);
        OR16(o + 0x12, 0x4000);
    }
    if (!FROZEN) {
        if (RS32(o + 0x34) < 0xA0000) W32(o + 0x34, R32(o + 0x34) + 0x5000);
        W32(o + 8, R32(o + 8) + R32(o + 0x30));
        W32(o + 0xC, R32(o + 0xC) + R32(o + 0x34));
        if (WORLD_BOTTOM < RS32(o + 0xC)) W16(o, 0xFFFF);
        W16(o + 0x14, R16(o + 0x14) + 1);
    }
    f_spr_cam(o);
    f_spr(0x0600, 0, 0, 0, RS16(o + 0x48) != 0 ? 2 : 0xFFFFFF80u);
    W16(SPR + 0x14, (R16(o + 0x14) & 0x3F) << 2);
    if (RS16(o) != -1 && (VBL & 1)) Anim_Tick(o + 0x58);
}

/* ======================================================= 8-1: 0x1045-0x1049 */

/* ---- 0x1045 (0x0605D234): scenery block (anim 0, part 1 with flags & 1: drawn in front). */
void ObjHandler_1045(void)
{
    uint32_t o = CUR;
    if (R16(o + 2) & 1) {
        f_anim(o, DEFS81, SCR81, 0);
        if (R16(o + 0x12) & 1) W16(o + 0x5A, 1);
        AND16(o + 2, 0xFFFE);
        OR16(o + 0x1C, 0x8000); OR16(o + 0x12, 0x4000);
    }
    W16(o + 0x16, 0x7FFF);
    AND16(o + 2, 0xFFF7);
    if (f_scr_dx(o) >= 0x1000001) return;
    f_spr_cam(o);
    f_spr(0x0200, 0, 0, 0, RS16(o + 0x5A) == 1 ? 0x82 : 0xFFFFFF80u);
    if (RS16(o) != -1) Anim_Tick(o + 0x58);
}

/* 0x0605DF3E: the type 0x24 enemies standing on `self` are moved by (dx, -dy); crushed ones die. */
static void carry_type24(uint32_t self, int32_t dx, int32_t dy)
{
    for (int16_t s = 0; s < 0xC0; s++) {
        uint32_t e = f_slot(s);
        uint16_t t = R16(e);
        if (t == 0xFFFF || t >= 0x1000) continue;
        if (Enemy_FindSlot((int16_t)t) == -1) continue;
        if (t != 0x24 || !Obj_CarryContact(self, e, 2, 8)) continue;
        Obj_Push(self, 0, 0);
        if (!(R16(e + 0x10) & 0x400)) {
            if (R16(e + 0x10) & 0x2000) {
                W32(e + 8, (uint32_t)(RS32(e + 8) + dx));
                W32(e + 0xC, (uint32_t)(RS32(e + 0xC) - dy));
            }
        } else { W16(e + 0x16, 0); OR16(e + 2, 8); }
    }
}

/* ---- 0x1046 (0x0605D3B0): crumbling ledge (anim 1, part 1 with flags & 1). Sword clangs;
 * with flags & 2, Sho standing on it makes it shake and fall (sound 0x10E, accelerating by
 * t/16 px up to 4 px/frame, Sho pushed down 4 px), freed 252 px below the world. */
void ObjHandler_1046(void)
{
    uint32_t o = CUR;
    if (R16(o + 2) & 1) {
        f_anim(o, DEFS81, SCR81, 1);
        if (R16(o + 0x12) & 1) W16(o + 0x5A, 1);
        AND16(o + 2, 0xFFFE);
        W16(o + 0x1C, 0x4000);
        OR16(o + 0x12, 0x4000);
    }
    if (!(f_scr_dx(o) < 0x2000001 || RS16(o + 0x48) != 0)) return;
    W16(o + 0x16, 0x7FFF);
    AND16(o + 2, 0xFFF7);
    if (!FROZEN) {
        int16_t c = 0;
        carry_type24(o, 0, 0);
        ObjF_SwordClang(o);
        if (RS16(o + 0x48) != -1) {
            c = (int16_t)Obj_PlayerContact(0);
            if (c == 2 && (R16(o + 0x12) & 2) && RS16(o + 0x48) == 0) { W16(o + 0x48, 1); W16(o + 0x4A, 0); }
        }
        if (RS16(o + 0x48) == 1) {
            if (RS16(o + 0x4A) == 0) { W16(o + 0x48, 2); SFX(0x10E); }
            else W16(o + 0x4A, R16(o + 0x4A) - 1);
        }
        if (RS16(o + 0x48) == 2) {
            if (WORLD_BOTTOM + 0xFC0000 < RS32(o + 0xC)) W16(o, 0xFFFF);
            else {
                if (RS32(o + 0x34) < 0x40000) W32(o + 0x34, (uint32_t)(RS16(o + 0x4A) << 12));
                if (c == 2) W32(PL + 0x3C, 0xFFFC0000u);
                W32(o + 0xC, R32(o + 0xC) + R32(o + 0x34));
                W16(o + 0x4A, R16(o + 0x4A) + 1);
            }
        }
        W16(o + 0x14, R16(o + 0x14) + 1);
    }
    int16_t sx = (int16_t)((uint32_t)(RS32(o + 8) - CX) >> 16);
    if (RS16(o + 0x48) != 0) sx = (int16_t)(sx + ((R16(o + 0x14) & 1) ? 2 : -2));
    W16(SPR + 2, (uint16_t)sx);
    W16(SPR + 4, (uint32_t)((RS32(o + 0xC) - CY) >> 16));
    f_spr(0x0200, 0, 0, 0, 0xFFFFFF80u);
    if (RS16(o) != -1) Anim_Tick(o + 0x58);
}

/* ---- 0x1047 (0x0605D668): breakable post (anim 2): the sword or any hit (+2 & 8) -> sound
 * 0x122, spark, Enemy_DeathBurst (explosion + item +0x18), gone. */
void ObjHandler_1047(void)
{
    uint32_t o = CUR;
    if (R16(o + 2) & 1) {
        f_anim(o, DEFS81, SCR81, 2);
        AND16(o + 2, 0xFFFE);
        OR16(o + 0x1C, 0x8000);
    }
    if (f_scr_dx(o) > 0x1000000) return;
    if (!FROZEN && ((R16(o + 2) & 8) || (int16_t)Hitbox_Test(o, 0, PL, 2) != -1)) {
        SFX(0x122);
        Obj_Spawn(2, RS32(HIT_POINT), RS32(HIT_POINT + 4), 0, 0);
        Enemy_DeathBurst(o);
        W16(o, 0xFFFF);
        return;
    }
    f_spr_cam(o);
    f_spr(0x0200, 0, 0, 0, 0xFFFFFF80u);
    if (RS16(o) != -1) Anim_Tick(o + 0x58);
}

/* 0x0605D838: 2 or 3 falling rocks (0x1048) every 64 px to the left of x (enemy code
 * 0x060A203C / 0x060A21AC), each with a random part and a delay of 0..28 frames. */
void ObjF_RockfallSpawn(int32_t x, int32_t y)
{
    SFX(0x213);
    uint16_t a = (uint16_t)rnd_div(7);
    uint16_t n = (uint16_t)((a + 2) & 7);
    if (n > 3) n = 3;
    if (n < 3) n = 2;
    for (uint16_t i = 0; i < n; i++) {
        x -= 0x400000;
        a = (uint16_t)((rnd_div(7) + a) & 3);
        int16_t s = (int16_t)Obj_Register(0x1048, x, y, a, 0xFFFF);
        if ((uint16_t)s != 0xFFFF) W16(f_slot(s) + 0x14, (rnd_div(6) & 7) << 2);
    }
}

/* 0x0605DBEC: 4 small rubble pieces (0x1049) from (x, y); part random (2 for kind 2). */
void ObjF_Rubble2Spawn(int32_t x, int32_t y, int16_t kind)
{
    SFX(0x122);
    uint32_t a = rnd_div(7) & 0xFFFF;
    for (uint16_t i = 0; i < 4; i++) {
        a = (rnd_div(7) + a) & 3;
        if (kind == 2) a = 2;
        uint16_t s = (uint16_t)Obj_Register(0x1049, x, y, (uint16_t)a, 0xFFFF);
        /* the original tests the zero-extended slot against -1, so a full table writes slot -1 */
        uint32_t p = f_slot((int16_t)s);
        W32(p + 0x34, (uint32_t)(-0x30000 - (int32_t)(rnd_div(7) & 0xF) * 0x4000));
        if (i & 1) OR16(p + 2, 0x8000);
    }
}

/* ---- 0x1048 (0x0605D928): falling rock (anim 3, part flags & 3) after its delay (+0x14);
 * shakes the scenery (C+0x20 |= 0x80) while alive. Breaks into rubble (0x0605DBEC) on the
 * sword (sound 0x122), on the map, or on Sho (1 damage); freed 50 px below the world. */
void ObjHandler_1048(void)
{
    uint32_t o = CUR;
    if (R16(o + 2) & 1) {
        f_anim(o, DEFS81, SCR81, 3);
        W16(o + 0x5A, R16(o + 0x12) & 3);
        AND16(o + 2, 0xFFFE);
        OR16(o + 0x12, 0x4000);
    }
    OR16(C_FLAGS, 0x80);
    if (!FROZEN) {
        if (RS16(o + 0x14) != 0) { W16(o + 0x14, R16(o + 0x14) - 1); return; }
        if (!(R16(o + 2) & 8) && (int16_t)Hitbox_Test(o, 0, PL, 2) != -1) {
            int16_t k = (int16_t)Player_AttackKind();
            if (k != 4 && k != 5) {
                SFX(0x122);
                Obj_Spawn(2, RS32(HIT_POINT), RS32(HIT_POINT + 4), 0, 0);
                ObjF_Rubble2Spawn(RS32(HIT_POINT), RS32(HIT_POINT + 4), RS16(o + 0x5A));
                W16(o, 0xFFFF);
                OR16(o + 2, 8);
                AND16(C_FLAGS, 0xFF7F);
            }
        }
        int32_t y = RS32(o + 0xC);
        if (Map_GetAttr(RS32(o + 8), y, LAYER_DESC) != 0xFFFF) ObjF_Rubble2Spawn(RS32(o + 8), y, RS16(o + 0x5A));
        else if (Map_GetAttr(RS32(o + 8), y - 0x80000, LAYER_DESC) != 0xFFFF) ObjF_Rubble2Spawn(RS32(o + 8), y - 0x80000, RS16(o + 0x5A));
        else if (ObjF_SwordHurtsPlayer(1)) ObjF_Rubble2Spawn(RS32(o + 8), RS32(o + 0xC), RS16(o + 0x5A));
        else if (RS32(o + 0xC) <= WORLD_BOTTOM + 0x320000) {
            if (RS32(o + 0x34) < 0xA0000) W32(o + 0x34, R32(o + 0x34) + 0x5000);
            W32(o + 0xC, R32(o + 0xC) + R32(o + 0x34));
            W16(o + 0x4A, R16(o + 0x4A) + 1);
            goto draw;
        }
        W16(o, 0xFFFF);
        AND16(C_FLAGS, 0xFF7F);
        return;
    }
draw:
    f_spr_cam(o);
    f_spr(0x0600, 0, 0, 0, 0x80);
    W16(SPR + 0x14, (uint32_t)(RS16(o + 0x4A) << 2));
    if (RS16(o) != -1) Anim_Tick(o + 0x58);
}

/* ---- 0x1049 (0x0605DCF8): rubble piece (anim 4, part flags & 3): drifts (0.5..0.69 px/frame
 * away from the mirror side), falls, bounces once (3 px) on the map, spins. */
void ObjHandler_1049(void)
{
    uint32_t o = CUR;
    if (R16(o + 2) & 1) {
        f_anim(o, DEFS81, SCR81, 4);
        W16(o + 0x5A, R16(o + 0x12) & 3);
        AND16(o + 2, 0xFFFE);
        OR16(o + 2, 8);
    }
    if (!FROZEN) {
        if (WORLD_BOTTOM + 0x320000 < RS32(o + 0xC)) { W16(o, 0xFFFF); return; }
        uint32_t v = rnd_div(7) & 3;
        W32(o + 0x30, (R16(o + 2) & 0x8000) ? v * 0x1000 - 0x8000 : v * 0x1000 + 0x8000);
        if (RS32(o + 0x34) < 0xA0000) W32(o + 0x34, R32(o + 0x34) + 0x5000);
        W32(o + 8, R32(o + 8) + R32(o + 0x30));
        W32(o + 0xC, R32(o + 0xC) + R32(o + 0x34));
        if (RS32(o + 0x34) > 0 && RS16(o + 0x48) == 0 && Map_GetAttr(RS32(o + 8), RS32(o + 0xC), LAYER_DESC) != 0xFFFF) {
            W32(o + 0x34, 0xFFFD0000u);
            W16(o + 0x48, 1);
            W16(o + 0x4C, 0);
        }
        if (RS16(o + 0x4A) != 0) W16(o + 0x4C, R16(o + 0x4C) + 1);
        W16(o + 0x4A, R16(o + 0x4A) + 1);
    }
    f_spr_cam(o);
    f_spr(0x0600, (uint16_t)(RS16(o + 0x4C) >> 4), 0, 0, 0x80);
    W16(SPR + 0x14, (uint32_t)(RS16(o + 0x4A) << 2));
    if (RS16(o) != -1) Anim_Tick(o + 0x58);
}

/* ======================================================= 9-1: 0x1035, 0x1036, 0x1050 */

/* ---- 0x1035 (0x0605E81C): sinking platform (anim 0). Sho's down-thrust (attack kind 5) on it,
 * or Sho crouching (+2 & 0x10) on it within 48 px, makes it sink up to 64 px (1 px/frame for
 * 32 frames, sound 0x211); after 160 frames it rises back 32 px at a time. Sword clangs;
 * Sho on top is carried. */
void ObjHandler_1035(void)
{
    uint32_t o = CUR;
    if (R16(o + 2) & 1) {
        f_anim(o, DEFS91, SCR91, 0);
        AND16(o + 2, 0xFFFE);
        W16(o + 0x14, 0); W16(o + 0x48, 0); W16(o + 0x4A, 0); W16(o + 0x4C, 1); W16(o + 0x4E, 0);
        W32(o + 0x34, 0);
        OR16(o + 0x12, 0x4000);
        W16(o + 0x1C, 0x4000);
    }
    if (f_scr_dx(o) >= 0x1800001) return;
    if (!FROZEN) {
        ObjF_SwordClang(o);
        int16_t c = (int16_t)Obj_PlayerContact(R16(o + 2) & 0x8000);
        W32(o + 0x34, 0);
        if (RS16(o + 0x4C) == 1) {
            if (!(R16(o + 2) & 8) && (int16_t)Hitbox_Test(o, 0, PL, 2) != -1 && (int16_t)Player_AttackKind() == 5) {
                W16(o + 0x48, 0x20); W16(o + 0x4C, 0);
            }
            if (!(R16(o + 2) & 8) && RS32(o + 8) - 0x300000 < PX && PX < RS32(o + 8) + 0x300000 && (R16(PL + 2) & 0x10)) {
                W16(o + 0x48, 0x20); W16(o + 0x4C, 0);
                if (RS16(o + 0x4A) < 0x40) SFX(0x211);
            }
        }
        int16_t s = RS16(o + 0x4C);
        if (s == 0) {
            if (RS16(o + 0x4A) < 0x40 && RS16(o + 0x48) != 0) {
                W16(o + 0x48, R16(o + 0x48) - 1);
                W16(o + 0x4A, R16(o + 0x4A) + 1);
                W32(o + 0x34, 0x10000);
            } else { W16(o + 0x4C, R16(o + 0x4C) + 1); W16(o + 0x4E, 0xA0); }
        } else if (s == 1) {
            if (RS16(o + 0x4E) == 0) {
                if (RS16(o + 0x4A) > 0) {
                    W16(o + 0x4C, R16(o + 0x4C) + 1);
                    W16(o + 0x50, 0x20);
                    if (RS16(o + 0x4A) > 0) SFX(0x211);
                }
            } else W16(o + 0x4E, R16(o + 0x4E) - 1);
        } else if (s == 2) {
            if (RS16(o + 0x4A) < 1 || RS16(o + 0x50) == 0) { W16(o + 0x4C, R16(o + 0x4C) - 1); W16(o + 0x4E, 0xA0); }
            else {
                W16(o + 0x4A, R16(o + 0x4A) - 1);
                W16(o + 0x50, R16(o + 0x50) - 1);
                W32(o + 0x34, 0xFFFF0000u);
            }
        }
        W32(o + 0xC, R32(o + 0xC) + R32(o + 0x34));
        if (c == 2) W32(PL + 0x3C, R32(PL + 0x3C) - R32(o + 0x34));
    }
    f_spr_cam(o);
    f_spr(0x0200, 0, 0, 0, 0x81);
    Anim_Tick(o + 0x58);
}

/* ---- 0x1036 (0x0605EB8C): breakable urn (anim 1): the sword -> sound 0x115, 6 explosion parts,
 * item +0x18, gone. */
void ObjHandler_1036(void)
{
    uint32_t o = CUR;
    if (R16(o + 2) & 1) {
        f_anim(o, DEFS91, SCR91, 1);
        AND16(o + 2, 0xFFFE);
        W16(o + 0x14, 0); W16(o + 0x1C, 0x8000);
        OR16(o + 0x12, 0x4000);
    }
    if (f_scr_dx(o) >= 0x1800001) return;
    if (!FROZEN && !(R16(o + 2) & 8) && (int16_t)Hitbox_Test(o, 0, PL, 2) != -1) {
        SFX(0x115);
        ObjF_BreakParts(o);
        Item_Drop(RS16(o + 0x18), RS32(o + 8), RS32(o + 0xC));
        W16(o, 0xFFFF);
        return;
    }
    f_spr_cam(o);
    f_spr(0x0200, 0, 0, 0, 0xFFFFFF80u);
    Anim_Tick(o + 0x58);
}

/* ---- 0x1050 (0x0605ED0C): scenery (anim 2) drawn near the screen. */
void ObjHandler_1050(void)
{
    uint32_t o = CUR;
    if (R16(o + 2) & 1) {
        f_anim(o, DEFS91, SCR91, 2);
        AND16(o + 2, 0xFFFE);
        W16(o + 0x14, 0); W16(o + 0x1C, 0x8000);
        OR16(o + 0x12, 0x4000);
    }
    if (f_scr_dx(o) >= 0x1800001) return;
    f_spr_cam(o);
    f_spr(0x0200, 0, 0, 0, 0xFFFFFF80u);
    Anim_Tick(o + 0x58);
}

/* ======================================================= 9-1 boss: 0x104C-0x104F */
#define DEFS9B  0x060BA5D0u
#define SCR9B   0x060E19D0u

/* 0x060842F8: boss (enemy type 0x1F) phase step when an orb is taken (boss code, ported here
 * because 0x104C drives it; state 0x060F81D2..0x060F81D6, tables 0x060BC338 / 0x060BC33E). */
void Boss1F_OrbTaken(uint32_t b)                           /* exported: also called by enemy_boss1f.c */
{
    int16_t hp = RS16(b + 0x16);
    if (hp > RS16(0x060F81D6u)) return;
    if (!(R16(b + 4) & 0x10) && RS16(0x060BC33Eu + 2u * (uint32_t)RS16(b + 0x4E)) != 0) return;
    W16(b + 0x4E, R16(b + 0x4E) + 1);
    AND16(b + 4, 0xFFEF);
    W16(0x060F81D4u, R16(0x060F81D6u));
    if (RS16(b + 0x4E) == 3 || RS16(b + 0x4E) == 5) W16(0x060F81D6u, (uint32_t)(hp - 1));
    else W16(0x060F81D6u, (uint32_t)(hp - RS16(0x060BC338u + 2u * (uint32_t)RS16(0x060252C4u))));
    OR16(b + 4, 8);
    if (RS16(0x060BC33Eu + 2u * (uint32_t)RS16(b + 0x4E)) != 0) W16(0x060F81D2u, R16(0x060F7AD4u));
}
/* 0x060843BE */
static void Boss1F_Notify(void)
{
    for (int16_t s = 0; s < 0xC0; s++)
        if (R16(f_slot(s)) == 0x1F) { OR16(f_slot(s) + 4, 0x10); Boss1F_OrbTaken(f_slot(s)); return; }
}

/* ---- 0x104C (0x0605F520): orb (anim 0x1C, chars 0x60, bank 0x59 + flags & 0x1F) of the 9-1
 * boss; 180 frames, blinking below 45. Touching it (sound 0x106) collects bit flags & 0x1F into
 * 0x060F22C4 (count 0x060F7AD4) and steps the boss; tints Sho (C+0x18.. from 0x060D2E4C /
 * 0x060D2E58 / 0x060D2E64) and rises for 15 frames; at 6 orbs the arena changes (NBG2 priority
 * 2, colour calc off, layer offsets, effects cleared, music faded, Sho faces the boss, magic
 * 6 active). +0x48 = 2: flies to the boss (0x060F7AD8/DC) at 6 px/frame (set by boss code). */
void ObjHandler_104C(void)
{
    uint32_t o = CUR;
    uint16_t blue = 0;
    if (R16(o + 2) & 1) {
        OR16(o + 0x1C, 0x8000);
        W16(o + 0x14, 0xB4);
        W16(o + 6, 0x1C);
        Anim_Start(o + 0x58, DEFS9B, SCR9B, 0x60, 0x1C);
        AND16(o + 2, 0xFFFE);
        W16(o + 0x48, 0);
    }
    if (RS16(0x060F7AD4u) == 6) { W16(o, 0xFFFF); return; }
    uint32_t k = R16(o + 0x12) & 0x1F;
    if (!FROZEN) {
        W16(o + 0x14, R16(o + 0x14) - 1);
        if (R16(o + 0x48) == 1) {
            if (R16(o + 0x14) == 0) {
                W16(0x060EA426u, 0xFFFF);
                W16(STAGE_C + 0x18, 0); W16(STAGE_C + 0x1A, 0); W16(STAGE_C + 0x1C, 0);
                W16(o, 0xFFFF);
                return;
            }
            W16(0x060EA426u, 0xFFFF);
            if (!(R16(o + 0x14) & 1)) {
                W16(STAGE_C + 0x18, R16(0x060D2E4Cu + 2 * k));
                W16(STAGE_C + 0x1A, R16(0x060D2E58u + 2 * k));
                blue = R16(0x060D2E64u + 2 * k);
            } else { W16(STAGE_C + 0x18, 0); W16(STAGE_C + 0x1A, 0); }
            W16(STAGE_C + 0x1C, blue);
            W32(o + 0xC, R32(o + 0xC) - 0x18000);
        } else if (R16(o + 0x48) == 2) {
            int a = Atan8((int16_t)(R32(o + 8) >> 16), (int16_t)(R32(o + 0xC) >> 16), (int16_t)(R32(0x060F7AD8u) >> 16),
                          (int16_t)((R32(0x060F7ADCu) - 0x320000) >> 16));
            W32(o + 8, (uint32_t)(RS32(o + 8) + FixMul(0x60000, Cos8(a))));
            W32(o + 0xC, (uint32_t)(RS32(o + 0xC) - FixMul(0x60000, Sin8(a))));
            if (f_abs(RS32(o + 8) - RS32(0x060F7AD8u)) < 0x200000 &&
                f_abs(RS32(o + 0xC) - RS32(0x060F7ADCu) + 0x320000) < 0x280000) { W16(o, 0xFFFF); return; }
        } else {
            if (R16(o + 0x14) == 0) { W16(o, 0xFFFF); return; }
            if ((int16_t)Hitbox_Test(o, 0, PL, 0) != -1) {
                SFX(0x106);
                uint16_t bit = 1;
                if (k != 0) bit = (uint16_t)(1u << k);
                if (!(bit & R16(0x060F22C4u))) {
                    W16(0x060F7AD4u, R16(0x060F7AD4u) + 1);
                    OR16(0x060F22C4u, bit);
                    Boss1F_Notify();
                    if (RS16(0x060F7AD4u) == 6) {
                        W16(o, 0xFFFF);
                        W16(STAGE_C + 0x2E, 2);
                        Scl_SetPriority(0x10, R16(STAGE_C + 0x2E) & 0xFF);
                        W32(0x0603B4ACu, R32(0x0603B4ACu) | (1u << 27));   /* 0x06009254(1, 0x0401): shadow dirty */
                        W16(0x0603B494u, R16(0x0603B494u) & 0xFF00);       /* colour calc ratios NBG off */
                        W32(0x060EA5F8u, 0x04400000); W32(0x060EA5FCu, 0x05200000);
                        W32(0x060EA5E8u, 0x04400000); W32(0x060EA5ECu, 0x05200000);
                        for (int16_t i = 0; i < 0x40; i++)
                            if (RS16(EFFECTS + (uint32_t)(int16_t)(i * 0x30)) != -1) W16(EFFECTS + (uint32_t)(int16_t)(i * 0x30), 0xFFFF);
                        Snd_SeqSlotVolume(0, 0, 0x80);
                        if (PX < RS32(0x060F7AD8u)) AND16(PL + 2, 0x7FFF); else OR16(PL + 2, 0x8000);
                        W16(0x060252BAu, (R16(0x060252BAu) & 0xFFF0) | 0x2006);
                    } else if (RS16(0x060F7AD4u) > 2) W16(0x060F7AD6u, 1);
                }
                W16(0x060EA426u, 0xFFFF);
                W16(STAGE_C + 0x18, R16(0x060D2E4Cu + 2 * k));
                W16(STAGE_C + 0x1A, R16(0x060D2E58u + 2 * k));
                W16(STAGE_C + 0x1C, R16(0x060D2E64u + 2 * k));
                W16(o + 0x14, 0xF);
                W16(o + 0x48, 1);
            }
        }
    }
    if (R16(o + 0x48) == 0 && R16(o + 0x14) < 0x2D) {
        if (VBL & 1) AND16(o + 0x58, 0xBFFF); else OR16(o + 0x58, 0x4000);
    }
    f_spr_cam(o);
    f_spr(0x0200, 0, (uint16_t)(k + 0x59), 1, 0x81);
    Anim_Tick(o + 0x58);
}

/* ---- 0x104D (0x0605FAA8): flash sprite (anim 0x23, chars 0x60, bank 0x58) at a random scale
 * (>= 0.5), drawn without camera; while the world is frozen by magic 6 it fades out (colour
 * calc 3 - t/3, 10 frames). Spawned by the stage hook (0x0606EC46). */
void ObjHandler_104D(void)
{
    uint32_t o = CUR;
    if (R16(o + 2) & 1) {
        OR16(o + 0x1C, 0x8000);
        W16(o + 6, 0x23);
        Anim_Start(o + 0x58, DEFS9B, SCR9B, 0x60, 0x23);
        AND16(o + 2, 0xFFFE);
        W32(o + 0x30, ((uint32_t)sub_0602DDD2() >> 3) & 0xFFFF);
        if (RS32(o + 0x30) < 0x8000) W32(o + 0x30, 0x8000);
        W16(o + 0x14, 10);
    }
    if (!FROZEN && (R16(o + 0x58) & 0x8000)) { W16(o, 0xFFFF); return; }
    W16(SPR + 2, (R32(o + 8) - 0x04000000u) >> 16);
    W16(SPR + 4, (R32(o + 0xC) - 0x04000000u) >> 16);
    W16(SPR + 0x10, 0x1A00);
    W32(SPR + 0x1C, R32(o + 0x30)); W32(SPR + 0x20, R32(o + 0x30));
    W32(SPR + 0x18, 0xFFFFFF80u);
    W16(SPR + 0x0A, 0x58);
    W16(SPR + 0x0C, 0xFFFF);
    W16(SPR + 0x0E, R16(o + 0x58) & 3);
    if (FROZEN && (R16(0x060252BAu) & 0x8000) && (R16(0x060252BAu) & 0xF) == 6) {
        W16(SPR + 0x0E, (uint32_t)(3 - (int16_t)(R16(o + 0x14) / 3)));
        W16(o + 0x14, R16(o + 0x14) - 1);
        if (R16(o + 0x14) == 0) { W16(o, 0xFFFF); return; }
    }
    Anim_Tick(o + 0x58);
}

/* ---- 0x104E (0x0605FC38): rising bubble / ember of enemy code (0x06082A76): anim flags & 0x1F
 * (chars 0x132), rises 2 px/frame and sways (+0x30 bounded to 1 px, random rate), anim 1
 * spins; freed above the camera. */
void ObjHandler_104E(void)
{
    uint32_t o = CUR;
    if (R16(o + 2) & 1) {
        OR16(o + 0x1C, 0x8000);
        W16(o + 6, R16(o + 0x12) & 0x1F);
        Anim_Start(o + 0x58, 0x060BC16Eu, 0x060E1A9Cu, 0x132, RS16(o + 6));
        AND16(o + 2, 0xFFFE);
        W16(o + 0x48, 0);
        W32(o + 0x30, (R16(o + 2) & 0x8000) ? 0xFFFE8000u : 0x18000);
        W32(o + 0x34, (f_rnd() & 0x7FF) + 0xC00);
    }
    if (!FROZEN) {
        W32(o + 0xC, R32(o + 0xC) - 0x20000);
        W32(o + 8, R32(o + 8) + R32(o + 0x30));
        if (!(R16(o + 2) & 0x8000)) {
            W32(o + 0x30, R32(o + 0x30) - R32(o + 0x34));
            if (RS32(o + 0x30) < -0x10000) W32(o + 0x30, 0xFFFF0000u);
        } else {
            W32(o + 0x30, R32(o + 0x30) + R32(o + 0x34));
            if (RS32(o + 0x30) > 0x10000) W32(o + 0x30, 0x10000);
        }
        if (R16(o + 6) == 1) W16(o + 0x48, R16(o + 0x48) + ((R16(o + 2) & 0x8000) ? (uint16_t)-3 : 3));
        if (RS32(o + 0xC) < CY) { W16(o, 0xFFFF); return; }
    }
    f_spr_cam(o);
    W16(SPR + 0x10, (R16(o + 2) & 0x8000) | 0x200);
    if (R16(o + 0x48) != 0) { W16(SPR + 0x10, (R16(o + 2) & 0x8000) | 0x600); W16(SPR + 0x14, R16(o + 0x48)); }
    W32(SPR + 0x18, 0xFFFFFFFFu); W16(SPR + 0x0A, 0x20); W16(SPR + 0x0C, 1); W16(SPR + 0x0E, 1);
    Anim_Tick(o + 0x58);
}

/* ---- 0x104F (0x0605FEC4): short-lived spark of enemy code (0x06084184): anim flags & 0x1F
 * (chars 0x132, bank 0x60), moves by +0x30/+0x34, spins, 26 frames fading (colour calc 1..3). */
void ObjHandler_104F(void)
{
    uint32_t o = CUR;
    if (R16(o + 2) & 1) {
        OR16(o + 0x1C, 0x8000);
        W16(o + 6, R16(o + 0x12) & 0x1F);
        Anim_Start(o + 0x58, 0x060BC1BEu, 0x060E1AC8u, 0x132, RS16(o + 6));
        AND16(o + 2, 0xFFFE);
        W16(o + 0x14, 0x1A);
        W16(o + 0x48, 0);
    }
    if (!FROZEN) {
        W32(o + 8, R32(o + 8) + R32(o + 0x30));
        W32(o + 0xC, R32(o + 0xC) - R32(o + 0x34));
        W16(o + 0x48, R16(o + 0x48) + 3);
        if (R16(o + 0x14) == 0) { W16(o, 0xFFFF); return; }
        W16(o + 0x14, R16(o + 0x14) - 1);
    }
    f_spr_cam(o);
    W16(SPR + 0x10, 0x0600); W16(SPR + 0x14, R16(o + 0x48));
    W32(SPR + 0x18, 0xFFFFFFFFu); W16(SPR + 0x0A, 0x60); W16(SPR + 0x0C, 1);
    W16(SPR + 0x0E, R16(o + 0x14) < 0xE ? (R16(o + 0x14) < 9 ? 3 : 2) : 1);
    Anim_Tick(o + 0x58);
}

/* ======================================================= strays: 0x103C, 0x104B */

/* 0x0605E76C: a shard (0x103C) at the CURRENT PROJECTILE + (dx, dy), thrown outwards
 * (x speed dx/8 px, y 2 px + 2 x sin(angle)); called by a projectile handler (0x0605E272..). */
int ObjF_ShardSpawn(int32_t dx, int32_t dy)
{
    uint32_t p = R32(PROJ_CUR);
    int16_t s = (int16_t)Obj_Register(0x103C, RS32(p + 8) + dx, RS32(p + 0xC) + dy, 0, 0xFFFF);
    int16_t sx = (int16_t)((uint32_t)dx >> 16);
    int a = Atan8(0, 0, sx, (int16_t)((uint32_t)dy >> 16));
    if (s == -1) return -1;
    uint32_t o = f_slot(s);
    W32(o + 0x30, (uint32_t)(sx << 13));
    W32(o + 0x34, (uint32_t)(FixMul(0x20000, Sin8(a)) + 0x20000));
    return s;
}

/* ---- 0x103C (0x0605E40A): shard (random anim 3/4/5 of 0x060AC54C / 0x060D2AF4, kept in +0x70):
 * flies with gravity 0x5000, spins (+4) against its x direction, blinks once its anim has
 * events, colour bank = events - 1; freed at the end of its animation. */
void ObjHandler_103C(void)
{
    uint32_t o = CUR;
    if (R16(o + 2) & 1) {
        W16(o + 0x1C, 0x8000);
        uint32_t r = ((uint32_t)sub_0602DDD2() >> 4) % 3u;
        if (r == 0) W16(o + 0x70, 3); else if (r == 1) W16(o + 0x70, 4); else if (r == 2) W16(o + 0x70, 5);
        Anim_Start(o + 0x58, 0x060AC54Cu, 0x060D2AF4u, 0x10, RS16(o + 0x70));
        AND16(o + 2, 0xFFFE);
        W16(o + 4, 0);
    }
    if (!FROZEN) {
        if (R16(o + 0x58) & 0x8000) { W16(o, 0xFFFF); return; }
        W32(o + 8, R32(o + 8) + R32(o + 0x30));
        W32(o + 0xC, R32(o + 0xC) - R32(o + 0x34));
        W32(o + 0x34, R32(o + 0x34) - 0x5000);
        if (RS32(o + 0x30) < 1) {
            W16(o + 4, R16(o + 4) + 4);
            if (RS16(o + 4) > 0x100) W16(o + 4, R16(o + 4) - 0x100);
        } else {
            W16(o + 4, R16(o + 4) - 4);
            if (RS16(o + 4) < -0x100) W16(o + 4, R16(o + 4) + 0x100);
        }
    }
    if (RS16(o) == -1) return;
    if (R16(o + 0x58) & 0xF) f_blink(o, 1);
    f_spr_cam(o);
    f_spr(0x0600, 0, 0, 0, 0xFFFFFFFFu);
    W16(SPR + 0x14, R16(o + 4));
    if ((R16(o + 0x58) & 0xF) > 1) W16(SPR + 0x0E, (R16(o + 0x58) & 0xF) - 1);
    Anim_Tick(o + 0x58);
}

/* ---- 0x104B (0x06052C4C): leaf of the 2-1 plants (spawned at 0x06052BB6, see 0x1013): anim 0
 * (0x060AA4C0 / 0x060D24CC), after +0x4C frames rises at -3 px (+0x34 = -3.125 or -3.5 with
 * flags & 1) slowing quickly, then sinks slowly while drifting away (+0x30 += 1/64 px), tilting;
 * 63 frames. */
void ObjHandler_104B(void)
{
    uint32_t o = CUR;
    if (R16(o + 2) & 1) {
        OR16(o + 0x1C, 0x8000);
        W16(o + 6, 0);
        Anim_Start(o + 0x58, 0x060AA4C0u, 0x060D24CCu, 0x10, 0);
        AND16(o + 2, 0xFFFE);
        W32(o + 0x34, (R16(o + 0x12) & 1) ? 0xFFFD8000u : 0xFFFDE000u);
        OR16(o + 0x12, 0x4000);
    }
    if (!FROZEN) {
        if (RS16(o + 0x4C) > 0) { W16(o + 0x4C, R16(o + 0x4C) - 1); return; }
        if (RS32(o + 0x34) < 0) W32(o + 0x34, R32(o + 0x34) + (uint32_t)R16(o + 0x14) * 0x600);
        else { W32(o + 0x34, R32(o + 0x34) + (uint32_t)R16(o + 0x14) * 0x40); W16(o + 0x48, R16(o + 0x48) + 1); }
        W32(o + 0x30, R32(o + 0x30) + 0x400);
        if (!(R16(o + 2) & 0x8000)) W32(o + 8, R32(o + 8) - R32(o + 0x30));
        else                        W32(o + 8, R32(o + 8) + R32(o + 0x30));
        W32(o + 0xC, R32(o + 0xC) + R32(o + 0x34));
        if (R16(o + 0x14) > 0x3E) W16(o, 0xFFFF);
        W16(o + 0x14, R16(o + 0x14) + 1);
    }
    f_spr_cam(o);
    f_spr((R16(o + 2) & 0x8000) | 0x600, (R16(o + 0x14) >> 4) & 3, 0, 0, 0x80);
    W16(SPR + 0x14, (uint32_t)((R16(o + 2) & 0x8000) ? RS16(o + 0x48) * 2 : RS16(o + 0x48) * -2));
    if (RS16(o) != -1) Anim_Tick(o + 0x58);
}
