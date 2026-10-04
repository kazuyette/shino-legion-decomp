/* Bosses of 7-2 (type 6, Koryu), 8-2 (type 0x17) and 9-2 (type 0x1F, Kazuma), agent "bossesB":
 * each on its real stage data: the boss is placed, acts (several actions, projectiles),
 * takes damage from Sho's sword, dies and posts the stage end (0x0602529E & 7 == 2;
 * for 9-2 the ending path is checked too).
 *   gcc -std=c11 -Isrc -Itests tests/test_bosses_b.c $(cat tests/SOURCES.txt) -lm
 *   ./a.out ../extracted/ [type-in-hex: only that boss] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pad.h"
#include "input.h"
#include "player.h"
#include "ram.h"
#include "render.h"
#include "stage.h"
#include "camera.h"
#include "sprite.h"
#include "objects.h"
#include "enemy.h"
#include "fx.h"
#include "shinobi.h"
#include "resource.h"
#include "ninjutsu.h"
#include "stage_hooks.h"
#include "fade.h"
#include "objhandlers_c.h"
static InputPort fake[2];
const InputPort *Input_GetPort(int p) { return &fake[p]; }
static const char *g_dir;
static int g_only = -1;
static int g_proj[0x40], g_fx[0x40];
static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("   FAIL: " __VA_ARGS__); printf("\n"); fails++; } } while (0)

static void scan(void)
{
    for (int k = 0; k < 64; k++) { uint16_t t = R16(PROJECTILES + (uint32_t)k * 0x40); if (t < 0x40) g_proj[t]++; }
    for (int k = 0; k < 64; k++) { uint16_t t = R16(EFFECTS + (uint32_t)k * 0x30); if (t < 0x40) g_fx[t]++; }
}
static void tick(uint16_t held)
{
    fake[0].connected = 1; fake[0].raw = (uint16_t)~held;
    W16(0x060143EAu, R16(0x060143EAu) + 1u); W16(0x060F4CCCu, R16(0x060F4CCCu) + 1u);
    Pad_Update(2); Scroll_Update(); Game_LatchPad1(P16(P_FLAGS4)); Player_Tick();
    Obj_UpdateEnemies(); Obj_UpdateAll(); Projectiles_Update(); Effects_Update(); Items_Update();
    Stage_FrameHooks();                                  /* 0x0606BCD0 */
    Ninjutsu_Update();                                   /* 0x06063554: the 9-2 NYORAI finish */
    Fade_Update();
}
static uint32_t slot(int s) { return OBJ_TABLE + (uint32_t)s * OBJ_SIZE; }
static void place_sho(uint32_t e, int32_t dx, int32_t dy)
{
    PW16(P_HP, 16);
    PW32(P_X, (uint32_t)(RS32(e + 8) + dx));
    PW32(P_Y, (uint32_t)(RS32(e + 0xC) + dy));
    PW32(P_VELY, 0);
    W32(0x060252A0u, (uint32_t)(P32(P_X) - RS32(0x060EA550u)));
    W32(0x060252A4u, (uint32_t)(P32(P_Y) - RS32(0x060EA554u)));
}
static int load_stage(int idx)
{
    char p[512];
    snprintf(p, sizeof p, "%sA.BIN", g_dir);           if (!Ram_LoadImage(p, 0x06004000u)) return 0;
    snprintf(p, sizeof p, "%sSHINOBI.dec.bin", g_dir); if (!Ram_LoadImage(p, 0x06040000u)) return 0;
    snprintf(p, sizeof p, "%sSHOU.CHR", g_dir);        if (!Ram_LoadImage(p, 0x00200000u)) return 0;
    W16(0x060EC658u, 0); W16(0x060EC65Au, 0); W16(0x060252DCu, 1); W16(0x0602529Eu, 0);
    Shinobi_InitVdp2();
    Fade_Init();                                         /* A.BIN boot: fade weights 1.0 */
    W32(SPR_ZOOM, 0x10000);
    Player_Init();
    Res_SetAssetDir(g_dir);
    if (!Stage_Load(g_dir, idx)) return 0;
    W16(0x060252DCu, 1);
    Stage_SpawnPlayer();
    return 1;
}
static uint32_t find(uint16_t type)
{
    for (int s = 0; s < OBJ_SLOTS; s++) if (R16(slot(s)) == type && (R16(slot(s) + 0x12) & 0x1F) == 0) return slot(s);
    for (int s = 0; s < OBJ_SLOTS; s++) if (R16(slot(s)) == type) return slot(s);
    return 0;
}
static void show(const char *what, const int *v)
{
    printf("   %s:", what);
    for (int i = 0; i < 0x40; i++) if (v[i]) printf(" %X(%d)", i, v[i]);
    printf("\n");
}

typedef struct { uint16_t type; int stage; int maxf; int idle; int32_t dx; int32_t dy0; int ground; } BossCase;   /* dy0: Sho's height offset base; ground: no height cycling */

/* idle `idle` frames with Sho beside it (immortal), then keep Sho next to it slashing; the
 * vertical offset cycles so the sword reaches the weak spots. */
