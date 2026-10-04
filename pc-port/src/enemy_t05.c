/* Enemy type 0x0005 (stage 3-1): agile ninja. Loader 0x06098130, handler 0x06098164,
 * init 0x06098CEE, action setter 0x06098FC0, dodge test 0x06098DC0 / 0x06098EBE,
 * decision 0x06098EE0. 3 HP, 200 points.
 * It stands facing Sho; when a shuriken is coming or Sho swings at it from the front
 * within 80x40 px it dodges (backward leap 0x11 when Sho attacked, otherwise a leap
 * over the shuriken, action 8). Otherwise, when the timer +0x14 runs out, it picks from
 * the distance (0x06098EE0, random bit 1): < 32 px slash (0x10), < 80 px slash (0xB)
 * or leaping slash (0x13), < 160 px run (1) or leaping slash, < 320 px jump at Sho (2,
 * speed (dx + 8 px) / 56 per frame, +0x4C/+0x4E = its 16.16 parts), else wait.
 * Subtype 1 starts with +4 bit 3 set (waits until Sho is within 240x110 px).
 * Actions: 0 stand, 1 run, 2 jump, 3 land, 4 crouch, 5 hop, 6/7 turn, 8 leap over,
 * 9 fall, 10 land, 0xB/0x10 slash, 0x11/0x12 back leap, 0x13 leaping slash (3 phases),
 * 0xC hurt, 0xD knocked away, 0xE cut down (anim 0xF drifting away). */
#include "enemy_more.h"
#include "anim.h"
#include "sprite.h"
#include "collision.h"
#include "player_ext.h"
#include "fx.h"

#define T5_DEFS 0x060C2460u
#define T5_SCR  0x060E58C0u

void Enemy5_Load(void)                                      /* 0x06098130 */
{
    Enemy_RegisterChars(5, 0x060E5364u);
    int16_t s = (int16_t)Enemy_FindSlot(5);
    Pal_Copy((R16(0x060A9084u + 2u * (uint32_t)s) & 0xFFF) << 4, 16, 0x060E5558u);
}

/* 0x06051432 (local copy of objhandlers_f.c's ObjF_CarryListAdd): enemy slot list
 * 0x060F2250 (count 0x060F227E, max 23) carried by the 0x1021 lanterns */
static void carry_list_add(int16_t slot)
{
    int16_t n = RS16(0x060F227Eu);
    if (n >= 23) return;
    W16(0x060F2250u + 2u * (uint32_t)n, (uint16_t)slot);
    W16(0x060F227Eu, (uint32_t)(n + 1));
}

/* 0x06098FC0: switch to action a (with its side effects) */
static void Ninja5_Set(int16_t a)
{
    uint32_t o = O;
    if (a == 0) {
        if (RS32(o + 0x34) == 0) { W16(o + 0x14, 10); W16(o + 6, 0); return; }
        a = 9;                                                  /* still in the air: fall */
    }
    switch (a) {
    case 1:
        if (RS32(o + 8) < RS32(PL + 8)) AND16(o + 2, 0x7FFF); else OR16(o + 2, 0x8000);
        W16(o + 6, 1);
        return;
    case 5:
        W32(o + 0x34, 0x00090000u);
        sub_06007CDE(0x126);
        break;
    case 10:
        W32(o + 0x30, 0);
        W16(o + 6, 10);
        return;
    case 0x11:
        W32(o + 0x30, (R16(o + 2) & 0x8000) ? 0x00048000u : 0xFFFB8000u);
        W32(o + 0x34, 0x00040000u);
        sub_06007CDE(0x126);
        break;
    case 2: case 3: case 4: case 6: case 7: case 8: case 9: case 0xB: case 0xC: case 0xD:
    case 0xE: case 0xF: case 0x10: case 0x12: case 0x13:
        break;
    case 1000: W16(o + 6, 8); OR16(o + 4, 1); return;            /* leap over a wall */
    case 1001: W16(o + 6, 2); OR16(o + 4, 2); return;            /* jump at Sho */
    case 1002: Ninja5_Set(0); OR16(o + 4, 8); return;            /* (subtype 1) wait */
    default: W16(o + 0x14, 3); W16(o + 6, 0); return;
    }
    W16(o + 6, (uint16_t)a);
}

