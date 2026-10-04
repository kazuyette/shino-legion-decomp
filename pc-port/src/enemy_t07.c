/* Enemy type 0x0007 (stage 4-1): armoured giant. Loader 0x060991D0, handler
 * 0x06099254, init 0x0609991C, action setter 0x06099AD0, decision 0x06099A3C, death
 * 0x060999C4, armour flash 0x06099DBC; its shock wave = projectile type 6 (0x06099BE4).
 * Stationary (no physics). 12 HP; after each hurt it becomes invulnerable on every other
 * frame-parity (+4 bit 0, palette flashing to white, 20 frames) and only blocks. It
 * waits (+0x14 from 0x060C32F4[rand % 7]); Sho close in front -> slash combo (1, swings
 * 0x060C3302[rand % 5], halved while flashing) or shock wave (2: projectile 6 at +-28,-39
 * px, grows along X); a shuriken coming -> guard (9). Death: heavy hit -> knocked down
 * (5), else cut down (6: overlay anim 8 + body anim 7 drifting away); 800 points.
 * Fields: +0x52 swings left, +0x54 shock wave slot, +0x56 flash level, +0x48/+0x4A drift,
 * +0x4C/+0x4E/+0x50 overlay anim state. */
#include "enemy_more.h"
#include "anim.h"
#include "sprite.h"
#include "player_ext.h"
#include "fx.h"

#define T7_DEFS 0x060C2C50u
#define T7_SCR  0x060E5D7Cu

void Enemy7_Load(void)                                      /* 0x060991D0 */
{
    Enemy_RegisterChars(7, 0x060E5910u);
    int16_t s = (int16_t)Enemy_FindSlot(7);
    uint16_t b = R16(0x060A9084u + 2u * (uint32_t)s);
    Pal_Copy((b & 0xFFF) << 4, 16, 0x060E5B44u);
    Pal_Copy((uint16_t)(b + 1) * 16, 16, 0x060E5B64u);
    Pal_Copy((uint16_t)(b + 2) * 16, 16, 0x060E5B84u);
}

static void Giant7_Set(int16_t a)                           /* 0x06099AD0 */
{
    uint32_t o = O;
    switch (a) {
    case 0:
        W16(o + 0x14, R16(0x060C32F4u + 2u * ((uint32_t)sub_0602DDD2() % 7u)));   /* A.BIN 0x0600CEF4 */
        W16(o + 6, 0);
        AND16(o + 2, 0xFFFB);
        break;
    case 1:
        W16(o + 0x52, R16(0x060C3302u + 2u * ((uint32_t)sub_0602DDD2() % 5u)));
        if (R16(o + 4) & 1) W16(o + 0x52, (uint16_t)(RS16(o + 0x52) >> 1));
        W16(o + 6, 1);
        OR16(o + 2, 4);
        break;
    case 2: W16(o + 0x54, 0xFFFF); W16(o + 6, 2); break;
    case 4: case 5: case 6: W16(o + 6, (uint16_t)a); break;
    case 9: W16(o + 6, 9); OR16(o + 2, 4); W16(o + 0x52, 1); break;
    case 0x1000:                                                /* hurt: flash, invulnerable */
        Giant7_Set(0);
        W16(o + 0x14, 0x14);
        OR16(o + 2, 8);
        OR16(o + 4, 1);
        break;
    }
}

static void Giant7_Init(void)                               /* 0x0609991C */
{
    uint32_t o = O;
    Giant7_Set(0);
    W16(o + 0x16, 0xC);
    Anim_Start(o + 0x58, T7_DEFS, T7_SCR, CHAR_BASE(SLOT), RS16(o + 6));
    AND16(o + 2, 0xFFFE);
    W16(o + 4, 0);
    W16(o + 0x56, 0);
    if (RS32(o + 8) < RS32(PL + 8)) AND16(o + 2, 0x7FFF); else OR16(o + 2, 0x8000);
}

static int16_t Giant7_Decide(void)                          /* 0x06099A3C */
{
    uint32_t o = O;
    uint32_t r = (uint32_t)sub_0602DDD2();
    if (R16(o + 4) & 1) return 1;
    if (EM_Abs(RS32(PL + 8) - RS32(o + 8)) < 0x00A00000 && RS32(o + 0xC) - RS32(PL + 0xC) < 0x00A00000 &&
        RS32(PL + 0xC) - RS32(o + 0xC) < 0x00080000)
        return ((r >> 1) & 1) == 0 ? 2 : 1;
    return 0;
}

