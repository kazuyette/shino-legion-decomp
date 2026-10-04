/* DIRECTOR end screens (work packages D5 / D6): stage clear tally (scene 8), continue /
 * game over (scene 5) and staff roll (scene 7). Headless boot to the title, then each scene
 * is entered directly by writing g_DirScene / the variables as SHINOBI does before its
 * hand-off. Movies are the test stub (finished at once); the CNTCOUNT countdown is kept
 * "playing" by pinning the movie state to 3 (a state Movie_Update ignores).
 * S=$(cat tests/SOURCES.txt); gcc -std=c11 -Isrc -Itests tests/test_director_end.c $S */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include "input.h"
#include "pad.h"
#include "ram.h"
#include "render.h"
#include "sprite.h"
#include "boot.h"
#include "resource.h"
#include "director_int.h"
#include "movie.h"

extern RenderSprite g_spriteLog[]; extern int g_spriteLogCount;
static InputPort fake[2];
const InputPort *Input_GetPort(int p) { return &fake[p]; }

static const char *g_dir;
static uint16_t pinMovie;                /* 1: keep a started movie "playing" */

static void frame(uint16_t held)
{
    fake[0].connected = 1; fake[0].raw = (uint16_t)~held;
    fake[1].connected = 0;
    g_spriteLogCount = 0;
    if (pinMovie && RS32(MOVIE_STATE) == 2) W32(MOVIE_STATE, 3);
    Boot_Frame();
}
static void press(uint16_t b) { frame(b); }   /* edge on this frame; next frame(0) releases */

static int in_title_loop(void) { return Res_OverlayResident() == OVL_DIRECTOR && R32(G_DIR_SCENE) == 1 && R32(D_STEP) == 4 && R16(FADE_STATE) == 0; }

/* font text drawn this frame, one line per y ("x:TEXT"), glyph found from the sprite source */
static char glyph_of(uint32_t src)
{
    static const char *g = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ.:-";
    for (int i = 0; i < 39; i++) {
        uint32_t e = CHAR_TABLE + (uint32_t)(0x200 + i) * 10;
        if (src == VDP1_VRAM_BASE + ((uint32_t)R16(e) << 3)) return g[i];
    }
    return 0;
}
static int cmp_xy(const void *a, const void *b)
{
    const RenderSprite *p = a, *q = b;
    return p->y != q->y ? p->y - q->y : p->x - q->x;
}
static void font_lines(char *out, size_t n)
{
    RenderSprite f[512]; int k = 0;
    for (int i = 0; i < g_spriteLogCount && k < 512; i++)
        if (g_spriteLog[i].w == 8 && g_spriteLog[i].h == 16 && glyph_of(g_spriteLog[i].src)) f[k++] = g_spriteLog[i];
    qsort(f, (size_t)k, sizeof f[0], cmp_xy);
    out[0] = 0;
    int lastY = -9999, lastX = 0;
    for (int i = 0; i < k; i++) {
        char b[32];
        if (f[i].y != lastY) { snprintf(b, sizeof b, "%s(%d,%d) ", lastY == -9999 ? "" : "\n    ", f[i].x, f[i].y); strncat(out, b, n - strlen(out) - 1); lastX = f[i].x - 8; }
        for (int x = lastX + 8; x < f[i].x; x += 8) strncat(out, " ", n - strlen(out) - 1);
        b[0] = glyph_of(f[i].src); b[1] = 0; strncat(out, b, n - strlen(out) - 1);
        lastY = f[i].y; lastX = f[i].x;
    }
}
static void print_text(const char *what)
{
    char t[2048]; font_lines(t, sizeof t);
    printf("  %s:\n    %s\n", what, t);
}
static int has_text(const char *s) { char t[2048]; font_lines(t, sizeof t); return strstr(t, s) != NULL; }
static int find_sprite_at(int x, int y, int w, int h)
{
    for (int i = 0; i < g_spriteLogCount; i++)
        if (g_spriteLog[i].x == x && g_spriteLog[i].y == y && g_spriteLog[i].w == w && g_spriteLog[i].h == h) return i;
    return -1;
}

