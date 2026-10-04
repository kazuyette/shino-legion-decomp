/* Stage audit (not part of the test suite): boots SHINOBI straight into stage index N at
 * checkpoint C like --stage, plays it with a scripted pad (walk right, jump, slash, shuriken,
 * with a few "unstick" strategies) for F frames with infinite HP (0x060EA2CA = HP max
 * 0x060252B0 every frame while > 0, so pits / drowning still kill) and lives, and writes K
 * soft_render snapshots <out>/<NN>_c<C>_<frame>.png. Prints the start position, world bounds,
 * layer setup (streamed or not, SclConfig plane / plates), a one-line summary and every event
 * (reload, death, game-mode change, stuck, stage clear). C must exist in the checkpoint table
 * 0x060A8D94. docs/screenshots/audit/ = 4000 frames per checkpoint, ~8 shots per stage.
 *   S=$(cat tests/SOURCES.txt); gcc -std=c11 -O2 -Isrc -Itests tests/audit_stages.c $S -lm -o audit
 *   ./audit ../extracted/ N [C] [F] [K] [outdir] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pad.h"
#include "input.h"
#include "player.h"
#include "ram.h"
#include "render.h"
#include "boot.h"
#include "resource.h"
#include "stage.h"
#include "camera.h"
#include "enemy.h"
#include "objhandlers_c.h"
#include "soft_render.h"
extern RenderSprite g_spriteLog[]; extern int g_spriteLogCount;
static InputPort fake[2];
const InputPort *Input_GetPort(int p) { return &fake[p]; }

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "../extracted/";
    int idx = argc > 2 ? atoi(argv[2]) : 0;
    int cp = argc > 3 ? atoi(argv[3]) : 0;
    int frames = argc > 4 ? atoi(argv[4]) : 3000;
    int shots = argc > 5 ? atoi(argv[5]) : 8;
    const char *out = argc > 6 ? argv[6] : NULL;
    char p[512];
    static uint32_t fb[SCREEN_W * SCREEN_H];
    snprintf(p, sizeof p, "%s/A.BIN", dir);
    if (!Ram_LoadImage(p, 0x06004000u)) { printf("no A.BIN\n"); return 2; }
    Res_SetAssetDir(dir);
    snprintf(p, sizeof p, "%s/SHINOBI.dec.bin", dir);
    if (cp > 0 && Ram_LoadImage(p, 0x06040000u)) {        /* checkpoint table 0x060A8D94 */
        uint32_t t = 0x060A8D94u + (uint32_t)idx * 16 + (uint32_t)cp * 4u;
        if (R32(t) == 0) { printf("SUMMARY stage %2d cp %d: no such checkpoint\n", idx, cp); return 0; }
    }
    Boot_Init(0);
    Boot_DevStartStage(idx);
    int loads = 0, deaths = 0, stuckEv = 0, clear = 0, lastMode = -1, started = 0;
    int f, strat = 0, stratT = 0, still = 0, maxX = 0, minY = 1 << 30, maxY = -(1 << 30);
    int32_t lastX = 0, lastY = 0;
    int every = shots > 0 ? frames / shots : frames + 1;
    for (f = 0; f < frames; f++) {
        int inGame = Res_OverlayResident() == OVL_SHINOBI && R16(G_GAME_MODE) == 6;
        uint16_t held = 0;
        if (inGame) {
            int t = f;
            switch (strat) {
            case 0:                                  /* walk right, hop, slash */
                held = PAD_RIGHT;
                if (t % 48 < 14) held |= PAD_C;
                if (t % 10 < 2) held |= PAD_B;
                if (t % 90 == 30) held |= PAD_A;
                break;
            case 1: held = PAD_RIGHT | PAD_C | (t % 10 < 2 ? PAD_B : 0); break;   /* long jumps */
            case 2: held = PAD_LEFT | (t % 40 < 14 ? PAD_C : 0); break;            /* back off */
            case 3: held = PAD_UP | (t % 30 < 12 ? PAD_C : 0) | (t % 60 < 30 ? PAD_RIGHT : 0); break;
            case 4: held = PAD_DOWN | (t % 40 < 10 ? PAD_C : 0) | PAD_RIGHT; break;   /* drop through */
            }
            if (++stratT > (strat ? 90 : 100000)) { strat = 0; stratT = 0; }
        }
        fake[0].connected = 1; fake[0].raw = (uint16_t)~held;
        g_spriteLogCount = 0;
        Boot_Frame();
        int mode = Res_OverlayResident() == OVL_SHINOBI ? R16(G_GAME_MODE) : 100 + (int)R32(G_DIR_SCENE);
        if (mode != lastMode) {
            printf("  f%5d mode %d (stage %d)\n", f, mode, RS16(G_STAGE_INDEX));
            if (mode == 5) {
                loads++;
                if (loads == 1 && cp) { W16(G_CHECKPOINT, (uint16_t)cp); W16(0x060252DCu, 1); }
            }
            if (mode == 7) clear = 1;
            lastMode = mode;
        }
        if (Res_OverlayResident() == OVL_SHINOBI && mode == 6) {
            if (!started) {
                started = 1; lastX = (int32_t)P32(P_X);
                printf("  start: player %d,%d bounds %d,%d..%d,%d cam %d,%d\n", (int32_t)P32(P_X) >> 16, (int32_t)P32(P_Y) >> 16,
                       RS32(0x060EA618u) >> 16, RS32(0x060EA61Cu) >> 16, RS32(0x060EA620u) >> 16, RS32(0x060EA624u) >> 16,
                       RS32(CAM_X) >> 16, RS32(CAM_Y) >> 16);
                printf("  layers: C+0x20 %04X SCRCTL %04X", R16(0x060EA638u), R16(0x0603A60Au));
                for (uint32_t l = 0; l < 3; l++) {
                    uint32_t c = 0x06020D08u + 0x4Cu * l, d = 0x060EA588u + 0x30u * l, lay = R32(d + 0x20);
                    printf(" | NBG%u plane %u plates %08X %08X %08X %08X ring %08X layout %dx%d", l, Ram_Ptr(c)[3], R32(c + 12), R32(c + 16),
                           R32(c + 20), R32(c + 24), R32(d + 0x24), lay ? Ram_Ptr(lay)[0] : 0, lay ? Ram_Ptr(lay)[1] : 0);
                }
                printf("\n");
            }
            if (RS16(PLAYER + P_HP) > 0) W16(PLAYER + P_HP, R16(0x060252B0u));   /* pits / drowning still kill */
            W16(LIVES, 3);
            if (RS16(G_STAGE_INDEX) != idx) { printf("  f%5d stage index -> %d\n", f, RS16(G_STAGE_INDEX)); }
            int32_t x = (int32_t)P32(P_X) >> 16, y = (int32_t)P32(P_Y) >> 16;
            if (x > maxX) maxX = x;
            if (y < minY) minY = y;
            if (y > maxY) maxY = y;
            if (R16(G_RESPAWN) && loads > 1 && deaths < loads - 1) { deaths = loads - 1; printf("  f%5d death/reload #%d at %d,%d\n", f, deaths, x, y); }
            if (abs(x - (lastX >> 16)) < 2 && abs(y - (lastY >> 16)) < 2) still++; else still = 0;
            lastX = (int32_t)P32(P_X); lastY = (int32_t)P32(P_Y);
            if (still == 150) {
                stuckEv++;
                printf("  f%5d stuck at %d,%d act %02X anim %04X flags %04X/%04X state %04X\n", f, x, y,
                       Player_Action(), P16(P_ANIM), P16(P_FLAGS2), P16(P_FLAGS4), R16(G_STAGE_STATE));
                strat = 1 + stuckEv % 4; stratT = 0; still = 0;
            }
        }
        if (out && (f % every == every - 1)) {
            Soft_ComposeFrame(fb, g_spriteLog, g_spriteLogCount, 0xFF000000u);
            snprintf(p, sizeof p, "%s/%02d_c%d_%05d.png", out, idx, cp, f + 1);
            Soft_WritePng(p, fb, SCREEN_W, SCREEN_H);
            printf("  shot %s  player %d,%d cam %d,%d act %02X sprites %d\n", p, (int32_t)P32(P_X) >> 16, (int32_t)P32(P_Y) >> 16,
                   RS32(CAM_X) >> 16, RS32(CAM_Y) >> 16, Player_Action(), g_spriteLogCount);
        }
        if (Res_OverlayResident() == OVL_DIRECTOR && clear) break;
    }
    printf("SUMMARY stage %2d cp %d: frames %d loads %d deaths %d stuck %d clear %d x<=%d y %d..%d final mode %d\n",
           idx, cp, f, loads, deaths, stuckEv, clear, maxX, minY, maxY, lastMode);
    return 0;
}