static void Giant7_Die(void)                                /* 0x060999C4 */
{
    uint32_t o = O;
    Enemy_DeathBurst(o);
    Score_Add(800);
    W16(o, 0xFFFF);
    Pal_Copy((R16(0x060A9084u + 2u * (uint32_t)SLOT) & 0xFFF) << 4, 16, 0x060E5B44u);
}

static void Giant7_Flash(void)                              /* 0x06099DBC */
{
    uint32_t o = O;
    int dst = (R16(0x060A9084u + 2u * (uint32_t)SLOT) & 0xFFF) << 4;
    if (!(R16(o + 4) & 1)) {
        if (RS16(o + 0x56) == 0) return;
        W16(o + 0x56, R16(o + 0x56) - 1);
    } else if (RS16(o + 0x56) < 9) W16(o + 0x56, R16(o + 0x56) + 1);
    Pal_SetFaded(dst, 16, 0x060E5B44u, 0, 0, (int16_t)(RS16(o + 0x56) << 4));
}

void EnemyHandler_0007(void)                                /* 0x06099254 */
{
    uint32_t o = O;
    if (R16(o + 2) & 1) Giant7_Init();
    if (EM_Far(o, 0x00A00000) && !(R16(o + 2) & 8)) return;
    uint16_t ovl = 0;
    if (!FROZEN) {
        if ((int16_t)Enemy_AttackPlayer(1, 0, 0xC000) != 0) EM_PlayerFaceBack(o);
        int16_t r = (int16_t)Enemy_CheckPlayerHits(0xC000);
        int16_t a = RS16(o + 6);
        if ((R16(o + 2) & 8) && !(R16(o + 4) & 1) && a != 5 && a != 6 && a != 4) {
            if (a == 2 && RS16(o + 0x54) != -1) {                   /* cancel the shock wave */
                W16(PROJECTILES + (uint32_t)(RS16(o + 0x54) * 0x40), 0xFFFF);
                W16(o + 0x54, 0xFFFF);
            }
            int16_t k = (r == 2) ? (int16_t)Player_AttackKind() : 4;
            if (RS16(o + 0x16) < 1) {
                if (k == 4 || k == 5) { Giant7_Set(5); sub_06007CDE(0x206); }
                else {
                    Giant7_Set(6);
                    W16(o + 0x48, 0); W16(o + 0x4A, 0);
                    EM_OverlayStart(T7_DEFS, T7_SCR, 8, o + 0x4C, o + 0x4E, o + 0x50);
                    sub_06007CDE(0x206);
                    sub_06007CDE(0x10B);
                }
            } else { Giant7_Set(4); sub_06007CDE(0x206); }
        }
        if (!(R16(o + 0x58) & 0x8000)) {
            a = RS16(o + 6);
            if (a == 0) {
                EM_FaceSho(o);
                int32_t d = (R16(o + 2) & 0x8000) ? (int32_t)0xFF600000 : 0x00A00000;
                if (!Projectile_InBox(RS32(o + 8) + d, RS32(o + 0xC) + (int32_t)0xFFC00000, (d >> 8) + RS32(o + 8), RS32(o + 0xC))) {
                    int16_t t = (int16_t)(RS16(o + 0x14) - 1);
                    W16(o + 0x14, (uint16_t)t);
                    if (t < 0) Giant7_Set(Giant7_Decide());
                } else Giant7_Set(9);                               /* guard */
            } else if (a == 1 || a == 9) {
                if ((R16(o + 0x58) & 3) == 1) {
                    AND16(o + 0x58, 0xFFFC);
                    int16_t n = (int16_t)(RS16(o + 0x52) - 1);
                    W16(o + 0x52, (uint16_t)n);
                    if (n < 1) {
                        Giant7_Set(0);
                        if (R16(o + 4) & 1) { AND16(o + 2, 0xFFF7); AND16(o + 4, 0xFFFE); }
                    }
                }
            } else if (a == 2) {
                if ((R16(o + 0x58) & 3) == 1) {
                    AND16(o + 0x58, 0xFFFC);
                    int32_t d = (R16(o + 2) & 0x8000) ? (int32_t)0xFFE40000 : 0x001C0000;
                    int s = Projectile_Spawn(6, RS32(o + 8) + d, RS32(o + 0xC) + (int32_t)0xFFD90000, 0, 0);
                    W16(o + 0x54, (uint16_t)s);
                    if ((int16_t)s != -1) {
                        sub_06007CDE(0x113);
                        uint32_t p = PROJECTILES + (uint32_t)((int16_t)s * 0x40);
                        W16(p + 4, (R16(p + 4) & 0x7FFF) | (R16(o + 2) & 0x8000));
                    }
                }
            }
        } else if (RS16(o + 6) == 4) {
            AND16(o + 2, 0xFFF7);
            Giant7_Set((R16(0x060143EAu) & 1) == 0 ? 0x1000 : 0);
        } else if (RS16(o + 6) == 5) {
            Giant7_Die();
            return;
        } else Giant7_Set(0);
        Enemy_AnimSwitch();
        Giant7_Flash();
    }
    if (R16(o + 6) == 6) {                                          /* cut down */
        Anim_Start(SCRATCH_ANIM, T7_DEFS, T7_SCR, CHAR_BASE(SLOT), 8);
        Enemy_DrawOverlay(SCRATCH_ANIM, (int16_t)(COLOR_BANK(SLOT) + 2), o + 0x4C, o + 0x4E, o + 0x50);
        ovl = R16(SCRATCH_ANIM);
        if (ovl & 0x8000) { Giant7_Die(); return; }
        Anim_Start(SCRATCH_ANIM2, T7_DEFS, T7_SCR, CHAR_BASE(SLOT), 7);
        W16(SPR + 2, (uint32_t)((int16_t)((RS32(o + 8) - RS32(CAM_X)) >> 16) + (RS16(o + 0x48) >> 1)));
        W16(SPR + 4, (uint32_t)((int16_t)((RS32(o + 0xC) - RS32(CAM_Y)) >> 16) + (RS16(o + 0x4A) >> 1)));
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
    EM_SprBasic(o, COLOR_BANK(SLOT));
    if (R16(o + 6) == 5) W16(SPR + 0x0E, R16(o + 0x58) & 3);
    else if (R16(o + 6) == 6) W16(SPR + 0x0E, ovl & 3);
    Anim_Tick(o + 0x58);
}

/* Projectile 6 (0x06099BE4): the giant's shock wave (anim 3), stretched along X by
 * 0x180/frame-step, hits Sho for 2 (guardable), freed when its anim ends. */
void Proj_ShockWave06(uint32_t e)
{
    int16_t s = (int16_t)Enemy_FindSlot(7);
    if (R16(e + 4) & 1) {
        Anim_Start(e + 0x20, T7_DEFS, T7_SCR, CHAR_BASE(s), 3);
        AND16(e + 4, 0xFFFE);
        W16(e + 0x12, 0x7FFF);
        W16(e + 6, 0);
    }
    if (!FROZEN) {
        Projectile_Collide(2, 0xC000);
        if (R16(e + 0x20) & 0x8000) { W16(e, 0xFFFF); return; }
        if ((R16(e + 0x20) & 3) == 0) W16(e + 6, R16(e + 6) + 1);
    }
    if (R16(e) == 0xFFFF) return;
    W16(SPR + 2, (uint32_t)((RS32(e + 8) - RS32(CAM_X)) >> 16));
    W16(SPR + 4, (uint32_t)((RS32(e + 0xC) - RS32(CAM_Y)) >> 16));
    W16(SPR + 0x0A, (uint16_t)(COLOR_BANK(s) + 1));
    W16(SPR + 0x10, (R16(e + 4) & 0x8000) | 0x1A00);
    W32(SPR + 0x1C, (uint32_t)R16(e + 6) * 0x180u + 0x10000u);
    W32(SPR + 0x20, 0x10000u);
    W32(SPR + 0x18, 1);
    W16(SPR + 0x0C, 0);
    W16(SPR + 0x0E, R16(e + 0x20) & 3);
    Anim_Tick(e + 0x20);
}
