/* Enemy type 0x0009: stage 6-2 boss (index 11, slot 0), the crystal monk.
 * Loader 0x0609ABA8 (0x9E chars of 0x060E60F2, palettes 0x060E65E0 / 6600 / 6620 / 6640 / 6660
 * / 6680 into banks +0..+5), handler 0x0609B448, init 0x0609B338 (x 1272, 48 px above the
 * camera, mode 8 / action 0x12 = drops in), face Sho 0x0609D2DE (1 = turned), decision
 * 0x0609D0A4 (|dy| / 8 px <= 10: mode = table 0x060C4C4E + 10 x band [((rand >> 8) / 10) % 10],
 * bands of |dx| / 8 px <5, 5-9, 10-14, 15-19, 20-29, 30-34, 35-99; farther below / above:
 * mode 0x15 when |dx| / 8 px < 15), run 0x0609D34E, close-attack check 0x0609D660, wall
 * jump 0x0609D6C8, fire charge 0x0609D4C4 + 0x0609D530 (anim 0x1A in 0x060F82C8, then
 * projectile 0xA via 0x0609ACF8), glow 0x0609E060 (anim 0x23 in 0x060F8308), flash 0x0609DDB8
 * (anim 0x26 in 0x060F82E8), hurt aura 0x0609D3B4 + 0x0609D3FC (anim 0x21 in 0x060F82A8),
 * death: shock ring 0x0609DD58 + 0x0609D740 (anim 0x23 in 0x060F82A8, scale 0x060F82A0 /
 * 0x060F82A4: sets +0x48 of every collapsing wall 0x1039 it reaches, rubble 0x0609DF0C,
 * screen shake CTRL+0x20 |= 0x80), cut-down body 0x0609DC18 (anim 0x18, 0x060F8298 /
 * 0x060F829C) + head 0x0609DADC / 0x0609DB48 (anim 0x22 in 0x060F82E8), stage clear
 * 0x0609DA9A (fade to white, 0x0602529E = (& 0xFFF8) | 10). HP 0x060C4C94[difficulty]
 * (25/30/35), boss bar 0x06061B58 every frame. Last attack kind that hurt it 0x060F8294,
 * invulnerable frames 0x060F8296 (32 after a hit). All its random numbers are
 * ((rand >> 8) / 10) (A.BIN 0x0600A55C).
 * Modes: 0 idle (Sho's shuriken in the box ahead or Sho close and attacking -> 0xF: dodge
 * with a blue palette pulse, or a wall jump), 1 fire charge (32 or 64 frames, shot 0xA small
 * or big (+0x10 & 8: breaks the floor tiles in VDP2 VRAM, walls 0x1039 and throws debris
 * 0x103A)), 2 / 3 / 4 sword combos (1 / 2 / 3 extra swings, lunge at frame 5), 5 jump at Sho
 * (air time (|dx| + 20 px) / 8 - 19, 6..12 frames, 6 px/frame), 6 / 7 spin (7: rolls at 4
 * px/frame 1..8 times, stops 64 px before a wall), 8 high jump coming down on Sho's X
 * (crystal burst 0x0605AF84 at the top of the screen), 10..0xD walk (until it turns, or
 * Sho is within 80 px: action 0x24 sweep with a flash), 0x11 hurt aura (green pulse, then
 * pushed 6 px/frame), 0x13 death, 0x14 back flip, 0x15 leap to Sho's level. */
#include "enemy_more.h"
#include "anim.h"
#include "sprite.h"
#include "objects.h"
#include "collision.h"
#include "player_ext.h"
#include "objhandlers_f.h"
#include "fade.h"
#include "hud.h"
#include "fx.h"
#include "fixmath.h"

#define B09_DEFS 0x060C36C8u
#define B09_SCR  0x060E6ADCu
#define PAL0     0x060E65E0u
#define KIND     0x060F8294u
#define INV      0x060F8296u
#define BODYX    0x060F8298u
#define BODYY    0x060F829Cu
#define RINGX    0x060F82A0u           /* shock ring scale (x, y) */
#define RINGY    0x060F82A4u
#define ST_A8    0x060F82A8u           /* anim states */
#define ST_C8    0x060F82C8u
#define ST_E8    0x060F82E8u
#define ST_08    0x060F8308u
#define PROJ(i)  (PROJECTILES + (uint32_t)((int16_t)(i) * 0x40))
#define ACT      6
#define TMR      0x14
#define CNT      0x48
#define J4A      0x4A
#define C4C      0x4C
#define MODE     0x4E
#define SUB      0x50
#define F56      0x56
#define AN       0x58
#define FRM      0x5A
#define A(o)     RS16((o) + ACT)
#define M(o)     RS16((o) + MODE)
#define LEFT     RS32(0x060EA618u)
#define RIGHT    RS32(0x060EA620u)

static void add16(uint32_t a, int v) { W16(a, (uint16_t)(RS16(a) + v)); }
static void add32(uint32_t a, int32_t v) { W32(a, (uint32_t)(RS32(a) + v)); }
static uint32_t r10(void) { return ((uint32_t)sub_0602DDD2() >> 8) / 10u; }   /* A.BIN 0x0600A55C */
static int16_t slot09(void) { return (int16_t)Enemy_FindSlot(9); }
static int16_t cbase(void) { return RS16(0x060A9064u + 2u * (uint32_t)slot09()); }
static uint16_t bank(void) { return R16(0x060A9084u + 2u * (uint32_t)slot09()); }
static void anim(uint32_t st, int a) { Anim_Start(st, B09_DEFS, B09_SCR, cbase(), a); }
static void pal_normal(void) { Pal_Copy((bank() & 0xFFF) << 4, 0x10, PAL0); }
static void pal_fade(int r, int g, int b) { Pal_SetFaded((bank() & 0xFFF) << 4, 0x10, PAL0, r, g, b); }
static int32_t dirv(uint32_t o, int32_t v) { return (R16(o + 2) & 0x8000) ? -v : v; }
static void spr_pos(int32_t x, int32_t y)
{
    W16(SPR + 2, (uint32_t)((x - RS32(CAM_X)) >> 16));
    W16(SPR + 4, (uint32_t)((y - RS32(CAM_Y)) >> 16));
}

