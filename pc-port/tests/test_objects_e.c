/* Stage objects 0x1007-0x101F (objhandlers_e.c / objhandlers_e2.c) on the real stages:
 * idx 13 lantern, idx 2 bushes / gate / crumbling block, idx 3 boss heads + debris,
 * idx 5 plants / lamp / sparks, idx 0 clouds / stone lantern / block / rock pillar,
 * idx 10 turret / crate / decor / debris / rope.
 *   gcc -std=c11 -Isrc -Itests tests/test_objects_e.c $(cat tests/SOURCES.txt) -lm
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
#include "objhandlers_e.h"
#include "enemy.h"
#include "fx.h"
#include "shinobi.h"
#include "collision.h"
extern int g_spriteCount;
static InputPort fake[2];
const InputPort *Input_GetPort(int p) { return &fake[p]; }

#define C_BLOCK 0x060EA618u
static void tick(uint16_t held)
{
    fake[0].connected = 1; fake[0].raw = (uint16_t)~held;
    W16(0x060143EAu, R16(0x060143EAu) + 1u); W16(0x060F4CCCu, R16(0x060F4CCCu) + 1u);
    PW16(P_HP, 16);                                         /* keep Sho alive */
    Pad_Update(2); Scroll_Update(); Game_LatchPad1(P16(P_FLAGS4)); Player_Tick();
    PW16(P_HP, 16);                                         /* keep Sho alive */
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
static int proj_count(uint16_t t) { int n = 0; for (int i = 0; i < 64; i++) n += R16(PROJECTILES + (uint32_t)i * 0x40) == t; return n; }
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
/* Sho on the floor next to o (side -1 left / +1 right of it, at distance d), facing it,
 * slashing (ground slash, then jump slashes) until done(o) or the attempts run out. */
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
static int st48(uint32_t o) { return R16(o + 0x48) != 0; }
static int hit8(uint32_t o) { return (R16(o + 2) & 8) != 0; }
static int bush_cut(uint32_t o) { return R16(o + 0x4A) != 0; }
static int lid_off(uint32_t o) { return R16(o + 0x48) != 0; }
static int pillar_cut(uint32_t o) { return R16(o + 0x48) != 1; }
static int gone(uint32_t o) { return R16(o) == 0xFFFF; }

static void stage13(void)
{
    load(13);
    uint32_t g = find(0x1007, 0), l = find(0x1008, 0);
    assert(g && l && count(0x1007) == 2 && count(0x1008) == 4);
    tick(0);
    int n = run_handler(g);
    printf("0x1007 glow: +0x14 %d, %d sprites, x %.0f\n", R16(g + 0x14), n, px(RS32(g + 8)));
    assert(R16(g + 0x14) > 20 && (R16(g + 0x12) & 0x4000));
    int32_t x0 = RS32(l + 8), y0 = RS32(l + 0xC);
    printf("0x1008 lantern at %.0f,%.0f anim %d\n", px(x0), px(y0), R16(l + 6));
    assert(R16(l + 6) == 2 && R16(l + 0x48) == 0 && RS16(l + 0x4C) == (int16_t)(x0 >> 16));
    assert(slash(l, y0, st48));
    int dir = R16(l + 0x52);
    for (int i = 0; i < 60; i++) tick(0);
    printf("0x1008 top knocked %s: state %d, moved %.0f,%.0f, flags %04X\n", dir ? "left" : "right", R16(l + 0x48),
           px(RS32(l + 8) - x0), px(RS32(l + 0xC) - y0), R16(l + 2));
    assert(R16(l + 0x48) == 2 && (R16(l + 2) & 8) && RS32(l + 0xC) < y0);
    assert(dir ? RS32(l + 8) < x0 : RS32(l + 8) > x0);
}

