/* Soldier (type 0x0001) subtypes 10 and 12, ported from SHINOBI.PPB.
 *  10 (0x060927EC): respawning ninja. Hidden (action 0x13) at its home position
 *     (+0x50/+0x52, px) until Sho comes within 0xA0 px, then drops in from the top
 *     of the screen 8 px beside him; once killed it goes back into hiding and waits
 *     for Sho to move 0x140 px away before it can come back (+4 = armed).
 *  12 (0x0608F58C): ninja riding a floating platform (spawned by ObjHandler_1004,
 *     carried through +0x3C). Hidden until Sho comes within 0xC0 px, then leaps out
 *     (action 0x1B) and throws in the air (0x0B). Respawns the same way; on STAGE 8
 *     it is removed for good instead (and when 0x200 px off screen).
 * Both keep the overlay anim state in +0x4C/+0x54/+0x56 (+0x50/+0x52 = home). */
#include "enemy_int.h"
#include "enemy_soldier_sub.h"
#include "anim.h"
#include "collision.h"

void Obj_Integrate(uint32_t o);      /* physics.c, 0x060452A4 */

static int32_t iabs(int32_t v) { return v < 0 ? -v : v; }

/* 0x06092DA4: hide the sprite while waiting (0x13) or at the end of anim 0x11. */
static void Soldier_HideCheck(void)
{
    uint32_t o = O;
    if (R16(o + 6) == 0x13) OR16(o + 0x58, 0x4000);
    else if (R16(o + 6) == 0x11 && R16(o + 0x5A) == 0) OR16(o + 0x58, 0x4000);
    else AND16(o + 0x58, 0xBFFF);
}

/* |x - camX - 160| (16.16), as computed by the originals. */
static int32_t screen_dx(uint32_t o)
{
    int32_t d = RS32(o + 8) - RS32(CAM_X) + (int32_t)0xFF600000;
    if (d < 0) d = RS32(CAM_X) - RS32(o + 8) + 0x00A00000;
    return d;
}

static int on_screen(uint32_t o)
{
    if (screen_dx(o) >= 0x01000000) return 0;
    int32_t d = RS32(o + 0xC) - RS32(CAM_Y) + (int32_t)0xFF600000;
    if (d < 0) d = RS32(CAM_Y) - RS32(o + 0xC) + 0x00A00000;
    return d < 0x01000000;
}

/* back into hiding at the home position (common tail of the death paths) */
static void back_home(uint32_t o)
{
    W32(o + 8, (uint32_t)(RS16(o + 0x50) * 0x10000));
    W32(o + 0xC, (uint32_t)(RS16(o + 0x52) * 0x10000));
    W16(o + 0x16, 1);
    W16(o + 6, 0x13);
    W16(o + 4, 0);
}

/* cut-down overlay (action 0x0F): 1 when it has finished playing */
static int overlay_done(uint32_t o)
{
    Anim_Start(SCRATCH_ANIM, 0x060BFFB0u, 0x060E43D4u, CHAR_BASE(SLOT), 0x10);
    Enemy_DrawOverlay(SCRATCH_ANIM, (int16_t)(COLOR_BANK(SLOT) + 1), o + 0x4C, o + 0x54, o + 0x56);
    return (R16(SCRATCH_ANIM) & 0x8000) != 0;
}

/* ------------------------------------------------------------ subtype 10 */

static void Soldier0A_Init(void)                             /* 0x06092BB8 */
{
    uint32_t o = O;
    W16(o + 6, 0x13);
    Anim_Start(o + 0x58, 0x060BFFB0u, 0x060E43D4u, CHAR_BASE(SLOT), RS16(o + 6));
    AND16(o + 2, 0xFFFE);
    W16(o + 0x16, 1);
    W16(o + 4, 1);
    W16(o + 0x50, R32(o + 8) >> 16);
    W16(o + 0x52, R32(o + 0xC) >> 16);
    if (RS32(o + 8) < RS32(0x060EA2BCu)) OR16(o + 2, 0x8000);
    else AND16(o + 2, 0x7FFF);
}

void Soldier_Sub0A(void)                                     /* 0x060927EC */
{
    uint32_t o = O;
    if (R16(o + 2) & 1) Soldier0A_Init();
    if (!on_screen(o) && (!(R16(o + 2) & 0x4008) || R16(o + 6) == 0x13)) {
        if (R16(o + 6) == 0x13) return;
        W16(o + 6, 0);
        W16(o + 0x14, 3);
        W16(o + 4, 1);
        Enemy_AnimSwitch();
        return;
    }
    if (!FROZEN) {
        int r = Soldier_CheckHits();
        uint16_t a = R16(o + 6);
        if ((R16(o + 2) & 8) && a != 0x13 && a != 0x0E && a != 0x0F && a != 0x0D)
            Soldier_OnHit(r, o + 0x4C, o + 0x54, o + 0x56);
        if (R16(o + 0x58) & 0x8000) {
            if (R16(o + 6) == 0x0E) {                               /* knocked away: gone */
                Score_Add(100);
                Enemy_DeathBurst(o);
                back_home(o);
                W16(o + 0x18, 0xFFFF);                              /* no more item */
            } else if (Soldier_AnimDone(RS16(o + 0x4A))) return;
        } else if (R16(o + 6) == 0x13) {                            /* hidden */
            int32_t d = iabs(RS32(o + 8) - RS32(PL + 8));
            if (R16(o + 4) == 0) {                                  /* re-arm when Sho is far */
                if (d >= RS32(0x060C0940u)) W16(o + 4, 1);
                return;
            }
            if (d >= RS32(0x060C0934u)) return;
            if (RS32(o + 8) < RS32(PL + 8)) {                       /* drop in beside Sho */
                OR16(o + 2, 0x8000);
                W32(o + 8, (uint32_t)(RS32(PL + 8) + RS32(0x060C0938u)));
            } else {
                AND16(o + 2, 0x7FFF);
                W32(o + 8, (uint32_t)(RS32(PL + 8) - RS32(0x060C0938u)));
            }
            W32(o + 0xC, R32(CAM_Y));
            W32(o + 0x34, R32(0x060C093Cu));
            W16(o + 6, 0);
            AND16(o + 2, 0xFFF7);
        } else {
            Soldier_Think(o + 0x4A);
        }
        if (R16(o + 6) != 0x11 && R16(o + 6) != 0x13) Soldier_Physics();
        Enemy_AnimSwitch();
    }
    if ((R16(o + 0x58) & 3) == 1 && R16(o + 6) == 0x0F && overlay_done(o)) {
        Score_Add(100);
        Enemy_DeathBurst(o);
        back_home(o);
        W16(o + 0x18, 0xFFFF);
        Enemy_AnimSwitch();
    }
    Soldier_HideCheck();
    Soldier_Draw(R16(o + 0x56));
}

