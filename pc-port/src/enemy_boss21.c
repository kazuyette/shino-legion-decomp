/* Enemy type 0x0021: stage 2-1 boss (index 2), the ninja Sakyou (slot 3 of stage 2, no type
 * loader: its graphics are loaded by its own init). Registered by the 2-1 frame hook
 * (stage_hooks.c hook_02, tail 0x0606C9E4: x 1914, y 1503, CTRL+0x1E = its slot,
 * CTRL+0x20 |= 0xB locks the camera). Handler 0x06089E40, init 0x06089B3C, graphics
 * 0x06089A18 (SAKYOU.PBC into VRAM, 0xF4 chars of 0x060E2936, palettes 0x060E30D4 (128) and
 * 0x060E31D4 (16, bank +8)), charge glow 0x06089C34, death body 0x0608BD30, death flash
 * 0x0608BE9C, retreat target 0x0608BF74, shot spawner 0x0608B998 (projectile 0x26), blast
 * spawner 0x0608B9E8 (projectile 0x27). Own 16-bit RNG 0x060F828C (= rng * 5 + 0x3573 each
 * frame, seeded vblanks * 5 + 0x3573). HP 0x060BF670[difficulty] (15/18/21), boss bar
 * 0x06061B58 every frame. Fields: +0x14 timer, +0x48 counter, +0x4A attack pattern (0 shots,
 * 1 jump shots, 2 charge), +0x4C repeats, +0x4E spin / direction, +0x50 charge step,
 * +0x52 last attack, +0x54 attack kind that hurt it, +0x56 scratch.
 * Actions: 0 jump in (spinning), 1 land, 2 stand (pattern every 16 frames; dodges Sho's
 * shurikens 1/2 when > 200 px away: 0xE jump), 3 shots (+0x4C = rng & 3 repeats), 4 jump +
 * 2 aimed shots, 5 charge (glow grows, then dashes to the midpoint towards each wall,
 * 0x060F8290 = glow timer), 0xD glow shrinks, 8 hurt (pushed 1 px/frame) -> 0x10 big jump
 * to the far side of the screen (7-shot fan at the top) or death: 6 (shuriken / thrust:
 * blasts 0x27 sub 1, fades) or 7 (sword: flash anim 0xC 41 frames, body anim 7 sliding,
 * blasts sub 0); at the end the camera lock is released (CTRL+0x20 &= 0xFFF4, CTRL+0x1E =
 * -1, camera offset = Sho's screen position, Sho flags4 |= 0x10) and 1500 points: the
 * stage then ends at its exit box (Stage_CheckExit case 2). */
#include "enemy_more.h"
#include "anim.h"
#include "sprite.h"
#include "objects.h"
#include "collision.h"
#include "player_ext.h"
#include "resource.h"
#include "hud.h"
#include "fx.h"

#define B21_DEFS 0x060BD814u
#define B21_SCR  0x060E3594u
#define RNG      0x060F828Cu
#define GLOW     0x060F8290u
#define PROJ(i)  (PROJECTILES + (uint32_t)((int16_t)(i) * 0x40))
#define ACT(o)   R16((o) + 6)
#define CNT      0x48
#define PAT      0x4A
#define REP      0x4C
#define DIR      0x4E
#define SUB      0x50
#define KIND     0x54
#define F56      0x56
#define AN       0x58
#define FRM      0x5A

static void add16(uint32_t a, int v) { W16(a, (uint16_t)(RS16(a) + v)); }

static void Boss21_LoadGfx(void)                            /* 0x06089A18 */
{
    int16_t s = (int16_t)Enemy_FindSlot(0x21);
    uint32_t off = R32(0x060A9070u + 4u * (uint32_t)s);
    int base = (uint16_t)RS16(0x060A9064u + 2u * (uint32_t)s);
    Res_Request(1, 1, 0, 0xFFFFFFFFu, off + 0x25C04740u, 0x060BF678u);  /* "SAKYOU.PBC" */
    for (int i = 0; i < 0xF4; i++) {
        uint32_t r = 0x060E2936u + (uint32_t)i * 8;                    /* {size, mode, w/8, h} */
        Char_Register((base + i) & 0xFFFF, Ram_Ptr(r + 4)[0] * 8, Ram_Ptr(r + 5)[0], RS16(r + 2), off);
        off += (uint32_t)R16(r) * 32;
    }
    uint16_t b = R16(0x060A9084u + 2u * (uint32_t)s);
    Pal_Copy((b & 0xFFF) << 4, 0x80, 0x060E30D4u);
    Pal_Copy((uint16_t)(b + 8) * 16, 0x10, 0x060E31D4u);
}

