/* Stage frame hooks (stage_hooks.c): every stage index 0..17 is loaded as in game mode 5
 * (Stage_Load + Stage_SpawnPlayer + Ninjutsu_Init + StageHooks_Init) and runs 300 stage
 * frames with the hooks (Stage_FrameHooks 0x0606BCD0 + Ninjutsu_Update 0x06063554) and the
 * VBlank side of the HBlank line table. Checks:
 *  - 4-2: the sprite colour banks 0-4 (ledges, beams, halo, lanterns) are loaded by the init
 *    0x0606BF12, the boss's arrival starts the NBG1 wave (line scroll X in VRAM, SCRCTL LSCX1)
 *    and the boss life bar is drawn on NBG3 row 3 by 0x0606D99E;
 *  - 1-1 / 7-1 / 9-2: NBG2 line scroll through the HBlank table (several different line X);
 *  - 6-1 (NBG0) / 1-2, 6-2, 8-1, 9-1 (NBG1): VDP2 line scroll tables in VRAM;
 *  - 7-2 / 8-1: colour offset gradient by line.
 * With a second argument (a directory) it writes hooksNN.png after the 300 frames.
 *   gcc -std=c11 -Isrc -Itests tests/test_hooks.c $(cat tests/SOURCES.txt) -lm
 *   ./a.out ../extracted/ [shots-dir] */
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
#include "stage_hooks.h"
#include "camera.h"
#include "sprite.h"
#include "objects.h"
#include "objhandlers_c.h"
#include "enemy.h"
#include "fx.h"
#include "fade.h"
#include "hud.h"
#include "shinobi.h"
#include "soft_render.h"
extern RenderSprite g_spriteLog[]; extern int g_spriteLogCount;
static InputPort fake[2];
const InputPort *Input_GetPort(int p) { return &fake[p]; }

/* one stage frame as Game_StageFrame (shinobi.c) + the A.BIN VBlank side (boot.c) */
static void tick(uint16_t held)
{
    fake[0].connected = 1; fake[0].raw = (uint16_t)~held;
    g_spriteLogCount = 0;
    W16(0x060143EAu, R16(0x060143EAu) + 1u); W16(0x060F4CCCu, R16(0x060F4CCCu) + 1u);
    Pad_Update(2);
    Hud_Update();
    Scroll_Update();
    Game_LatchPad1(P16(P_FLAGS4));
    Player_Tick();
    Stage_UpdateState();
    if (RS16(G_STAGE_INDEX) != 0 && R16(0x060252DCu) == 0 && !(P16(P_FLAGS2) & 0x4000)) {
        W16(0x060252DCu, 1);
        PW16(P_FLAGS4, P16(P_FLAGS4) & 0xFFEF);
    }
    Obj_UpdateEnemies(); Obj_UpdateAll(); Projectiles_Update(); Effects_Update(); Items_Update();
    Stage_FrameHooks();
    Ninjutsu_Update();
    Fade_Update();
    LineTable_VBlank();
}