static void Ninja5_Init(void)                               /* 0x06098CEE */
{
    uint32_t o = O;
    Ninja5_Set((R16(o + 0x12) & 0x1F) == 0 ? 0x13 : 1002);
    W16(o + 0x16, 3);
    Anim_Start(o + 0x58, T5_DEFS, T5_SCR, CHAR_BASE(SLOT), RS16(o + 6));
    AND16(o + 2, 0xFFFE);
    W16(o + 0x1E, 0);
    W16(o + 0x20, 0);
    if (RS32(o + 8) < RS32(PL + 8)) AND16(o + 2, 0x7FFF); else OR16(o + 2, 0x8000);
}

/* 0x06098DC0: a shuriken ahead, or Sho attacking from the front close by (+4 bit 2) */
static int Ninja5_Threat(void)
{
    uint32_t o = O;
    int32_t d = (R16(o + 2) & 0x8000) ? (int32_t)0xFF600000 : 0x00A00000;
    int r = Projectile_InBox(RS32(o + 8) + d, RS32(o + 0xC) + (int32_t)0xFFC00000, (d >> 8) + RS32(o + 8), RS32(o + 0xC)) != 0;
    if ((R16(PL + 2) & 0x8000) != (R16(o + 2) & 0x8000) && EM_Abs(RS32(o + 8) - RS32(PL + 8)) < 0x00500000 &&
        EM_Abs(RS32(o + 0xC) - RS32(PL + 0xC)) < 0x00280000 && (int16_t)Player_AttackKind() != 6) {
        r = 1;
        OR16(o + 4, 4);
    }
    return r;
}

static void Ninja5_Dodge(void)                              /* 0x06098EBE */
{
    uint32_t o = O;
    if (R16(o + 4) & 4) { AND16(o + 4, 0xFFFB); Ninja5_Set(0x11); }
    else Ninja5_Set(8);
}

static int16_t Ninja5_Decide(void)                          /* 0x06098EE0 */
{
    uint32_t o = O;
    uint32_t r = (uint32_t)sub_0602DDD2();
    int32_t d = EM_Abs(RS32(PL + 8) - RS32(o + 8));
    int bit = (int)((r & 2) >> 1);
    if (d < 0x00200000) return 0x10;
    if (d < 0x00500000) {
        if (bit == 0) return EM_Abs(RS32(PL + 0xC) - RS32(o + 0xC)) < 0x00280000 ? 0xB : 0x13;
        return 0x13;
    }
    if (d < 0x00A00000) return bit == 0 ? 1 : 0x13;
    if (d < 0x01400000) return 1001;
    return 0;
}

/* jump at Sho: (|dx| + 8 px) / div per frame (A.BIN 0x060092BC), mirrored */
static int32_t leap_speed(uint32_t o, int32_t div)
{
    int32_t v = EM_Sdiv(EM_Abs(RS32(PL + 8) - RS32(o + 8)) + 0x80000, div);
    return (R16(o + 2) & 0x8000) ? -v : v;
}

