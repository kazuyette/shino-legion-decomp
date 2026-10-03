#include "pad.h"
#include "input.h"

PadPort  g_padPorts[2];
uint16_t g_gameHeld, g_gamePressed, g_gamePressedPrev;
uint16_t g_actionBtn[3];
int16_t  g_buttonConfig;
uint32_t g_demoFlags;

/* A.BIN 0x0600F290: 6 button layouts, 8-byte stride (3 used words + pad). */
static const uint16_t kButtonConfig[6][4] = {
    { PAD_A, PAD_B, PAD_C, 0 },
    { PAD_A, PAD_C, PAD_B, 0 },
    { PAD_C, PAD_A, PAD_B, 0 },
    { PAD_B, PAD_A, PAD_C, 0 },
    { PAD_C, PAD_B, PAD_A, 0 },
    { PAD_B, PAD_C, PAD_A, 0 },
};

void Pad_Clear(void)
{
    for (int p = 0; p < 2; p++)
        g_padPorts[p].held = g_padPorts[p].held_prev =
        g_padPorts[p].pressed = g_padPorts[p].pressed_prev = 0;
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

int Game_LatchPad1(uint16_t playerFlags4)
{
    g_gameHeld = g_gamePressed = g_gamePressedPrev = 0;

    int cfg = g_buttonConfig;
    if (g_demoFlags & 0x8000) cfg = 0;
    if (cfg < 0 || cfg > 5)   cfg = 0;   /* PC safety; original indexes blindly */
    for (int i = 0; i < 3; i++)
        g_actionBtn[i] = kButtonConfig[cfg][i];

    if (!(playerFlags4 & PFLAG4_INPUT_OFF)) {
        g_gameHeld        = g_padPorts[0].held;
        g_gamePressed     = g_padPorts[0].pressed;
        g_gamePressedPrev = g_padPorts[0].pressed_prev;
    }
    if (playerFlags4 & PFLAG4_MIRROR_LR) {
        g_gameHeld    = mirror_lr(g_gameHeld);
        g_gamePressed = mirror_lr(g_gamePressed);
    }
    if (playerFlags4 & PFLAG4_ALT_INPUT)
        return 1;   /* TODO: port SHINOBI 0x060463DC */
    if (g_demoFlags & 0x8000)
        return 2;   /* TODO: port SHINOBI 0x06046A98 (demo input) */
    return 0;
}
