/* Runs the real frame order from the start of 1-1 (drop-in) and saves frames. */
#include <SDL2/SDL.h>
#include <stdio.h>
#include "pad.h"
#include "input.h"
#include "player.h"
#include "ram.h"
#include "render.h"
#include "stage.h"
#include "camera.h"
#include "objects.h"
#include "enemy.h"
#include "fx.h"
#include "objhandlers_c.h"
static InputPort fake[2];
const InputPort *Input_GetPort(int p) { return &fake[p]; }
static SDL_Window *win;
static void frame(uint16_t held, const char *shot)
{
    fake[0].connected = 1; fake[0].raw = (uint16_t)~held;
    W16(0x060143EAu, R16(0x060143EAu) + 1u); W16(0x060F4CCCu, R16(0x060F4CCCu) + 1u);
    Pad_Update(2); Scroll_Update(); Game_LatchPad1(P16(P_FLAGS4)); Player_Tick(); Stage_UpdateState();
    if (R16(0x060252DCu) == 0 && !(P16(P_FLAGS2) & 0x4000)) { W16(0x060252DCu, 1); PW16(P_FLAGS4, P16(P_FLAGS4) & 0xFFEF); }
    Obj_UpdateEnemies(); Obj_UpdateAll(); Projectiles_Update(); Effects_Update(); Items_Update();
    Render_Clear(); Render_DrawFrame();
    if (shot) {
        SDL_Renderer *r = SDL_GetRenderer(win);
        SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, 640, 448, 32, SDL_PIXELFORMAT_ARGB8888);
        SDL_RenderReadPixels(r, NULL, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch);
        SDL_SaveBMP(s, shot); SDL_FreeSurface(s);
    }
    Render_Present();
}
int main(int argc, char **argv)
{
    const char *d = argc > 1 ? argv[1] : "../extracted";
    char p[512];
    SDL_Init(SDL_INIT_VIDEO);
    win = SDL_CreateWindow("shot", 0, 0, 640, 448, 0);
    Render_Init(win);
    snprintf(p, sizeof p, "%s/A.BIN", d); Ram_LoadImage(p, 0x06004000u);
    snprintf(p, sizeof p, "%s/SHINOBI.dec.bin", d); Ram_LoadImage(p, 0x06040000u);
    snprintf(p, sizeof p, "%s/SHOU.CHR", d); Ram_LoadImage(p, 0x00200000u);
    Player_Init(); W16(LIVES, 3); W16(0x060252DCu, 0); W16(G_GAME_MODE, 6);
    Stage_Load(d, 12);
    Stage_SpawnPlayer();
    static char name[32]; int n = 0;
    for (int i = 0; i < 600; i++) {
        uint16_t held = i < 60 ? 0 : (i < 520 ? PAD_RIGHT | ((i & 16) ? PAD_B : 0) : 0);
        if (i % 50 == 0) { snprintf(name, sizeof name, "play_%02d.bmp", n++); frame(held, name); }
        else frame(held, NULL);
    }
    printf("player %.0f,%.0f HP %d mode %d state %04X\n", P32(P_X)/65536.0, P32(P_Y)/65536.0, PS16(P_HP), R16(G_GAME_MODE), R16(G_STAGE_STATE));
    return 0;
}