void EnemyHandler_0005(void)                                /* 0x06098164 */
{
    uint32_t o = O;
    if (R16(o + 2) & 1) Ninja5_Init();
    if (EM_Far(o, 0x00A00000) && !(R16(o + 2) & 0x4008)) return;
    if (!FROZEN) {
        if ((int16_t)Enemy_AttackPlayer(1, 0, 0xC000) != 0) EM_PlayerFaceBack(o);
        if (!(R16(o + 2) & 0x4000)) W32(o + 0x30, 0);
        int16_t r = (int16_t)Enemy_CheckPlayerHits(0xC000);
        uint16_t a = R16(o + 6);
        if ((R16(o + 2) & 8) && a != 0xD && a != 0xE && a != 0xC) {
            int16_t k = (r == 2) ? (int16_t)Player_AttackKind() : 4;
            if (RS16(o + 0x16) < 1) {
                if (k == 4 || k == 5) Ninja5_Set(0xD);
                else { Ninja5_Set(0xE); W16(o + 0x48, 0); W16(o + 0x4A, 0); }
                sub_06007CDE(0x235);
            } else Ninja5_Set(0xC);
        }
        if (!(R16(o + 0x58) & 0x8000)) {
            switch (R16(o + 6)) {
            case 0:
                EM_FaceSho(o);
                if (R16(o + 4) & 8) {                               /* subtype 1: wait for Sho */
                    if (!(EM_Abs(RS32(o + 8) - RS32(PL + 8)) < 0x00F00000 &&
                          EM_Abs(RS32(o + 0xC) - RS32(PL + 0xC)) < 0x006E0000)) break;
                    AND16(o + 4, 0xFFF7);
                }
                if (Ninja5_Threat()) Ninja5_Dodge();
                else {
                    int16_t t = (int16_t)(RS16(o + 0x14) - 1);
                    W16(o + 0x14, (uint16_t)t);
                    if (t < 0) Ninja5_Set(Ninja5_Decide());
                }
                break;
            case 1:                                                 /* run */
                W32(o + 0x30, (R16(o + 2) & 0x8000) ? 0xFFFD8000u : 0x00028000u);
                if (RS32(o + 0x34) != 0) { Ninja5_Set(9); break; }
                if (Ninja5_Threat()) Ninja5_Dodge();
                else if (R16(o + 0x10) & 8) { AND16(o + 2, 0x7FFF); Ninja5_Set(1000); }
                else if (R16(o + 0x10) & 4) { OR16(o + 2, 0x8000); Ninja5_Set(1000); }
                break;
            case 2:                                                 /* jump at Sho */
                if ((R16(o + 0x58) & 3) == 1) {
                    AND16(o + 0x58, 0xFFFC);
                    W32(o + 0x34, 0x00090000u);
                    sub_06007CDE(0x126);
                    if (!(R16(o + 4) & 2)) {
                        W32(o + 0x30, (uint32_t)leap_speed(o, 56));
                        W16(o + 0x4C, R32(o + 0x30) >> 16);
                        W16(o + 0x4E, R32(o + 0x30) & 0xFFFF);
                    } else W32(o + 0x30, (R16(o + 2) & 0x8000) ? 0xFFFD2493u : 0x0002DB6Du);
                } else if ((R16(o + 0x58) & 3) == 2 && RS32(o + 0x34) < 1) Ninja5_Set(3);
                break;
            case 3:
                if ((R16(o + 0x58) & 3) == 1 && !(R16(o + 2) & 0x4000)) Ninja5_Set(4);
                break;
            case 5:
                W32(o + 0x30, (uint32_t)((int32_t)RS16(o + 0x4C) * 0x10000 + (uint32_t)R16(o + 0x4E)));
                if ((R16(o + 0x58) & 3) == 1 && RS32(o + 0x34) < 1) Ninja5_Set(6);
                break;
            case 6:
                if ((R16(o + 0x58) & 3) == 1 && !(R16(o + 2) & 0x4000)) Ninja5_Set(7);
                break;
            case 8:                                                 /* leap over */
                if (!(R16(o + 4) & 1)) {
                    if ((R16(o + 0x58) & 3) == 1) {
                        AND16(o + 0x58, 0xFFFC);
                        W32(o + 0x34, 0x00080000u);
                        sub_06007CDE(0x126);
                    } else if ((R16(o + 0x58) & 3) == 2 && RS32(o + 0x34) < 1) { Ninja5_Set(9); break; }
                    /* (0x06098828: a contradictory flags test, never true) */
                } else {                                            /* over a wall */
                    W32(o + 0x30, (R16(o + 2) & 0x8000) ? 0xFFFF0000u : 0x00010000u);
                    if ((R16(o + 0x58) & 3) == 1) {
                        AND16(o + 0x58, 0xFFFC);
                        W32(o + 0x34, 0x00080000u);
                        sub_06007CDE(0x126);
                    } else if ((R16(o + 0x58) & 3) == 2 && RS32(o + 0x34) < 1) { Ninja5_Set(9); break; }
                    if (RS32(o + 0x28) != RS32(o + 8)) {
                        if (R16(o + 2) & 0x4000) { AND16(o + 0x58, 0xFFFC); OR16(o + 0x58, 2); }
                        AND16(o + 4, 0xFFFE);
                    }
                }
                break;
            case 9:
                if ((R16(o + 0x58) & 3) == 1 && !(R16(o + 2) & 0x4000)) Ninja5_Set(10);
                break;
            case 0xD: case 0xE:
                if (R16(o + 2) & 0x4000) { AND16(o + 0x58, 0x7FFF); W16(o + 0x5A, 0); W16(o + 0x72, 0); }
                break;
            case 0x10:
                W32(o + 0x30, (R16(o + 2) & 0x8000) ? 0x00028000u : 0xFFFD8000u);
                break;
            case 0x11:
                if ((R16(o + 0x58) & 3) == 1 && !(R16(o + 2) & 0x4000)) Ninja5_Set(0x12);
                break;
            case 0x13:                                              /* leaping slash */
                if (R16(o + 0x5A) < 3 && R16(o + 0x5A) != 0 && RS32(o + 0x34) < 0) Ninja5_Set(9);
                else if ((R16(o + 0x58) & 0xF) == 1) {
                    AND16(o + 0x58, 0xFFF0);
                    W32(o + 0x34, 0x00090000u);
                    sub_06007CDE(0x126);
                    W32(o + 0x30, (uint32_t)leap_speed(o, 56));
                    W16(o + 0x4C, R32(o + 0x30) >> 16);
                    W16(o + 0x4E, R32(o + 0x30) & 0xFFFF);
                } else if ((R16(o + 0x58) & 0xF) == 2) {
                    AND16(o + 0x58, 0xFFF0);
                    W32(o + 0x34, 0x00014000u);
                    W32(o + 0x30, 0);
                } else if ((R16(o + 0x58) & 0xF) == 3) {
                    W32(o + 0x30, (uint32_t)leap_speed(o, 26));
                    if (RS32(o + 0x34) < 1) Ninja5_Set(3);
                }
                break;
            }
        } else {
            switch (RS16(o + 6)) {
            case 4:
                if (!(R16(o + 4) & 2)) Ninja5_Set(5);
                else { AND16(o + 4, 0xFFFD); Ninja5_Set(0); }
                break;
            case 7: W16(o + 2, R16(o + 2) ^ 0x8000); Ninja5_Set(0); break;
            case 10:
                if (!(R16(o + 4) & 1)) Ninja5_Set(0);
                else {
                    AND16(o + 4, 0xFFFE);
                    W16(o + 2, R16(o + 2) ^ 0x8000);
                    W32(o + 0x30, (R16(o + 2) & 0x8000) ? 0xFFFE0000u : 0x00020000u);
                    Ninja5_Set(8);
                }
                break;
            case 0xC:
                if (!(R16(o + 2) & 0x4000)) { AND16(o + 2, 0xFFF7); Ninja5_Set(0); }
                else { AND16(o + 0x58, 0x7FFF); W16(o + 0x5A, 0); W16(o + 0x72, 0); }
                break;
            case 0xD: case 0xE:
                Enemy_DeathBurst(o); Score_Add(200); W16(o, 0xFFFF);
                return;
            default: Ninja5_Set(0); break;
            }
        }
        W16(o + 0x1E, 3);
        W16(o + 0x20, 8);
        Obj_Integrate(o);
        EM_ClampX(o);
        Obj_CollideMap(o, RS16(o + 0x1E), RS16(o + 0x20));
        if (RS32(o + 0xC) > 0x07800000) { W16(o, 0xFFFF); return; }   /* fell out of the world */
        AND16(o + 0x10, 0xDFFF);
        carry_list_add((int16_t)R16(ENEMY_CUR_IDX));
        Enemy_AnimSwitch();
    }
    if (R16(o + 6) == 0xE) {                                        /* cut down: drifting body */
        Anim_Start(SCRATCH_ANIM, T5_DEFS, T5_SCR, CHAR_BASE(SLOT), 0xF);
        W16(SPR + 2, (uint32_t)((int16_t)((RS32(o + 8) - RS32(CAM_X)) >> 16) + (RS16(o + 0x48) >> 1)));
        W16(SPR + 4, (uint32_t)((int16_t)((RS32(o + 0xC) - RS32(CAM_Y)) >> 16) + (RS16(o + 0x4A) >> 1)));
        W16(SPR + 0x10, R16(o + 2) & 0x8000);
        W16(SPR + 0x0A, (uint16_t)COLOR_BANK(SLOT));
        W16(SPR + 0x0C, 0);
        W16(SPR + 0x0E, R16(o + 0x58) & 3);
        Anim_Tick(SCRATCH_ANIM);
        if (!FROZEN) {
            W16(o + 0x48, (uint16_t)(RS16(o + 0x48) + ((R16(o + 2) & 0x8000) ? 1 : -1)));
            W16(o + 0x4A, R16(o + 0x4A) + 1);
        }
    }
    EM_SprBasic(o, COLOR_BANK(SLOT));
    if (R16(o + 6) == 0xD || R16(o + 6) == 0xE) W16(SPR + 0x0E, R16(o + 0x58) & 3);
    Anim_Tick(o + 0x58);
}