static void test_boss(const BossCase *c)
{
    if (g_only >= 0 && g_only != c->type) return;
    printf("boss type %02X on stage index %d\n", c->type, c->stage);
    if (!load_stage(c->stage)) { CHECK(0, "stage %d not loaded", c->stage); return; }
    uint32_t e = find(c->type);
    CHECK(e != 0, "no boss %02X placed", c->type);
    if (!e) return;
    printf("   at %d,%d\n", RS32(e + 8) >> 16, RS32(e + 0xC) >> 16);
    int acts[0x40] = {0}, hits = 0, cleared = -1, f, hurt = 0;
    static const int32_t dys[6] = { 0, -0x200000, 0x200000, -0x400000, 0x400000, -0x600000 };
    memset(g_proj, 0, sizeof g_proj); memset(g_fx, 0, sizeof g_fx);
    int16_t hp = -1, hp0 = -1;
    place_sho(e, c->dx, 0);
    for (f = 0; f < c->maxf; f++) {
        if (R16(e) == c->type) {
            int32_t dx = (RS32(e + 8) < P32(P_X)) ? 0x00300000 : -0x00300000;
            if ((f / 300) & 1) dx = (R16(e + 2) & 0x8000) ? 0x00300000 : -0x00300000;
            if (f == 1) place_sho(e, c->dx, 0);                 /* after the boss's init */
            if (f > c->idle) place_sho(e, dx, c->dy0 + (c->ground ? 0 : dys[(f / 150) % 6]));
            else { if ((int16_t)P16(P_HP) < 16) { hurt++; if (getenv("DBGH") && hurt < 20) printf("f%d Sho HP %d act %X sho fl2 %04X\n", f, (int16_t)P16(P_HP), R16(e+6), P16(2)); } PW16(P_HP, 16); }
            PW32(P_VELY, 0);
            if (hp >= 0 && RS16(e + 0x16) < hp) hits++;
            hp = RS16(e + 0x16);
            if (hp0 < hp) hp0 = hp;
            acts[R16(e + 6) & 0x3F]++;
            if (getenv("DBG") && f % 25 == 0) { uint32_t hb = R32(e + 0x68); printf("   boxes:"); for (int k = 0; hb && k < 8 && R16(hb + 10u*k) != 0xFFFF; k++) printf(" [%d %d %d %d %d]", RS16(hb+10u*k), RS16(hb+10u*k+2), RS16(hb+10u*k+4), RS16(hb+10u*k+6), RS16(hb+10u*k+8)); printf("\n"); }
            if (getenv("DBGK") && f % 50 == 0) printf("f%d K_ACT %X sub %d +6 %X +4 %04X fl2 %04X hp %d 4E %d 50 %d orbs %d x %d y %d sho %d,%d\n", f, R16(0x060F7AE0u), RS16(0x060F7AE2u), R16(e+6), R16(e+4), R16(e+2), RS16(e+0x16), RS16(e+0x4E), RS16(e+0x50), RS16(0x060F7AD4u), RS32(e+8)>>16, RS32(e+0xC)>>16, P32(P_X)>>16, P32(P_Y)>>16);
            if (getenv("DBGN") && f % 20 == 0 && f > 4400 && f < 6600) printf("cur %X %X %X tgt %X sp %X run %d %X ", R32(0x06025100u), R32(0x06025104u), R32(0x06025108u), R16(0x060250F8u), R32(0x06025110u), R16(0x06025130u), R32(0x0602512Cu)), printf("fade %d q %d tm %d mask %04X o0 %d,%d sho %d,%d cam %d,%d ", R16(0x0602510Cu), Ram_Ptr(0x06025114u)[0], R16(0x060F23C4u), R16(0x060F22C4u), RS32(0x060F2394u)>>16, RS32(0x060F23ACu)>>16, P32(P_X)>>16, P32(P_Y)>>16, RS32(0x060252A0u)>>16, RS32(0x060252A4u)>>16), printf("f%d frz %04X magic %04X njst %d step %d orbst %d sho act %X anim %04X fl2 %04X\n", f, R16(0x0602529Eu), R16(0x060252BAu), R16(0x060F4BF0u), R16(0x060F4BF6u), R16(0x060F23CAu), P16(6), P16(0x58), P16(2));
            if (getenv("DBGX") && f % 10 == 0 && f < 2000) printf("f%d act %X +4 %04X 4A %04X 4C %d 48 %d 50 %d 52 %X steps %d idle %d 14 %d\n", f, R16(e+6), R16(e+4), R16(e+0x4A), RS16(e+0x4C), RS16(e+0x48), RS16(e+0x50), R16(e+0x52), RS16(0x060F6894u), RS16(0x060F6B44u), R16(e+0x14));
            if (getenv("DBG") && f % 25 == 0) printf("f%d act %X fl2 %04X +4 %04X hp %d x %d y %d sho %d,%d anim %04X/%d\n", f, R16(e+6), R16(e+2), R16(e+4), RS16(e+0x16), RS32(e+8)>>16, RS32(e+0xC)>>16, P32(P_X)>>16, P32(P_Y)>>16, R16(e+0x58), R16(e+0x5A));
        }
        uint16_t held = (f > c->idle) ? (uint16_t)((f & 8) ? PAD_B : 0) : 0;
        if (f > c->idle && R16(e) == c->type) {
            if (RS32(e + 8) > P32(P_X) && (P16(2) & 0x8000)) held |= PAD_RIGHT;
            if (RS32(e + 8) < P32(P_X) && !(P16(2) & 0x8000)) held |= PAD_LEFT;
        }
        tick(held); scan();
        if ((R16(0x0602529Eu) & 7) == 2 && cleared < 0) { cleared = f; break; }
    }
    show("actions", acts);
    show("projectiles", g_proj);
    show("effects", g_fx);
    printf("   HP %d -> %d, %d hits, Sho hurt %d times while idle, stage end at frame %d\n", hp0, hp, hits, hurt, cleared);
    int nacts = 0; for (int i = 0; i < 0x40; i++) nacts += acts[i] != 0;
    int nproj = 0; for (int i = 0; i < 0x40; i++) nproj += g_proj[i] != 0;
    CHECK(nacts > 3, "boss did nothing");
    CHECK(nproj > 0, "no projectiles");
    CHECK(hits > 0, "boss never hurt");
    CHECK(cleared >= 0, "stage end not posted");
    if (cleared >= 0 && c->stage == 17) {                /* 9-2: clear -> tally -> ending (stage -1) */
        PW16(P_HP, 16);
        for (int i = 0; i < 600 && R16(G_GAME_MODE) != 7; i++) { Fade_Update(); Stage_UpdateState(); }
        printf("   after the fade: game mode %d, stage index %d\n", R16(G_GAME_MODE), RS16(G_STAGE_INDEX));
        CHECK(R16(G_GAME_MODE) == 7 && RS16(G_STAGE_INDEX) == -1, "9-2 clear does not lead to the ending");
        if (R16(G_GAME_MODE) == 7) {
            Game_ModeDispatch();                           /* 0x0606BA08: bonuses, DIRECTOR request 8 */
            printf("   director request %u, game mode %d (DIRECTOR tally, then story -1 = ENDING + staff roll)\n", R32(G_DIRECTOR_REQ), R16(G_GAME_MODE));
            CHECK(R32(G_DIRECTOR_REQ) == 8 && R16(G_GAME_MODE) == 11, "no DIRECTOR tally request");
        }
    }
}