static void stage2(void)
{
    load(2);
    assert(count(0x1009) == 77 && count(0x100A) == 16 && count(0x100B) == 5 && count(0x100C) == 2 && count(0x100D) == 7);
    /* bush: walk through it, then cut it */
    uint32_t b = 0;
    for (int k = 0; (b = find(0x1009, k)); k++) if (R16(b + 6) >= 3 && R16(b + 6) != 5) break;
    assert(b);
    int32_t bx = RS32(b + 8), by = RS32(b + 0xC);
    printf("0x1009 bush anim %d at %.0f,%.0f\n", R16(b + 6), px(bx), px(by));
    place_sho(bx, by);                                      /* (bushes hang on the cliff: no floor) */
    PW16(P_FLAGS2, P16(P_FLAGS2) & 0xBFFF);
    run_handler(b);
    printf("   Sho standing in it: +0x48 %d, +0x5A %d\n", R16(b + 0x48), R16(b + 0x5A));
    assert(R16(b + 0x48) == 0);
    PW32(P_X, (uint32_t)(bx + 0x800000));
    run_handler(b);
    assert(R16(b + 0x48) == 1);
    int e0 = fx_count(8);
    assert(slash(b, by, bush_cut));
    tick(0);
    printf("   cut: %d leaf effects (type 8), cooldown %d\n", fx_count(8) - e0, R16(b + 0x4C));
    assert(fx_count(8) - e0 >= 3 && R16(b + 0x4A) == 1);
    for (int i = 0; i < 80; i++) tick(0);
    assert(fx_count(8) == 0);                                /* leaves freed after 65 frames */
    /* decor */
    uint32_t a = find(0x100A, 0);
    place_sho(RS32(a + 8), RS32(a + 0xC));
    tick(0);
    assert(run_handler(a) >= 1 && R16(a + 6) == 7);
    /* crumbling block */
    uint32_t c = find(0x100D, 0);
    int32_t cx = RS32(c + 8), cy = RS32(c + 0xC);
    printf("0x100D block at %.0f,%.0f (anim %d)\n", px(cx), px(cy), R16(c + 6));
    place_sho(cx - 0x3A0000, cy - 0x300000);                /* its box: x-117..x, y-6..y+6 */
    int shake = -1, fall = -1, freed = -1, stood = 0;
    for (int i = 0; i < 300 && freed < 0; i++) {
        tick(0);
        if (shake < 0 && R16(c + 0x48) == 2) shake = i;
        if (shake >= 0 && fall < 0 && R16(c + 0x48) == 2 && !(P16(P_FLAGS2) & 0x4000)) stood++;
        if (fall < 0 && R16(c + 0x48) == 0) fall = i;
        if (R16(c) == 0xFFFF) freed = i;
    }
    printf("   Sho landed on it -> shaking at %d (stood %d frames), falls at %d, freed at %d (y %.0f)\n",
           shake, stood, fall, freed, px(RS32(c + 0xC)));
    assert(shake >= 0 && fall - shake == 64 && freed - fall == 65 && stood > 30);

    /* gate (flags 2, closed at checkpoint 0) is solid: Sho dropped on it stands on its top */
    uint32_t g2 = 0;
    for (int k = 0; (g2 = find(0x100C, k)); k++) if ((R16(g2 + 0x12) & 3) == 2) break;
    assert(g2);
    uint32_t ghb = R32(g2 + 0x68);
    int32_t gtop = RS32(g2 + 0xC) + RS16(ghb + 4) * 0x10000 - 2;
    place_sho(RS32(g2 + 8) + (RS16(ghb + 2) + RS16(ghb + 6)) / 2 * 0x10000, gtop - 0x300000);
    int touched = 0;
    for (int i = 0; i < 60; i++) { tick(0); touched |= (R16(g2 + 0x10) & 0x1000) != 0; }
    printf("0x100C gate box %d,%d..%d,%d: Sho dropped on it rests at y %.0f (top %.0f), touched %d, airborne %d\n",
           RS16(ghb + 2), RS16(ghb + 4), RS16(ghb + 6), RS16(ghb + 8), px(P32(P_Y)), px(gtop), touched,
           (P16(P_FLAGS2) & 0x4000) != 0);
    assert(touched && P32(P_Y) <= gtop + 0x10000 && !(P16(P_FLAGS2) & 0x4000));
    /* gate: opens at its checkpoint, moves the world bottom */
    uint32_t gt = 0;
    for (int k = 0; (gt = find(0x100C, k)); k++) if ((R16(gt + 0x12) & 3) == 1) break;
    assert(gt);
    int32_t gx = RS32(gt + 8);
    place_sho(gx - 0x300000, RS32(gt + 0xC));
    W16(G_CHECKPOINT, 1);
    for (int i = 0; i < 12; i++) tick(0);
    printf("0x100C gate (cp 1): moved %.0f px, world bottom %.0f, flags %04X\n", px(RS32(gt + 8) - gx),
           px(RS32(C_BLOCK + 0xC)), R16(gt + 0x12));
    assert(RS32(gt + 8) - gx == 0x400000 && RS32(C_BLOCK + 0xC) == 0x0E040000 && !(R16(gt + 0x12) & 3));
    W16(G_CHECKPOINT, 0);
}

