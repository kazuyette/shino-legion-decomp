/* Stage objects 0x1020-0x1050 (objhandlers_f*.c) and effects 0xA-0xD (fx_b.c) on the real
 * stages: idx 1 (1-2) bell / sign / clouds / petals, idx 4 (3-1) lanterns / wall blocks / eye,
 * idx 6 (4-1) lantern / ropes / spears / doors, idx 8-9 (5-1, 5-2) cart / elevator / crate,
 * idx 10-11 (6-1, 6-2) decoration / crystal / collapsing wall, idx 14 (8-1) post / rocks,
 * idx 16 (9-1) urn / sinking platform; effects 0xA-0xD (looping sparkles: they stay, and do
 * not advance off screen).
 *   gcc -std=c11 -Isrc -Itests tests/test_objects_f.c $(cat tests/SOURCES.txt) -lm
 *   ./a.out ../extracted/ */
#include <stdio.h>
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
#include "objhandlers.h"
#include "objhandlers_c.h"
#include "objhandlers_f.h"
#include "enemy.h"
#include "fx.h"
#include "shinobi.h"
#include "collision.h"
extern int g_spriteCount;
static InputPort fake[2];
const InputPort *Input_GetPort(int p) { return &fake[p]; }

static void tick(uint16_t held)
{
    fake[0].connected = 1; fake[0].raw = (uint16_t)~held;
    W16(0x060143EAu, R16(0x060143EAu) + 1u); W16(0x060F4CCCu, R16(0x060F4CCCu) + 1u);
    PW16(P_HP, 16);
    Pad_Update(2); Scroll_Update(); Game_LatchPad1(P16(P_FLAGS4)); Player_Tick();
    PW16(P_HP, 16);
    Obj_UpdateEnemies(); Obj_UpdateAll(); Projectiles_Update(); Effects_Update(); Items_Update();
}
static uint32_t slot(int s) { return OBJ_TABLE + (uint32_t)s * OBJ_SIZE; }
static double px(int32_t v) { return v / 65536.0; }
static uint32_t find(uint16_t t, int n)
{
    for (int s = 0; s < OBJ_SLOTS; s++) if (R16(slot(s)) == t && n-- == 0) return slot(s);
    return 0;
}
static int count(uint16_t t) { int n = 0; for (int s = 0; s < OBJ_SLOTS; s++) n += R16(slot(s)) == t; return n; }
static int fx_count(uint16_t t) { int n = 0; for (int i = 0; i < 64; i++) n += R16(EFFECTS + (uint32_t)i * 0x30) == t; return n; }
static int items(void) { int n = 0; for (int i = 0; i < 64; i++) n += R16(ITEMS + (uint32_t)i * 0x30) != 0xFFFF; return n; }
static void place_sho(int32_t x, int32_t y)
{
    PW32(P_X, (uint32_t)x); PW32(P_Y, (uint32_t)y); PW32(0x2C, (uint32_t)y); PW32(P_VELY, 0);
    PW32(P_VELX, 0); PW32(P_PUSHX, 0); PW32(0x3C, 0); PW32(0x40, 0); PW32(0x44, 0);
    W32(CAM_X, (uint32_t)(P32(P_X) - RS32(0x060EA550u)));
    W32(CAM_Y, (uint32_t)(P32(P_Y) - RS32(0x060EA554u)));
    Scroll_Update();
}
static const char *g_dir;
static void load(int idx)
{
    W16(G_CHECKPOINT, 0); W16(G_RESPAWN, 0); W16(0x060252DCu, 1); W16(G_STAGE_STATE, 0);
    Shinobi_InitVdp2();
    W32(SPR_ZOOM, 0x10000);
    Player_Init();
    assert(Stage_Load(g_dir, idx));
    Stage_SpawnPlayer();
    W16(G_GAME_MODE, 6);
    for (int i = 0; i < 20; i++) tick(0);
    printf("--- stage idx %d: Sho at %.0f,%.0f (action %d), %d objects\n", idx, px(P32(P_X)), px(P32(P_Y)), P16(P_ACTION), Obj_Count());
}
static int run_handler(uint32_t o)
{
    int n = g_spriteCount;
    W32(OBJ_CUR, o);
    ObjHandler_Call(R16(o));
    return g_spriteCount - n;
}
/* Sho next to o at height y, slashing (ground, then jump slashes) until done(o). */
static int slash(uint32_t o, int32_t y, int (*done)(uint32_t))
{
    static const int32_t dists[] = { 0x200000, 0x280000, 0x180000, 0x300000, 0x100000 };
    for (int a = 0; a < 10; a++) {
        int side = (a & 1) ? 1 : -1;
        int32_t d = dists[(a >> 1) % 5];
        place_sho(RS32(o + 8) + side * d, y);
        for (int i = 0; i < 8; i++) tick(0);
        tick(side < 0 ? PAD_RIGHT : PAD_LEFT);
        for (int i = 0; i < 60; i++) {
            uint16_t k = a < 6 ? ((i % 20) < 4 ? PAD_B : 0) : ((i % 30) < 6 ? PAD_C : ((i % 30) >= 8 && (i % 30) < 12) ? PAD_B : 0);
            tick(k);
            if (done(o)) {
                printf("   slash hit (attempt %d, Sho %.0f,%.0f, object %.0f,%.0f)\n", a, px(P32(P_X)), px(P32(P_Y)),
                       px(RS32(o + 8)), px(RS32(o + 0xC)));
                return 1;
            }
        }
    }
    return 0;
}
/* Sho's sword box (kind 2) forced over o (synthetic hitbox list), then o's handler runs once. */
static void sword_on(uint32_t o)
{
    const uint32_t L = 0x060FFF00u;
    W16(L, 2); W16(L + 2, (uint16_t)-16); W16(L + 4, (uint16_t)-16); W16(L + 6, 16); W16(L + 8, 16); W16(L + 10, 0xFFFF);
    PW32(P_X, R32(o + 8)); PW32(P_Y, R32(o + 0xC));
    W32(PLAYER + 0x68, L);
    run_handler(o);
}
static int hit8(uint32_t o) { return (R16(o + 2) & 8) != 0; }
static int gone(uint32_t o) { return R16(o) == 0xFFFF; }
static int st48(uint32_t o) { return R16(o + 0x48) != 0; }
static int anim6(uint32_t o) { return R16(o + 6) == 6 || R16(o) != 0x1021; }

