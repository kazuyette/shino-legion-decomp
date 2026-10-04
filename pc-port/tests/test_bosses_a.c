/* Bosses ported by agent "bossesA" on their real stages: 0x21 (2-1, enemy_boss21.c), 0x1A
 * (3-2, enemy_boss1a.c) and 9 (6-2, enemy_boss09.c). For each: the boss appears, attacks
 * (actions / projectiles seen), takes sword hits (HP drops, boss bar drawn), dies and the
 * stage clear is posted (0x0602529E & 7 == 2; on 2-1 through the exit box once the boss
 * has released the camera).
 *   gcc -std=c11 -Isrc -Itests tests/test_bosses_a.c $(cat tests/SOURCES.txt) -lm
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
#include "stage_hooks.h"
#include "camera.h"
#include "sprite.h"
#include "objects.h"
#include "objhandlers_c.h"
#include "enemy.h"
#include "fx.h"
#include "fade.h"
#include "shinobi.h"
#include "resource.h"
#include "soft_render.h"
extern RenderSprite g_spriteLog[]; extern int g_spriteLogCount;
static InputPort fake[2];
const InputPort *Input_GetPort(int p) { return &fake[p]; }
static const char *g_dir;
static int g_only = -1, fails;
static int g_proj[0x40];
#define CHECK(c, ...) do { if (!(c)) { printf("   FAIL: " __VA_ARGS__); printf("\n"); fails++; } } while (0)
#define CTRL 0x060EA618u

static void tick(uint16_t held)
{
    g_spriteLogCount = 0;
    fake[0].connected = 1; fake[0].raw = (uint16_t)~held;
    W16(0x060143EAu, R16(0x060143EAu) + 1u); W16(0x060F4CCCu, R16(0x060F4CCCu) + 1u);
    Pad_Update(2); Scroll_Update(); Game_LatchPad1(P16(P_FLAGS4)); Player_Tick();
    Stage_UpdateState();
    Obj_UpdateEnemies(); Obj_UpdateAll(); Projectiles_Update(); Effects_Update(); Items_Update();
    Stage_FrameHooks();
    Fade_Update();
    for (int k = 0; k < 64; k++) { uint16_t t = R16(PROJECTILES + (uint32_t)k * 0x40); if (t < 0x40) g_proj[t]++; }
}
/* SHOTS=dir: screenshot of the current frame (after a tick) */
static void shot(const char *name)
{
    static uint32_t fb[SCREEN_W * SCREEN_H];
    char p[512];
    const char *d = getenv("SHOTS");
    if (!d) return;
    W16(0x0603A570u, R16(0x0603A570u) | 0x8000);
    Soft_ComposeFrame(fb, g_spriteLog, g_spriteLogCount, 0xFF000000u);
    snprintf(p, sizeof p, "%s/%s.png", d, name);
    if (Soft_WritePng(p, fb, SCREEN_W, SCREEN_H)) printf("   screenshot %s (state %04X, Sho %d,%d HP %d)\n", p, R16(0x0602529Eu), P32(P_X) >> 16, P32(P_Y) >> 16, (int16_t)P16(P_HP));
}
static uint32_t slot(int s) { return OBJ_TABLE + (uint32_t)s * OBJ_SIZE; }
static uint32_t find(uint16_t type)
{
    for (int s = 0; s < OBJ_SLOTS; s++) if (R16(slot(s)) == type) return slot(s);
    return 0;
}
static void place_sho(int32_t x, int32_t y)
{
    PW32(P_X, (uint32_t)x); PW32(P_Y, (uint32_t)y); PW32(P_VELY, 0);
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
    W32(SPR_ZOOM, 0x10000);
    Res_SetAssetDir(g_dir);                              /* the bosses' .PBC graphics */
    Fade_Init();
    Player_Init();
    if (!Stage_Load(g_dir, idx)) return 0;
    W16(0x060252DCu, 1);
    Stage_SpawnPlayer();
    memset(g_proj, 0, sizeof g_proj);
    return 1;
}
static void show(const char *what, const int *acts)
{
    printf("   %s actions:", what);
    for (int i = 0; i < 0x40; i++) if (acts[i]) printf(" %X(%d)", i, acts[i]);
    printf("\n   projectile types seen:");
    for (int k = 0; k < 0x40; k++) if (g_proj[k]) printf(" %X(%d)", k, g_proj[k]);
    printf("\n");
}
/* keep Sho (immortal) next to boss e, slashing, until it is gone; returns frames or -1 */
static int fight(uint32_t e, uint16_t type, int maxf, int *acts, int *hits, int *hurt)
{

    static const int32_t dys[4] = { 0, -0x200000, 0x200000, -0x400000 };
    int16_t hp = RS16(e + 0x16);
    for (int f = 0; f < maxf; f++) {
        if (R16(e) != type) return f;
        int32_t dx = (RS32(e + 8) < P32(P_X)) ? 0x00280000 : -0x00280000;
        if ((f / 300) & 1) dx *= 2;                       /* vary the distance (other moves) */
        int32_t ex = RS32(e + 8) - P32(P_X), ey = RS32(e + 0xC) - P32(P_Y);
        if (ex > 0x700000 || ex < -0x700000 || ey > 0x500000 || ey < -0x500000 || (f % 97) == 0)
            place_sho(RS32(e + 8) + dx, RS32(e + 0xC) + dys[(f / 200) % 4]);
        /* 2-1: the locked camera keeps Sho out of the exit box (x 1938..1986) in the game */
        if (type == 0x21 && P32(P_X) > 0x07800000) place_sho(0x07800000, P32(P_Y));
        uint16_t held = (f & 8) ? PAD_B : 0;
        if (RS32(e + 8) > P32(P_X) && (P16(2) & 0x8000)) held |= PAD_RIGHT;
        if (RS32(e + 8) < P32(P_X) && !(P16(2) & 0x8000)) held |= PAD_LEFT;
        PW16(P_HP, 16);
        int16_t php = 16;
        tick(held);
        if ((int16_t)P16(P_HP) < php) (*hurt)++;
        /* PC test: Sho shrugs the hits off (no hurt reaction / knock-back) and keeps slashing */
        PW16(2, P16(2) & 0xFFF7); PW16(4, P16(4) & 0xBFFF);
        if (R16(e) != type) return f;
        if (f == 400 || f == 1000) { char n[32]; snprintf(n, sizeof n, "boss%02X_f%d", type, f); shot(n); }
        if (RS16(e + 0x16) < hp) (*hits)++;
        hp = RS16(e + 0x16);
        acts[R16(e + 6) & 0x3F]++;
        if (getenv("DBG") && f % 30 == 0) printf("f%d act %X hp %d x %d y %d sho %d,%d anim %04X fl2 %04X\n", f, R16(e + 6), RS16(e + 0x16), RS32(e + 8) >> 16, RS32(e + 0xC) >> 16, P32(P_X) >> 16, P32(P_Y) >> 16, R16(e + 0x58), R16(e + 2));
    }
    return -1;
}

