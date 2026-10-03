/* Headless boot through DIRECTOR (needs extracted/ files: A.BIN, DIRECTOR.dec.bin or
 * DIRECTOR.PPB, SHINOBI.dec.bin or SHINOBI.PPB, SHOU.CHR, TITLE.PBC, STGCLEAR.PBC, STAGE7_1.*):
 * SEGA logo -> title -> Start -> stage start -> story -> SHINOBI 1-1 running -> forced
 * stage clear on an act 2 -> back in DIRECTOR's tally -> next stage. Also checks the font.
 * S=$(cat tests/SOURCES.txt); gcc -std=c11 -Isrc -Itests tests/test_director.c $S */
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "input.h"
#include "pad.h"
#include "ram.h"
#include "render.h"
#include "sprite.h"
#include "boot.h"
#include "resource.h"
#include "director_int.h"
#include "objhandlers_c.h"
#include "stage.h"
#include "player.h"

extern RenderSprite g_spriteLog[]; extern int g_spriteLogCount;
static InputPort fake[2];
const InputPort *Input_GetPort(int p) { return &fake[p]; }

static int frame_no;
static char seq[2048];

/* one main-loop frame; prints every change of overlay / DIRECTOR scene / SHINOBI mode */
static void frame(uint16_t held)
{
    static int lastOvl = -1, lastScene = -1, lastMode = -1;
    fake[0].connected = 1; fake[0].raw = (uint16_t)~held;
    fake[1].connected = 0;
    g_spriteLogCount = 0;
    Boot_Frame();
    frame_no++;
    int o = Res_OverlayResident();
    int sc = (int)R32(G_DIR_SCENE), gm = R16(G_GAME_MODE);
    if (o == OVL_DIRECTOR && (o != lastOvl || sc != lastScene)) {
        char b[64];
        printf("frame %5d: DIRECTOR scene %d\n", frame_no, sc);
        snprintf(b, sizeof b, "D%d ", sc); strcat(seq, b);
    }
    if (o == OVL_SHINOBI && (o != lastOvl || gm != lastMode)) {
        char b[64];
        printf("frame %5d: SHINOBI game mode %d (stage %d)\n", frame_no, gm, RS16(V_STAGE));
        snprintf(b, sizeof b, "S%d ", gm); strcat(seq, b);
    }
    lastOvl = o; lastScene = sc; lastMode = gm;
}

static int run_until(int (*cond)(void), uint16_t held, int max)
{
    for (int i = 0; i < max; i++) { if (cond()) return i; frame(held); }
    return cond() ? max : -1;
}
static int in_title_loop(void) { return Res_OverlayResident() == OVL_DIRECTOR && R32(G_DIR_SCENE) == 1 && R32(D_STEP) == 4 && R16(FADE_STATE) == 0; }
static int logo_shown(void) { return Res_OverlayResident() == OVL_DIRECTOR && R32(G_DIR_SCENE) == 0 && R32(D_STEP) == 2; }
static int stage_running(void) { return Res_OverlayResident() == OVL_SHINOBI && R16(G_GAME_MODE) == 6 && R16(FADE_STATE) == 0; }
static int in_tally(void) { return Res_OverlayResident() == OVL_DIRECTOR && R32(G_DIR_SCENE) == 8 && R32(D_STEP) == 1; }

static int find_sprite(int w, int h, int mode)
{
    for (int i = 0; i < g_spriteLogCount; i++)
        if (g_spriteLog[i].w == w && g_spriteLog[i].h == h && g_spriteLog[i].mode == mode) return i;
    return -1;
}
static int nonzero(uint32_t a, int n) { int k = 0; for (int i = 0; i < n; i++) k += Ram_Ptr(a)[i] != 0; return k; }