void Enemy9_Load(void)                                      /* 0x0609ABA8 */
{
    int16_t s = slot09();
    uint32_t off = R32(0x060A9070u + 4u * (uint32_t)s);
    int base = (uint16_t)RS16(0x060A9064u + 2u * (uint32_t)s);
    for (int i = 0; i < 0x9E; i++) {
        uint32_t r = 0x060E60F2u + (uint32_t)i * 8;                    /* {size, mode, w/8, h} */
        Char_Register((base + i) & 0xFFFF, Ram_Ptr(r + 4)[0] * 8, Ram_Ptr(r + 5)[0], RS16(r + 2), off);
        off += (uint32_t)R16(r) * 32;
    }
    uint16_t b = R16(0x060A9084u + 2u * (uint32_t)s);
    Pal_Copy((b & 0xFFF) << 4, 0x10, 0x060E65E0u);
    for (uint16_t k = 1; k < 6; k++) Pal_Copy((uint16_t)(b + k) * 16, 0x10, 0x060E65E0u + 0x20u * k);
}

static void Boss09_Init(void)                               /* 0x0609B338 */
{
    uint32_t o = O;
    W16(o + ACT, 0);
    anim(o + AN, RS16(o + ACT));
    AND16(o + 2, 0xFFFE);
    W16(o + 0x16, R16(0x060C4C94u + 2u * (uint32_t)RS16(0x060252C4u)));
    for (uint32_t k = 0x48; k <= 0x56; k += 2) W16(o + k, 0);
    W32(o + 8, 0x04F80000u);
    W32(o + 0xC, (uint32_t)(RS32(CAM_Y) - 0x300000));
    W16(o + MODE, 8);
    W16(o + ACT, 0x12);
    W16(KIND, 0xFFFF);
    W16(INV, 0);
    EM_TrailReset();
    anim(ST_08, 0x23);
}

static int Boss09_Face(void)                                /* 0x0609D2DE: 1 = turned round */
{
    uint32_t o = O;
    uint16_t old = R16(o + 2);
    AND16(o + 2, 0x7FFF);
    if (RS32(PL + 8) < RS32(o + 8)) OR16(o + 2, 0x8000);
    return (R16(o + 2) & 0x8000) != (old & 0x8000);
}

static void Boss09_Decide(void)                             /* 0x0609D0A4 */
{
    static const uint32_t t[7] = { 0x060C4C4Eu, 0x060C4C58u, 0x060C4C62u, 0x060C4C6Cu, 0x060C4C76u, 0x060C4C80u, 0x060C4C8Au };
    uint32_t o = O;
    uint16_t by = (uint16_t)((uint32_t)EM_Abs(RS32(o + 0xC) - RS32(PL + 0xC)) >> 19);
    uint16_t bx = (uint16_t)((uint32_t)EM_Abs(RS32(o + 8) - RS32(PL + 8)) >> 19);
    if (by >= 0xB) { if (bx < 0xF) W16(o + MODE, 0x15); return; }
    int b;
    if (bx < 5) b = 0; else if (bx <= 9) b = 1; else if (bx <= 0xE) b = 2; else if (bx <= 0x13) b = 3;
    else if (bx <= 0x1D) b = 4; else if (bx <= 0x22) b = 5; else if (bx < 100) b = 6;
    else return;
    W16(o + MODE, Ram_Ptr(t[b] + r10() % 10u)[0]);                  /* A.BIN 0x0600CEF4 */
}

static int Boss09_Run(int32_t v)                            /* 0x0609D34E: 1 = turned (idle) */
{
    uint32_t o = O;
    if (!Boss09_Face()) {
        W32(o + 0x30, (uint32_t)dirv(o, v));
        if (RS16(o + FRM) == 8) W32(o + 0x30, 0);
        return 0;
    }
    W16(o + MODE, 0); W16(o + ACT, 0); W32(o + 0x30, 0);
    return 1;
}

static int Boss09_Close(void)                               /* 0x0609D660: sweep when Sho is near */
{
    uint32_t o = O;
    if (EM_Abs(RS32(o + 8) - RS32(PL + 8)) < 0x500000 && !(R16(PL + 2) & 0x4000)) {
        W16(o + ACT, 0x24); W16(o + MODE, 0); W32(o + 0x30, 0); W16(o + SUB, 0);
        return 1;
    }
    return 0;
}

static int Boss09_WallJump(void)                            /* 0x0609D6C8 */
{
    uint32_t o = O;
    if (RS32(o + 8) < LEFT + 0x400000 && !(R16(o + 2) & 0x8000)) return 0;
    if (RIGHT - 0x400000 < RS32(o + 8) && (R16(o + 2) & 0x8000)) return 0;
    W32(o + 0x34, 0x44000);
    W16(o + ACT, 3);
    W16(o + MODE, 0x14);
    return 1;
}

static void Boss09_Shot(int32_t x, int32_t y, uint16_t big)  /* 0x0609ACF8: projectile 0xA */
{
    int s = Projectile_Spawn(0xA, x, y, 0, 0);
    if ((int16_t)s == -1) return;
    OR16(PROJ(s) + 0x10, big);
    OR16(PROJ(s) + 4, R16(O + 2) & 0x8000);
}

static void Boss09_FireInit(void)                           /* 0x0609D4C4 */
{
    anim(ST_C8, 0x1A);
    Pal_Copy((uint16_t)(bank() + 1) * 16, 0x10, 0x060E6600u);
}