/* 2-1: the frame hook registers the boss when the camera reaches it (camera locked) */
static void test_21(void)
{
    if (g_only >= 0 && g_only != 0x21) return;
    printf("boss 21 on stage index 2 (2-1)\n");
    if (!load_stage(2)) { CHECK(0, "stage 2 not loaded"); return; }
    CHECK(find(0x21) == 0, "boss present before the camera reaches it");
    place_sho(0x06E00000, 0x05A00000);
    uint32_t e = 0;
    for (int f = 0; f < 60 && !e; f++) { PW16(P_HP, 16); tick(0); e = find(0x21); }
    printf("   cam %d,%d sho %d,%d\n", RS32(0x060252A0u)>>16, RS32(0x060252A4u)>>16, P32(P_X)>>16, P32(P_Y)>>16);
    CHECK(e != 0, "boss not registered by the 2-1 hook");
    if (!e) return;
    printf("   registered at %d,%d, camera lock %04X, slot field %d\n", RS32(e + 8) >> 16, RS32(e + 0xC) >> 16, R16(CTRL + 0x20), RS16(CTRL + 0x1E));
    CHECK((R16(CTRL + 0x20) & 0xB) == 0xB, "camera not locked");
    {   /* its init streams SAKYOU.PBC into VDP1 VRAM at the slot's area + 0x4740 */
        int sl = Enemy_FindSlot(0x21), nz = 0;
        for (int k = 0; k < 64 && R16(e + 2) & 1; k++) tick(0);
        uint32_t v = 0x25C04740u + R32(0x060A9070u + 4u * (uint32_t)sl);
        for (uint32_t k = 0; k < 0x1000; k += 4) nz += R32(v + k) != 0;
        printf("   SAKYOU.PBC in VDP1 VRAM: %d/1024 non-zero words\n", nz);
        CHECK(nz > 0, "SAKYOU.PBC graphics not loaded");
    }
    int acts[0x40] = {0}, hits = 0, hurt = 0;
    int16_t php = 16;
    for (int f = 0; f < 900 && R16(e) == 0x21; f++) {     /* watch it attack Sho standing aside */
        PW16(P_HP, 16); php = 16;
        if (f % 120 == 0) place_sho(0x06A00000, RS32(e + 0xC));
        tick(0);
        if ((int16_t)P16(P_HP) < php) hurt++;
        if (R16(e) == 0x21) acts[R16(e + 6) & 0x3F]++;
        if (getenv("DBG") && f % 30 == 0) printf("i%d act %X hp %d x %d y %d anim %04X pat %d\n", f, R16(e + 6), RS16(e + 0x16), RS32(e + 8) >> 16, RS32(e + 0xC) >> 16, R16(e + 0x58), RS16(e + 0x4A));
    }
    show("idle", acts);
    int nacts = 0; for (int i = 0; i < 0x40; i++) nacts += acts[i] != 0;
    CHECK(nacts >= 4, "boss did too little (%d actions)", nacts);
    CHECK(g_proj[0x26] > 0, "no shuriken (projectile 0x26)");
    CHECK(RS16(e + 0x16) == 15, "HP %d (want 15 on normal)", RS16(e + 0x16));
    memset(acts, 0, sizeof acts);
    int sc = RS32(SCORE);
    int f = fight(e, 0x21, 12000, acts, &hits, &hurt);
    show("fight", acts);
    printf("   gone after %d frames, %d hits, Sho hurt %d times, score +%d, camera lock %04X\n", f, hits, hurt, RS32(SCORE) - sc, R16(CTRL + 0x20));
    CHECK(f >= 0, "boss not killed");
    CHECK(hits > 0, "boss never hurt");
    CHECK(acts[6] + acts[7] > 0, "no death action");
    CHECK(g_proj[0x27] > 0, "no death blasts (projectile 0x27)");
    CHECK(RS32(SCORE) - sc >= 1500, "no 1500 points");
    CHECK((R16(CTRL + 0x20) & 0xB) == 0 && R16(CTRL + 0x1E) == 0xFFFF, "camera still locked");
    int cleared = -1;                                     /* walk to the exit box */
    place_sho(0x07700000, 0x05DF0000);
    for (int k = 0; k < 600 && cleared < 0; k++) { PW16(P_HP, 16); tick(PAD_RIGHT); if ((R16(0x0602529Eu) & 7) == 2) cleared = k; }
    printf("   stage clear at frame %d (Sho %d,%d)\n", cleared, P32(P_X) >> 16, P32(P_Y) >> 16);
    CHECK(cleared >= 0, "stage not cleared");
}

