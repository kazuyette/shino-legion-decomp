/* Enemy types ported by agent "enemies" (enemy_t02.c, ...) on the real stage data:
 * for each type, load the stage it lives on, put Sho next to one, let it act for a few
 * seconds (it must move or attack), then fight it with the sword (shurikens for
 * flying ones) until it dies (score must go up).
 *   gcc -std=c11 -Isrc -Itests tests/test_enemy_more.c $(cat tests/SOURCES.txt) -lm
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
#define CHECK(c, ...) do { if (!(c)) { printf("   FAIL: " __VA_ARGS__); printf("\n"); fails++; } } while (0)

static void tick(uint16_t held)
{
    fake[0].connected = 1; fake[0].raw = (uint16_t)~held;
    W16(0x060143EAu, R16(0x060143EAu) + 1u); W16(0x060F4CCCu, R16(0x060F4CCCu) + 1u);
    Pad_Update(2); Scroll_Update(); Game_LatchPad1(P16(P_FLAGS4)); Player_Tick();
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
        int32_t dx = RS32(e + 8) - P32(P_X), dy = RS32(e + 0xC) - P32(P_Y);
        uint16_t held = 0;
        if (keep_close && (dx > 0x00C00000 || dx < -0x00C00000 || dy > 0x00600000 || dy < -0x00600000))
            place_sho(e, dx > 0 ? -0x00300000 : 0x00300000, 0), dx = RS32(e + 8) - P32(P_X);
        if (shuriken) {
            if (dx > 0 && (P16(2) & 0x8000)) held = PAD_RIGHT;
            else if (dx < 0 && !(P16(2) & 0x8000)) held = PAD_LEFT;
            else held = (i & 8) ? PAD_A : 0;
        } else {
            if (dx > 0x280000) held = PAD_RIGHT; else if (dx < -0x280000) held = PAD_LEFT;
            else held = (i & 8) ? PAD_B : 0;
        }
        int16_t hp = (int16_t)P16(P_HP);
        tick(held); scan_proj();
        if ((int16_t)P16(P_HP) < hp && (int16_t)P16(P_HP) > -8) h += hp - (int16_t)P16(P_HP);
        if (R16(e) != type || RS32(SCORE) != sc0) { *hurt = h; return i; }   /* gone, or respawned */
        acts[R16(e + 6) & 0x3F]++;
        if (getenv("DBG2")) printf("f%d act %X anim %04X vx %X vy %X x %d y %d sho %d,%d p48 %d t %d\n", i, R16(e+6), R16(e+0x58), RS32(e+0x30), RS32(e+0x34), RS32(e+8)>>16, RS32(e+0xC)>>16, P32(P_X)>>16, P32(P_Y)>>16, RS16(e+0x48), RS16(e+0x14));
        if (getenv("DBG") && i % 7 == 0) { uint32_t hb = R32(e + 0x68); printf("f%d act %X fl2 %04X dx %d dy %d sho act %d boxes:", i, R16(e+6), R16(e+2), dx>>16, dy>>16, P16(6)); for (int k = 0; hb && k < 6 && R16(hb + 10u*k) != 0xFFFF; k++) printf(" [%d %d %d %d %d]", RS16(hb+10u*k), RS16(hb+10u*k+2), RS16(hb+10u*k+4), RS16(hb+10u*k+6), RS16(hb+10u*k+8)); printf("\n"); }
        if ((int16_t)P16(P_HP) < 6) PW16(P_HP, 16);                    /* keep Sho alive */
    }
    *hurt = h;
    return -1;
}
/* generic test: idle 4 s next to it (must move/attack), then fight to the death */
static void test_type(uint16_t type, int stage, int sub, int32_t dx, int shuriken, int maxf)
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
    printf("   slot %d chars %04X colour bank %d (%s), at %.0f,%.0f HP field %d\n", s, cb, bank,
           pal ? "palette ok" : "EMPTY", RS32(e + 8) / 65536.0, RS32(e + 0xC) / 65536.0, RS16(e + 0x16));
    CHECK(pal && R16(CHAR_TABLE + cb * 10u) != 0, "graphics not loaded");
    for (int k = 0; k < OBJ_SLOTS; k++)             /* alone with it */
        if (R16(slot(k)) < 0x1000 && R16(slot(k)) != type) W16(slot(k), 0xFFFF);
    place_sho(e, dx, 0);
    int acts[0x40] = {0}, hurt = 0, moved = 0;
    memset(g_proj, 0, sizeof g_proj);
    int32_t x0 = RS32(e + 8), y0 = RS32(e + 0xC);
    int16_t hp0 = (int16_t)P16(P_HP);
    for (int i = 0; i < g_idle && R16(e) == type; i++) {
        PW16(P_HP, 16); tick(0); scan_proj(); if ((int16_t)P16(P_HP) < 16) hurt++;
        if (getenv("DBG2") && R16(e) == type) printf("i%d act %X anim %04X vx %X vy %X x %d y %d sho %d,%d p48 %d\n", i, R16(e+6), R16(e+0x58), RS32(e+0x30), RS32(e+0x34), RS32(e+8)>>16, RS32(e+0xC)>>16, P32(P_X)>>16, P32(P_Y)>>16, RS16(e+0x48));
        if (R16(e) == type) { acts[R16(e + 6) & 0x3F]++; if (RS32(e + 8) != x0 || RS32(e + 0xC) != y0) moved = 1; }
    }
    int nacts = 0; for (int i = 0; i < 0x40; i++) nacts += acts[i] != 0;
    show_acts("idle", acts);
    printf("   projectile types seen:"); for (int k = 0; k < 0x40; k++) if (g_proj[k]) printf(" %X(%d)", k, g_proj[k]); printf("\n");
    { int n = 0; for (int k = 0; k < OBJ_SLOTS; k++) n += R16(slot(k)) == type; printf("   %d objects of type %02X now\n", n, type); }
    printf("   moved %s, %d actions, hit Sho %d times\n", moved ? "yes" : "no", nacts, hurt);
    (void)hp0;
    CHECK(R16(e) == type, "vanished on its own");
    CHECK(moved || nacts > 1, "did nothing");
    memset(acts, 0, sizeof acts);
    int sc = RS32(SCORE);
    int f = fight(e, type, shuriken, maxf, acts, &hurt, 1);
    show_acts("fight", acts);
    printf("   projectile types seen:"); for (int k = 0; k < 0x40; k++) if (g_proj[k]) printf(" %X(%d)", k, g_proj[k]); printf("\n");
    printf("   killed after %d frames, score +%d, Sho lost %d HP\n", f, RS32(SCORE) - sc, hurt);
    CHECK(f >= 0, "not killed");
    CHECK(RS32(SCORE) > sc, "no score");
}