/* ------------------------------------------------------------------ scene 8 */
static void enter_tally(int stage, uint32_t life, uint32_t shur, uint32_t dmg, uint32_t score, uint32_t hi, int lives)
{
    W16(V_DIFFICULTY, 1);                             /* NORMAL: extra life every 60000 */
    W16(V_STAGE, (uint32_t)stage);
    W32(V_BONUS_LIFE, life); W32(V_BONUS_SHUR, shur); W32(V_BONUS_DMG, dmg);
    W32(V_SCORE, score); W32(V_HISCORE, hi);
    W16(V_LIVES, (uint32_t)lives);
    Dir_SetScene(8);
}

static void test_tally(void)
{
    printf("== scene 8: stage clear (2-x reached, HP 6 -> LIFE 6000, NO SHURIKEN 5000, NO DAMAGE 10000)\n");
    enter_tally(2, 6000, 5000, 10000, 55000, 70000, 3);
    frame(0);                                         /* step 0: StgClear_Init */
    assert(R32(D_STEP) == 1 && RS16(D_TIMER) == 0);
    frame(0);                                         /* tally frame 1 */
    int bg = find_sprite_at(0, 0, 320, 224), dg = find_sprite_at(128, 8, 64, 88), bn = find_sprite_at(16, 24, 288, 64);
    printf("  bg %s, stage digit %s (char 0 = '1'), banner %s\n", bg >= 0 ? "ok" : "MISSING", dg >= 0 ? "ok" : "MISSING", bn >= 0 ? "ok" : "MISSING");
    assert(bg >= 0 && dg >= 0 && bn >= 0);
    print_text("text, tally frame 1");
    assert(has_text("HIGH SCORE") && has_text("00070000") && has_text("00055000") && has_text("LIFE BONUS") &&
           has_text("06000") && has_text("NO SHURIKEN BONUS") && has_text("05000") && has_text("NO DAMAGE BONUS") && has_text("10000"));
    for (int i = 2; i <= 160; i++) frame(i == 100 ? PAD_START : 0);   /* Start before the end: ignored */
    assert(RS16(D_TIMER) == 160 && R32(V_SCORE) == 55000 && R32(D_STEP) == 1);
    frame(0);                                         /* timer 161: first 3 x 100 */
    assert(R32(V_SCORE) == 55300 && R32(V_BONUS_LIFE) == 5900 && R32(V_BONUS_SHUR) == 4900 && R32(V_BONUS_DMG) == 9900);
    int extendAt = -1, f = 161, livesBefore = RS16(V_LIVES);
    while (R32(V_BONUS_LIFE) | R32(V_BONUS_SHUR) | R32(V_BONUS_DMG)) {
        frame(f == 200 ? PAD_A : 0);                  /* A while counting: ignored */
        f++;
        if (extendAt < 0 && RS16(V_LIVES) != livesBefore) extendAt = f;
    }
    printf("  bonuses counted by timer %d: score %u, hi-score %u, lives %d -> %d (extra life at timer %d)\n",
           f, (unsigned)R32(V_SCORE), (unsigned)R32(V_HISCORE), livesBefore, RS16(V_LIVES), extendAt);
    assert(f == 260 && R32(V_SCORE) == 76000 && R32(V_HISCORE) == 76000 && RS16(V_LIVES) == 4);
    assert(extendAt > 0);
    frame(0);                                         /* drawn before the last subtraction: next frame */
    print_text("text, after the count");
    assert(has_text("00076000") && has_text("00000"));
    press(PAD_C);                           /* C after the count: timer -> 480 */
    assert(RS16(D_TIMER) == 480 && R32(D_STEP) == 1);
    frame(0);                                         /* 481: fade out */
    assert(R32(D_STEP) == 2 && R16(FADE_STATE) == 2 && RS16(D_TIMER) == 0);
    int n = 0;
    while (R32(G_DIR_SCENE) == 8) { frame(0); n++; assert(n < 200); }
    printf("  C pressed -> fade out %d frames -> scene %u (story movie), DISP %s\n", n, (unsigned)R32(G_DIR_SCENE),
           (R16(VDP2_TVMD) & 0x8000) ? "on" : "off");
    assert(R32(G_DIR_SCENE) == 4 && R32(D_STEP) == 0 && !(R16(VDP2_TVMD) & 0x8000));

    printf("== scene 8, no input, end of the game (stage -1), hi-score kept, score clamp\n");
    enter_tally(-1, 1000, 0, 0, 99999950, 99999999, 99);
    frame(0); frame(0);
    assert(find_sprite_at(128, 8, 72, 88) >= 0 || find_sprite_at(128, 8, 64, 88) >= 0);
    {   /* stage -1 -> digit char 8 ("9") */
        int i = find_sprite_at(128, 8, 72, 88); if (i < 0) i = find_sprite_at(128, 8, 64, 88);
        uint32_t e = CHAR_TABLE + 8 * 10;
        assert(g_spriteLog[i].src == VDP1_VRAM_BASE + ((uint32_t)R16(e) << 3));
    }
    int t = 1;
    while (R32(D_STEP) == 1) { frame(0); t++; assert(t < 1000); }
    printf("  fade started at timer %d; score %u (clamped 99999990), hi-score %u, lives %d\n", t,
           (unsigned)R32(V_SCORE), (unsigned)R32(V_HISCORE), RS16(V_LIVES));
    assert(t == 481 && R32(V_SCORE) == 99999990 && R32(V_HISCORE) == 99999999 && RS16(V_LIVES) == 99);
    while (R32(G_DIR_SCENE) == 8) frame(0);
    assert(R32(G_DIR_SCENE) == 4);
}