/* 9-2 specials: force the boss's phase (+0x4E) and the orb-call flag (+4 & 8), then watch
 * for the storm (0x3F8: pendulum flames 0x1B) and the dragon (0x3F9: projectile 0x1C). */
static void test_kazuma_special(int16_t phase, uint16_t act, uint16_t proj)
{
    if (g_only >= 0 && g_only != 0x1F) return;
    printf("boss 1F special phase %d (action %X, projectile %X)\n", phase, act, proj);
    if (!load_stage(17)) { CHECK(0, "stage 17 not loaded"); return; }
    uint32_t e = find(0x1F);
    if (!e) { CHECK(0, "no boss"); return; }
    memset(g_proj, 0, sizeof g_proj); memset(g_fx, 0, sizeof g_fx);
    int seen = 0, done = 0;
    for (int f = 0; f < 12000; f++) {
        if (f == 1) place_sho(e, -0x00C00000, 0);
        PW16(P_HP, 16);
        if (R16(e) == 0x1F && f > 200 && !seen) { W16(e + 0x4E, (uint16_t)phase); OR16(e + 4, 8); }
        if (R16(e) == 0x1F && R16(0x060F7AE0u) == act) seen = 1;
        if (seen && R16(e) == 0x1F && R16(0x060F7AE0u) != act && R16(0x060F7AE0u) != 0x17) { done = 1; break; }
        tick(0); scan();
    }
    show("projectiles", g_proj);
    printf("   special %s, %s\n", seen ? "started" : "NOT started", done ? "finished" : "not finished");
    CHECK(seen, "special %X not started", act);
    CHECK(done, "special %X did not finish", act);
    CHECK(g_proj[proj] > 0, "no projectile %X", proj);
}

int main(int argc, char **argv)
{
    g_dir = argc > 1 ? argv[1] : "../extracted/";
    if (argc > 2) g_only = (int)strtol(argv[2], NULL, 16);
    static const BossCase cases[] = {
        { 0x06, 13, 20000, 1500, -0x00A00000, 0 },          /* 7-2 Koryu */
        { 0x17, 15, 30000, 4000, -0x00600000, -0x00600000 },          /* 8-2 */
        { 0x1F, 17, 60000, 3000, -0x00A00000, 0, 1 },                     /* 9-2 Kazuma */
    };
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) test_boss(&cases[i]);
    test_kazuma_special(3, 0x3F8, 0x1B);                 /* storm, pendulum flames */
    test_kazuma_special(5, 0x3F9, 0x1C);                 /* fire dragon */
    printf(fails ? "%d FAILURES\n" : "all bossesB tests OK\n", fails);
    return fails != 0;
}
