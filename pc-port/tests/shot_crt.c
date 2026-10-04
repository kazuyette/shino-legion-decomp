/* CRT filter (src/crt.c) check: boots stage 1-1 with the real renderer on SDL's offscreen
 * video driver (software renderer), then for each window size and filter mode redraws the
 * same frame, times Render_Present and saves the window picture as OUTDIR/crt_<mode>_<w>x<h>.png
 * (uncompressed PNG, screenshot.c).
 *   SDL_VIDEODRIVER=offscreen ./shot_crt ASSETS OUTDIR [frames=200] [WxH ...]
 * Build: the CMakeLists.txt sources without src/main.c and src/input.c, plus this file:
 *   gcc -std=c11 -O2 -Isrc -I<generated version.h dir> tests/shot_crt.c <sources> $(sdl2-config --cflags --libs) -lm */
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include "boot.h"
#include "config.h"
#include "crt.h"
#include "input.h"
#include "ram.h"
#include "render.h"
#include "resource.h"
#include "screenshot.h"

static InputPort fake[2];
const InputPort *Input_GetPort(int p) { return &fake[p]; }
void Input_Update(void) {}
void Input_Shutdown(void) {}
void Input_ApplyConfig(void) {}
void Input_SuppressKey(int sc) { (void)sc; }

static void draw(void) { Render_BeginFrame(); Render_Clear(); Render_DrawFrame(); }

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: shot_crt ASSETS OUTDIR [frames] [WxH ...]\n"); return 1; }
    const char *assets = argv[1], *out = argv[2];
    int frames = argc > 3 ? atoi(argv[3]) : 200;
    char p[1200];
    snprintf(p, sizeof p, "%s/A.BIN", assets);
    if (!Ram_LoadImage(p, 0x06004000u)) { fprintf(stderr, "%s missing\n", p); return 1; }
    Res_SetAssetDir(assets);
    Config_SetDefaults(&g_config);
    if (getenv("CRT_STRENGTH")) g_config.crt_strength = atoi(getenv("CRT_STRENGTH"));
    if (getenv("CRT_MASK")) g_config.crt_mask = atoi(getenv("CRT_MASK"));
    if (getenv("CRT_CURV")) g_config.crt_curvature = atoi(getenv("CRT_CURV"));
    if (getenv("CRT_INT")) g_config.integer_scaling = atoi(getenv("CRT_INT"));
    if (getenv("CRT_43")) g_config.aspect43 = atoi(getenv("CRT_43"));
    g_config.vsync = 0;
    for (int i = 0; i < 2; i++) { fake[i].connected = 1; fake[i].raw = 0xFFFF; }
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
    SDL_Window *win = SDL_CreateWindow("crt", 0, 0, 960, 672, 0);
    if (!win || !Render_Init(win)) { fprintf(stderr, "window: %s\n", SDL_GetError()); return 1; }
    SDL_Renderer *r = SDL_GetRenderer(win);
    Boot_Init(0);
    Boot_DevStartStage(getenv("CRT_STAGE") ? atoi(getenv("CRT_STAGE")) : 0);
    for (int f = 0; f < frames; f++) { Boot_Frame(); draw(); Render_Present(); }
    Render_SetKeepQueue(1);                         /* same frame from now on */

    static const int defSizes[][2] = { { 1280, 960 }, { 1920, 1080 }, { 3840, 2160 } };
    int nsizes = argc > 4 ? argc - 4 : 3;
    const Uint64 freq = SDL_GetPerformanceFrequency();
    for (int si = 0; si < nsizes; si++) {
        int w = defSizes[si < 3 ? si : 0][0], h = defSizes[si < 3 ? si : 0][1];
        if (argc > 4 && sscanf(argv[4 + si], "%dx%d", &w, &h) != 2) continue;
        /* new window + renderer per size (resizing an offscreen window leaves the software
         * renderer's surface stale) */
        Render_Shutdown();
        SDL_DestroyWindow(win);
        win = SDL_CreateWindow("crt", 0, 0, w, h, 0);
        if (!win || !Render_Init(win)) { fprintf(stderr, "window: %s\n", SDL_GetError()); return 1; }
        r = SDL_GetRenderer(win);
        for (int m = 0; m < CRT_NMODES; m++) {
            Crt_SetMode(m);
            draw(); 
            Uint64 t0 = SDL_GetPerformanceCounter();
            Render_Present();                       /* first one builds the overlays */
            double first = (double)(SDL_GetPerformanceCounter() - t0) * 1000.0 / freq;
            int n = h > 1500 ? 10 : 30;
            double sum = 0;
            for (int i = 0; i < n; i++) {
                draw();
                t0 = SDL_GetPerformanceCounter();
                Render_Present();
                sum += (double)(SDL_GetPerformanceCounter() - t0);
            }
            printf("%dx%d %-9s Render_Present %.2f ms (first, with overlay build %.2f ms)\n",
                   w, h, Crt_ModeName(m), sum * 1000.0 / freq / n, first);
            if (getenv("CRT_TOAST")) { Crt_Toast(getenv("CRT_TOAST")); draw(); Render_Present(); }
            int ow, oh;
            SDL_GetRendererOutputSize(r, &ow, &oh);
            uint8_t *rgb = (uint8_t *)malloc((size_t)ow * oh * 3);
            int lw, lh;                             /* read the whole window, not the viewport */
            SDL_RenderGetLogicalSize(r, &lw, &lh);
            SDL_RenderSetLogicalSize(r, 0, 0);
            SDL_RenderReadPixels(r, NULL, SDL_PIXELFORMAT_RGB24, rgb, ow * 3);
            SDL_RenderSetLogicalSize(r, lw, lh);
            snprintf(p, sizeof p, "%s/crt_%s_%dx%d%s.png", out, Crt_ModeName(m), w, h,
                     getenv("CRT_TAG") ? getenv("CRT_TAG") : "");
            if (!Screenshot_WritePng(p, rgb, ow, oh)) fprintf(stderr, "cannot write %s\n", p);
            free(rgb);
        }
    }
    Render_Shutdown();
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