static void Boss21_Init(void)                               /* 0x06089B3C */
{
    int16_t s = (int16_t)Enemy_FindSlot(0x21);
    Boss21_LoadGfx();
    uint32_t o = O;
    W16(o + 6, 0);
    Anim_Start(o + AN, B21_DEFS, B21_SCR, RS16(0x060A9064u + 2u * (uint32_t)s), RS16(o + 6));
    AND16(o + 2, 0xFFFE);
    W16(o + 0x16, R16(0x060BF670u + 2u * (uint32_t)RS16(0x060252C4u)));
    OR16(o + 2, 0x8000);
    for (uint32_t k = 0x48; k <= 0x56; k += 2) W16(o + k, 0);
    W16(RNG, (uint16_t)(R16(0x060143EAu) * 5u + 0x3573u));
}

static void shot26(int32_t x, int32_t y, int16_t ang, int32_t spd)   /* 0x0608B998 */
{
    Projectile_Spawn(0x26, x, y, ang, spd);
}

static void blast27(int32_t x, int32_t y, uint16_t sub)     /* 0x0608B9E8 */
{
    int s = Projectile_Spawn(0x27, x, y, 0, 0);
    if ((int16_t)s == -1) return;
    uint32_t p = PROJ(s);
    OR16(p + 2, sub);
    OR16(p + 4, R16(O + 2) & 0x8000);
}

static void Boss21_Retreat(void)                            /* 0x0608BF74 */
{
    uint32_t o = O;
    int32_t cx = RS32(CAM_X), x = RS32(o + 8);
    int32_t dl = EM_Abs(cx - x), dr = EM_Abs(cx + 0x1400000 - x), t;
    if (dl < dr) { AND16(o + 2, 0x7FFF); t = cx + 0x1360000; }
    else { OR16(o + 2, 0x8000); t = cx + 0xA0000; }
    int16_t d = (int16_t)((uint32_t)EM_Abs(t - x) >> 16);
    W16(o + F56, (uint16_t)(int16_t)(d / 2 / 4 - 32));
    if (RS16(o + F56) < 3) W16(o + F56, 3);
}

static void face_sho(uint32_t o)
{
    AND16(o + 2, 0x7FFF);
    if (RS32(PL + 8) < RS32(o + 8)) OR16(o + 2, 0x8000);
}

static void Boss21_Release(uint32_t o)                      /* end of actions 6 / 7 */
{
    W16(o, 0xFFFF);
    AND16(0x060EA638u, 0xFFF4);
    W32(0x060EA550u, (uint32_t)(RS32(PL + 8) - RS32(CAM_X)));
    W32(0x060EA554u, (uint32_t)(RS32(PL + 0xC) - RS32(CAM_Y)));
    OR16(PL + 4, 0x10);
    W16(0x060EA636u, 0xFFFF);
    Score_Add(1500);
}

/* charge glow (anim 0xF, frame (t / 4) % 5, scaled): mode 0 grows with 0x060F8290 << 11,
 * 1 full size, 2 shrinks (nothing drawn once below 0) */
static int Boss21_Glow(int16_t t, int16_t mode)             /* 0x06089C34 */
{
    uint32_t o = O;
    int16_t s = (int16_t)Enemy_FindSlot(0x21);
    Anim_Start(SCRATCH_ANIM, B21_DEFS, B21_SCR, RS16(0x060A9064u + 2u * (uint32_t)s), 0xF);
    W16(SCRATCH_ANIM + 2, (uint16_t)(((uint32_t)(uint16_t)t >> 2) % 5u));   /* A.BIN 0x0600A604 */
    if (mode == 0) {
        W32(SPR + 0x20, R32(GLOW) << 11); W32(SPR + 0x1C, R32(GLOW) << 11);
        if (RS32(SPR + 0x20) > 0x10000) W32(SPR + 0x20, 0x10000);
        if (RS32(SPR + 0x1C) > 0x10000) W32(SPR + 0x1C, 0x10000);
    }
    if (mode == 1) { W32(SPR + 0x20, 0x10000); W32(SPR + 0x1C, 0x10000); }
    if (mode == 2) {
        W32(SPR + 0x20, (uint32_t)(RS32(GLOW) * -0x800 + 0x10000));
        W32(SPR + 0x1C, (uint32_t)(RS32(GLOW) * -0x800 + 0x10000));
        if (RS32(SPR + 0x20) < 0) return 2;
    }
    if (!FROZEN) W32(GLOW, R32(GLOW) + 1);
    W32(SPR + 0x18, 1);
    W16(SPR + 0x10, (R16(o + 2) & 0x8000) | 0x1E00);
    W16(SPR + 2, (uint32_t)((RS32(o + 8) - RS32(CAM_X)) >> 16));
    W16(SPR + 4, (uint32_t)((RS32(o + 0xC) - RS32(CAM_Y)) >> 16));
    W16(SPR + 0x0A, (uint16_t)(R16(0x060A9084u + 2u * (uint32_t)s) + 8));
    W16(SPR + 0x0C, 0);
    W16(SPR + 0x14, 0);         /* +-10, then a dead FixMul(|vx|, 0x14000) tilt, then 0 */
    if (R16(0x060143EAu) & 1) { W16(SPR + 0x0E, 3); W32(SPR + 0x18, 0xFFFFFFFFu); }
    Anim_Tick(SCRATCH_ANIM);
    return 0;
}

