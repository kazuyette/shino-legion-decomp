/* HUD / status bar (hud.c, SHINOBI 0x06061A10): loads stage index 0 (STAGE7_1, game 1-1) on the
 * real data, runs stage frames with Hud_Update as Game_StageFrame does, changes lives /
 * shurikens / 1-up tokens / HP / ninjutsu in RAM and checks the NBG3 pattern names and the HUD
 * sprites. Also unit-checks the number formats of 0x06061D8E. With a second argument (a file
 * name) it writes a software-rendered screenshot (soft_render.c) of the last frame.
 *   gcc -std=c11 -Isrc -Itests tests/test_hud.c $(cat tests/SOURCES.txt) -lm
 *   ./a.out ../extracted/ [hud.png] */
#include <stdio.h>
#include <stdlib.h>
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
#include "hud.h"
extern RenderSprite g_spriteLog[]; extern int g_spriteLogCount;
static InputPort fake[2];
const InputPort *Input_GetPort(int p) { return &fake[p]; }

static int failures;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); failures++; } } while (0)

static uint32_t cell(int x, int y) { return R32(HUD_NBG3_PAGE + (uint32_t)y * 256u + (uint32_t)x * 4u); }
static uint32_t dtop(int d) { return 0x2C00u | R16(HUD_DIGITS_TBL + 4u * (uint32_t)d); }
static uint32_t dbot(int d) { return 0x2C00u | R16(HUD_DIGITS_TBL + 4u * (uint32_t)d + 2u); }

/* digits expected at (x,1)/(x,2): string of '0'-'9' or ' ' (blank = digit 10) */
static void expect_number(int x, const char *s, const char *what)
{
    for (int i = 0; s[i]; i++) {
        int d = s[i] == ' ' ? 10 : s[i] - '0';
        CHECK(cell(x + i, 1) == dtop(d) && cell(x + i, 2) == dbot(d),
              "%s: cell %d = %08X/%08X, want digit %d (%08X/%08X)", what, x + i,
              cell(x + i, 1), cell(x + i, 2), d, dtop(d), dbot(d));
    }
}

/* one stage frame, as Game_StageFrame (shinobi.c): HUD first, then the stage tick */
static void tick(void)
{
    fake[0].connected = 1; fake[0].raw = 0xFFFF;
    g_spriteLogCount = 0;
    W16(0x060143EAu, R16(0x060143EAu) + 1u); W16(0x060F4CCCu, R16(0x060F4CCCu) + 1u);
    Pad_Update(2);
    Hud_Update();
    Scroll_Update();
    Game_LatchPad1(P16(P_FLAGS4));
    Player_Tick();
    Stage_UpdateState();
    if (R16(0x060252DCu) == 0 && !(P16(P_FLAGS2) & 0x4000)) {
        W16(0x060252DCu, 1);
        PW16(P_FLAGS4, P16(P_FLAGS4) & 0xFFEF);
    }
    Obj_UpdateEnemies(); Obj_UpdateAll(); Projectiles_Update(); Effects_Update(); Items_Update();
}

static int count_sprites(uint32_t charId)
{
    uint32_t src = VDP1_VRAM_BASE + ((uint32_t)R16(CHAR_TABLE + charId * 10u) << 3);
    int n = 0;
    for (int i = 0; i < g_spriteLogCount; i++) if (g_spriteLog[i].src == src) n++;
    return n;
}

/* life bar row 1 from column 6: 12 cells of 8 units + end cap */
static void expect_bar(int fill, int colour, const char *what)
{
    uint32_t tbl = R32(HUD_BAR_TBLS + 4u * (uint32_t)colour);
    for (int n = 0; n < 12; n++) {
        int part = fill >= 8 ? 8 : fill; fill -= part;
        CHECK(cell(6 + n, 1) == (0x2C00u | R16(tbl + 4u * (uint32_t)part)) &&
              cell(6 + n, 2) == (0x2C00u | R16(tbl + 4u * (uint32_t)part + 2u)),
              "%s: bar cell %d = %08X, want part %d colour %d", what, n, cell(6 + n, 1), part, colour);
    }
    CHECK(cell(18, 1) == 0x2C25u && cell(18, 2) == 0x2C35u, "%s: bar end cap", what);
}

