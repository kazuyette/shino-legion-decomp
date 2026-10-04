/* DIRECTOR scene 1 (work package D4) headless test: title "PRESS START", main menu, options
 * (every row, the variables they write, auto-repeat, the 999-shuriken and invincibility
 * cheats), sound test, back to the title, attract demo after 2280 idle frames, demo abort,
 * title cheats. Needs the extracted/ files of test_director.c.
 * S=$(cat tests/SOURCES.txt); gcc -std=c11 -Isrc -Itests tests/test_title.c $S -lm */
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

extern RenderSprite g_spriteLog[]; extern int g_spriteLogCount;
static InputPort fake[2];
const InputPort *Input_GetPort(int p) { return &fake[p]; }

#define D_OPT_MUSIC  0x060DCF98u
#define D_OPT_SE     0x060DCF9Au
#define D_SND_BANK   0x060DCF9Cu
#define V_AUDIO_MONO 0x06025246u
#define SNDT_BANK    0x060872ECu
#define SNDT_SEQ     0x060872EEu
#define SNDT_ROW     0x060872F0u

static int frame_no;

/* one main-loop frame; prints every change of overlay / scene / step (scene 1) */
static void frame(uint16_t held)
{
    static int lastOvl = -1, lastScene = -1, lastStep = -1, lastMode = -1;
    fake[0].connected = 1; fake[0].raw = (uint16_t)~held;
    fake[1].connected = 0;
    g_spriteLogCount = 0;
    Boot_Frame();
    frame_no++;
    int o = Res_OverlayResident();
    int sc = (int)R32(G_DIR_SCENE), st = (int)R32(D_STEP), gm = R16(G_GAME_MODE);
    if (o == OVL_DIRECTOR && (o != lastOvl || sc != lastScene || (sc == 1 && st != lastStep)))
        printf("frame %5d: DIRECTOR scene %d step %d\n", frame_no, sc, st);
    if (o == OVL_SHINOBI && (o != lastOvl || gm != lastMode))
        printf("frame %5d: SHINOBI game mode %d (stage %d, demo flags %04X)\n", frame_no, gm, RS16(V_STAGE),
               (unsigned)R32(V_DEMO));
    lastOvl = o; lastScene = sc; lastStep = st; lastMode = gm;
}
static void press(uint16_t b) { frame(b); frame(0); }
static void press_mod(uint16_t mod, uint16_t b) { frame(mod | b); frame(mod); }

static int run_until(int (*cond)(void), uint16_t held, int max)
{
    for (int i = 0; i < max; i++) { if (cond()) return i; frame(held); }
    return cond() ? max : -1;
}
static int dir_step(int sc, int st) { return Res_OverlayResident() == OVL_DIRECTOR && (int)R32(G_DIR_SCENE) == sc && (int)R32(D_STEP) == st; }
static int in_title_loop(void) { return dir_step(1, 4) && R16(FADE_STATE) == 0; }
static int in_options(void) { return dir_step(1, 7) && R16(FADE_STATE) == 0; }
static int in_soundtest(void) { return dir_step(1, 17); }
static int in_stage_start(void) { return dir_step(2, 1); }
static int logo_shown(void) { return dir_step(0, 2); }
static int demo_running(void) { return Res_OverlayResident() == OVL_SHINOBI && R16(G_GAME_MODE) == 6 && R16(FADE_STATE) == 0; }
static int back_in_title(void) { return in_title_loop() && R32(D_PRESS_START) == 1; }