static void Boss21_Body(void)                               /* 0x0608BD30: anim 7 frame 1, sliding */
{
    uint32_t o = O;
    int16_t s = (int16_t)Enemy_FindSlot(0x21);
    Anim_Start(SCRATCH_ANIM, B21_DEFS, B21_SCR, RS16(0x060A9064u + 2u * (uint32_t)s), 7);
    W16(SCRATCH_ANIM + 2, 1);
    int32_t d = RS16(o + F56) * 0x1000;
    int32_t x = (R16(o + 2) & 0x8000) ? RS32(o + 8) + d : RS32(o + 8) - d;
    W16(SPR + 2, (uint32_t)((x - RS32(CAM_X)) >> 16));
    W16(SPR + 4, (uint32_t)((RS32(o + 0xC) + d - RS32(CAM_Y)) >> 16));
    W32(SPR + 0x18, 0);
    W16(SPR + 0x0E, RS16(o + CNT) < 0xC0);
    if (RS16(o + CNT) < 0xB0) W16(SPR + 0x0E, 2);
    if (RS16(o + CNT) < 0xA0) W16(SPR + 0x0E, 3);
    if (RS16(o + CNT) < 0x90) OR16(o + AN, 0x4000);
    W16(SPR + 0x10, (R16(o + 2) & 0x8000) | 0x200);
    W16(SPR + 0x0C, 0);
    W16(SPR + 0x0A, R16(0x060A9084u + 2u * (uint32_t)s));
    Anim_Tick(SCRATCH_ANIM);
}

static void Boss21_Flash(void)                              /* 0x0608BE9C: anim 0xC, frame (41 - +0x54) / 6 */
{
    uint32_t o = O;
    int16_t s = (int16_t)Enemy_FindSlot(0x21);
    Anim_Start(SCRATCH_ANIM, B21_DEFS, B21_SCR, RS16(0x060A9064u + 2u * (uint32_t)s), 0xC);
    W16(SCRATCH_ANIM + 2, (uint16_t)EM_Sdiv(41 - RS16(o + KIND), 6));      /* A.BIN 0x060092BC */
    W16(SPR + 2, (uint32_t)((RS32(o + 8) - RS32(CAM_X)) >> 16));
    W16(SPR + 4, (uint32_t)((RS32(o + 0xC) - RS32(CAM_Y)) >> 16));
    W16(SPR + 0x10, (R16(o + 2) & 0x8000) | 0x200);
    W16(SPR + 0x0C, 0);
    W16(SPR + 0x0E, 0);
    W16(SPR + 0x0A, R16(0x060A9084u + 2u * (uint32_t)s));
    W32(SPR + 0x18, 1);
    Anim_Tick(SCRATCH_ANIM);
}

/* one step of the charge (action 5, +0x50 = step): even steps wait for the anim event and
 * end the charge 1/4 (not on step 0); 1 / 3 / 5 dash to the midpoint between the boss and
 * the left (0x628) or right (0x72D) wall, speeding up while short of it (3 / 5: twice as
 * fast, alternating sides); 7 back to the centre 0x6A2; 9 brakes after passing Sho. */
