/* Stage objects 0x1020-0x1024 (6-1, 3-1) and 0x1031-0x1034 (1-2), plus the helpers shared
 * by objhandlers_f2.c / objhandlers_f3.c. Ported from SHINOBI.PPB (Ghidra decompilation +
 * disassembly). See objhandlers_f.h for what each object is. Style: objhandlers_d.c. */
#include "objhandlers_f_int.h"

/* ================================================================ shared helpers */

/* 0x0606078C (second argument unused): Sho's sword hits an indestructible object:
 * spark (effect 5) + "clang" once per swing (player +4 |= 0x20). DUPLICATE of the
 * static Obj_SwordClang in objhandlers_a.c. */
int ObjF_SwordClang(uint32_t o)
{
    if (R16(PL + 4) & 0x20) return 0;
    if (Hitbox_Test(o, 0, PL, 2) == 0xFFFF) return 0;
    OR16(PL + 4, 0x20);
    Obj_Spawn(5, RS32(HIT_POINT), RS32(HIT_POINT + 4), R16(PL + 2) & 0x8000, 0);
    SFX(0x105);
    return 1;
}

/* 0x060601BA: the current object's weapon box (kind 2) touches Sho's body (kind 0):
 * spark + damage, unless Sho is invulnerable (0x060EA41C, +2 & 8, +4 & 0x410). */
int ObjF_SwordHurtsPlayer(int16_t dmg)
{
    if (R16(0x060EA41Cu) != 0 || (R16(PL + 2) & 8)) return 0;
    if (R16(PL + 4) & 0x410) return 0;
    if ((int16_t)Hitbox_Test(CUR, 2, PL, 0) == -1) return 0;
    Obj_Spawn(2, RS32(HIT_POINT), RS32(HIT_POINT + 4), 0, 0);
    Player_TakeDamage(dmg);
    return 1;
}

/* 0x0606081A: 6 explosion parts (effect 6) around o from table 0x060AC964
 * {s16 dx, s16 dy, s16 delay, s16 flags}; returns o+0x18 (item to drop). */
int ObjF_BreakParts(uint32_t o)
{
    for (uint32_t i = 0; i < 6; i++) {
        uint32_t t = 0x060AC964u + i * 8;
        Obj_Spawn(6, RS16(t) * 0x10000 + RS32(o + 8), RS16(t + 2) * 0x10000 + RS32(o + 0xC),
                  RS16(t + 6), RS16(t + 4));
    }
    return RS16(o + 0x18);
}

/* 0x0605817C: DUPLICATE of the static Obj_CarryEnemies in objhandlers_b.c: move the
 * enemies standing on `self` by (dx, -dy). */
void ObjF_CarryEnemies(uint32_t self, int32_t dx, int32_t dy)
{
    for (int16_t s = 0; s < OBJ_SLOTS; s++) {
        uint32_t e = f_slot(s);
        if (R16(e) == 0xFFFF || R16(e) >= 0x1000) continue;
        if (Enemy_FindSlot((int16_t)R16(e)) == -1) continue;
        if ((int16_t)Obj_CarryContact(self, e, 2, 8) == 0) continue;
        if (!(R16(e + 0x10) & 0x2000)) continue;
        W32(e + 8, R32(e + 8) + (uint32_t)dx);
        W32(e + 0xC, R32(e + 0xC) - (uint32_t)dy);
    }
}

/* 0x06060E8E: DUPLICATE (fx.c / objhandlers_a.c): object hitboxes vs projectile hitboxes. */
uint16_t ObjF_ProjHitbox(uint32_t o, int16_t ko, uint32_t p, int16_t kp)
{
    if (f_abs(RS32(o + 8) - RS32(p + 8)) > 0x02000000) return 0xFFFF;
    if (f_abs(RS32(o + 0xC) - RS32(p + 0xC)) > 0x02000000) return 0xFFFF;
    static const uint32_t TMP = 0x060FFE00u;
    W32(TMP + 8, R32(p + 8)); W32(TMP + 0xC, R32(p + 0xC));
    W16(TMP + 2, R16(p + 4) & 0x8000); W32(TMP + 0x68, R32(p + 0x30));
    return Hitbox_Test(o, ko, TMP, kp);
}

/* 0x0604B7AC: DUPLICATE: enemy shurikens (type 1, hostile, not bouncing) bounce off o. */
void ObjF_BlockShurikens(uint32_t o)
{
    for (uint32_t p = PROJECTILES; p < PROJECTILES + 0x1000u; p += 0x40) {
        if (R16(p) != 1) continue;
        if ((R16(p + 4) & 0x6000) != 0x2000) continue;
        if (ObjF_ProjHitbox(o, 0, p, 2) == 0xFFFF) continue;
        W16(p + 6, 0);
        OR16(p + 4, 0x4000);
        int32_t v = RS32(p + 0x18); W32(p + 0x18, (uint32_t)-((v + (v < 0)) >> 1));
        v = RS32(p + 0x1C);         W32(p + 0x1C, (uint32_t)-((v + (v < 0)) >> 1));
    }
}

/* ---- stage-hook state block 0x060F4D40 (the per-stage frame hooks, 0x060B10CC, are not
 * ported; these setters are what the objects call) ---- */
