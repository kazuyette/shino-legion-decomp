#include <SDL2/SDL.h>
#include <stdio.h>
#include "pad.h"
#include "input.h"
#include "player.h"
#include "ram.h"
#include "render.h"
static InputPort fake[2];
const InputPort *Input_GetPort(int p) { return &fake[p]; }
static SDL_Window *win;
static void frame(uint16_t held, const char *shot)
{
    fake[0].connected = 1; fake[0].raw = (uint16_t)~held;
    Pad_Update(2); Game_LatchPad1(P16(P_FLAGS4)); Player_Tick();
    Render_Clear(); Render_DrawFrame();
    if (shot) {
        SDL_Renderer *r = SDL_GetRenderer(win);
        SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, 640, 448, 32, SDL_PIXELFORMAT_ARGB8888);
        SDL_RenderReadPixels(r, NULL, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch);
        SDL_SaveBMP(s, shot); SDL_FreeSurface(s);
    }
    Render_Present();
}
int main(void)
{
    SDL_Init(SDL_INIT_VIDEO);
    win = SDL_CreateWindow("shot", 0, 0, 640, 448, 0);
    Render_Init(win);
    Ram_LoadImage("assets/A.BIN", 0x06004000u); Ram_LoadImage("assets/SHINOBI.dec.bin", 0x06040000u);
    Ram_LoadImage("assets/SHOU.CHR", 0x00200000u);
    Player_Init();
    for (int i = 0; i < 20; i++) frame(0, i == 19 ? "s1_idle.bmp" : NULL);
    for (int i = 0; i < 20; i++) frame(PAD_RIGHT, i == 12 ? "s2_walk.bmp" : NULL);
    for (int i = 0; i < 40; i++) frame(0, NULL);
    frame(PAD_B, NULL); for (int i = 0; i < 12; i++) frame(0, i == 6 ? "s3_slash.bmp" : NULL);
    for (int i = 0; i < 60; i++) frame(0, NULL);
    for (int i = 0; i < 25; i++) frame(PAD_C, i == 18 ? "s4_jump.bmp" : NULL);
    for (int i = 0; i < 80; i++) frame(0, NULL);
    for (int i = 0; i < 10; i++) frame(PAD_DOWN | PAD_LEFT, i == 9 ? "s5_crouch_left.bmp" : NULL);
    for (int i = 0; i < 30; i++) frame(0, NULL);
    frame(PAD_Y, NULL); for (int i = 0; i < 10; i++) frame(PAD_Y, i == 9 ? "s6_guard.bmp" : NULL);
    return 0;
}
