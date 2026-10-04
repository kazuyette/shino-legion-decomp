/* Loads every stage index 0..17 on the real data (per-stage init table 0x060A8B0C,
 * stage_inits.c), drops Sho in at checkpoint 0 and runs 120 stage frames; checks the layer
 * set-up, that Sho lands (on the map, or on the 0x1025 ledges in 4-2) and that objects were placed. With a second argument
 * (a directory) it also writes a software-rendered screenshot of each stage after the 120
 * frames (VDP2 layers + sprites, soft_render.c) as stageNN_<file>.png.
 *   gcc -std=c11 -Isrc -Itests tests/test_stages.c $(cat tests/SOURCES.txt) -lm
 *   ./a.out ../extracted/ [../docs/screenshots/stages] */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "pad.h"
#include "input.h"
#include "player.h"
#include "ram.h"
#include "render.h"
#include "stage.h"
#include "stage_inits.h"
#include "camera.h"
#include "sprite.h"
#include "objects.h"
#include "objhandlers_c.h"
#include "enemy.h"
#include "fx.h"
#include "shinobi.h"
#include "soft_render.h"
#include "collision.h"
extern RenderSprite g_spriteLog[]; extern int g_spriteLogCount;
static InputPort fake[2];
const InputPort *Input_GetPort(int p) { return &fake[p]; }

static const char *kFiles[18] = { "STAGE7_1", "STAGE7_2", "STAGE2_1", "STAGE2_2", "STAGE3_1", "STAGE3_2",
    "STAGE4_1", "STAGE4_2", "STAGE5_1", "STAGE5_2", "STAGE6_1", "STAGE6_2", "STAGE1_1", "STAGE1_2",
    "STAGE8_1", "STAGE8_2", "STAGE9_1", "STAGE9_2" };
static const uint32_t kHandlers[18] = { 0x06047F90, 0x060480E6, 0x0604757C, 0x0604765C, 0x06047748, 0x0604789A,
    0x0604797C, 0x06047A68, 0x06047B4C, 0x06047C64, 0x06047D8C, 0x06047EA4, 0x060473B4, 0x0604749C,
    0x060481CC, 0x06048308, 0x060483E0, 0x060484D4 };

/* one stage frame, as Game_StageFrame (shinobi.c) */
static void tick(void)
{
    fake[0].connected = 1; fake[0].raw = 0xFFFF;
    g_spriteLogCount = 0;
    W16(0x060143EAu, R16(0x060143EAu) + 1u); W16(0x060F4CCCu, R16(0x060F4CCCu) + 1u);
    Pad_Update(2);
    Scroll_Update();
    Game_LatchPad1(P16(P_FLAGS4));
    Player_Tick();
    Stage_UpdateState();
    if (R16(0x060252DCu) == 0 && !(P16(P_FLAGS2) & 0x4000)) {   /* PC approximation of 0x0606E440 */
        W16(0x060252DCu, 1);
        PW16(P_FLAGS4, P16(P_FLAGS4) & 0xFFEF);
    }
    Obj_UpdateEnemies(); Obj_UpdateAll(); Projectiles_Update(); Effects_Update(); Items_Update();
}