/* boss: keep Sho (immortal) beside it, slashing; the vertical offset cycles until the
 * hits land. Passes when the stage-clear state (0x0602529E & 7 == 2) is posted or the
 * boss is gone with its HP <= 0. */
static void test_boss(uint16_t type, int stage, int maxf)
{
    if (g_only >= 0 && g_only != type) return;
    printf("boss type %02X on stage index %d\n", type, stage);
    if (!load_stage(stage)) { CHECK(0, "stage %d not loaded", stage); return; }
    uint32_t e = find(type, 0, 0);
    CHECK(e != 0, "no boss %02X placed", type);
    if (!e) return;
    int acts[0x40] = {0}, hits = 0, cleared = -1, f;
    static const int32_t dys[6] = { 0, -0x200000, 0x200000, -0x400000, 0x400000, -0x600000 };
    memset(g_proj, 0, sizeof g_proj);
    int16_t hp = -1;
    int sc = RS32(SCORE);
    for (f = 0; f < maxf; f++) {
        if (R16(e) == type) {
            int32_t dx = (RS32(e + 8) < P32(P_X)) ? 0x00300000 : -0x00300000;
            if ((f / 300) & 1) dx = (R16(e + 2) & 0x8000) ? 0x00300000 : -0x00300000;   /* behind it */
            if (f > 600) place_sho(e, dx, dys[(f / 150) % 6]);
            else PW16(P_HP, 16);
            PW32(P_VELY, 0);
            if (hp >= 0 && RS16(e + 0x16) < hp) hits++;
            hp = RS16(e + 0x16);
            acts[R16(e + 6) & 0x3F]++;
            if (getenv("DBG3") && f % 50 == 0) printf("f%d act %X fl2 %04X +4 %04X inv %d hp %d x %d y %d sho %d,%d anim %04X\n", f, R16(e+6), R16(e+2), R16(e+4), RS16(e+0x48), RS16(e+0x16), RS32(e+8)>>16, RS32(e+0xC)>>16, P32(P_X)>>16, P32(P_Y)>>16, R16(e+0x58));
            if (getenv("DBG3") && f % 50 == 0) { uint32_t hb = R32(e + 0x68), sb = R32(PLAYER + 0x68); printf("   boss:"); for (int k = 0; hb && k < 8 && R16(hb + 10u*k) != 0xFFFF; k++) printf(" [%d %d %d %d %d]", RS16(hb+10u*k), RS16(hb+10u*k+2), RS16(hb+10u*k+4), RS16(hb+10u*k+6), RS16(hb+10u*k+8)); printf("  sho act %X:", P16(6)); for (int k = 0; sb && k < 8 && R16(sb + 10u*k) != 0xFFFF; k++) printf(" [%d %d %d %d %d]", RS16(sb+10u*k), RS16(sb+10u*k+2), RS16(sb+10u*k+4), RS16(sb+10u*k+6), RS16(sb+10u*k+8)); printf("\n"); }
        }
        uint16_t held = (f > 600) ? (uint16_t)((f & 8) ? PAD_B : 0) : 0;
        if (f > 600) {                                      /* face the boss */
            if (RS32(e + 8) > P32(P_X) && (P16(2) & 0x8000)) held |= PAD_RIGHT;
            if (RS32(e + 8) < P32(P_X) && !(P16(2) & 0x8000)) held |= PAD_LEFT;
        }
        tick(held); scan_proj();
        if (f == 1 && type == 0x15) { W16(0x060F688Cu, 1); W16(0x060F688Eu, 60); }   /* 2-2: heads destroyed (0x0604FAC0) */
        if ((R16(0x0602529Eu) & 7) == 2 && cleared < 0) { cleared = f; break; }
    }
    show_acts("boss", acts);
    printf("   projectile types seen:"); for (int k = 0; k < 0x40; k++) if (g_proj[k]) printf(" %X(%d)", k, g_proj[k]); printf("\n");
    printf("   %d hits, HP now %d, stage clear at frame %d, score +%d\n", hits, hp, cleared, RS32(SCORE) - sc);
    int nacts = 0; for (int i = 0; i < 0x40; i++) nacts += acts[i] != 0;
    CHECK(nacts > 2, "boss did nothing");
    CHECK(hits > 0, "boss never hurt");
    CHECK(cleared >= 0, "stage not cleared");
}