static void stage3(void)
{
    load(3);
    uint32_t sp = find(0x100F, 0), hl = find(0x1010, 0), hr = find(0x1011, 0);
    assert(sp && hl && hr);
    for (int i = 0; i < 50; i++) tick(0);                  /* first group after 60 frames */
    int n = count(0x100F);
    printf("0x100F: %d object(s) after 70 frames, heads HP %d/%d, alive %d\n", n, RS16(hl + 0x16), RS16(hr + 0x16), R16(0x060F21FAu));
    assert(n > 1 && RS16(hl + 0x16) == 12 && R16(0x060F21FAu) == 2);
    uint32_t p = 0;
    for (int k = 0; (p = find(0x100F, k)); k++) if (R16(p + 0x12) & 0x1F) break;
    int32_t py = RS32(p + 0xC);
    for (int i = 0; i < 20; i++) tick(0);
    printf("   piece fell %.1f px in 20 frames\n", px(RS32(p + 0xC) - py));
    assert(RS32(p + 0xC) > py);
    /* the left head opens its mouth after 120 frames and spits a 0x1012 */
    int open = -1, shot = -1;
    for (int i = 0; i < 200 && shot < 0; i++) {
        tick(0);
        if (open < 0 && R16(0x060F2244u) == 1) open = i;
        if (count(0x1012)) shot = i;
    }
    printf("0x1010: mouth state 1 at %d, fireball 0x1012 at %d\n", open, shot);
    assert(open >= 0 && shot > open);
    /* a hit: 10 flashing frames */
    OR16(hl + 2, 8); W16(hl + 0x16, 5);
    tick(0);
    assert(R16(hl + 0x48) == 1 && R16(hl + 0x14) == 9);
    for (int i = 0; i < 12; i++) tick(0);
    assert(R16(hl + 0x48) == 0 && !(R16(hl + 2) & 8));
    /* kill both: explosions, score, then 0x100E */
    int32_t sc = RS32(SCORE);
    OR16(hl + 2, 8); W16(hl + 0x16, 0);
    tick(0);
    printf("   left head destroyed: type %04X, alive %d, score +%d, explosions %d, 0x100E %d\n", R16(hl),
           R16(0x060F21FAu), RS32(SCORE) - sc, fx_count(6), count(0x100E));
    assert(R16(hl) != 0x1010 && R16(0x060F21FAu) == 1 && RS32(SCORE) - sc == 1000 && count(0x100E) == 0);
    OR16(hr + 2, 8); W16(hr + 0x16, 0);
    tick(0);
    printf("   right head destroyed: alive %d, 0x100E %d, 0x060F688C %d\n", R16(0x060F21FAu), count(0x100E), R16(0x060F688Cu));
    assert(R16(0x060F21FAu) == 0 && count(0x100E) == 1 && R16(0x060F688Cu) == 1);
}