/* a boss placed with the stage: intro, watch it a while, fight it, stage clear */
static void test_placed(uint16_t type, int stage, const char *name, int16_t hp_normal, const int *projs, int nproj)
{
    if (g_only >= 0 && g_only != type) return;
    printf("boss %02X on stage index %d (%s)\n", type, stage, name);
    if (!load_stage(stage)) { CHECK(0, "stage %d not loaded", stage); return; }
    uint32_t e = find(type);
    int walls0 = 0;
    for (int k = 0; k < OBJ_SLOTS; k++) walls0 += R16(slot(k)) == 0x1039;
    CHECK(e != 0, "boss %02X not placed", type);
    if (!e) return;
    for (int k = 0; k < OBJ_SLOTS; k++)                  /* alone with it */
        if (R16(slot(k)) < 0x1000 && R16(slot(k)) != type) W16(slot(k), 0xFFFF);
    PW16(P_HP, 16); tick(0);                             /* boss init: its own start position */
    place_sho(RS32(e + 8) - 0x800000, 0x05DF0000);       /* (both arenas: floor at y 1503) */
    int acts[0x40] = {0}, hits = 0, hurt = 0;
    for (int f = 0; f < 1500 && R16(e) == type; f++) {
        PW16(P_HP, 16);
        if (f > 0 && f % 200 == 0) place_sho(RS32(e + 8) + ((f / 200) & 1 ? 0x900000 : -0x900000), 0x05DF0000);
        tick(0);
        if ((int16_t)P16(P_HP) < 16) hurt++;
        if (R16(e) == type) acts[R16(e + 6) & 0x3F]++;
        if (getenv("DBG") && f % 30 == 0) printf("i%d act %X mode %X hp %d x %d y %d anim %04X sho %d,%d\n", f, R16(e + 6), R16(e + 0x4E), RS16(e + 0x16), RS32(e + 8) >> 16, RS32(e + 0xC) >> 16, R16(e + 0x58), P32(P_X) >> 16, P32(P_Y) >> 16);
    }
    show("idle", acts);
    int nacts = 0; for (int i = 0; i < 0x40; i++) nacts += acts[i] != 0;
    { int w = 0; for (int k = 0; k < OBJ_SLOTS; k++) w += R16(slot(k)) == 0x1039;
      printf("   HP %d, hit Sho %d times, walls 0x1039 %d -> %d\n", RS16(e + 0x16), hurt, walls0, w); }
    CHECK(R16(e) == type, "boss vanished on its own");
    CHECK((R16(0x0602529Eu) & 7) == 0, "stage state %04X after the idle phase", R16(0x0602529Eu));
    CHECK(nacts >= 4, "boss did too little (%d actions)", nacts);
    CHECK(RS16(e + 0x16) == hp_normal, "HP %d (want %d on normal)", RS16(e + 0x16), hp_normal);
    memset(acts, 0, sizeof acts);
    int sc = RS32(SCORE), cleared = -1, walls1 = 0;
    int f = fight(e, type, 15000, acts, &hits, &hurt);
    show("fight", acts);
    for (int k = 0; k < nproj; k++) CHECK(g_proj[projs[k]] > 0, "projectile %X never seen", projs[k]);
    for (int k = 0; k < 900 && cleared < 0; k++) { if ((R16(0x0602529Eu) & 7) == 2) cleared = k; else { PW16(P_HP, 16); tick(0); } }
    for (int k = 0; k < OBJ_SLOTS; k++) walls1 += R16(slot(k)) == 0x1039;
    printf("   gone after %d frames, %d hits, Sho hurt %d times, score +%d, stage clear %d frames later\n", f, hits, hurt, RS32(SCORE) - sc, cleared);
    if (type == 9) {                                     /* 6-2: the death ring collapses the walls 0x1039 */
        printf("   collapsing walls 0x1039: %d on the stage, %d after the boss\n", walls0, walls1);
        CHECK(walls0 > 0 && walls1 < walls0, "no wall 0x1039 collapsed");
    }
    CHECK(f >= 0, "boss not killed");
    CHECK(hits > 0, "boss never hurt");
    CHECK(cleared >= 0, "stage not cleared");
}

int main(int argc, char **argv)
{
    g_dir = argc > 1 ? argv[1] : "../extracted/";
    if (argc > 2) g_only = (int)strtol(argv[2], NULL, 16);
    test_21();
    { static const int p1a[] = { 0x2A, 0x2B, 0x2C }; test_placed(0x1A, 5, "3-2", 20, p1a, 3); }
    { static const int p09[] = { 0xA }; test_placed(0x09, 11, "6-2", 25, p09, 1); }
    printf(fails ? "%d FAILURES\n" : "all bosses-A tests OK\n", fails);
    return fails != 0;
}