#define HK 0x060F4D40u
void Hook_0606D774(void) { W16(HK + 0x18, 1); }
void Hook_0606D77C(void)
{
    W16(HK + 0x18, 0);
    W16(HK + 0x0A, RS16(HK + 0x0A) < 4 ? 5 : 7);
    W16(HK + 0x0C, 0); W32(HK + 0x10, 0); W32(HK + 0x14, 0);
}
void Hook_0606D7AE(void)
{
    W16(HK + 0x0A, 4); W16(HK + 0x0C, 0); W32(HK + 0x10, 0); W32(HK + 0x14, 0); W16(HK + 0x0E, 0);
}
int Hook_0606D7C8(void)
{
    int16_t s = RS16(HK + 0x0A);
    return s == 1 || s == 2 || s == 3 || s == 6 || s == 8;
}
void Hook_0606E154(void) { W16(HK + 4, R16(HK + 4) + 1); W16(HK + 6, 60); OR16(0x060EA638u, 0x80); }
void Hook_0606E184(void)
{
    W16(HK + 4, 6);
    W32(STAGE_C + 0xC, 0x2FA00000u);
    W32(STAGE_C, 0x05400000u);
    W32(CAM_Y, R32(STAGE_C + 0xC) + 0xFF200000u);
    W32(0x060EA550u, (uint32_t)(PX - CX));
    W32(0x060EA554u, (uint32_t)(PY - CY));
}
void Hook_0606E1B8(void) { W16(HK + 4, R16(HK + 4) + 1); }

/* 0x06051432: register an enemy slot in the list 0x060F2250 (count 0x060F227E, max 23)
 * of enemies carried by the 0x1021 lanterns (called by enemy code 0x06098B50). */
void ObjF_CarryListAdd(int16_t slot)
{
    int16_t n = RS16(0x060F227Eu);
    if (n >= 23) return;
    W16(0x060F2250u + 2u * (uint32_t)n, (uint16_t)slot);
    W16(0x060F227Eu, (uint32_t)(n + 1));
}

/* ======================================================= 6-1: 0x1020 */
#define DEFS61  0x060ABCF0u
#define SCR61   0x060D2878u

/* 0x06059DE0: background decoration scrolling at 1/8 (flags & 0x1F = 0) or 5/16 of the
 * camera (part = flags & 0x1F), drawn against the layer camera Y 0x060EA58C. */
void ObjHandler_1020(void)
{
    uint32_t o = CUR;
    if (R16(o + 2) & 1) {
        f_anim(o, DEFS61, SCR61, 7);
        AND16(o + 2, 0xFFFE);
        W16(o + 0x14, 0);
        W16(o + 0x1C, 0x8000);
    }
    uint32_t m = (uint32_t)FixMul(CX - 0x04000000, (R16(o + 0x12) & 0x1F) == 0 ? 0x2000 : 0x5000);
    int32_t base = (int32_t)(m & 0xFFFF0000u) + 0x04000000;
    int32_t d = RS32(o + 8) - base + (int32_t)0xFF600000;
    if (d < 0) d = base - RS32(o + 8) + 0x00A00000;
    if (d >= 0x01800001) return;
    W16(SPR + 2, (uint32_t)((RS32(o + 8) - base) >> 16));
    W16(SPR + 4, (uint32_t)((RS32(o + 0xC) - RS32(0x060EA58Cu)) >> 16));
    f_spr(0x0200, 2, 0x20, 0xFFFF, 0xFFFFFF00u);
    if (R16(o + 0x12) & 0x1F) { W16(SPR + 0x0A, 0); W16(SPR + 0x0E, 0); W32(SPR + 0x18, 0xFFFFFF02u); }
    W16(o + 0x5A, R16(o + 0x12) & 0x1F);
    Anim_Tick(o + 0x58);
}

/* ======================================================= 3-1: 0x1021-0x1024 */
#define DEFS31  0x060AA13Eu
#define SCR31   0x060D2380u

/* 0x060510BC: a 0x1021 lantern's glass is cut: 17 shards (0x1024, table 0x060AA438
 * {s32 dx, s32 dy}), the linked lid/base (+0x48) gets its fall, item / enemy type 5. */
static void Lantern_Shatter(uint32_t o)
{
    SFX(0x22D);
    for (int16_t i = 0; (uint16_t)i < 0x11; i++) {
        int32_t dx = RS32(0x060AA438u + (uint32_t)i * 8), dy = RS32(0x060AA43Cu + (uint32_t)i * 8);
        int16_t s;
        if (!(R16(o + 4) & 1)) {
            s = (int16_t)Obj_Register(0x1024, RS32(o + 8) + dx, RS32(o + 0xC) - dy, (uint16_t)f_smod(i, 6), 0xFFFF);
            if (s != -1) {
                int16_t sx = (int16_t)((uint32_t)dx >> 16);
                int a = Atan8(0, 0, sx, (int16_t)((uint32_t)(0x400000 - dy) >> 16));
                uint32_t p = f_slot(s);
                W32(p + 0x34, (uint32_t)(FixMul(0x50000, Sin8(a)) + 0x80000));
                W32(p + 0x30, (uint32_t)(sx * 0xE00));
                W16(p + 0x14, 0x3C);
                W16(p + 4, 1);
            }
        } else {
            s = (int16_t)Obj_Register(0x1024, RS32(o + 8) + dx, RS32(o + 0xC) + dy - 0x400000, (uint16_t)f_smod(i, 6), 0xFFFF);
            if (s != -1) {
                uint32_t p = f_slot(s);
                W32(p + 0x30, 0);
                W32(p + 0x34, (uint32_t)((0x11 - i) * 0x6000));
                W16(p + 0x14, 0x23);
                W16(p + 4, 0);
            }
        }
    }
    uint32_t q = f_slot(RS16(o + 0x48));
    W16(q + 0x4A, 1);
    if (!(R16(o + 4) & 1)) {
        W32(q + 0x30, 0); W32(q + 0x34, 0x88000); W16(q + 0x4C, 0); W16(q + 0x14, 0x3C);
    } else {
        W32(q + 0x30, 0); W32(q + 0x34, 0); W16(q + 0x14, 0x1E);
    }
    if (!(R16(o + 0x12) & 8)) {
        if (RS16(o + 0x18) != -1) {
            if (R16(o + 4) & 1) Item_Drop(RS16(o + 0x18), RS32(o + 8), RS32(o + 0xC) - 0x300000);
            else                Item_Drop(RS16(o + 0x18), RS32(o + 8), RS32(o + 0xC));
        }
    } else if ((int16_t)Obj_Register(5, RS32(o + 8), RS32(o + 0xC), 0, (uint16_t)RS16(o + 0x18)) == -1) {
        for (int16_t s = 0; s < 0xC0; s++)                   /* no slot: recycle a shard */
            if (R16(f_slot(s)) == 0x1024) {
                W16(f_slot(s), 0xFFFF);
                Obj_Register(5, RS32(o + 8), RS32(o + 0xC), 0, (uint16_t)RS16(o + 0x18));
                return;
            }
    }
}

