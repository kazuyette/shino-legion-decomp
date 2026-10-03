/* Runs the player on the real game data (needs extracted/ files).
 * gcc -std=c11 -Isrc tests/test_realdata.c <src...> tests/render_stub.c */
#include <stdio.h>
#include <assert.h>
#include "pad.h"
#include "input.h"
#include "player.h"
#include "ram.h"
#include "render.h"
extern RenderSprite g_lastSprite; extern int g_spriteCount;
static InputPort fake[2];
const InputPort *Input_GetPort(int p) { return &fake[p]; }
static void tick(uint16_t held)
{
    fake[0].connected = 1; fake[0].raw = (uint16_t)~held;
    Pad_Update(2); Game_LatchPad1(P16(P_FLAGS4)); Player_Tick();
}
static int run_until(uint16_t held, int act, int max)
{
    for (int i = 0; i < max; i++) { tick(held); if (Player_Action() == act) return i + 1; }
    return -1;
}
int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "../extracted/";
    char p[512];
    snprintf(p, sizeof p, "%sA.BIN", dir);           assert(Ram_LoadImage(p, 0x06004000u));
    snprintf(p, sizeof p, "%sSHINOBI.dec.bin", dir); assert(Ram_LoadImage(p, 0x06040000u));
    snprintf(p, sizeof p, "%sSHOU.CHR", dir);        assert(Ram_LoadImage(p, 0x00200000u));
    Player_Init();
    for (int i = 0; i < 10; i++) tick(0);
    assert(g_spriteCount > 0);
    printf("idle sprite: %dx%d at %d,%d src %08X pal %08X\n", g_lastSprite.w, g_lastSprite.h,
           g_lastSprite.x, g_lastSprite.y, (unsigned)g_lastSprite.src, (unsigned)g_lastSprite.palette);
    assert(g_lastSprite.w > 0 && g_lastSprite.h > 0 && (g_lastSprite.src >> 20) == 2);
    assert(R16(g_lastSprite.palette + 2) != 0);                    /* palette loaded */
    int n = run_until(PAD_B, ACT_SLASH, 3);                        assert(n > 0);
    n = run_until(0, ACT_RECOVER, 120); printf("slash anim ends after %d frames\n", n); assert(n > 0);
    n = run_until(0, ACT_IDLE, 120);    printf("recover -> idle after %d frames\n", n); assert(n > 0);
    int32_t x0 = P32(P_X);
    for (int i = 0; i < 30; i++) tick(PAD_RIGHT);
    printf("walked %.1f px in 30 frames\n", (P32(P_X) - x0) / 65536.0);
    for (int i = 0; i < 120; i++) tick(0);
    n = run_until(PAD_C, 0x37, 60);     printf("jump take-off after %d frames\n", n); assert(n > 0);
    for (int i = 0; i < 120 && Player_Action() != ACT_IDLE; i++) tick(0);
    printf("back to idle: %s\n", Player_Action() == ACT_IDLE ? "yes" : "no");
    assert(Player_Action() == ACT_IDLE);
    printf("all real-data tests OK\n");
    return 0;
}