static void stage5(void)
{
    load(5);
    assert(count(0x1013) == 14 && count(0x1014) == 3 && count(0x1015) == 64);
    /* sparks: start on their path after 64 frames, then fly and are freed */
    uint32_t s = find(0x1015, 3);
    int32_t sx = RS32(s + 8), sy = RS32(s + 0xC);
    int freed = -1, moved = -1;
    for (int i = 0; i < 400 && freed < 0; i++) {
        tick(0);
        if (moved < 0 && (RS32(s + 8) != sx || RS32(s + 0xC) != sy)) moved = i;
        if (R16(s) != 0x1015) freed = i;
    }
    printf("0x1015 spark from %.0f,%.0f: moves at frame %d, freed at %d (last %.0f,%.0f), sparks left %d\n",
           px(sx), px(sy), moved, freed, px(RS32(s + 8)), px(RS32(s + 0xC)), count(0x1015));
    assert(moved >= 40 && freed > moved);
    uint32_t pl = find(0x1013, 1);
    int32_t x = RS32(pl + 8), y = RS32(pl + 0xC);
    printf("0x1013 plant at %.0f,%.0f, anim %d, partner slot %d\n", px(x), px(y), R16(pl + 6), RS16(pl + 0x4E));
    place_sho(x + 0x500000, y - 0x20000);
    for (int i = 0; i < 130; i++) tick(0);                 /* 128 frames idle at start */
    assert(R16(pl + 0x4C) == 0);
    printf("   Sho action %d after the wait\n", P16(P_ACTION));
    PW32(P_X, (uint32_t)x);                                 /* Sho walks into it */
    run_handler(pl);
    int brush = R16(pl + 0x48) ? 0 : -1;
    printf("   brushed at %d (+0x50 %d)\n", brush, R16(pl + 0x50));
    assert(brush >= 0 && R16(pl + 0x50) == 2);
    int n0 = count(0x104B);
    pl = find(0x1013, 3);                                   /* one not brushed (brushed frames have no hitbox) */
    y = RS32(pl + 0xC);
    for (uint32_t h = R32(pl + 0x68); h && RS16(h) != -1; h += 10)
        printf("   plant hitbox kind %d: %d,%d..%d,%d\n", RS16(h), RS16(h + 2), RS16(h + 4), RS16(h + 6), RS16(h + 8));
    assert(slash(pl, y - 0x20000, hit8));
    printf("   cut: %d leaves (0x104B)\n", count(0x104B) - n0);
    assert(count(0x104B) - n0 >= 2);                     /* (the next plant may be cut by the same slash) */
    uint32_t lp = find(0x1014, 0);
    place_sho(RS32(lp + 8), P32(P_Y));
    uint16_t t0 = R16(lp + 0x14);
    for (int i = 0; i < 10; i++) tick(0);
    printf("0x1014 lamp: timer %d -> %d, script pos %d\n", t0, R16(lp + 0x14), R16(lp + 0x5A));
    assert(R16(lp + 0x14) != t0);
}

