/* Soldier subtypes 10 (respawning ninja, 0x060927EC) and 12 (platform rider, 0x0608F58C)
 * on stage 1-1 real data.
 * gcc -std=c11 -Isrc -Itests tests/test_soldier_c.c $(cat tests/SOURCES.txt) */
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
extern RenderSprite g_lastSprite; extern int g_spriteCount;
static InputPort fake[2];
const InputPort *Input_GetPort(int p) { return &fake[p]; }
static void tick(uint16_t held)
{
    fake[0].connected = 1; fake[0].raw = (uint16_t)~held;
    W16(0x060143EAu, R16(0x060143EAu) + 1u); W16(0x060F4CCCu, R16(0x060F4CCCu) + 1u);
    Pad_Update(2); Scroll_Update(); Game_LatchPad1(P16(P_FLAGS4)); Player_Tick(); Obj_UpdateEnemies(); Obj_UpdateAll(); Projectiles_Update(); Effects_Update(); Items_Update();
}
static uint32_t slot(int s) { return OBJ_TABLE + (uint32_t)s * OBJ_SIZE; }
static void place_sho(int32_t x, int32_t y)
{
    PW32(P_X, (uint32_t)x);
    PW32(P_Y, (uint32_t)y);
    W32(CAM_X, (uint32_t)(P32(P_X) - RS32(0x060EA550u)));
    W32(CAM_Y, (uint32_t)(P32(P_Y) - RS32(0x060EA554u)));
}
static void print_acts(const char *what, const int *acts)
{
    printf("%s actions seen:", what);
    for (int i = 0; i < 0x20; i++) if (acts[i]) printf(" %X(%d)", i, acts[i]);
    printf("\n");
}
/* walk to the enemy and slash (B every other 8 frames) until it goes back into hiding */
static int slash_until_hidden(uint32_t e, int maxf, int *acts)
{
    for (int i = 0; i < maxf; i++) {
        int32_t dx = RS32(e + 8) - P32(P_X);
        uint16_t held;
        if (dx > 0x300000) held = PAD_RIGHT; else if (dx < -0x300000) held = PAD_LEFT;
        else held = (i & 8) ? PAD_B : 0;
        tick(held);
        if (acts) acts[R16(e + 6) & 0x1F]++;
        if (R16(e) != 1 || R16(e + 6) == 0x13) return i;
    }
    return -1;
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
    Stage_SpawnPlayer();

    /* (1) first subtype-10 soldier (lowest X) */
    uint32_t e = 0; int32_t bx = 0x7FFFFFFF;
    for (int s = 0; s < OBJ_SLOTS; s++)
        if (R16(slot(s)) == 1 && (R16(slot(s) + 0x12) & 0x1F) == 0x0A && RS32(slot(s) + 8) < bx) { bx = RS32(slot(s) + 8); e = slot(s); }
    assert(e);
    int32_t hx = RS32(e + 8), hy = RS32(e + 0xC);
    printf("subtype 10 at %.0f,%.0f flags %04X item %04X\n", hx / 65536.0, hy / 65536.0, R16(e + 0x12), R16(e + 0x18));
    /* Sho 0x100 px left: out of reach (> 0xA0) -> stays hidden */
    place_sho(hx - 0x01000000, hy - 0x00100000);
    int acts[0x20] = {0};
    for (int i = 0; i < 60; i++) { tick(0); acts[R16(e + 6) & 0x1F]++; }
    print_acts("subtype 10 with Sho 256 px away:", acts);
    assert(!(R16(e + 2) & 1) && R16(e + 6) == 0x13);                   /* hidden (off screen: not even drawn) */
    assert(RS16(e + 0x50) == (int16_t)(hx >> 16) && RS16(e + 0x52) == (int16_t)(hy >> 16));
    /* step within 0xA0 px: drops in from the top of the screen 8 px beside Sho */
    place_sho(hx - 0x00800000, hy - 0x00100000);
    for (int i = 0; i < 0x20; i++) acts[i] = 0;
    int dropped = -1, landed = -1;
    for (int i = 0; i < 240; i++) {
        tick(0);
        acts[R16(e + 6) & 0x1F]++;
        if (dropped < 0 && R16(e + 6) != 0x13) {
            dropped = i;
            printf("dropped in at frame %d: x %.0f (Sho %.0f) y %.0f (cam %.0f) facing %s\n", i, RS32(e + 8) / 65536.0,
                   P32(P_X) / 65536.0, RS32(e + 0xC) / 65536.0, RS32(CAM_Y) / 65536.0, (R16(e + 2) & 0x8000) ? "left" : "right");
        }
        if (dropped >= 0 && landed < 0 && R16(e + 6) == 6) landed = i;
    }
    print_acts("subtype 10 after 4 s:", acts);
    printf("landed at frame %d, now action %d at %.0f,%.0f, Sho HP %d\n", landed, RS16(e + 6),
           RS32(e + 8) / 65536.0, RS32(e + 0xC) / 65536.0, RS16(0x060EA2CAu));
    assert(dropped >= 0 && landed > dropped);
    assert(!(R16(e + 0x58) & 0x4000));                                     /* visible */
    PW16(P_HP, 16);
    int32_t score0 = RS32(SCORE);
    int k = slash_until_hidden(e, 600, NULL);
    printf("subtype 10 killed after %d frames, score %d -> %d; back home %.0f,%.0f armed %d item %04X\n", k, score0,
           RS32(SCORE), RS32(e + 8) / 65536.0, RS32(e + 0xC) / 65536.0, R16(e + 4), R16(e + 0x18));
    assert(k >= 0 && R16(e) == 1 && R16(e + 6) == 0x13);
    assert(RS32(SCORE) == score0 + 100);
    assert(RS32(e + 8) == (hx & (int32_t)0xFFFF0000) && RS32(e + 0xC) == (hy & (int32_t)0xFFFF0000));
    assert(R16(e + 4) == 0 && R16(e + 0x18) == 0xFFFF);
    for (int i = 0; i < 30; i++) tick(0);
    assert(R16(e + 6) == 0x13 && R16(e + 4) == 0);                        /* Sho still near: not re-armed */

    /* (2) first platform carrying a ninja */
    uint32_t pf = 0; bx = 0x7FFFFFFF;
    for (int s = 0; s < OBJ_SLOTS; s++)
        if (R16(slot(s)) == 0x1004 && (R16(slot(s) + 0x12) & 0x10) && RS32(slot(s) + 8) < bx) { bx = RS32(slot(s) + 8); pf = slot(s); }
    assert(pf);
    /* keep Sho alone with it: remove the other soldiers around */
    for (int s = 0; s < OBJ_SLOTS; s++)
        if (R16(slot(s)) == 1 && (R16(slot(s) + 0x12) & 0x1F) != 0x0C && RS32(slot(s) + 8) > bx - 0x02000000 &&
            RS32(slot(s) + 8) < bx + 0x02000000) W16(slot(s), 0xFFFF);
    printf("platform at %.0f,%.0f flags %04X rider slot %d\n", RS32(pf + 8) / 65536.0, RS32(pf + 0xC) / 65536.0,
           R16(pf + 0x12), RS16(pf + 0x1E));
    assert(RS16(pf + 0x1E) >= 0);                                          /* rider registered at init */
    uint32_t r = slot(RS16(pf + 0x1E));
    assert(R16(r) == 1 && (R16(r + 0x12) & 0x1F) == 0x0C);
    /* Sho 0x100 px left of it, on the shore: rider stays hidden but carried */
    place_sho(bx - 0x01000000, RS32(slot(0) + 0xC) - 0x00100000);
    for (int i = 0; i < 0x20; i++) acts[i] = 0;
    for (int i = 0; i < 60; i++) { tick(0); acts[R16(r + 6) & 0x1F]++; }
    print_acts("rider with Sho 256 px away:", acts);
    printf("rider at %.0f,%.0f (home %d,%d), platform y %.2f\n", RS32(r + 8) / 65536.0, RS32(r + 0xC) / 65536.0,
           RS16(r + 0x50), RS16(r + 0x52), RS32(pf + 0xC) / 65536.0);
    assert(R16(r + 6) == 0x13);
    /* walk right until it leaps out */
    for (int i = 0; i < 0x20; i++) acts[i] = 0;
    int leap = -1, onpf = -1, frames_on = 0;
    int32_t rymin = 0x7FFFFFFF, rymax = -0x7FFFFFFF, offmin = 0x7FFFFFFF, offmax = -0x7FFFFFFF;
    for (int i = 0; i < 300; i++) {
        tick(leap < 0 ? PAD_RIGHT : 0);
        acts[R16(r + 6) & 0x1F]++;
        if (leap < 0 && R16(r + 6) == 0x1B) {
            leap = i;
            printf("rider leaps out at frame %d (Sho %.0f px away)\n", i, (RS32(r + 8) - P32(P_X)) / 65536.0);
        }
        if (leap >= 0 && onpf < 0 && !(R16(r + 2) & 0x4000) && R16(r + 6) != 0x1B) onpf = i;
        if (onpf >= 0 && i > onpf + 10 && !(R16(r + 2) & 0x4000)) {
            int32_t off = RS32(r + 0xC) - RS32(pf + 0xC);
            frames_on++;
            if (RS32(r + 0xC) < rymin) rymin = RS32(r + 0xC);
            if (RS32(r + 0xC) > rymax) rymax = RS32(r + 0xC);
            if (off < offmin) offmin = off;
            if (off > offmax) offmax = off;
        }
    }
    print_acts("rider after 5 s:", acts);
    printf("back on the platform at frame %d; %d frames standing, rider y %.2f..%.2f, rider-platform %.2f..%.2f, Sho HP %d\n",
           onpf, frames_on, rymin / 65536.0, rymax / 65536.0, offmin / 65536.0, offmax / 65536.0, RS16(0x060EA2CAu));
    assert(leap >= 0 && acts[0x0B] > 0);                                  /* leapt and threw */
    assert(onpf > leap && frames_on > 100);
    assert(rymax > rymin);                                                /* bobs with the platform */
    assert(offmax - offmin <= 0x00040000);                                /* stays on it */
    /* slash it (Sho jumps onto the platform if needed) */
    PW16(P_HP, 16);
    score0 = RS32(SCORE);
    for (int i = 0; i < 0x20; i++) acts[i] = 0;
    k = -1;
    for (int i = 0; i < 900 && k < 0; i++) {
        int32_t dx = RS32(r + 8) - P32(P_X);
        uint16_t held;
        if (dx > 0x200000) held = PAD_RIGHT | ((i & 16) ? PAD_C : 0);
        else if (dx < -0x200000) held = PAD_LEFT;
        else held = (i & 8) ? PAD_B : 0;
        tick(held);
        acts[R16(r + 6) & 0x1F]++;
        if (R16(r + 6) == 0x13) k = i;
    }
    print_acts("rider while fighting:", acts);
    printf("rider killed after %d frames, score +%d, back home %.0f,%.0f (platform %.0f,%.0f) hidden %d\n", k,
           RS32(SCORE) - score0, RS32(r + 8) / 65536.0, RS32(r + 0xC) / 65536.0, RS32(pf + 8) / 65536.0,
           RS32(pf + 0xC) / 65536.0, (R16(r + 0x58) & 0x4000) != 0);
    assert(k >= 0 && RS32(SCORE) == score0 + 100 && R16(r) == 1);
    printf("all soldier subtype 10/12 tests OK\n");
    return 0;
}
