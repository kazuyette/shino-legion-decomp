/* Enemy type 0x0022 (stage 2-1): flying fire-breather with an orbit of fireballs.
 * Loader 0x0608C050, handler 0x0608C084 (subtype 0), 0x0608D606 (subtype 1, orbit
 * leader, init 0x0608DB60), 0x0608DE88 (subtype 2, orbit member, init 0x0608E1FC);
 * flame = projectile 0x28 (0x0608D4A8). 2 HP, 300 points.
 * Subtype 0 waits invisible at home (+0x4C/+0x4E px, action 8) until Sho is within
 * 160 px, appears (9) with an orbit leader (subtype 1 at +32 px) which registers three
 * members (subtype 2, angles 0x400/0x800/0xC00 of 0x1000); all four circle the breather
 * at 32 px (+0x4C += 0x14 per frame) and are only active (state +0x48 = 2) while it
 * flies. It flies after Sho (0: X acceleration 0xE00/0x1800, max 1.5 px/frame, turning
 * round through 0xD/0xF; Y towards Sho's chest, max 1.125 px/frame), and when level
 * with him 80..200 px away stops (1) and breathes fire (projectile 0x28: anim 6, then
 * 0xE), then rests (0xB). If Sho is 480 px above/below it, or above Y 0x5E0, it
 * vanishes (10) back home. Hurt -> 2 (sprite rotated by +0x48); killed -> falls (3) or
 * cut down (4: overlay anim 0xC, body anim 5 drifting); it reappears after 480 frames.
 * Killing an orbit member: subtype 1 hands its member list to the next member. */
#include "enemy_more.h"
#include "anim.h"
#include "sprite.h"
#include "objects.h"
#include "player_ext.h"
#include "fixmath.h"
#include "fx.h"

#define T22_DEFS 0x060BF684u
#define T22_SCR  0x060E3C00u
#define OBJ(i)   (OBJ_TABLE + (uint32_t)(int16_t)((int16_t)(i) * 0x78))

void Enemy22_Load(void)                                     /* 0x0608C050 */
{
    Enemy_RegisterChars(0x22, 0x060E35D8u);
    int16_t s = (int16_t)Enemy_FindSlot(0x22);
    Pal_Copy((R16(0x060A9084u + 2u * (uint32_t)s) & 0xFFF) << 4, 0x80, 0x060E37FCu);
}

static void link_state(uint32_t o, uint16_t v)
{
    if (RS16(o + 0x56) != -1) W16(OBJ(RS16(o + 0x56)) + 0x48, v);
}

static void Breather_Init(void)                             /* 0x0608D3C8 */
{
    uint32_t o = O;
    W16(o + 6, 8);
    Anim_Start(o + 0x58, T22_DEFS, T22_SCR, CHAR_BASE(SLOT), RS16(o + 6));
    AND16(o + 2, 0xFFFE);
    OR16(o + 2, 0x100);
    W16(o + 0x16, 2);
    EM_FaceSho(o);
    W16(o + 0x48, 0xFFFF);
    W16(o + 0x4C, R32(o + 8) >> 16);
    W16(o + 0x4E, R32(o + 0xC) >> 16);
    W16(o + 4, 1);
    W16(o + 0x56, 0xFFFF);
    W16(o + 0x14, 0x3C);
    W16(o + 0x1E, 0);
}

/* killed: back home, reappears 480 frames later, no item */
static void Breather_Home(uint32_t o)
{
    Enemy_DeathBurst(o);
    Score_Add(300);
    W16(o + 6, 8);
    W32(o + 8, (uint32_t)((int32_t)RS16(o + 0x4C) << 16));
    W32(o + 0xC, (uint32_t)((int32_t)RS16(o + 0x4E) << 16));
    W32(o + 0x30, 0);
    W32(o + 0x34, 0);
    W16(o + 0x16, 2);
    if (RS16(o + 0x56) != -1) { link_state(o, 4); W16(o + 0x56, 0xFFFF); }
    W16(o + 0x18, 0xFFFF);
    W16(o + 0x1E, 0x1E0);
}