static void Boss09_Fire(void)                               /* 0x0609D530: the charge, then the shot */
{
    uint32_t o = O;
    spr_pos(RS32(o + 8) + dirv(o, -0xF0000), RS32(o + 0xC) - 0x1A0000);
    W16(SPR + 0x10, (R16(o + 2) & 0x8000) | 0x200);
    W16(SPR + 0x0C, 0); W16(SPR + 0x0E, 0);
    W16(SPR + 0x0A, (uint16_t)(bank() + 1));
    W32(SPR + 0x18, 0x81);
    Anim_Tick(ST_C8);
    if (R16(ST_C8) & 0x8000) {
        Boss09_Shot(RS32(o + 8) + dirv(o, 0x330000), RS32(o + 0xC) - 0x2E0000, R16(o + 4) & 8);
        AND16(o + AN, 0xFFFE);
        add16(o + FRM, 1);
    }
}

static void Boss09_Glow(void)                               /* 0x0609E060 */
{
    uint32_t o = O;
    W16(SPR + 2, (uint32_t)((RS32(o + 8) - RS32(CAM_X)) >> 16));
    W16(SPR + 4, (uint16_t)(((RS32(o + 0xC) - RS32(CAM_Y)) >> 16) - 0x20));
    W16(SPR + 0x10, (R16(o + 2) & 0x8000) | 0x1A00);
    W16(SPR + 0x0C, 0);
    W16(SPR + 0x0A, (uint16_t)(bank() + 3));
    W32(SPR + 0x18, 1);
    W16(SPR + 0x0E, 0);
    W32(SPR + 0x1C, 0xC000); W32(SPR + 0x20, 0xC000);
    if (R16(0x060143EAu) & 1) OR16(ST_08, 0x4000); else AND16(ST_08, 0xBFFF);
    Anim_Tick(ST_08);
}

static void Boss09_Flash(int32_t x, int32_t y, int32_t dx, int16_t kind)   /* 0x0609DDB8 */
{
    uint32_t o = O;
    anim(ST_E8, 0x26);
    W16(ST_E8 + 2, 1);
    if (R16(o + 2) & 0x8000) dx = -dx;
    spr_pos(x + dx, y);
    uint16_t f = R16(o + 2) & 0x8000;
    W16(SPR + 0x10, f | 0x200);
    if (R16(0x060143EAu) & 1) W16(SPR + 0x10, f | 0x4200);
    if (kind == 1) { W16(SPR + 0x10, (R16(SPR + 0x10) & 0x7FFF) | 0x400); W16(SPR + 0x14, 0xFFC0); }
    W16(SPR + 0x0C, 0); W16(SPR + 0x0E, 0);
    W16(SPR + 0x0A, (uint16_t)(bank() + 1));
    W32(SPR + 0x18, 1);
    Anim_Tick(ST_E8);
}

static void Boss09_Aura(void)                               /* 0x0609D3FC: hurt aura (anim 0x21) */
{
    uint32_t o = O;
    spr_pos(RS32(o + 8), RS32(o + 0xC));
    W16(SPR + 0x10, (R16(o + 2) & 0x8000) | 0x1A00);
    W16(SPR + 0x0C, 0);
    W16(SPR + 0x0A, (uint16_t)(bank() + 2));
    W32(SPR + 0x1C, (uint32_t)(RS16(o + C4C) * 0x555));
    W32(SPR + 0x20, (uint32_t)(RS16(o + C4C) * 0x555));
    W32(SPR + 0x18, 0xFFFFFFFFu);
    W16(SPR + 0x0E, 0);
    if (R16(0x060143EAu) & 1) Anim_Tick(ST_A8);
}

static void Boss09_Rubble(void)                             /* 0x0609DF0C: rubble on both sides */
{
    uint32_t o = O;
    uint16_t n = (uint16_t)((uint16_t)(int16_t)(FixMul(0x400000, RS32(RINGX) + 0x2000) >> 16) >> 4);
    for (uint16_t i = 0; i < n; i++) {
        uint16_t part = (uint16_t)(r10() & 7);
        int32_t x = RS32(o + 8) - (int32_t)i * 0x100000;
        x -= (int32_t)((r10() & 7) << 16);
        ObjF_RubbleSpawn(x, RS32(o + 0xC), part, 1, 0x8000);
        part = (uint16_t)(r10() & 7);
        x = RS32(o + 8) + (int32_t)i * 0x100000;
        x += (int32_t)((r10() & 7) << 16);
        ObjF_RubbleSpawn(x, RS32(o + 0xC), part, 1, 0);
    }
}

