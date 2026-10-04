/* Ninjutsu (ninjutsu.c) and the ported player sub-routines (player_stubs.c) on real data.
 *  - stage 1-1 (index 0): Sho casts the scroll's ninjutsu (X): ZIRYU effect (state 2) runs its
 *    8 steps and ends, the on-screen enemies lose 8 HP (killed), the off-screen ones are
 *    untouched, the HUD counter 0x060252BE drops to 0, Sho gets 0x60 frames of invulnerability
 *  - kinds 0 (burst + shield), 3 (heal), 4 (Bishamon, sword flashes), 6 (Nyorai effect start,
 *    boss 0x1F killed by Magic_Apply), the shield absorbing a hit, afterimages
 *  - __modsi3 / __umodsi3 call sites, idle fidget, command buffer, 8-shuriken fan, debug move,
 *    on-hit, posture-4 selector, sword clink on walls.
 *   gcc -std=c11 -Isrc -Itests tests/test_ninjutsu.c $(cat tests/SOURCES.txt) -lm
 *   ./a.out ../extracted/ */
#include <stdio.h>
#include <string.h>
#include "pad.h"
#include "input.h"
#include "player.h"
#include "player_ext.h"
#include "ram.h"
#include "render.h"
#include "stage.h"
#include "stage_hooks.h"
#include "camera.h"
#include "sprite.h"
#include "objects.h"
#include "enemy.h"
#include "fx.h"
#include "fade.h"
#include "resource.h"
#include "shinobi.h"
#include "collision.h"
#include "ninjutsu.h"
extern int g_spriteCount;
extern RenderSprite g_spriteLog[]; extern int g_spriteLogCount;
static InputPort fake[2];
const InputPort *Input_GetPort(int p) { return &fake[p]; }
static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("   FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

#define MAGIC      0x060252BAu
#define MAGIC_PTS  0x060252BCu
#define MAGIC_AV   0x060252BEu
#define SYSFLAGS   0x0602529Eu
#define NJ_STATE   0x060F4BF0u
#define NJ_STEP    0x060F4BF6u

static void tick(uint16_t held)
{
    fake[0].connected = 1; fake[0].raw = (uint16_t)~held;
    W16(0x060143EAu, R16(0x060143EAu) + 1u); W16(0x060F4CCCu, R16(0x060F4CCCu) + 1u);
    Pad_Update(2); Scroll_Update(); Game_LatchPad1(P16(P_FLAGS4)); Player_Tick();
    PlayerFx_Update();
    Stage_UpdateState();
    Obj_UpdateEnemies(); Obj_UpdateAll(); Projectiles_Update(); Effects_Update(); Items_Update();
    Stage_FrameHooks();
    Ninjutsu_Update();
    Fade_Update();
}
static uint32_t slot(int s) { return OBJ_TABLE + (uint32_t)s * OBJ_SIZE; }
static int is_enemy(uint32_t o) { uint16_t t = R16(o); return t != 0xFFFF && t < 0x1000; }
/* the on-screen box of Enemies_MagicDamage (0x06049768) */
static int on_screen(uint32_t o)
{
    int32_t dx = RS32(o + 8) - RS32(CAM_X) - 0xA00000, dy = RS32(o + 0xC) - RS32(CAM_Y) - 0x800000;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    return dx <= 0xC00000 && dy <= 0xA00000;
}
static void place_sho(int32_t x, int32_t y)
{
    PW32(P_X, (uint32_t)x); PW32(P_Y, (uint32_t)y); PW32(0x2C, (uint32_t)y); PW32(P_VELY, 0);
    W32(CAM_X, (uint32_t)(P32(P_X) - RS32(0x060EA550u)));
    W32(CAM_Y, (uint32_t)(P32(P_Y) - RS32(0x060EA554u)));
    Scroll_Update();
}
static const char *g_dir;
static void load(int idx)
{
    char p[512];
    snprintf(p, sizeof p, "%sA.BIN", g_dir);           Ram_LoadImage(p, 0x06004000u);
    snprintf(p, sizeof p, "%sSHINOBI.dec.bin", g_dir); Ram_LoadImage(p, 0x06040000u);
    snprintf(p, sizeof p, "%sSHOU.CHR", g_dir);        Ram_LoadImage(p, 0x00200000u);
    W16(0x060EC658u, 0); W16(0x060EC65Au, 0); W16(0x060252DCu, 1); W16(SYSFLAGS, 0);
    Shinobi_InitVdp2();
    W32(SPR_ZOOM, 0x10000);
    Fade_Init();
    Player_Init();
    if (!Stage_Load(g_dir, idx)) { CHECK(0, "stage %d not loaded", idx); return; }
    W16(0x060252DCu, 1);
    Stage_SpawnPlayer();
    Ninjutsu_PlayerStageInit();
    Ninjutsu_Init();
    StageHooks_Init();
    W16(MAGIC, 0); W16(MAGIC_PTS, 0); W16(MAGIC_AV, 0);
    for (int i = 0; i < 30; i++) tick(0);
}

/* cast with X and run until the effect is over and Sho is back; returns frames (-1 = stuck) */
static int is_fx_src(uint32_t src)
{
    for (int c = 0x480; c < 0x4A0; c++) if (R16(CHAR_TABLE + (uint32_t)c * 10) && src == VDP1_VRAM_BASE + R16(CHAR_TABLE + (uint32_t)c * 10) * 8u) return 1;
    return 0;
}
static int cast(int *maxStep, int *effectSprites, int *flashFrames, int *stateSeen)
{
    PW16(P_ACTION, ACT_IDLE); PW16(P_FLAGS2, P16(P_FLAGS2) & 0xF7F7); PW16(P_FLAGS4, P16(P_FLAGS4) & 0xBFFF);
    PW16(P_HP, 16); W16(R32(0x060EA41Cu), 0x60);
    tick(PAD_X);
    int a = Player_Action();
    CHECK(a == ACT_NINJUTSU || a == ACT_CROUCH_NINJUTSU || a == ACT_AIR_NINJUTSU, "X did not start the ninjutsu (action %02X)", a);
    CHECK(R16(SYSFLAGS) & 0x8000, "world not frozen");
    CHECK(R16(MAGIC) & 0x2000, "MAGIC 0x2000 not set (%04X)", R16(MAGIC));
    int started = 0;
    for (int f = 0; f < 1500; f++) {
        int n0 = g_spriteLogCount;
        tick(0);
        if (R16(NJ_STATE)) { started = 1; *stateSeen = R16(NJ_STATE); if (RS16(NJ_STEP) > *maxStep) *maxStep = RS16(NJ_STEP); }
        for (int i = n0; i < g_spriteLogCount; i++) if (is_fx_src(g_spriteLog[i].src)) (*effectSprites)++;
        g_spriteLogCount = 0;
        int r, g, b, m;
        Fade_GetColorOffset(&r, &g, &b, &m);
        if (r > 100) (*flashFrames)++;
        if (started && R16(NJ_STATE) == 0 && !(R16(SYSFLAGS) & 0x8000) && Player_Action() != ACT_NINJUTSU &&
            Player_Action() != ACT_CROUCH_NINJUTSU && Player_Action() != ACT_AIR_NINJUTSU) return f;
    }
    return -1;
}

static void test_ziryu(void)
{
    printf("stage 1-1: ZIRYU (kind 2)\n");
    load(0);
    /* put Sho on the ground near the first enemies, in front of them */
    uint32_t e0 = 0;
    for (int s = 0; s < OBJ_SLOTS; s++) if (is_enemy(slot(s)) && (!e0 || RS32(slot(s) + 8) < RS32(e0 + 8))) e0 = slot(s);
    CHECK(e0, "no enemy on stage 1-1");
    if (!e0) return;
    int best = -1; int32_t bx = 0;
    for (int s = 0; s < OBJ_SLOTS; s++) {                   /* the spot with most enemies in view */
        if (!is_enemy(slot(s))) continue;
        int32_t x = RS32(slot(s) + 8) - 0x500000, cx = x - RS32(0x060EA550u) + 0xA00000;
        int n = 0;
        for (int t = 0; t < OBJ_SLOTS; t++)
            if (is_enemy(slot(t)) && RS32(slot(t) + 8) - cx <= 0xC00000 && cx - RS32(slot(t) + 8) <= 0xC00000 &&
                RS32(slot(t) + 0xC) - RS32(slot(s) + 0xC) < 0x600000 && RS32(slot(s) + 0xC) - RS32(slot(t) + 0xC) < 0x600000) n++;
        if (n > best) { best = n; bx = x; e0 = slot(s); }
    }
    place_sho(bx, RS32(e0 + 0xC) - 0x400000);
    for (int i = 0; i < 40; i++) { tick(0); PW16(P_HP, 16); }
    tick(0);
    int16_t hp[OBJ_SLOTS]; uint16_t ty[OBJ_SLOTS]; int on[OBJ_SLOTS]; int non = 0, noff = 0;
    for (int s = 0; s < OBJ_SLOTS; s++) {
        uint32_t o = slot(s);
        ty[s] = R16(o); hp[s] = RS16(o + 0x16);
        on[s] = is_enemy(o) && on_screen(o) && !(R16(o + 0x12) & 0x4000) && !(R16(o + 2) & 8);
        if (on[s]) non++; else if (is_enemy(o)) noff++;
    }
    printf("   Sho %d,%d action %02X; enemies on screen %d, off screen %d\n", P32(P_X) >> 16, P32(P_Y) >> 16, Player_Action(), non, noff);
    CHECK(non > 0, "no enemy on screen");
    W16(MAGIC_AV, 1);                                       /* the scroll was picked up */
    int score0 = RS32(SCORE);
    int maxStep = -1, spr = 0, flash = 0, st = 0;
    int frames = cast(&maxStep, &spr, &flash, &st);
    printf("   effect: state %d, last step %d, %d frames, %d effect sprites, %d flash frames\n", st, maxStep, frames, spr, flash);
    CHECK(frames > 200 && frames < 400, "ninjutsu did not end in time (%d)", frames);
    CHECK(st == 2, "effect state %d, expected 2 (Ziryu)", st);
    CHECK(maxStep == 6, "effect stopped at step %d", maxStep);   /* step 7 ends at once */
    CHECK(spr > 100, "dragon sprites not drawn (%d)", spr);
    CHECK(flash > 10, "no white flashes (%d)", flash);
    CHECK(R16(MAGIC_AV) == 0, "HUD ninjutsu counter not used (%d)", R16(MAGIC_AV));
    CHECK((R16(MAGIC) & 0xE000) == 0, "MAGIC flags left %04X", R16(MAGIC));
    CHECK(!(R16(SYSFLAGS) & 0x8000), "world still frozen");
    CHECK(R16(R32(0x060EA41Cu)) > 0x40 && R16(R32(0x060EA41Cu)) <= 0x60, "invulnerability %d", R16(R32(0x060EA41Cu)));
    int hit = 0, killed = 0, offHit = 0;
    for (int s = 0; s < OBJ_SLOTS; s++) {
        uint32_t o = slot(s);
        if (on[s]) {
            if (R16(o) != ty[s] || RS16(o + 0x16) <= hp[s] - 8 || RS16(o + 0x16) < 1) hit++;
            if (R16(o) != ty[s] || RS16(o + 0x16) < 1) killed++;
        } else if (ty[s] != 0xFFFF && ty[s] < 0x1000 && R16(o) == ty[s] && RS16(o + 0x16) < hp[s] && !on_screen(o)) offHit++;
    }
    for (int i = 0; i < 120; i++) tick(0);                  /* death animations */
    int gone = 0;                                           /* freed, or reset (respawning subtype 2) */
    for (int s = 0; s < OBJ_SLOTS; s++) if (on[s] && (R16(slot(s)) != ty[s] || RS16(slot(s) + 6) == 0x13)) gone++;
    printf("   enemies hit %d/%d (HP <= 0: %d, dead 2 s later: %d), off-screen hit %d, score +%d\n", hit, non, killed, gone, offHit, RS32(SCORE) - score0);
    CHECK(hit == non, "not every on-screen enemy was damaged");
    CHECK(gone == killed && killed > 0 && RS32(SCORE) - score0 >= 100 * killed, "killed enemies did not die (%d/%d)", gone, killed);
    CHECK(offHit == 0, "off-screen enemies damaged");
    /* no scroll: X does nothing */
    PW16(P_ACTION, ACT_IDLE);
    tick(PAD_X);
    CHECK(Player_Action() != ACT_NINJUTSU && R16(NJ_STATE) == 0, "ninjutsu without the scroll");
}

static void test_kinds(void)
{
    printf("kind 0: afterimage burst, then the shield\n");
    load(0);
    W16(MAGIC, 0x2000);                                     /* forced cast, kind 0 */
    int maxStep = -1, spr = 0, flash = 0, st = 0;
    int f = cast(&maxStep, &spr, &flash, &st);
    printf("   %d frames, state %d, last step %d, MAGIC %04X pts %d\n", f, st, maxStep, R16(MAGIC), RS16(MAGIC_PTS));
    CHECK(f > 0 && st == 7 && maxStep == 3, "burst effect");
    CHECK((R16(MAGIC) & 0x40FF) == 0x4000 && RS16(MAGIC_PTS) == 6, "shield not armed");
    CHECK(R16(CHAR_TABLE + 0x480 * 10) == ((0x66000 + 0x4740) >> 3), "shield char 0x480 not registered");
    /* afterimages (0x06044280) every other frame while the shield is armed */
    int n = 0;
    for (int i = 0; i < 4; i++) { int c = g_spriteCount; tick(0); n += g_spriteCount - c; }
    CHECK(n >= 4 + 12, "afterimages not drawn (%d sprites in 4 frames)", n);
    /* the shield absorbs a hit: points down, HP kept */
    int16_t hp = PS16(P_HP);
    W16(R32(0x060EA41Cu), 0);
    Player_TakeDamage(2);
    printf("   hit 2: HP %d -> %d, shield points %d\n", hp, PS16(P_HP), RS16(MAGIC_PTS));
    CHECK(PS16(P_HP) == hp && RS16(MAGIC_PTS) == 4, "shield did not absorb");

    printf("kind 3: heal\n");
    PW16(P_HP, 3); W16(MAGIC, (R16(MAGIC) & 0xFFF0) | 3);
    Magic_Apply();
    CHECK(PS16(P_HP) == RS16(0x060252B0u), "not healed (%d)", PS16(P_HP));

    printf("kind 4: BISYAMON, sword flashes\n");
    load(0);
    W16(MAGIC, 0x2004);
    maxStep = -1; spr = 0; flash = 0; st = 0;
    f = cast(&maxStep, &spr, &flash, &st);
    printf("   %d frames, state %d, last step %d, %d effect sprites, MAGIC %04X pts %d\n", f, st, maxStep, spr, R16(MAGIC), RS16(MAGIC_PTS));
    CHECK(f > 0 && st == 6 && maxStep == 4 && spr > 50, "Bishamon effect");
    CHECK((R16(MAGIC) & 0x40F0) == 0x4040 && RS16(MAGIC_PTS) == 6, "kind 4 not armed");
    CHECK(R16(CHAR_TABLE + 0x480 * 10) == ((0x66000 + 0x4740) >> 3), "kind-4 chars not registered");
    /* slash: anim event 2 spawns a flash in 0x060EA32C */
    int flashes = 0;
    for (int i = 0; i < 40; i++) { tick((i % 20) < 3 ? PAD_B : 0); if (R16(0x060EA32Cu) != 0xFFFF || R16(0x060EA32Cu + 0x78) != 0xFFFF) flashes++; }
    printf("   sword flash alive %d frames\n", flashes);
    CHECK(flashes > 0, "no sword flash on slash");

    printf("kind 6: NYORAI start, boss 0x1F killed\n");
    load(0);
    uint32_t b = slot(OBJ_SLOTS - 1);
    W16(b, 0x1F); W16(b + 0x16, 40); W16(b + 2, 0);
    W16(MAGIC, 0x2006);
    W16(0x060F23CAu, 0); W16(0x060F22C4u, 0x3F); W16(0x060F23C8u, 0);   /* six orbs to absorb */
    for (uint32_t i = 0; i < 6; i++) { W32(0x060F2394u + 4 * i, (i * 0x10 + 0x38) << 16); W32(0x060F23ACu + 4 * i, 0x200000u); }
    PW16(P_ACTION, ACT_IDLE);
    tick(PAD_X);
    int s5 = 0;
    for (int i = 0; i < 200 && !s5; i++) { tick(0); if (R16(NJ_STATE) == 5) s5 = 1; }
    CHECK(s5, "Nyorai effect (state 5) not started");
    int steps = 0;
    for (int i = 0; i < 4000 && R16(NJ_STATE) == 5; i++) { tick(0); if (RS16(NJ_STEP) > steps) steps = RS16(NJ_STEP); }
    printf("   Nyorai reached step %d (state %d)\n", steps, R16(NJ_STATE));
    CHECK(steps >= 21 && R16(NJ_STATE) == 0, "Nyorai effect stuck at step %d", steps);
    CHECK(R16(0x060F22C4u) == 0 && R16(0x060F23C8u) == 0x3F, "orbs not absorbed");
    W16(b, 0x1F); W16(b + 0x16, 40); W16(b + 2, 0);
    W16(MAGIC, 6);
    Magic_Apply();
    CHECK(R16(b + 0x16) == 0x8001 && (R16(b + 2) & 8), "boss 0x1F not killed");
    W16(b, 0xFFFF);
}

static void test_player_subs(void)
{
    printf("player sub-routines\n");
    /* A.BIN runtime: __modsi3 / __umodsi3 */
    CHECK(sub_0600A604(-7, 3) == -1 && sub_0600A604(7, -3) == 1 && sub_0600A604(5, 0) == 0, "__modsi3");
    CHECK(sub_0600CEF4(0xFFFFFFF9u, 3) == (0xFFFFFFF9u % 3u) && sub_0600CEF4(9, 0) == 0, "__umodsi3");
    load(0);
    /* idle fidget: idle counter % 0x060EA444 == 0 -> 0x1E, new period 0x20 + 0x60 * k or 0xB4 */
    W16(0x060EA444u, 0x50); W16(0x060EA430u, 0);
    PW16(P_ACTION, ACT_IDLE); PW16(P_FLAGS2, P16(P_FLAGS2) & 0x77FF);
    int first = -1;
    for (int i = 0; i < 0xA0 && first < 0; i++) { tick(0); if (Player_Action() == ACT_IDLE_FIDGET2) first = i; }
    uint16_t per = R16(0x060EA444u);
    printf("   fidget after %d idle frames, next period %d\n", first, per);
    CHECK(first >= 0x4E && first <= 0x51, "fidget not at idle counter 0x50 (%d)", first);
    CHECK(per == 0xB4 || (per >= 0x20 && (per - 0x20) % 0x60 == 0 && per <= 0x1A0), "fidget period %d", per);
    /* command buffer 0x06042B90: up, up+side, side -> ACT_COMMAND_Q (0x25) */
    for (int i = 0; i < 20; i++) tick(0);
    PW16(P_ACTION, ACT_IDLE); W16(0x060EA432u, 0);
    tick(PAD_UP); tick(PAD_UP | PAD_RIGHT); tick(PAD_RIGHT); tick(0);
    CHECK(Player_Action() == ACT_COMMAND_Q, "command move not triggered (action %02X, buffer %04X)", Player_Action(), R16(0x060EA432u));
    /* 8-shuriken fan 0x06042C88 */
    W16(0x060252B6u, 20);
    int np = 0;
    for (int k = 0; k < 64; k++) np += R16(PROJECTILES + (uint32_t)k * 0x40) != 0xFFFF;
    sub_06042C88();
    int np2 = 0;
    for (int k = 0; k < 64; k++) np2 += R16(PROJECTILES + (uint32_t)k * 0x40) != 0xFFFF;
    CHECK(np2 - np == 8 && R16(0x060252B6u) == 12, "shuriken fan: %d projectiles, %d left", np2 - np, R16(0x060252B6u));
    W16(0x060252B6u, 5); sub_06042C88();
    CHECK(R16(0x060252B6u) == 0, "shuriken count not clamped");
    /* on hit 0x060410F8 */
    PW16(P_FLAGS4, 0x4001); PW16(P_FLAGS2, P16(P_FLAGS2) | 0x1000);
    sub_060410F8();
    CHECK(P16(P_FLAGS4) == 1 && !(P16(P_FLAGS2) & 0x1000), "on-hit flags");
    /* debug move 0x06040F80 */
    W16(ADDR_GAME_HELD, PAD_LEFT | PAD_DOWN);
    sub_06040F80();
    CHECK(P32(P_VELX) == (int32_t)0xFFFE0000 && P32(P_VELY) == (int32_t)0xFFFE0000 && (P16(P_FLAGS2) & 0x100), "debug move");
    /* third combo slash 0x06043A44 */
    PW16(P_ACTION, 0x1C); PW16(P_ANIM, 0); PW16(P_FLAGS2, P16(P_FLAGS2) & 0x7FFF); PW32(P_VELX, 0);
    sub_06043A44();
    CHECK(P32(P_VELX) == 0x30000 && Player_Action() == 0x1C, "0x1C step");
    PW16(P_ANIM, PANIM_FINISHED); sub_06043A44();
    CHECK(Player_Action() == ACT_RECOVER, "0x1C end");
    /* posture 4: hanging, X with the scroll -> ninjutsu 0x4A */
    PW16(P_ACTION, 0x40); W16(MAGIC_AV, 1); W16(MAGIC, 0); W16(ADDR_GAME_PRESSED, PAD_X); W16(ADDR_GAME_HELD, PAD_X);
    sub_060424A6();
    CHECK(Player_Action() == 0x4A && (R16(MAGIC) & 0x200F) == 0x2002 && (R16(SYSFLAGS) & 0x8000), "posture-4 ninjutsu");
    AND16(SYSFLAGS, 0x7FFF); W16(MAGIC, 0); W16(MAGIC_AV, 0);
    PW16(P_ACTION, 0x40); W16(ADDR_GAME_PRESSED, R16(ADDR_ACTION_BTN2)); W16(ADDR_GAME_HELD, PAD_DOWN);
    sub_060424A6();
    CHECK(Player_Action() == 0x3A, "posture 4: down + jump does not let go (%02X)", Player_Action());
    W16(ADDR_GAME_PRESSED, 0); W16(ADDR_GAME_HELD, 0);
    /* sword clink 0x0604592E: a sword box inside a wall -> spark + FLAGS4 0x20 */
    const uint32_t HB = 0x060FFD80u;
    W16(HB, 2); W16(HB + 2, 0); W16(HB + 4, -30); W16(HB + 6, 24); W16(HB + 8, -10); W16(HB + 10, 0xFFFF);
    int found = 0;
    for (int32_t x = 0x04000000; x < 0x07000000 && !found; x += 0x80000)
        for (int32_t y = 0x04000000; y < 0x06000000 && !found; y += 0x80000)
            if (Coll_AttrFlags(Map_GetAttr(x, y, LAYER_DESC + (uint32_t)RS16(G_COLL_LAYER) * 0x30u)) & 0xC) {
                PW32(P_X, (uint32_t)(x - 0x100000)); PW32(P_Y, (uint32_t)(y + 0x140000)); found = 1;
            }
    CHECK(found, "no wall tile on 1-1");
    PW32(0x68, HB); PW16(P_FLAGS2, P16(P_FLAGS2) & 0x7FFF); PW16(P_FLAGS4, 0);
    sub_0604592E();
    CHECK(P16(P_FLAGS4) & 0x20, "sword in a wall: no clink");
    PW16(P_FLAGS4, 0); PW32(P_Y, P32(P_Y) - 0x02000000);
    sub_0604592E();
    CHECK(!(P16(P_FLAGS4) & 0x20), "clink in the air");
}

int main(int argc, char **argv)
{
    g_dir = argc > 1 ? argv[1] : "../extracted/";
    Res_SetAssetDir(g_dir);
    test_ziryu();
    test_kinds();
    test_player_subs();
    if (fails) { printf("%d FAILURES\n", fails); return 1; }
    printf("all ninjutsu tests OK\n");
    return 0;
}
