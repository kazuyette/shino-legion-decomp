/* Enemy type 0x001A: stage 3-2 boss (index 5, slot 0), the plant-armoured ninja.
 * Loader 0x0607AD88 (0x1C5 chars of 0x060DF32A, palettes 0x060E0150 / 70 / 90 / B0 into
 * banks +0..+3), handler 0x0607AFC0, init 0x0607AEA8 (x 1267, y 1440, mode 0xF = jump-in
 * intro, Sho's controls held by flags4 0x400 until it lands), face Sho 0x0607D47C (1 = turned),
 * decision 0x0607CF9E (mode = table 0x060BA52E + 10 x band [((rand / 7) >> 5) % 10]: bands
 * of |dx| / 8 px <4, 4-8, 9-19, 20-24, 25-29, 30-34, 35-39, 40-99 (none beyond), tables
 * 0x060BA52E.. when |dy| / 8 px <= 10, 0x060BA57E.. otherwise), spawners 0x0607D4CC (proj
 * 0x2A), 0x0607D53E (0x2B), 0x0607D5A4 (0x2C), death debris 0x0607E038, death body
 * 0x0607DEE8 / overlay 0x0607E3B0 + 0x0607E3E0 (anims 0x1C / 0x1D, state 0x060F7AB4, body
 * position 0x060F7AAC / 0x060F7AB0), turn-around 0x0607E362, stage clear 0x0607E320 (fade to
 * white, 0x0602529E = (& 0xFFF8) | 10). HP 0x060BA528[difficulty] (20/25/30), boss bar
 * 0x06061B58 every frame. Last attack kind that hurt it: 0x060F7AA8.
 * Fields: +6 action, +0x14 think timer (16 frames), +0x48 counter, +0x4A hop length,
 * +0x4C colour / death counter, +0x4E mode, +0x50 repeats / dash pattern, +0x56
 * invulnerable frames (32 after a hit), +4 bit 0 afterimages, bit 1 death blasts, bit 2
 * roll direction, bit 4 hurt; flags2 0x100 = no map collision.
 * Modes: 0 idle, 1 walk at Sho (-> 0x12 when close or after 320 frames), 2 spore shower
 * (green palette pulse, projectile 0x2C every 8 frames for 128 frames), 3 rolling leap,
 * 4 / 7 hop x2 (7: length (|dx| / 64 px) + 1, at most 3), 5 / 9 / 10 / 11 dashes, 8 /
 * 9..11 run (until it has to turn), 0xC back step, 0xD slide, 0xF intro, 0x10 death
 * (action 0x11 sinks / 0x13 cut down: body + overlay drift, debris 0x2A / 0x2B), 0x11 hurt
 * (action 0x12, pushed 1 px/frame; then 1/2 mode 0x13 roll (wall probe 32 px ahead on
 * the layer 0x060EA5B8, up to 121 frames) or mode 3), 0x12 leap up to a ceiling (probe
 * 95 px above, attribute bit 4) and drop (mode 0xD). */
#include "enemy_more.h"
#include "anim.h"
#include "sprite.h"
#include "objects.h"
#include "collision.h"
#include "player_ext.h"
#include "fade.h"
#include "hud.h"
#include "fx.h"
#include "fixmath.h"

#define B1A_DEFS 0x060B7D90u
#define B1A_SCR  0x060E0674u
#define KIND     0x060F7AA8u           /* attack kind that hurt it last (-1 none) */
#define BODYX    0x060F7AACu
#define BODYY    0x060F7AB0u
#define OVL      0x060F7AB4u           /* death overlay anim state */
#define PROJ(i)  (PROJECTILES + (uint32_t)((int16_t)(i) * 0x40))
#define ACT      6
#define TMR      0x14
#define CNT      0x48
#define HOP      0x4A
#define C4C      0x4C
#define MODE     0x4E
#define SUB      0x50
#define INV      0x56
#define AN       0x58
#define A(o)     RS16((o) + ACT)
#define M(o)     RS16((o) + MODE)
#define LAYER    0x060EA5B8u

static void add16(uint32_t a, int v) { W16(a, (uint16_t)(RS16(a) + v)); }
static void add32(uint32_t a, int32_t v) { W32(a, (uint32_t)(RS32(a) + v)); }
static uint32_t r7(void) { return ((uint32_t)sub_0602DDD2() / 7u) >> 5; }   /* A.BIN 0x0600A55C */
static int16_t slot1a(void) { return (int16_t)Enemy_FindSlot(0x1A); }
static int16_t cbase(void) { return RS16(0x060A9064u + 2u * (uint32_t)slot1a()); }
static uint16_t bank(void) { return R16(0x060A9084u + 2u * (uint32_t)slot1a()); }

void Enemy1A_Load(void)                                     /* 0x0607AD88 */
{
    int16_t s = slot1a();
    uint32_t off = R32(0x060A9070u + 4u * (uint32_t)s);
    int base = (uint16_t)RS16(0x060A9064u + 2u * (uint32_t)s);
    for (int i = 0; i < 0x1C5; i++) {
        uint32_t r = 0x060DF32Au + (uint32_t)i * 8;                    /* {size, mode, w/8, h} */
        Char_Register((base + i) & 0xFFFF, Ram_Ptr(r + 4)[0] * 8, Ram_Ptr(r + 5)[0], RS16(r + 2), off);
        off += (uint32_t)R16(r) * 32;
    }
    uint16_t b = R16(0x060A9084u + 2u * (uint32_t)s);
    Pal_Copy((b & 0xFFF) << 4, 0x10, 0x060E0150u);
    Pal_Copy((uint16_t)(b + 1) * 16, 0x10, 0x060E0170u);
    Pal_Copy((uint16_t)(b + 2) * 16, 0x10, 0x060E0190u);
    Pal_Copy((uint16_t)(b + 3) * 16, 0x10, 0x060E01B0u);
}