/* 0x06050884: stone / hanging lantern. flags & 7: 0 glass (anim 7, cut by the sword or,
 * with flags & 0x18 = 0x18, when Sho walks under it), 1 stone lantern (anim 9: spawns its
 * glass 32 px up and the lid (2) 96 px up), 2 lid (anim 8, Sho stands on it; falls and spins
 * once the glass breaks), 3 hanging lantern (anim 0xC: glass 97 px down, base (4) 96 px
 * down), 4 base (anim 0xD), 5/6 static parts (anims 10/11). The enemies listed in 0x060F2250
 * (count 0x060F227E) within 80 px are carried. +4 bit 0: standing type, bit 1 no contact,
 * bit 2 no carrying; +0x48 linked part, +0x4E parent lantern. */
void ObjHandler_1021(void)
{
    uint32_t o = CUR;
    uint16_t preg = 0;
    if (R16(o + 2) & 1) {
        OR16(o + 0x1C, 0x4000);
        uint16_t k = R16(o + 0x12) & 7;
        if (k == 0) W16(o + 6, 7);
        else if (k == 1) {
            W16(o + 4, 0); W16(o + 6, 9);
            int16_t a = (int16_t)Obj_Register(0x1021, RS32(o + 8), RS32(o + 0xC) - 0x200000, R16(o + 0x12) & 0x18,
                                              (uint16_t)RS16(o + 0x18));
            int16_t b = (int16_t)Obj_Register(0x1021, RS32(o + 8), RS32(o + 0xC) - 0x600000, 2, 0xFFFF);
            if (a != -1 && b != -1) {
                uint32_t p = f_slot(a);
                W16(p + 0x48, (uint16_t)b); W16(p + 4, 0); W16(p + 0x4E, CUR_IDX);
                W16(f_slot(b) + 0x48, (uint16_t)a);
            }
        } else if (k == 2) { W16(o + 4, 0); W16(o + 6, 8); W16(o + 0x4A, 0); }
        else if (k == 3) {
            W16(o + 4, 0); W16(o + 6, 0xC);
            int16_t a = (int16_t)Obj_Register(0x1021, RS32(o + 8), RS32(o + 0xC) + 0x610000, R16(o + 0x12) & 0x18,
                                              (uint16_t)RS16(o + 0x18));
            int16_t b = (int16_t)Obj_Register(0x1021, RS32(o + 8), RS32(o + 0xC) + 0x600000, 4, 0xFFFF);
            if (a != -1 && b != -1) {
                uint32_t p = f_slot(a);
                W16(p + 0x48, (uint16_t)b); W16(p + 4, 1); W16(p + 0x4E, CUR_IDX);
            }
        } else if (k == 4) { W16(o + 4, 0); W16(o + 6, 0xD); W16(o + 0x4A, 0); }
        else if (k == 5) { W16(o + 4, 0); W16(o + 6, 10); }
        else if (k == 6) { W16(o + 4, 0); W16(o + 6, 0xB); }
        f_anim_cur(o, DEFS31, SCR31);
        AND16(o + 2, 0xFFFE);
    }
    if (!(R16(o + 4) & 4)) {                                    /* carry the listed enemies */
        for (int16_t i = 0; i < RS16(0x060F227Eu); i++) {
            uint32_t e = f_slot(RS16(0x060F2250u + 2u * (uint32_t)i));
            if (f_abs(RS32(e + 8) - RS32(o + 8)) < 0x500000) {
                uint16_t fl = R16(e + 0x10);
                Obj_CarryContact(o, e, 4, 7);
                OR16(e + 0x10, fl);
            }
        }
    }
    if (!((f_scr_dx(o) < 0x01000000 && f_scr_dy(o) < 0x01000000) || (R16(o + 2) & 0x4008))) return;
    if (!FROZEN) {
        if (!(R16(o + 4) & 2)) Obj_PlayerContact(0);
        if (!(R16(o + 0x58) & 0x8000)) {
            int16_t a = RS16(o + 6);
            if (a == 7) {
                if ((R16(o + 0x12) & 0x18) == 0x18 && f_abs(RS32(o + 8) - PX) < 0xA00000 &&
                    PY - RS32(o + 0xC) < 0x280000)
                    OR16(o + 2, 8);
                if ((int16_t)Hitbox_Test(o, 0, PL, 2) != -1) {
                    OR16(o + 2, 8);
                    Obj_Spawn(2, RS32(HIT_POINT), RS32(HIT_POINT + 4), 0, 0);
                }
                if (R16(o + 2) & 8) { W16(o + 6, 6); f_anim_cur(o, DEFS31, SCR31); }
            } else if (a == 8) {
                OR16(o + 4, 2);
                if (R16(o + 0x4A) == 0) {
                    if (PY <= RS32(o + 0xC) + 0x10000) {
                        Obj_PlayerContact(0);
                        if ((int16_t)Hitbox_Test(o, 0, PL, 2) != -1) OR16(f_slot(RS16(o + 0x48)) + 2, 8);
                    }
                } else {                                        /* falling, spinning */
                    OR16(o + 4, 4); OR16(o + 2, 0x4000);
                    W32(o + 8, R32(o + 8) + R32(o + 0x30));
                    W32(o + 0xC, R32(o + 0xC) - R32(o + 0x34));
                    W32(o + 0x34, R32(o + 0x34) - 0x5000);
                    W16(o + 0x4C, R16(o + 0x4C) + 5);
                    if (RS16(o + 0x4C) > 0xFF) W16(o + 0x4C, R16(o + 0x4C) - 0x100);
                    if (R16(o + 0x14) < 0xF) f_blink(o, 1);
                    W16(o + 0x14, R16(o + 0x14) - 1);
                    if (RS16(o + 0x14) < 1) W16(o, 0xFFFF);
                }
            } else if (a == 0xD && R16(o + 0x4A) != 0) {
                OR16(o + 4, 2); OR16(o + 4, 4); OR16(o + 2, 0x4000);
                W32(o + 8, R32(o + 8) + R32(o + 0x30));
                W32(o + 0xC, R32(o + 0xC) - R32(o + 0x34));
                W32(o + 0x34, R32(o + 0x34) - 0x5000);
                if (R16(o + 0x14) < 0xF) f_blink(o, 1);
                W16(o + 0x14, R16(o + 0x14) - 1);
                if (RS16(o + 0x14) < 1) W16(o, 0xFFFF);
            }
        } else if (R16(o + 6) == 6) {                           /* glass break anim over */
            Lantern_Shatter(o);
            uint32_t p = f_slot(RS16(o + 0x4E));
            if (RS16(p + 6) == 9) W16(p + 6, 10);
            else if (RS16(p + 6) == 0xC) W16(p + 6, 0xB);
            Anim_Start(p + 0x58, DEFS31, SCR31, 0x10, RS16(p + 6));
            W16(o, 0xFFFF);
        }
    }
    if (R16(o + 6) == 7) {                                      /* the flame inside */
        Anim_Start(SCRATCH_ANIM, DEFS31, SCR31, 0x10, 0x10);
        f_spr_cam(o);
        W16(SPR + 0x10, (R16(o + 2) & 0x8000) | 0x200);
        W32(SPR + 0x18, 0x84); W16(SPR + 0x0A, 0x20); W16(SPR + 0x0C, 0xFFFF); W16(SPR + 0x0E, 0);
        Anim_Tick(SCRATCH_ANIM);
    }
    f_spr_cam(o);
    W16(SPR + 0x10, (R16(o + 2) & 0x8000) | 0x200);
    if (R16(o + 6) == 8 && R16(o + 0x4A) != 0) {
        W16(SPR + 0x10, (R16(o + 2) & 0x8000) | 0x600);
        W16(SPR + 0x14, R16(o + 0x4C));
    }
    W32(SPR + 0x18, 0x83);
    W16(SPR + 0x0A, 0x20);
    if (!(R16(o + 2) & 0x4000)) preg = 0xFFFF;
    W16(SPR + 0x0E, R16(o + 6) == 7);
    W16(SPR + 0x0C, preg);
    Anim_Tick(o + 0x58);
}

