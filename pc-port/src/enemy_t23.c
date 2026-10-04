/* Enemy type 0x0023 (stages 2-1, 3-1, 5-2): ninja pair running in from the screen edges.
 * Loader 0x06095310, handler 0x0609535E, init 0x0609609C, flip check 0x060961C0.
 * A subtype-0 ninja waits (not initialised) until Sho walks past its X (within 16 px,
 * 200 px vertically; on stage index 2 only from above); it then appears at the left
 * screen edge and registers its partner (subtype 1) at the right edge, with the item
 * pair of table 0x060C1AC6[+0x18]. Each runs (action 7, 2.56 px/frame) to 40 px beside
 * Sho (left / right), jumps up to his level (velocity from the height, max 9 px/frame)
 * or crouches when he is lower, slashes, and may back-flip (0x10) when Sho faces it at
 * close range or jump when a shuriken comes. A wall ahead -> hop (+4 bit 0, +0x4E).
 * 1 HP; heavy attack -> knocked away (0xD), else cut down (0xE, overlay anim 0xF in
 * +0x52/+0x54/+0x56). 200 points.
 * Actions: 0 stand, 1 crouch, 2 crouched, 3 stand up, 4 jump, 5 fall, 6 land, 7 run,
 * 8 skid, 9/10 slash standing/crouched, 0xB air slash, 0xC hurt, 0x10 back-flip.
 * Fields: +0x48 next action, +0x4C jump speed >> 12, +0x4E hop speed >> 12, +0x14 timer,
 * +4 bits 0x8000 shuriken seen, 0x4000 flip considered, 1 hopping. */
#include "enemy_more.h"
#include "anim.h"
#include "sprite.h"
#include "collision.h"
#include "player_ext.h"
#include "objects.h"
#include "fx.h"

#define T23_DEFS 0x060C1264u
#define T23_SCR  0x060E4EB4u

void Enemy23_Load(void)                                     /* 0x06095310 */
{
    Enemy_RegisterChars(0x23, 0x060E49D4u);
    int16_t s = (int16_t)Enemy_FindSlot(0x23);
    Pal_Copy((R16(0x060A9084u + 2u * (uint32_t)s) & 0xFFF) << 4, 16, 0x060E4C48u);
    Pal_Copy((uint16_t)(R16(0x060A9084u + 2u * (uint32_t)s) + 1) * 16, 16, 0x060E4C68u);
}

static void Ninja23_Init(void)                              /* 0x0609609C */
{
    uint32_t o = O;
    W16(o + 6, 7);
    Anim_Start(o + 0x58, T23_DEFS, T23_SCR, CHAR_BASE(SLOT), RS16(o + 6));
    AND16(o + 2, 0xFFFE);
    W16(o + 0x16, 1);
    W16(o + 4, 0);
    if ((R16(o + 0x12) & 0x1F) == 0) {                         /* appear left, partner right */
        uint32_t k = (RS16(o + 0x18) == -1) ? 0 : (uint32_t)(uint16_t)R16(o + 0x18);
        uint32_t t = (k & 0xFFFF) * 4;
        W16(o + 0x18, R16(0x060C1AC6u + t));
        W32(o + 8, R32(CAM_X));
        Obj_Register(0x23, RS32(CAM_X) + 0x01400000, RS32(o + 0xC), 1, R16(0x060C1AC8u + t));
    }
    if ((R16(o + 0x12) & 0x1F) == 2) AND16(o + 0x12, 0xFFE0);
    EM_FaceSho(o);
}