static void stage0(void)
{
    load(0);
    assert(count(0x1016) == 2 && count(0x1017) == 14 && count(0x1018) == 5 && count(0x1019) == 6 && count(0x101A) == 34);
    uint32_t cl = find(0x1016, 0);
    int32_t cx = RS32(cl + 8);
    tick(0);
    printf("0x1016 cloud: dx %.4f px/frame\n", px(RS32(cl + 8) - cx));
    assert(RS32(cl + 8) - cx == -0x5000);
    assert(run_handler(find(0x1018, 0)) >= 0);
    /* stone lantern: knock the lid off (the 4th one drops item 4) */
    uint32_t ln = find(0x1017, 3);
    int32_t lx = RS32(ln + 8), ly = RS32(ln + 0xC), base = RS16(ln + 0x4C) * 0x10000;
    printf("0x1017 lantern at %.0f,%.0f (base y %.0f), item %d\n", px(lx), px(ly), px(base), RS16(ln + 0x18));
    int it0 = items();
    assert(slash(ln, base, lid_off));
    int states = 0, last = 1;
    for (int i = 0; i < 400 && R16(ln + 0x48) != 6; i++) {
        tick(0);
        if (R16(ln + 0x48) != last) { last = R16(ln + 0x48); states |= 1 << last; }
    }
    printf("   lid states seen %02X, final %d at %.0f,%.0f, items %d -> %d\n", states, R16(ln + 0x48),
           px(RS32(ln + 8)), px(RS32(ln + 0xC)), it0, items());
    assert(R16(ln + 0x48) == 6 && (states & 0x3C) == 0x3C && items() > it0);
    /* solid block: Sho dropped onto it stands on it */
    uint32_t bk = find(0x1019, 0);
    int32_t kx = RS32(bk + 8), ky = RS32(bk + 0xC);
    place_sho(kx, ky - 0x600000);
    for (int i = 0; i < 60; i++) tick(0);
    printf("0x1019 block at %.0f,%.0f: Sho at %.0f,%.0f airborne %d\n", px(kx), px(ky), px(P32(P_X)), px(P32(P_Y)),
           (P16(P_FLAGS2) & 0x4000) != 0);
    assert(!(P16(P_FLAGS2) & 0x4000) && P32(P_Y) < ky);
    /* rock pillar: cut the middle part -> rocks (projectile 0x25), the top falls */
    uint32_t pm = 0;
    for (int k = 0; (pm = find(0x101A, k)); k++) if ((R16(pm + 0x12) & 3) == 2) break;
    assert(pm);
    int32_t mx = RS32(pm + 8), my = RS32(pm + 0xC);
    printf("0x101A upper part at %.0f,%.0f\n", px(mx), px(my));
    int r0 = proj_count(0x25);
    assert(slash(pm, RS16(pm + 0x56) * 0x10000 + 0x0, pillar_cut));
    int rocks = proj_count(0x25) - r0;
    int seen = 0;
    for (int i = 0; i < 400 && R16(pm + 0x48) != 8 && R16(pm + 0x48) != 4; i++) { tick(0); seen |= 1 << R16(pm + 0x48); }
    printf("   cut: %d rocks, upper part states seen %03X, final %d (+0x4E %d) at %.0f,%.0f\n", rocks, seen,
           R16(pm + 0x48), R16(pm + 0x4E), px(RS32(pm + 8)), px(RS32(pm + 0xC)));
    /* cut directly: falls (6), bounces (7), lands (8); cut below it: thrown off (0), rocks (2), tilted (4) */
    assert(rocks >= 1 && (R16(pm + 0x48) == 8 || R16(pm + 0x48) == 4) && RS32(pm + 0xC) > my);
}