static void Orbit_Sub1(void);
static void Orbit_Sub2(void);

static void fly(uint32_t o)                                 /* action 0 */
{
    if (!(R16(o + 4) & 2) || R16(o + 0x20) == 0) W16(o + 0x14, R16(o + 0x14) - 1);
    else {                                                  /* an orbit member died: back off */
        W16(o + 0x20, R16(o + 0x20) - 1);
        if (R16(o + 0x20) == 0) AND16(o + 4, 0xFFFD);
        W32(o + 0x30, RS32(o + 8) < RS32(PL + 8) ? 0xFFFE8000u : 0x00018000u);
        W32(o + 0x34, 0);
    }
    int32_t x = RS32(o + 8), sx = RS32(PL + 8);
    int16_t f = (int16_t)(R16(o + 0x58) & 3);
    if (x < sx) {
        if (RS32(o + 0x30) < 0) {
            if (sx - x > 0x00500000) {
                W32(o + 0x30, (uint32_t)(RS32(o + 0x30) + 0xE00));
                if (RS32(o + 0x30) >= 0) {
                    if (f == 2) {
                        if (!(R16(o + 2) & 0x8000)) W16(o + 6, 0xF);
                        else { W16(o + 6, 0xD); W16(o + 0x4A, 0xF); }
                    } else W32(o + 0x30, 0xFFFF8000u);
                }
            }
        } else W32(o + 0x30, (uint32_t)(RS32(o + 0x30) + 0x1800));
        if (f == 2 && RS16(o + 0x14) < 1 && EM_Abs(sx - x) < 0x00C80000 &&
            EM_Abs(RS32(PL + 0xC) - RS32(o + 0xC)) < 0x00780000 && EM_Abs(sx - x) > 0x004FFFFF) {
            if (!(R16(o + 2) & 0x8000)) W16(o + 6, 1);
            else { W16(o + 6, 0xD); W16(o + 0x4A, 1); }
            return;
        }
        if (RS32(o + 0x30) > 0x18000) W32(o + 0x30, 0x18000u);
    } else {
        if (RS32(o + 0x30) < 1) W32(o + 0x30, (uint32_t)(RS32(o + 0x30) - 0x1800));
        else if (x - sx > 0x00500000) {
            W32(o + 0x30, (uint32_t)(RS32(o + 0x30) - 0xE00));
            if (RS32(o + 0x30) < 1) {
                if (f == 2) {
                    if (!(R16(o + 2) & 0x8000)) { W16(o + 6, 0xD); W16(o + 0x4A, 0xF); }
                    else W16(o + 6, 0xF);
                } else W32(o + 0x30, 0x8000u);
            }
        }
        if (f == 2 && RS16(o + 0x14) < 1 && EM_Abs(sx - x) < 0x00C80000 &&
            EM_Abs(RS32(PL + 0xC) - RS32(o + 0xC)) < 0x00780000 && EM_Abs(x - sx) > 0x004FFFFF) {
            if (!(R16(o + 2) & 0x8000)) { W16(o + 6, 0xD); W16(o + 0x4A, 1); }
            else W16(o + 6, 1);
            return;
        }
        if (RS32(o + 0x30) < -0x18000) W32(o + 0x30, 0xFFFE8000u);
    }
    if (RS32(o + 0x30) < 1) {
        if (!(R16(o + 2) & 0x8000) && (R16(o + 0x58) & 3) == 1) { OR16(o + 2, 0x8000); W16(o + 0x5A, 0); }
    } else if ((R16(o + 2) & 0x8000) && (R16(o + 0x58) & 3) == 1) { AND16(o + 2, 0x7FFF); W16(o + 0x5A, 0); }
    int32_t ty = (RS32(PL + 0xC) * 2 + RS16(PL + 0x20) * -0x80000) >> 1;     /* Sho's chest */
    if (RS32(o + 0xC) < ty) {
        W32(o + 0x34, (uint32_t)(RS32(o + 0x34) - (RS32(o + 0x34) < 1 ? 0x2C00 : 0x1D00)));
        if (RS32(o + 0x34) < -0x12000) W32(o + 0x34, 0xFFFEE000u);
    } else {
        W32(o + 0x34, (uint32_t)(RS32(o + 0x34) + (RS32(o + 0x34) < 0 ? 0x1D00 : 0x2C00)));
        if (RS32(o + 0x34) > 0x12000) W32(o + 0x34, 0x12000u);
    }
}