static void unit_number_formats(void)
{
    Hud_DrawNumber(40, 1, 7, 3, 0);    expect_number(40, "  7", "right-aligned 7");
    Hud_DrawNumber(40, 1, 0, 3, 0);    expect_number(40, "  0", "right-aligned 0");
    Hud_DrawNumber(40, 1, 45, 3, 1);   expect_number(40, "045", "zero-padded 45");
    Hud_DrawNumber(40, 1, 45, 3, 2);   expect_number(40, "45 ", "left-aligned 45");
    Hud_DrawNumber(40, 1, 0, 3, 2);    expect_number(40, "0  ", "left-aligned 0");
    Hud_DrawNumber(40, 1, 1234, 3, 2); expect_number(40, "234", "left-aligned 1234 (3 digits)");
    Hud_DrawNumber(40, 1, 105, 3, 2);  expect_number(40, "105", "left-aligned 105 (inner zero)");
    Hud_DrawNumber(40, 1, 9, 2, 6);
    CHECK(cell(40, 1) == (dtop(9) | 0x80000u) && cell(41, 1) == dtop(10), "flag 4: palette 8 on digits only");
    Hud_PutDigit(63, 1, 5, 0);
    CHECK(cell(63, 1) == dtop(5) && cell(63, 2) == dbot(5), "PutDigit column 63");
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "../extracted/";
    const char *shot = argc > 2 ? argv[2] : NULL;
    char p[512];
    snprintf(p, sizeof p, "%sA.BIN", dir);
    if (!Ram_LoadImage(p, 0x06004000u)) { printf("cannot load %s\n", p); return 1; }
    snprintf(p, sizeof p, "%sSHINOBI.dec.bin", dir); Ram_LoadImage(p, 0x06040000u);
    snprintf(p, sizeof p, "%sSHOU.CHR", dir);        Ram_LoadImage(p, 0x00200000u);

    /* new game defaults (Boot_DevStartStage) */
    int diff = 1;
    W16(0x060252C4u, (uint32_t)diff);
    W16(0x060252B0u, R16(0x06014424u + 2u * (uint32_t)diff));
    W16(0x060252B8u, R16(0x0601442Au + 2u * (uint32_t)diff));
    W16(0x060252B2u, 0); W16(0x060252BAu, 0); W16(0x060252BEu, 0);
    W16(0x060252B6u, R16(0x060252B4u));
    W32(0x060252C8u, 0);
    W16(G_STAGE_INDEX, 0);
    W16(0x060EC658u, 0); W16(0x060EC65Au, 0);
    W16(0x060252DCu, 0);
    W16(0x0602529Eu, 0);
    Shinobi_InitVdp2();
    W32(0x06024058u, 0); W32(0x0602405Cu, 0); W32(0x06024060u, 0x10000); W32(0x06024064u, 0x10000);
    W32(SPR_ZOOM, 0x10000);
    Player_Init();
    if (!Stage_Load(dir, 0)) { printf("stage 0 load failed\n"); return 1; }
    Stage_SpawnPlayer();
    W16(0x0603A570u, R16(0x0603A570u) | 0x8000);
    int hpMax = RS16(0x060252B0u);
    printf("HP max %d, HP %d, lives %d, shurikens %d\n", hpMax, RS16(PLAYER + P_HP), RS16(0x060252B8u), RS16(0x060252B6u));
    CHECK(hpMax > 1, "HP max");

    /* before any HUD update: digits area is the static map (dashes) */
    uint32_t before = cell(37, 1);
    for (int f = 0; f < 30; f++) tick();
    CHECK(RS16(G_STAGE_INDEX) == 0, "still on stage 0");

    /* frame values: drawn by the next Hud_Update */
    W16(0x060252B8u, 3); W16(0x060252B6u, 150); W16(0x060252B2u, 4);
    PW16(P_HP, (uint16_t)hpMax);
    W16(0x060252BEu, 0);
    tick();
    expect_number(37, "3 ", "lives 3");
    expect_number(25, "150", "shurikens 150");
    expect_number(32, "4", "1-up tokens 4");
    CHECK(before != cell(37, 1), "lives cell changed from the static map (%08X)", before);
    expect_bar(95, 0, "full HP");
    CHECK(count_sprites(0x4DD) == hpMax - 1, "HP ticks: %d sprites, want %d", count_sprites(0x4DD), hpMax - 1);
    CHECK(count_sprites(0x4E5) == 0, "no ninjutsu icon");

    W16(0x060252B8u, 12); W16(0x060252B6u, 7); W16(0x060252B2u, 0);
    PW16(P_HP, 0);
    W16(0x060252BEu, 1);
    g_spriteLogCount = 0;
    Hud_Update();
    expect_number(37, "12", "lives 12");
    expect_number(25, "7  ", "shurikens 7");
    expect_number(32, "0", "1-up tokens 0");
    expect_bar(0, 3, "HP 0");
    CHECK(count_sprites(0x4E5) == 1, "ninjutsu icon");

    PW16(P_HP, (uint16_t)(hpMax / 2));
    Hud_Update();
    {
        int f = ((hpMax / 2) * 256 / hpMax * 95 + 0x80) / 256, c = 3 - f / 23;
        if (c < 0) c = 0;
        expect_bar(f, c, "half HP");
        printf("half HP: fill %d colour %d\n", f, c);
    }

    /* boss bar (rows 3-4) */
    Hud_DrawBossBar(40, 10);
    {
        uint32_t tbl = R32(HUD_BAR_TBLS + 4u * 2u);   /* fill 24 -> colour 3 - 1 = 2 */
        CHECK(cell(6, 3) == (0x2C00u | R16(tbl + 32u)) && cell(9, 3) == (0x2C00u | R16(tbl)), "boss bar fill");
        CHECK(cell(1, 3) == (0x2C00u | R16(HUD_LABEL_TBL)) && cell(1, 4) == (0x2C00u | R16(HUD_LABEL_TBL + 10u)), "boss label");
    }

    /* back to the game values, draw a real frame for the screenshot */
    W16(0x060252B8u, 3); W16(0x060252B6u, 150); W16(0x060252B2u, 4); PW16(P_HP, (uint16_t)(hpMax * 3 / 4));
    tick();
    if (shot) {
        static uint32_t fb[SCREEN_W * SCREEN_H];
        Soft_ComposeFrame(fb, g_spriteLog, g_spriteLogCount, 0xFF000000u);
        if (!Soft_WritePng(shot, fb, SCREEN_W, SCREEN_H)) printf("could not write %s\n", shot);
        else printf("wrote %s\n", shot);
    }

    unit_number_formats();
    if (failures) { printf("%d HUD check(s) failed\n", failures); return 1; }
    printf("all HUD tests OK\n");
    return 0;
}