static void Boss09_Ring(void)                               /* 0x0609D740: death shock ring */
{
    uint32_t o = O;
    if (!FROZEN) {
        if ((R16(ST_A8) & 3) == 2) {
            add32(RINGX, RS16(o + J4A) * 0x24);
            add32(RINGY, RS16(o + J4A) * 0x24);
            for (uint32_t k = 0; k < OBJ_SLOTS; k++) {                  /* collapse the walls it reaches */
                uint32_t w = OBJ_TABLE + k * OBJ_SIZE;
                if (R16(w) != 0x1039 || RS16(w + 0x48) != 0) continue;
                if (EM_Abs(RS32(w + 8) - RS32(o + 8)) < FixMul(0x400000, RS32(RINGX) + 0x2000)) W16(w + 0x48, 1);
            }
            if ((R16(o + J4A) & 7) == 2) Boss09_Rubble();
            add16(o + J4A, 1);
            OR16(0x060EA638u, 0x80);                                    /* screen shake */
            if (!(R16(o + 4) & 0x20)) { sub_06007CDE(0x10E); OR16(o + 4, 0x20); }
        }
        if ((R16(ST_A8) & 3) == 3) {
            int v = (int)((RS16(o + F56) & 0x7FFF) << 1);
            Pal_SetFaded((uint16_t)(bank() + 3) * 16, 0x10, 0x060E6640u, v, v, v);
            add16(o + F56, 1);
            AND16(0x060EA638u, 0xFF7F);
        }
    }
    int16_t sx = (int16_t)((RS32(o + 8) - RS32(CAM_X)) >> 16), sy = (int16_t)((RS32(o + 0xC) - RS32(CAM_Y)) >> 16);
    if (A(o) == 0x19) sy = (int16_t)(sy - 0x2F);
    if (A(o) == 0x17) { sx = (int16_t)(sx + ((R16(o + 2) & 0x8000) ? -0x18 : 0x18)); sy = (int16_t)(sy - 8); }
    W16(SPR + 2, (uint16_t)sx); W16(SPR + 4, (uint16_t)sy);
    W32(SPR + 0x1C, (uint32_t)(RS32(RINGX) + 0x2000));
    W32(SPR + 0x20, (uint32_t)(RS32(RINGY) + 0x2000));
    uint16_t ev = R16(ST_A8) & 3;
    AND16(ST_A8, 0xBFFF);
    if (ev == 1) {
        uint16_t k = R16(0x060143EAu) & 3;
        if (k == 0) { W32(SPR + 0x1C, 0x1000); W32(SPR + 0x20, 0x1000); }
        else if (k == 2) { W32(SPR + 0x1C, 0x2000); W32(SPR + 0x20, 0x2000); }
        else OR16(ST_A8, 0x4000);
    }
    W16(SPR + 0x10, 0x1A00);
    W32(SPR + 0x18, 1);
    W16(SPR + 0x0A, (uint16_t)(bank() + 3));
    W16(SPR + 0x0C, 0);
    W16(SPR + 0x0E, (RS16(o + F56) >> 4) & 3);
    if (!(R16(ST_A8) & 0x8000) && ((R16(ST_A8) & 3) == 1 || (R16(0x060143EAu) & 1))) Anim_Tick(ST_A8);
}

static void Boss09_Body(void)                               /* 0x0609DC18: cut-down body (anim 0x18) */
{
    uint32_t o = O;
    anim(SCRATCH_ANIM, 0x18);
    if (!FROZEN && (R16(o + AN) & 3) == 1) {
        add32(BODYX, (R16(o + 2) & 0x8000) ? RS16(o + CNT) * 0x200 : RS16(o + CNT) * -0x200);
        add32(BODYY, RS16(o + CNT) * 0x200);
    }
    spr_pos(RS32(BODYX), RS32(BODYY));
    W16(SPR + 0x10, (R16(o + 2) & 0x8000) | 0x200);
    W32(SPR + 0x18, 0);
    W16(SPR + 0x0A, bank());
    W16(SPR + 0x0C, 0);
    if ((R16(o + AN) & 3) == 2) W16(SPR + 0x0E, (uint16_t)(RS16(o + J4A) >> 4));
    Anim_Tick(SCRATCH_ANIM);
}

static void Boss09_Head(void)                               /* 0x0609DB48 (anim 0x22 from 0x0609DADC) */
{
    uint32_t o = O;
    W16(SPR + 2, (uint32_t)((RS32(o + 8) - RS32(CAM_X)) >> 16));
    W16(SPR + 4, (uint16_t)(((RS32(o + 0xC) - RS32(CAM_Y)) >> 16) - 0x2F));
    W16(SPR + 0x10, (R16(o + 2) & 0x8000) | 0x200);
    W16(SPR + 0x0C, 0); W16(SPR + 0x0E, 0);
    W16(SPR + 0x0A, (uint16_t)(bank() + 5));
    W32(SPR + 0x18, 1);
    if ((R16(o + AN) & 3) == 2) W16(SPR + 0x0E, (uint16_t)(RS16(o + F56) >> 2));
    if (!(R16(ST_E8) & 0x8000)) Anim_Tick(ST_E8);
}

static void Boss09_Clear(void)                              /* 0x0609DA9A */
{
    Fade_SetTarget(0x100, 0x100, 0x100);
    Fade_SetEndLayerMask(0);
    Fade_SetNextSpeed(0x20000, 1);
    Fade_StartOut();
    W16(0x0602529Eu, (R16(0x0602529Eu) & 0xFFF8) | 10);
    W16(O, 0xFFFF);
}

