/* Attract demo (title idle -> SHINOBI demo -> back to the logo / title) headless test.
 * Checks: the title launches the demo exactly when its idle counter reaches 2280 (+ the fade),
 * the demo stages cycle 1-1, 2-1, 3-1, 4-1, 5-1 (index 0/2/4/6/8, counter 0x06014408), the game
 * pad of every stage frame is the recorded input of SHINOBI 0x06046A98 (tables decoded here
 * straight from SHINOBI.dec.bin), the demo ends after 0x709 stage frames (timer 0x060F4CCC)
 * and returns to the SEGA logo then the title, and Start interrupts it (back to the title,
 * "PRESS START" phase). Needs the extracted/ files of test_director.c.
 * S=$(cat tests/SOURCES.txt); gcc -std=c11 -Isrc -Itests tests/test_demo.c $S -lm && ./a.out ../extracted */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "input.h"
#include "pad.h"
#include "ram.h"
#include "render.h"
#include "boot.h"
#include "resource.h"
#include "director_int.h"
#include "objhandlers_c.h"
#include "player.h"

static InputPort fake[2];
const InputPort *Input_GetPort(int p) { return &fake[p]; }
extern int g_spriteLogCount;

#define DEMO_TIMER 0x060F4CCCu
static int frame_no;
static uint8_t *shinobi; static size_t shinobi_len;

static void frame(uint16_t held)
{
    fake[0].connected = 1; fake[0].raw = (uint16_t)~held; fake[1].connected = 0;
    g_spriteLogCount = 0;
    Boot_Frame();
    frame_no++;
}
static int run_until(int (*cond)(void), uint16_t held, int max)
{
    for (int i = 0; i < max; i++) { if (cond()) return i; frame(held); }
    return cond() ? max : -1;
}
static int dir_step(int sc, int st) { return Res_OverlayResident() == OVL_DIRECTOR && (int)R32(G_DIR_SCENE) == sc && (int)R32(D_STEP) == st; }
static int in_title_loop(void) { return dir_step(1, 4) && R16(FADE_STATE) == 0; }
static int logo_shown(void) { return dir_step(0, 2); }
static int demo_mode6(void) { return Res_OverlayResident() == OVL_SHINOBI && R16(G_GAME_MODE) == 6; }
static int title_launch(void) { return dir_step(1, 5); }

/* recorded input, decoded from the file (not from emulated RAM) */
static uint32_t table_of(int stage)
{
    static const uint32_t t[5] = { DEMO_TABLE_S0, DEMO_TABLE_S2, DEMO_TABLE_S4, DEMO_TABLE_S6, DEMO_TABLE_S8 };
    return t[stage / 2];
}
static uint16_t file16(uint32_t a) { size_t o = a - 0x06040000u; assert(o + 1 < shinobi_len); return (uint16_t)(shinobi[o] << 8 | shinobi[o + 1]); }

/* plays one demo from the title loop; returns the number of stage frames. abort_at > 0: press
 * Start at that stage frame. */