static void breathe(uint32_t o)                             /* action 1 */
{
    int stopped = 1;
    if (RS32(o + 0x30) != 0) {
        if (EM_Abs(RS32(o + 0x30)) < 0x1400) W32(o + 0x30, 0);
        else { W32(o + 0x30, (uint32_t)(RS32(o + 0x30) + (RS32(o + 0x30) < 1 ? 0x1400 : -0x1400))); stopped = 0; }
    }
    if (RS32(o + 0x34) != 0) {
        if (EM_Abs(RS32(o + 0x34)) < 0x2000) W32(o + 0x34, 0);
        else { W32(o + 0x34, (uint32_t)(RS32(o + 0x34) + (RS32(o + 0x34) < 1 ? 0x2000 : -0x2000))); stopped = 0; }
    }
    if (!stopped) return;
    if ((R16(o + 0x58) & 3) == 1) {
        AND16(o + 0x58, 0xFFFC); OR16(o + 0x58, 2);
        W16(o + 0x14, 0x78);
        W16(o + 0x48, (uint16_t)Projectile_Spawn(0x28, RS32(o + 8), RS32(o + 0xC), (R16(o + 2) & 0x8000) ? 0x80 : 0, 0));
        if (RS16(o + 0x48) == -1) W16(o + 6, 0);
    } else if ((R16(o + 0x58) & 3) == 2) {
        uint32_t p = PROJECTILES + (uint32_t)((int16_t)RS16(o + 0x48) * 0x40);
        if (R16(p) == 0x28) {
            if (R16(p + 0x20) & 0x8000) {
                if (RS16(p + 0x38) == 6) {
                    int16_t s = (int16_t)Enemy_FindSlot(0x22);
                    (void)s;
                    Anim_Start(p + 0x20, T22_DEFS, T22_SCR, CHAR_BASE(SLOT), 0xE);
                }
                W16(o + 0x48, 0xFFFF);
                AND16(o + 0x58, 0xFFFC);
                W16(o + 6, 0xB);
            }
        } else {
            AND16(o + 0x58, 0xFFFC);
            W16(o + 0x48, 0xFFFF);
            W16(o + 6, 0xB);
        }
    }
}