/* 0x060516BC: the wall-mounted eye of 3-1's end (anim 0xE, 6 HP, palette 0x1C0 x 64 from the
 * stage palette + 0xF80). Positioned from the stage-hook scroll (0x060F4D68/0x060F4D6C, or
 * 0x060EA5F8/FC while dying); hidden while the hook position is (0x800, 0x540). While the hook
 * state 0x060F4D4A is 1/2/3/6/8 it hurts Sho (1), its palette pulses towards 0x060D2298 and
 * Sho's sword takes Player_SwordPower(); hit -> anim 0xF (flashing 0x060D2098), hook 0x0606D774,
 * then 0x0606D77C, and at 0 HP 0x0606D7AE. The original also calls 0x0604967C (shurikens vs
 * the CURRENT ENEMY 0x060F205C, not this object) - kept. */
void ObjHandler_1022(void)
{
    uint32_t o = CUR;
    uint32_t pal = R32(0x060EA650u) + 0xF80;
    if (R16(o + 2) & 1) {
        OR16(o + 0x1C, 0x4000);
        f_anim(o, DEFS31, SCR31, 0xE);
        AND16(o + 2, 0xFFFE); AND16(o + 2, 0xFFF7);
        Pal_Copy(0x1C0, 0x40, pal);
        W16(o + 0x16, 6); W16(o + 0x48, 0); W16(o + 0x4A, 0); W16(o + 0x4C, 0);
    }
    if (RS32(0x060F4D68u) == 0x08000000 && RS32(0x060F4D6Cu) == 0x05400000) return;
    if (!FROZEN) {
        uint32_t sx = 0x060F4D68u, sy = 0x060F4D6Cu;
        if (RS16(o + 6) == 0xF && !(R16(o + 0x58) & 0x8000)) { sx = 0x060EA5F8u; sy = 0x060EA5FCu; }
        W32(o + 8, (uint32_t)((0x800 - (int16_t)(R32(sx) >> 16)) * 0x10000 + CX + 0xD20000));
        W32(o + 0xC, (uint32_t)((0x520 - (int16_t)(R32(sy) >> 16)) * 0x10000 + CY + 0x1F0000));
        W32(o + 0x30, R32(o + 8));
        W32(o + 0x34, R32(o + 0xC));
        if (!(R16(o + 0x58) & 0x8000)) {
            if (RS16(o + 6) == 0xE) {
                if (Hook_0606D7C8()) {
                    ObjF_SwordHurtsPlayer(1);
                    Enemy_CheckShurikens();
                    if (RS16(o + 0x4A) == 0) { if (Pal_StepTowards(0x1C0, 0x40, 0x060D2298u, 1)) W16(o + 0x4A, 1); }
                    else if (Pal_StepTowards(0x1C0, 0x40, pal, 1)) W16(o + 0x4A, 0);
                    if ((int16_t)Hitbox_Test(o, 0, PL, 2) != -1) {
                        Obj_Spawn(2, RS32(HIT_POINT), RS32(HIT_POINT + 4), 0, 0);
                        OR16(o + 2, 8);
                        W16(o + 0x16, (uint32_t)(RS16(o + 0x16) - (int16_t)Player_SwordPower()));
                        SFX(0x104);
                    }
                }
                if (R16(o + 2) & 8) {
                    W16(o + 6, 0xF); f_anim_cur(o, DEFS31, SCR31);
                    Hook_0606D774();
                    SFX(0x237);
                }
            } else if (RS16(o + 6) == 0xF) {
                Pal_Copy(0x1C0, 0x40, (VBL & 1) ? 0x060D2098u : pal);
            }
        } else if (RS16(o + 6) == 0xF && RS16(o + 0x48) == 0) {
            Hook_0606D77C();
            Pal_Copy(0x1C0, 0x40, pal);
            if (RS16(o + 0x16) < 1) { Hook_0606D7AE(); W16(o + 0x48, 1); }
            else { W16(o + 6, 0xE); f_anim_cur(o, DEFS31, SCR31); AND16(o + 2, 0xFFF7); }
        }
    }
    if (!FROZEN) {
        W16(SPR + 2, (uint32_t)((RS32(o + 0x30) - CX) >> 16));
        W16(SPR + 4, (uint32_t)((RS32(o + 0x34) - CY) >> 16));
        W32(o + 0x30, (uint32_t)(RS32(o + 0x30) - CX));
        W32(o + 0x34, (uint32_t)(RS32(o + 0x34) - CY));
    } else {
        W16(SPR + 2, R32(o + 0x30) >> 16);
        W16(SPR + 4, R32(o + 0x34) >> 16);
    }
    f_spr(0x0200, 0, 0x10, 1, 0x85);
    Anim_Tick(o + 0x58);
}