/* pages referenced by a layer's layout must exist in the MAP (offsets table +0x1200) */
static int layout_ok(int l, int *w, int *h, int *maxPage)
{
    uint32_t d = LAYER_DESC + (uint32_t)l * 0x30, lay = R32(d + 0x20), m = R32(0x060EA650u);
    *w = Ram_Ptr(lay)[0]; *h = Ram_Ptr(lay)[1]; *maxPage = -1;
    uint32_t start = R32(m + 0x1200 + 4u * (uint32_t)l);
    uint32_t end = l < 2 ? R32(m + 0x1204 + 4u * (uint32_t)l) : 0xFFFFFFFFu;
    for (int i = 0; i < *w * *h; i++) {
        int pid = Ram_Ptr(lay)[2 + i];
        if (pid == 0xFF) continue;
        if (pid > *maxPage) *maxPage = pid;
        if (l < 2 && start + (uint32_t)(pid + 1) * 0x800u > end) return 0;
    }
    return *w > 0 && *h > 0;
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "../extracted/";
    const char *shots = argc > 2 ? argv[2] : NULL;
    char p[512];
    snprintf(p, sizeof p, "%sA.BIN", dir);           assert(Ram_LoadImage(p, 0x06004000u));
    snprintf(p, sizeof p, "%sSHINOBI.dec.bin", dir); assert(Ram_LoadImage(p, 0x06040000u));
    snprintf(p, sizeof p, "%sSHOU.CHR", dir);        assert(Ram_LoadImage(p, 0x00200000u));
    static uint32_t fb[SCREEN_W * SCREEN_H];
    int failures = 0;
    printf("idx game file      handler   flags coll prio N0-3/S0-3  layouts (w x h, max page)      objs  Sho (world px)      landed\n");
    for (int idx = 0; idx < 18; idx++) {
        assert(R32(0x060A8B0Cu + 4u * (uint32_t)idx) == kHandlers[idx]);   /* table = handlers ported */
        /* game mode 5 (0x0606B6EC) */
        W16(0x060EC658u, 0); W16(0x060EC65Au, 0);           /* checkpoint 0, no respawn */
        /* 0x060252DC = 0 only for the first stage of a new game (DIRECTOR title step 0): Sho
         * drops in from y = 1248; later stages start on the checkpoint position */
        W16(0x060252DCu, idx == 0 ? 0 : 1);
        W16(0x0602529Eu, 0);                                /* stage state */
        Shinobi_InitVdp2();
        W32(0x06024058u, 0); W32(0x0602405Cu, 0); W32(0x06024060u, 0x10000); W32(0x06024064u, 0x10000);
        W32(SPR_ZOOM, 0x10000);
        Player_Init();
        if (!Stage_Load(dir, idx)) { printf("%2d: load FAILED\n", idx); failures++; continue; }
        int nobj = Obj_Count();
        Stage_SpawnPlayer();
        W16(0x0603A570u, R16(0x0603A570u) | 0x8000);        /* display on (0x06005BA0) */
        int16_t coll = RS16(G_COLL_LAYER);
        int frames = 0, landedAt = -1;
        for (; frames < 120; frames++) {
            tick();
            if (RS16(0x0602526Au) != idx) break;            /* stage ended */
            if (landedAt < 0 && !(P16(P_FLAGS2) & 0x4000)) landedAt = frames;
        }
        int landed = !(P16(P_FLAGS2) & 0x4000);
        char lays[96]; int ok = 1, o = 0;
        for (int l = 0; l < 3; l++) {
            int w, h, mx;
            int good = layout_ok(l, &w, &h, &mx);
            ok &= good;
            o += snprintf(lays + o, sizeof lays - (size_t)o, "%dx%d,%d%s ", w, h, mx, good ? "" : "!");
        }
        const uint32_t C = STAGE_CTRL_ADDR;
        printf("%2d  %d-%d  %s  %08X  %04X  %3d  %d%d%d%d/%d%d%d%d  %-30s %4d  %7.1f,%7.1f  %s",
               idx, idx / 2 + 1, idx % 2 + 1, kFiles[idx], kHandlers[idx], R16(C + 0x20), coll,
               R16(C + 0x2A), R16(C + 0x2C), R16(C + 0x2E), R16(C + 0x30),
               R16(C + 0x22), R16(C + 0x24), R16(C + 0x26), R16(C + 0x28), lays, nobj,
               P32(P_X) / 65536.0, P32(P_Y) / 65536.0, landed ? "yes" : "NO");
        if (landedAt >= 0) printf(" (frame %d)", landedAt);
        if (frames < 120) printf(" [stage ended at frame %d]", frames);
        printf("\n");
        if (!ok) printf("    note: a layout names a page past the layer's MAP data (never on screen: outside the\n"
                        "          world bounds or on a fixed layer; the hardware would show the next layer's page)\n");
        if (coll < 0) printf("    note: no map collision layer (C+0x32 = -1): Sho stands on the ledge objects 0x1025\n"
                             "          (objhandlers_d.c)\n");
        if (nobj <= 0) { printf("    no objects placed\n"); failures++; }
        if (!landed) { printf("    Sho did not land\n"); failures++; }   /* 4-2 included: ledges 0x1025 */
        /* NBG3 status bar built from 0x060CE5B8 */
        if (R32(NBG3_PN_ADDR) != 0x00002C00u + (R16(HUD_MAP_BUF) & 0x3FF) + ((uint32_t)(R16(HUD_MAP_BUF) & 0xF000) << 4)) failures++;
        if (shots) {
            Soft_ComposeFrame(fb, g_spriteLog, g_spriteLogCount, 0xFF000000u);
            snprintf(p, sizeof p, "%s/stage%02d_%s.png", shots, idx, kFiles[idx]);
            if (!Soft_WritePng(p, fb, SCREEN_W, SCREEN_H)) printf("    could not write %s\n", p);
        }
    }
    if (failures) { printf("%d stage check(s) failed\n", failures); return 1; }
    printf("all stage tests OK\n");
    return 0;
}