static void Boss1A_Init(void)                               /* 0x0607AEA8 */
{
    uint32_t o = O;
    W16(o + ACT, 1);
    Anim_Start(o + AN, B1A_DEFS, B1A_SCR, cbase(), RS16(o + ACT));
    AND16(o + 2, 0xFFFE);
    OR16(o + 2, 0x100);
    W16(o + 0x16, R16(0x060BA528u + 2u * (uint32_t)RS16(0x060252C4u)));
    for (uint32_t k = 0x48; k <= 0x56; k += 2) W16(o + k, 0);
    W32(o + 8, 0x04F30000u);
    W32(o + 0xC, 0x05A00000u);
    W16(o + MODE, 0xF);
    W16(KIND, 0xFFFF);
    EM_TrailReset();
    OR16(PL + 4, 0x400);
}

static int Boss1A_Face(void)                                /* 0x0607D47C: 1 = turned round */
{
    uint32_t o = O;
    uint16_t old = R16(o + 2);
    AND16(o + 2, 0x7FFF);
    if (RS32(PL + 8) < RS32(o + 8)) OR16(o + 2, 0x8000);
    return (R16(o + 2) & 0x8000) != (old & 0x8000);
}

static void Boss1A_Decide(void)                             /* 0x0607CF9E */
{
    static const uint32_t near[8] = { 0x060BA52Eu, 0x060BA538u, 0x060BA542u, 0x060BA54Cu, 0x060BA556u, 0x060BA560u, 0x060BA56Au, 0x060BA574u };
    static const uint32_t far[8]  = { 0x060BA57Eu, 0x060BA588u, 0x060BA592u, 0x060BA59Cu, 0x060BA5A6u, 0x060BA5B0u, 0x060BA5BAu, 0x060BA5C4u };
    uint32_t o = O;
    uint16_t by = (uint16_t)((uint32_t)EM_Abs(RS32(o + 0xC) - RS32(PL + 0xC)) >> 19);
    uint16_t bx = (uint16_t)((uint32_t)EM_Abs(RS32(o + 8) - RS32(PL + 8)) >> 19);
    const uint32_t *t = by < 0xB ? near : far;
    int b;
    if (bx < 4) b = 0; else if (bx <= 8) b = 1; else if (bx <= 0x13) b = 2; else if (bx <= 0x18) b = 3;
    else if (bx <= 0x1D) b = 4; else if (bx <= 0x22) b = 5; else if (bx <= 0x27) b = 6; else if (bx < 100) b = 7;
    else return;
    W16(o + MODE, Ram_Ptr(t[b] + r7() % 10u)[0]);                  /* A.BIN 0x0600CEF4 */
}

static void spawn2A(int32_t x, int32_t y, uint16_t p3, uint16_t p4)   /* 0x0607D4CC */
{
    int s = Projectile_Spawn(0x2A, x, y, 0, 0);
    if ((int16_t)s == -1) return;
    OR16(PROJ(s) + 0x10, p3);
    W16(PROJ(s) + 0x14, p4);
}

static void spawn2B(int32_t x, int32_t y, uint16_t p3, uint16_t p4)   /* 0x0607D53E */
{
    int s = Projectile_Spawn(0x2B, x, y, 0, 0);
    if ((int16_t)s == -1) return;
    OR16(PROJ(s) + 0x10, p3);
    W16(PROJ(s) + 0x14, p4);
}

static void spawn2C(int32_t x, int32_t y, uint16_t p3, uint16_t p4, uint16_t p5)   /* 0x0607D5A4 */
{
    int s = Projectile_Spawn(0x2C, x, y, 0, 0);
    if ((int16_t)s == -1) return;
    uint32_t p = PROJ(s);
    OR16(p + 4, R16(O + 2) & 0x8000);
    W16(p + 0x14, p3);
    OR16(p + 2, p4);
    OR16(p + 0x10, p5);
}

static void Boss1A_Debris(void)                             /* 0x0607E038: blasts on the dying boss */
{
    uint32_t o = O;
    int32_t rx, ry;
#define RXY() (rx = (int32_t)((r7() & 0xF) << 16), ry = (int32_t)((r7() & 0xF) << 16))
    if (RS16(o + C4C) < 0x20 || RS16(o + C4C) >= 0xE1) {
        RXY(); spawn2B(RS32(o + 8) + rx - 0x90000, RS32(o + 0xC) + ry - 0x60000, 1, 5);
        RXY(); spawn2B(RS32(o + 8) + rx, RS32(o + 0xC) + ry - 0x140000, 0, 10);
    } else {
        RXY(); spawn2B(RS32(o + 8) + rx - 0x200000, RS32(o + 0xC) + ry - 0x60000, 1, 5);
        RXY(); spawn2B(RS32(o + 8) + rx, RS32(o + 0xC) + ry - 0x120000, 0, 10);
        RXY(); spawn2B(RS32(o + 8) + rx - 0x80000, RS32(o + 0xC) + ry - 0xA0000, 1, 3);
        RXY(); spawn2B(RS32(o + 8) + rx + 0x200000, RS32(o + 0xC) + ry - 0x80000, 1, 7);
    }
#undef RXY
}

