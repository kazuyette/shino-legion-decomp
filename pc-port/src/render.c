#include <SDL2/SDL.h>
#include "render.h"
#include "ram.h"
#include "lz.h"
#include "sprite.h"
#include "stage.h"
#include <stdio.h>
#include <string.h>

static SDL_Renderer *s_renderer = NULL;

#define MAX_QUEUE 256
static RenderSprite s_queue[MAX_QUEUE];
static int s_queued;

/* Texture cache keyed by (src, palette). */
#define CACHE_SIZE 1024
typedef struct { uint32_t src, pal; SDL_Texture *tex; } CacheEntry;
static CacheEntry s_cache[CACHE_SIZE];

int Render_Init(SDL_Window *window)
{
    s_renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!s_renderer) s_renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (!s_renderer) {
        fprintf(stderr, "[render] SDL_CreateRenderer failed: %s\n", SDL_GetError());
        return 0;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    SDL_RenderSetLogicalSize(s_renderer, SCREEN_W, SCREEN_H);
    return 1;
}

/* ---- VDP2 background layers (stage maps) ---- */
#define LAYERS     3
#define LAYER_DESC 0x060EA588u
static SDL_Texture *s_pageTex[LAYERS][256];

void Render_InvalidatePalettes(void)
{
    for (int i = 0; i < CACHE_SIZE; i++) {
        if (s_cache[i].tex) SDL_DestroyTexture(s_cache[i].tex);
        s_cache[i].tex = NULL;
    }
    for (int l = 0; l < LAYERS; l++)
        for (int p = 0; p < 256; p++)
            if (s_pageTex[l][p]) { SDL_DestroyTexture(s_pageTex[l][p]); s_pageTex[l][p] = NULL; }
}

/* One 512x512 page = 32x32 pattern names (1 word: bit15 H-flip (guess),
 * bits 14-12 palette bank, bits 9-0 16x16 8bpp tile = 4 cells TL,TR,BL,BR). */
static SDL_Texture *page_texture(int l, int pid)
{
    if (s_pageTex[l][pid]) return s_pageTex[l][pid];
    static uint32_t argb[512 * 512];
    uint32_t d = LAYER_DESC + (uint32_t)l * 0x30;
    uint32_t pages = R32(d + 0x1C) + (uint32_t)pid * 0x800;
    uint32_t chr = VDP2_VRAM_BASE + R32(d + 0x2C) * 0x20;
    unsigned craof = (R16(VDP2_SHADOW_ADDR + 4) >> (4 * l)) & 7;   /* CRAOFA: per-layer CRAM offset */
    for (int ty = 0; ty < 32; ty++)
        for (int tx = 0; tx < 32; tx++) {
            uint16_t pn = R16(pages + (uint32_t)(ty * 32 + tx) * 2);
            uint32_t tile = chr + (uint32_t)(pn & 0x3FF) * 128;
            uint32_t pal = SHADOW_CRAM + 2u * (((((pn >> 12) & 7) + craof) & 7) * 256u);
            for (int y = 0; y < 16; y++)
                for (int x = 0; x < 16; x++) {
                    int sx = (pn & 0x8000) ? 15 - x : x;
                    uint32_t cell = (uint32_t)((y >> 3) * 2 + (sx >> 3));
                    uint8_t v = Ram_Ptr(tile + cell * 64 + (uint32_t)(y & 7) * 8 + (uint32_t)(sx & 7))[0];
                    uint32_t c = 0;
                    if (v) {
                        uint16_t rgb = R16(pal + 2u * v);
                        c = 0xFF000000u | ((rgb & 31) * 255 / 31) << 16 | (((rgb >> 5) & 31) * 255 / 31) << 8 |
                            (((rgb >> 10) & 31) * 255 / 31);
                    }
                    argb[(ty * 16 + y) * 512 + tx * 16 + x] = c;
                }
        }
    SDL_Texture *t = SDL_CreateTexture(s_renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, 512, 512);
    if (!t) return NULL;
    SDL_UpdateTexture(t, NULL, argb, 512 * 4);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    s_pageTex[l][pid] = t;
    return t;
}

static int floordiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
static int wrap(int a, int n) { int r = a % n; return r < 0 ? r + n : r; }

/* Layer scroll = VDP2 scroll register = layer position (desc +0/+4, computed
 * by Scroll_Update / 0x060470A4) - 1024 px. */