static void dash_to(uint32_t o, int sh, int32_t t)
{
    if (RS16(o + DIR) == 1) W32(o + 0x30, (uint32_t)((int32_t)RS16(o + CNT) << sh));
    else W32(o + 0x30, (uint32_t)(RS16(o + CNT) * -(1 << sh)));
    int32_t c = RS16(o + F56) * 0x10000;
    int32_t dx = RS32(o + 8) - RS32(PL + 8);
    if (c + 0x3F80000 < RS32(o + 8) && RS32(o + 8) < c + 0x4080000) {
        if (RS16(o + DIR) == 1 && dx > 0) { W16(o + F56, R16(o + SUB)); W16(o + SUB, 9); return; }
        if (RS16(o + DIR) == 2 && dx < 0) { W16(o + F56, R16(o + SUB)); W16(o + SUB, 9); return; }
    }
    (void)t;
    if (RS16(o + DIR) == 1) add16(o + CNT, c + 0x4000000 < RS32(o + 8) ? -1 : 1);
    if (RS16(o + DIR) == 2) add16(o + CNT, RS32(o + 8) < c + 0x4000000 ? -1 : 1);
}

static int16_t half_px(int32_t v) { int16_t h = (int16_t)((uint32_t)v >> 16); return (int16_t)(h / 2); }

static void Boss21_Charge(uint32_t o)
{
    switch (RS16(o + SUB)) {
    case 0: case 2: case 4: case 6:
        if ((R16(o + AN) & 1) == 1) {
            if ((((R16(RNG) & 0xF0u) >> 4) / 4u) == 0) {                /* A.BIN 0x060092BC */
                if (RS16(o + SUB) == 0) add16(o + SUB, 1);
                else { W16(o + 6, 0xD); W16(o + CNT, 0); W16(o + SUB, 0); W16(o + REP, 0); W32(GLOW, 0); }
            } else add16(o + SUB, 1);
            AND16(o + AN, 0xFFFE);
        }
        break;
    case 1:
        if (RS16(o + CNT) == 0) {
            if (RS32(o + 8) - RS32(PL + 8) < 1) { W16(o + DIR, 1); W16(o + F56, (uint16_t)half_px(RS32(o + 8) - 0xD30000)); }
            else { W16(o + DIR, 2); W16(o + F56, (uint16_t)half_px(RS32(o + 8) - 0x1D80000)); }
        }
        dash_to(o, 12, 0);
        break;
    case 3: case 5:
        if (RS16(o + CNT) == 0) {
            if (RS16(o + DIR) == 1) { W16(o + DIR, 2); W16(o + F56, (uint16_t)half_px(RS32(o + 8) - 0x1D80000)); }
            else { W16(o + DIR, 1); W16(o + F56, (uint16_t)half_px(RS32(o + 8) - 0xD30000)); }
        }
        dash_to(o, 13, 0);
        break;
    case 7:
        if (RS16(o + CNT) == 0) {
            W16(o + F56, (uint16_t)half_px(RS32(o + 8) - 0x15E0000));
            W16(o + DIR, RS32(o + 8) - 0x6A20000 < 1 ? 1 : 2);
        }
        if (RS16(o + DIR) == 1) W32(o + 0x30, (uint32_t)((int32_t)RS16(o + CNT) << 12));
        else W32(o + 0x30, (uint32_t)(RS16(o + CNT) * -0x1000));
        if (RS32(o + 8) < 0x69E0001 || 0x6A5FFFF < RS32(o + 8)) {
            int32_t c = RS16(o + F56) * 0x10000 + 0x4000000;
            if (RS16(o + DIR) == 1) add16(o + CNT, c < RS32(o + 8) ? -1 : 1);
            if (RS16(o + DIR) == 2) add16(o + CNT, RS32(o + 8) < c ? -1 : 1);
        } else {
            add16(o + SUB, 1); W16(o + CNT, 0);
            W32(o + 8, 0x06A20000u); W32(o + 0x30, 0);
        }
        break;
    case 8:
        W16(o + 6, 0xD); W16(o + CNT, 0); W16(o + SUB, 0); W16(o + REP, 0); W32(GLOW, 0);
        break;
    case 9:
        add16(o + CNT, -4);
        if (RS16(o + CNT) < 0) {
            W16(o + CNT, 0);
            W16(o + SUB, R16(o + F56));
            if ((int32_t)((R16(RNG) & 0xFF00u) >> 8) / 4 < 0x20) {       /* A.BIN 0x060092BC */
                W16(o + 6, 0xD); W16(o + CNT, 0); W16(o + SUB, 0); W32(o + 0x30, 0); W32(GLOW, 0); W16(o + REP, 0);
            }
        }
        break;
    default: break;
    }
    if (RS16(o + CNT) != 0) {
        if (RS16(o + DIR) == 2 && RS32(o + 8) < 0x6280000) { W32(o + 0x30, 0); add16(o + SUB, 1); W16(o + CNT, 0); }
        if (RS16(o + DIR) == 1 && 0x72D0000 < RS32(o + 8)) { W32(o + 0x30, 0); add16(o + SUB, 1); W16(o + CNT, 0); }
    }
}