static int play_demo(int expect_stage, int abort_at)
{
    int idle0 = RS16(D_TITLE_IDLE), k = 0;
    while (RS16(D_TITLE_IDLE) < 2280 && k < 3000) { frame(0); k++; }
    assert(RS16(D_TITLE_IDLE) == 2280 && k == 2280 - idle0 && R16(FADE_STATE) == 2);   /* fade to black starts */
    int f0 = frame_no;
    int n = run_until(title_launch, 0, 300); assert(n >= 0);
    printf("  idle 2280 at frame %d, title step 5 (launch) %d frames later", f0, n);
    frame(0);
    assert(R32(V_DEMO) & 0x8000);
    assert(RS16(V_STAGE) == expect_stage && (int)(R32(V_DEMO) & 0xFF) == (expect_stage + 2 > 9 ? 0 : expect_stage + 2));
    n = run_until(demo_mode6, 0, 300); assert(n >= 0);
    printf(", SHINOBI stage %d in mode 6 %d frames later\n", RS16(V_STAGE), n);
    assert(R32(ADDR_DEMO_PTR) == table_of(expect_stage) && R32(ADDR_DEMO_CNT) == 0);

    /* follow the recorder: every stage frame steps it once (no scripted-input frames in demos) */
    uint32_t rec = table_of(expect_stage); uint32_t cnt = 0;
    uint16_t prev_held = 0;
    int frames = 0, x0 = (int)(RS32(0x060EA2BCu) >> 16);
    while (demo_mode6() && frames < 4000) {
        int stepped = !(R16(0x060EA2B8u) & PFLAG4_ALT_INPUT);
        frame((abort_at && frames + 1 == abort_at) ? PAD_START : 0);
        frames++;
        if (!demo_mode6()) break;
        if (!stepped) continue;
        cnt++;
        if (!(file16(rec + 2) > cnt)) { rec += 4; cnt = 0; }
        uint16_t held = file16(rec);
        if (R16(ADDR_GAME_HELD) != held || R16(ADDR_GAME_PRESSED) != (uint16_t)(held & ~prev_held) ||
            R32(ADDR_DEMO_PTR) != rec || R32(ADDR_DEMO_CNT) != cnt) {
            printf("  stage frame %d: pad %04X/%04X ptr %08X cnt %u, expected %04X ptr %08X cnt %u\n", frames,
                   R16(ADDR_GAME_HELD), R16(ADDR_GAME_PRESSED), (unsigned)R32(ADDR_DEMO_PTR), (unsigned)R32(ADDR_DEMO_CNT),
                   held, (unsigned)rec, (unsigned)cnt);
            assert(0);
        }
        prev_held = held;
        if (R32(V_DEMO) & 0x2000) break;                    /* end requested: frozen from now on */
    }
    printf("  demo stage %d: %d stage frames, timer %d, Sho x %d -> %d, HP %d, flags %04X, record %u of the table\n",
           expect_stage, frames, R16(DEMO_TIMER), x0, (int)(RS32(0x060EA2BCu) >> 16), RS16(0x060EA2CAu),
           (unsigned)R32(V_DEMO), (unsigned)((rec - table_of(expect_stage)) / 4));
    return frames;
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "../extracted/";
    char p[512];
    snprintf(p, sizeof p, "%s/A.BIN", dir);
    assert(Ram_LoadImage(p, 0x06004000u));
    snprintf(p, sizeof p, "%s/SHINOBI.dec.bin", dir);
    FILE *f = fopen(p, "rb"); assert(f);
    fseek(f, 0, SEEK_END); shinobi_len = (size_t)ftell(f); fseek(f, 0, SEEK_SET);
    shinobi = malloc(shinobi_len); assert(shinobi && fread(shinobi, 1, shinobi_len, f) == shinobi_len); fclose(f);
    Res_SetAssetDir(dir);
    setvbuf(stdout, NULL, _IONBF, 0);

    /* recorded tables: {held, frames}, each ends with {0, 0xFFFF} (held until the demo ends);
     * 1805-1818 frames of input, 5-1 only 1474 */
    for (int s = 0; s <= 8; s += 2) {
        uint32_t a = table_of(s), total = 0; int n = 0;
        while (file16(a + 2) != 0xFFFF) { total += file16(a + 2); a += 4; n++; }
        printf("demo table stage %d: %d records, %u frames\n", s, n, (unsigned)total);
        assert(n > 80 && total > 1400 && file16(a) == 0);
    }

    Boot_Init(1);
    assert(run_until(logo_shown, 0, 200) >= 0);
    int n = run_until(in_title_loop, 0, 600); assert(n >= 0 && R32(D_PRESS_START) == 1);
    printf("title after %d frames\n", frame_no);

    /* five natural demos: 1-1, 2-1, 3-1, 4-1, 5-1, then 1-1 again; each one ends on the timer
     * (0x060F4CCC > 0x708) and goes back to the SEGA logo (DIRECTOR scene 0), then the title */
    for (int s = 0; s <= 10; s += 2) {
        int st = s > 8 ? 0 : s;
        int fr = play_demo(st, 0);
        assert(fr == 0x709 && (R32(V_DEMO) & 0x2000) && !(R32(V_DEMO) & 0x4000));
        n = run_until(logo_shown, 0, 600);
        printf("  -> SEGA logo after %d frames", n); assert(n >= 0);
        n = run_until(in_title_loop, 0, 600);
        printf(", title after %d more\n", n); assert(n >= 0 && R32(D_PRESS_START) == 1);
        assert((R32(V_DEMO) & 0xFFFF) == (unsigned)(st + 2 > 9 ? 0 : st + 2));   /* flags cleared at title step 3 */
    }

    /* Start during the demo: back to the title (scene 1, PRESS START phase), no logo */
    {
        int fr = play_demo(2, 300);
        assert(fr == 300 && (R32(V_DEMO) & 0x6000) == 0x6000);
        int saw_logo = 0;
        for (n = 0; n < 600 && !in_title_loop(); n++) { frame(0); saw_logo |= logo_shown(); }
        printf("  Start at stage frame 300 -> title after %d frames (logo %s)\n", n, saw_logo ? "shown" : "skipped");
        assert(in_title_loop() && !saw_logo && R32(D_PRESS_START) == 1 && R32(G_DIR_SCENE) == 1);
    }
    printf("all demo tests OK\n");
    free(shinobi);
    return 0;
}