/* 0x06051B9C: sliding wall block (anim 0x11; flags & 1: fixed part, anim 0x12). Placement
 * flags & 0x1E = 0/2/4/6 -> block k (slot kept in 0x060F2280 + 2k; 4/6 start 76 px left)
 * and phase: wait 30 frames, slide 76 px left (1.75 px/frame), wait 60, slide back. Sho
 * standing on block k < 3 to the left of block k+1's x - 10 is pushed off (+0x10 |= 0x8000). */
void ObjHandler_1023(void)
{
    uint32_t o = CUR;
    if (R16(o + 2) & 1) {
        OR16(o + 0x1C, 0x4000);
        if (!(R16(o + 0x12) & 1)) {
            W16(o + 6, 0x11);
            W16(o + 0x48, R32(o + 8) >> 16);
            uint16_t k = R16(o + 0x12) & 0x1E;
            if (k == 0) { W16(o + 0x14, 0); W16(o + 0x4A, 0); W16(o + 0x4C, 0); W16(0x060F2280u, CUR_IDX); }
            else if (k == 2) { W16(o + 0x14, 0); W16(o + 0x4A, 2); W16(o + 0x4C, 1); W16(0x060F2284u, CUR_IDX); }
            else if (k == 4) {
                W32(o + 8, (uint32_t)((RS16(o + 0x48) - 0x4C) * 0x10000));
                W16(o + 0x4A, 1); W16(o + 0x4C, 2); W16(o + 0x14, 0); W16(0x060F2282u, CUR_IDX);
            } else if (k == 6) {
                W32(o + 8, (uint32_t)((RS16(o + 0x48) - 0x4C) * 0x10000));
                W16(o + 0x4A, 3); W16(o + 0x4C, 3); W16(o + 0x14, 0); W16(0x060F2286u, CUR_IDX);
            }
        } else W16(o + 6, 0x12);
        f_anim_cur(o, DEFS31, SCR31);
        AND16(o + 2, 0xFFFE);
    }
    if (f_scr_dx(o) >= 0x01000000) return;
    if (!FROZEN && RS16(o + 6) == 0x11) {
        int16_t c = (int16_t)Obj_PlayerContact(0);
        if (c == 2 && RS16(o + 0x4A) < 3 &&
            RS32(f_slot(RS16(0x060F2282u + 2u * (uint32_t)RS16(o + 0x4A))) + 8) - 0xA0000 < PX)
            OR16(PL + 0x10, 0x8000);
        int16_t ph = RS16(o + 0x4C);
        if (ph == 0) {
            W16(o + 0x14, R16(o + 0x14) + 1);
            if (R16(o + 0x14) > 0x1E) { W16(o + 0x4C, R16(o + 0x4C) + 1); W16(o + 0x14, 0); }
        } else if (ph == 1) {
            W32(o + 8, R32(o + 8) - 0x1C000);
            if ((int16_t)(R32(o + 8) >> 16) <= RS16(o + 0x48) - 0x4C) {
                W32(o + 8, (uint32_t)((RS16(o + 0x48) - 0x4C) * 0x10000));
                W16(o + 0x4C, R16(o + 0x4C) + 1); W16(o + 0x14, 0);
            }
        } else if (ph == 2) {
            W16(o + 0x14, R16(o + 0x14) + 1);
            if (R16(o + 0x14) > 0x3C) { W16(o + 0x4C, R16(o + 0x4C) + 1); W16(o + 0x14, 0); }
        } else if (ph == 3) {
            W32(o + 8, R32(o + 8) + 0x1C000);
            if (RS16(o + 0x48) <= (int16_t)(R32(o + 8) >> 16)) {
                W32(o + 8, (uint32_t)(RS16(o + 0x48) << 16));
                W16(o + 0x4C, 0); W16(o + 0x14, 0);
            }
        }
    }
    f_spr_cam(o);
    f_spr(0x0200, 0, 0, 1, RS16(o + 6) == 0x11 ? 0x84 : 0x83);
    Anim_Tick(o + 0x58);
}