static void Boss1A_Pieces(void)                             /* 4 x projectile 0x2A */
{
    uint32_t o = O;
    for (int k = 0; k < 4; k++) {
        uint16_t p4 = (uint16_t)(r7() % 6u);
        uint16_t p3 = (uint16_t)(r7() & 7);
        spawn2A(RS32(o + 8), RS32(o + 0xC), p3, p4);
    }
}

static void Boss1A_Clear(void)                              /* 0x0607E320 */
{
    Fade_SetTarget(0x100, 0x100, 0x100);
    Fade_SetEndLayerMask(0);
    Fade_SetNextSpeed(0x20000, 1);
    Fade_StartOut();
    W16(0x0602529Eu, (R16(0x0602529Eu) & 0xFFF8) | 10);
    W16(O, 0xFFFF);
}

static void Boss1A_TurnRun(void)                            /* 0x0607E362 */
{
    uint32_t o = O;
    W16(o + MODE, 0x12);
    W16(o + ACT, 8);
    W32(o + 0x30, 0);
    Boss1A_Face();
    W16(o + SUB, 0);
}

static void Boss1A_Body(void)                               /* 0x0607DEE8: anim 0x1C frame 1, drifting */
{
    uint32_t o = O;
    Anim_Start(SCRATCH_ANIM, B1A_DEFS, B1A_SCR, cbase(), 0x1C);
    W16(SCRATCH_ANIM + 2, 1);
    if (!FROZEN) {
        add32(BODYX, (R16(o + 2) & 0x8000) ? (int32_t)RS16(o + CNT) << 7 : RS16(o + CNT) * -0x80);
        add32(BODYY, RS16(o + CNT) * 0x50);
    }
    W16(SPR + 2, (uint32_t)((RS32(BODYX) - RS32(CAM_X)) >> 16));
    W16(SPR + 4, (uint32_t)((RS32(BODYY) - RS32(CAM_Y)) >> 16));
    W16(SPR + 0x10, (R16(o + 2) & 0x8000) | 0x200);
    W16(SPR + 0x0C, 0);
    W16(SPR + 0x0A, bank());
    W32(SPR + 0x18, 0);
    W16(SPR + 0x0E, (RS16(o + C4C) >> 6) & 3);
    Anim_Tick(SCRATCH_ANIM);
}

static void Boss1A_Overlay(void)                            /* 0x0607E3E0 (state from 0x0607E3B0) */
{
    uint32_t o = O;
    W16(SPR + 2, (uint32_t)((RS32(o + 8) - RS32(CAM_X)) >> 16));
    W16(SPR + 4, (uint32_t)((RS32(o + 0xC) - RS32(CAM_Y)) >> 16));
    W16(SPR + 0x10, (R16(o + 2) & 0x8000) | 0x200);
    W16(SPR + 0x0C, 0);
    W16(SPR + 0x0E, (RS16(o + C4C) >> 6) & 3);
    W16(SPR + 0x0A, (uint16_t)(bank() + 1));
    W32(SPR + 0x18, 1);
    Anim_Tick(OVL);
}

static void hop_len(uint32_t o)                             /* +0x4A = |dx| / 64 px + 1, <= 3 */
{
    int16_t h = (int16_t)(((uint32_t)EM_Abs(RS32(PL + 8) - RS32(o + 8)) >> 1) >> 16);
    W16(o + HOP, (uint16_t)(int16_t)(EM_Sdiv(h, 32) + 1));                 /* A.BIN 0x060092BC */
    if (RS16(o + HOP) > 3) W16(o + HOP, 3);
}

static int32_t dirv(uint32_t o, int32_t v) { return (R16(o + 2) & 0x8000) ? -v : v; }

static void run(uint32_t o, int32_t v)                      /* modes 8 - 0xB, action 0xD */
{
    if (Boss1A_Face()) { W16(o + MODE, 0); W16(o + ACT, 0); W32(o + 0x30, 0); return; }
    W32(o + 0x30, (uint32_t)dirv(o, v));
}

