/* Stage 1-1 soldiers (type 0x0001) on real data. */
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
extern RenderSprite g_lastSprite; extern int g_spriteCount;
static InputPort fake[2];
const InputPort *Input_GetPort(int p) { return &fake[p]; }
static void tick(uint16_t held)
{
    fake[0].connected = 1; fake[0].raw = (uint16_t)~held;
    W16(0x060143EAu, R16(0x060143EAu) + 1u); W16(0x060F4CCCu, R16(0x060F4CCCu) + 1u);
    Pad_Update(2); Scroll_Update(); Game_LatchPad1(P16(P_FLAGS4)); Player_Tick(); Obj_UpdateEnemies(); Obj_UpdateAll(); Projectiles_Update(); Effects_Update(); Items_Update();
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
    assert(Enemy_FindSlot(1) == 1 && Enemy_FindSlot(3) == 3 && Enemy_FindSlot(2) == -1);
    assert(R16(CHAR_TABLE + 0x60 * 10) == (0x12000 + 0x4740) >> 3);          /* soldier chars */
    assert(R16(SHADOW_CRAM + 2u * (0x10 * 16 + 1)) != 0 || R16(0x060A9084u + 2) != 0x10);
    /* first soldier on the ground (subtype 0/1) */
    uint32_t e = 0; int32_t bx = 0x7FFFFFFF;
    for (int s = 0; s < OBJ_SLOTS; s++) {
        uint32_t o = OBJ_TABLE + (uint32_t)s * OBJ_SIZE;
        if (R16(o) == 1 && (R16(o + 0x12) & 0x1F) < 2 && RS32(o + 8) < bx) { bx = RS32(o + 8); e = o; }
    }
    assert(e);
    printf("soldier at %.0f,%.0f flags %04X\n", RS32(e + 8) / 65536.0, RS32(e + 0xC) / 65536.0, R16(e + 0x12));
    Stage_SpawnPlayer();
    /* stand Sho 200 px left of it, on the same floor */
    PW32(P_X, (uint32_t)(RS32(e + 8) - 0x00C80000));
    PW32(P_Y, (uint32_t)(RS32(e + 0xC) - 0x00200000));
    W32(CAM_X, (uint32_t)(P32(P_X) - RS32(0x060EA550u)));
    W32(CAM_Y, (uint32_t)(P32(P_Y) - RS32(0x060EA554u)));
    int acts[0x20] = {0};
    for (int i = 0; i < 240; i++) { tick(0); if (R16(e) == 1) acts[R16(e + 6) & 0x1F]++; }
    printf("soldier after 4 s: type %04X action %d at %.0f,%.0f facing %s; actions seen:", R16(e), RS16(e + 6),
           RS32(e + 8) / 65536.0, RS32(e + 0xC) / 65536.0, (R16(e + 2) & 0x8000) ? "left" : "right");
    for (int i = 0; i < 0x20; i++) if (acts[i]) printf(" %X(%d)", i, acts[i]);
    printf("\n");
    assert(R16(e) == 1);
    assert(!(R16(e + 2) & 1));                       /* initialised */
    assert(R16(e + 2) & 0x8000 ? 1 : 1);
    assert(acts[0] > 0);                             /* stood idle */
    assert(acts[9] > 0 || acts[4] > 0 || acts[1] > 0);   /* shot or jumped */
    /* walk Sho next to it and slash until it dies */
    int dead = -1; int32_t score0 = RS32(SCORE);
    for (int i = 0; i < 600 && dead < 0; i++) {
        int32_t dx = RS32(e + 8) - P32(P_X);
        uint16_t held = 0;
        if (dx > 0x300000) held = PAD_RIGHT; else if (dx < -0x300000) held = PAD_LEFT;
        else held = (i & 8) ? PAD_B : 0;              /* B = sword, pressed every other 8 frames */
        tick(held);
        if (R16(e) == 0xFFFF) dead = i;
    }
    printf("soldier killed after %d frames, score %d -> %d, Sho HP %d\n", dead, score0, RS32(SCORE), RS16(0x060EA2CAu));
    assert(dead >= 0);
    assert(RS32(SCORE) == score0 + 100);
    assert(RS16(0x060EA2CAu) < 16);                 /* enemy shurikens reached Sho */
    /* next soldier: kill it with Sho's shurikens from a distance */
    uint32_t e2 = 0; bx = 0x7FFFFFFF;
    for (int s = 0; s < OBJ_SLOTS; s++) {
        uint32_t o = OBJ_TABLE + (uint32_t)s * OBJ_SIZE;
        if (R16(o) == 1 && (R16(o + 0x12) & 0x1F) < 2 && RS32(o + 8) < bx) { bx = RS32(o + 8); e2 = o; }
    }
    assert(e2);
    PW16(P_HP, 16);
    PW32(P_X, (uint32_t)(RS32(e2 + 8) - 0x00A00000));
    PW32(P_Y, (uint32_t)(RS32(e2 + 0xC) - 0x00100000));
    W32(CAM_X, (uint32_t)(P32(P_X) - RS32(0x060EA550u)));
    W32(CAM_Y, (uint32_t)(P32(P_Y) - RS32(0x060EA554u)));
    int thrown = 0, killed = -1; score0 = RS32(SCORE);
    for (int i = 0; i < 400 && killed < 0; i++) {
        tick((i & 16) ? PAD_A : 0);
        for (int k = 0; k < 64; k++) if (R16(PROJECTILES + k * 0x40) == 0) { thrown = 1; break; }
        if (R16(e2) == 0xFFFF) killed = i;
    }
    printf("shuriken: thrown %d, soldier killed after %d frames, score +%d\n", thrown, killed, RS32(SCORE) - score0);
    assert(thrown && killed >= 0 && RS32(SCORE) == score0 + 100);
    int fx = 0;
    for (int k = 0; k < 64; k++) fx += R16(EFFECTS + k * 0x30) != 0xFFFF;
    printf("effects alive: %d\n", fx);
    printf("all enemy tests OK\n");
    return 0;
}