/* 0x060961C0: Sho facing it, close and attacking -> maybe back-flip (once per approach) */
static void Ninja23_FlipCheck(void)
{
    uint32_t o = O;
    if ((R16(PL + 2) & 0x8000) == (R16(o + 2) & 0x8000)) return;
    int32_t d = EM_Abs(RS32(o + 8) - RS32(PL + 8));
    if (d < 0x00500000 && d < 0x00500000) {                     /* (the original tests |dx| twice) */
        if ((int16_t)Player_AttackKind() == 6) { AND16(o + 4, 0xBFFF); return; }
        if (R16(o + 4) & 0x4000) return;
        OR16(o + 4, 0x4000);
        if (sub_0602DDD2() & 1) W16(o + 6, 0x10);
        return;
    }
    AND16(o + 4, 0xBFFF);
}

/* climb speed to rise `h` (16.16) under gravity 0x5000, max 9 px/frame -> +0x4C */
static void jump_for(uint32_t o, int32_t h)
{
    int32_t v = 0, s = 0;
    if (h > 0) do { v += 0x5000; s += v; } while (s < h);
    if (v > 0x90000) v = 0x90000;
    W16(o + 0x4C, (uint16_t)(v >> 12));
    W16(o + 6, 4);
    AND16(o + 4, 0xFFFE);
}

static void Ninja23_Run(uint32_t o)
{
    int32_t T = RS32(PL + 8);                                  /* target: 40 px beside Sho */
    if ((R16(o + 0x12) & 0x1F) == 0) T = RS32(PL + 8) - 0x00280000;
    else if ((R16(o + 0x12) & 0x1F) == 1) T = RS32(PL + 8) + 0x00280000;
    if (RS32(o + 8) < T) AND16(o + 2, 0x7FFF); else OR16(o + 2, 0x8000);
    if (STAGE == 4) {
        if (R16(o + 0x10) & 0xC) { W16(o + 6, 0); W16(o + 0x14, 3); }
    } else if (R16(o + 0x10) & 8) {
        AND16(o + 2, 0x7FFF); W16(o + 6, 4); W16(o + 0x4E, 0x20); W16(o + 0x14, 0xC); OR16(o + 4, 1);
        return;
    } else if (R16(o + 0x10) & 4) {
        OR16(o + 2, 0x8000); W16(o + 6, 4); W16(o + 0x4E, 0xFFE0); W16(o + 0x14, 0xC); OR16(o + 4, 1);
        return;
    }
    Ninja23_FlipCheck();
    int32_t d = (R16(o + 2) & 0x8000) ? (int32_t)0xFF800000 : 0x00800000;
    if (!Projectile_InBox(RS32(o + 8) + d, RS32(o + 0xC) + (int32_t)0xFFC00000, RS32(o + 8), RS32(o + 0xC)))
        AND16(o + 4, 0x7FFF);
    else if (!(R16(o + 4) & 0x8000)) {
        OR16(o + 4, 0x8000);
        if (!(sub_0602DDD2() & 1)) { jump_for(o, 0x00A00000); return; }   /* jump the shuriken */
    }
    if (EM_Abs(T - RS32(o + 8)) < 0x29000) {                    /* arrived */
        W32(o + 8, (uint32_t)T);
        int32_t dy = (int16_t)(R32(PL + 0xC) >> 16) - (int16_t)(R32(o + 0xC) >> 16);
        if (EM_Abs(dy) < 3) { W16(o + 6, 8); W16(o + 0x48, 9); }            /* skid, slash */
        else if (RS32(PL + 0xC) < RS32(o + 0xC)) jump_for(o, RS32(o + 0xC) - RS32(PL + 0xC));
        else { W16(o + 6, 1); W16(o + 0x48, 10); }                          /* crouch, slash */
    } else {
        AND16(o + 2, 0xF7FF);
        W32(o + 0x30, (R16(o + 2) & 0x8000) ? 0xFFFD7000u : 0x00029000u);
    }
}