/* ---------------------------------------------------------------- 1-2 */
static void stage1(void)
{
    load(1);
    assert(count(0x1031) == 1 && count(0x1032) == 3 && count(0x1033) == 2);
    uint32_t bell = find(0x1031, 0), gen = find(0x1034, 0);
    assert(bell && gen);
    for (int i = 0; i < 960; i++) tick(0);
    printf("0x1031 bell: phase %d angle %d; petals %d\n", RS16(bell + 0x4A), RS16(bell + 0x48), count(0x1034) - 1);
    assert(RS16(bell + 0x4A) >= 1 && RS16(bell + 0x48) < 0x18);
    assert(count(0x1034) > 1);
    uint32_t p = 0;
    for (int k = 0; (p = find(0x1034, k)); k++) if (R16(p + 0x12) & 1) break;
    assert(p && RS32(p + 0x30) < 0);                         /* petals drift left */
    uint32_t c = find(0x1033, 0);
    int32_t cx = RS32(c + 8);
    tick(0);
    assert(RS32(c + 8) < cx || RS32(c + 8) == 0x06800000);
    for (int k = 0; k < 3; k++) {
        uint32_t q = find(0x1032, k), hb = R32(q + 0x68);
        printf("0x1032 #%d at %.0f,%.0f flags %04X hb %08X: %d %d %d %d %d\n", k, px(RS32(q + 8)), px(RS32(q + 0xC)), R16(q + 0x12), hb,
               RS16(hb), RS16(hb + 2), RS16(hb + 4), RS16(hb + 6), RS16(hb + 8));
    }
    printf("Sho %.0f,%.0f\n", px(P32(P_X)), px(P32(P_Y)));
    uint32_t sign = find(0x1032, 0);
    int32_t sy = RS32(sign + 0xC);
    assert(slash(sign, RS32(sign + 0xC), hit8));
    for (int i = 0; i < 40; i++) tick(0);
    printf("0x1032 sign cut: state %d, dropped %.0f px, tilt %d\n", RS16(sign + 0x4E), px(RS32(sign + 0xC) - sy), RS16(sign + 0x4C));
    assert(RS16(sign + 0x4E) >= 2 && RS32(sign + 0xC) - sy > 0x100000);
}

