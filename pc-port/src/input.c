#include "input.h"
#include <SDL2/SDL.h>

static PadState s_pad;

/* Default keymap -- arbitrary, easy to change later. Roughly follows the
 * common "Saturn pad on a keyboard" convention used by several emulators:
 * arrows = D-pad, Z/X/C = A/B/C, A/S/D = X/Y/Z, Q/W = L/R, Enter = Start. */
void Input_Update(void)
{
    const Uint8 *keys = SDL_GetKeyboardState(NULL);

    s_pad.up    = keys[SDL_SCANCODE_UP];
    s_pad.down  = keys[SDL_SCANCODE_DOWN];
    s_pad.left  = keys[SDL_SCANCODE_LEFT];
    s_pad.right = keys[SDL_SCANCODE_RIGHT];

    s_pad.a = keys[SDL_SCANCODE_Z];
    s_pad.b = keys[SDL_SCANCODE_X];
    s_pad.c = keys[SDL_SCANCODE_C];

    s_pad.x = keys[SDL_SCANCODE_A];
    s_pad.y = keys[SDL_SCANCODE_S];
    s_pad.z = keys[SDL_SCANCODE_D];

    s_pad.l = keys[SDL_SCANCODE_Q];
    s_pad.r = keys[SDL_SCANCODE_W];

    s_pad.start = keys[SDL_SCANCODE_RETURN];
}

const PadState *Input_GetPadState(void)
{
    return &s_pad;
}
