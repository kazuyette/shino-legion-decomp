#include "input.h"
#include "pad.h"
#include <SDL2/SDL.h>

static InputPort s_ports[INPUT_MAX_PORTS];

/* Default keymap (port 0). Arrows = D-pad, Z/X/C = A/B/C, A/S/D = X/Y/Z,
 * Q/W = L/R triggers, Enter = Start. Port 1 is reported disconnected. */
void Input_Update(void)
{
    const Uint8 *k = SDL_GetKeyboardState(NULL);
    uint16_t held = 0;

    if (k[SDL_SCANCODE_RIGHT])  held |= PAD_RIGHT;
    if (k[SDL_SCANCODE_LEFT])   held |= PAD_LEFT;
    if (k[SDL_SCANCODE_DOWN])   held |= PAD_DOWN;
    if (k[SDL_SCANCODE_UP])     held |= PAD_UP;
    if (k[SDL_SCANCODE_RETURN]) held |= PAD_START;
    if (k[SDL_SCANCODE_Z])      held |= PAD_A;
    if (k[SDL_SCANCODE_X])      held |= PAD_B;
    if (k[SDL_SCANCODE_C])      held |= PAD_C;
    if (k[SDL_SCANCODE_W])      held |= PAD_RT;
    if (k[SDL_SCANCODE_A])      held |= PAD_X;
    if (k[SDL_SCANCODE_S])      held |= PAD_Y;
    if (k[SDL_SCANCODE_D])      held |= PAD_Z;
    if (k[SDL_SCANCODE_Q])      held |= PAD_LT;

    s_ports[0].connected = 1;
    s_ports[0].raw = (uint16_t)~held;      /* hardware is active-low */
    s_ports[1].connected = 0;
    s_ports[1].raw = 0xFFFF;
}

const InputPort *Input_GetPort(int port)
{
    return (port >= 0 && port < INPUT_MAX_PORTS) ? &s_ports[port] : NULL;
}