void EnemyHandler_0009(void)                                /* 0x0609B448 */
{
    /* PC: the weapon damage passed to 0x0604931E lives in a stack slot (r15 + 10) that is only
     * written for actions 0xB/0xE/0x10/0x12/0x24/0x29 (1) and 0x13 (2); for the others the
     * original reuses whatever is left there, here: the last value written. */
    static int16_t s_dmg = 1;
    uint32_t o = O;
    if (R16(o + 2) & 1) Boss09_Init();
    Hud_DrawBossBar(RS16(0x060C4C94u + 2u * (uint32_t)RS16(0x060252C4u)), RS16(o + 0x16));
    if (FROZEN) goto draw;
    if (A(o) == 0 || A(o) == 0x15 || A(o) == 0x12) Boss09_Face();
    if (!(R16(PL + 2) & 8)) {
        int16_t a = A(o);
        if (a == 0xB || a == 0xE || a == 0x10 || a == 0x12 || a == 0x24 || a == 0x29) s_dmg = 1;
        else if (a == 0x13) s_dmg = 2;
        if ((int16_t)Enemy_AttackPlayer(s_dmg, 0, 0) == 3) sub_06007CDE(0x10C);
    }
    if (!(R16(o + 2) & 8) && !(R16(o + 4) & 0x10)) {
        W16(KIND, 0xFFFF);
        if ((int16_t)Enemy_CheckPlayerHits(0xC000) == 2) W16(KIND, (uint16_t)(int16_t)Player_AttackKind());
    }
    if (M(o) != 0x12 && M(o) != 0x11 && M(o) != 0x13) {
        pal_normal();
        if (!(R16(o + 4) & 0x10) && (R16(o + 2) & 8)) {             /* hurt */
            W16(o + ACT, 0x16); W16(o + MODE, 0x12);
            W32(o + 0x30, 0); W16(o + SUB, 0);
            AND16(o + 4, 0xFFFE);
            W32(o + 0x34, 0x30000);
            AND16(o + 4, 0xFFFD);
            OR16(o + 4, 0x10);
            W16(INV, 0x20);
            if (RS16(o + 0x16) > 0) sub_06007CDE(0x206);
            goto phys;
        }
    }
    if (M(o) != 0x13 && M(o) != 0x11 && M(o) != 0x12) {
        if (RS16(INV) == 0) { AND16(o + 4, 0xFFEF); AND16(o + 2, 0xFFF7); }
        else add16(INV, -1);
    }
    if (A(o) != 0 && A(o) != 0x15) goto Lbc36;
    if (RS16(o + TMR) == 0) {                                       /* idle: pick the next move */
        static const int16_t wait[0x16] = { 0, 0x10, 2, 2, 2, 0x10, 5, 3, 6, 3, 8, 6, 5, 3, 2, 0, 0, 0, 0, 0, 0, 2 };
        AND16(o + 4, 0xFFFE); AND16(o + 4, 0xFFFD); AND16(o + 4, 0xFFFB);
        Boss09_Decide();
        if (M(o) >= 1 && M(o) <= 0x15 && wait[M(o)] != 0) W16(o + TMR, (uint16_t)wait[M(o)]);
        goto Lbc36;
    }
    add16(o + TMR, -1);
    if (RS16(o + TMR) != 0) goto Lbc36;
    switch (M(o)) {
    case 1:
        W16(o + SUB, 0); W16(o + MODE, 1); W16(o + ACT, 0xD);
        W16(o + CNT, (uint16_t)((r10() & 1) * 0x20 + 0x20));
        if (RS16(o + CNT) == 0x40) OR16(o + 4, 8); else AND16(o + 4, 0xFFF7);
        OR16(o + 4, 2);
        Boss09_FireInit();
        break;
    case 2: case 3: case 4:
        W16(o + CNT, 0); W16(o + SUB, 0); W16(o + ACT, 10);
        break;
    case 5: {
        int16_t d = (int16_t)((uint32_t)EM_Abs(RS32(PL + 8) - RS32(o + 8)) >> 16);
        W16(o + J4A, (uint16_t)(int16_t)(EM_Sdiv((d + 20) / 2, 4) - 19));   /* A.BIN 0x060092BC */
        if (RS16(o + J4A) < 6) W16(o + J4A, 6);
        if (RS16(o + J4A) > 0xC) W16(o + J4A, 0xC);
        W16(o + SUB, 0); W16(o + MODE, 5); W16(o + ACT, 2);
        break;
    }
    case 6: W16(o + CNT, 0); W16(o + SUB, 0); W16(o + MODE, 6); W16(o + ACT, 0xF); break;
    case 7: W16(o + CNT, 0); W16(o + SUB, 0); W16(o + MODE, 7); W16(o + ACT, 0xF); OR16(o + 4, 1); break;
    case 8: W16(o + CNT, 0); W16(o + SUB, 0); W16(o + MODE, 8); W16(o + ACT, 2); break;
    case 10: W16(o + CNT, 0); W16(o + SUB, 1); W16(o + MODE, 10); W16(o + ACT, 5); break;
    case 0xB: W16(o + CNT, 0); W16(o + SUB, 2); W16(o + MODE, 0xB); W16(o + ACT, 5); break;
    case 0xC: W16(o + CNT, 0); W16(o + SUB, (uint16_t)(r10() % 3u + 3)); W16(o + MODE, 0xC); W16(o + ACT, 5); break;
    case 0xD: W16(o + CNT, 0); W16(o + SUB, (uint16_t)(r10() % 3u + 5)); W16(o + MODE, 0xD); W16(o + ACT, 5); break;
    case 0xE:
        W16(o + CNT, 0); W16(o + SUB, 0);
        if (!Boss09_WallJump()) { W16(o + MODE, 2); W16(o + ACT, 10); }
        break;
    case 0x15: W16(o + SUB, 0); W16(o + MODE, 0x15); W16(o + ACT, 0x28); OR16(o + 4, 1); break;
    default: break;
    }
Lbc36:
    if ((A(o) == 0 || A(o) == 0x15) && !(R16(o + 4) & 0x10)) {
        /* (the original passes x - 64 px as the box top: a bug, kept) */
        if (Projectile_InBox(RS32(o + 8) - 0x900000, RS32(o + 8) - 0x400000, RS32(o + 8) + 0x900000, RS32(o + 0xC) + 0x100000)) {
            W16(o + ACT, 9); W16(o + CNT, 0); W16(o + SUB, 0); W16(o + MODE, 0xF);
            goto phys;
        }
        if (EM_Abs(RS32(o + 8) - RS32(PL + 8)) < 0x780000 && EM_Abs(RS32(o + 0xC) - RS32(PL + 0xC)) < 0x500000 &&
            (int16_t)Player_AttackKind() != 6) {
            W16(o + SUB, 0);
            if ((r10() & 1) == 0 || !Boss09_WallJump()) { W16(o + ACT, 9); W16(o + MODE, 0xF); }
            goto phys;
        }
    }
    if (M(o) == 0xF && A(o) == 9 && (R16(o + AN) & 3) == 1) {      /* dodge: blue pulse */
        pal_fade(0, 0, (R16(o + CNT) & 7) * 10);
        add16(o + CNT, 1);
    }
    if (M(o) == 0x15) {                                             /* leap to Sho's level */
        if (A(o) == 0x28) {
            if ((R16(o + AN) & 3) == 1) { sub_06007CDE(0x126); W32(o + 0x34, 0xC0000); AND16(o + AN, 0xFFFE); }
            if ((R16(o + AN) & 3) == 2) { W16(o + ACT, 0x29); AND16(o + AN, 0xFFFD); goto phys; }
        }
        if (A(o) == 0x29) {
            W32(o + 0x30, (uint32_t)dirv(o, 0x10000));
            if (RS32(o + 0x34) < 0) { W16(o + ACT, 0x27); AND16(o + 4, 0xFFFE); }
            add16(o + CNT, 1);
        }
        if (A(o) == 0x27 && (R16(o + AN) & 1) && (R16(o + 2) & 0x10)) {
            add16(o + FRM, 1); AND16(o + AN, 0xFFFE); W32(o + 0x30, 0);
        }
    }
    if (M(o) == 5) {                                                /* jump at Sho */
        if (A(o) == 2 && (R16(o + AN) & 1)) {
            sub_06007CDE(0x126); W32(o + 0x34, 0x30000); AND16(o + AN, 0xFFFE); OR16(o + 4, 4);
        }
        if (R16(o + 4) & 4) {
            if ((int32_t)R16(o + 0x1A) < RS16(o + J4A)) W32(o + 0x34, 0x30000);
            else { W16(o + ACT, 0xE); AND16(o + 4, 0xFFFB); }
        }
        if (A(o) == 0xE && (R16(o + AN) & 1) && (R16(o + 2) & 0x10)) {
            AND16(o + AN, 0xFFFE); W16(o + ACT, 0x25); W32(o + 0x30, 0); AND16(o + 4, 0xFFFB);
            goto phys;
        }
        if (A(o) == 0x25) W32(o + 0x30, 0);
        if (R16(o + 4) & 4) W32(o + 0x30, (uint32_t)dirv(o, 0x60000));
    }
    if (M(o) == 8) {                                                /* high jump down on Sho */
        if (A(o) == 2) {
            if (R16(o + AN) & 1) { W32(o + 0x34, 0xF0000); AND16(o + AN, 0xFFFE); OR16(o + 4, 4); goto phys; }
            if (!(R16(o + 4) & 4) || RS32(o + 0x34) >= 0) goto Lc1a4;
            W16(o + ACT, 0x12);
            W32(o + 8, R32(PL + 8));
            Crystal_Burst(RS32(o + 8), RS32(0x060EA61Cu) + 0x300000);   /* 0x0605AF84 */
            AND16(o + 4, 0xFFFB);
            goto phys;
        }
Lc1a4:
        if (A(o) != 0x12 || !(R16(o + 2) & 0x10)) goto Lc1d2;
        W16(o + ACT, 0x14); W16(o + MODE, 0);
        goto phys;
    }
Lc1d2:
    if (M(o) == 1 && (R16(o + AN) & 3) == 1) {                      /* fire charge */
        if (RS16(o + CNT) == 0 && (R16(o + 4) & 2)) {
            AND16(o + 4, 0xFFFD);
            W16(ST_C8 + 2, (R16(o + 4) & 8) ? 6 : 10);
            pal_normal();
        } else {
            if ((R16(o + CNT) & 3) == 0) pal_fade(0, 0, (R16(o + CNT) & 0xF) << 3);
            add16(o + CNT, -1);
        }
    }
    if (M(o) == 0x11) {                                             /* hurt aura */
        if ((R16(o + AN) & 3) == 2) W32(o + 0x30, (uint32_t)dirv(o, 0x60000));
        if ((R16(o + AN) & 3) == 1 && RS16(o + C4C) < 0x30) add16(o + C4C, 1);
        pal_fade(0, 0, (R16(o + CNT) & 7) * 10);
        add16(o + CNT, 1);
    }
    if (M(o) == 0x14 && A(o) == 3) {                                /* back flip */
        if (R16(o + AN) & 1) W32(o + 0x30, (R16(o + 2) & 0x8000) ? 0x40000u : 0xFFFC0000u);
        if (R16(o + 2) & 0x10) { add16(o + FRM, 1); AND16(o + AN, 0xFFFE); W32(o + 0x30, 0); }
    }
    if (A(o) == 0x16) W32(o + 0x30, (R16(o + 2) & 0x8000) ? 0x10000u : 0xFFFF0000u);
    {
        int skip = 0;
        if (M(o) == 10 && A(o) == 5) skip = Boss09_Run(0x14000) || Boss09_Close();
        if (!skip && M(o) == 0xB && A(o) == 5) skip = Boss09_Run(0x14000) || Boss09_Close();
        if (!skip && M(o) == 0xC && A(o) == 5) skip = Boss09_Run(0x14000) || Boss09_Close();
        if (!skip && M(o) == 0xD && A(o) == 5) skip = Boss09_Run(0x14000) || Boss09_Close();
        if (skip) goto phys;
    }
    if (M(o) >= 2 && M(o) <= 4 && A(o) == 0xB && RS16(o + FRM) == 5)   /* sword lunge */
        W32(o + 0x30, (R16(o + 2) & 0x8000) ? 0xFFFDE000u : 0x22000u);
    if (M(o) == 7 && A(o) == 0x10) {                                /* rolling */
        if (EM_Abs(RS32(PL + 8) - RS32(o + 8)) < 0x600001 || !Boss09_Face()) {
            if (!(R16(o + 2) & 0x8000)) {
                if (RIGHT - RS32(o + 8) - 0x400000 >= 0) goto Lc788;
            } else if (LEFT - RS32(o + 8) + 0x400000 < 1) goto Lc788;
        }
        W16(o + ACT, 0x11); W16(o + SUB, 0); W32(o + 0x30, 0); AND16(o + 4, 0xFFFE);
        goto phys;
Lc788:
        W32(o + 0x30, (uint32_t)dirv(o, 0x40000));
    }
    if (M(o) == 0x13) {                                             /* death: white fade */
        if ((A(o) == 0x17 && (R16(o + AN) & 3) == 1) || (A(o) == 0x19 && (R16(o + AN) & 3) != 0)) {
            int v = R16(o + CNT) & 0xFF;
            pal_fade(v, v, v);
            add16(o + CNT, 1);
        }
    }
    if (R16(o + AN) & 0x8000) {                                     /* animation over */
        W32(o + 0x30, 0);
        W16(o + TMR, 0);
        AND16(o + AN, 0x7FFF);
        if (M(o) == 0x13 && (A(o) == 0x17 || A(o) == 0x19)) { Boss09_Clear(); goto phys; }
        if (M(o) == 0x11 && A(o) == 0x13) {
            AND16(o + 4, 0xFFFE); AND16(o + 4, 0xFFEF); AND16(o + 2, 0xFFF7);
            W16(o + ACT, 0x14); W16(o + MODE, 0); W16(o + CNT, 0);
            pal_normal();
        }
        if (A(o) == 0x16) {
            if (!(R16(o + 2) & 0x4000)) {
                if (RS16(o + 0x16) < 1) {
                    int16_t k = RS16(KIND);
                    W16(o + MODE, 0x13);
                    sub_06007CDE(0x226);
                    anim(ST_A8, 0x23);                                  /* 0x0609DD58 */
                    W32(RINGX, 0); W32(RINGY, 0);
                    if (k == 0 || k == 1 || k == 2 || k == 3) {
                        W16(o + ACT, 0x19);
                        anim(ST_E8, 0x22);                              /* 0x0609DADC */
                        W32(BODYX, R32(o + 8));
                        W32(BODYY, (uint32_t)(RS32(o + 0xC) + 0x40000));
                    } else W16(o + ACT, 0x17);
                    W16(o + CNT, 0); W16(o + C4C, 0); W16(o + F56, 0); W16(o + J4A, 0);
                    OR16(PL + 4, 0x400);
                    goto phys;
                }
                if (r10() & 1) {
                    W16(o + MODE, 0x11); W16(o + ACT, 0x13);
                    anim(ST_A8, 0x21);                                  /* 0x0609D3B4 */
                    OR16(o + 4, 1);
                    W16(o + CNT, 0); W16(o + C4C, 0);
                    sub_06007CDE(0x106);
                    goto phys;
                }
            } else { W16(o + 0x22, 0); goto phys; }
        }
        if (A(o) == 10) {                                           /* sword: swings */
            W16(o + ACT, 0xB);
            if (M(o) == 2) W16(o + SUB, 0);
            if (M(o) == 3) W16(o + SUB, 1);
            if (M(o) == 4) W16(o + SUB, 2);
        } else if (A(o) == 0xB) {
            if (RS16(o + SUB) == 0) W16(o + ACT, 0x11);
            else { W16(o + 0x22, 0); add16(o + SUB, -1); }
        } else if (M(o) == 6 && (A(o) == 0xF || A(o) == 0x10)) {
            W16(o + ACT, A(o) == 0xF ? 0x10 : 0x11);
        } else if (M(o) == 7 && A(o) == 0xF) {
            W16(o + ACT, 0x10);
            W16(o + SUB, (uint16_t)((r10() & 7) + 1));
        } else if (M(o) == 7 && A(o) == 0x10) {
            if (RS16(o + SUB) == 0) { W16(o + ACT, 0x11); AND16(o + 4, 0xFFFE); }
            else { W16(o + 0x22, 0); add16(o + SUB, -1); }
        } else if (M(o) == 7 && A(o) == 0x11) {
            W16(o + ACT, 0); W16(o + MODE, 0);
        } else if (RS16(o + SUB) == 0) { W16(o + ACT, 0); W16(o + MODE, 0); }
        else { W16(o + 0x22, 0); add16(o + SUB, -1); }
    }
phys:
    Obj_Integrate(o);
    if (0 < LEFT - RS32(o + 8) + 0x300000) W32(o + 8, (uint32_t)(LEFT + 0x300000));
    if (RIGHT - RS32(o + 8) - 0x300000 < 0) W32(o + 8, (uint32_t)(RIGHT - 0x300000));
    Obj_CollideMap(o, 3, 6);
    Enemy_AnimSwitch();
draw:
    if (M(o) == 0xF && A(o) == 9 && (R16(o + AN) & 3) == 1) Boss09_Glow();
    if (M(o) == 8 && A(o) == 0x12) Boss09_Flash(RS32(o + 8), RS32(o + 0xC) - 0x2D0000, 0x100000, 1);
    if (M(o) == 0 && A(o) == 0x24 && RS16(o + FRM) == 8) Boss09_Flash(RS32(o + 8), RS32(o + 0xC) - 0x3A0000, 0x400000, 2);
    if (M(o) == 0x13 || M(o) == 0x11) AND16(o + AN, 0xBFFF);
    else if (R16(o + 4) & 0x10) { if (R16(0x060143EAu) & 1) OR16(o + AN, 0x4000); else AND16(o + AN, 0xBFFF); }
    if (M(o) == 0x11 && (R16(o + AN) & 3) == 2) Boss09_Aura();
    if (M(o) == 1 && (R16(o + AN) & 1)) Boss09_Fire();
    if (M(o) == 0x13) {
        if (A(o) == 0x17 && (R16(o + AN) & 3) == 1) Boss09_Ring();
        if (A(o) == 0x19) {
            if (RS16(o + J4A) < 0x20) Boss09_Body();
            if ((R16(o + AN) & 3) == 1) Boss09_Head();
            if ((R16(o + AN) & 3) == 2) Boss09_Ring();
        }
    }
    EM_Trail(R16(o + 4) & 1, o);
    W16(SPR + 0x0E, 0);
    W16(SPR + 2, (uint32_t)((RS32(o + 8) - RS32(CAM_X)) >> 16));
    W16(SPR + 4, (uint32_t)((RS32(o + 0xC) - RS32(CAM_Y)) >> 16));
    W16(SPR + 0x10, R16(o + 2) & 0x8000);
    W16(SPR + 0x0A, bank());
    W16(SPR + 0x0C, 0);
    W32(SPR + 0x18, 0);
    if (R16(o) != 0xFFFF) {
        if (M(o) == 0x13) {
            if ((R16(o + AN) & 3) == 1) W16(SPR + 0x0E, (RS16(o + J4A) >> 4) & 3);
            if (A(o) == 0x19 && (R16(o + AN) & 3) == 2) W16(SPR + 0x0E, (RS16(o + J4A) >> 4) & 3);
            if (RS16(o + J4A) > 0x1F) OR16(o + AN, 0x4000);
        }
        Anim_Tick(o + AN);
        AND16(o + AN, 0xBFFF);
    }
}