/* death countdown (+0x48 from 0x100): blasts at 0x100 / 0xC0 / 0x80 / 0x40, sound every 8 */
static int Boss21_Dying(uint32_t o, int32_t dx, uint16_t sub)
{
    if (RS16(o + CNT) == 0) { Boss21_Release(o); return 1; }
    int16_t c = RS16(o + CNT);
    if (c == 0x100) blast27(RS32(o + 8), RS32(o + 0xC), sub);
    if (c == 0xC0 || c == 0x80 || c == 0x40) blast27(RS32(o + 8) + dx, RS32(o + 0xC), sub);
    if ((c & 7) == 0) sub_06007CDE(0x115);
    add16(o + CNT, -1);
    return 0;
}

void EnemyHandler_0021(void)                                /* 0x06089E40 */
{
    uint32_t o = O;
    int16_t s = (int16_t)Enemy_FindSlot(0x21);
    W16(RNG, (uint16_t)(R16(RNG) * 5u + 0x3573u));
    if (R16(o + 2) & 1) Boss21_Init();
    Hud_DrawBossBar(RS16(0x060BF670u + 2u * (uint32_t)RS16(0x060252C4u)), RS16(o + 0x16));
    if (!FROZEN) {
        uint16_t a = ACT(o);
        if (a == 2 || a == 4) face_sho(o);
        if (!(R16(PL + 2) & 8) && (int16_t)Enemy_AttackPlayer(1, 0, 0) == 3) sub_06007CDE(0x10C);
        a = ACT(o);
        if (a != 6 && a != 7 && a != 8 && !(R16(o + 2) & 8)) {
            W16(o + KIND, 0xFFFF);
            if ((int16_t)Enemy_CheckPlayerHits(0xC000) == 2) W16(o + KIND, (uint16_t)(int16_t)Player_AttackKind());
        }
        a = ACT(o);
        if (a != 6 && a != 7 && a != 8 && (R16(o + 2) & 8)) {
            W16(o + 6, 8);
            W32(o + 0x30, 0); W32(o + 0x34, 0x30000);
            if (RS16(o + 0x16) > 0) sub_06007CDE(0x206);
        }
        if (ACT(o) == 2) {                                          /* stand: next attack */
            if (RS16(o + 0x14) == 0) {
                if (RS16(o + PAT) >= 0 && RS16(o + PAT) <= 2) W16(o + 0x14, 0x10);
            } else {
                add16(o + 0x14, -1);
                if (RS16(o + 0x14) == 0) {
                    int16_t p = RS16(o + PAT);
                    if (p == 0 || p == 1) {
                        W16(o + CNT, 0); W16(o + PAT, (uint16_t)(p + 1)); W16(o + REP, R16(RNG) & 3);
                        W16(o + 0x52, (uint16_t)(p + 3)); W16(o + F56, 0); W16(o + 6, (uint16_t)(p + 3));
                        AND16(o + 2, 0xFFFB);
                    } else if (p == 2) {
                        W16(o + CNT, 0); W16(o + PAT, 0); W16(o + REP, 0);
                        if (RS16(o + SUB) == 0) W16(o + DIR, 0);
                        W16(o + 0x52, 5); W16(o + 6, 5); W16(o + F56, 0);
                        OR16(o + 2, 4);
                        W32(GLOW, 0);
                    }
                }
            }
        }
        if (ACT(o) == 2) {                                          /* dodge Sho's shurikens */
            int32_t x0, x1;
            if (!(R16(o + 2) & 0x8000)) { x0 = RS32(o + 8); x1 = RS32(o + 8) + 0xF00000; }
            else { x0 = RS32(o + 8) - 0xF00000; x1 = RS32(o + 8); }
            if (!Projectile_InBox(x0, RS32(o + 0xC) - 0x400000, x1, RS32(o + 0xC) + 0x100000)) W16(o + F56, 0);
            else if (RS16(o + F56) == 0) {
                W16(o + F56, 1);
                if (((R16(RNG) >> 4) & 1) && EM_Abs(RS32(PL + 8) - RS32(o + 8)) > 0xC80000) {
                    W16(o + 6, 0xE); W16(o + CNT, 0); W16(o + REP, 0);
                }
            }
        }
        if (ACT(o) == 0xE) {
            if ((R16(o + AN) & 3) == 1) {
                if (RS16(o + CNT) == 0) W32(o + 0x34, 0x90000);
                if (RS32(o + 0x34) < 0) { AND16(o + AN, 0xFFFE); W16(o + CNT, 0); }
                else add16(o + CNT, 1);
            }
            if ((R16(o + AN) & 3) == 3 && !(R16(o + 2) & 0x4000)) { add16(o + FRM, 1); AND16(o + AN, 0xFFFC); }
        }
        if (ACT(o) == 0) {                                          /* jump in */
            W32(o + 0x30, 0xFFFC8000u);
            if (RS16(o + CNT) < 0x13) W32(o + 0x34, 0x50000);
            if (RS16(o + CNT) > 0x13 && (R16(o + 2) & 0x10)) { W32(o + 0x30, 0); W16(o + 6, 1); }
            add16(o + CNT, 1);
            add16(o + DIR, 1);
        }
        if (ACT(o) == 5) {
            if (R16(o + FRM) > 0xD) Boss21_Charge(o);
            add16(o + 0x14, 1);
        }
        if (ACT(o) == 0xD) add16(o + 0x14, 1);
        if (ACT(o) == 3 && (R16(o + AN) & 1)) {                     /* one shot, two heights */
            int32_t dx, dy, v = (int32_t)0xFFFC0000;
            if (RS16(o + CNT) == 0) { dx = -0x280000; dy = 0x3F0000; } else { dx = -0x250000; dy = 0x240000; }
            if (!(R16(o + 2) & 0x8000)) { dx = -dx; v = 0x40000; }
            sub_06007CDE(0x100);
            shot26(RS32(o + 8) + dx, RS32(o + 0xC) - dy, 0, v);
            AND16(o + AN, 0xFFFE);
            W16(o + CNT, RS16(o + CNT) == 0 ? 1 : 0);
        }
        if (ACT(o) == 4) {                                          /* jump, two aimed shots */
            if ((R16(o + AN) & 3) == 1) {
                if (RS16(o + CNT) == 0) W32(o + 0x34, 0x90000);
                if (RS32(o + 0x34) < 0) { AND16(o + AN, 0xFFFE); W16(o + CNT, 0); }
                else add16(o + CNT, 1);
            }
            if ((R16(o + AN) & 3) == 2) {
                int32_t dx, dy, v = (int32_t)0xFFFC0000;
                int right = !(R16(o + 2) & 0x8000);
                if (RS16(o + CNT) == 0) { dx = -0x200000; dy = 0x2E0000; add16(o + CNT, 1); }
                else { dx = -0x1F0000; dy = 0x300000; }
                if (right) { dx = -dx; v = 0x40000; }
                int16_t ang = (int16_t)Atan8((int16_t)((uint32_t)(RS32(o + 8) + dx) >> 16), (int16_t)((uint32_t)(RS32(o + 0xC) - dy) >> 16),
                                             (int16_t)((uint32_t)RS32(PL + 8) >> 16), (int16_t)((uint32_t)(RS32(PL + 0xC) - 0x200000) >> 16));
                if (R16(o + 2) & 0x8000) ang = (int16_t)(ang + 0x80);
                sub_06007CDE(0x101);
                shot26(RS32(o + 8) + dx, RS32(o + 0xC) - dy, (int16_t)(ang - 2), v);
                shot26(RS32(o + 8) + dx, RS32(o + 0xC) - dy, (int16_t)(ang + 2), v);
                AND16(o + AN, 0xFFFD);
            }
            if ((R16(o + AN) & 3) == 3 && !(R16(o + 2) & 0x4000)) { add16(o + FRM, 1); AND16(o + AN, 0xFFFC); }
        }
        if (ACT(o) == 6 && (R16(o + AN) & 1) == 1) {                /* death: fades */
            if (Boss21_Dying(o, -0x100000, 1)) return;
        }
        if (ACT(o) == 7) {                                          /* death: cut down */
            if (RS16(o + KIND) == 0) {
                if (Boss21_Dying(o, 0x140000, 0)) return;
                add16(o + F56, 1);
            } else add16(o + KIND, -1);
        }
        if (ACT(o) == 8) {                                          /* hurt: pushed back */
            W32(o + 0x30, (R16(o + 2) & 0x8000) ? 0x10000u : 0xFFFF0000u);
            if (RS32(o + 8) < 0x6280000) W32(o + 0x30, 0);
            if (0x72D0000 < RS32(o + 8)) W32(o + 0x30, 0);
        }
        if (ACT(o) == 0x10) {                                       /* big jump across */
            if ((int32_t)R16(o + 0x1A) < RS16(o + F56)) W32(o + 0x34, 0xA0000);
            if (RS32(o + 0x34) < 0 && !(R16(o + 4) & 2) && EM_Abs(RS32(o + 0xC) - RS32(0x060EA624u)) > 0xB40000) {
                int32_t y = RS32(o + 0xC) - 0x200000;
                int32_t v = (R16(o + 2) & 0x8000) ? (int32_t)0xFFFC0000 : 0x40000;
                int16_t ang = (int16_t)Atan8((int16_t)((uint32_t)RS32(o + 8) >> 16), (int16_t)((uint32_t)RS32(o + 0xC) >> 16),
                                             (int16_t)((uint32_t)RS32(PL + 8) >> 16), (int16_t)((uint32_t)(RS32(PL + 0xC) - 0x200000) >> 16));
                if (R16(o + 2) & 0x8000) ang = (int16_t)(ang + 0x80);
                sub_06007CDE(0x101);
                for (int k = -3; k <= 3; k++) shot26(RS32(o + 8), y, (int16_t)(ang + k * 8), v);
                OR16(o + 4, 2);
            }
            W32(o + 0x30, (R16(o + 2) & 0x8000) ? 0xFFFA8000u : 0x58000u);
            if (R16(o + 2) & 0x10) {
                W16(o + 6, 2);
                W32(o + 0x30, 0);
                W16(o + PAT, (uint16_t)(int16_t)(((EM_Sdiv((int32_t)R16(RNG), 7) >> 4) & 0x30) >> 4));  /* 0x060092BC */
                if (RS16(o + PAT) == 3) W16(o + PAT, 0);
                AND16(o + 4, 0xFFFD);
                W32(o + 0x34, 0);
            }
            add16(o + DIR, 1);
        }
        if (R16(o + AN) & 0x8000) {                                 /* animation over */
            W32(o + 0x30, 0);
            AND16(o + 2, 0xFFFB);
            W16(o + 0x14, 0);
            if (ACT(o) == 8) {
                if (!(R16(o + 2) & 0x4000)) {
                    if (RS16(o + 0x16) < 1) {
                        sub_06007CDE(0x224);
                        int16_t k = RS16(o + KIND);
                        if (k == 0 || k == 1 || k == 2 || k == 3) { W16(o + 6, 7); W16(o + CNT, 0x100); W16(o + KIND, 0x29); W16(o + F56, 0); }
                        else { W16(o + 6, 6); W16(o + CNT, 0x100); W16(o + KIND, 0); }
                    } else {
                        AND16(o + 2, 0xFFF7);
                        Boss21_Retreat();
                        W16(o + 6, 0x10);
                    }
                } else W16(o + FRM, 0);
            } else if (RS16(o + REP) == 0) { W16(o + 6, 2); W16(o + F56, 0); }
            else {
                add16(o + REP, -1);
                W16(o + FRM, 0);
                face_sho(o);
                if (ACT(o) == 4) W16(o + CNT, 0);
            }
            AND16(o + AN, 0x7FFF);
        }
        Obj_Integrate(o);
        if (ACT(o) != 0) {
            if (RS32(o + 8) < RS32(CAM_X) + 0x80000) W32(o + 8, (uint32_t)(RS32(CAM_X) + 0x80000));
            if (RS32(CAM_X) + 0x1380000 < RS32(o + 8)) W32(o + 8, (uint32_t)(RS32(CAM_X) + 0x1380000));
        }
        Obj_CollideMap(o, 3, 6);
        Enemy_AnimSwitch();
    }
    if (ACT(o) == 5) Boss21_Glow(RS16(o + 0x14), R16(o + FRM) < 0xD ? 0 : 1);
    if (ACT(o) == 0xD) Boss21_Glow(RS16(o + 0x14), 2);
    if (ACT(o) == 7) { Boss21_Body(); if (RS16(o + KIND) != 0) Boss21_Flash(); }
    W16(SPR + 2, (uint32_t)((RS32(o + 8) - RS32(CAM_X)) >> 16));
    W16(SPR + 4, (uint32_t)((RS32(o + 0xC) - RS32(CAM_Y)) >> 16));
    W16(SPR + 0x10, (R16(o + 2) & 0x8000) | 0x1C00);
    W32(SPR + 0x20, 0x10000); W32(SPR + 0x1C, 0x10000);
    W16(SPR + 0x0A, R16(0x060A9084u + 2u * (uint32_t)s));
    W16(SPR + 0x0C, 0);
    W16(SPR + 0x0E, 0);
    W16(SPR + 0x14, 0);
    if (ACT(o) == 6 || ACT(o) == 7) {
        W16(SPR + 0x0E, RS16(o + CNT) < 0xC0);
        if (RS16(o + CNT) < 0xB0) W16(SPR + 0x0E, 2);
        if (RS16(o + CNT) < 0xA0) W16(SPR + 0x0E, 3);
        if (RS16(o + CNT) < 0x90) OR16(o + AN, 0x4000);
    }
    if (ACT(o) == 0 || ACT(o) == 0x10) {                            /* spinning in the air */
        W16(SPR + 4, (uint16_t)(RS16(SPR + 4) - 0x22));
        W16(SPR + 0x14, (uint16_t)(RS16(o + DIR) * ((R16(o + 2) & 0x8000) ? 0x10 : -0x10)));
    }
    if (R16(o) != 0xFFFF) { Anim_Tick(o + AN); AND16(o + AN, 0xBFFF); }
}