void EnemyHandler_001A(void)                                /* 0x0607AFC0 */
{
    uint32_t o = O;
    if (R16(o + 2) & 1) Boss1A_Init();
    Hud_DrawBossBar(RS16(0x060BA528u + 2u * (uint32_t)RS16(0x060252C4u)), RS16(o + 0x16));
    if (FROZEN) goto draw;
    if (M(o) == 0) Boss1A_Face();
    if (!(R16(PL + 2) & 8) && (int16_t)Enemy_AttackPlayer(1, 0, 0) == 3) sub_06007CDE(0x10C);
    if (!(R16(o + 2) & 8) && !(R16(o + 4) & 0x10)) {
        W16(KIND, 0xFFFF);
        if ((int16_t)Enemy_CheckPlayerHits(0xC000) == 2) W16(KIND, (uint16_t)(int16_t)Player_AttackKind());
    }
    if (M(o) != 0x11 && M(o) != 0x10 && !(R16(o + 4) & 0x10) && (R16(o + 2) & 8)) {   /* hurt */
        Pal_Copy((bank() & 0xFFF) << 4, 0x10, 0x060E0150u);
        W16(o + ACT, 0x12); W16(o + MODE, 0x11);
        W32(o + 0x30, 0); W16(o + SUB, 0);
        AND16(o + 2, 0xFEFF); AND16(o + 4, 0xFFFE);
        W32(o + 0x34, 0x30000);
        W16(o + INV, 0x20);
        OR16(o + 4, 0x10);
        if (RS16(o + 0x16) > 0) sub_06007CDE(0x236);
    }
    if (M(o) != 0x10 && M(o) != 0x11) {
        if (RS16(o + INV) == 0) { AND16(o + 4, 0xFFEF); AND16(o + 2, 0xFFF7); }
        else add16(o + INV, -1);
    }
    if (A(o) != 0) goto L626;
    if (RS16(o + TMR) == 0) {                                       /* idle: pick the next move */
        Boss1A_Decide();
        if (M(o) >= 1 && M(o) <= 0xD) W16(o + TMR, 0x10);
        goto L626;
    }
    add16(o + TMR, -1);
    if (RS16(o + TMR) != 0) goto L626;
    AND16(o + 4, 0xFFFE);
    switch (M(o)) {
    case 1: W16(o + CNT, 0); W16(o + SUB, 0); W16(o + MODE, 1); W16(o + ACT, 3); break;
    case 2: W16(o + C4C, 0); W16(o + CNT, 0); W16(o + SUB, 0); W16(o + MODE, 2); W16(o + ACT, 0xF); break;
    case 3: W16(o + CNT, 0); W16(o + SUB, 0); W16(o + MODE, 3); W16(o + ACT, 0xE); OR16(o + 4, 1); break;
    case 4: W16(o + CNT, 0); W16(o + SUB, 0); W16(o + MODE, 4); W16(o + ACT, 3); break;
    case 5: W16(o + CNT, 0); W16(o + SUB, (uint16_t)((r7() & 1) + 1)); W16(o + MODE, 5); W16(o + ACT, 0x10); break;
    case 6: W16(o + CNT, 0); W16(o + SUB, 0); W16(o + MODE, 0x12); W16(o + ACT, 8); break;
    case 7: hop_len(o); W16(o + SUB, 0); W16(o + MODE, 7); W16(o + ACT, 3); break;
    case 8: W16(o + CNT, 0); W16(o + SUB, 0); W16(o + MODE, 8); W16(o + ACT, 0xD); break;
    case 9: W16(o + CNT, 0); W16(o + SUB, (uint16_t)(r7() % 3u)); W16(o + MODE, 9); W16(o + ACT, 0xD); break;
    case 10: W16(o + CNT, 0); W16(o + SUB, (uint16_t)(r7() % 3u + 1)); W16(o + MODE, 10); W16(o + ACT, 0xD); break;
    case 11: W16(o + CNT, 0); W16(o + SUB, (uint16_t)(r7() % 5u + 2)); W16(o + MODE, 0xB); W16(o + ACT, 0xD); break;
    case 12: W16(o + CNT, 0); W16(o + SUB, 0); W16(o + MODE, 0xC); W16(o + ACT, 0x17); break;
    default: break;
    }
L626:
    if (M(o) == 0x11 && A(o) == 0x12) W32(o + 0x30, (R16(o + 2) & 0x8000) ? 0x10000u : 0xFFFF0000u);
    if (M(o) == 0 && A(o) == 0) {                                   /* Sho close and attacking: hop away */
        if (EM_Abs(RS32(o + 8) - RS32(PL + 8)) > 0x9FFFFF) goto L782;
        if (EM_Abs(RS32(o + 0xC) - RS32(PL + 0xC)) > 0x1FFFFF || (int16_t)Player_AttackKind() == 6) goto L782;
        W16(o + CNT, 0); W16(o + SUB, 0);
        hop_len(o);
        W16(o + MODE, 7); W16(o + ACT, 3); W16(o + TMR, 0);
        goto phys;
    }
L782:
    if (M(o) != 7) goto L9a8;
    if (A(o) == 3 && (R16(o + AN) & 3) == 1) { AND16(o + AN, 0xFFFE); W16(o + ACT, 2); goto phys; }
    if (A(o) == 2 && (R16(o + AN) & 3) == 1) {
        sub_06007CDE(0x102); W32(o + 0x34, 0xB0000); W16(o + ACT, 4); AND16(o + AN, 0xFFFE); goto phys;
    }
    if (A(o) == 4) {
        int32_t v = dirv(o, RS16(o + HOP) * 0x10000);
        if (RS32(o + 0x34) < 1) {
            OR16(o + 2, 0x100); W32(o + 0x34, 0); W16(o + ACT, 5); AND16(o + AN, 0xFFFE);
            goto phys;
        }
        W32(o + 0x30, (uint32_t)v);
    }
    if (A(o) == 5) {
        int32_t v = dirv(o, RS16(o + HOP) * 0x10000);
        if ((R16(o + AN) & 3) == 1) { AND16(o + 2, 0xFEFF); AND16(o + AN, 0xFFFE); }
        if ((R16(o + AN) & 3) == 2 && (R16(o + 2) & 0x10)) {
            AND16(o + AN, 0xFFFD); W16(o + MODE, 0); W16(o + ACT, 0); W32(o + 0x30, 0);
            Boss1A_Face(); sub_06007CDE(0x212);
            goto phys;
        }
        W32(o + 0x30, (uint32_t)v);
    }
L9a8:
    if (M(o) != 4) goto Lbd24;
    if (A(o) == 3 && (R16(o + AN) & 3) == 1) { AND16(o + AN, 0xFFFE); W16(o + ACT, 2); goto phys; }
    if (A(o) == 2 && (R16(o + AN) & 3) == 1) {
        sub_06007CDE(0x102); W32(o + 0x34, 0xB0000); W16(o + ACT, 4); AND16(o + AN, 0xFFFE); goto phys;
    }
    if (A(o) == 4) {
        if (RS32(o + 0x34) < 1) {
            OR16(o + 2, 0x100); W32(o + 0x34, 0); W16(o + ACT, 5); AND16(o + AN, 0xFFFE); W32(o + 0x30, 0);
            goto phys;
        }
        add32(o + 0x30, dirv(o, 0xC00));
    }
    if (A(o) == 5) {
        if ((R16(o + AN) & 3) == 1) { AND16(o + 2, 0xFEFF); AND16(o + AN, 0xFFFE); goto phys; }
        if ((R16(o + AN) & 3) != 2 || !(R16(o + 2) & 0x10)) { add32(o + 0x30, dirv(o, 0xC00)); goto Lbb88; }
        AND16(o + AN, 0xFFFD); W16(o + ACT, 6); W32(o + 0x30, 0); sub_06007CDE(0x212);
        goto phys;
    }
Lbb88:
    if (A(o) == 6) {
        if ((R16(o + AN) & 3) == 1) { sub_06007CDE(0x102); W32(o + 0x34, 0xB0000); AND16(o + AN, 0xFFFE); goto phys; }
        if ((R16(o + AN) & 3) != 2 || RS32(o + 0x34) > 0) { add32(o + 0x30, dirv(o, 0xC00)); goto Lbc60; }
        OR16(o + 2, 0x100); W32(o + 0x34, 0); W32(o + 0x30, 0); W16(o + ACT, 7); AND16(o + AN, 0xFFFD);
        goto phys;
    }
Lbc60:
    if (A(o) == 7) {
        if ((R16(o + AN) & 3) == 1) { AND16(o + 2, 0xFEFF); AND16(o + AN, 0xFFFE); goto phys; }
        if ((R16(o + AN) & 3) != 2 || !(R16(o + 2) & 0x10)) { add32(o + 0x30, dirv(o, 0xC00)); goto Lbd24; }
        AND16(o + AN, 0xFFFD); W16(o + ACT, 0x1A); W32(o + 0x30, 0); sub_06007CDE(0x212);
        goto phys;
    }
Lbd24:
    if (M(o) == 1) {                                                /* walk at Sho */
        Boss1A_Face();
        if (A(o) == 3 && (R16(o + AN) & 3) == 1) {
            W32(o + 0x30, (R16(o + 2) & 0x8000) ? 0xFFFE8000u : 0x18000u);
            if (EM_Abs(RS32(o + 8) - RS32(PL + 8)) < 0x180000) {
                W16(o + MODE, 0x12); W16(o + ACT, 8); W32(o + 0x30, 0);
                goto phys;
            }
        }
        if (RS16(o + CNT) > 0x140) {
            W16(o + MODE, 0x12); W32(o + 0x30, 0); W16(o + ACT, 8); W16(o + CNT, 0);
            goto phys;
        }
        add16(o + CNT, 1);
    }
    if (M(o) == 0x12) {                                             /* leap to the ceiling */
        Boss1A_Face();
        if (A(o) == 8) {
            if ((R16(o + AN) & 3) == 1) { sub_06007CDE(0x102); W32(o + 0x34, 0xD0000); AND16(o + AN, 0xFFFE); goto phys; }
            uint16_t u = Map_GetAttr(RS32(o + 8), RS32(o + 0xC) - 0x5F0000, LAYER);
            if (u == 0xFFFF || (u & 0x10) != 0x10) goto Lbef2;
            W16(o + ACT, 9); W32(o + 0x34, 0); OR16(o + 2, 0x100);
            goto phys;
        }
Lbef2:
        if (A(o) != 9 || (R16(o + AN) & 3) != 1) goto Lbf70;
        Boss1A_Face();
        W16(o + MODE, 0xD); W16(o + ACT, 0xC); W16(o + CNT, 0);
        AND16(o + 2, 0xFEFF);
        sub_06007CDE(0x220);
        goto phys;
    }
Lbf70:
    if (M(o) == 0xD && A(o) == 0xC) {                               /* drop / slide */
        add32(o + 0x30, dirv(o, 0x2800));
        if ((R16(o + AN) & 3) == 1 && (R16(o + 2) & 0x10)) W16(o + ACT, 0x18);
    }
    if (M(o) == 2) {                                                /* spore shower */
        if ((R16(o + AN) & 3) == 1) {
            int32_t dx = dirv(o, 0x210000);
            if (RS16(o + CNT) % 8 == 0) {                               /* A.BIN 0x0600A604 */
                uint16_t c = (uint16_t)(r7() & 7);
                spawn2C(RS32(o + 8) + dx, RS32(o + 0xC) - 0x2E0000, 0, c, 1);
            }
            if (RS16(o + CNT) > 0x7F) AND16(o + AN, 0xFFFE);
            add16(o + CNT, 1);
        }
        Pal_SetFaded((bank() & 0xFFF) << 4, 0x10, 0x060E0150u, 0, (R16(o + C4C) & 7) << 3, 0);
        add16(o + C4C, 1);
    }
    if (M(o) == 3 && A(o) == 0xE) {                                 /* rolling leap */
        add32(o + 0x30, dirv(o, 0x1800));
        if ((R16(o + AN) & 3) == 1) {
            sub_06007CDE(0x102); sub_06007CDE(0x127);
            W32(o + 0x34, 0x50000); AND16(o + AN, 0xFFFE);
        }
        if ((R16(o + AN) & 3) == 2 && (R16(o + 2) & 0x10)) {
            W16(o + ACT, 0x19); W32(o + 0x30, 0); sub_06007CDE(0x212);
            goto phys;
        }
    }
    if (M(o) == 0xF) {                                              /* intro: jumps in */
        if ((R16(o + AN) & 3) == 1) { add32(o + 0x30, 0x2800); add32(o + 0x34, 0x1A00); OR16(o + 2, 0x8000); }
        if ((R16(o + AN) & 3) == 2) { AND16(o + 2, 0xFEFF); AND16(o + AN, 0xFFFD); W32(o + 0x30, 0); }
        if (R16(o + 2) & 0x10) { W16(o + ACT, 0); W16(o + MODE, 0); AND16(PL + 4, 0xFBFF); sub_06007CDE(0x212); }
        add16(o + CNT, 1);
    }
    if (M(o) == 0xC && A(o) == 0x17) W32(o + 0x30, (R16(o + 2) & 0x8000) ? 0x18000u : 0xFFFE8000u);
    if (M(o) == 8 && A(o) == 0xD) { run(o, 0x14000); if (M(o) == 0) goto phys; }
    if (M(o) == 9 && A(o) == 0xD) { run(o, 0x14000); if (M(o) == 0) goto phys; }
    if (M(o) == 10 && A(o) == 0xD) { run(o, 0x16000); if (M(o) == 0) goto phys; }
    if (M(o) == 0xB && A(o) == 0xD) { run(o, 0x18000); if (M(o) == 0) goto phys; }
    if (M(o) == 0x13 && A(o) == 3 && (R16(o + AN) & 3) == 1) {      /* roll until a wall */
        int fwd = (R16(o + 4) & 4) != 0;
        W32(o + 0x30, fwd ? 0x1C000u : 0xFFFE4000u);
        if ((int16_t)Map_GetAttr(RS32(o + 8) + (fwd ? 0x200000 : -0x200000), RS32(o + 0xC), LAYER) == -1) {
            if (RS16(o + CNT) < 0x79) { add16(o + CNT, 1); goto Lc5bc; }
        }
        Boss1A_TurnRun();
        goto phys;
    }
Lc5bc:
    if (M(o) == 0x10) {                                             /* death */
        if (A(o) == 0x11) {
            if ((R16(o + AN) & 3) == 1) { AND16(o + AN, 0xFFFE); OR16(o + 4, 2); W16(o + C4C, 0); }
            if (R16(o + 4) & 2) {
                if (RS16(o + C4C) < 0x100) {
                    if (RS16(o + C4C) < 0x120 && (R16(o + C4C) & 7) == 0) { Boss1A_Debris(); sub_06007CDE(0x115); }
                    if (RS16(o + C4C) < 0x120 && (R16(o + C4C) & 0xF) == 0) Boss1A_Pieces();
                }
                add16(o + C4C, 1);
            }
        }
        if (A(o) == 0x1C) {
            if (RS16(o + CNT) > 0x20) {
                if (!(R16(o + 4) & 2)) { OR16(o + 4, 2); W16(o + C4C, 0); }
                else {
                    if (RS16(o + C4C) < 0x181) {
                        if (RS16(o + C4C) < 0x120 && (R16(o + C4C) & 7) == 0) { Boss1A_Debris(); sub_06007CDE(0x115); }
                        if (RS16(o + C4C) < 0x120 && (R16(o + C4C) & 7) == 0) Boss1A_Pieces();
                        add16(o + C4C, 1);
                        goto Lc9d2;
                    }
                    Boss1A_Clear();
                }
                goto phys;
            }
            add16(o + CNT, 1);
        }
    }
Lc9d2:
    if (R16(o + AN) & 0x8000) {                                     /* animation over */
        W32(o + 0x30, 0);
        W16(o + TMR, 0);
        AND16(o + AN, 0x7FFF);
        if (M(o) == 2) Pal_Copy((bank() & 0xFFF) << 4, 0x10, 0x060E0150u);
        if (M(o) == 0x10 && A(o) == 0x13) {
            W16(o + CNT, 0);
            W32(BODYX, R32(o + 8));
            W32(BODYY, (uint32_t)(RS32(o + 0xC) - 0x300000));
            W16(o + ACT, 0x1C);
            Anim_Start(OVL, B1A_DEFS, B1A_SCR, cbase(), 0x1D);        /* 0x0607E3B0 */
        } else if (A(o) == 0x12) {
            if (!(R16(o + 2) & 0x4000)) {
                if (RS16(o + 0x16) < 1) {
                    int16_t k = RS16(KIND);
                    W16(o + MODE, 0x10); W16(o + CNT, 0); W16(o + C4C, 0);
                    sub_06007CDE(0x236);
                    W16(o + ACT, (k == 0 || k == 1 || k == 2 || k == 3) ? 0x13 : 0x11);
                    OR16(PL + 4, 0x400);
                } else if ((r7() & 1) == 0) {
                    W16(o + ACT, 3); W16(o + MODE, 0x13); W16(o + CNT, 0); W16(o + C4C, 0);
                    if ((r7() & 1) == 0) AND16(o + 4, 0xFFFB); else OR16(o + 4, 4);
                    if (RS32(o + 8) < RS32(0x060EA618u) + 0x600000) { AND16(o + 2, 0x7FFF); OR16(o + 4, 4); }
                    if (RS32(0x060EA620u) - 0x600000 < RS32(o + 8)) { OR16(o + 2, 0x8000); AND16(o + 4, 0xFFFB); }
                } else { W16(o + MODE, 3); W16(o + ACT, 0xE); OR16(o + 4, 1); }
            } else W16(o + 0x22, 0);
        } else {
            if (M(o) == 0x13 && A(o) == 0x1B && EM_Abs(RS32(o + 8) - RS32(PL + 8)) < 0x500000) {
                W16(o + SUB, 0); W16(o + ACT, 0x10); W16(o + MODE, 5);
                goto phys;
            }
            if (M(o) == 0x10 && A(o) == 0x11) { Boss1A_Clear(); return; }
            if (M(o) == 0xF) { W16(o + ACT, 0); W16(o + MODE, 0); AND16(PL + 4, 0xFBFF); }
            else if (RS16(o + SUB) == 0) { W16(o + ACT, 0); W16(o + MODE, 0); }
            else { W16(o + 0x22, 0); add16(o + SUB, -1); }
        }
    }
phys:
    Obj_Integrate(o);
    Obj_CollideMap(o, 3, 6);
    Enemy_AnimSwitch();
draw:
    EM_Trail(R16(o + 4) & 1, o);
    if (M(o) != 0x10 && (R16(o + 2) & 8)) {                         /* blinking while invulnerable */
        if (R16(0x060143EAu) & 1) OR16(o + AN, 0x4000); else AND16(o + AN, 0xBFFF);
    }
    if ((M(o) == 1 || M(o) == 0x13) && A(o) == 3 && (R16(o + AN) & 3) == 1) OR16(o + AN, 0x4000);
    if (M(o) == 0x10 && A(o) == 0x1C && RS16(o + C4C) < 0xFF) { Boss1A_Body(); Boss1A_Overlay(); }
    W16(SPR + 2, (uint32_t)((RS32(o + 8) - RS32(CAM_X)) >> 16));
    W16(SPR + 4, (uint32_t)((RS32(o + 0xC) - RS32(CAM_Y)) >> 16));
    W16(SPR + 0x10, R16(o + 2) & 0x8000);
    W16(SPR + 0x0A, bank());
    W16(SPR + 0x0C, 0);
    W16(SPR + 0x0E, 0);
    W32(SPR + 0x18, 0);
    if (M(o) == 0x10) {
        W16(SPR + 0x0E, (RS16(o + C4C) >> 6) & 3);
        if (RS16(o + C4C) > 0xFF) OR16(o + AN, 0x4000);
    }
    if (R16(o) != 0xFFFF) { Anim_Tick(o + AN); AND16(o + AN, 0xBFFF); }
}

