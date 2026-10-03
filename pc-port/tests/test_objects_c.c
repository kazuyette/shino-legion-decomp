/* Stage 1-1 water objects (0x1005 waves, 0x1006 splash) and the end-of-stage checks
 * (Stage_CheckExit / Player_CheckFall / Stage_UpdateState) on real data.
 * gcc -std=c11 -Isrc -Itests tests/test_objects_c.c <src...> tests/render_stub.c */
#include <stdio.h>
#include <assert.h>
#include "pad.h"
#include "input.h"
#include "player.h"
#include "ram.h"
#include "render.h"
#include "stage.h"
#include "camera.h"
#include "objects.h"
#include "enemy.h"
#include "fx.h"
#include "objhandlers.h"
#include "objhandlers_c.h"
extern RenderSprite g_lastSprite; extern int g_spriteCount;
static InputPort fake[2];
const InputPort *Input_GetPort(int p) { return &fake[p]; }
/* SHINOBI frame 0x0606B8FA: stage tick 0x06040B44 (ends with 0x06046190), then the
 * object lists unless the stage index changed during the tick. */
static void tick(uint16_t held)
{
    fake[0].connected = 1; fake[0].raw = (uint16_t)~held;
    int16_t stage = RS16(G_STAGE_INDEX);
    W16(0x060143EAu, R16(0x060143EAu) + 1u); W16(0x060F4CCCu, R16(0x060F4CCCu) + 1u);
    Pad_Update(2); Scroll_Update(); Game_LatchPad1(P16(P_FLAGS4)); Player_Tick();
    Stage_UpdateState();
    if (RS16(G_STAGE_INDEX) != stage) return;
    Obj_UpdateEnemies(); Obj_UpdateAll(); Projectiles_Update(); Effects_Update(); Items_Update();
}
static uint32_t find(uint16_t type)
{
    for (int s = 0; s < OBJ_SLOTS; s++)
        if (R16(OBJ_TABLE + (uint32_t)s * OBJ_SIZE) == type) return OBJ_TABLE + (uint32_t)s * OBJ_SIZE;
    return 0;
}
static int run_handler(uint32_t o, void (*h)(void))
{
    int n = g_spriteCount;
    W32(OBJ_CUR, o);
    h();
    return g_spriteCount - n;
}
static void place(int32_t x, int32_t y)
{
    PW32(P_X, (uint32_t)x); PW32(P_Y, (uint32_t)y);
    PW32(0x2C, (uint32_t)y);
    W32(CAM_X, (uint32_t)(P32(P_X) - RS32(0x060EA550u)));
    W32(CAM_Y, (uint32_t)(P32(P_Y) - RS32(0x060EA554u)));
    Scroll_Update();
}
int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "../extracted/";
    char p[512];
    snprintf(p, sizeof p, "%sA.BIN", dir);           assert(Ram_LoadImage(p, 0x06004000u));
    snprintf(p, sizeof p, "%sSHINOBI.dec.bin", dir); assert(Ram_LoadImage(p, 0x06040000u));
    snprintf(p, sizeof p, "%sSHOU.CHR", dir);        assert(Ram_LoadImage(p, 0x00200000u));
    Player_Init();
    assert(Stage_Load(dir, 12));
    W16(0x060252DCu, 1);                            /* no drop-in (0x0606E440 not run here) */
    Stage_SpawnPlayer();
    W16(G_GAME_MODE, 6);                     /* stage frames (dispatcher 0x0606B54C) */
    W16(LIVES, 3);
    printf("1-1 checkpoint 0 (table 0x060A8D94): layer %d,%d -> world %d,%d\n",
           R16(0x060A8D94u + 12 * 16), R16(0x060A8D94u + 12 * 16 + 2),
           R16(0x060A8D94u + 12 * 16) + 1024, R16(0x060A8D94u + 12 * 16 + 2) + 1024);

    uint32_t w = find(0x1005), s = find(0x1006);
    assert(w && s);
    printf("0x1005 at %.0f,%.0f  0x1006 at %.0f,%.0f\n", RS32(w + 8) / 65536.0, RS32(w + 0xC) / 65536.0,
           RS32(s + 8) / 65536.0, RS32(s + 0xC) / 65536.0);
    /* at the start (camera high, x < 1024+480): nothing drawn */
    for (int i = 0; i < 30; i++) tick(0);
    int n = run_handler(w, ObjHandler_1005);
    printf("start: cam %.0f,%.0f, waves drew %d sprites, splash visible %d\n",
           RS32(CAM_X) / 65536.0, RS32(CAM_Y) / 65536.0, n, R16(s + 0x14));
    assert(n == 0 && R16(s + 0x14) == 0 && R16(w + 6) == 2 && R16(s + 6) == 5);
    assert(!(R16(G_STAGE_STATE) & 7));

    /* above the water (first gap, layer x ~1200): Sho falls in */
    place(0x08B00000, 0x05A00000);
    n = run_handler(w, ObjHandler_1005);
    printf("over water: cam %.0f,%.0f, waves x %.0f, drew %d sprites (last at %d,%d bank %X)\n",
           RS32(CAM_X) / 65536.0, RS32(CAM_Y) / 65536.0, RS32(w + 8) / 65536.0 - 640, n,
           g_lastSprite.x, g_lastSprite.y, g_lastSprite.colr);
    assert(n >= 5);
    int splash = -1, died = -1, hp0 = -1;
    for (int i = 0; i < 400 && died < 0; i++) {
        tick(0);
        if (splash < 0 && R16(s + 0x14)) {
            splash = i;
            printf("frame %d: splash at %.0f,%.0f (Sho %.0f,%.0f)\n", i, RS32(s + 8) / 65536.0,
                   RS32(s + 0xC) / 65536.0, P32(P_X) / 65536.0, P32(P_Y) / 65536.0);
            assert(RS32(s + 8) == P32(P_X) && RS32(s + 0xC) == 0x05EE0000);
        }
        if (hp0 < 0 && PS16(P_HP) == 0) { hp0 = i; printf("frame %d: fell off (y %.0f, cam y %.0f) -> HP 0\n", i, P32(P_Y) / 65536.0, RS32(CAM_Y) / 65536.0); }
        if (R16(G_GAME_MODE) != 6) died = i;
    }
    printf("splash visible until anim end: %d; death -> mode %d, lives %d, respawn %d, state %04X (frame %d)\n",
           R16(s + 0x14), R16(G_GAME_MODE), RS16(LIVES), R16(G_RESPAWN), R16(G_STAGE_STATE), died);
    assert(splash >= 0 && hp0 > splash);
    if (died >= 0) assert(R16(G_GAME_MODE) == 5 && RS16(LIVES) == 2 && R16(G_RESPAWN) == 1);
    else printf("(death sequence not started by the player code within 400 frames)\n");

    /* exit zone */
    W16(G_STAGE_STATE, 0); W16(G_GAME_MODE, 6); W16(G_RESPAWN, 0); W16(G_CHECKPOINT, 2);
    PW16(P_HP, 8); PW16(P_FLAGS2, 0);
    place(0x37C00000, 0x05D00000);
    assert(Stage_CheckExit() == 0 && !(R16(G_STAGE_STATE) & 7));
    place(0x37E00000, 0x05D00000);
    assert(Stage_CheckExit() == 1);
    printf("exit zone: Stage_CheckExit fired, state %04X\n", R16(G_STAGE_STATE));
    assert((R16(G_STAGE_STATE) & 0xF) == 10);
    assert(Stage_CheckExit() == 0);                                     /* already running */
    Stage_UpdateState();                                                /* request frame: fade out */
    printf("after request frame: state %04X mode %d stage %d\n", R16(G_STAGE_STATE), R16(G_GAME_MODE), RS16(G_STAGE_INDEX));
    assert((R16(G_STAGE_STATE) & 0xF) == 2 && RS16(G_STAGE_INDEX) == 12);
    Stage_UpdateState();                                                /* fade done (not emulated) */
    printf("next frame: mode %d (5 = load stage), stage %d (13 = 1-2), checkpoint %d\n",
           R16(G_GAME_MODE), RS16(G_STAGE_INDEX), R16(G_CHECKPOINT));
    assert(R16(G_GAME_MODE) == 5 && RS16(G_STAGE_INDEX) == 13 && R16(G_CHECKPOINT) == 0);
    printf("all stage-state tests OK\n");
    return 0;
}