/* Projectile 0x26 (0x0608BA64): the boss's spinning shuriken (anim 9), hurts Sho for 1
 * (mode 0x3000: blocked / reflected by the sword), gone 128 px off screen. */
void Proj_Shuriken26(uint32_t e)
{
    int16_t s = (int16_t)Enemy_FindSlot(0x21);
    if (R16(e + 4) & 1) {
        Anim_Start(e + 0x20, B21_DEFS, B21_SCR, RS16(0x060A9064u + 2u * (uint32_t)s), 9);
        AND16(e + 4, 0xFFFE);
    }
    if (!FROZEN) {
        if ((int16_t)Projectile_Collide(1, 0x3000) == 1) W16(e, 0xFFFF);
        W32(e + 8, R32(e + 8) + R32(e + 0x18));
        W32(e + 0xC, R32(e + 0xC) - R32(e + 0x1C));
        W16(e + 0x14, R16(e + 0x14) + 0x10);
    }
    int32_t dx = RS32(e + 8) - RS32(CAM_X), dy = RS32(e + 0xC) - RS32(CAM_Y);
    if (dx < -0x800000 || 0x1C00000 < dx || dy < -0x800000 || 0x1C00000 < dy) W16(e, 0xFFFF);
    W16(SPR + 2, (uint32_t)(dx >> 16));
    W16(SPR + 4, (uint32_t)(dy >> 16));
    W16(SPR + 0x0C, 0);
    W16(SPR + 0x0E, 0);
    W16(SPR + 0x14, R16(e + 0x14));
    W16(SPR + 0x0A, R16(0x060A9084u + 2u * (uint32_t)s));
    W16(SPR + 0x10, (R16(e + 4) & 0x8000) | 0x600);
    W32(SPR + 0x18, 0xFFFFFFFFu);
    if (R16(e) != 0xFFFF) Anim_Tick(e + 0x20);
    if (R16(e + 0x20) & 0x8000) W16(e, 0xFFFF);
}