static void check_font(void)
{
    /* glyph indexes for "SCORE 0123:-." (space skipped) */
    static const int glyph[] = { 28, 12, 24, 27, 14, -1, 0, 1, 2, 3, 37, 38, 36 };
    g_spriteLogCount = 0;
    Font_DrawText("SCORE 0123:-.", 0, 88, 112);
    printf("font: \"SCORE 0123:-.\" -> %d sprites:", g_spriteLogCount);
    assert(g_spriteLogCount == 12);
    int k = 0;
    for (int i = 0; i < 13; i++) {
        if (glyph[i] < 0) continue;
        const RenderSprite *s = &g_spriteLog[k++];
        uint32_t e = CHAR_TABLE + (uint32_t)(0x200 + glyph[i]) * 10;
        printf(" %d@%d", glyph[i], s->x);
        assert(s->x == 88 + 8 * i && s->y == 112);
        assert(s->w == 8 && s->h == 16 && (s->mode & 0x38) == 0);          /* 8x16 4bpp bank */
        assert(s->src == VDP1_VRAM_BASE + ((uint32_t)R16(e) << 3));
        assert(((s->colr >> 4) & 0x7F) == 64);                            /* colour bank 64 */
        assert(nonzero(s->src, 64) > 0);                                  /* glyph pixels there */
    }
    printf("\n");
    assert(R16(SHADOW_CRAM + 2u * (64 * 16 + 1)) != 0 || R16(SHADOW_CRAM + 2u * (64 * 16 + 2)) != 0);
    g_spriteLogCount = 0;
    Font_DrawNumber(1234, 0, 0, 0, 8, 1);                                 /* leading zeros blank */
    assert(g_spriteLogCount == 4 && g_spriteLog[0].x == 0 && g_spriteLog[3].x == 24);
    g_spriteLogCount = 0;
    Font_DrawNumber(1234, 0, 0, 0, 8, 0);
    assert(g_spriteLogCount == 8 && g_spriteLog[4].src == VDP1_VRAM_BASE + ((uint32_t)R16(CHAR_TABLE + 0x201 * 10) << 3));
    g_spriteLogCount = 0;
    Font_DrawHex(0x1F, 0, 0, 0, 4, 1);                                    /* 'F' -> d + 65 = 'P' */
    assert(g_spriteLogCount == 2 && g_spriteLog[1].src == VDP1_VRAM_BASE + ((uint32_t)R16(CHAR_TABLE + (0x200 + 25) * 10) << 3));
    printf("font numbers OK (1234 blank/zero-padded, hex)\n");
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "../extracted/";
    char p[512];
    snprintf(p, sizeof p, "%s/A.BIN", dir);
    assert(Ram_LoadImage(p, 0x06004000u));
    Res_SetAssetDir(dir);

    Boot_Init(1);                                     /* PC: DIRECTOR's logo stands in for the BIOS */
    assert(R16(RES_QCOUNT) == 1);

    int n = run_until(logo_shown, 0, 200);
    printf("SEGA logo after %d frames\n", n); assert(n >= 0);
    frame(0);
    int i = find_sprite(320, 224, 0x28);
    assert(i >= 0 && nonzero(g_spriteLog[i].src, 2000) > 1000);
    printf("logo sprite: 320x224 RGB at VRAM %08X\n", (unsigned)g_spriteLog[i].src);

    n = run_until(in_title_loop, 0, 600);
    printf("title loop after %d more frames\n", n); assert(n >= 0);
    frame(0);
    i = find_sprite(320, 224, 0x28);
    assert(i >= 0 && nonzero(g_spriteLog[i].src, 2000) > 1000);       /* TITLE.PBC char 0 */
    {   /* TITLE.PBC char list: 0 bg 320x224 RGB ... 6 PRESS START 152x13 */
        uint32_t e = CHAR_TABLE + 6 * 10;
        assert(((R16(e + 2) >> 5) & 0x7F8) == 152 && (R16(e + 2) & 0xFF) == 13);
        assert(R16(SHADOW_CRAM + 2) != 0 || R16(SHADOW_CRAM + 4) != 0);   /* title palette */
    }
    assert(RS16(V_STAGE) == 0 && RS16(V_LIVES) == 4 && RS16(V_CONTINUES) == 5 && RS16(V_HP_MAX) == 6);

    frame(PAD_START); frame(0);                       /* GAME START */
    n = run_until(stage_running, 0, 2000);
    printf("stage running after %d frames\n", n); assert(n >= 0);
    assert(RS16(G_STAGE_INDEX) == 0 && RS16(V_NOT_FIRST) == 0);
    assert(R32(V_BONUS_SHUR) == 5000 && R32(V_BONUS_DMG) == 1);
    for (int k = 0; k < 120; k++) frame(0);
    printf("1-1 (STAGE7_1): player at %.0f,%.0f, mode %d, %d sprites this frame, dropped in: %s\n",
           P32(P_X) / 65536.0, P32(P_Y) / 65536.0, R16(G_GAME_MODE), g_spriteLogCount,
           RS16(V_NOT_FIRST) ? "yes" : "no");
    assert(R16(G_GAME_MODE) == 6 && g_spriteLogCount > 0);

    /* force a stage clear at the end of an act 2 (stage index 1) */
    W16(G_STAGE_INDEX, 1);
    W16(G_STAGE_STATE, (R16(G_STAGE_STATE) & 0xFFF8) | 10);
    n = run_until(in_tally, 0, 600);
    printf("tally after %d frames: stage now %d, bonuses LIFE %u NO SHURIKEN %u NO DAMAGE %u, score %u\n", n,
           RS16(V_STAGE), (unsigned)R32(V_BONUS_LIFE), (unsigned)R32(V_BONUS_SHUR), (unsigned)R32(V_BONUS_DMG),
           (unsigned)R32(V_SCORE));
    assert(n >= 0 && RS16(V_STAGE) == 2);
    frame(0);
    i = find_sprite(320, 224, 0x28);
    assert(i >= 0 && nonzero(g_spriteLog[i].src, 2000) > 1000);       /* STGCLEAR.PBC bg (char 10) */
    check_font();

    n = run_until(stage_running, PAD_START, 3000);   /* tally -> story -> stage start -> SHINOBI */
    printf("next stage running after %d frames (stage index %d)\n", n, RS16(V_STAGE));
    assert(n >= 0 && RS16(V_STAGE) == 2);

    printf("sequence: %s\n", seq);
    assert(strstr(seq, "D0 D1 D2 D4 S0 S3 S4 S5 S6 ") == seq);
    assert(strstr(seq, "S7 S11 D8 D4 D2 S0 S3 S4 S5 S6") != NULL);
    printf("all director tests OK\n");
    return 0;
}