/* 0x06051458: glass shard / lid piece (anim = flags & 0x1F, spawned by Lantern_Shatter):
 * +0x30/+0x34 velocity (Y up, gravity 0x5000, max fall 1.5 px), +0x14 life (blinks below
 * 25 frames), spins (+0x48 += 2). Freed off screen. */
void ObjHandler_1024(void)
{
    uint32_t o = CUR;
    if (R16(o + 2) & 1) {
        W16(o + 0x1C, 0x8000);
        W16(o + 6, R16(o + 0x12) & 0x1F);
        f_anim_cur(o, DEFS31, SCR31);
        AND16(o + 2, 0xFFFE);
        W16(o + 0x48, 0);
    }
    if (f_scr_dx(o) >= 0x01000000 || f_scr_dy(o) >= 0x01000000) { W16(o, 0xFFFF); return; }
    if (!FROZEN) {
        W32(o + 8, R32(o + 8) + R32(o + 0x30));
        W32(o + 0xC, R32(o + 0xC) - R32(o + 0x34));
        W32(o + 0x34, R32(o + 0x34) - 0x5000);
        if (RS32(o + 0x34) < -0x180000) W32(o + 0x34, 0xFFE80000u);
        if (R16(o + 0x14) < 0xC) f_blink(o, 2);
        else if (R16(o + 0x14) < 0x19) f_blink(o, 1);
        else AND16(o + 0x58, 0xBFFF);
        W16(o + 0x14, R16(o + 0x14) - 1);
        if (RS16(o + 0x14) < 1) W16(o, 0xFFFF);
        W16(o + 0x48, R16(o + 0x48) + 2);
        if (RS16(o + 0x48) > 0xFF) W16(o + 0x48, R16(o + 0x48) - 0x100);
    }
    f_spr_cam(o);
    W16(SPR + 0x10, (R16(o + 2) & 0x8000) | 0x600);
    W16(SPR + 0x14, R16(o + 0x48));
    W32(SPR + 0x18, 0x83); W16(SPR + 0x0A, 0x20); W16(SPR + 0x0C, 0); W16(SPR + 0x0E, 2);
    Anim_Tick(o + 0x58);
}

/* ======================================================= 1-2: 0x1031-0x1034 */
#define DEFS12  0x060AC250u
#define SCR12   0x060D2A04u

/* 0x0605C85C: swinging bell / gong (anim 0, parts 1 and 0): rests 900 frames at angle 24,
 * swings to -24 one step a frame, waits 24 frames, swings back 3 steps a frame (sound 0x21C). */
void ObjHandler_1031(void)
{
    uint32_t o = CUR;
    if (R16(o + 2) & 1) {
        f_anim(o, DEFS12, SCR12, 0);
        AND16(o + 2, 0xFFFE);
        W16(o + 0x14, 0); W16(o + 0x48, 0x18); W16(o + 0x4A, 0); W16(o + 0x1C, 0x8000);
    }
    if (!FROZEN) {
        int16_t s = RS16(o + 0x4A);
        if (s == 0) {
            W16(o + 0x48, 0x18);
            W16(o + 0x14, R16(o + 0x14) + 1);
            if (R16(o + 0x14) > 900) { W16(o + 0x14, 0); W16(o + 0x4A, R16(o + 0x4A) + 1); }
        } else if (s == 1) {
            W16(o + 0x48, R16(o + 0x48) - 1);
            if (RS16(o + 0x48) < -0x18) { W16(o + 0x14, 0); W16(o + 0x4A, R16(o + 0x4A) + 1); }
        } else if (s == 2) {
            W16(o + 0x14, R16(o + 0x14) + 1);
            if (R16(o + 0x14) > 0x18) { W16(o + 0x14, 0); W16(o + 0x4A, R16(o + 0x4A) + 1); }
        } else if (s == 3) {
            W16(o + 0x48, R16(o + 0x48) + 3);
            if (RS16(o + 0x48) > 0x18) { W16(o + 0x14, 0); W16(o + 0x4A, 0); SFX(0x21C); }
        }
    }
    f_spr_cam(o);
    f_spr(0x0200, 0, 0x20, 0, 0xFFFFFF7Eu);
    W16(o + 0x5A, 1);
    Anim_Tick(o + 0x58);
    W16(SPR + 0x10, 0x0600);
    W16(SPR + 0x14, R16(o + 0x48));
    W16(SPR + 4, R16(SPR + 4) - 6);
    W16(o + 0x5A, 0);
    Anim_Tick(o + 0x58);
}

/* 0x0605CA4C: hanging sign / lantern on a rope (anim 1: part 0 fixed at the spawn point,
 * part 1 drawn by hand with +0x4C tilt). The sword cuts it (spark, sound 0x122, attack kind
 * not 4/5): it slides down-left 16 px accelerating, then tilts away (sound 0x213 twice).
 * +0x50 flicker period (8.8, grows by 0x18 per frame to 0x1800 then stops: +0x52 sign picks
 * the colour bank 0x24 / 0x25). */