/* Projectile 0x27 (0x0608BBEC): blast on the dying boss, anim 10 (+2 & 1: 11), animated on
 * odd frames of its own counter (+6). Harmless. */
void Proj_Blast27(uint32_t e)
{
    int anim = (R16(e + 2) & 1) ? 0xB : 0xA;
    int16_t s = (int16_t)Enemy_FindSlot(0x21);
    if (R16(e + 4) & 1) {
        Anim_Start(e + 0x20, B21_DEFS, B21_SCR, RS16(0x060A9064u + 2u * (uint32_t)s), anim);
        AND16(e + 4, 0xFFFE);
    }
    if (!FROZEN) W16(e + 6, R16(e + 6) + 1);
    W16(SPR + 2, (uint32_t)((RS32(e + 8) - RS32(CAM_X)) >> 16));
    W16(SPR + 4, (uint32_t)((RS32(e + 0xC) - RS32(CAM_Y)) >> 16));
    W16(SPR + 0x10, 0x200);
    W16(SPR + 0x0C, 0);
    W16(SPR + 0x0E, 0);
    W16(SPR + 0x0A, R16(0x060A9084u + 2u * (uint32_t)s));
    W32(SPR + 0x18, 1);
    if (R16(e) != 0xFFFF && (R16(e + 6) & 1)) Anim_Tick(e + 0x20);
    if (R16(e + 0x20) & 0x8000) W16(e, 0xFFFF);
}