/* ------------------------------------------------------------------ scene 5 */
static int lwram_matches(const char *file)
{
    char p[512]; snprintf(p, sizeof p, "%s/%s", g_dir, file);
    FILE *fp = fopen(p, "rb"); if (!fp) return 0;
    uint8_t b[256]; size_t n = fread(b, 1, sizeof b, fp); fclose(fp);
    return n == sizeof b && memcmp(b, Ram_Ptr(LWRAM_BASE), n) == 0;
}

static void enter_continue(int continues, int stage, uint32_t score)
{
    W16(V_DIFFICULTY, 1);
    W16(V_LIVES, 0xFFFF);                             /* SHINOBI game over: lives < 0 */
    W16(V_CONTINUES, (uint32_t)continues);
    W16(V_STAGE, (uint32_t)stage);
    W16(V_SHUR_INIT, 20); W16(V_SHURIKENS, 3); W16(V_SHUR_PICK, 7); W16(V_MAGIC_AVAIL, 1); W16(V_STAGE_STATE, 5);
    W32(V_SCORE, score); W32(V_HISCORE, 76000);
    Dir_SetScene(5);
}

static void test_continue(void)
{
    printf("== scene 5: continue (2 left, died in 3-2 = stage 5)\n");
    enter_continue(2, 5, 12345);
    pinMovie = 1;
    frame(0);                                         /* step 0 */
    assert(R32(D_STEP) == 1);
    frame(0);                                         /* step 1: CONTINUE.CPK -> LWRAM, CNTCOUNT */
    assert(R32(D_STEP) == 2 && RS16(MOVIE_PLAYLIST) == 13 && lwram_matches("CONTINUE.CPK"));
    frame(0);                                         /* timer 1 */
    int c = find_sprite_at(176, 16, 112, 16), d = find_sprite_at(288, 16, 16, 16);
    assert(c >= 0 && d >= 0);
    assert(g_spriteLog[d].src == VDP1_VRAM_BASE + ((uint32_t)R16(CHAR_TABLE + 2 * 10) << 3));   /* digit '2' */
    printf("  CREDIT- (112x16) at 176,16 and digit char 2 (16x16) at 288,16 drawn over the countdown\n");
    while (RS16(D_TIMER) < 48) frame(0);
    press(PAD_START);                                 /* timer 49: too early */
    assert(R32(D_STEP) == 2 && RS16(V_CONTINUES) == 2);
    while (RS16(D_TIMER) < 99) frame(0);
    press(PAD_START);                                 /* timer 100: continue */
    printf("  Start at timer 49 ignored, at timer 100 -> step %u, continues %d\n", (unsigned)R32(D_STEP), RS16(V_CONTINUES));
    assert(R32(D_STEP) == 3 && RS16(V_CONTINUES) == 1 && RS32(MOVIE_STATE) == -1);
    pinMovie = 0;
    frame(0);                                         /* step 3: playlist {0x100E} */
    assert(R32(D_STEP) == 4 && R16(MOVIE_PLAYLIST) == 0x100E);
    int n = 0;
    while (R32(G_DIR_SCENE) == 5) { frame(0); n++; assert(n < 50); }
    printf("  CONTINUE clip + SHOU.CHR reload, %d frames -> scene %u: stage %d, lives %d, shurikens %d, continues %d, score %u\n",
           n, (unsigned)R32(G_DIR_SCENE), RS16(V_STAGE), RS16(V_LIVES), RS16(V_SHURIKENS), RS16(V_CONTINUES), (unsigned)R32(V_SCORE));
    assert(R32(G_DIR_SCENE) == 2 && R32(D_STEP) == 0);
    assert(RS16(V_STAGE) == 4 && RS16(V_LIVES) == 4 && RS16(V_SHURIKENS) == 20 && RS16(V_SHUR_PICK) == 0 &&
           RS16(V_MAGIC_AVAIL) == 0 && RS16(V_STAGE_STATE) == 0 && R32(V_SCORE) == 12345);
    assert(lwram_matches("SHOU.CHR"));

    printf("== scene 5: countdown runs out (1 left)\n");
    enter_continue(1, 8, 4321);
    pinMovie = 1;
    frame(0); frame(0);
    while (RS16(D_TIMER) < 529) frame(0);
    press(PAD_START);                                 /* timer 530: too late */
    assert(R32(D_STEP) == 2 && RS16(V_CONTINUES) == 1);
    pinMovie = 0;
    W32(MOVIE_STATE, 2);                              /* let the stub finish the movie */
    frame(0);
    assert(R32(D_STEP) == 18 && RS16(D_TIMER) == 1);
    printf("  Start at timer 530 ignored; movie end -> step 18 (score screen over the last frame)\n");
    frame(0);
    print_text("score screen");
    assert(has_text("HIGH SCORE") && has_text("00076000") && has_text("00004321"));
    int t = 2;
    while (R32(D_STEP) == 18) { frame(0); t++; assert(t < 800); }
    printf("  no input: step 19 after timer %d\n", t);
    frame(0);
    assert(R32(G_DIR_SCENE) == 0 && R32(D_STEP) == 0 && RS16(V_CONTINUES) == 1);

    printf("== scene 5: no continue left -> GAMEOVER.CPK -> score -> logo\n");
    enter_continue(0, 3, 98765);
    frame(0);
    assert(R32(D_STEP) == 16);
    frame(0);
    assert(R32(D_STEP) == 17 && RS16(MOVIE_PLAYLIST) == 16);
    n = 0;
    while (R32(D_STEP) == 17) { frame(0); n++; assert(n < 20); }
    int cc[5];
    for (int i = 0; i < 40; i++) {
        frame(0);
        if (i % 8 == 0) cc[i / 8] = R16(SPR_WORK + 0x0E);
    }
    printf("  GAMEOVER movie (stub) %d frames; text colour calc at timer 0/8/16/24/32: %d %d %d %d %d\n",
           n, cc[0], cc[1], cc[2], cc[3], cc[4]);
    assert(cc[0] == 3 && cc[1] == 2 && cc[2] == 1 && cc[3] == 0 && cc[4] == 0);
    assert(has_text("00098765") && has_text("00076000"));
    press(PAD_START);                                 /* skip -> 600, fade */
    assert(RS16(D_TIMER) == 600 && R16(FADE_STATE) == 2);
    n = 0;
    while (R32(G_DIR_SCENE) == 5) { frame(0); n++; assert(n < 200); }
    printf("  Start -> fade %d frames -> scene %u\n", n, (unsigned)R32(G_DIR_SCENE));
    assert(R32(G_DIR_SCENE) == 0 && !(R16(VDP2_TVMD) & 0x8000));
}

