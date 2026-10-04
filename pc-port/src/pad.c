#include "pad.h"
#include "input.h"
#include "ram.h"

PadPort  g_padPorts[2];

/* A.BIN 0x0600F290: 6 button layouts, 8-byte stride (3 used words + pad). */
static const uint16_t kButtonConfig[6][4] = {
    { PAD_A, PAD_B, PAD_C, 0 },
    { PAD_A, PAD_C, PAD_B, 0 },
    { PAD_C, PAD_A, PAD_B, 0 },
    { PAD_B, PAD_A, PAD_C, 0 },
    { PAD_C, PAD_B, PAD_A, 0 },
    { PAD_B, PAD_C, PAD_A, 0 },
};

/* The A.BIN table really lives at 0x0601C4A0 (8 bytes per port: held, held_prev, pressed,
 * pressed_prev); DIRECTOR and SHINOBI read it there, so keep the emulated RAM in sync. */
#define PAD_TABLE 0x0601C4A0u
static void pad_mirror(void)
{
    for (unsigned p = 0; p < 2; p++) {
        uint32_t a = PAD_TABLE + 8u * p;
        W16(a + 0, g_padPorts[p].held);
        W16(a + 2, g_padPorts[p].held_prev);
        W16(a + 4, g_padPorts[p].pressed);
        W16(a + 6, g_padPorts[p].pressed_prev);
    }
}

void Pad_Clear(void)
{
    for (int p = 0; p < 2; p++)
        g_padPorts[p].held = g_padPorts[p].held_prev =
        g_padPorts[p].pressed = g_padPorts[p].pressed_prev = 0;
    pad_mirror();
}

int Pad_Update(unsigned nports)
{
    if (nports > 2)
        return 0;
    for (unsigned p = 0; p < nports; p++) {
        PadPort *e = &g_padPorts[p];
        const InputPort *in = Input_GetPort((int)p);
        if (in && in->connected) {
            e->held_prev    = e->held;
            e->pressed_prev = e->pressed;
            e->held         = (uint16_t)~in->raw;
            e->pressed      = (uint16_t)(e->held & ~e->held_prev);
        } else {
            e->held = e->held_prev = e->pressed = e->pressed_prev = 0;
        }
    }
    pad_mirror();
    return 1;
}

int Sys_CheckSoftReset(void)
{
    const PadPort *p = &g_padPorts[0];
    return (p->held & (PAD_A | PAD_B | PAD_C)) == (PAD_A | PAD_B | PAD_C)
        && (p->pressed & PAD_START);
}

static uint16_t mirror_lr(uint16_t v)
{
    v ^= (PAD_LEFT | PAD_RIGHT);
    if ((v & (PAD_LEFT | PAD_RIGHT)) == (PAD_LEFT | PAD_RIGHT))
        v &= (uint16_t)~(PAD_LEFT | PAD_RIGHT);
    return v;
}

/* SHINOBI 0x060463DC: scripted pad while the player flags 4 bit 0x10 is set (drop-in of 1-1,
 * 5-1 jump out of the water, 5-2 lift, end-of-stage walk-outs set by the stage hooks...).
 * The real pad is ignored; per stage index (jump table 0x06046520, 17 entries):
 *  0: drop-in (0x060252DC == 0): pressed = jump button if PF2 & 0x1000; else walk right;
 *  2, 3, 4, 6, 10, 12, 14, 16: walk right;
 *  8: during the jump-in (0x060F4D38 in 65..0x7FFD) held = right (+ jump as for 0), else walk right;
 *  9: walk right when the 5-2 hook state 0x060F4D44 is 2, or 7 (then PL+0x1C = 0x8000);
 *  1, 5, 7, 11, 13, 15, 17: nothing pressed.
 * "Walk right" (0x0604654A): held = right only when the player flags 2 & 0x4200 == 0x0200 or
 * both clear (on the ground); when already walking (action 1) the direction-tap / idle /
 * walk counters 0x060EA42E / 0x060EA430 / 0x060EA43A are set to 1 and pressed = right too. */
#define PL_F2   0x060EA2B6u
static void walk_right(void)                     /* 0x0604654A */
{
    uint16_t f2 = R16(PL_F2) & 0x4200;
    if (f2 == 0x0200) { W16(ADDR_GAME_HELD, PAD_RIGHT); return; }
    if (f2) return;
    if (R16(0x060EA2BAu) == 1) {                 /* action: walk */
        W16(0x060EA42Eu, 1); W16(0x060EA430u, 1); W16(0x060EA43Au, 1);
        W16(ADDR_GAME_PRESSED, PAD_RIGHT);
    }
    W16(ADDR_GAME_HELD, PAD_RIGHT);
}

