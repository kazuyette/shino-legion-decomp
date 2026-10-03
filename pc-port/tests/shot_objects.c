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
static InputPort fake[2];
const InputPort *Input_GetPort(int p) { return &fake[p]; }
static SDL_Window *win;
static void frame(uint16_t held, const char *shot)
{
    fake[0].connected = 1; fake[0].raw = (uint16_t)~held;
    W16(0x060143EAu, R16(0x060143EAu) + 1u); W16(0x060F4CCCu, R16(0x060F4CCCu) + 1u);
    Pad_Update(2); Scroll_Update(); Game_LatchPad1(P16(P_FLAGS4)); Player_Tick(); Obj_UpdateEnemies(); Obj_UpdateAll(); Projectiles_Update(); Effects_Update();
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
    Player_Init();
    Stage_Load(d, 12);
    Stage_SpawnPlayer();
    uint32_t pf = 0; int32_t bx = 0x7FFFFFFF;
    for (int s = 0; s < OBJ_SLOTS; s++) {
        uint32_t o = OBJ_TABLE + (uint32_t)s * OBJ_SIZE;
        if (R16(o) == 0x1004 && RS32(o + 8) < bx) { bx = RS32(o + 8); pf = o; }
    }
    PW32(P_X, (uint32_t)RS32(pf + 8) - 0x400000);
    PW32(P_Y, (uint32_t)(RS32(pf + 0xC) - 0x300000));
    W32(CAM_X, (uint32_t)(P32(P_X) - RS32(0x060EA550u)));
    W32(CAM_Y, (uint32_t)(P32(P_Y) - RS32(0x060EA554u)));
    for (int i = 0; i < 60; i++) frame(0, NULL);
    frame(0, "shot_a.bmp");
    for (int i = 0; i < 25; i++) frame(PAD_RIGHT, NULL);
    for (int i = 0; i < 40; i++) frame(0, i == 8 ? "shot_b.bmp" : NULL);
    frame(0, "shot_c.bmp");
    printf("player %.1f,%.1f air %d\n", P32(P_X)/65536.0, P32(P_Y)/65536.0, (P16(P_FLAGS2)&0x4000)!=0);
    return 0;
}
