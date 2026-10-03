/* Stage objects 0x1000 (breakable container) and 0x1001 (7-piece statue) on 1-1.
 * gcc -std=c11 -Isrc -Itests tests/test_objects_a.c <src...> tests/render_stub.c */
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
#include "enemy.h"
#include "fx.h"
#include "objhandlers.h"
extern int g_spriteCount;
static InputPort fake[2];
const InputPort *Input_GetPort(int p) { return &fake[p]; }
static void tick(uint16_t held)
{
    fake[0].connected = 1; fake[0].raw = (uint16_t)~held;
    W16(0x060143EAu, R16(0x060143EAu) + 1u); W16(0x060F4CCCu, R16(0x060F4CCCu) + 1u);
    Pad_Update(2); Scroll_Update(); Game_LatchPad1(P16(P_FLAGS4)); Player_Tick(); Obj_UpdateEnemies(); Obj_UpdateAll(); Projectiles_Update(); Effects_Update(); Items_Update();
}
static uint32_t slot(int s) { return OBJ_TABLE + (uint32_t)s * OBJ_SIZE; }
static double px(int32_t v) { return v / 65536.0; }
static void place_sho(int32_t x, int32_t y)
{
    PW32(P_X, (uint32_t)x);
    PW32(P_Y, (uint32_t)y);
    W32(CAM_X, (uint32_t)(P32(P_X) - RS32(0x060EA550u)));
    W32(CAM_Y, (uint32_t)(P32(P_Y) - RS32(0x060EA554u)));
}
static int items(int16_t *kind)
{
    int n = 0;
    for (int i = 0; i < 64; i++) if (R16(ITEMS + (uint32_t)i * 0x30) != 0xFFFF) { n++; if (kind) *kind = RS16(ITEMS + (uint32_t)i * 0x30); }
    return n;
}
/* walk below x, then jump (C) and slash (B) in the air */
static uint16_t approach(int32_t x, int i)
{
    int32_t dx = x - P32(P_X);
    if (dx > 0x100000) return PAD_RIGHT;
    if (dx < -0x100000) return PAD_LEFT;
    i %= 48;
    return i < 10 ? PAD_C : (i >= 14 && i < 20) ? PAD_B : 0;
}
int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    const char *dir = argc > 1 ? argv[1] : "../extracted/";
    char p[512];
    snprintf(p, sizeof p, "%sA.BIN", dir);           assert(Ram_LoadImage(p, 0x06004000u));
    snprintf(p, sizeof p, "%sSHINOBI.dec.bin", dir); assert(Ram_LoadImage(p, 0x06040000u));
    snprintf(p, sizeof p, "%sSHOU.CHR", dir);        assert(Ram_LoadImage(p, 0x00200000u));
    Player_Init();
    assert(Stage_Load(dir, 12));
    W16(0x060252DCu, 1);                            /* no drop-in (0x0606E440 not run here) */
    Stage_SpawnPlayer();
    int box[8], nb = 0, st = -1;
    for (int s = 0; s < OBJ_SLOTS; s++) {
        uint32_t o = slot(s);
        if (R16(o) == 0x1000 && nb < 8) box[nb++] = s;
        if (R16(o) == 0x1001 && (R16(o + 0x12) & 0xF) == 0 && st < 0) st = s;
        if (R16(o) == 0x1000 || R16(o) == 0x1001)
            printf("slot %3d type %04X at %.0f,%.0f flags %04X param %d\n", s, R16(o), px(RS32(o + 8)),
                   px(RS32(o + 0xC)), R16(o + 0x12), RS16(o + 0x18));
    }
    assert(nb == 4 && st >= 0);
    for (int k = 1; k < 7; k++) assert(R16(slot(st + k)) == 0x1001 && (R16(slot(st + k) + 0x12) & 0xF) == k);

    /* ---- 0x1000: first container (orientation 0, drops item 4) */
    uint32_t b = slot(box[0]);
    int32_t bx0 = RS32(b + 8), by0 = RS32(b + 0xC);
    place_sho(bx0 - 0x400000, 0x05A00000);
    for (int i = 0; i < 30; i++) tick(0);
    printf("box: Sho at %.1f,%.1f (box %.1f,%.1f) half B %.1f,%.1f\n", px(P32(P_X)), px(P32(P_Y)),
           px(RS32(b + 8)), px(RS32(b + 0xC)), px(RS32(b + 0x48)), px(RS32(b + 0x4C)));
    assert(RS32(b + 0x4C) == by0 + 0x100000 && R16(b + 2) == 0);
    int cut = -1, gone = -1, land = -1, n0 = items(NULL);
    int16_t kind = -1;
    int32_t ymax = 0;
    for (int i = 0; i < 400 && gone < 0; i++) {
        tick(cut < 0 ? approach(bx0 + 0x300000, i) : 0);
        if (cut < 0 && (R16(b + 2) & 8)) {
            cut = i;
            printf("box cut at frame %d by attack, Sho %.1f,%.1f, items %d -> %d\n", i, px(P32(P_X)), px(P32(P_Y)), n0, items(&kind));
        }
        if (cut >= 0 && land < 0 && R16(b + 0x14)) land = i;
        if (cut >= 0 && RS32(b + 0xC) > ymax) ymax = RS32(b + 0xC);
        if (cut >= 0 && (i - cut) % 8 == 0 && R16(b) == 0x1000)
            printf("  +%2d A %.1f,%.1f rot %u  B %.1f,%.1f rot %u  v %.2f,%.2f t %d\n", i - cut,
                   px(RS32(b + 8)), px(RS32(b + 0xC)), Ram_Ptr(b + 0x50)[0], px(RS32(b + 0x48)), px(RS32(b + 0x4C)),
                   Ram_Ptr(b + 0x51)[0], px(RS32(b + 0x30)), px(RS32(b + 0x34)), R16(b + 0x14));
        if (cut >= 0 && R16(b) == 0xFFFF) gone = i;
    }
    printf("box: cut %d, landed %d, removed %d; lowest y %.1f; item kind %d\n", cut, land, gone, px(ymax), kind);
    assert(cut >= 0 && land > cut && gone > land && kind == 4);
    assert(ymax == 0x05A40000);                                          /* floor clamp */
    assert(gone - land == 31);

    /* ---- 0x1001: statue */
    uint32_t m = slot(st);
    int32_t mx = RS32(m + 8), my = RS32(m + 0xC);
    place_sho(mx - 0x600000, my - 0x100000);
    for (int i = 0; i < 40; i++) tick(0);
    printf("statue master slot %d at %.1f,%.1f mirror %d; pieces:", st, px(mx), px(my), (R16(m + 2) & 0x8000) != 0);
    for (int k = 0; k < 7; k++) printf(" [%d %.0f,%.0f f%d]", k, px(RS32(slot(st + k) + 8)), px(RS32(slot(st + k) + 0xC)), R16(slot(st + k) + 0x5A));
    printf("\n");
    for (int k = 1; k < 7; k++) assert(R16(slot(st + k) + 0x18) == R16(m + 0x18));
    /* solid: walk right into it */
    for (int i = 0; i < 60; i++) tick(PAD_RIGHT);
    printf("walked into the statue: Sho x %.1f (statue x %.1f) contact %04X\n", px(P32(P_X)), px(mx), R16(m + 0x10));
    assert(P32(P_X) < mx);
    /* enemy shuriken thrown at it bounces off */
    Projectiles_Clear();
    int ps = Projectile_Spawn(1, mx + 0x300000, my - 0x200000, 0x80, 0x20000);
    uint32_t pe = PROJECTILES + (uint32_t)ps * 0x40;
    int bounced = -1;
    for (int i = 0; i < 40 && bounced < 0; i++) { tick(0); if (R16(pe + 4) & 0x4000) bounced = i; }
    printf("enemy shuriken bounced off the statue after %d frames (x %.1f)\n", bounced, px(RS32(pe + 8)));
    /* slash it */
    int hits = 0, last6 = 0, n1 = items(NULL), boom = -1;
    for (int i = 0; i < 1200; i++) {
        tick((i & 8) ? PAD_B : 0);
        if (R16(m + 6) != last6) {
            last6 = R16(m + 6); hits++;
            printf("  frame %4d: hit -> counter %d; pieces:", i, last6);
            for (int k = 1; k < 7; k++) {
                uint32_t q = slot(st + k);
                printf(" %d%s", k, R16(q) == 0xFFFF ? "x" : (R16(q + 2) & 8) ? "*" : "");
            }
            printf("\n");
        }
        if (boom < 0 && R16(slot(st + 5)) == 0xFFFF) boom = i;
        if (last6 >= 6 && i > boom + 120) break;
    }
    int16_t k2 = -1; int n2 = items(&k2);
    printf("statue: counter %d, piece 5 exploded at %d, items %d -> %d (last kind %d), master frame %d\n",
           R16(m + 6), boom, n1, n2, k2, R16(m + 0x5A));
    for (int k = 1; k < 7; k++) {
        uint32_t q = slot(st + k);
        printf("  piece %d: %s at %.1f,%.1f t %d v %.2f,%.2f\n", k, R16(q) == 0xFFFF ? "removed" : "alive",
               px(RS32(q + 8)), px(RS32(q + 0xC)), RS16(q + 0x14), px(RS32(q + 0x30)), px(RS32(q + 0x34)));
    }
    assert(R16(m + 6) == 6 && boom >= 0 && n2 > n1 && k2 == RS16(m + 0x18));
    assert(R16(slot(st + 4)) == 0xFFFF && R16(slot(st + 5)) == 0xFFFF);
    assert(RS32(slot(st + 1) + 0xC) == 0x05CE0000 && RS32(slot(st + 2) + 0xC) == 0x05CE0000);
    assert(RS32(slot(st + 3) + 0xC) == 0x05C80000 && RS32(slot(st + 6) + 0xC) == 0x05D00000);
    assert(R16(m) == 0x1001);                                           /* the core stays */
    printf("all 0x1000/0x1001 tests OK\n");
    return 0;
}