/* ------------------------------------------------------------ subtype 12 */

static void Soldier0C_Init(void)                             /* 0x0608F4A0 */
{
    uint32_t o = O;
    W16(o + 6, 0x13);
    if (STAGE == 8) W16(o + 6, 0x0B);                               /* already in the air */
    Anim_Start(o + 0x58, 0x060BFFB0u, 0x060E43D4u, CHAR_BASE(SLOT), RS16(o + 6));
    AND16(o + 2, 0xFFFE);
    W16(o + 0x16, 1);
    W16(o + 4, 1);
    W16(o + 0x14, 0);
    W16(o + 0x50, R32(o + 8) >> 16);
    W16(o + 0x52, R32(o + 0xC) >> 16);
    OR16(o + 2, 0x4000);
    if (STAGE != 8) return;
    AND16(o + 2, 0x7FFF);
    if (RS32(0x060EA2BCu) < RS32(o + 8)) OR16(o + 2, 0x8000);
}

/* common death tail of subtype 12 */
static void sub0c_killed(uint32_t o)
{
    Score_Add(100);
    Enemy_DeathBurst(o);
    back_home(o);
    W16(o + 0x14, 0);
    W16(o + 0x18, 0xFFFF);
}

void Soldier_Sub0C(void)                                     /* 0x0608F58C */
{
    uint32_t o = O;
    if (R16(o + 2) & 1) Soldier0C_Init();
    if (STAGE == 8 && screen_dx(o) > 0x02000000) { W16(o, 0xFFFF); return; }
    if (!on_screen(o) && (!(R16(o + 2) & 0x4008) || R16(o + 6) == 0x13)) {
        W32(o + 0xC, (uint32_t)(RS16(o + 0x52) * 0x10000));
        W16(o + 6, 0x13);
        W16(o + 4, 1);
        Enemy_AnimSwitch();
        return;
    }
    if (!FROZEN) {
        int r = Soldier_CheckHits();
        uint16_t a = R16(o + 6);
        if ((R16(o + 2) & 8) && a != 0x13 && a != 0x11 && a != 0x12 && a != 0x0E &&
            a != 0x0F && a != 0x1B && a != 0x0D)
            Soldier_OnHit(r, o + 0x4C, o + 0x54, o + 0x56);
        if (R16(o + 0x58) & 0x8000) {
            if (R16(o + 6) == 0x0E) {                               /* knocked away */
                sub0c_killed(o);
                if (STAGE == 8) { W16(o, 0xFFFF); return; }
            } else if (Soldier_AnimDone(RS16(o + 0x4A))) return;
        } else {
            switch (R16(o + 6)) {
            case 0x0F:
                if (R16(o + 2) & 0x4000) { W16(o + 0x5A, 0); W16(o + 0x72, 0); }
                break;
            case 0x13:                                              /* hidden on the platform */
                if (!(R16(o + 4) & 1)) return;
                if (iabs(RS32(o + 8) - RS32(PL + 8)) >= 0x00C00000) return;
                W16(o + 6, 0x1B);                                   /* leap out */
                W16(o + 0x14, 0);
                AND16(o + 2, 0x7FFF);
                if (RS32(PL + 8) < RS32(o + 8)) OR16(o + 2, 0x8000);
                W32(o + 0x34, 0x00070000u);
                break;
            case 0x1B:
                if (R16(o + 0x58) & 1) {
                    if (RS32(o + 0x34) < 0) {                       /* top of the leap: throw */
                        W16(o + 6, 0x0B);
                        AND16(o + 0x58, 0xFFFE);
                        AND16(o + 2, 0xFFF7);
                        W32(o + 0x30, 0);
                    } else {
                        W16(o + 0x14, R16(o + 0x14) + 1);
                        Obj_Integrate(o);
                    }
                }
                break;
            default:
                Soldier_Think(o + 0x4A);
                break;
            }
        }
        if (R16(o + 6) != 0x13 && R16(o + 6) != 0x1B) {
            if (STAGE == 8) {                                       /* no map collision */
                uint16_t save = R16(G_COLL_LAYER);
                W16(G_COLL_LAYER, 0xFFFF);
                Soldier_Physics();
                W16(G_COLL_LAYER, save);
            } else Soldier_Physics();
        }
        Enemy_AnimSwitch();
    }
    if ((R16(o + 0x58) & 3) == 1 && R16(o + 6) == 0x0F && overlay_done(o)) {
        sub0c_killed(o);
        Enemy_AnimSwitch();
        if (STAGE == 8) { W16(o, 0xFFFF); return; }
    }
    Soldier_HideCheck();
    Soldier_Draw(R16(o + 0x56));
}