void ObjHandler_1032(void)
{
    uint32_t o = CUR;
    if (R16(o + 2) & 1) {
        f_anim(o, DEFS12, SCR12, 1);
        AND16(o + 2, 0xFFFE);
        W16(o + 0x14, 0);
        W16(o + 0x48, R32(o + 8) >> 16);
        W16(o + 0x4A, R32(o + 0xC) >> 16);
        W16(o + 0x4C, 0); W16(o + 0x4E, 0); W16(o + 0x50, 0); W16(o + 0x52, 1);
        W16(o + 0x1C, 0x8000);
        OR16(o + 0x12, 0x4000);
    }
    if (!FROZEN) {
        int16_t s = RS16(o + 0x4E);
        if (s == 0) {
            if (!(R16(o + 2) & 8)) {
                OR16(o + 0x58, 0x4000);
                W16(o + 0x5A, 0);
                Anim_Tick(o + 0x58);                             /* hitboxes only */
                AND16(o + 0x58, 0xBFFF);
                if ((int16_t)Hitbox_Test(o, 0, PL, 2) != -1) {
                    int16_t k = (int16_t)Player_AttackKind();
                    if (k != 4 && k != 5) {
                        Obj_Spawn(2, RS32(HIT_POINT), RS32(HIT_POINT + 4), 0, 0);
                        OR16(o + 2, 8);
                        W32(o + 0x34, 0);
                        W16(o + 0x50, 0x100);
                        W16(o + 0x4E, R16(o + 0x4E) + 1);
                        SFX(0x122);
                    }
                }
            }
        } else if (s == 1) {
            W32(o + 0x34, R32(o + 0x34) + 0x800);
            W32(o + 8, R32(o + 8) - R32(o + 0x34));
            W32(o + 0xC, R32(o + 0xC) + R32(o + 0x34));
            if ((int16_t)(R32(o + 0xC) >> 16) - RS16(o + 0x4A) > 0x10) {
                W32(o + 0x34, 0); W16(o + 0x4C, 0);
                W16(o + 0x4E, R16(o + 0x4E) + 1);
                SFX(0x213);
            }
        } else if (s == 2) {
            W32(o + 0x34, R32(o + 0x34) - 8);
            W16(o + 0x4C, R16(o + 0x4C) + (uint16_t)R32(o + 0x34));
            if (RS16(o + 0x4C) < -0x2000) { W16(o + 0x4E, R16(o + 0x4E) + 1); SFX(0x213); }
        }
        if (RS16(o + 0x50) < 0) W16(o + 0x52, 0xFFFF);
        else {
            W16(o + 0x14, R16(o + 0x14) + 1);
            if ((RS16(o + 0x50) >> 8) <= (int)R16(o + 0x14)) {
                W16(o + 0x52, (uint32_t)-RS16(o + 0x52));
                W16(o + 0x14, 0);
                if (RS16(o + 0x50) < 0x100) W16(o + 0x52, 1);
            }
            if (RS16(o + 0x50) != 0) {
                W16(o + 0x50, R16(o + 0x50) + 0x18);
                if (RS16(o + 0x50) > 0x1800) W16(o + 0x50, 0xFFFF);
            }
        }
    }
    AND16(o + 0x58, 0xBFFF);
    W16(SPR + 2, (uint32_t)((RS16(o + 0x48) * 0x10000 - CX) >> 16));
    W16(SPR + 4, (uint32_t)((RS16(o + 0x4A) * 0x10000 - CY) >> 16));
    f_spr(0x0200, 0, 0x23, 0, 0xFFFFFF80u);
    W16(o + 0x5A, 0);
    Anim_Tick(o + 0x58);
    OR16(o + 0x58, 0x4000);
    W16(o + 0x5A, 1);
    Anim_Tick(o + 0x58);                                        /* part 1: fill the sprite only */
    W16(SPR + 6, R16(SPR + 6) + 3);
    W16(SPR + 8, R16(SPR + 8) + 0x14);
    W16(SPR + 2, (uint32_t)(((RS32(o + 8) - CX) >> 16) - 4));
    W16(SPR + 4, (uint32_t)(((RS32(o + 0xC) - CY) >> 16) - 0x10));
    W16(SPR + 0x0A, (uint32_t)(RS16(SPR + 0x0A) + (RS16(o + 0x52) < 0 ? 2 : 1)));
    W16(SPR + 0x10, 0x0600);
    W16(SPR + 0x14, (uint32_t)(RS16(o + 0x4C) >> 8));
    Sprite_Draw(SPR);
}

/* 0x0605CE4A: cloud drifting left (0.25 px/frame, flags & 1: 3/16 px and part 1), wraps from
 * x < -160 (layer) to 1024+... (0x06800000); drawn without camera (world - 1024). */
void ObjHandler_1033(void)
{
    uint32_t o = CUR;
    if (R16(o + 2) & 1) {
        f_anim(o, DEFS12, SCR12, 2);
        AND16(o + 2, 0xFFFE);
        W16(o + 0x14, 0); W16(o + 0x1C, 0x8000);
    }
    if (!FROZEN) {
        W32(o + 8, R32(o + 8) - ((R16(o + 0x12) & 1) ? 0x3000 : 0x4000));
        if (RS32(o + 8) - 0x04000000 < -0xA00000) W32(o + 8, 0x06800000u);
    }
    W16(SPR + 2, (R32(o + 8) - 0x04000000u) >> 16);
    W16(SPR + 4, (R32(o + 0xC) - 0x04000000u) >> 16);
    f_spr(0x0200, 2, 0x21, 0xFFFF, 0xFFFFFF7Eu);
    W16(o + 0x5A, R16(o + 0x12) & 1);
    Anim_Tick(o + 0x58);
}

