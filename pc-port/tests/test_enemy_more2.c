/* Regular enemy types 0xA / 0xB (8-1), 0xC (6-1) and 0x19 (9-1, projectile 0x23) on the
 * real stage data (agent "regulars", enemy_t0a.c / enemy_t0b.c / enemy_t0c.c /
 * enemy_t19.c): each one is woken next to Sho, must move / attack (projectiles seen),
 * then is fought with the sword until it dies (score must go up). Type 0x19: Sho walks
 * past a launcher, a sled must be launched, fly left, blow up on him (projectile 0x23,
 * Sho hurt) and disappear.
 *   gcc -std=c11 -Isrc -Itests tests/test_enemy_more2.c $(cat tests/SOURCES.txt) -lm
 *   ./a.out ../extracted/ [type-in-hex: only that type] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
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
static InputPort fake[2];
const InputPort *Input_GetPort(int p) { return &fake[p]; }
static const char *g_dir;
static int g_only = -1;                              /* argv[2]: run only this type (hex) */
static int g_proj[0x40];
static int g_pick_y = -1;
static int g_idle = 240;
static int g_pick_x = -1;                            /* pick the instance nearest to this X (px) */                             /* idle frames before the fight */                            /* pick the instance nearest to this Y (px) */                             /* projectile types seen */
static void scan_proj(void) { for (int k = 0; k < 64; k++) { uint16_t t = R16(PROJECTILES + (uint32_t)k * 0x40); if (t < 0x40) g_proj[t]++; } }
static int fails;
static int32_t g_dx;                                 /* fight target: enemy origin + g_dx (type C: its body) */
static int32_t g_pin;                                /* != 0: fight hovering at (+g_dx, +g_pin) of the enemy */
static int32_t g_floor;                              /* Sho's floor (world Y, 16.16) when not at the enemy's Y */
#define CHECK(c, ...) do { if (!(c)) { printf("   FAIL: " __VA_ARGS__); printf("\n"); fails++; } } while (0)

