/* Screen fade (A.BIN 0x06006EE8-0x06007BA8) on the real binaries.
 * S=$(cat tests/SOURCES.txt) && gcc -std=c11 -Wall -Wextra -O1 -Isrc -Itests tests/test_fade.c src/fade.c $S -o /tmp/tf && /tmp/tf ../extracted/ */
#include <stdio.h>
#include <assert.h>
#include "fade.h"
#include "ram.h"
#include "input.h"

static InputPort fake[2];
const InputPort *Input_GetPort(int p) { return &fake[p]; }   /* pad.c needs it */

static void show(int f)
{
    int r, g, b, m;
    Fade_GetColorOffset(&r, &g, &b, &m);
    printf("  frame %2d: state %d  cur %08X %08X %08X  COA %03X %03X %03X  offset %4d %4d %4d  CLOFEN %02X  CRAMdirty %d\n",
           f, R16(FADE_STATE), (unsigned)R32(0x06025100u), (unsigned)R32(0x06025104u), (unsigned)R32(0x06025108u),
           R16(0x0603B49Eu), R16(0x0603B4A0u), R16(0x0603B4A2u), r, g, b, m, R16(0x060250A8u));
}

static int run(int max)
{
    int f;
    for (f = 1; f <= max && R16(FADE_STATE) != 0; f++) { Fade_Update(); show(f); }
    return f - 1;
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "../extracted/";
    char p[512];
    snprintf(p, sizeof p, "%sA.BIN", dir);           assert(Ram_LoadImage(p, 0x06004000u));
    snprintf(p, sizeof p, "%sSHINOBI.dec.bin", dir); assert(Ram_LoadImage(p, 0x06040000u));
    W16(0x060250A8u, 0);

    Fade_Reset();                                   /* boot: A.BIN 0x06004094 */
    int r, g, b, m;
    Fade_GetColorOffset(&r, &g, &b, &m);
    printf("after Fade_Reset: offset %d %d %d, CLOFEN %02X, default speed %08X\n", r, g, b, m, (unsigned)R32(0x06025110u));
    assert(r == 0 && g == 0 && b == 0 && m == 0x7F && R16(FADE_STATE) == 0);

    /* stage end, exactly as Stage_UpdateState 0x060461DA */
    printf("stage end: 0x0600725C(0), 0x06006F48(0x100,0x100,0x100), 0x060072AC(16,2,1), 0x06006FDE\n");
    assert(R16(FADE_STATE) == 0);
    Fade_StageOutro();
    printf("  timed speed %08X, count %d, state %d\n", (unsigned)R32(0x0602512Cu), R16(0x06025130u), R16(FADE_STATE));
    assert(R32(0x0602512Cu) == 0x00100000u && R16(FADE_STATE) == 2);
    int n = run(100);
    printf("fade-out done after %d frames\n", n);
    Fade_GetColorOffset(&r, &g, &b, &m);
    assert(n == 16 && r == -256 && g == -256 && b == -256 && m == 0x7F && R16(0x06025130u) == 0);
    /* idle frames keep the screen black */
    for (int i = 0; i < 3; i++) Fade_Update();
    Fade_GetColorOffset(&r, &g, &b, &m); assert(r == -256);

    /* stage load, exactly as Game_LoadStage 0x0606B7C6 */
    printf("stage load: 0x06006F56, cur = -256, 0x060072AC(16,1,1), 0x06006FB8\n");
    Fade_StageIntro();
    show(0);
    n = run(100);
    printf("fade-in done after %d frames\n", n);
    Fade_GetColorOffset(&r, &g, &b, &m);
    assert(n == 16 && r == 0 && g == 0 && b == 0 && m == 0x7F);

    /* untimed fade (default speed 10/frame) + queue */
    Fade_SetTarget(0x100, 0x100, 0x100);
    assert(Fade_RequestOut() == 1 && Fade_RequestIn() == 1 && Fade_RequestIn() == 0);
    int out = 0;
    while (R16(FADE_STATE) == 2) { Fade_Update(); out++; }
    int in = 0;
    while (R16(FADE_STATE) == 1) { Fade_Update(); in++; }
    printf("default speed: out %d frames, then queued in %d frames\n", out, in);
    assert(out == 26 && in == 26);

    /* positive target (white flash style), mode-2 distance through the sign change */
    Fade_SetTarget(0x0C0, 0x040, 0x000);
    assert(Fade_SetupDuration(8, 2, 1) && Fade_StartOut() == 0);
    n = run(100);
    Fade_GetColorOffset(&r, &g, &b, &m);
    printf("to +192/+64/0 in %d frames: %d %d %d\n", n, r, g, b);
    assert(n == 8 && r == 192 && g == 64 && b == 0);
    assert(R16(0x060250A8u) == 0);                  /* the fade never touches CRAM */
    printf("all fade tests OK\n");
    return 0;
}
