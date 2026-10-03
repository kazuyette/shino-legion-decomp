#include <SDL2/SDL.h>
#include "render.h"
#include "ram.h"
#include "lz.h"
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

void Render_InvalidatePalettes(void)
{
    for (int i = 0; i < CACHE_SIZE; i++) {
        if (s_cache[i].tex) SDL_DestroyTexture(s_cache[i].tex);
        s_cache[i].tex = NULL;
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
    /* PC placeholder ground line matching the flat-floor collision (Y = 192). */
    SDL_SetRenderDrawColor(s_renderer, 70, 90, 70, 255);
    SDL_Rect ground = { 0, 192, SCREEN_W, SCREEN_H - 192 };
    SDL_RenderFillRect(s_renderer, &ground);

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