void EnemyHandler_0022(void)                                /* 0x0608C084 */
{
    uint32_t o = O;
    uint16_t sub = R16(o + 0x12) & 0x1F;
    if (sub == 1) { Orbit_Sub1(); return; }
    if (sub == 2) { Orbit_Sub2(); return; }
    if (!FROZEN && R16(o + 0x1E) != 0) W16(o + 0x1E, R16(o + 0x1E) - 1);
    if (R16(o + 2) & 1) Breather_Init();
    {
        int32_t d = RS32(o + 8) - RS32(CAM_X) + (int32_t)0xFF600000;
        if (d < 0) d = RS32(CAM_X) - RS32(o + 8) + 0x00A00000;
        if (d > 0x017FFFFF) {
            if (RS32(0x060EA618u) < RS32(o + 8) && RS32(o + 8) < RS32(0x060EA620u) && RS16(o + 0x48) == -1) {
                if (!(R16(o + 2) & 8)) return;
                if (R16(o + 6) == 8) return;
            }
            d = RS32(o + 0xC) - RS32(CAM_Y) - 0x00A00000;
            if (d < 0) d = RS32(CAM_Y) - RS32(o + 0xC) + 0x00A00000;
            if (d > 0x00FFFFFF && R16(o + 6) == 8) return;
        }
    }
    uint16_t ovl = 0;
    if (!FROZEN) {
        if (R16(o + 6) != 8 && (int16_t)Enemy_AttackPlayer(2, 0, 0xC000) != 0) EM_PlayerFaceBack(o);
        int16_t a = RS16(o + 6), r = a;
        if (a == 8 || ((a == 9 || a == 10) && (R16(o + 0x58) & 3) != 0) || (a == 9 && R16(o + 0x5A) == 0)) {
            OR16(o + 2, 8); OR16(o + 0x12, 0x4000);                /* not hittable */
        } else if (a == 9 && (R16(o + 0x58) & 3) == 0) {
            AND16(o + 2, 0xFFF7); AND16(o + 0x12, 0xBFFF);
            r = (int16_t)Enemy_CheckPlayerHits(0xC000);
        } else {
            AND16(o + 0x12, 0xBFFF);
            r = (int16_t)Enemy_CheckPlayerHits(0xC000);
        }
        a = RS16(o + 6);
        if ((R16(o + 2) & 8) && (a != 9 || ((R16(o + 0x58) & 3) == 0 && R16(o + 0x5A) != 0)) &&
            (a != 10 || (R16(o + 0x58) & 3) == 0) && a != 8 && a != 3 && a != 4 && a != 2) {
            int16_t k = (r == 2) ? (int16_t)Player_AttackKind() : 4;
            W32(o + 0x30, 0); W32(o + 0x34, 0);
            if (RS16(o + 0x48) != -1) {                             /* cancel the flame */
                uint32_t p = PROJECTILES + (uint32_t)((int16_t)RS16(o + 0x48) * 0x40);
                if (R16(p) == 0x28 && RS16(p + 0x38) == 6) W16(p, 0xFFFF);
            }
            if (RS16(o + 0x16) < 1) {
                if (k == 4 || k == 5) { W16(o + 6, 3); sub_06007CDE(0x206); }
                else {
                    W16(o + 0x14, 0);
                    W16(o + 6, 4);
                    EM_OverlayStart(T22_DEFS, T22_SCR, 0xC, o + 0x50, o + 0x52, o + 0x54);
                    W16(o + 0x48, 0); W16(o + 0x4A, 0);
                    sub_06007CDE(0x206);
                    sub_06007CDE(0x10B);
                }
            } else { W16(o + 0x48, 0); W16(o + 6, 2); }
            Enemy_AnimSwitch();
        }
        if (!(R16(o + 0x58) & 0x8000)) {
            if (R16(o + 6) == 0) {
                int32_t d = EM_Abs(RS32(PL + 0xC) - ((int32_t)RS16(o + 0x4E) << 16));
                if (d > 0x01E00000 || RS32(PL + 0xC) < 0x05E00000) { W16(o + 6, 10); link_state(o, 1); }
            }
            switch (R16(o + 6)) {
            case 0: fly(o); break;
            case 1: breathe(o); break;
            case 2:                                                 /* hurt: tilt */
                if ((R16(o + 0x58) & 3) == 1) {
                    if (!(R16(o + 2) & 0x8000)) { W16(o + 0x48, R16(o + 0x48) - 1); if (RS16(o + 0x48) < 0) W16(o + 0x48, 0); }
                    else { W16(o + 0x48, R16(o + 0x48) + 1); if (RS16(o + 0x48) > 0) W16(o + 0x48, 0); }
                } else if (!(R16(o + 2) & 0x8000)) { W16(o + 0x48, R16(o + 0x48) + 3); if (RS16(o + 0x48) > 0x19) W16(o + 0x48, 0x19); }
                else { W16(o + 0x48, R16(o + 0x48) - 3); if (RS16(o + 0x48) < -0x19) W16(o + 0x48, 0xFFE7); }
                break;
            case 3:
                if ((R16(o + 0x58) & 3) == 1) AND16(o + 2, 0xFEFF);  /* gravity on: falls */
                link_state(o, 1);
                break;
            case 4: W16(o + 0x14, R16(o + 0x14) + 1); link_state(o, 1); break;
            case 8:                                                 /* invisible at home */
                link_state(o, 0);
                if (EM_Abs(RS32(o + 8) - RS32(PL + 8)) > 0x009FFFFF || EM_Abs(RS32(o + 0xC) - RS32(PL + 0xC)) > 0x009FFFFF ||
                    !(R16(o + 4) & 1)) {
                    if (R16(o + 0x1E) != 0) return;
                    OR16(o + 4, 1);
                    return;
                }
                AND16(o + 4, 0xFFFE);
                W16(o + 6, 9);
                Enemy_AnimSwitch();
                if (RS32(o + 8) < RS32(PL + 8)) AND16(o + 2, 0x7FFF); else OR16(o + 2, 0x8000);
                W16(o + 0x56, (uint16_t)Obj_Register(0x22, RS32(o + 8) + 0x00200000, RS32(o + 0xC), 1, 0));
                if (RS16(o + 0x56) != -1) {
                    uint32_t l = OBJ(RS16(o + 0x56));
                    W16(l + 0x48, 0); W16(l + 0x4C, 0); W16(l + 0x4E, R16(ENEMY_CUR_IDX));
                }
                break;
            case 9: case 10: link_state(o, 1); break;
            case 0xD:
                W32(o + 0x30, 0); W32(o + 0x34, 0);
                if (R16(o + 0x4A) == 0xF) W32(o + 0x30, (R16(o + 2) & 0x8000) ? 0xFFFF8000u : 0x8000u);
                break;
            case 0xF: W32(o + 0x30, 0); W32(o + 0x34, 0); break;
            }
        } else {
            switch (RS16(o + 6)) {
            case 2:
                W16(o + 6, 0); AND16(o + 2, 0xFFF7); W16(o + 0x48, 0xFFFF);
                link_state(o, 2);
                break;
            case 3:
                Enemy_DeathBurst(o);
                Score_Add(300);
                W16(o + 6, 8);
                W32(o + 8, (uint32_t)((int32_t)RS16(o + 0x4C) << 16));
                W32(o + 0xC, (uint32_t)((int32_t)RS16(o + 0x4E) << 16));
                W32(o + 0x30, 0); W32(o + 0x34, 0);
                OR16(o + 2, 0x100);
                W16(o + 0x16, 2);
                if (RS16(o + 0x56) != -1) { link_state(o, 4); W16(o + 0x56, 0xFFFF); }
                W16(o + 0x18, 0xFFFF);
                W16(o + 0x1E, 0x1E0);
                return;
            case 9: W16(o + 6, 0); AND16(o + 2, 0xFFF7); link_state(o, 2); break;
            case 10:                                                /* vanished: back home */
                AND16(o + 2, 0xFFF7);
                W16(o + 6, 8);
                W32(o + 8, (uint32_t)((int32_t)RS16(o + 0x4C) << 16));
                W32(o + 0xC, (uint32_t)((int32_t)RS16(o + 0x4E) << 16));
                W32(o + 0x30, 0); W32(o + 0x34, 0);
                W16(o + 0x16, 2);
                OR16(o + 4, 1);
                if (RS16(o + 0x56) != -1) { link_state(o, 4); W16(o + 0x56, 0xFFFF); }
                break;
            case 0xB: case 0xF: W16(o + 6, 0); break;
            case 0xD: W16(o + 6, R16(o + 0x4A)); W16(o + 2, R16(o + 2) ^ 0x8000); break;
            }
        }
        Obj_Integrate(o);
        Enemy_AnimSwitch();
    }
    if (R16(o + 6) == 4) {                                          /* cut down */
        Anim_Start(SCRATCH_ANIM, T22_DEFS, T22_SCR, CHAR_BASE(SLOT), 0xC);
        Enemy_DrawOverlay(SCRATCH_ANIM, (int16_t)(COLOR_BANK(SLOT) + 7), o + 0x50, o + 0x52, o + 0x54);
        ovl = R16(SCRATCH_ANIM);
        W16(o + 0x58, (R16(o + 0x58) & 0xFFFC) | (ovl & 3));
        if (ovl & 0x8000) { Breather_Home(o); Enemy_AnimSwitch(); return; }
        Anim_Start(SCRATCH_ANIM2, T22_DEFS, T22_SCR, CHAR_BASE(SLOT), 5);
        W16(SPR + 2, (uint32_t)((int16_t)((RS32(o + 8) - RS32(CAM_X)) >> 16) + (int16_t)EM_Sdiv(RS16(o + 0x48), 3)));
        W16(SPR + 4, (uint32_t)((int16_t)((RS32(o + 0xC) - RS32(CAM_Y)) >> 16) + (int16_t)EM_Sdiv(RS16(o + 0x4A), 3)));
        W16(SPR + 0x10, R16(o + 2) & 0x8000);
        W16(SPR + 0x0A, (uint16_t)COLOR_BANK(SLOT));
        W16(SPR + 0x0C, 0);
        W16(SPR + 0x0E, ovl & 3);
        Anim_Tick(SCRATCH_ANIM2);
        if (!FROZEN) {
            W16(o + 0x48, (uint16_t)(RS16(o + 0x48) + ((R16(o + 2) & 0x8000) ? 1 : -1)));
            W16(o + 0x4A, R16(o + 0x4A) + 1);
        }
    }
    if (R16(o + 6) == 8 || (R16(o + 6) == 9 && R16(o + 0x5A) == 0)) OR16(o + 0x58, 0x4000);
    else AND16(o + 0x58, 0xBFFF);
    EM_SprBasic(o, COLOR_BANK(SLOT));
    if (R16(o + 6) == 2) { OR16(SPR + 0x10, 0x400); W16(SPR + 0x14, R16(o + 0x48)); }
    { uint16_t a = R16(o + 6); if (a == 3 || a == 4 || a == 9 || a == 10) W16(SPR + 0x0E, R16(o + 0x58) & 3); }
    Anim_Tick(o + 0x58);
}