/* Projectile 0x2A (0x0607DBAE): a piece of the dying boss (anim 0x14, frame +0x14), thrown
 * up at 13-14 px/frame (by +0x10 & 7, minus (rand & 15) / 16 px) and sideways at a speed
 * growing with its age (+6), falls (0x5000), spins; gone below the stage bottom. */
void Proj_Piece2A(uint32_t e)
{
    static const int32_t vy0[8] = { (int32_t)0xFFF30000, (int32_t)0xFFF2E000, (int32_t)0xFFF2C000, (int32_t)0xFFF32000,
                                    (int32_t)0xFFF36000, (int32_t)0xFFF2B000, (int32_t)0xFFF2D000, (int32_t)0xFFF36000 };
    static const int32_t kx[8] = { -0x100, 0x180, -0x120, 0x130, -0x140, 0x160, -0x1A0, 0x1C0 };
    if (R16(e + 4) & 1) {
        Anim_Start(e + 0x20, B1A_DEFS, B1A_SCR, cbase(), 0x14);
        AND16(e + 4, 0xFFFE);
        W16(e + 0x22, R16(e + 0x14));
        W32(e + 0x1C, (uint32_t)vy0[R16(e + 0x10) & 7]);
        add32(e + 0x1C, (int32_t)(r7() & 0xF) * -0x1000);
    }
    if (!FROZEN) {
        W32(e + 0x18, (uint32_t)((int32_t)(uint32_t)R16(e + 6) * kx[R16(e + 0x10) & 7]));
        add32(e + 0x1C, 0x5000);
        add32(e + 8, RS32(e + 0x18));
        add32(e + 0xC, RS32(e + 0x1C));
        W16(e + 6, R16(e + 6) + 1);
        if (RS32(0x060EA624u) < RS32(e + 0xC)) W16(e, 0xFFFF);
    }
    W16(e + 0x12, 0x7FFF);
    AND16(e + 4, 0xFFF7);
    W16(SPR + 2, (uint32_t)((RS32(e + 8) - RS32(CAM_X)) >> 16));
    W16(SPR + 4, (uint32_t)((RS32(e + 0xC) - RS32(CAM_Y)) >> 16));
    W16(SPR + 0x10, (R16(e + 4) & 0x8000) | 0x600);
    W16(SPR + 0x0C, 0);
    W16(SPR + 0x0E, 0);
    W16(SPR + 0x0A, bank());
    W32(SPR + 0x18, 1);
    W16(SPR + 0x14, (R16(e + 6) & 0x3F) << 2);
    if (R16(e) != 0xFFFF && (R16(e + 6) & 1)) Anim_Tick(e + 0x20);
}

