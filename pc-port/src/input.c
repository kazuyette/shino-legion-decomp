#include "input.h"
#include "config.h"
#include "pad.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <string.h>

static InputPort s_ports[INPUT_MAX_PORTS];
static SDL_GameController *s_ctrl[INPUT_MAX_PORTS];
static int s_scanTimer;

/* Saturn pad bit of each config button (config.h order: A B C X Y Z L R Start Up Down Left Right). */
static const uint16_t kPadBit[CFG_NBUTTONS] = {
    PAD_A, PAD_B, PAD_C, PAD_X, PAD_Y, PAD_Z, PAD_LT, PAD_RT, PAD_START,
    PAD_UP, PAD_DOWN, PAD_LEFT, PAD_RIGHT
};

#define MAX_BINDS (CFG_NBUTTONS * CFG_MAX_BINDS)
#define TRIGGER_THRESHOLD 16000

/* Mapping resolved from shinobi.ini ([keyboard] / [controller], see config.c for the
 * defaults: keyboard arrows / Z X C / A S D / Q W / Enter; pad Xbox A -> Saturn C (jump),
 * X -> B (sword), B -> A (shuriken), Y -> X (ninjutsu), LB/RB -> Y/Z, LT/RT -> L/R). */
static struct { SDL_Scancode sc; uint16_t bit; } s_keys[MAX_BINDS];
static int s_nkeys;
static struct {
    int axis;                   /* 0: button, 1: axis */
    int id;                     /* SDL_GameControllerButton / SDL_GameControllerAxis */
    int sign;                   /* axis: +1 / -1 */
    uint16_t bit;
} s_padb[MAX_BINDS];
static int s_npad;
static int s_stickDpad = 1, s_deadzone = 12000;
static int s_mapped;
static uint8_t s_suppress[SDL_NUM_SCANCODES];   /* held keys ignored until released */

void Input_ApplyConfig(void)
{
    const Config *c = Config_Get();
    s_nkeys = s_npad = 0;
    for (int b = 0; b < CFG_NBUTTONS; b++) {
        for (int i = 0; i < c->keys[b].n; i++) {
            const char *name = c->keys[b].name[i];
            SDL_Scancode sc = SDL_GetScancodeFromName(name);
            if (sc == SDL_SCANCODE_UNKNOWN) {
                fprintf(stderr, "[input] [keyboard] %s: unknown key name \"%s\"\n", g_cfgButtonNames[b], name);
                continue;
            }
            s_keys[s_nkeys].sc = sc;
            s_keys[s_nkeys++].bit = kPadBit[b];
        }
        for (int i = 0; i < c->pad[b].n; i++) {
            const char *name = c->pad[b].name[i];
            SDL_GameControllerButton btn = SDL_GameControllerGetButtonFromString(name);
            SDL_GameControllerAxis ax = SDL_GameControllerGetAxisFromString(name);  /* skips a +/- prefix */
            if (btn != SDL_CONTROLLER_BUTTON_INVALID) {
                s_padb[s_npad].axis = 0;
                s_padb[s_npad].id = btn;
            } else if (ax != SDL_CONTROLLER_AXIS_INVALID) {
                s_padb[s_npad].axis = 1;
                s_padb[s_npad].id = ax;
                s_padb[s_npad].sign = name[0] == '-' ? -1 : 1;
            } else {
                fprintf(stderr, "[input] [controller] %s: unknown button name \"%s\"\n", g_cfgButtonNames[b], name);
                continue;
            }
            s_padb[s_npad++].bit = kPadBit[b];
        }
    }
    s_stickDpad = c->stick_as_dpad;
    s_deadzone = c->deadzone * 32767 / 100;
    s_mapped = 1;
}

void Input_SuppressKey(int scancode)
{
    if (scancode > 0 && scancode < SDL_NUM_SCANCODES) s_suppress[scancode] = 1;
}

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
    for (int i = 0; i < s_npad; i++) {
        if (!s_padb[i].axis) {
            if (SDL_GameControllerGetButton(c, (SDL_GameControllerButton)s_padb[i].id)) held |= s_padb[i].bit;
        } else {
            int v = SDL_GameControllerGetAxis(c, (SDL_GameControllerAxis)s_padb[i].id) * s_padb[i].sign;
            int trig = s_padb[i].id == SDL_CONTROLLER_AXIS_TRIGGERLEFT || s_padb[i].id == SDL_CONTROLLER_AXIS_TRIGGERRIGHT;
            if (v > (trig ? TRIGGER_THRESHOLD : s_deadzone)) held |= s_padb[i].bit;
        }
    }
    if (s_stickDpad) {
        int lx = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTX);
        int ly = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTY);
        if (lx < -s_deadzone) held |= PAD_LEFT;
        if (lx >  s_deadzone) held |= PAD_RIGHT;
        if (ly < -s_deadzone) held |= PAD_UP;
        if (ly >  s_deadzone) held |= PAD_DOWN;
    }
    return held;
}

/* Keyboard (port 0), mapped by [keyboard]. Keyboard and controller 1 are combined. */
static uint16_t read_keyboard(void)
{
    const Uint8 *k = SDL_GetKeyboardState(NULL);
    uint16_t held = 0;
    for (int sc = 1; sc < SDL_NUM_SCANCODES; sc++)
        if (s_suppress[sc] && !k[sc]) s_suppress[sc] = 0;
    for (int i = 0; i < s_nkeys; i++)
        if (k[s_keys[i].sc] && !s_suppress[s_keys[i].sc]) held |= s_keys[i].bit;
    return held;
}

void Input_Update(void)
{
    if (!s_mapped) Input_ApplyConfig();
    if (--s_scanTimer <= 0) { scan_controllers(); s_scanTimer = 30; }

    uint16_t p0 = read_keyboard() | read_controller(s_ctrl[0]);
    /* a real pad can't press opposite directions together */
    if ((p0 & (PAD_LEFT | PAD_RIGHT)) == (PAD_LEFT | PAD_RIGHT)) p0 &= (uint16_t)~(PAD_LEFT | PAD_RIGHT);
    if ((p0 & (PAD_UP | PAD_DOWN)) == (PAD_UP | PAD_DOWN))       p0 &= (uint16_t)~(PAD_UP | PAD_DOWN);
    s_ports[0].connected = 1;
    s_ports[0].raw = (uint16_t)~p0;                    /* hardware is active-low */

    uint16_t p1 = read_controller(s_ctrl[1]);
    if ((p1 & (PAD_LEFT | PAD_RIGHT)) == (PAD_LEFT | PAD_RIGHT)) p1 &= (uint16_t)~(PAD_LEFT | PAD_RIGHT);
    if ((p1 & (PAD_UP | PAD_DOWN)) == (PAD_UP | PAD_DOWN))       p1 &= (uint16_t)~(PAD_UP | PAD_DOWN);
    s_ports[1].connected = s_ctrl[1] != NULL;
    s_ports[1].raw = (uint16_t)~p1;
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