/* ------------------------------------------------------------ the orbit */

static void orbit_pos(uint32_t o)
{
    uint32_t p = OBJ(RS16(o + 0x4E));
    W32(o + 8, (uint32_t)(RS32(p + 8) + FixMul(0x200000, Cos8(RS16(o + 0x4C) >> 4))));
    W32(o + 0xC, (uint32_t)(RS32(p + 0xC) + FixMul(0x200000, Sin8(RS16(o + 0x4C) >> 4))));
}

static void orbit_draw(uint32_t o)
{
    uint32_t p = OBJ(RS16(o + 0x4E));
    W16(SPR + 2, (uint32_t)((RS32(o + 8) - RS32(CAM_X)) >> 16));
    W16(SPR + 4, (uint32_t)((RS32(o + 0xC) - RS32(CAM_Y)) >> 16));
    W16(SPR + 0x10, (R16(o + 2) & 0x8000) | 0x200);
    W32(SPR + 0x18, 0);
    W16(SPR + 0x0A, (uint16_t)(COLOR_BANK(SLOT) + 5));
    W16(SPR + 0x0C, 0);
    uint16_t f = 1;
    if (R16(o + 0x48) == 1) { f = R16(p + 0x58) & 3; if (f == 0) f = 1; }
    W16(SPR + 0x0E, f);
    Anim_Tick(o + 0x58);
}