static void Game_ScriptedInput(void)
{
    W16(ADDR_GAME_HELD, 0); W16(ADDR_GAME_PRESSED, 0); W16(ADDR_GAME_PRESSED_PREV, 0);
    uint16_t st = R16(0x0602526Au);
    switch (st) {
    case 0:
        if (R16(0x060252DCu) != 0) { walk_right(); break; }
        if (R16(PL_F2) & 0x1000) W16(ADDR_GAME_PRESSED, R16(ADDR_ACTION_BTN2));
        break;
    case 2: case 3: case 4: case 6: case 10: case 12: case 14: case 16:
        walk_right();
        break;
    case 8: {
        int16_t t = (int16_t)R16(0x060F4D38u);
        if (t != 0x7FFE && t > 64) {
            W16(ADDR_GAME_HELD, PAD_RIGHT);
            if (R16(PL_F2) & 0x1000) W16(ADDR_GAME_PRESSED, R16(ADDR_ACTION_BTN2));
        } else walk_right();
        break;
    }
    case 9: {
        int16_t s = (int16_t)R16(0x060F4D44u);  /* 0x0606E1F0 */
        if (s == 7) W16(0x060EA2D0u, 0x8000);   /* PL + 0x1C */
        if (s == 2 || s == 7) walk_right();
        break;
    }
    default: break;                              /* 1, 5, 7, 11, 13, 15 (17+: index >= 17) */
    }
}

int Game_LatchPad1(uint16_t playerFlags4)
{
    uint16_t held = 0, pressed = 0, pressedPrev = 0;
    uint32_t demo = R32(ADDR_DEMO_FLAGS);

    int cfg = RS16(ADDR_BUTTON_CONFIG);
    if (demo & 0x8000) cfg = 0;
    if (cfg < 0 || cfg > 5) cfg = 0;   /* PC safety; original indexes blindly */
    for (int i = 0; i < 3; i++)
        W16(ADDR_ACTION_BTN0 + 2u * (unsigned)i, kButtonConfig[cfg][i]);

    if (!(playerFlags4 & PFLAG4_INPUT_OFF)) {
        held        = g_padPorts[0].held;
        pressed     = g_padPorts[0].pressed;
        pressedPrev = g_padPorts[0].pressed_prev;
    }
    if (playerFlags4 & PFLAG4_MIRROR_LR) {
        held    = mirror_lr(held);
        pressed = mirror_lr(pressed);
    }
    W16(ADDR_GAME_HELD, held);
    W16(ADDR_GAME_PRESSED, pressed);
    W16(ADDR_GAME_PRESSED_PREV, pressedPrev);

    if (playerFlags4 & PFLAG4_ALT_INPUT) {
        Game_ScriptedInput();                    /* tail jump to 0x060463DC */
        return 1;
    }
    if (demo & 0x8000) {
        Game_DemoInput();                        /* tail jump to 0x06046A98 */
        return 2;
    }
    return 0;
}

/* 0x060469FE: reached at the end of every stage (re)load through Player_InitForStage. Clears the
 * demo pad and points at the recorded input of the stage; other stages keep the old pointer
 * (only read while the demo flag is set, and demos only run on stages 0/2/4/6/8). */
void Game_DemoInputInit(void)
{
    W16(ADDR_DEMO_PAD + 2, 0); W16(ADDR_DEMO_PAD, 0); W16(ADDR_DEMO_PAD + 6, 0); W16(ADDR_DEMO_PAD + 4, 0);
    W32(ADDR_DEMO_CNT, 0);
    switch (R16(0x0602526Au)) {
    case 0: W32(ADDR_DEMO_PTR, DEMO_TABLE_S0); break;
    case 2: W32(ADDR_DEMO_PTR, DEMO_TABLE_S2); break;
    case 4: W32(ADDR_DEMO_PTR, DEMO_TABLE_S4); break;
    case 6: W32(ADDR_DEMO_PTR, DEMO_TABLE_S6); break;
    case 8: W32(ADDR_DEMO_PTR, DEMO_TABLE_S8); break;
    default: break;
    }
}

/* 0x06046A98: one record {u16 held, u16 frames} per run of identical input. The counter is
 * bumped first, and the record is left when the counter reaches its length (so the first record
 * lasts length-1 frames, the next ones length frames). The game pad words are then replaced
 * (pressed = held & ~previous held); the real pad only matters for Start (Game_CheckDemoEnd). */
void Game_DemoInput(void)
{
    W32(ADDR_DEMO_CNT, R32(ADDR_DEMO_CNT) + 1u);
    if (!(R16(R32(ADDR_DEMO_PTR) + 2) > R32(ADDR_DEMO_CNT))) {
        W32(ADDR_DEMO_PTR, R32(ADDR_DEMO_PTR) + 4u);
        W32(ADDR_DEMO_CNT, 0);
    }
    W16(ADDR_DEMO_PAD + 2, R16(ADDR_DEMO_PAD));
    W16(ADDR_DEMO_PAD + 6, R16(ADDR_DEMO_PAD + 4));
    W16(ADDR_DEMO_PAD, R16(R32(ADDR_DEMO_PTR)));
    W16(ADDR_DEMO_PAD + 4, (uint16_t)(R16(ADDR_DEMO_PAD) & ~R16(ADDR_DEMO_PAD + 2)));
    W16(ADDR_GAME_HELD, R16(ADDR_DEMO_PAD));
    W16(ADDR_GAME_PRESSED, R16(ADDR_DEMO_PAD + 4));
    W16(ADDR_GAME_PRESSED_PREV, R16(ADDR_DEMO_PAD + 6));
}
