/* Stage 1-1 soldier subtypes 2 and 3/11 (enemy_soldier_a.c) on real data. */
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
static InputPort fake[2];
const InputPort *Input_GetPort(int p) { return &fake[p]; }
static void tick(uint16_t held)
{
    fake[0].connected = 1; fake[0].raw = (uint16_t)~held;
    W16(0x060143EAu, R16(0x060143EAu) + 1u); W16(0x060F4CCCu, R16(0x060F4CCCu) + 1u);
    Pad_Update(2); Scroll_Update(); Game_LatchPad1(P16(P_FLAGS4)); Player_Tick(); Obj_UpdateEnemies(); Obj_UpdateAll(); Projectiles_Update(); Effects_Update(); Items_Update();
}
static void put_sho(uint32_t e, int32_t dx, int32_t dy)
{
    PW16(P_HP, 16);
    PW32(P_X, (uint32_t)(RS32(e + 8) + dx));
    PW32(P_Y, (uint32_t)(RS32(e + 0xC) + dy));
    W32(CAM_X, (uint32_t)(P32(P_X) - RS32(0x060EA550u)));
    W32(CAM_Y, (uint32_t)(P32(P_Y) - RS32(0x060EA554u)));
}
static uint32_t find_sub(int sub)
{
    for (int s = 0; s < OBJ_SLOTS; s++) {
        uint32_t o = OBJ_TABLE + (uint32_t)s * OBJ_SIZE;
        if (R16(o) == 1 && (R16(o + 0x12) & 0x1F) == sub) return o;
    }
    return 0;
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
    for (int k = 0; k < 0xC0; k++) {                 /* PC test: no crates/boxes/statues/doors in the way */
        uint32_t ob = 0x060EC65Cu + (uint32_t)k * 0x78;
        if (R16(ob) >= 0x1000 && R16(ob) <= 0x1003) W16(ob, 0xFFFF);
    }
    printf("type-1 subtypes in 1-1:");
    for (int s = 0; s < OBJ_SLOTS; s++) {
        uint32_t o = OBJ_TABLE + (uint32_t)s * OBJ_SIZE;
        if (R16(o) == 1) printf(" %d", R16(o + 0x12) & 0x1F);
    }
    printf("\n");
    uint32_t e = find_sub(2);
    assert(e);
    int32_t ox = RS32(e + 8), oy = RS32(e + 0xC);
    printf("sub2 at %.0f,%.0f flags12 %04X\n", ox / 65536.0, oy / 65536.0, R16(e + 0x12));
    W16(0x060252DCu, 1);                            /* no drop-in (0x0606E440 not run here) */
    Stage_SpawnPlayer();
    put_sho(e, -0x00960000, -0x00100000);                 /* 150 px left: inside the 200 px trigger */
    int acts[0x20] = {0}, appear = -1, vanish = -1, shots = 0, back = -1;
    for (int i = 0; i < 600; i++) {
        uint16_t pa = R16(e + 6), pe = R16(e + 0x4E);
        PW16(P_HP, 16);
        tick(0);
        uint16_t a = R16(e + 6);
        acts[a & 0x1F]++;
        if (a == 0x11 && appear < 0) appear = i;
        if (a == 0x12 && vanish < 0) vanish = i;
        if (R16(e + 0x4E) < pe && pa != 0x13) shots++;
        if (vanish >= 0 && a == 0x13 && back < 0) back = i;
    }
    printf("sub2: appeared f%d, vanished f%d, waiting again f%d at %.0f,%.0f, shots %d; actions:", appear, vanish, back,
           RS32(e + 8) / 65536.0, RS32(e + 0xC) / 65536.0, shots);
    for (int i = 0; i < 0x20; i++) if (acts[i]) printf(" %X(%d)", i, acts[i]);
    printf("\n");
    assert(appear >= 0 && vanish > appear && back > vanish && shots == 3);
    assert(RS32(e + 8) == (RS16(e + 0x50) << 16) && RS16(e + 0x14) <= 0x2D0);
    /* wait for the reappearance, then slash it */
    int re = -1;
    for (int i = 0; i < 900 && re < 0; i++) { PW16(P_HP, 16); tick(0); if (R16(e + 6) == 0x11) re = i; }
    printf("sub2: reappeared %d frames after going back to wait (delay 0x2D0 = 720)\n", re + (600 - back));
    assert(re >= 0);
    int32_t score0 = RS32(SCORE); int dead = -1;
    for (int i = 0; i < 600 && dead < 0; i++) {
        int32_t dx = RS32(e + 8) - P32(P_X);
        uint16_t held = 0;
        if (dx > 0x300000) held = PAD_RIGHT; else if (dx < -0x300000) held = PAD_LEFT;
        else held = (i & 8) ? PAD_B : 0;
        PW16(P_HP, 16);
        tick(held);
        if (RS32(SCORE) == score0 + 100) dead = i;
    }
    printf("sub2: killed after %d frames, action %X at %.0f,%.0f (origin %d,%d) timer %d type %04X\n", dead, R16(e + 6),
           RS32(e + 8) / 65536.0, RS32(e + 0xC) / 65536.0, RS16(e + 0x50), RS16(e + 0x52), RS16(e + 0x14), R16(e));
    assert(dead >= 0 && R16(e) == 1 && R16(e + 6) == 0x13 && RS32(e + 8) == ox && RS32(e + 0xC) == oy);
    put_sho(e, -0x00960000, -0x00100000);
    int resp = -1;
    for (int i = 0; i < 1000 && resp < 0; i++) { PW16(P_HP, 16); tick(0); if (R16(e + 6) == 0x11) resp = i; }
    printf("sub2: respawned at its origin %d frames after death\n", resp);
    assert(resp >= 0x2D0);
    /* subtype 3 / 11 */
    uint32_t e3 = find_sub(3); if (!e3) e3 = find_sub(11);
    if (!e3) {
        printf("no subtype 3/11 soldier in stage 1-1: forcing a subtype-0 soldier to subtype 3\n");
        e3 = find_sub(0);
        assert(e3 && (R16(e3 + 2) & 1));                  /* not initialised yet */
        W16(e3 + 0x12, (R16(e3 + 0x12) & ~0x1Fu) | 3u);
    }
    printf("sub3 at %.0f,%.0f\n", RS32(e3 + 8) / 65536.0, RS32(e3 + 0xC) / 65536.0);
    put_sho(e3, -0x00A00000, -0x00100000);               /* target = origin - 80 px; Sho 160 px left */
    int acts3[0x20] = {0}, walkin = -1, shoot = -1, out = -1, shots3 = 0, tx = 0;
    for (int i = 0; i < 600; i++) {
        uint16_t pe = R16(e3 + 0x4E), pa = R16(e3 + 6);
        PW16(P_HP, 16);
        tick(0);
        uint16_t a = R16(e3 + 6);
        acts3[a & 0x1F]++;
        if (a == 7 && walkin < 0) { walkin = i; tx = RS16(e3 + 0x1E); }
        if ((a == 9 || a == 10) && shoot < 0) shoot = i;
        if (R16(e3 + 0x4E) < pe && pa != 0x13) shots3++;
        if (shots3 == 3 && out < 0 && (a == 7 || a == 5)) out = i;
    }
    printf("sub3: walk-in f%d (target x %d), first shot f%d, walk-out f%d (action %X, +4 %04X, y %08X vs +52 %d), shots %d; actions:",
           walkin, tx, shoot, out, R16(e3 + 6), R16(e3 + 4), R32(e3 + 0xC), RS16(e3 + 0x52), shots3);
    for (int i = 0; i < 0x20; i++) if (acts3[i]) printf(" %X(%d)", i, acts3[i]);
    printf("\n");
    assert(walkin >= 0 && shoot > walkin && shots3 == 3 && out > shoot);
    score0 = RS32(SCORE); dead = -1;
    for (int i = 0; i < 900 && dead < 0; i++) {
        int32_t dx = RS32(e3 + 8) - P32(P_X);
        uint16_t held = 0;
        if (dx > 0x300000) held = PAD_RIGHT; else if (dx < -0x300000) held = PAD_LEFT;
        else held = (i & 8) ? PAD_B : 0;
        PW16(P_HP, 16);
        tick(held);
        if (R16(e3) == 0xFFFF) dead = i;
    }
    printf("sub3: killed after %d frames, score +%d\n", dead, RS32(SCORE) - score0);
    assert(dead >= 0 && RS32(SCORE) >= score0 + 100);   /* +100 per soldier (a neighbour may die too) */
    printf("all soldier_a tests OK\n");
    return 0;
}