int main(int argc, char **argv)
{
    g_dir = argc > 1 ? argv[1] : "../extracted/";
    if (argc > 2) g_only = (int)strtol(argv[2], NULL, 16);
    test_type(0x02, 0, 0, 0x00A00000, 0, 3000);           /* 1-1 jumping ninja */
    test_type(0x02, 0, 1, -0x00A00000, 0, 3000);          /* 1-1, respawning (drops in) */
    test_type(0x0F, 0, 0, -0x00A00000, 0, 3000);          /* 1-1 ninja (type F) */
    test_type(0x0F, 0, 1, -0x00A00000, 0, 3000);
    test_type(0x01, 0, 5, -0x00A00000, 0, 3000);          /* 1-1 soldier leaping out of the water */
    test_type(0x01, 0, 6, -0x00A00000, 0, 3000);          /* 1-1 2-HP rifleman */
    test_type(0x01, 2, 9, -0x00A00000, 0, 3000);          /* 2-1 soldier jumping up a floor */
    test_type(0x23, 2, 0, 0, 0, 3000);                    /* 2-1 ninja pair from the screen edges */
    test_type(0x23, 4, 0, 0, 0, 3000);                    /* 3-1 */
    test_type(0x05, 4, 1, -0x00A00000, 0, 3000);          /* 3-1 agile ninja */
    test_type(0x07, 6, -1, -0x00600000, 0, 4000);         /* 4-1 armoured giant (shock wave) */
    test_type(0x16, 4, 0, -0x00A00000, 0, 4000);          /* 3-1 bomber (drops in, respawns) */
    test_type(0x16, 4, 1, -0x00A00000, 0, 4000);
    g_pick_y = 3440; g_idle = 900;
    test_type(0x22, 2, 0, -0x00600000, 0, 4000);          /* 2-1 fire-breather + orbit */
    g_pick_y = -1; g_idle = 240;
    g_pick_x = 5788;
    test_type(0x24, 14, 0, -0x00A00000, 0, 3000);         /* 8-1 grenadier */
    g_pick_x = -1;
    test_boss(0x18, 1, 12000);                            /* 1-2 Hannya mask */
    test_boss(0x15, 3, 12000);                            /* 2-2 masked ninja */
    printf(fails ? "%d FAILURES\n" : "all enemy-more tests OK\n", fails);
    return fails != 0;
}