static void stage10(void)
{
    load(10);
    assert(count(0x101B) == 5 && count(0x101C) == 11 && count(0x101D) == 3 && count(0x101E) == 1 && count(0x101F) == 3);
    /* turret: wakes up, aims and fires a cannon ball */
    uint32_t tu = find(0x101B, 0);
    int32_t tx = RS32(tu + 8) + ((R16(tu + 2) & 0x8000) ? -0x800000 : 0x800000);
    int fired = -1;
    for (int i = 0; i < 400 && fired < 0; i++) {
        place_sho(tx, RS32(tu + 0xC));                      /* (no floor there: hold Sho in place) */
        tick(0);
        if (proj_count(0x24)) fired = i;
    }
    printf("0x101B turret: state %d, angle %d, fired at frame %d\n", R16(tu + 0x50), RS16(tu + 0x48) >> 6, fired);
    assert(fired >= 60);
    /* crate: hit -> burst + item */
    uint32_t cr = find(0x101C, 0);
    int it0 = items();
    place_sho(RS32(cr + 8) - 0x800000, RS32(cr + 0xC));
    tick(0);
    OR16(cr + 2, 8);
    tick(0);
    printf("0x101C crate: type %04X, explosions %d, items %d -> %d\n", R16(cr), fx_count(6), it0, items());
    assert(R16(cr) != 0x101C && fx_count(6) >= 1 && items() == it0 + 1);
    /* parallax decor */
    uint32_t d = find(0x101D, 0);
    int32_t dx = RS32(d + 8);
    tick(0);
    printf("0x101D decor: dx %.3f\n", px(RS32(d + 8) - dx));
    assert(RS32(d + 8) != dx);
    /* watcher: Sho's sword on a breakable cell of layer 2 throws 8 debris */
    {
        const uint32_t d2 = 0x060EA588u + 0x60;
        int32_t fx = 0, fy = 0;
        int nf = 0;   /* a solid cell (type & 0xC) with empty space above */
        for (int32_t y = 0x04400000; y < 0x04000000 + 0x05000000; y += 0x80000)
            for (int32_t x = 0x04400000; x < 0x04000000 + 0x12000000; x += 0x80000) {
                uint16_t at = Map_GetAttr(x, y, d2), up = Map_GetAttr(x, y - 0x200000, d2);
                if (at != 0xFFFF && (R16(0x060CA700u + 2u * ((at >> 4) & 7)) & 0xC) && up == 0xFFFF) {
                    nf++;
                    if (!fx) { fx = x; fy = y; }
                }
            }
        printf("0x101E: %d breakable layer-2 surface cells, first at %.0f,%.0f\n", nf, px(fx), px(fy));
        if (fx) {
            uint32_t w = find(0x101E, 0);
            int n0 = count(0x101E);
            /* Sho standing on it, facing right, with a fake sword box {2, 0,-40, 24,-8} */
            const uint32_t hb = 0x060FFF80u;
            static const int16_t box[6] = { 2, 0, -40, 24, -8, -1 };
            for (int k = 0; k < 6; k++) W16(hb + 2u * k, (uint16_t)box[k]);
            place_sho(fx - 0x80000, fy + 0x100000);
            PW16(P_FLAGS2, P16(P_FLAGS2) & 0x7FFF);
            PW16(P_ACTION, 4);                                  /* ACT_SLASH */
            W32(PLAYER + 0x68, hb);
            W16(w + 0x14, 0);
            W32(OBJ_CUR, w);
            ObjHandler_101E();
            int ok = count(0x101E) > n0;
            printf("   sword on it: %d debris spawned, watcher cooldown %d\n", count(0x101E) - n0, R16(w + 0x14));
            assert(ok && count(0x101E) - n0 == 8 && R16(w + 0x14) == 0x20);
            uint16_t c0 = R16(w + 0x14);
            W32(PLAYER + 0x68, hb);
            ObjHandler_101E();                                  /* cooldown: nothing more */
            assert(count(0x101E) - n0 == 8 && R16(w + 0x14) == c0 - 1);
            for (int i = 0; i < 100; i++) tick(0);
            assert(count(0x101E) == n0);
        }
    }
    /* debris piece */
    int s = Obj_Register(0x101E, P32(P_X), P32(P_Y) - 0x400000, 1, 0xFFFF);
    assert(s >= 0);
    W16(slot(s) + 0x48, 0x40);
    int32_t ey = RS32(slot(s) + 0xC);
    int life = 0;
    while (R16(slot(s)) == 0x101E && life < 200) { tick(0); life++; }
    printf("0x101E debris: rose %.0f px, lived %d frames\n", px(ey - RS32(slot(s) + 0xC)), life);
    assert(life == 96);
    /* rope: Sho jumping at the rope end grabs it */
    uint32_t rp = find(0x101F, 0);
    place_sho(RS32(rp + 8), RS32(rp + 0xC));
    tick(0);
    int grab = -1;
    for (int i = 0; i < 120 && grab < 0; i++) {
        PW32(P_X, R32(rp + 8)); PW32(P_Y, RS32(rp + 0xC) + 0x600000 + 0x480000);
        PW16(P_FLAGS2, P16(P_FLAGS2) | 0x0400);
        W16(PLAYER + 0x10, R16(PLAYER + 0x10) & 0xFFEF);
        W32(OBJ_CUR, rp);
        ObjHandler_101F();
        if (R32(0x060EA440u) == rp && (P16(4) & 0x100)) grab = i;
        W16(0x060143EAu, R16(0x060143EAu) + 1u);
    }
    printf("0x101F rope: grabbed at step %d (PL+0x10 %04X, PL+4 %04X)\n", grab, P16(0x10), P16(4));
    assert(grab >= 0 && (P16(0x10) & 0x90) == 0x90);
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    g_dir = argc > 1 ? argv[1] : "../extracted/";
    char p[512];
    snprintf(p, sizeof p, "%sA.BIN", g_dir);           assert(Ram_LoadImage(p, 0x06004000u));
    snprintf(p, sizeof p, "%sSHINOBI.dec.bin", g_dir); assert(Ram_LoadImage(p, 0x06040000u));
    snprintf(p, sizeof p, "%sSHOU.CHR", g_dir);        assert(Ram_LoadImage(p, 0x00200000u));
    stage13();
    stage2();
    stage3();
    stage5();
    stage0();
    stage10();
    printf("all 0x1007-0x101F object tests OK\n");
    return 0;
}