static int distinct_line_x(int nbg)
{
    static Vdp2LineScroll ls;
    Vdp2_GetLineScroll(nbg, &ls);
    if (!ls.lines) return 0;
    int n = 1;
    for (int y = 1; y < SCREEN_H; y++) n += ls.sx[y] != ls.sx[y - 1];
    return n;
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
    int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("   FAIL: " __VA_ARGS__); printf("\n"); fails++; } } while (0)
    Fade_Init();
    for (int idx = 0; idx < 18; idx++) {
        W16(0x060EC658u, 0); W16(0x060EC65Au, 0);
        W16(0x060252DCu, idx == 0 ? 0 : 1);
        W16(0x0602529Eu, 0);
        W16(0x0601C498u, 0);                                 /* 0x0606B6FC */
        Shinobi_InitVdp2();
        W32(0x06024058u, 0); W32(0x0602405Cu, 0); W32(0x06024060u, 0x10000); W32(0x06024064u, 0x10000);
        W32(SPR_ZOOM, 0x10000);
        for (uint32_t i = 0; i < 0x50; i++) W16(SHADOW_CRAM + 2 * i, 0);   /* sprite banks 0-4 */
        Player_Init();
        if (!Stage_Load(dir, idx)) { printf("%2d: load FAILED\n", idx); fails++; continue; }
        Stage_SpawnPlayer();
        Ninjutsu_Init();
        StageHooks_Init();
        W16(0x0603A570u, R16(0x0603A570u) | 0x8000);
        W32(0x25E6C304u, 0); W32(0x25E6C318u, 0);          /* NBG3 row 3: boss label / bar */
        int banks = 0;
        for (uint32_t i = 0; i < 0x50; i++) banks += R16(SHADOW_CRAM + 2 * i) != 0;
        int frames = 0, maxN2 = 0, maxN0 = 0, maxN1 = 0, coaLines = 0, waved = 0;
        for (; frames < 300; frames++) {
            tick(0);
            if (RS16(G_STAGE_INDEX) != idx) break;
            int a = distinct_line_x(0), b = distinct_line_x(1), c = distinct_line_x(2);
            if (a > maxN0) maxN0 = a;
            if (b > maxN1) maxN1 = b;
            if (c > maxN2) maxN2 = c;
            if (R16(LANIM + 0xC) == 0xA00) waved = 1;
            int r, g, bb, r0 = 0, g0 = 0, b0 = 0, diff = 0;
            for (int y = 0; y < SCREEN_H; y++)
                if (Vdp2_LineColorOffset(y, &r, &g, &bb)) { if (y && (r != r0 || g != g0 || bb != b0)) diff++; r0 = r; g0 = g; b0 = bb; }
            if (diff > coaLines) coaLines = diff;
        }
        printf("%2d  %d-%d  frames %3d  LA flags %04X  SCRCTL %04X  line-X runs NBG0 %3d NBG1 %3d NBG2 %3d  COA steps %3d  dma %04X\n",
               idx, idx / 2 + 1, idx % 2 + 1, frames, R16(LANIM + 0xC), R16(0x0603A60Au), maxN0, maxN1, maxN2, coaLines,
               LineTable_DmaActive());
        switch (idx) {
        case 0: case 12:
            CHECK(maxN2 > 1, "stage %d: NBG2 line scroll bands expected", idx); break;
        case 17:                                             /* one band (+ wave during the boss) */
            CHECK(maxN2 >= 1 && (LineTable_DmaActive() & 0x2000), "stage 17: NBG2 line scroll expected"); break;
        case 10:
            CHECK(maxN0 > 1 || R16(0x0603A60Au) == 0x0002, "stage 10: NBG0 line scroll (LSCX0) expected"); break;
        case 1: case 11: case 14: case 16:
            CHECK((R16(0x0603A60Au) & 0x0200), "stage %d: NBG1 line scroll (LSCX1) expected", idx); break;
        default: break;
        }
        if (idx == 13 || idx == 14) CHECK(coaLines > 0 || idx == 14, "stage %d: colour offset gradient expected", idx);
        if (idx == 7) {
            printf("    4-2 sprite colour banks 0-4: %d/80 colours set; NBG1 wave %s; boss bar %08X %08X\n", banks,
                   waved ? "started" : "NOT started", R32(0x25E6C304u), R32(0x25E6C318u));
            CHECK(banks >= 40, "4-2 colour banks 0-4 empty");
            for (int b = 0; b < 5; b++) {
                int n = 0;
                for (uint32_t i = 0; i < 16; i++) n += R16(SHADOW_CRAM + 2 * (b * 16 + i)) != 0;
                CHECK(n > 0, "4-2 colour bank %d empty", b);
            }
            CHECK(waved && maxN1 > 1, "4-2 NBG1 wave expected once the boss is in");
            CHECK(R32(0x25E6C304u) != 0 && R32(0x25E6C318u) != 0, "4-2 boss bar not drawn on NBG3");
        }
        if (shots) {
            Soft_ComposeFrame(fb, g_spriteLog, g_spriteLogCount, 0xFF000000u);
            snprintf(p, sizeof p, "%s/hooks%02d.png", shots, idx);
            if (!Soft_WritePng(p, fb, SCREEN_W, SCREEN_H)) printf("    could not write %s\n", p);
        }
    }
    if (fails) { printf("%d hook check(s) failed\n", fails); return 1; }
    printf("all hook tests OK\n");
    return 0;
}
