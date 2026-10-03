#include "input.h"
#include "pad.h"
#include <SDL2/SDL.h>
#include <stdio.h>

static InputPort s_ports[INPUT_MAX_PORTS];
static SDL_GameController *s_ctrl[INPUT_MAX_PORTS];
static int s_scanTimer;

#define STICK_DEADZONE 12000

/* Default gamepad mapping (Xbox naming; PlayStation: A=Cross, B=Circle,
 * X=Square, Y=Triangle). Chosen for comfort with the game's default
 * button layout (Saturn A = shuriken, B = sword/guard, C = jump):
 *   A -> Saturn C (jump)      X -> Saturn B (sword, hold = guard)
 *   B -> Saturn A (shuriken)  Y -> Saturn X (ninjutsu)
 *   LB -> Saturn Y (guard)    RB -> Saturn Z
 *   LT -> Saturn L            RT -> Saturn R
 *   Start -> Start, D-pad / left stick -> directions. */
static const struct { SDL_GameControllerButton b; uint16_t pad; } kButtons[] = {
    { SDL_CONTROLLER_BUTTON_A,             PAD_C },
    { SDL_CONTROLLER_BUTTON_X,             PAD_B },
    { SDL_CONTROLLER_BUTTON_B,             PAD_A },
    { SDL_CONTROLLER_BUTTON_Y,             PAD_X },
    { SDL_CONTROLLER_BUTTON_LEFTSHOULDER,  PAD_Y },
    { SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, PAD_Z },
    { SDL_CONTROLLER_BUTTON_START,         PAD_START },
    { SDL_CONTROLLER_BUTTON_DPAD_UP,       PAD_UP },
    { SDL_CONTROLLER_BUTTON_DPAD_DOWN,     PAD_DOWN },
    { SDL_CONTROLLER_BUTTON_DPAD_LEFT,     PAD_LEFT },
    { SDL_CONTROLLER_BUTTON_DPAD_RIGHT,    PAD_RIGHT },
};

/* Opens newly plugged controllers / drops unplugged ones (checked about
 * twice a second, so hot-plugging works without event plumbing). */
static void scan_controllers(void)
{
    for (int p = 0; p < INPUT_MAX_PORTS; p++) {
        if (s_ctrl[p] && !SDL_GameControllerGetAttached(s_ctrl[p])) {
            printf("[input] controller %d disconnected\n", p + 1);
            SDL_GameControllerClose(s_ctrl[p]);
            s_ctrl[p] = NULL;
        }
    }
    for (int j = 0; j < SDL_NumJoysticks(); j++) {
        if (!SDL_IsGameController(j)) continue;
        SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(j);
        int used = 0, freeSlot = -1;
        for (int p = 0; p < INPUT_MAX_PORTS; p++) {
            if (s_ctrl[p] && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(s_ctrl[p])) == id) used = 1;
            if (!s_ctrl[p] && freeSlot < 0) freeSlot = p;
        }
        if (used || freeSlot < 0) continue;
        s_ctrl[freeSlot] = SDL_GameControllerOpen(j);
        if (s_ctrl[freeSlot])
            printf("[input] controller %d: %s\n", freeSlot + 1, SDL_GameControllerName(s_ctrl[freeSlot]));
    }
}

static uint16_t read_controller(SDL_GameController *c)
{
    uint16_t held = 0;
    if (!c) return 0;
    for (size_t i = 0; i < sizeof kButtons / sizeof kButtons[0]; i++)
        if (SDL_GameControllerGetButton(c, kButtons[i].b)) held |= kButtons[i].pad;
    int lx = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTX);
    int ly = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTY);
    if (lx < -STICK_DEADZONE) held |= PAD_LEFT;
    if (lx >  STICK_DEADZONE) held |= PAD_RIGHT;
    if (ly < -STICK_DEADZONE) held |= PAD_UP;
    if (ly >  STICK_DEADZONE) held |= PAD_DOWN;
    if (SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERLEFT)  > 16000) held |= PAD_LT;
    if (SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 16000) held |= PAD_RT;
    return held;
}

/* Keyboard (port 0): arrows = D-pad, Z/X/C = A/B/C, A/S/D = X/Y/Z,
 * Q/W = L/R, Enter = Start. Keyboard and controller 1 are combined. */
static uint16_t read_keyboard(void)
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
    return held;
}

void Input_Update(void)
{
    if (--s_scanTimer <= 0) { scan_controllers(); s_scanTimer = 30; }

    uint16_t p0 = read_keyboard() | read_controller(s_ctrl[0]);
    /* a real pad can't press opposite directions together */
    if ((p0 & (PAD_LEFT | PAD_RIGHT)) == (PAD_LEFT | PAD_RIGHT)) p0 &= (uint16_t)~(PAD_LEFT | PAD_RIGHT);
    if ((p0 & (PAD_UP | PAD_DOWN)) == (PAD_UP | PAD_DOWN))       p0 &= (uint16_t)~(PAD_UP | PAD_DOWN);
    s_ports[0].connected = 1;
    s_ports[0].raw = (uint16_t)~p0;                    /* hardware is active-low */

    s_ports[1].connected = s_ctrl[1] != NULL;
    s_ports[1].raw = (uint16_t)~read_controller(s_ctrl[1]);
}

const InputPort *Input_GetPort(int port)
{
    return (port >= 0 && port < INPUT_MAX_PORTS) ? &s_ports[port] : NULL;
}

void Input_Shutdown(void)
{
    for (int p = 0; p < INPUT_MAX_PORTS; p++)
        if (s_ctrl[p]) { SDL_GameControllerClose(s_ctrl[p]); s_ctrl[p] = NULL; }
}