/* 0x0605D150: one falling leaf/petal (0x1034 flags 1) at the stage's right edge (C+8), random
 * height 0..255, direction 0x8C..0x9B, speed 0.625..0.75 px/frame, phase, colour (+0x4C). */
static void Petal_Spawn(void)
{
    int32_t x = RS32(STAGE_C + 8);
    int16_t s = (int16_t)Obj_Register(0x1034, x, (int32_t)((f_rnd() & 0xFF) << 16) + 0x04000000, 1, 0xFFFF);
    if (s < 0) return;
    int16_t a = (int16_t)((f_rnd() & 0xF) + 0x8C);
    int32_t v = (int32_t)(f_rnd() & 0x1FFF) + 0xA000;
    uint32_t p = f_slot(s);
    W32(p + 0x30, (uint32_t)FixMul(Cos8(a), v));
    W32(p + 0x34, (uint32_t)FixMul(Sin8(a), v));
    W16(p + 0x4A, f_rnd() & 0xFF);
    W16(p + 0x4C, f_rnd() & 1);
}

/* 0x0605CF64: petal generator (4 petals every 32..63 frames) / flags & 1: a petal (anim 3,
 * bank 0x22, flutters: y += sin(+0x4A) * vy/2, rotation +0x4A), freed outside x/y 1008..2048. */
void ObjHandler_1034(void)
{
    uint32_t o = CUR;
    if (R16(o + 2) & 1) {
        f_anim(o, DEFS12, SCR12, 3);
        AND16(o + 2, 0xFFFE);
        W16(o + 0x14, 0); W16(o + 0x1C, 0x8000);
    }
    if (!FROZEN) {
        if (!(R16(o + 0x12) & 1)) {
            if (R16(o + 0x14) == 0) {
                Petal_Spawn(); Petal_Spawn(); Petal_Spawn(); Petal_Spawn();
                W16(o + 0x14, (f_rnd() & 0x1F) + 0x20);
            } else W16(o + 0x14, R16(o + 0x14) - 1);
            return;
        }
        W32(o + 8, R32(o + 8) + R32(o + 0x30));
        W32(o + 0xC, R32(o + 0xC) - R32(o + 0x34));
        int32_t vy = RS32(o + 0x34);
        W32(o + 0xC, (uint32_t)(RS32(o + 0xC) + FixMul(Sin8(RS16(o + 0x4A)), (vy + (vy < 0)) >> 1)));
        W16(o + 0x4A, R16(o + 0x4A) + 1);
        if (RS32(o + 8) + 0x100000 < 0x04000000 || RS32(o + 8) > 0x06000000 ||
            RS32(o + 0xC) + 0x100000 < 0x04000000 || RS32(o + 0xC) > 0x06000000) { W16(o, 0xFFFF); return; }
    }
    f_spr_cam(o);
    W16(SPR + 0x10, 0x0600);
    W16(SPR + 0x14, R16(o + 0x4A));
    W16(SPR + 0x0E, 0); W16(SPR + 0x0A, 0x22); W16(SPR + 0x0C, 0);
    W32(SPR + 0x18, R16(o + 0x4C) != 0 ? 0xFFFFFF80u : 0x80);
    Anim_Tick(o + 0x58);
}

/* ================================================================ dispatch */
int ObjHandlerF_Call(uint16_t type)
{
    switch (type) {
    case 0x1020: ObjHandler_1020(); break;
    case 0x1021: ObjHandler_1021(); break;
    case 0x1022: ObjHandler_1022(); break;
    case 0x1023: ObjHandler_1023(); break;
    case 0x1024: ObjHandler_1024(); break;
    case 0x1029: ObjHandler_1029(); break;
    case 0x102A: ObjHandler_102A(); break;
    case 0x102B: ObjHandler_102B(); break;
    case 0x102C: ObjHandler_102C(); break;
    case 0x102D: ObjHandler_102D(); break;
    case 0x102E: ObjHandler_102E(); break;
    case 0x102F: ObjHandler_102F(); break;
    case 0x1030: ObjHandler_1030(); break;
    case 0x1031: ObjHandler_1031(); break;
    case 0x1032: ObjHandler_1032(); break;
    case 0x1033: ObjHandler_1033(); break;
    case 0x1034: ObjHandler_1034(); break;
    case 0x1035: ObjHandler_1035(); break;
    case 0x1036: ObjHandler_1036(); break;
    case 0x1037: ObjHandler_1037(); break;
    case 0x1038: ObjHandler_1038(); break;
    case 0x1039: ObjHandler_1039(); break;
    case 0x103A: ObjHandler_103A(); break;
    case 0x103B: ObjHandler_103B(); break;
    case 0x103C: ObjHandler_103C(); break;
    case 0x103D: ObjHandler_103D(); break;
    case 0x103E: ObjHandler_103E(); break;
    case 0x103F: ObjHandler_103F(); break;
    case 0x1040: ObjHandler_1040(); break;
    case 0x1041: ObjHandler_1041(); break;
    case 0x1042: ObjHandler_1042(); break;
    case 0x1043: ObjHandler_1043(); break;
    case 0x1044: ObjHandler_1044(); break;
    case 0x1045: ObjHandler_1045(); break;
    case 0x1046: ObjHandler_1046(); break;
    case 0x1047: ObjHandler_1047(); break;
    case 0x1048: ObjHandler_1048(); break;
    case 0x1049: ObjHandler_1049(); break;
    case 0x104B: ObjHandler_104B(); break;
    case 0x104C: ObjHandler_104C(); break;
    case 0x104D: ObjHandler_104D(); break;
    case 0x104E: ObjHandler_104E(); break;
    case 0x104F: ObjHandler_104F(); break;
    case 0x1050: ObjHandler_1050(); break;
    default: return 0;
    }
    return 1;
}