/* ------------------------------------------------------------------ scene 7 */
static void test_staff_roll(void)
{
    printf("== scene 7: staff roll\n");
    W16(V_STAGE, 0xFFFF);
    Dir_SetScene(7);
    frame(0);
    assert(R32(D_STEP) == 1 && R32(0x060DD028u) == 0 && (R16(VDP2_TVMD) & 0x8000));
    int k = 0, maxVis = 0;
    while (R32(D_STEP) == 1) {
        frame(0); k++;
        int vis = 0;
        for (int i = 0; i < g_spriteLogCount; i++) if (g_spriteLog[i].w == 320 && g_spriteLog[i].y > -56 && g_spriteLog[i].y < 224) vis++;
        if (vis > maxVis) maxVis = vis;
        if (k == 600) {
            printf("  scroll frame 600: %d sprites queued, credit lines on screen:", g_spriteLogCount);
            for (int i = 0; i < g_spriteLogCount; i++) if (g_spriteLog[i].y > -24 && g_spriteLog[i].y < 224) printf(" y%d(%dx%d)", g_spriteLog[i].y, g_spriteLog[i].w, g_spriteLog[i].h);
            printf("\n");
        }
        assert(k < 10000);
    }
    printf("  end marker reached after %d scroll frames (scroll %08X), max %d lines on screen\n",
           k, (unsigned)R32(0x060DD028u), maxVis);
    assert(k == 6002 && R32(0x060DD028u) == 6002u * 0x8000u);
    {   /* drawn with the scroll before the last += 0x8000 (3000.5 px): last line (char 0x3E)
         * at (3096 - 3000.5) -> 95, end marker (char 0x1000, a zero-size char table entry) at 223 */
        printf("  last scroll frame:");
        for (int j = 0; j < g_spriteLogCount; j++) printf(" y%d(%dx%d)", g_spriteLog[j].y, g_spriteLog[j].w, g_spriteLog[j].h);
        printf("\n");
        assert(find_sprite_at(0, 95, 320, 24) >= 0);
    }
    frame(0);
    printf("  hold frame 1 (scroll 3001 px): %d sprites:", g_spriteLogCount);
    for (int i = 0; i < g_spriteLogCount; i++) printf(" y%d(%dx%d)", g_spriteLog[i].y, g_spriteLog[i].w, g_spriteLog[i].h);
    printf("\n");
    for (int i = 1; i < 50; i++) frame(0);
    press(PAD_START);                                 /* timer 51: ignored (<= 120) */
    assert(RS16(D_TIMER) == 51 && R32(D_STEP) == 2);
    while (RS16(D_TIMER) < 300) frame(0);
    press(PAD_START);                                 /* skip -> 900, fade */
    assert(RS16(D_TIMER) == 900);
    int n = 0;
    while (R32(G_DIR_SCENE) == 7) { frame(0); n++; assert(n < 200); }
    printf("  Start at hold 51 ignored, at 301 -> fade %d frames -> scene %u\n", n, (unsigned)R32(G_DIR_SCENE));
    assert(R32(G_DIR_SCENE) == 0 && !(R16(VDP2_TVMD) & 0x8000));

    printf("== scene 7: no input, hold timing\n");
    Dir_SetScene(7);
    while (R32(D_STEP) != 2) frame(0);
    int h = 0;
    while (R32(D_STEP) == 2 && R16(FADE_STATE) == 0) { frame(0); h++; }
    printf("  fade starts after %d hold frames\n", h);
    assert(h == 900);
    while (R32(G_DIR_SCENE) == 7) frame(0);
    assert(R32(G_DIR_SCENE) == 0);
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    g_dir = argc > 1 ? argv[1] : "../extracted/";
    char p[512];
    snprintf(p, sizeof p, "%s/A.BIN", g_dir);
    assert(Ram_LoadImage(p, 0x06004000u));
    Res_SetAssetDir(g_dir);
    Boot_Init(1);
    int n = 0;
    while (!in_title_loop()) { frame(0); n++; assert(n < 1000); }
    printf("title loop after %d frames\n", n);

    test_tally();
    test_continue();
    test_staff_roll();
    printf("all director end-screen tests OK\n");
    return 0;
}