/* ---- screen dump: TITLE.PBC chars by size, font strings rebuilt from the glyph sources ---- */
static uint32_t glyph_src(int g) { return VDP1_VRAM_BASE + ((uint32_t)R16(CHAR_TABLE + (uint32_t)(0x200 + g) * 10) << 3); }
static char glyph_char(uint32_t src)
{
    static const char map[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ.:-";
    for (int g = 0; g < 39; g++) if (glyph_src(g) == src) return map[g];
    return 0;
}
static int ccalc(const RenderSprite *s) { return (s->colr >> 11) & 3; }
static const char *pbc_name(const RenderSprite *s)
{
    if (s->w == 320 && s->h == 224) return s->src == VDP1_VRAM_BASE + 0x4740u ? "title bg" : "options bg";
    if (s->w == 40 && s->h == 10) return "shuriken cursor";
    if (s->w == 80 && s->h == 12) return "GAME START";
    if (s->w == 64 && s->h == 12) return "OPTIONS";
    if (s->w == 112 && s->h == 16) return "OPTIONS title";
    if (s->w == 152 && s->h == 13) return "PRESS START BUTTON";
    return "?";
}
static char screen[4096];
/* text of the font sprites at row y (gaps of 8 px = one space), "" if none */
static const char *text_at(int y, int *cc)
{
    static char line[128];
    int xs[64], n = 0; char cs[64];
    for (int i = 0; i < g_spriteLogCount && n < 64; i++) {
        const RenderSprite *s = &g_spriteLog[i];
        char c = (s->w == 8 && s->h == 16) ? glyph_char(s->src) : 0;
        if (c && s->y == y) { xs[n] = s->x; cs[n] = c; if (cc) *cc = ccalc(s); n++; }
    }
    for (int i = 1; i < n; i++)                      /* sort by x */
        for (int j = i; j > 0 && xs[j] < xs[j - 1]; j--) {
            int t = xs[j]; xs[j] = xs[j - 1]; xs[j - 1] = t;
            char u = cs[j]; cs[j] = cs[j - 1]; cs[j - 1] = u;
        }
    int k = 0;
    if (n) k += snprintf(line, sizeof line, "x%3d: ", xs[0]);
    for (int i = 0; i < n && k < 120; i++) {
        if (i) for (int g = (xs[i] - xs[i - 1]) / 8 - 1; g > 0 && k < 120; g--) line[k++] = ' ';
        line[k++] = cs[i];
    }
    line[k] = 0;
    return line;
}
static void dump_screen(const char *label)
{
    int ys[64], ny = 0;
    screen[0] = 0;
    printf("  [%s] %d sprites\n", label, g_spriteLogCount);
    for (int i = 0; i < g_spriteLogCount; i++) {
        const RenderSprite *s = &g_spriteLog[i];
        if (s->w == 8 && s->h == 16 && glyph_char(s->src)) {
            int seen = 0;
            for (int j = 0; j < ny; j++) seen |= ys[j] == s->y;
            if (!seen && ny < 64) ys[ny++] = s->y;
            continue;
        }
        printf("    sprite %-18s %3dx%-3d at %3d,%3d bank %3d ccalc %d\n", pbc_name(s), s->w, s->h, s->x, s->y,
               (s->colr >> 4) & 0x7F, ccalc(s));
    }
    for (int i = 1; i < ny; i++) for (int j = i; j > 0 && ys[j] < ys[j - 1]; j--) { int t = ys[j]; ys[j] = ys[j - 1]; ys[j - 1] = t; }
    for (int i = 0; i < ny; i++) {
        int cc = 0;
        const char *t = text_at(ys[i], &cc);
        printf("    y%3d %s %s\n", ys[i], cc ? "(dim)" : "     ", t);
        strncat(screen, t, sizeof screen - strlen(screen) - 2); strcat(screen, "|");
    }
}
static int find_pbc(const char *name, int x, int y)
{
    for (int i = 0; i < g_spriteLogCount; i++)
        if (!strcmp(pbc_name(&g_spriteLog[i]), name) && g_spriteLog[i].x == x && g_spriteLog[i].y == y) return i;
    return -1;
}

static void opt_press(uint16_t b, const char *what)
{
    press(b);
    printf("  %-26s -> cursor %d  LEVEL %d SHUR %d/%d CTRL %d MUSIC %d SE %d AUDIO %d\n", what,
           RS16(D_MENU_CURSOR), RS16(V_DIFFICULTY), RS16(V_SHUR_INIT), RS16(V_SHURIKENS), RS16(V_CONTROL),
           RS16(D_OPT_MUSIC), RS16(D_OPT_SE), RS16(V_AUDIO_MONO));
}

static void boot_to_title(const char *dir)
{
    (void)dir;
    Boot_Init(1);
    int n = run_until(logo_shown, 0, 200); assert(n >= 0);
    n = run_until(in_title_loop, 0, 600);
    printf("title loop (PRESS START phase) after %d frames\n", n); assert(n >= 0 && R32(D_PRESS_START) == 1);
}

/* title cheat: Start (PRESS START) + buttons on the menu + Start (GAME START) */
static void title_cheat(const uint16_t *keys, int nk)
{
    for (int i = 0; i < nk; i++) press(keys[i]);
    printf("  cheat buffer:");
    for (int i = 0; i < (int)R32(D_CHEAT_COUNT); i++) printf(" %04X", R16(D_CHEAT_BUF + 2u * (uint32_t)i));
    printf("\n");
    int n = run_until(in_stage_start, 0, 300);
    printf("  -> stage start (scene 2) after %d frames: HP max %d lives %d continues %d shurikens %d difficulty %d\n",
           n, RS16(V_HP_MAX), RS16(V_LIVES), RS16(V_CONTINUES), RS16(V_SHURIKENS), RS16(V_DIFFICULTY));
    assert(n >= 0);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "../extracted/";
    char p[512];
    snprintf(p, sizeof p, "%s/A.BIN", dir);
    assert(Ram_LoadImage(p, 0x06004000u));
    Res_SetAssetDir(dir);
    setvbuf(stdout, NULL, _IONBF, 0);

    /* ---------------- title: PRESS START, menu ---------------- */
    boot_to_title(dir);
    int i, n;
    while (!(R16(FRAME_COUNT) & 0x10)) frame(0);
    frame(0);
    dump_screen("title, PRESS START visible");
    i = find_pbc("PRESS START BUTTON", 85, 164); assert(i >= 0 && ((g_spriteLog[i].colr >> 4) & 0x7F) == 3);
    while (R16(FRAME_COUNT) & 0x10) frame(0);
    frame(0);
    assert(find_pbc("PRESS START BUTTON", 85, 164) < 0);              /* blinking */
    press(PAD_START);
    assert(R32(D_PRESS_START) == 0 && R32(V_RESET_ACT) == 0);
    frame(0);
    dump_screen("main menu, cursor GAME START");
    assert(find_pbc("GAME START", 136, 152) >= 0 && ccalc(&g_spriteLog[find_pbc("GAME START", 136, 152)]) == 0);
    assert(find_pbc("OPTIONS", 136, 172) >= 0 && ccalc(&g_spriteLog[find_pbc("OPTIONS", 136, 172)]) == 2);
    assert(find_pbc("shuriken cursor", 84, 152) >= 0 || find_pbc("shuriken cursor", 85, 152) >= 0
           || find_pbc("shuriken cursor", 88 + RS16(0x0604387Au + 2u * (R16(D_MENU_BOB) % 24)), 152) >= 0);
    press(PAD_DOWN);
    frame(0);
    assert(R16(D_MENU_CURSOR) == 1);
    dump_screen("main menu, cursor OPTIONS");
    assert(ccalc(&g_spriteLog[find_pbc("GAME START", 136, 152)]) == 2);
    assert(ccalc(&g_spriteLog[find_pbc("OPTIONS", 136, 172)]) == 0);
    press(PAD_UP); assert(R16(D_MENU_CURSOR) == 0);
    press(PAD_DOWN); assert(R16(D_MENU_CURSOR) == 1);
    press(PAD_START);                                               /* OPTIONS */
    assert(R16(D_MENU_CURSOR) == 0x81);
    {   /* the chosen item blinks: hidden while timer & 8 == 0 */
        int shown = 0, hidden = 0;
        for (int k = 0; k < 16; k++) { frame(0); if (find_pbc("OPTIONS", 136, 172) >= 0) shown++; else hidden++; }
        printf("confirmed OPTIONS blinks: shown %d / hidden %d of 16 frames\n", shown, hidden);
        assert(shown == 8 && hidden == 8);
    }

    /* ---------------- options ---------------- */
    n = run_until(in_options, 0, 400);
    printf("options screen after %d frames\n", n); assert(n >= 0);
    frame(0);
    dump_screen("options");
    assert(strstr(screen, "LEVEL") && strstr(screen, "NORMAL") && strstr(screen, "SHURIKENS") && strstr(screen, "20")
           && strstr(screen, "CONTROL TYPE") && strstr(screen, "A:SHURIKEN") && strstr(screen, "STEREO") && strstr(screen, "EXIT"));
    assert(RS16(V_DIFFICULTY) == 1 && RS16(V_SHUR_INIT) == 20 && RS16(V_CONTROL) == 0);

    printf("LEVEL:\n");
    opt_press(PAD_RIGHT, "Right"); assert(RS16(V_DIFFICULTY) == 2);
    opt_press(PAD_RIGHT, "Right (wrap)"); assert(RS16(V_DIFFICULTY) == 0);
    opt_press(PAD_LEFT, "Left (wrap)"); assert(RS16(V_DIFFICULTY) == 2);
    frame(0); assert(strstr(text_at(56, NULL), "HARD"));

    printf("SHURIKENS:\n");
    opt_press(PAD_DOWN, "Down"); assert(RS16(D_MENU_CURSOR) == 1);
    opt_press(PAD_RIGHT, "Right"); assert(RS16(V_SHUR_INIT) == 30 && RS16(V_SHURIKENS) == 30);
    opt_press(PAD_LEFT, "Left"); opt_press(PAD_LEFT, "Left"); opt_press(PAD_LEFT, "Left");
    assert(RS16(V_SHUR_INIT) == 0);
    opt_press(PAD_LEFT, "Left (wrap)"); assert(RS16(V_SHUR_INIT) == 100);
    opt_press(PAD_RIGHT, "Right (wrap)"); assert(RS16(V_SHUR_INIT) == 0);
    press_mod(PAD_LT | PAD_RT, PAD_C);                             /* cheat: LT+RT held, C A B */
    press_mod(PAD_LT | PAD_RT, PAD_A);
    press_mod(PAD_LT | PAD_RT, PAD_B);
    frame(0);
    printf("  LT+RT + C A B             -> SHUR %d/%d\n", RS16(V_SHUR_INIT), RS16(V_SHURIKENS));
    assert(RS16(V_SHUR_INIT) == 999 && RS16(V_SHURIKENS) == 999);
    assert(strstr(text_at(72, NULL), "999"));

    printf("CONTROL TYPE:\n");
    opt_press(PAD_DOWN, "Down"); assert(RS16(D_MENU_CURSOR) == 2);
    opt_press(PAD_RIGHT, "Right"); opt_press(PAD_RIGHT, "Right"); assert(RS16(V_CONTROL) == 2);
    opt_press(PAD_LEFT, "Left"); opt_press(PAD_LEFT, "Left"); opt_press(PAD_LEFT, "Left (wrap)");
    assert(RS16(V_CONTROL) == 5);
    opt_press(PAD_RIGHT, "Right (wrap)"); assert(RS16(V_CONTROL) == 0);
    for (int k = 0; k < 3; k++) opt_press(PAD_RIGHT, "Right");
    assert(RS16(V_CONTROL) == 3);
    frame(0);
    printf("  rows: %s | ", text_at(88, NULL)); printf("%s | ", text_at(104, NULL)); printf("%s\n", text_at(120, NULL));
    assert(strstr(text_at(88, NULL), "4") && strstr(text_at(88, NULL), "A:KATANA")
           && strstr(text_at(104, NULL), "B:SHURIKEN") && strstr(text_at(120, NULL), "C:JUMP"));
    Game_LatchPad1(0);                                              /* SHINOBI's use of the option */
    printf("  Game_LatchPad1: shuriken %04X katana %04X jump %04X\n", R16(ADDR_ACTION_BTN0), R16(ADDR_ACTION_BTN1), R16(ADDR_ACTION_BTN2));
    assert(R16(ADDR_ACTION_BTN0) == PAD_B && R16(ADDR_ACTION_BTN1) == PAD_A && R16(ADDR_ACTION_BTN2) == PAD_C);

    printf("MUSIC:\n");
    opt_press(PAD_DOWN, "Down"); assert(RS16(D_MENU_CURSOR) == 3);
    opt_press(PAD_LEFT, "Left (wrap)"); assert(RS16(D_OPT_MUSIC) == 23);
    frame(0); assert(strstr(text_at(136, NULL), "ENDING"));
    opt_press(PAD_RIGHT, "Right (wrap)"); assert(RS16(D_OPT_MUSIC) == 0);
    for (int k = 0; k < 8; k++) press(PAD_RIGHT);
    frame(0);
    printf("  music 8: %s\n", text_at(136, NULL));
    assert(RS16(D_OPT_MUSIC) == 8 && strstr(text_at(136, NULL), "STAGE 2-3"));
    printf("  C (play):\n"); press(PAD_C);
    assert(RS16(D_SND_BANK) == 1);                                  /* STAGE2.SND loaded for 2-3 */
    printf("  A (stop):\n"); press(PAD_A);

    printf("S.E.:\n");
    opt_press(PAD_DOWN, "Down"); assert(RS16(D_MENU_CURSOR) == 4);
    opt_press(PAD_LEFT, "Left (wrap)"); assert(RS16(D_OPT_SE) == 39);
    opt_press(PAD_RIGHT, "Right (wrap)"); assert(RS16(D_OPT_SE) == 0);
    {   /* Pad_GetDirRepeat: press, then after 20 frames held one step every 4 frames */
        int v[60];
        for (int k = 0; k < 60; k++) { frame(PAD_RIGHT); v[k] = RS16(D_OPT_SE); }
        frame(0);
        printf("  Right held 60 frames: value per frame:");
        for (int k = 0; k < 60; k++) printf(" %d", v[k]);
        printf("\n");
        assert(v[0] == 1 && v[20] == 1 && v[59] > 3 && v[59] < 15);
        for (int k = 22; k < 60; k++) assert(v[k] - v[k - 1] <= 1);
        int steps = 0;
        for (int k = 22; k < 60; k++) steps += v[k] != v[k - 1];
        assert(steps == (v[59] - 1));
    }
    while (RS16(D_OPT_SE) != 6) press(RS16(D_OPT_SE) > 6 ? PAD_LEFT : PAD_RIGHT);
    printf("  SE %d, C (play):\n", RS16(D_OPT_SE)); press(PAD_C);
    frame(0); assert(strstr(text_at(152, NULL), "06"));

    printf("AUDIO:\n");
    opt_press(PAD_DOWN, "Down"); assert(RS16(D_MENU_CURSOR) == 5);
    opt_press(PAD_RIGHT, "Right"); assert(R16(V_AUDIO_MONO) == 1);
    printf("  sound driver area pointer *0x0603BBB8 = %08X\n", (unsigned)R32(0x0603BBB8u));
    frame(0); assert(strstr(text_at(168, NULL), "MONO"));
    opt_press(PAD_LEFT, "Left"); assert(R16(V_AUDIO_MONO) == 0);
    opt_press(PAD_RIGHT, "Right"); assert(R16(V_AUDIO_MONO) == 1);
    {   /* invincibility: MUSIC 8, S.E. 6, cursor on AUDIO, LT held */
        static const struct { uint16_t b; int n; } seq[] = {
            { PAD_A, 10 }, { PAD_X, 1 }, { PAD_B, 3 }, { PAD_Y, 1 }, { PAD_C, 6 }, { PAD_Z, 1 },
            { PAD_X, 10 }, { PAD_A, 1 }, { PAD_Y, 3 }, { PAD_B, 1 }, { PAD_Z, 6 }, { PAD_C, 1 } };
        assert(R32(V_INVINCIBLE) != 0x4465268Fu);
        for (unsigned s = 0; s < sizeof seq / sizeof seq[0]; s++)
            for (int k = 0; k < seq[s].n; k++) press_mod(PAD_LT, seq[s].b);
        frame(0);
        printf("  LT + A*10 X B*3 Y C*6 Z X*10 A Y*3 B Z*6 C -> invincibility flag %08X\n", (unsigned)R32(V_INVINCIBLE));
        assert(R32(V_INVINCIBLE) == 0x4465268Fu);
    }
    frame(0);
    dump_screen("options, all changed");

    /* ---------------- sound test ---------------- */
    frame(PAD_RT); frame(PAD_RT | PAD_START); frame(0);
    n = run_until(in_soundtest, 0, 10);
    printf("RT + Start -> sound test after %d frames\n", n); assert(n >= 0);
    frame(0);
    dump_screen("sound test");
    assert(strstr(screen, "SOUND TEST") && strstr(screen, "SOUND FILE NAME:") && strstr(screen, "STAGE2.SND")
           && strstr(screen, "DRV-ERR :") && strstr(screen, "00000000"));
    press(PAD_DOWN); assert(R16(SNDT_ROW) == 1);
    for (int k = 0; k < 3; k++) press(PAD_RIGHT);
    press(PAD_LEFT); press(PAD_LEFT); press(PAD_LEFT); press(PAD_LEFT); assert(R16(SNDT_BANK) == 63);   /* & 63 */
    press(PAD_RIGHT); press(PAD_RIGHT); assert(R16(SNDT_BANK) == 1);
    press(PAD_DOWN); press(PAD_RIGHT); press(PAD_RIGHT); press(PAD_RIGHT); assert(R16(SNDT_SEQ) == 3);
    press(PAD_DOWN); assert(R16(SNDT_ROW) == 0);
    press(PAD_UP); assert(R16(SNDT_ROW) == 2);
    press(PAD_DOWN);
    press(PAD_LEFT); press(PAD_LEFT); assert(RS16(D_SND_BANK) == 9);    /* 1 -> 0 -> 9 */
    press(PAD_RIGHT); assert(RS16(D_SND_BANK) == 0);
    printf("  C / B / Z / Y:\n");
    press(PAD_C); press(PAD_B); press(PAD_Z); press(PAD_Y);
    frame(0);
    dump_screen("sound test, changed");
    press(PAD_START);
    n = run_until(in_options, 0, 100);
    printf("Start -> options again after %d frames (cursor %d)\n", n, RS16(D_MENU_CURSOR)); assert(n >= 0);

    /* ---------------- EXIT -> title ---------------- */
    press(PAD_UP); assert(RS16(D_MENU_CURSOR) == 6);
    press(PAD_C);
    n = run_until(in_title_loop, 0, 200);
    printf("EXIT -> title menu after %d frames\n", n); assert(n >= 0);
    frame(0);
    dump_screen("title menu after options");
    assert(R32(D_PRESS_START) == 0 && find_pbc("GAME START", 136, 152) >= 0);
    assert(RS16(V_DIFFICULTY) == 2 && RS16(V_SHUR_INIT) == 999 && RS16(V_CONTROL) == 3 && R16(V_AUDIO_MONO) == 1);

    /* ---------------- attract demo after 2280 idle frames ---------------- */
    {
        int idle0 = RS16(D_TITLE_IDLE), k = 0;
        while (RS16(D_TITLE_IDLE) < 2280 && k < 3000) { frame(0); k++; }
        printf("idle counter %d -> 2280 after %d more frames (fade to black + CD-DA fade starts)\n", idle0, k);
        assert(RS16(D_TITLE_IDLE) == 2280 && k == 2280 - idle0 && dir_step(1, 4) && R16(FADE_STATE) == 2);
        n = run_until(demo_running, 0, 600);
        printf("demo running after %d frames: stage %d demo flags %04X not-first %d\n", n, RS16(V_STAGE),
               (unsigned)R32(V_DEMO), RS16(V_NOT_FIRST));
        assert(n >= 0 && (R32(V_DEMO) & 0x8000) && RS16(V_STAGE) == 0 && (R32(V_DEMO) & 0xFF) == 2);
    }
    for (int k = 0; k < 60; k++) frame(0);
    press(PAD_START);                                              /* abort the demo */
    n = run_until(back_in_title, 0, 600);
    printf("demo aborted -> title PRESS START after %d frames\n", n); assert(n >= 0);

    /* ---------------- title cheats ---------------- */
    printf("cheat Start A Z B Y C X Start:\n");
    {
        static const uint16_t k[] = { PAD_START, PAD_A, PAD_Z, PAD_B, PAD_Y, PAD_C, PAD_X, PAD_START };
        title_cheat(k, 8);
        assert(RS16(V_LIVES) == 99 && RS16(V_CONTINUES) == 9 && RS16(V_HP_MAX) == 4);   /* HARD */
    }
    printf("reboot; cheat Start A B C B A Start:\n");
    boot_to_title(dir);
    {
        static const uint16_t k[] = { PAD_START, PAD_A, PAD_B, PAD_C, PAD_B, PAD_A, PAD_START };
        title_cheat(k, 7);
        assert(RS16(V_HP_MAX) == 1 && RS16(V_LIVES) == 0 && RS16(V_CONTINUES) == 0);
    }
    printf("reboot; no cheat:\n");
    boot_to_title(dir);
    {
        static const uint16_t k[] = { PAD_START, PAD_START };
        title_cheat(k, 2);
        assert(RS16(V_LIVES) == R16(T_LIVES + 2u * R16(V_DIFFICULTY)));
    }
    printf("all title tests OK\n");
    return 0;
}