/* ---------------------------------------------------------------- 3-1 */
static void stage4(void)
{
    load(4);
    int n21 = count(0x1021);
    printf("3-1: 0x1021 x%d, 0x1022 x%d, 0x1023 x%d\n", n21, count(0x1022), count(0x1023));
    assert(n21 > 45 && count(0x1022) == 1 && count(0x1023) == 8);
    uint32_t g = 0;
    for (int k = 0; (g = find(0x1021, k)); k++) if (R16(g + 6) == 7 && (R16(g + 0x12) & 0x18) != 0x18) break;
    assert(g);
    uint32_t lid = slot(RS16(g + 0x48));
    printf("0x1021 glass at %.0f,%.0f (lid slot type %04X anim %d)\n", px(RS32(g + 8)), px(RS32(g + 0xC)), R16(lid), R16(lid + 6));
    (void)0;
    {
        uint32_t hb = R32(g + 0x68);
        printf("   glass hb %d %d %d %d, parent type %04X anim %d at y %.0f\n", RS16(hb + 2), RS16(hb + 4), RS16(hb + 6), RS16(hb + 8),
               R16(slot(RS16(g + 0x4E))), R16(slot(RS16(g + 0x4E)) + 6), px(RS32(slot(RS16(g + 0x4E)) + 0xC)));
    }
    assert(slash(g, P32(P_Y), anim6));
    for (int i = 0; i < 60 && R16(g) == 0x1021; i++) tick(0);
    int sh = count(0x1024);
    printf("0x1021 glass broken: %d shards, lid +0x4A %d\n", sh, R16(lid + 0x4A));
    assert(R16(g) != 0x1021 && sh >= 10 && R16(lid + 0x4A) == 1);
    for (int i = 0; i < 80; i++) tick(0);
    assert(count(0x1024) < sh);                             /* shards expire */
    /* sliding blocks */
    uint32_t b = 0;
    for (int k = 0; (b = find(0x1023, k)); k++) if (R16(b + 6) == 0x11 && RS16(b + 0x4C) == 0) break;
    assert(b);
    int32_t bx = RS32(b + 8);
    place_sho(bx - 0x400000, RS32(b + 0xC) - 0x100000);
    for (int i = 0; i < 60; i++) tick(0);
    printf("0x1023 block: phase %d, moved %.0f px\n", RS16(b + 0x4C), px(RS32(b + 8) - bx));
    assert(RS16(b + 0x4C) >= 1 && RS32(b + 8) < bx);
}

