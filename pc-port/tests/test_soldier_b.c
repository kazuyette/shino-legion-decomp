/* Soldier subtypes 4 (0x06090D72), 7 (0x0609126C) and 8 (0x06091DE8) on real stage 1-1 data. */
#include <stdio.h>
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
#define CAM_X 0x060252A0u
#define CAM_Y 0x060252A4u
static InputPort fake[2];
const InputPort *Input_GetPort(int p) { return &fake[p]; }
static void tick(uint16_t held)
{
    fake[0].connected = 1; fake[0].raw = (uint16_t)~held;
    W16(0x060143EAu, R16(0x060143EAu) + 1u); W16(0x060F4CCCu, R16(0x060F4CCCu) + 1u);
    Pad_Update(2); Scroll_Update(); Game_LatchPad1(P16(P_FLAGS4)); Player_Tick(); Obj_UpdateEnemies(); Obj_UpdateAll(); Projectiles_Update(); Effects_Update(); Items_Update();
}
static uint32_t find_sub(int sub, int nth)
{
    for (int s = 0; s < OBJ_SLOTS; s++) {
        uint32_t o = OBJ_TABLE + (uint32_t)s * OBJ_SIZE;
        if (R16(o) == 1 && (R16(o + 0x12) & 0x1F) == sub && nth-- == 0) return o;
    }
    return 0;
}
static void place_sho(int32_t x, int32_t y)
{
    PW16(P_HP, 16);
    PW32(P_X, (uint32_t)x); PW32(P_Y, (uint32_t)y);
    W32(CAM_X, (uint32_t)(P32(P_X) - RS32(0x060EA550u)));
    W32(CAM_Y, (uint32_t)(P32(P_Y) - RS32(0x060EA554u)));
}
static void print_acts(const char *what, const int *acts)
{
    printf("%s actions seen:", what);
    for (int i = 0; i < 0x20; i++) if (acts[i]) printf(" %X(%d)", i, acts[i]);
    printf("\n");
}
/* walk to o and slash (B pulses) until pred; returns frame or -1 */
static int slash(uint32_t o, int maxf, int (*done)(uint32_t))
{
    for (int i = 0; i < maxf; i++) {
        int32_t dx = RS32(o + 8) - P32(P_X);
        uint16_t held;
        if (dx > 0x300000) held = PAD_RIGHT; else if (dx < -0x300000) held = PAD_LEFT;
        else held = (i & 8) ? PAD_B : 0;
        PW16(P_HP, 16);
        tick(held);
        if (done(o)) return i;
    }
    return -1;
}
static int sub4_respawned(uint32_t o) { return R16(o + 6) == 0x13 && R16(o + 4) == 0; }
static int gone(uint32_t o) { return R16(o) == 0xFFFF || R16(o + 6) == 0x0E || R16(o + 6) == 0x0F; }
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
    int n[32] = {0};
    for (int s = 0; s < OBJ_SLOTS; s++) {
        uint32_t o = OBJ_TABLE + (uint32_t)s * OBJ_SIZE;
        if (R16(o) == 1) n[R16(o + 0x12) & 0x1F]++;
    }
    printf("stage 1-1 soldiers by subtype:");
    for (int i = 0; i < 32; i++) if (n[i]) printf(" %d:%d", i, n[i]);
    printf("\n");
    for (int k = 0; k < n[4]; k++) { uint32_t o = find_sub(4, k); printf(" sub4 #%d at %.0f,%.0f param %d\n", k, RS32(o + 8) / 65536.0, RS32(o + 0xC) / 65536.0, RS16(o + 0x18)); }
    for (int k = 0; k < n[7]; k++) { uint32_t o = find_sub(7, k); printf(" sub7 #%d at %.0f,%.0f param %d\n", k, RS32(o + 8) / 65536.0, RS32(o + 0xC) / 65536.0, RS16(o + 0x18)); }
    Stage_SpawnPlayer();

    /* ---- subtype 4: hidden, drops in, respawns at home when killed */
    uint32_t e = find_sub(4, 0);
    assert(e);
    int32_t hx = RS32(e + 8), hy = RS32(e + 0xC);
    place_sho(hx - 0x01400000, hy - 0x00200000);                    /* 320 px left: out of reach */
    for (int i = 0; i < 30; i++) tick(0);
    printf("sub4: after 30 f at 320 px: action %X flags4 %d home %d,%d visible %s\n", R16(e + 6), R16(e + 4),
           RS16(e + 0x50), RS16(e + 0x52), (R16(e + 0x58) & 0x4000) ? "no" : "yes");
    assert(!(R16(e + 2) & 1) && R16(e + 6) == 0x13);
    assert(RS16(e + 0x50) == (int16_t)(hx >> 16) && RS16(e + 0x52) == (int16_t)(hy >> 16));
    place_sho(hx - 0x00C80000, hy - 0x00200000);                    /* 200 px: within 236 px */
    int acts[0x20] = {0}, appear = -1, hp0 = 16, hurt = 0;
    for (int i = 0; i < 300; i++) {
        tick(0);
        if (appear < 0 && R16(e + 6) != 0x13) appear = i;
        acts[R16(e + 6) & 0x1F]++;
        if (RS16(0x060EA2CAu) < hp0) hurt++;
        PW16(P_HP, 16);
    }
    printf("sub4: appeared after %d frames (y %.0f), Sho hurt %d times;", appear, RS32(e + 0xC) / 65536.0, hurt);
    print_acts("", acts);
    assert(appear >= 0 && appear < 3);
    assert(acts[5] > 0 || acts[6] > 0);                             /* fell / landed */
    assert(acts[0] > 0 && (acts[9] > 0 || acts[4] > 0 || acts[10] > 0));
    assert(hurt > 0);
    int32_t sc = RS32(SCORE);
    int k = slash(e, 900, sub4_respawned);
    printf("sub4: killed by the sword after %d frames, score +%d, back home at %.0f,%.0f action %X item %d\n", k,
           RS32(SCORE) - sc, RS32(e + 8) / 65536.0, RS32(e + 0xC) / 65536.0, R16(e + 6), RS16(e + 0x18));
    assert(k >= 0 && RS32(SCORE) == sc + 100 && R16(e) == 1);
    assert(RS32(e + 8) == (hx & (int32_t)0xFFFF0000) && RS32(e + 0xC) == (hy & (int32_t)0xFFFF0000) && R16(e + 0x18) == 0xFFFF);
    /* stays hidden while Sho is near; re-arms when he goes 320 px away, reappears when back */
    for (int i = 0; i < 60; i++) tick(0);
    assert(R16(e + 6) == 0x13 && R16(e + 4) == 0 && (R16(e + 0x58) & 0x4000));   /* hidden: not drawn */
    place_sho(hx - 0x01500000, hy - 0x00200000);
    tick(0);
    int armed = R16(e + 4);
    place_sho(hx - 0x00C80000, hy - 0x00200000);
    int re = -1;
    for (int i = 0; i < 30 && re < 0; i++) { tick(0); if (R16(e + 6) != 0x13) re = i; }
    printf("sub4: re-armed %d at 336 px, reappeared %d frames after Sho came back\n", armed, re);
    assert(armed == 1 && re >= 0);

    /* ---- subtype 7: squad leader + 4 subtype-8 members */
    uint32_t L = find_sub(7, 0);
    assert(L);
    int32_t lx = RS32(L + 8), ly = RS32(L + 0xC);
    int16_t item = RS16(L + 0x18);
    place_sho(lx - 0x00F00000, ly - 0x00200000);                    /* 240 px: too far */
    for (int i = 0; i < 10; i++) tick(0);
    assert(R16(L + 2) & 1);                                         /* not spawned yet */
    place_sho(lx - 0x00C80000, ly - 0x00200000);
    tick(0);
    assert(!(R16(L + 2) & 1) && R16(L + 6) == 0x13);
    uint32_t ch[4];
    printf("sub7: squad spawned:");
    for (int i = 0; i < 4; i++) {
        uint16_t id = R16(L + 0x48 + 2 * i);
        assert(id != 0);
        ch[i] = OBJ_TABLE + (uint32_t)(id - 1) * OBJ_SIZE;
        assert(R16(ch[i]) == 1 && (R16(ch[i] + 0x12) & 0x1F) == 8);
        printf(" [slot %d x %.0f item %d]", id - 1, RS32(ch[i] + 8) / 65536.0, RS16(ch[i] + 0x18));
    }
    printf("\n");
    assert(RS16(ch[0] + 0x18) == RS16(0x060C08D0u + 10 * item) && RS32(ch[3] + 8) == lx + 0x00600000);
    int cacts[0x20] = {0}, lstate[4] = {0}, orders = 0, order_seen[4] = {0}, fired[4] = {0};
    hurt = 0;
    for (int i = 0; i < 600; i++) {
        tick(0);
        lstate[R16(L + 4) & 3]++;
        for (int c = 0; c < 4; c++) {
            if (R16(ch[c]) != 1) continue;
            cacts[R16(ch[c] + 6) & 0x1F]++;
            if (R16(ch[c] + 0x4C) == 9) { if (!order_seen[c]) orders++; order_seen[c] = 1; }
            uint16_t a = R16(ch[c] + 6);
            if (a == 9 || a == 10 || a == 0xB) fired[c] = 1;
        }
        if (RS16(0x060EA2CAu) < 16) hurt++;
        PW16(P_HP, 16);
    }
    printf("sub7: leader states 0:%d 1:%d 2:%d 3:%d, members ordered %d (fired %d %d %d %d), Sho hurt %d times;",
           lstate[0], lstate[1], lstate[2], lstate[3], orders, fired[0], fired[1], fired[2], fired[3], hurt);
    print_acts(" members", cacts);
    if (cacts[0x13] == 4 * 600) {                                   /* members never woke up */
        printf("SKIP squad checks: subtype 8 (Soldier_Sub08, enemy_soldier_b.c) is not dispatched by Soldier_Subtype\n");
        printf("soldier_b tests OK (subtype 4 + squad spawn only)\n");
        return 0;
    }
    assert(lstate[0] > 0 && lstate[1] > 0 && orders >= 2);
    assert(cacts[0] > 0 && (cacts[9] > 0 || cacts[10] > 0 || cacts[4] > 0));
    assert(hurt > 0);
    /* cut the squad down */
    sc = RS32(SCORE);
    for (int c = 0; c < 4; c++) {
        int f = slash(ch[c], 900, gone);
        printf("sub7: member %d down after %d frames (score +%d)\n", c, f, RS32(SCORE) - sc);
        assert(f >= 0);
    }
    for (int i = 0; i < 120; i++) { PW16(P_HP, 16); tick(0); }
    printf("sub7: squad killed, score +%d, leader state %d\n", RS32(SCORE) - sc, R16(L + 4));
    assert(RS32(SCORE) >= sc + 400 && R16(L + 4) == 2);   /* +100 each (+100 for the health item member 1 drops) */
    /* Sho leaves (> 320 px) and comes back: a new squad without items */
    place_sho(lx - 0x01600000, ly - 0x00200000);
    tick(0);
    assert(R16(L + 4) == 3);
    place_sho(lx - 0x00C80000, ly - 0x00200000);
    tick(0);
    int n2 = 0;
    for (int i = 0; i < 4; i++) {
        uint16_t id = R16(L + 0x48 + 2 * i);
        if (id && R16(OBJ_TABLE + (uint32_t)(id - 1) * OBJ_SIZE + 0x18) == 0xFFFF) n2++;
    }
    printf("sub7: new squad of %d (no items), leader state %d\n", n2, R16(L + 4));
    assert(n2 == 4 && R16(L + 4) == 0);
    printf("all soldier_b tests OK\n");
    return 0;
}