/* common part of subtypes 1/2: 1 = go on, 0 = return (asleep / state 0) */
static int orbit_awake(uint32_t o)
{
    int32_t d = RS32(o + 8) - RS32(CAM_X) + (int32_t)0xFF600000;
    if (d < 0) d = RS32(CAM_X) - RS32(o + 8) + 0x00A00000;
    uint16_t st = R16(o + 0x48);
    if (d >= 0x01000000 && R16(OBJ(RS16(o + 0x4E)) + 6) == 8 && st != 3 && st != 4) return 0;
    return st != 0;
}

static void orbit_spin(uint32_t o)
{
    W16(o + 0x58, (R16(o + 0x58) & 0xBFFF) | (R16(OBJ(RS16(o + 0x4E)) + 0x58) & 0x4000));
    W16(o + 0x4C, R16(o + 0x4C) + 0x14);
    if (RS16(o + 0x4C) > 0xFFF) W16(o + 0x4C, R16(o + 0x4C) - 0x1000);
}

static void orbit_combat(uint32_t o, int sfx)
{
    if ((int16_t)Enemy_AttackPlayer(1, 0, 0xC000) != 0) EM_PlayerFaceBack(o);
    if ((int16_t)Enemy_CheckPlayerHits(0) != 0 && sfx) sub_06007CDE(0x201);
}