/* Projectile 0xA (0x0609AD74): the boss's fire wave (anim 0x1E, big one +0x10 & 8: 0x1D)
 * running along the floor at 5 px/frame, hurts Sho (1, big 2; mode 0x1100 / 0x8100). On Sho
 * it bursts (anim 0x20 / 0x1F). The big one breaks the floor tile row under it in VDP2
 * VRAM (0x25E60E80[column] = column * 8 + 0xF38), collapses the walls 0x1039 within 32 px and
 * throws debris 0x103A (0x0605A688). Anim event bit 0 = pause (sound 0x215). */
void Proj_FireWave0A(uint32_t e)
{
    int16_t s = slot09();
    if (R16(e + 4) & 1) {
        Anim_Start(e + 0x20, B09_DEFS, B09_SCR, RS16(0x060A9064u + 2u * (uint32_t)s), (R16(e + 0x10) & 8) ? 0x1D : 0x1E);
        AND16(e + 4, 0xFFFE);
        W32(e + 0x18, (R16(e + 4) & 0x8000) ? 0xFFFB0000u : 0x50000u);
    }
    AND16(e + 4, 0xFFF7);
    W16(e + 0x12, 0x7FFF);
    if (!FROZEN && !(R16(e + 0x10) & 1)) {
        int big = (R16(e + 0x10) & 8) != 0;
        int16_t r = (int16_t)Projectile_Collide(big ? 2 : 1, big ? 0x8100 : 0x1100);
        if (r == 4) {
            Anim_Start(e + 0x20, B09_DEFS, B09_SCR, RS16(0x060A9064u + 2u * (uint32_t)s), big ? 0x1F : 0x20);
            OR16(e + 0x10, 1);
        } else if (!(R16(e + 0x20) & 1)) {
            add32(e + 8, RS32(e + 0x18));
            if (big) {
                uint32_t col = ((uint32_t)RS32(e + 8) + 0xFC000000u) >> 20;
                W32(0x25E60E80u + col * 4u, col * 8u + 0xF38u);
                for (uint32_t k = 0; k < OBJ_SLOTS; k++) {
                    uint32_t w = OBJ_TABLE + k * OBJ_SIZE;
                    if (R16(w) == 0x1039 && RS16(w + 0x48) == 0 && EM_Abs(RS32(w + 8) - RS32(e + 8)) < 0x200000) W16(w + 0x48, 1);
                }
                uint16_t m = R16(e + 4) & 0x8000;
                if ((R16(e + 6) & 3) == 1) ObjF_DebrisSpawn(RS32(e + 8), RS32(e + 0xC) + 0x2A0000, 8, 0, m);
                if ((R16(e + 6) & 7) == 1) {
                    static const struct { uint8_t add, dy; uint16_t set; } d[6] = {
                        { 0, 0x31, 0 }, { 1, 0x2F, 1 }, { 2, 0x2E, 2 }, { 3, 0x30, 3 }, { 0, 0x28, 1 }, { 1, 0x2B, 0 } };
                    for (int k = 0; k < 6; k++) {
                        uint16_t fl = (uint16_t)((r10() + d[k].add) & 3);
                        int32_t y = RS32(e + 0xC) + (int32_t)(((r10() + d[k].dy) & 0xF) << 16);
                        ObjF_DebrisSpawn(RS32(e + 8), y, fl, d[k].set, m);
                    }
                }
            }
            W16(e + 6, R16(e + 6) + 1);
        } else {
            AND16(e + 0x20, 0xFFFE);
            sub_06007CDE(0x215);
        }
    }
    int32_t dx = RS32(e + 8) - RS32(CAM_X), dy = RS32(e + 0xC) - RS32(CAM_Y);
    if (dx < -0x800000 || 0x1C00000 < dx || dy < -0x800000 || 0x1C00000 < dy) W16(e, 0xFFFF);
    W16(SPR + 2, (uint32_t)(dx >> 16));
    W16(SPR + 4, (uint32_t)(dy >> 16));
    W16(SPR + 0x0C, 0); W16(SPR + 0x0E, 0);
    W16(SPR + 0x0A, (uint16_t)(R16(0x060A9084u + 2u * (uint32_t)s) + 1));
    W16(SPR + 0x10, (R16(e + 4) & 0x8000) | 0x200);
    W32(SPR + 0x18, 0x81);
    if (R16(e) != 0xFFFF) {
        if (R16(0x060143EAu) & 1) OR16(e + 0x20, 0x4000); else AND16(e + 0x20, 0xBFFF);
        Anim_Tick(e + 0x20);
    }
    if (R16(e + 0x20) & 0x8000) W16(e, 0xFFFF);
}