void EnemyHandler_0023(void)                                /* 0x0609535E */
{
    uint32_t o = O;
    if (EM_Far(o, 0x00700000) && !(R16(o + 2) & 8) && !(R16(o + 2) & 0x4000)) return;
    if (R16(o + 2) & 1) {
        int go = 1;
        if ((R16(o + 0x12) & 0x1F) == 0)
            go = EM_Abs(RS32(o + 8) - RS32(PL + 8)) < 0x00100000 && EM_Abs(RS32(o + 0xC) - RS32(PL + 0xC)) < 0x00C80000;
        if (go && (STAGE != 2 || RS32(o + 0xC) - RS32(PL + 0xC) >= 0)) Ninja23_Init();
    }
    if (R16(o + 2) & 1) return;
    uint16_t ovl = 0;
    if (!FROZEN) {
        if ((int16_t)Enemy_AttackPlayer(1, 0, 0xC000) != 0) { sub_06007CDE(0x104); EM_PlayerFaceBack(o); }
        int16_t r = (int16_t)Enemy_CheckPlayerHits(0xC000);
        if (!(R16(o + 2) & 0x4000)) W32(o + 0x30, 0);
        uint16_t a = R16(o + 6);
        if ((R16(o + 2) & 8) && a != 0xD && a != 0xE && a != 0xC) {
            int16_t k = (r == 2) ? (int16_t)Player_AttackKind() : 4;
            if (RS16(o + 0x16) < 1) {
                if (k == 4 || k == 5) { W16(o + 6, 0xD); sub_06007CDE(0x205); }
                else {
                    W16(o + 6, 0xE);
                    EM_OverlayStart(T23_DEFS, T23_SCR, 0xF, o + 0x52, o + 0x54, o + 0x56);
                    sub_06007CDE(0x205);
                }
            } else W16(o + 6, 0xC);
            Enemy_AnimSwitch();
        }
        if (!(R16(o + 0x58) & 0x8000)) {
            switch (R16(o + 6)) {
            case 0: case 2:
                if (EM_Abs(RS32(o + 8) - RS32(PL + 8)) < 0x00280001) {
                    int16_t t = (int16_t)(RS16(o + 0x14) - 1);
                    W16(o + 0x14, (uint16_t)t);
                    if (t != 0) break;
                }
                if (!(R16(o + 2) & 0x0800)) W16(o + 6, 7);
                else { W16(o + 6, 3); W16(o + 0x48, 7); }
                break;
            case 4:
                if (!(R16(o + 4) & 1)) {
                    if (!(R16(o + 2) & 0x4000)) { if ((R16(o + 0x58) & 3) == 2) W16(o + 6, 6); }
                    else if (EM_Abs(RS32(o + 0xC) - RS32(PL + 0xC)) < 0x00400000 &&
                             EM_Abs(RS32(o + 8) - RS32(PL + 8)) < 0x00380000) { W16(o + 6, 0xB); break; }
                    if ((R16(o + 0x58) & 3) == 1) {
                        W32(o + 0x34, (uint32_t)((int32_t)RS16(o + 0x4C) << 12));
                        AND16(o + 0x58, 0xFFFC); OR16(o + 0x58, 2);
                        sub_06007CDE(0x126);
                    }
                } else {                                            /* hop a wall */
                    if ((R16(o + 0x58) & 3) == 1) {
                        int16_t t = (int16_t)(RS16(o + 0x14) - 1);
                        W16(o + 0x14, (uint16_t)t);
                        if (t == 0 || (int16_t)(R32(o + 8) >> 16) != (int16_t)(R32(o + 0x28) >> 16)) {
                            AND16(o + 0x58, 0xFFFC); OR16(o + 0x58, 2);
                        } else { W32(o + 0x34, 0x00060000u); sub_06007CDE(0x126); }
                    }
                    if (!(R16(o + 2) & 0x4000)) { if ((R16(o + 0x58) & 3) == 2) W16(o + 6, 6); }
                    else {
                        W32(o + 0x30, (uint32_t)((int32_t)RS16(o + 0x4E) << 12));
                        if (RS32(o + 0x34) < 0) W16(o + 6, 5);
                    }
                }
                break;
            case 5:
                if (!(R16(o + 2) & 0x4000)) W16(o + 6, 6);
                else W32(o + 0x30, (uint32_t)((int32_t)RS16(o + 0x4E) << 12));
                break;
            case 7: Ninja23_Run(o); break;
            case 8: {                                               /* skid */
                static const int32_t v[4] = { 0, 0x18000, 0x10000, 0x8000 };
                uint16_t f = R16(o + 0x58) & 3;
                if (f) W32(o + 0x30, (uint32_t)((R16(o + 2) & 0x8000) ? -v[f] : v[f]));
                break;
            }
            case 9: case 10:
                if (RS32(o + 8) < RS32(PL + 8)) AND16(o + 2, 0x7FFF); else OR16(o + 2, 0x8000);
                break;
            case 0xB:
                if (RS32(o + 8) < RS32(PL + 8)) AND16(o + 2, 0x7FFF); else OR16(o + 2, 0x8000);
                if ((R16(o + 0x58) & 3) == 1 && !(R16(o + 2) & 0x4000)) W16(o + 6, 6);
                break;
            case 0x10:                                              /* back-flip */
                if ((R16(o + 0x58) & 3) == 1) {
                    W32(o + 0x30, (R16(o + 2) & 0x8000) ? 0x00030000u : 0xFFFD0000u);
                    W32(o + 0x34, 0x00040000u);
                    sub_06007CDE(0x126);
                    AND16(o + 0x58, 0xFFFC);
                } else if (!(R16(o + 2) & 0x4000)) W16(o + 6, 0x11);
                break;
            }
        } else {
            switch (RS16(o + 6)) {
            case 1: OR16(o + 2, 0x0800); W16(o + 6, R16(o + 0x48)); break;
            case 3: AND16(o + 2, 0xF7FF); W16(o + 6, R16(o + 0x48)); break;
            case 6:
                AND16(o + 2, 0xF7FF);
                if (!(R16(o + 4) & 1)) { W16(o + 6, 0); W16(o + 0x14, 0x78); }
                else { AND16(o + 4, 0xFFFE); W16(o + 6, 7); }
                break;
            case 8: W16(o + 6, R16(o + 0x48)); break;
            case 9: AND16(o + 2, 0xF7FF); W16(o + 6, 0); W16(o + 0x14, 0x78); break;
            case 10: OR16(o + 2, 0x0800); W16(o + 6, 2); Enemy_AnimSwitch(); W16(o + 0x14, 0x78); break;
            case 0xC:
                if (!(R16(o + 2) & 0x4000)) { W16(o + 6, 0); W16(o + 0x14, 3); AND16(o + 2, 0xFFF7); }
                else { AND16(o + 0x58, 0x7FFF); W16(o + 0x5A, 0); W16(o + 0x72, 0); }
                break;
            case 0xD:
                AND16(o + 2, 0xFFF7); Enemy_DeathBurst(o); Score_Add(200); W16(o, 0xFFFF);
                return;
            case 0x11: W16(o + 6, 0); W16(o + 0x14, 3); break;
            }
        }
        Obj_Integrate(o);
        EM_ClampX(o);
        Obj_CollideMap(o, 3, 8);
        Enemy_AnimSwitch();
    }
    if ((R16(o + 0x58) & 3) == 1 && R16(o + 6) == 0xE) {
        int done = EM_OverlayDraw(T23_DEFS, T23_SCR, 0xF, o + 0x52, o + 0x54, o + 0x56);
        ovl = R16(SCRATCH_ANIM);
        if (done) { Enemy_DeathBurst(o); Score_Add(200); W16(o, 0xFFFF); return; }
    }
    EM_SprBasic(o, COLOR_BANK(SLOT));
    if (R16(o + 6) == 0xD) W16(SPR + 0x0E, R16(o + 0x58) & 3);
    else if (R16(o + 6) == 0xE && (R16(o + 0x58) & 3) == 1) W16(SPR + 0x0E, ovl & 3);
    Anim_Tick(o + 0x58);
}