static void OrbitLeader_Init(void)                          /* 0x0608DB60 */
{
    uint32_t o = O;
    W16(o + 6, 7);
    Anim_Start(o + 0x58, T22_DEFS, T22_SCR, CHAR_BASE(SLOT), RS16(o + 6));
    AND16(o + 2, 0xFFFE);
    OR16(o + 2, 0x100);
    W16(o + 0x16, 1);
    W16(o + 4, 0);
    OR16(o + 2, R16(OBJ(RS16(o + 0x4E)) + 2) & 0x8000);
    orbit_pos(o);
    for (uint32_t k = 0; k < 3; k++) {
        int s = Obj_Register(0x22, RS32(o + 8), RS32(o + 0xC), 2, 0);
        W16(o + 0x50 + 2 * k, (uint16_t)s);
        if ((int16_t)s == -1) continue;
        uint32_t m = OBJ(s);
        W16(m + 0x48, 0);
        W16(m + 0x4A, R16(ENEMY_CUR_IDX));
        W16(m + 0x4C, (uint16_t)(0x400 * (k + 1)));
        W16(m + 0x4E, R16(o + 0x4E));
        W16(m + 0x56, (uint16_t)k);
    }
}

static void Orbit_Sub1(void)                                /* 0x0608D606 */
{
    uint32_t o = O;
    if (R16(o + 2) & 1) OrbitLeader_Init();
    if (!orbit_awake(o)) return;
    if (!FROZEN) {
        uint32_t p = OBJ(RS16(o + 0x4E));
        if (R16(o + 0x48) == 2) orbit_combat(o, 1);
        if (R16(p + 6) != 8) orbit_pos(o);
        if (R16(o + 0x48) != 3)
            for (uint32_t k = 0; k < 3; k++)
                if (RS16(o + 0x50 + 2 * k) != -1) W16(OBJ(RS16(o + 0x50 + 2 * k)) + 0x48, R16(o + 0x48));
        if (R16(o + 2) & 8) W16(o + 0x48, 3);
        uint16_t st = R16(o + 0x48);
        if (st == 1 || st == 2) orbit_spin(o);
        else if (st == 3 || st == 4) {                              /* destroyed: hand over */
            W16(o, 0xFFFF);
            W16(p + 0x56, 0xFFFF);
            W16(p + 0x20, 0x1E);
            OR16(p + 4, 2);
            int16_t k = 0;
            while (k < 3 && RS16(o + 0x50 + 2 * (uint32_t)k) == -1) k++;
            if (k >= 3) return;
            int16_t nl = RS16(o + 0x50 + 2 * (uint32_t)k);
            uint32_t n = OBJ(nl);
            W16(p + 0x56, (uint16_t)nl);
            W16(n + 0x50, R16(o + 0x50)); W16(n + 0x52, R16(o + 0x52)); W16(n + 0x54, R16(o + 0x54));
            W16(n + 0x12, (R16(n + 0x12) & 0xFFE0) | 1);              /* becomes the leader */
            W16(n + 0x50 + 2 * (uint32_t)k, 0xFFFF);
            for (int16_t j = (int16_t)(k + 1); j < 3; j++)
                if (RS16(o + 0x50 + 2 * (uint32_t)j) != -1) W16(OBJ(RS16(o + 0x50 + 2 * (uint32_t)j)) + 0x4A, (uint16_t)nl);
            return;
        }
    }
    orbit_draw(o);
}