/* ---------------------------------------------------------------- 4-1 */
static void stage6(void)
{
    load(6);
    assert(count(0x1029) == 20 && count(0x102A) == 17 && count(0x102B) == 17 && count(0x102E) == 17);
    uint32_t l = 0;
    for (int k = 0; (l = find(0x1029, k)); k++) if (RS16(l + 0x18) != -1) break;
    assert(l);
    int it = items();
    {
        uint32_t hb = R32(l + 0x68);
        printf("   lantern at %.0f,%.0f hb %d %d %d %d\n", px(RS32(l + 8)), px(RS32(l + 0xC)), RS16(hb + 2), RS16(hb + 4), RS16(hb + 6), RS16(hb + 8));
    }
    assert(slash(l, RS32(l + 0xC) - 0x20000, st48));
    for (int i = 0; i < 30; i++) tick(0);
    printf("0x1029 lantern cut: state %d/%d, items %d -> %d\n", RS16(l + 0x48), RS16(l + 0x50), it, items());
    assert(RS16(l + 0x50) >= 1 && items() > it);
    /* rope: lowers to the map, sways, rewinds */
    uint32_t r = 0;
    for (int k = 0; (r = find(0x102B, k)); k++) if (RS16(r + 0x48) == 0) break;
    assert(r);
    place_sho(RS32(r + 8) - 0x800000, RS32(r + 0xC) + 0x800000);
    int seen[7] = { 0 };
    for (int i = 0; i < 400; i++) { tick(0); seen[RS16(r + 0x48) % 7] = 1; }
    printf("0x102B rope: states seen %d%d%d%d%d, length %d\n", seen[0], seen[1], seen[2], seen[3], seen[4], RS16(r + 0x4A));
    assert(seen[1] && seen[3]);
    /* cut a rope: falling segments */
    W16(r + 0x48, 4); W16(r + 0x4C, 200);
    W16(r + 0x4A, 0x60);
    assert(slash(r, RS32(r + 0xC) + 0x500000, hit8));
    for (int i = 0; i < 2; i++) tick(0);
    printf("0x102B cut: segments %d, length %d, state %d\n", count(0x1037), RS16(r + 0x4A), RS16(r + 0x48));
    assert(RS16(r + 0x48) == 5 && RS16(r + 0x4E) == -1);
    /* spears hurt Sho */
    uint32_t sp = find(0x102E, 0);
    assert(sp);
    place_sho(RS32(sp + 8), RS32(sp + 0xC));
    int hurt = 0;
    for (int i = 0; i < 200 && !hurt; i++) {
        PW16(P_FLAGS2, P16(P_FLAGS2) & 0xFFF7); W16(0x060EA41Cu, 0);
        PW32(P_X, R32(sp + 8)); PW32(P_Y, R32(sp + 0xC));
        tick(0);
        hurt = fx_count(2) > 0;
    }
    printf("0x102E spears: hit Sho %d (phases %d %d %d %d)\n", hurt, Ram_Ptr(sp + 0x50)[0], Ram_Ptr(sp + 0x51)[0],
           Ram_Ptr(sp + 0x52)[0], Ram_Ptr(sp + 0x53)[0]);
    assert(hurt);
    /* door opens and releases its enemy */
    uint32_t d = 0;
    for (int k = 0; (d = find(0x102F, k)); k++) if (RS16(d + 0x48) == 0 && RS16(d + 0x4C) == -1 && (R16(d + 0x12) & 8)) break;
    assert(d);
    place_sho(RS32(d + 8) - 0x800000, RS32(d + 0xC));
    for (int i = 0; i < 5; i++) tick(0);
    printf("0x102F door: state %d, enemy slot %d (type %d)\n", RS16(d + 0x48), RS16(d + 0x4C),
           RS16(d + 0x4C) >= 0 ? R16(slot(RS16(d + 0x4C))) : -1);
    assert(RS16(d + 0x48) == 1 && RS16(d + 0x4C) >= 0 && R16(slot(RS16(d + 0x4C))) == 2);
    for (int i = 0; i < 30; i++) tick(0);
    printf("0x102F door: state %d, opening %d px\n", RS16(d + 0x48), RS16(d + 0x4A));
    assert(RS16(d + 0x48) == 2 && RS16(d + 0x4A) == 40);
}

