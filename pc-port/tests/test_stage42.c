/* Stage 4-2 (index 7, file STAGE4_2) on the real data: the ledge objects 0x1025-0x1028
 * (objhandlers_d.c) and the boss, enemy type 0x20 (enemy_boss42.c).
 *  1. Sho lands on a 0x1025 ledge (the stage has no map collision) and walks on it.
 *  2. The boss's intro ends: the ledges start sliding down and wrap around.
 *  3. The boss teleports / casts, takes sword hits, splits in two, both bodies die,
 *     the death flash posts "stage clear" (0x0602529E bits 0-2 = 2) and the stage ends.
 *   gcc -std=c11 -Isrc -Itests tests/test_stage42.c $(cat tests/SOURCES.txt) -lm
 *   ./a.out ../extracted/ [shots-dir]   (screenshots: after the intro, orb in flight, split) */
#include <stdio.h>
#include <stdlib.h>
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
#include "objhandlers_c.h"
#include "enemy.h"
#include "fx.h"
#include "shinobi.h"
#include "fade.h"
#include "soft_render.h"
extern RenderSprite g_spriteLog[]; extern int g_spriteLogCount;
static InputPort fake[2];
const InputPort *Input_GetPort(int p) { return &fake[p]; }

static void tick(uint16_t held)
{
    fake[0].connected = 1; fake[0].raw = (uint16_t)~held;
    g_spriteLogCount = 0;
    W16(0x060143EAu, R16(0x060143EAu) + 1u); W16(0x060F4CCCu, R16(0x060F4CCCu) + 1u);
    Pad_Update(2);
    Scroll_Update();
    Game_LatchPad1(P16(P_FLAGS4));
    Player_Tick();
    Stage_UpdateState();
    Obj_UpdateEnemies(); Obj_UpdateAll(); Projectiles_Update(); Effects_Update(); Items_Update();
    Fade_Update();                                       /* A.BIN vblank fader (boot.c) */
}
static uint32_t slot(int s) { return OBJ_TABLE + (uint32_t)s * OBJ_SIZE; }
static int count(uint16_t t) { int n = 0; for (int s = 0; s < OBJ_SLOTS; s++) n += R16(slot(s)) == t; return n; }
static uint32_t find(uint16_t t, int sub)
{
    for (int s = 0; s < OBJ_SLOTS; s++)
        if (R16(slot(s)) == t && (sub < 0 || (R16(slot(s) + 0x12) & 0x1F) == sub)) return slot(s);
    return 0;
}
static uint32_t ledge_under_sho(void)
{
    for (int s = 0; s < OBJ_SLOTS; s++) if (R16(slot(s)) == 0x1025 && R16(slot(s) + 0x4E)) return slot(s);
    return 0;
}
static const char *g_shots;
static void shot(const char *name)
{
    static uint32_t fb[SCREEN_W * SCREEN_H];
    char p[512];
    if (!g_shots) return;
    W16(0x0603A570u, R16(0x0603A570u) | 0x8000);
    Soft_ComposeFrame(fb, g_spriteLog, g_spriteLogCount, 0xFF000000u);
    snprintf(p, sizeof p, "%s/%s.png", g_shots, name);
    if (Soft_WritePng(p, fb, SCREEN_W, SCREEN_H)) printf("   screenshot %s\n", p);
}
static int airborne(void) { return (P16(P_FLAGS2) & 0x4000) != 0; }

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "../extracted/";
    g_shots = argc > 2 ? argv[2] : NULL;
    char p[512];
    snprintf(p, sizeof p, "%sA.BIN", dir);           assert(Ram_LoadImage(p, 0x06004000u));
    snprintf(p, sizeof p, "%sSHINOBI.dec.bin", dir); assert(Ram_LoadImage(p, 0x06040000u));
    snprintf(p, sizeof p, "%sSHOU.CHR", dir);        assert(Ram_LoadImage(p, 0x00200000u));
    W16(0x060EC658u, 0); W16(0x060EC65Au, 0); W16(0x060252DCu, 1); W16(0x0602529Eu, 0);
    Shinobi_InitVdp2();
    W32(0x06024058u, 0); W32(0x0602405Cu, 0); W32(0x06024060u, 0x10000); W32(0x06024064u, 0x10000);
    W32(SPR_ZOOM, 0x10000);
    Fade_Init();                                         /* A.BIN boot: default fade speed */
    Player_Init();
    assert(Stage_Load(dir, 7));
    Stage_SpawnPlayer();
    int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); printf("\n"); fails++; } } while (0)
    printf("objects: 0x1025 x%d, 0x1026 x%d, 0x1028 x%d, boss x%d\n", count(0x1025), count(0x1026), count(0x1028), count(0x20));
    CHECK(count(0x1025) == 22 && count(0x1026) == 11 && count(0x1028) == 20 && count(0x20) == 1, "stage objects");

    /* 1. land on a ledge, walk on it */
    int f = 0, landed = -1;
    for (; f < 60 && landed < 0; f++) { tick(0); if (!airborne() && ledge_under_sho()) landed = f; }
    printf("1. Sho landed on a ledge at frame %d: %.1f,%.1f\n", landed, P32(P_X) / 65536.0, P32(P_Y) / 65536.0);
    if (ledge_under_sho()) { uint32_t l = ledge_under_sho(), hb = R32(l + 0x68);
        printf("   ledge at %.1f,%.1f box x %d..%d y %d..%d\n", RS32(l + 8) / 65536.0, RS32(l + 0xC) / 65536.0, RS16(hb + 2), RS16(hb + 6), RS16(hb + 4), RS16(hb + 8)); }
    CHECK(landed >= 0, "Sho did not land on a 0x1025 ledge");
    /* 2. the boss intro ends: the ledges slide down (speed class from the placement flags) */
    uint32_t boss = find(0x20, 0);
    CHECK(boss != 0, "no boss");
    for (; f < 600 && boss && R16(boss + 6) == 0; f++) tick(0);
    printf("2. boss intro over at frame %d (action %d, HP %d)\n", f, boss ? R16(boss + 6) : -1, boss ? RS16(boss + 0x16) : 0);
    CHECK(boss && R16(boss + 6) != 0, "boss intro never ends");
    shot("stage42_intro_end");
    int moving = 0; for (int s = 0; s < OBJ_SLOTS; s++) moving += R16(slot(s)) == 0x1025 && R16(slot(s) + 0x48);
    CHECK(moving == 22, "%d/22 ledges moving", moving);
    /* (Sho's controls are off during the boss intro: walk once it is over) */
    uint32_t l0 = ledge_under_sho();
    int32_t x0 = (int32_t)P32(P_X), ly0 = l0 ? RS32(l0 + 0xC) : 0;
    int air = 0;
    for (int k = 0; k < 14; k++, f++) { tick(PAD_RIGHT); air += airborne(); }
    int32_t x1 = (int32_t)P32(P_X);
    for (int k = 0; k < 24; k++, f++) { tick(PAD_LEFT); air += airborne(); }   /* box: ledge x + 0..96 */
    int32_t x2 = (int32_t)P32(P_X);
    for (int k = 0; k < 10; k++, f++) tick(0);
    printf("   walked right %.1f px, left %.1f px, airborne %d of 38 frames, still on the ledge: %s\n",
           (x1 - x0) / 65536.0, (x1 - x2) / 65536.0, air, ledge_under_sho() == l0 ? "yes" : "no");
    CHECK(x1 - x0 > 0x100000 && x1 - x2 > 0x100000, "Sho did not walk");
    CHECK(air == 0 && ledge_under_sho() == l0 && !airborne(), "Sho left the ledge while walking");
    CHECK(l0 && RS32(l0 + 0xC) > ly0, "the ledge did not carry Sho down");
    CHECK((int32_t)P32(P_Y) - RS32(l0 + 0xC) >= 0 && (int32_t)P32(P_Y) - RS32(l0 + 0xC) < 0x200000, "Sho not on top of the ledge");


    uint32_t lf = 0; for (int s = 0; s < OBJ_SLOTS && !lf; s++) if (R16(slot(s)) == 0x1025 && (R16(slot(s) + 0x12) & 3) == 1) lf = slot(s);
    int32_t y0 = RS32(lf + 0xC);
    for (int k = 0; k < 64; k++, f++) { PW16(P_HP, 16); tick(0); }
    printf("   fast ledge moved %.2f px in 64 frames\n", (RS32(lf + 0xC) - y0) / 65536.0);
    CHECK(RS32(lf + 0xC) - y0 == 64 * 0x6000 || RS32(lf + 0xC) - y0 == 64 * 0x6000 - 0x04000000, "ledge speed");
    /* 2b. a lantern 0x1028 slides into reach: Sho cuts it (base + lid + flame, item drop) */
    uint32_t lan = 0;
    for (int k = 0; k < 3000 && !lan; k++, f++) {
        PW16(P_HP, 16);
        if (airborne() || !ledge_under_sho()) {                 /* keep Sho on screen */
            for (int s = 0; s < OBJ_SLOTS; s++) {
                uint32_t l = slot(s);
                if (R16(l) == 0x1025 && RS32(l + 0xC) > 0x04500000 && RS32(l + 0xC) < 0x04900000) {
                    PW32(P_X, (uint32_t)(RS32(l + 8) + 0x300000)); PW32(P_Y, (uint32_t)(RS32(l + 0xC) + 0x123000)); PW32(P_VELY, 0); break;
                }
            }
        }
        tick(0);
        for (int s = 0; s < OBJ_SLOTS; s++) {
            uint32_t o = slot(s);
            if (R16(o) == 0x1028 && R16(o + 6) == 7 && RS32(o + 0xC) > 0x04300000 && RS32(o + 0xC) < 0x04C00000) { lan = o; break; }
        }
    }
    CHECK(lan != 0, "no lantern came into reach");
    int pieces0 = count(0x1028), cut = -1;
    for (int k = 0; lan && k < 120 && cut < 0; k++, f++) {
        PW16(P_HP, 16);
        PW32(P_X, (uint32_t)(RS32(lan + 8) - 0x180000));
        PW32(P_Y, (uint32_t)(RS32(lan + 0xC)));
        PW16(P_FLAGS2, P16(P_FLAGS2) & 0x7FFF);
        tick((k & 8) ? PAD_B : 0);
        if (R16(lan + 6) == 8) cut = k;
    }
    printf("2b. lantern cut after %d frames: %d -> %d lantern objects (base, lid, flame)\n", cut, pieces0, count(0x1028));
    CHECK(cut >= 0 && count(0x1028) == pieces0 + 2, "lantern not cut into pieces");

    /* 3. fight: Sho is kept next to a body of the boss (and alive), slashing */
    int seen[0x20] = {0}, maxBodies = 0, hits = 0, orbs = 0, bolts = 0, flash = 0, cleared = -1;
    int16_t lastHp[2] = {RS16(boss + 0x16), 0};
    for (; f < 12000; f++) {
        int bodies = 0;
        for (int s = 0; s < OBJ_SLOTS; s++) {
            uint32_t o = slot(s);
            if (R16(o) != 0x20) continue;
            int sub = R16(o + 0x12) & 0x1F;
            if (sub == 2) { flash = 1; continue; }
            bodies++;
            int a = R16(o + 6);
            seen[a & 0x1F]++;
            if (RS16(o + 0x16) < lastHp[sub == 1]) hits++;
            lastHp[sub == 1] = RS16(o + 0x16);
        }
        for (int k = 0; k < 64; k++) {
            uint32_t pr = PROJECTILES + (uint32_t)k * 0x40;
            if (R16(pr) == 0x20 && R16(pr + 6) == 1 && !(R16(pr + 0x10) & 2)) orbs++;   /* first frame */
            if (R16(pr) == 0x21 && R16(pr + 6) == 0x27 && !R16(pr + 0x10)) bolts++;
        }
        static int shotOrb, shotSplit;
        if (!shotOrb) for (int k = 0; k < 64; k++) {
            uint32_t pr = PROJECTILES + (uint32_t)k * 0x40;
            if (R16(pr) == 0x20 && (R16(pr + 0x10) & 2) && !(R16(pr + 0x10) & 0xC) && R16(pr + 6) == 5) { shot("stage42_orb"); shotOrb = 1; break; }
        }
        if (bodies == 2 && !shotSplit && f % 64 == 0) { shot("stage42_split"); shotSplit = 1; }
        if (bodies > maxBodies) maxBodies = bodies;
        if ((R16(0x0602529Eu) & 7) == 2 && cleared < 0) cleared = f;
        if (RS16(G_STAGE_INDEX) != 7) break;
        PW16(P_HP, 16);
        uint16_t held = 0;
        /* stay on a ledge in the middle of the screen; slash the boss's orbs back at it */
        if (airborne() || !ledge_under_sho() || (int32_t)P32(P_Y) > 0x04000000 + 0xB00000) {
            uint32_t best = 0;
            for (int s = 0; s < OBJ_SLOTS; s++) {
                uint32_t l = slot(s);
                if (R16(l) == 0x1025 && RS32(l + 0xC) > 0x04500000 && RS32(l + 0xC) < 0x04900000 && (!best || RS32(l + 0xC) < RS32(best + 0xC))) best = l;
            }
            if (best) { PW32(P_X, (uint32_t)(RS32(best + 8) + 0x300000)); PW32(P_Y, (uint32_t)(RS32(best + 0xC) + 0x123000)); PW32(P_VELY, 0); }
        }
        for (int k = 0; k < 64; k++) {
            uint32_t pr = PROJECTILES + (uint32_t)k * 0x40;
            if (R16(pr) != 0x20 || !(R16(pr + 0x10) & 2) || (R16(pr + 0x10) & 0xD)) continue;
            int32_t dx = RS32(pr + 8) - (int32_t)P32(P_X), dy = RS32(pr + 0xC) - ((int32_t)P32(P_Y) - 0x200000);
            if (dx > -0x500000 && dx < 0x500000 && dy > -0x500000 && dy < 0x500000) {
                if (dx < 0) PW16(P_FLAGS2, P16(P_FLAGS2) | 0x8000); else PW16(P_FLAGS2, P16(P_FLAGS2) & 0x7FFF);
                held = (f & 4) ? PAD_B : 0;
            }
        }
        tick(held);
    }
    printf("3. fight: %d frames, boss actions seen:", f);
    for (int a = 0; a < 0x20; a++) if (seen[a]) printf(" %X(%d)", a, seen[a]);
    printf("\n   bodies max %d, HP drops %d, orbs %d, lightning %d, death flash %s, stage clear posted at frame %d, stage index now %d (game mode %d)\n",
           maxBodies, hits, orbs, bolts, flash ? "yes" : "no", cleared, RS16(G_STAGE_INDEX), RS16(G_GAME_MODE));
    CHECK(seen[5] && seen[4], "boss never cast / teleported");
    CHECK(hits >= 10, "boss took only %d hits", hits);
    CHECK(maxBodies == 2, "the boss never split in two");
    CHECK(orbs > 0, "no orb cast");
    CHECK(bolts > 0, "no lightning");
    CHECK(count(0x20) == 0 || RS16(G_STAGE_INDEX) != 7, "boss still alive");
    CHECK(flash, "no death flash");
    CHECK(cleared >= 0, "stage clear not posted");
    CHECK(RS16(G_STAGE_INDEX) == 8 && RS16(G_GAME_MODE) == 7, "stage did not end with the tally (idx %d mode %d)", RS16(G_STAGE_INDEX), RS16(G_GAME_MODE));
    if (fails) { printf("%d stage 4-2 check(s) failed\n", fails); return 1; }
    printf("all stage 4-2 tests OK\n");
    return 0;
}