static void OrbitMember_Init(void)                          /* 0x0608E1FC */
{
    uint32_t o = O;
    W16(o + 6, 7);
    Anim_Start(o + 0x58, T22_DEFS, T22_SCR, CHAR_BASE(SLOT), RS16(o + 6));
    AND16(o + 2, 0xFFFE);
    OR16(o + 2, 0x100);
    W16(o + 0x16, 1);
    W16(o + 4, 0);
    OR16(o + 2, R16(OBJ(RS16(o + 0x4E)) + 2) & 0x8000);
    orbit_pos(o);
}

static void Orbit_Sub2(void)                                /* 0x0608DE88 */
{
    uint32_t o = O;
    if (R16(o + 2) & 1) OrbitMember_Init();
    if (!orbit_awake(o)) return;
    if (!FROZEN) {
        uint32_t p = OBJ(RS16(o + 0x4E));
        if (R16(o + 0x48) == 2) orbit_combat(o, 0);
        if (R16(p + 6) != 8) orbit_pos(o);
        if (R16(o + 2) & 8) W16(o + 0x48, 3);
        uint16_t st = R16(o + 0x48);
        if (st == 0) return;
        if (st == 1 || st == 2) orbit_spin(o);
        else if (st == 3 || st == 4) {
            W16(p + 0x20, 0x1E);
            OR16(p + 4, 2);
            W16(OBJ(RS16(o + 0x4A)) + 0x50 + 2u * (uint32_t)RS16(o + 0x56), 0xFFFF);
            W16(o, 0xFFFF);
            return;
        }
    }
    orbit_draw(o);
}

/* Projectile 0x28 (0x0608D4A8): the flame (anim 6 then 0xE, set by the breather),
 * mirrored by its angle, hurts Sho for 1; freed when anim 0xE ends. */
void Proj_Flame28(uint32_t e)
{
    int16_t s = (int16_t)Enemy_FindSlot(0x22);
    if (R16(e + 4) & 1) {
        Anim_Start(e + 0x20, T22_DEFS, T22_SCR, CHAR_BASE(s), 6);
        AND16(e + 4, 0xFFFE);
        W16(e + 0x12, 0x7FFF);
    }
    if (!FROZEN) {
        AND16(e + 4, 0x7FFF);
        if (R16(e + 0x14) > 0x3F) OR16(e + 4, 0x8000);
        Projectile_Collide(1, 0xC000);
        if ((R16(e + 0x20) & 0x8000) && R16(e + 0x38) == 0xE) { W16(e, 0xFFFF); return; }
    }
    if (R16(e) == 0xFFFF) return;
    W16(SPR + 2, (uint32_t)((RS32(e + 8) - RS32(CAM_X)) >> 16));
    W16(SPR + 4, (uint32_t)((RS32(e + 0xC) - RS32(CAM_Y)) >> 16));
    W16(SPR + 0x0A, (uint16_t)(COLOR_BANK(s) + 6));
    W16(SPR + 0x10, (R16(e + 4) & 0x8000) | 0x200);
    W32(SPR + 0x18, 1);
    W16(SPR + 0x0C, 0);
    W16(SPR + 0x0E, R16(e + 0x20) & 3);
    Anim_Tick(e + 0x20);
}