/* ---------------------------------------------------------------- 5-1 / 5-2 */
static void stage8(void)
{
    load(8);
    uint32_t c = 0;
    for (int k = 0; (c = find(0x103D, k)); k++) if ((R16(c + 0x12) & 3) == 0) break;
    assert(c && count(0x103F) == 1);
    int32_t x0 = RS32(c + 8);
    place_sho(x0, RS32(c + 0xC) - 0x200000);
    for (int i = 0; i < 60; i++) tick(0);
    printf("0x103D cart: running %d, x +%.0f px, speed %.2f, track %02X\n", R16(c + 4) & 1, px(RS32(c + 8) - x0),
           px(RS32(c + 0x48)), R16(c + 0x50));
    assert((R16(c + 4) & 1) && RS32(c + 8) > x0 + 0x200000);
    tick(0);
    load(9);
    uint32_t e = find(0x1043, 0);
    assert(e && count(0x1044) == 17);
    int32_t ey = RS32(e + 0xC);
    W16(e + 0x48, 1);                                       /* descend (stage hook 0x0606DDBC) */
    for (int i = 0; i < 30; i++) tick(0);
    printf("0x1043 elevator: went down %.1f px (speed %.2f)\n", px(RS32(e + 0xC) - ey), px(-RS32(e + 0x34)));
    assert(RS32(e + 0xC) > ey && RS32(e + 0x34) < 0);
    W32(e + 0xC, 0x2F000000); W16(e + 0x48, 2);             /* rise from near the bottom */
    for (int i = 0; i < 60; i++) tick(0);
    printf("0x1043 elevator: rising at y %.1f, table index %d\n", px(RS32(e + 0xC)), R16(0x060F22BCu));
    assert(RS32(e + 0xC) == 0x2F000000 - 60 * 0xC000);
    uint32_t k = find(0x1044, 0);
    int it = items();
    {   /* the crates sit on the shaft wall: bring the elevator level with it to stand on */
        uint32_t hb = R32(e + 0x68);
        W16(e + 0x48, 0); W32(e + 0x34, 0); OR16(e + 4, 1);   /* world bounds follow it */
        W32(e + 0xC, (uint32_t)(RS32(k + 0xC) - RS16(hb + 4) * 0x10000 + 0x20000));
        printf("   crate at %.0f,%.0f; elevator top at %.0f\n", px(RS32(k + 8)), px(RS32(k + 0xC)), px(RS32(e + 0xC)) + RS16(hb + 4));
    }
    place_sho(RS32(k + 8) + 0x200000, RS32(k + 0xC));
    for (int i = 0; i < 300; i++) tick(0);
    { uint32_t hb = R32(k + 0x68);
      printf("   Sho %.0f,%.0f action %d; crate hb %d %d %d %d %d; cam %.0f,%.0f\n", px(P32(P_X)), px(P32(P_Y)), P16(P_ACTION), RS16(hb), RS16(hb + 2), RS16(hb + 4), RS16(hb + 6), RS16(hb + 8), px(RS32(CAM_X)), px(RS32(CAM_Y))); }
    {   /* slash from the crate's foot: higher up the sword clinks on the wall above it
         * (0x0604592E cancels the swing), so lower the elevator to the crate's bottom */
        int32_t foot = RS32(k + 0xC) + RS16(R32(k + 0x68) + 8) * 0x10000;
        W32(e + 0xC, (uint32_t)(foot - RS16(R32(e + 0x68) + 4) * 0x10000 + 0x20000));
        assert(slash(k, foot, gone));
    }
    printf("0x1044 crate broken: parts %d, items %d -> %d\n", fx_count(6), it, items());
    assert(fx_count(6) > 0);
}

/* ---------------------------------------------------------------- 6-1 / 6-2 */
static void stage10(void)
{
    load(10);
    assert(count(0x1020) == 10);
    int n = 0;
    for (int k = 0; k < 10; k++) n += run_handler(find(0x1020, k));
    printf("0x1020 decorations: %d sprites\n", n);
    assert(n > 0);
    load(11);
    assert(count(0x1038) == 5 && count(0x1039) == 3);
    uint32_t cr = find(0x1038, 0);
    for (int k = 0; k < 5; k++) { uint32_t q = find(0x1038, k), hb = R32(q + 0x68);
        printf("   crystal %.0f,%.0f hb %08X %d %d %d %d %d\n", px(RS32(q + 8)), px(RS32(q + 0xC)), hb, RS16(hb), RS16(hb + 2), RS16(hb + 4), RS16(hb + 6), RS16(hb + 8)); }
    place_sho(RS32(cr + 8), P32(P_Y));                      /* (the crystals hang 200 px up: sword box forced) */
    for (int i = 0; i < 10; i++) tick(0);
    sword_on(cr);
    assert(hit8(cr));
    tick(0);
    printf("0x1038 crystal: %d fragments\n", count(0x1038) - 5);
    assert(count(0x1038) == 13);
    for (int i = 0; i < 70; i++) tick(0);
    assert(count(0x1038) == 5);
    uint32_t w = find(0x1039, 0);
    W16(w + 0x48, 1);
    run_handler(w);
    printf("0x1039 wall collapsed: rubble %d\n", count(0x103B));
    assert(R16(w) == 0xFFFF && count(0x103B) >= 18);
}