/* Projectile 0x2B (0x0607D638): a blast puff (anim 0x16) rising from 16 px lower,
 * accelerating (0x4000/frame), flickering every other frame (bank +2). Harmless. The
 * spawner's delay (+0x14) is cleared on the first frame (kept). */
void Proj_Puff2B(uint32_t e)
{
    if (R16(e + 4) & 1) {
        Anim_Start(e + 0x20, B1A_DEFS, B1A_SCR, cbase(), 0x16);
        AND16(e + 4, 0xFFFE);
        W16(e + 0x22, 0);
        add32(e + 0xC, 0x100000);
        W16(e + 6, 0);
        W16(e + 0x14, 0);
    }
    if (!FROZEN) {
        if (RS16(e + 0x14) != 0) { add16(e + 0x14, -1); return; }
        add32(e + 0x1C, 0x4000);
        add32(e + 0xC, -RS32(e + 0x1C));
        W16(e + 6, R16(e + 6) + 1);
    } else if (RS16(e + 0x14) != 0) return;
    W16(e + 0x12, 0x7FFF);
    AND16(e + 4, 0xFFF7);
    W16(SPR + 2, (uint32_t)((RS32(e + 8) - RS32(CAM_X)) >> 16));
    W16(SPR + 4, (uint32_t)((RS32(e + 0xC) - RS32(CAM_Y)) >> 16));
    W16(SPR + 0x10, 0x200);
    W16(SPR + 0x0C, 0);
    W16(SPR + 0x0E, 0);
    W16(SPR + 0x0A, (uint16_t)(bank() + 2));
    if (!(R16(e + 6) & 1)) { W32(SPR + 0x18, 0xFFFFFF76u); W16(SPR + 0x0E, 3); }
    else W32(SPR + 0x18, 1);
    if (R16(e) != 0xFFFF) Anim_Tick(e + 0x20);
    if (R16(e + 0x20) & 0x8000) W16(e, 0xFFFF);
}