static void tick(uint16_t held)
{
    fake[0].connected = 1; fake[0].raw = (uint16_t)~held;
    W16(0x060143EAu, R16(0x060143EAu) + 1u); W16(0x060F4CCCu, R16(0x060F4CCCu) + 1u);
    Pad_Update(2); Scroll_Update(); Game_LatchPad1(P16(P_FLAGS4));
    if (getenv("DBG4")) {
        int h0 = (int16_t)P16(P_HP); Player_Tick(); int h1 = (int16_t)P16(P_HP); Obj_UpdateEnemies(); int h2 = (int16_t)P16(P_HP);
        Obj_UpdateAll(); int h3 = (int16_t)P16(P_HP); Projectiles_Update(); int h4 = (int16_t)P16(P_HP);
        Effects_Update(); Items_Update();
        if (h4 != h0) printf("   hp %d -> player %d enemies %d objects %d projectiles %d\n", h0, h1, h2, h3, h4);
        return;
    }
    Player_Tick();
    Obj_UpdateEnemies(); Obj_UpdateAll(); Projectiles_Update(); Effects_Update(); Items_Update();
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
    W32(SPR_ZOOM, 0x10000);
    Player_Init();
    if (!Stage_Load(g_dir, idx)) return 0;
    for (int k = 0; k < OBJ_SLOTS; k++)              /* PC test: no crates/boxes/statues/doors in the way */
        if (R16(slot(k)) >= 0x1000 && R16(slot(k)) <= 0x1003) W16(slot(k), 0xFFFF);
    W16(0x060252DCu, 1);                            /* no drop-in (0x0606E440 not run here) */
    Stage_SpawnPlayer();
    return 1;
}
/* the enemy of `type` (subtype `sub`, -1 any) nearest to Sho's start, skipping `skip` */
static uint32_t find(uint16_t type, int sub, uint32_t skip)
{
    uint32_t e = 0; int32_t bd = 0x7FFFFFFF;
    for (int s = 0; s < OBJ_SLOTS; s++) {
        uint32_t o = slot(s);
        if (R16(o) != type || o == skip || (sub >= 0 && (R16(o + 0x12) & 0x1F) != sub)) continue;
        int32_t d = RS32(o + 8) - P32(P_X); if (d < 0) d = -d;
        if (g_pick_y >= 0) { d = (RS32(o + 0xC) >> 16) - g_pick_y; if (d < 0) d = -d; }
        if (g_pick_x >= 0) { d = (RS32(o + 8) >> 16) - g_pick_x; if (d < 0) d = -d; }
        if (d < bd) { bd = d; e = o; }
    }
    return e;
}
static void show_acts(const char *who, const int *acts)
{
    printf("   %s actions:", who);
    for (int i = 0; i < 0x40; i++) if (acts[i]) printf(" %X(%d)", i, acts[i]);
    printf("\n");
}
/* fight e (sword, or shurikens) until it is gone or no longer `type`; frames or -1 */
static int fight(uint32_t e, uint16_t type, int shuriken, int maxf, int *acts, int *hurt, int keep_close)
{
    int h = 0, sc0 = RS32(SCORE);
    for (int i = 0; i < maxf; i++) {
        int32_t fy = g_floor ? g_floor - RS32(e + 0xC) : 0;
        int32_t dx = RS32(e + 8) + g_dx - P32(P_X), dy = RS32(e + 0xC) + fy - P32(P_Y);
        uint16_t held = 0;
        if (g_pin) {                                       /* hover Sho at a fixed spot of the enemy */
            place_sho(e, g_dx, g_pin); PW32(P_VELY, 0); dx = -g_dx;
            if ((P16(2) & 0x8000) != ((g_dx > 0) ? 0x8000 : 0)) held = (g_dx > 0) ? PAD_LEFT : PAD_RIGHT;
            else held = (i & 8) ? PAD_B : 0;
            goto go;
        }
        if (keep_close && (dx > 0x00C00000 || dx < -0x00C00000 || dy > 0x00600000 || dy < -0x00600000))
            place_sho(e, g_dx + (dx > 0 ? -0x00300000 : 0x00300000), fy), dx = RS32(e + 8) + g_dx - P32(P_X);
        if (shuriken) {
            if (dx > 0 && (P16(2) & 0x8000)) held = PAD_RIGHT;
            else if (dx < 0 && !(P16(2) & 0x8000)) held = PAD_LEFT;
            else held = (i & 8) ? PAD_A : 0;
        } else {
            if (dx > 0x280000) held = PAD_RIGHT; else if (dx < -0x280000) held = PAD_LEFT;
            else if (g_dx && (P16(2) & 0x8000) != ((g_dx > 0) ? 0x8000 : 0)) held = (g_dx > 0) ? PAD_LEFT : PAD_RIGHT;
            else held = (i & 8) ? PAD_B : 0;
        }
    go:;
        int16_t hp = (int16_t)P16(P_HP);
        tick(held); scan_proj();
        if ((int16_t)P16(P_HP) < hp && (int16_t)P16(P_HP) > -8) h += hp - (int16_t)P16(P_HP);
        if (getenv("DBGS") && RS32(SCORE) != sc0) printf("   score +%d at f%d, enemy %04X act %X\n", RS32(SCORE) - sc0, i, R16(e), R16(e + 6));
        if (R16(e) != type || RS32(SCORE) != sc0) { *hurt = h; return i; }   /* gone, or respawned */
        acts[R16(e + 6) & 0x3F]++;
        if (getenv("DBG2")) printf("f%d mode %d +4 %04X fl2 %04X act %X anim %04X vx %X vy %X x %d y %d sho %d,%d p48 %d t %d\n", i, RS16(e+0x4E), R16(e+4), R16(e+2), R16(e+6), R16(e+0x58), RS32(e+0x30), RS32(e+0x34), RS32(e+8)>>16, RS32(e+0xC)>>16, P32(P_X)>>16, P32(P_Y)>>16, RS16(e+0x48), RS16(e+0x14));
        if (getenv("DBG") && i % 7 == 0) { uint32_t hb = R32(e + 0x68); printf("f%d act %X fl2 %04X dx %d dy %d sho act %d boxes:", i, R16(e+6), R16(e+2), dx>>16, dy>>16, P16(6)); for (int k = 0; hb && k < 6 && R16(hb + 10u*k) != 0xFFFF; k++) printf(" [%d %d %d %d %d]", RS16(hb+10u*k), RS16(hb+10u*k+2), RS16(hb+10u*k+4), RS16(hb+10u*k+6), RS16(hb+10u*k+8)); printf("\n"); }
        if ((int16_t)P16(P_HP) < 6) PW16(P_HP, 16);                    /* keep Sho alive */
    }
    *hurt = h;
    return -1;
}
/* generic test: idle next to it (must move/attack), then fight to the death */
static void test_type(uint16_t type, int stage, int sub, int32_t dx, int32_t dy, int shuriken, int maxf)
{
    if (g_only >= 0 && g_only != type) return;
    printf("type %02X sub %d on stage index %d\n", type, sub, stage);
    if (!load_stage(stage)) { CHECK(0, "stage %d not loaded", stage); return; }
    uint32_t e = find(type, sub, 0);
    CHECK(e != 0, "no type %02X placed", type);
    if (!e) return;
    int s = Enemy_FindSlot((int16_t)type);
    uint16_t cb = R16(0x060A9064u + 2u * (uint32_t)s), bank = R16(0x060A9084u + 2u * (uint32_t)s);
    int pal = 0;
    for (int k = 0; k < 16; k++) pal |= R16(SHADOW_CRAM + 2u * (uint32_t)(bank * 16 + k));
    if (getenv("LIST")) { printf("   Sho start %d,%d cam %d,%d\n", P32(P_X)>>16, P32(P_Y)>>16, RS32(0x060252A0u)>>16, RS32(0x060252A4u)>>16);
        for (int k = 0; k < OBJ_SLOTS; k++) if (R16(slot(k)) == type) printf("   - %d,%d flags %04X\n", RS32(slot(k)+8)>>16, RS32(slot(k)+0xC)>>16, R16(slot(k)+0x12)); }
    printf("   slot %d chars %04X colour bank %d (%s), at %.0f,%.0f flags %04X\n", s, cb, bank,
           pal ? "palette ok" : "EMPTY", RS32(e + 8) / 65536.0, RS32(e + 0xC) / 65536.0, R16(e + 0x12));
    CHECK(pal && R16(CHAR_TABLE + cb * 10u) != 0, "graphics not loaded");
    for (int k = 0; k < OBJ_SLOTS; k++)             /* alone with it */
        if (R16(slot(k)) < 0x1000 && slot(k) != e) W16(slot(k), 0xFFFF);
    if (getenv("DY")) dy = atoi(getenv("DY")) * 0x10000;
    place_sho(e, dx, dy);
    if (type == 0xC) {                                    /* find the floor under it: drop Sho there with the golem parked */
        printf("   world bounds %d..%d x %d..%d\n", RS32(0x060EA618u) >> 16, RS32(0x060EA620u) >> 16, RS32(0x060EA61Cu) >> 16, RS32(0x060EA624u) >> 16);
        W32(0x060EA624u, 0x0C000000u);                    /* PC test: the stage hook widens the bounds as Sho goes down */
        W16(e, 0xFFFF);
        for (int32_t t = dy; t < 0x02000000; t += 0x00100000) {   /* first spot below where Sho survives */
            place_sho(e, 0x00600000, t);
            PW16(P_HP, 16);
            AND16(PLAYER + 2, 0xFFF7);
            int i;
            for (i = 0; i < 60 && (int16_t)P16(P_HP) > 0; i++) tick(0);
            if (i == 60) break;
        }
        g_floor = P32(P_Y);
        W16(e, type);
        printf("   floor at y %d\n", g_floor >> 16);
        place_sho(e, dx, g_floor - RS32(e + 0xC));
    }
    int acts[0x40] = {0}, hurt = 0, moved = 0, modes[0x10] = {0};
    memset(g_proj, 0, sizeof g_proj);
    int32_t x0 = RS32(e + 8), y0 = RS32(e + 0xC);
    for (int i = 0; i < g_idle && R16(e) == type; i++) {
        PW16(P_HP, 16); PW32(P_VELY, 0);
        if (g_floor && (R16(e + 4) & 0x10)) {               /* follow the woken golem (stay 96 px off) */
            int32_t fx = RS32(e + 8) - P32(P_X), fy = g_floor - P32(P_Y);
            if (fx > 0x00C00000 || fx < -0x00C00000 || fy > 0x00600000 || fy < -0x00600000)
                place_sho(e, fx > 0 ? -0x00600000 : 0x00600000, g_floor - RS32(e + 0xC));
        }
        tick(0); scan_proj(); if ((int16_t)P16(P_HP) < 16) hurt++;
        if (getenv("DBG3")) printf("i%d cam %d,%d sho hp %d act %X fl2 %04X fl4 %04X x %d y %d\n", i, RS32(0x060252A0u)>>16, RS32(0x060252A4u)>>16, (int16_t)P16(P_HP), P16(6), P16(2), P16(4), P32(P_X)>>16, P32(P_Y)>>16);
        if (getenv("DBG2") && R16(e) == type) printf("i%d act %X mode %d anim %04X vx %X vy %X x %d y %d sho %d,%d +4 %04X fl2 %04X\n", i, R16(e+6), RS16(e+0x4E), R16(e+0x58), RS32(e+0x30), RS32(e+0x34), RS32(e+8)>>16, RS32(e+0xC)>>16, P32(P_X)>>16, P32(P_Y)>>16, R16(e+4), R16(e+2));
        if (R16(e) == type) {
            acts[R16(e + 6) & 0x3F]++; modes[R16(e + 0x4E) & 0xF]++;
            if (RS32(e + 8) != x0 || RS32(e + 0xC) != y0) moved = 1;
        }
    }
    int nacts = 0; for (int i = 0; i < 0x40; i++) nacts += acts[i] != 0;
    show_acts("idle", acts);
    printf("   modes:"); for (int i = 0; i < 0x10; i++) if (modes[i]) printf(" %X(%d)", i, modes[i]); printf("\n");
    printf("   projectile types seen:"); for (int k = 0; k < 0x40; k++) if (g_proj[k]) printf(" %X(%d)", k, g_proj[k]); printf("\n");
    printf("   moved %s, %d actions, hit Sho %d times\n", moved ? "yes" : "no", nacts, hurt);
    CHECK(R16(e) == type, "vanished on its own");
    CHECK(moved || nacts > 1, "did nothing");
    CHECK(nacts > 2 || hurt > 0, "no attack");
    if (type == 0xC) {                                    /* its arm (attack box kind 2) must hurt Sho */
        int lost = 0;
        for (int i = 0; i < 400 && R16(e) == type; i++) {
            place_sho(e, 0x00640000, 0x008C0000); PW32(P_VELY, 0);    /* 100,140 px: in front of its muzzle */
            AND16(PLAYER + 4, 0xFBEF); W16(R32(0x060EA41Cu), 0);   /* no post-hit invulnerability */
            int16_t hp = (int16_t)P16(P_HP);
            tick(0);
            if ((int16_t)P16(P_HP) < hp) lost++;
            PW16(P_HP, 16); AND16(PLAYER + 2, 0xFFF7);
        }
        printf("   pinned under its arm: Sho hit on %d frames\n", lost);
        CHECK(lost > 0, "arm never hurt Sho");
    }
    memset(acts, 0, sizeof acts);
    memset(g_proj, 0, sizeof g_proj);
    int sc = RS32(SCORE);
    if (type == 0xC) { g_dx = 0x00300000; g_pin = 0x00600000; }   /* it hangs from the ceiling: slash its arm 48,96 px off */
    int f = fight(e, type, shuriken, maxf, acts, &hurt, 1);
    g_floor = 0; g_dx = 0; g_pin = 0;
    show_acts("fight", acts);
    printf("   projectile types seen:"); for (int k = 0; k < 0x40; k++) if (g_proj[k]) printf(" %X(%d)", k, g_proj[k]); printf("\n");
    printf("   killed after %d frames, score +%d, Sho lost %d HP\n", f, RS32(SCORE) - sc, hurt);
    CHECK(f >= 0, "not killed");
    CHECK(RS32(SCORE) > sc, "no score");
    if (type == 0xB) CHECK(g_proj[9] > 0, "no feathers (projectile 9)");
}