/* ---------------------------------------------------------------- 8-1 */
static void stage14(void)
{
    load(14);
    assert(count(0x1045) == 2 && count(0x1046) == 17 && count(0x1047) == 20);
    uint32_t p = find(0x1047, 0);
    assert(slash(p, RS32(p + 0xC), gone));
    printf("0x1047 post broken\n");
    place_sho(P32(P_X), P32(P_Y));
    for (int i = 0; i < 10; i++) tick(0);
    ObjF_RockfallSpawn(P32(P_X) + 0x1000000, RS32(CAM_Y) + 0x200000);
    int rocks = count(0x1048);
    for (int i = 0; i < 800 && count(0x1048); i++) tick(0);
    printf("0x1048 rockfall: %d rocks, after landing %d left, rubble %d\n", rocks, count(0x1048), count(0x1049));
    assert(rocks >= 2 && rocks <= 3 && count(0x1048) == 0 && count(0x1049) > 0);
}

/* ---------------------------------------------------------------- 9-1 */
static void stage16(void)
{
    load(16);
    assert(count(0x1035) == 8 && count(0x1036) == 21 && count(0x1050) == 1);
    uint32_t u = 0;
    for (int k = 0; (u = find(0x1036, k)); k++) if (RS16(u + 0x18) != -1) break;
    assert(u);
    int it = items();
    assert(slash(u, RS32(u + 0xC), gone));
    printf("0x1036 urn broken: items %d -> %d, parts %d\n", it, items(), fx_count(6));
    assert(items() > it);
    uint32_t pl = find(0x1035, 0);
    int32_t y0 = RS32(pl + 0xC);
    W16(pl + 0x48, 0x20); W16(pl + 0x4C, 0);                 /* as after a down-thrust */
    place_sho(RS32(pl + 8) - 0x800000, y0);
    for (int i = 0; i < 40; i++) tick(0);
    printf("0x1035 platform: sank %.0f px, phase %d\n", px(RS32(pl + 0xC) - y0), RS16(pl + 0x4C));
    assert(RS32(pl + 0xC) - y0 == 0x200000 && RS16(pl + 0x4C) == 1);
}

/* ---------------------------------------------------------------- effects 0xA-0xD */
static void effects(void)
{
    for (int t = 0xA; t <= 0xD; t++) {
        Effects_Clear();
        Obj_Spawn(t, RS32(CAM_X) + 0xA00000, RS32(CAM_Y) + 0x700000, 0, 0);
        int frames = 0, drawn = 0;
        while (fx_count((uint16_t)t) && frames < 3000) {
            int n = g_spriteCount;
            Effects_Update();
            drawn += g_spriteCount - n;
            frames++;
        }
        uint32_t e = EFFECTS;
        printf("effect %X: %s after %d frames, %d sprites (anim %d)\n", t, fx_count((uint16_t)t) ? "alive" : "gone", frames, drawn, R16(e + 0x10 + 24));
        assert(fx_count((uint16_t)t) == 1 && drawn >= frames);   /* looping sparkles: stay */
    }
    /* off screen: not drawn, does not advance */
    Effects_Clear();
    Obj_Spawn(0xA, RS32(CAM_X) + 0x3000000, RS32(CAM_Y), 0, 0);
    int n = g_spriteCount;
    for (int i = 0; i < 50; i++) Effects_Update();
    assert(g_spriteCount == n && fx_count(0xA) == 1);
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    g_dir = argc > 1 ? argv[1] : "../extracted/";
    char p[512];
    snprintf(p, sizeof p, "%sA.BIN", g_dir);           assert(Ram_LoadImage(p, 0x06004000u));
    snprintf(p, sizeof p, "%sSHINOBI.dec.bin", g_dir); assert(Ram_LoadImage(p, 0x06040000u));
    snprintf(p, sizeof p, "%sSHOU.CHR", g_dir);        assert(Ram_LoadImage(p, 0x00200000u));
    stage1();
    stage4();
    stage6();
    stage8();
    stage10();
    stage14();
    stage16();
    effects();
    printf("all 0x1020-0x1050 object tests OK\n");
    return 0;
}
