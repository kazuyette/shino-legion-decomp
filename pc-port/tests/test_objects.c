/* Stage objects on real data: floating platforms (0x1004) on 1-1.
 * gcc -std=c11 -Isrc -Itests tests/test_objects.c <src...> tests/render_stub.c */
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
#include "objhandlers.h"
#include "fixmath.h"
extern RenderSprite g_lastSprite; extern int g_spriteCount;
static InputPort fake[2];
const InputPort *Input_GetPort(int p) { return &fake[p]; }
static void tick(uint16_t held)
{
    fake[0].connected = 1; fake[0].raw = (uint16_t)~held;
    W16(0x060143EAu, R16(0x060143EAu) + 1u); W16(0x060F4CCCu, R16(0x060F4CCCu) + 1u);
    Pad_Update(2); Scroll_Update(); Game_LatchPad1(P16(P_FLAGS4)); Player_Tick(); Obj_UpdateEnemies(); Obj_UpdateAll();
}
int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "../extracted/";
    char p[512];
    snprintf(p, sizeof p, "%sA.BIN", dir);           assert(Ram_LoadImage(p, 0x06004000u));
    snprintf(p, sizeof p, "%sSHINOBI.dec.bin", dir); assert(Ram_LoadImage(p, 0x06040000u));
    snprintf(p, sizeof p, "%sSHOU.CHR", dir);        assert(Ram_LoadImage(p, 0x00200000u));
    /* fixed-point helpers */
    assert(Sin8(0) == 0 && Sin8(0x40) == 0x10000 && Sin8(0xC0) == -0x10000 && Cos8(0) == 0x10000);
    assert(Sin8(0x20) == -Sin8(0xE0) && Sin8(0x60) == Sin8(0x20));
    assert(FixMul(0x30000, 0x8000) == 0x18000 && FixDiv(0x10000, 0x40000) == 0x4000);
    Player_Init();
    assert(Stage_Load(dir, 12));
    /* stage sprite chars registered from the SPR file */
    assert(R16(CHAR_TABLE + 0x10 * 10) == (0x8000 + 0x4740) >> 3);
    /* first floating platform (lowest X) */
    int best = -1; int32_t bx = 0x7FFFFFFF;
    for (int s = 0; s < OBJ_SLOTS; s++) {
        uint32_t o = OBJ_TABLE + (uint32_t)s * OBJ_SIZE;
        if (R16(o) == 0x1004 && RS32(o + 8) < bx) { bx = RS32(o + 8); best = s; }
    }
    assert(best >= 0);
    uint32_t pf = OBJ_TABLE + (uint32_t)best * OBJ_SIZE;
    printf("first platform: slot %d at %.0f,%.0f flags %04X param %04X\n", best,
           RS32(pf + 8) / 65536.0, RS32(pf + 0xC) / 65536.0, R16(pf + 0x12), R16(pf + 0x18));
    /* drop Sho right above it */
    Stage_SpawnPlayer();
    PW32(P_X, (uint32_t)RS32(pf + 8));
    PW32(P_Y, (uint32_t)(RS32(pf + 0xC) - 0x400000));
    W32(CAM_X, (uint32_t)(P32(P_X) - RS32(0x060EA550u)));
    W32(CAM_Y, (uint32_t)(P32(P_Y) - RS32(0x060EA554u)));
    g_spriteCount = 0;
    int landed = -1;
    int32_t ymin = 0x7FFFFFFF, ymax = -0x7FFFFFFF;
    for (int i = 0; i < 240; i++) {
        tick(0);
        if (landed < 0 && !(P16(P_FLAGS2) & 0x4000)) landed = i;
        if (landed >= 0 && i > landed + 30) {
            if (P32(P_Y) < ymin) ymin = P32(P_Y);
            if (P32(P_Y) > ymax) ymax = P32(P_Y);
        }
    }
    printf("landed on the platform after %d frames; Sho y range %.2f..%.2f, platform y %.2f angle %d\n",
           landed, ymin / 65536.0, ymax / 65536.0, RS32(pf + 0xC) / 65536.0, RS16(pf + 0x56));
    assert(landed >= 0);
    assert(!(P16(P_FLAGS2) & 0x4000));                              /* still standing */
    assert(RS16(pf + 0x52) == 2);                                    /* platform knows Sho is on it */
    assert(P32(P_Y) <= RS32(pf + 0xC) + 0x40000 && P32(P_Y) >= RS32(pf + 0xC) - 0x200000);
    assert(ymax > ymin);                                             /* bobbing carries Sho */
    assert(g_spriteCount > 240);                                     /* Sho + platform sprites */
    printf("all object tests OK\n");
    return 0;
}