/* Projectile 0x2C (0x0607D800): a spore (anim 0x15, bank +1, scaled sprite). +0x10 bit 0
 * clear: rises 2 px/frame until y < 0x573 px. Set: after +0x14 frames flies on an arc
 * (angle (age + 0xDC) & 0xFF until age 0x5E, then 0x38) with the speeds 0x060E06EC (x) /
 * 0x060E070C (y) [+2 & 7], mirrored by +4 & 0x8000, stopped by walls (layer 0x060EA5B8),
 * grows from 0x2800 to full size; gone above the stage top. Hurts Sho for 1. */
void Proj_Spore2C(uint32_t e)
{
    if (R16(e + 4) & 1) {
        Anim_Start(e + 0x20, B1A_DEFS, B1A_SCR, cbase(), 0x15);
        AND16(e + 4, 0xFFFE);
    }
    if (!FROZEN) {
        if (!(R16(PL + 2) & 8)) Projectile_Collide(1, 0xC100);
        if (!(R16(e + 0x10) & 1)) {
            add32(e + 0xC, -0x20000);
            W16(e + 6, R16(e + 6) + 1);
            if (0 < 0x5730000 - RS32(e + 0xC)) { W16(e, 0xFFFF); return; }
        } else {
            if (RS16(e + 0x14) != 0) { add16(e + 0x14, -1); return; }
            int32_t sx = RS32(0x060E06ECu + 4u * (R16(e + 2) & 7u)), sy = RS32(0x060E070Cu + 4u * (R16(e + 2) & 7u));
            int ang = ((uint32_t)R16(e + 6) + 0x20 < 0x7E) ? (int)((R16(e + 6) + 0xDCu) & 0xFF) : 0x38;
            W32(e + 0x18, (uint32_t)FixMul(Cos8(ang), sx));
            W32(e + 0x1C, (uint32_t)FixMul(Sin8(ang), sy));
            if ((int16_t)Map_GetAttr(RS32(e + 8), RS32(e + 0xC), LAYER) == -1) {
                if (!(R16(e + 4) & 0x8000)) add32(e + 8, RS32(e + 0x18));
                else add32(e + 8, -RS32(e + 0x18));
            }
            add32(e + 0xC, -RS32(e + 0x1C));
            W16(e + 6, R16(e + 6) + 1);
            if (0 < RS32(0x060EA61Cu) - RS32(e + 0xC)) { W16(e, 0xFFFF); return; }
        }
    }
    W16(e + 0x12, 0x7FFF);
    AND16(e + 4, 0xFFF7);
    W16(SPR + 2, (uint32_t)((RS32(e + 8) - RS32(CAM_X)) >> 16));
    W16(SPR + 4, (uint32_t)((RS32(e + 0xC) - RS32(CAM_Y)) >> 16));
    W16(SPR + 0x10, 0x1A00);
    W16(SPR + 0x0C, 0);
    W16(SPR + 0x0A, (uint16_t)(bank() + 1));
    W16(SPR + 0x0E, 0);
    if (!(R16(0x060143EAu) & 1)) { W32(SPR + 0x18, 0xFFFFFF76u); W16(SPR + 0x0E, 3); }
    else W32(SPR + 0x18, 1);
    if (!(R16(e + 0x10) & 1)) { W32(SPR + 0x1C, 0x10000); W32(SPR + 0x20, 0x10000); }
    else {
        uint32_t s = (uint32_t)R16(e + 6) * 0x400u + 0x2800u;
        if (s > 0x10000) s = 0x10000;
        W32(SPR + 0x1C, s); W32(SPR + 0x20, s);
    }
    if (R16(e) != 0xFFFF) Anim_Tick(e + 0x20);
}