static void draw_layer(int l)
{
    uint32_t d = LAYER_DESC + (uint32_t)l * 0x30;
    uint32_t lay = R32(d + 0x20);
    if (!lay || !R32(d + 0x1C)) return;
    int pw = Ram_Ptr(lay)[0], ph = Ram_Ptr(lay)[1];
    if (pw <= 0 || ph <= 0) return;
    int sx = (RS32(d + 0) >> 16) - 1024;
    int sy = (RS32(d + 4) >> 16) - 1024;
    for (int py = floordiv(sy, 512); py * 512 < sy + SCREEN_H; py++)
        for (int px = floordiv(sx, 512); px * 512 < sx + SCREEN_W; px++) {
            uint8_t pid = Ram_Ptr(lay)[2 + wrap(py, ph) * pw + wrap(px, pw)];
            if (pid == 0xFF) continue;
            SDL_Texture *t = page_texture(l, pid);
            if (!t) continue;
            SDL_Rect dst = { px * 512 - sx, py * 512 - sy, 512, 512 };
            SDL_RenderCopy(s_renderer, t, NULL, &dst);
        }
}

static SDL_Texture *get_texture(const RenderSprite *s)
{
    unsigned h = (s->src * 2654435761u ^ s->palette * 40503u) % CACHE_SIZE;
    for (int probe = 0; probe < CACHE_SIZE; probe++, h = (h + 1) % CACHE_SIZE) {
        CacheEntry *e = &s_cache[h];
        if (e->tex && e->src == s->src && e->pal == s->palette) return e->tex;
        if (!e->tex) {
            static uint8_t px[512 * 512];
            static uint32_t argb[512 * 512];
            size_t n = (size_t)s->w * (size_t)s->h;
            if (n == 0 || n > sizeof(px)) return NULL;
            size_t avail = 0x00100000u - (s->src & 0x000FFFFFu);
            if (!Lz_Decompress(Ram_Ptr(s->src), avail, px, sizeof(px))) return NULL;
            for (size_t i = 0; i < n; i++) {
                if (px[i] == 0) { argb[i] = 0; continue; }           /* index 0 = transparent */
                uint16_t c = R16(s->palette + 2u * px[i]);
                uint32_t r = (c & 31) * 255 / 31, g = ((c >> 5) & 31) * 255 / 31, b = ((c >> 10) & 31) * 255 / 31;
                argb[i] = 0xFF000000u | r << 16 | g << 8 | b;
            }
            SDL_Texture *t = SDL_CreateTexture(s_renderer, SDL_PIXELFORMAT_ARGB8888,
                                               SDL_TEXTUREACCESS_STATIC, s->w, s->h);
            if (!t) return NULL;
            SDL_UpdateTexture(t, NULL, argb, s->w * 4);
            SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
            e->src = s->src; e->pal = s->palette; e->tex = t;
            return t;
        }
    }
    return NULL;
}

void Render_QueueSprite(const RenderSprite *s)
{
    if (s_queued < MAX_QUEUE) s_queue[s_queued++] = *s;
}

void Render_Clear(void)
{
    if (!s_renderer) return;
    SDL_SetRenderDrawColor(s_renderer, 24, 32, 56, 255);
    SDL_RenderClear(s_renderer);
}

void Render_DrawFrame(void)
{
    if (!s_renderer) { s_queued = 0; return; }
    if (R16(0x060250A8u)) { Render_InvalidatePalettes(); W16(0x060250A8u, 0); }   /* CRAM changed */
    for (int l = LAYERS - 1; l >= 0; l--) draw_layer(l);

    for (int i = 0; i < s_queued; i++) {
        const RenderSprite *s = &s_queue[i];
        SDL_Texture *t = get_texture(s);
        if (!t) continue;
        SDL_Rect dst = { s->x, s->y, s->w, s->h };
        SDL_RendererFlip f = (SDL_RendererFlip)((s->flipH ? SDL_FLIP_HORIZONTAL : 0) |
                                                (s->flipV ? SDL_FLIP_VERTICAL : 0));
        double ang = s->rotate ? s->angle * 360.0 / 256.0 : 0.0;
        SDL_Point c = { s->cx - s->x, s->cy - s->y };
        SDL_RenderCopyEx(s_renderer, t, NULL, &dst, ang, s->rotate ? &c : NULL, f);
    }
    s_queued = 0;
}

void Render_Present(void)
{
    if (s_renderer) SDL_RenderPresent(s_renderer);
}

void Render_Shutdown(void)
{
    Render_InvalidatePalettes();
    if (s_renderer) { SDL_DestroyRenderer(s_renderer); s_renderer = NULL; }
}
