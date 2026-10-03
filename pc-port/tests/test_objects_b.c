/* Stage objects 0x1002 (item crate) and 0x1003 (trap door -> guard ninja type 4) on 1-1.
 * gcc -std=c11 -Isrc -Itests tests/test_objects_b.c <src...> tests/render_stub.c */
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
#include "collision.h"
#include "objhandlers.h"
extern RenderSprite g_lastSprite; extern int g_spriteCount;
static InputPort fake[2];
const InputPort *Input_GetPort(int p) { return &fake[p]; }
static void tick(uint16_t held)
{
    fake[0].connected = 1; fake[0].raw = (uint16_t)~held;
    W16(0x060143EAu, R16(0x060143EAu) + 1u); W16(0x060F4CCCu, R16(0x060F4CCCu) + 1u);
    Pad_Update(2); Scroll_Update(); Game_LatchPad1(P16(P_FLAGS4)); Player_Tick(); Obj_UpdateEnemies(); Obj_UpdateAll(); Projectiles_Update(); Effects_Update(); Items_Update();
}
static void place_sho(int32_t x, int32_t y)
{
    PW16(P_HP, 16);
    PW32(P_X, (uint32_t)x);
    PW32(P_Y, (uint32_t)y);
    W32(0x060252A0u, (uint32_t)(P32(P_X) - RS32(0x060EA550u)));
    W32(0x060252A4u, (uint32_t)(P32(P_Y) - RS32(0x060EA554u)));
}
static int count_type(uint16_t t)
{
    int n = 0;
    for (int s = 0; s < OBJ_SLOTS; s++) n += R16(OBJ_TABLE + (uint32_t)s * OBJ_SIZE) == t;
    return n;
}
static uint32_t nth_of(uint16_t type, int k)          /* k-th in slot order */
{
    for (int s = 0; s < OBJ_SLOTS; s++) {
        uint32_t o = OBJ_TABLE + (uint32_t)s * OBJ_SIZE;
        if (R16(o) == type && k-- == 0) return o;
    }
    return 0;
}
static uint16_t cell(int32_t x, int32_t y) { return Map_GetPN(x, y, LAYER_DESC); }
static int items(void)
{
    int n = 0;
    for (int s = 0; s < 64; s++) n += R16(ITEMS + (uint32_t)s * 0x30) != 0xFFFF;
    return n;
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
    int n2 = count_type(0x1002), n3 = count_type(0x1003);
    printf("1-1: %d crates (0x1002), %d trap doors (0x1003)\n", n2, n3);
    for (int k = 0; k < n2 && k < 8; k++) {
        uint32_t o = nth_of(0x1002, k);
        printf("  crate %d at %.0f,%.0f flags %04X item %04X\n", k, (RS32(o + 8) - 0x04000000) / 65536.0,
               (RS32(o + 0xC) - 0x04000000) / 65536.0, R16(o + 0x12), R16(o + 0x18));
    }
    for (int k = 0; k < n3; k++) {
        uint32_t o = nth_of(0x1003, k);
        printf("  door %d at %.0f,%.0f flags %04X item %04X\n", k, (RS32(o + 8) - 0x04000000) / 65536.0,
               (RS32(o + 0xC) - 0x04000000) / 65536.0, R16(o + 0x12), R16(o + 0x18));
    }
    assert(n2 > 0 && n3 > 0);

    /* ---- 0x1002: crate written into the map, broken by the sword */
    uint32_t c = nth_of(0x1002, 0);
    place_sho(RS32(c + 8) - 0x00400000, RS32(c + 0xC) + 0x00200000);
    tick(0);
    int32_t cx = RS32(c + 8), cy = RS32(c + 0xC);
    printf("crate: state %d at %.0f,%.0f, cells %03X %03X %03X %03X (saved %04X %04X %04X %04X), floor flags %X\n",
           R16(c + 6), (cx - 0x04000000) / 65536.0, (cy - 0x04000000) / 65536.0,
           cell(cx, cy), cell(cx + 0x100000, cy), cell(cx, cy + 0x100000), cell(cx + 0x100000, cy + 0x100000),
           R16(c + 0x48), R16(c + 0x4A), R16(c + 0x4C), R16(c + 0x4E),
           Coll_AttrFlags(Map_GetAttr(cx, cy + 0x200000, LAYER_DESC)));
    assert(!(cx & 0xFFFFF) && !(cy & 0xFFFFF));
    assert(cell(cx, cy) == 0x3F8 && cell(cx + 0x100000, cy) == 0x3FA &&
           cell(cx, cy + 0x100000) == 0x3FC && cell(cx + 0x100000, cy + 0x100000) == 0x3FE);
    for (int i = 0; i < 30; i++) tick(0);
    printf("  after 30 frames: state %d\n", R16(c + 6));
    uint16_t saved[4] = { R16(c + 0x48), R16(c + 0x4A), R16(c + 0x4C), R16(c + 0x4E) };
    uint16_t item = R16(c + 0x18);
    int it0 = items(), seen4 = -1, gone = -1, fx = 0;
    for (int i = 0; i < 400 && gone < 0; i++) {
        int32_t dx = cx + 0x100000 - (int32_t)P32(P_X);
        uint16_t held = dx > 0x300000 ? PAD_RIGHT : (dx < -0x300000 ? PAD_LEFT : ((i & 8) ? PAD_B : 0));
        tick(held);
        if (seen4 < 0 && R16(c) == 0x1002 && R16(c + 6) >= 4) {
            seen4 = i;
            for (int s = 0; s < 64; s++) fx += R16(EFFECTS + (uint32_t)s * 0x30) == 2;
        }
        if (seen4 >= 0 && R16(c) != 0x1002) gone = i;
        if ((int16_t)P16(P_HP) < 4) PW16(P_HP, 16);
    }
    printf("  sword: broken at frame %d (hit sparks %d), slot freed at %d, items %d -> %d, cells now %04X %04X %04X %04X\n",
           seen4, fx, gone, it0, items(), cell(cx, cy), cell(cx + 0x100000, cy), cell(cx, cy + 0x100000),
           cell(cx + 0x100000, cy + 0x100000));
    assert(seen4 >= 0 && gone > seen4 && gone - seen4 >= 32);
    assert(cell(cx, cy) == saved[0] && cell(cx + 0x100000, cy) == saved[1] &&
           cell(cx, cy + 0x100000) == saved[2] && cell(cx + 0x100000, cy + 0x100000) == saved[3]);
    if (item != 0xFFFF) assert(items() > it0 || items() >= 1);

    /* ---- 0x1002 falling: a crate registered 96 px above the old one's floor lands there */
    int s = Obj_Register(0x1002, cx + 0x37000, cy - 0x00600000, 0, 0xFFFF);
    assert(s >= 0);
    uint32_t f = OBJ_TABLE + (uint32_t)s * OBJ_SIZE;
    int states[8] = {0}, land = -1;
    for (int i = 0; i < 120; i++) {
        tick(0);
        states[R16(f + 6) & 7]++;
        if (land < 0 && i > 2 && R16(f + 6) == 0) land = i;
    }
    printf("crate falling: states 0:%d 1:%d 2:%d 3:%d, landed at frame %d, y %.0f (floor %.0f)\n",
           states[0], states[1], states[2], states[3], land, (RS32(f + 0xC) - 0x04000000) / 65536.0,
           (cy - 0x04000000) / 65536.0);
    assert(states[3] > 0 && land > 0 && RS32(f + 0xC) == cy && RS32(f + 8) == cx);
    assert(cell(cx, cy) == 0x3F8);

    /* ---- 0x1003: trap door spawns a guard ninja (type 4) when Sho comes close */
    uint32_t d = nth_of(0x1003, 0);
    int32_t dx0 = RS32(d + 8), dy0 = RS32(d + 0xC);
    place_sho(dx0 + 0x00280000 + 0x00C00000, dy0);                 /* 192 px right of the opening */
    int g0 = count_type(4);
    for (int i = 0; i < 20; i++) tick(0);
    printf("door: Sho 192 px away -> state %d, guards %d -> %d\n", R16(d + 6), g0, count_type(4));
    assert(R16(d + 6) == 0 && count_type(4) == g0 && !(R16(d + 2) & 1));
    place_sho(dx0 + 0x00280000 + 0x00600000, dy0);                 /* 96 px */
    int dst[4] = {0}, maxang = 0;
    for (int i = 0; i < 80; i++) {
        tick(0);
        dst[R16(d + 6) & 3]++;
        if (RS16(d + 0x14) > maxang) maxang = RS16(d + 0x14);
    }
    int g1 = count_type(4);
    printf("door: Sho 96 px away -> states 0:%d 1:%d 2:%d 3:%d, max angle %d, done %d, guards %d -> %d\n",
           dst[0], dst[1], dst[2], dst[3], maxang, R16(d + 0x48), g0, g1);
    assert(g1 == g0 + 1 && dst[1] > 0 && dst[2] == 17 && dst[3] > 0 && maxang == 0x3C && R16(d + 0x48) == 1);
    for (int i = 0; i < 30; i++) tick(0);
    assert(count_type(4) <= g1);                                       /* only once */
    /* the guard: newest type-4 near the door */
    uint32_t g = 0;
    for (int k = 0; k < OBJ_SLOTS; k++) {
        uint32_t o = OBJ_TABLE + (uint32_t)k * OBJ_SIZE;
        if (R16(o) == 4 && RS32(o + 8) - dx0 > -0x01000000 && RS32(o + 8) - dx0 < 0x01000000) g = o;
    }
    assert(g);
    printf("guard: at %.0f,%.0f (door +%.0f px) subtype flags %04X item %04X HP %d action %d\n",
           (RS32(g + 8) - 0x04000000) / 65536.0, (RS32(g + 0xC) - 0x04000000) / 65536.0,
           (RS32(g + 8) - dx0) / 65536.0, R16(g + 0x12), R16(g + 0x18), RS16(g + 0x16), R16(g + 6));
    int acts[0x20] = {0}, sc = RS32(SCORE), killed = -1;
    int16_t hp0 = (int16_t)P16(P_HP);
    for (int i = 0; i < 1500; i++) {
        int32_t dx = RS32(g + 8) - (int32_t)P32(P_X);
        uint16_t held = dx > 0x280000 ? PAD_RIGHT : (dx < -0x280000 ? PAD_LEFT : ((i & 8) ? PAD_B : 0));
        tick(held);
        if (R16(g) != 4) { killed = i; break; }
        acts[R16(g + 6) & 0x1F]++;
        if ((int16_t)P16(P_HP) < 4) PW16(P_HP, 16), hp0 += 12;
    }
    printf("guard actions seen:");
    for (int i = 0; i < 0x20; i++) if (acts[i]) printf(" %d(%d)", i, acts[i]);
    printf("\n  sword: killed after %d frames, score +%d, Sho lost %d HP\n", killed, RS32(SCORE) - sc,
           hp0 - (int16_t)P16(P_HP));
    assert(killed >= 0 && RS32(SCORE) - sc == 200);
    printf("all object_b tests OK\n");
    return 0;
}