/* 9-1 sled launcher: Sho walks right past it; a sled (sub 0) must appear ahead, fly
 * left, blow up on him (projectile 0x23, Sho hurt) and be freed */
static void test_sled(int sub)
{
    if (g_only >= 0 && g_only != 0x19) return;
    printf("type 19 (launcher sub %d) on stage index 16\n", sub);
    if (!load_stage(16)) { CHECK(0, "stage 16 not loaded"); return; }
    { int n[16] = {0}; for (int k = 0; k < OBJ_SLOTS; k++) if (R16(slot(k)) == 0x19) n[R16(slot(k) + 0x12) & 0xF]++;
      printf("   placed:"); for (int i = 0; i < 16; i++) if (n[i]) printf(" sub %X x%d", i, n[i]); printf("\n"); }
    uint32_t l = find(0x19, sub, 0);
    CHECK(l != 0, "no launcher sub %d", sub);
    if (!l) return;
    for (int k = 0; k < OBJ_SLOTS; k++)
        if (R16(slot(k)) < 0x1000 && R16(slot(k)) != 0x19) W16(slot(k), 0xFFFF);
    printf("   launcher at %d,%d\n", RS32(l + 8) >> 16, RS32(l + 0xC) >> 16);
    place_sho(l, sub == 3 ? -0x00C80000 : -0x00100000, 0);
    memset(g_proj, 0, sizeof g_proj);
    uint32_t sled = 0; int launched = -1, moved = 0, blown = -1, gone = -1, lost = 0, rode = 0;
    int32_t sx0 = 0;
    for (int i = 0; i < 1200; i++) {
        uint16_t held = 0;
        if (i < 40) held = PAD_RIGHT;                               /* walk past it */
        if (sub == 3 && i < 70) held = PAD_RIGHT;
        int16_t hp = (int16_t)P16(P_HP);
        tick(held); scan_proj();
        if ((int16_t)P16(P_HP) < hp) lost += hp - (int16_t)P16(P_HP);
        if ((int16_t)P16(P_HP) < 6) PW16(P_HP, 16);
        if (!sled) {
            for (int k = 0; k < OBJ_SLOTS; k++)
                if (R16(slot(k)) == 0x19 && (R16(slot(k) + 0x12) & 0xF) == 0) { sled = slot(k); launched = i; sx0 = RS32(sled + 8); break; }
            continue;
        }
        if (R16(sled) == 0x19 && (R16(sled + 0x12) & 0xF) == 0 && RS32(sled + 8) < sx0) moved = 1;
        if (R16(sled) == 0x19 && (R16(sled + 0x10) & 0x2000)) rode++;
        if (blown < 0 && R16(sled) == 0x19 && (R16(sled + 0x12) & 0xF) == 0xC) blown = i;
        if (blown >= 0 && gone < 0 && R16(sled) != 0x19) gone = i;
        if (getenv("DBG2") && i % 10 == 0) printf("i%d sled sub %X x %d y %d sho %d,%d hp %d\n", i, R16(sled+0x12)&0xF, RS32(sled+8)>>16, RS32(sled+0xC)>>16, P32(P_X)>>16, P32(P_Y)>>16, (int16_t)P16(P_HP));
        if (gone >= 0 && i > gone + 60) break;
    }
    printf("   sled launched at frame %d (x %d), flew left %s, blew up at %d, gone at %d; explosions %d, Sho lost %d HP\n",
           launched, sx0 >> 16, moved ? "yes" : "no", blown, gone, g_proj[0x23], lost);
    CHECK(launched >= 0, "no sled launched");
    CHECK(moved, "sled did not move");
    CHECK(blown >= 0, "sled never blew up");
    CHECK(g_proj[0x23] > 0, "no explosion (projectile 0x23)");
    CHECK(lost > 0, "Sho not hurt");
    CHECK(gone >= 0, "sled not freed");
}

int main(int argc, char **argv)
{
    g_dir = argc > 1 ? argv[1] : "../extracted/";
    if (argc > 2) g_only = (int)strtol(argv[2], NULL, 16);
    test_type(0x0A, 14, -1, -0x00500000, 0, 0, 4000);     /* 8-1 stone giant */
    test_type(0x0B, 14, -1, -0x00600000, -0x00400000, 0, 4000);   /* 8-1 bird (feathers) */
    for (int k = 0; k < 3; k++) { static const int xs[3] = { 1296, 4420, 8845 }; g_pick_x = xs[k]; if (getenv("K") && atoi(getenv("K")) != k) continue;
        test_type(0x0C, 10, -1, -0x00300000, 0x00780000, 0, 4000); }   /* 6-1 ceiling golem (three of them) */
    g_pick_x = -1;
    test_sled(1);                                         /* 9-1 sled launchers */
    printf(fails ? "%d FAILURES\n" : "all enemy-more2 tests OK\n", fails);
    return fails != 0;
}
