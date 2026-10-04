#include <SDL2/SDL.h>
#include "render.h"
#include "fade.h"
#include "ram.h"
#include "sprite.h"
#include "stage.h"
#include "soft_render.h"
#include "resource.h"
#include "stage_hooks.h"
#include "config.h"
#include "screenshot.h"
#include "crt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static SDL_Renderer *s_renderer = NULL;

#define MAX_QUEUE 256
static RenderSprite s_queue[MAX_QUEUE];
static int s_queued;
static int s_keepQueue;     /* PC debug menu: redraw the last frame while paused */

/* Texture cache keyed by (src, mode, colr, size). */
#define CACHE_SIZE 1024
typedef struct { uint32_t src, key; int w, h; SDL_Texture *tex; } CacheEntry;
static CacheEntry s_cache[CACHE_SIZE];

/* PC output (shinobi.ini [video]): the frame is drawn at 320x224 into s_scene (Render_BeginFrame),
 * then Render_Present scales it to the window: SDL logical size (letterbox), optional integer
 * scale, nearest or bilinear filter, square pixels or 4:3. Without render-target support
 * everything is drawn straight to the window (no bilinear / 4:3). */
static SDL_Window *s_window;
static SDL_Texture *s_scene;
static int s_shotPending;

static int sprite_layers_ok(void);

static SDL_Renderer *create_renderer(SDL_Window *window, int vsync)
{
    SDL_Renderer *r = NULL;
    if (vsync) r = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!r) r = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!r) r = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    return r;
}

int Render_Init(SDL_Window *window)
{
    const Config *cfg = Config_Get();
    s_window = window;
    SDL_SetHint(SDL_HINT_RENDER_VSYNC, cfg->vsync ? "1" : "0");   /* also for the fallbacks */
    /* shinobi.ini [video] renderer: SDL render driver (auto = SDL's choice: direct3d on Windows) */
    char avail[256] = "";
    for (int i = 0; i < SDL_GetNumRenderDrivers(); i++) {
        SDL_RendererInfo di;
        if (SDL_GetRenderDriverInfo(i, &di) == 0 && strlen(avail) + strlen(di.name) + 3 < sizeof avail) {
            if (*avail) strcat(avail, ", ");
            strcat(avail, di.name);
        }
    }
    const char *want = cfg->renderer[0] ? cfg->renderer : "auto";
    if (strcmp(want, "auto") != 0) SDL_SetHint(SDL_HINT_RENDER_DRIVER, want);
    s_renderer = create_renderer(window, cfg->vsync);
    if (!s_renderer && strcmp(want, "auto") != 0) {
        fprintf(stderr, "[render] renderer \"%s\" not available (%s): automatic choice\n", want, SDL_GetError());
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, "");
        s_renderer = create_renderer(window, cfg->vsync);
    }
    if (!s_renderer) {
        fprintf(stderr, "[render] SDL_CreateRenderer failed: %s\n", SDL_GetError());
        return 0;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");   /* all game textures */
    int logicalH = SCREEN_H;
    if (SDL_RenderTargetSupported(s_renderer))
        s_scene = SDL_CreateTexture(s_renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, SCREEN_W, SCREEN_H);
    if (s_scene) {
        SDL_SetTextureBlendMode(s_scene, SDL_BLENDMODE_NONE);
        SDL_SetTextureScaleMode(s_scene, cfg->bilinear ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
        if (cfg->aspect43) logicalH = SCREEN_W * 3 / 4;       /* 320x240: 224 lines shown 4:3 */
    } else fprintf(stderr, "[render] no render-target support: bilinear, 4:3 and screenshots unavailable\n");
    SDL_RenderSetLogicalSize(s_renderer, SCREEN_W, logicalH);
    SDL_RenderSetIntegerScale(s_renderer, cfg->integer_scaling ? SDL_TRUE : SDL_FALSE);
    if (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) SDL_ShowCursor(SDL_DISABLE);
    SDL_RendererInfo info;
    if (SDL_GetRendererInfo(s_renderer, &info) == 0) {
        printf("[render] renderer: %s (shinobi.ini renderer = %s; available: %s)\n", info.name, want, avail);
        if (strcmp(want, "auto") != 0 && strcmp(want, info.name) != 0)
            fprintf(stderr, "[render] requested renderer \"%s\" not available: using %s\n", want, info.name);
        printf("[render] %s%s, %s%s%s\n", info.name, (info.flags & SDL_RENDERER_PRESENTVSYNC) ? " + vsync" : "",
               cfg->bilinear && s_scene ? "bilinear" : "nearest", cfg->integer_scaling ? ", integer scale" : "",
               cfg->aspect43 && s_scene ? ", 4:3" : "");
    }
    printf("[render] VDP1 framebuffer emulation: %s\n", sprite_layers_ok()
           ? "sprite layer targets (custom blend modes verified)"
           : "portable path (sprites grouped by priority)");
    Crt_Init(cfg->crt, cfg->crt_strength, cfg->crt_mask, cfg->crt_curvature);   /* crt.c, F10 */
    if (cfg->crt && s_scene)
        printf("[render] CRT filter: %s, strength %d, mask %s, curvature %d\n", Crt_ModeName(cfg->crt),
               cfg->crt_strength, Crt_MaskName(cfg->crt_mask), cfg->crt_curvature);
    return 1;
}

/* ---- VDP2 background layers ----
 * NBG0..NBG2 = the stage map layers, drawn page by page from the MAP data (layout desc+0x20,
 * pages desc+0x1C) with the pattern-name format of 0x06046B14 (decoder in soft_render.c), or,
 * for the layers the stage does not stream, from their VDP2 name tables (Vdp2_LayerPageAt);
 * NBG3 = the status-bar layer, drawn from its VDP2 VRAM page (map built by 0x06047244). */
#define LAYERS 4
static SDL_Texture *s_pageTex[3][VDP2_PAGE_IDS];    /* MAP pages 0..255, VDP2 VRAM pages above (soft_render.h) */
static uint32_t s_pageSum[3][VDP2_PAGE_IDS];   /* PC: checksum of the page's name table (map cells change at runtime) */
static SDL_Texture *s_nbg3Tex;
static uint32_t s_nbg3Sum;
/* Page variants = the pixels of a page selected by their flags (Vdp2_DecodePageSel):
 *  - special priority per character (SFPRMD mode 1, all stages): split by the SPR bit, drawn at
 *    priority (p & 6) and (p & 6) | 1 (2-1's grass in front of Sho's feet, 1-1's temple pillar
 *    in front of Sho; checked against the real game);
 *  - colour calculation per character / colour MSB (SFCCMD 1 / 3): split into the blended and
 *    the opaque pixels.
 * Variant index = SPR value | CC value << 1; the selection mask is part of the cache key. */
#define NVAR 4
static SDL_Texture *s_varTex[NVAR][LAYERS][VDP2_PAGE_IDS];
static uint32_t s_varSum[NVAR][LAYERS][VDP2_PAGE_IDS];

void Render_InvalidatePalettes(void)
{
    for (int i = 0; i < CACHE_SIZE; i++) {
        if (s_cache[i].tex) SDL_DestroyTexture(s_cache[i].tex);
        s_cache[i].tex = NULL;
    }
    for (int l = 0; l < 3; l++)
        for (int p = 0; p < VDP2_PAGE_IDS; p++)
            if (s_pageTex[l][p]) { SDL_DestroyTexture(s_pageTex[l][p]); s_pageTex[l][p] = NULL; }
    if (s_nbg3Tex) { SDL_DestroyTexture(s_nbg3Tex); s_nbg3Tex = NULL; }
    for (int v = 0; v < NVAR; v++)
        for (int l = 0; l < LAYERS; l++)
            for (int p = 0; p < VDP2_PAGE_IDS; p++)
                if (s_varTex[v][l][p]) { SDL_DestroyTexture(s_varTex[v][l][p]); s_varTex[v][l][p] = NULL; }
}

static SDL_Texture *argb_texture(const uint32_t *argb)
{
    SDL_Texture *t = SDL_CreateTexture(s_renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC,
                                       VDP2_PAGE_PX, VDP2_PAGE_PX);
    if (!t) return NULL;
    SDL_UpdateTexture(t, NULL, argb, VDP2_PAGE_PX * 4);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    return t;
}

static SDL_Texture *page_texture(int l, int pid)
{
    static uint32_t argb[VDP2_PAGE_PX * VDP2_PAGE_PX];
    uint32_t sum = Vdp2_MapPageChecksum(l, pid);
    if (s_pageTex[l][pid] && s_pageSum[l][pid] != sum) { SDL_DestroyTexture(s_pageTex[l][pid]); s_pageTex[l][pid] = NULL; }
    s_pageSum[l][pid] = sum;
    if (s_pageTex[l][pid]) return s_pageTex[l][pid];
    Vdp2_DecodeMapPage(l, pid, argb);
    return s_pageTex[l][pid] = argb_texture(argb);
}

static SDL_Texture *nbg3_texture(void)
{
    static uint32_t argb[VDP2_PAGE_PX * VDP2_PAGE_PX];
    uint32_t sum = Vdp2_Nbg3Checksum();
    if (s_nbg3Tex && s_nbg3Sum != sum) { SDL_DestroyTexture(s_nbg3Tex); s_nbg3Tex = NULL; }
    s_nbg3Sum = sum;
    if (s_nbg3Tex) return s_nbg3Tex;
    Vdp2_DecodeNbg3(argb);
    return s_nbg3Tex = argb_texture(argb);
}

static SDL_Texture *var_texture(int l, int pid, int var, unsigned mask, unsigned val)
{
    static uint32_t argb[VDP2_PAGE_PX * VDP2_PAGE_PX];
    if (l == 3) pid = 0;
    uint32_t sum = (l == 3 ? Vdp2_Nbg3Checksum() : Vdp2_MapPageChecksum(l, pid)) ^ (mask * 0x9E3779B1u);
    SDL_Texture **t = &s_varTex[var][l][pid];
    if (*t && s_varSum[var][l][pid] != sum) { SDL_DestroyTexture(*t); *t = NULL; }
    s_varSum[var][l][pid] = sum;
    if (*t) return *t;
    Vdp2_DecodePageSel(l, pid, argb, mask, val);
    return *t = argb_texture(argb);
}

static int floordiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

/* blit_region state: page variant (s_var < 0 = whole pages) and the VDP2 colour calculation of
 * the pixels drawn (s_ccAlpha 255 = opaque; s_ccAdd = CCCTL add mode) */
static int s_var = -1;
static unsigned s_varMask, s_varVal;
static Uint8 s_ccAlpha = 255;
static int s_ccAdd;

/* Screen rect [x0,x0+w) x [y0,y0+h) showing layer pixels (sx + x, sy + y). */
static void blit_region(int l, int sx, int sy, int x0, int y0, int w, int h)
{
    int wx0 = sx + x0, wy0 = sy + y0, wx1 = wx0 + w, wy1 = wy0 + h;
    for (int py = floordiv(wy0, VDP2_PAGE_PX); py * VDP2_PAGE_PX < wy1; py++)
        for (int px = floordiv(wx0, VDP2_PAGE_PX); px * VDP2_PAGE_PX < wx1; px++) {
            SDL_Texture *t;
            if (l == 3) t = s_var >= 0 ? var_texture(3, 0, s_var, s_varMask, s_varVal) : nbg3_texture();   /* one page, all plates the same */
            else {
                int pid = Vdp2_LayerPageAt(l, px, py);
                if (pid < 0) continue;
                t = s_var >= 0 ? var_texture(l, pid, s_var, s_varMask, s_varVal) : page_texture(l, pid);
            }
            if (!t) continue;
            /* VDP2 colour calculation with the screens below: plain alpha (or additive) blending,
             * supported by every SDL renderer (no custom blend mode) */
            SDL_SetTextureBlendMode(t, s_ccAdd ? SDL_BLENDMODE_ADD : SDL_BLENDMODE_BLEND);
            SDL_SetTextureAlphaMod(t, s_ccAlpha);
            int ax = wx0 > px * VDP2_PAGE_PX ? wx0 : px * VDP2_PAGE_PX;
            int ay = wy0 > py * VDP2_PAGE_PX ? wy0 : py * VDP2_PAGE_PX;
            int bx = wx1 < (px + 1) * VDP2_PAGE_PX ? wx1 : (px + 1) * VDP2_PAGE_PX;
            int by = wy1 < (py + 1) * VDP2_PAGE_PX ? wy1 : (py + 1) * VDP2_PAGE_PX;
            SDL_Rect src = { ax - px * VDP2_PAGE_PX, ay - py * VDP2_PAGE_PX, bx - ax, by - ay };
            SDL_Rect dst = { ax - sx, ay - sy, bx - ax, by - ay };
            SDL_RenderCopy(s_renderer, t, &src, &dst);
        }
}

/* var < 0: the whole layer; else the page variant var (see s_varTex), mask / val its pixel
 * selection. cc = colour-calculation ratio of these pixels (-1 = none), add = add mode. */
static void draw_layer(int l, int var, unsigned mask, unsigned val, int cc, int add)
{
    int sx, sy;
    if (!Vdp2_LayerEnabled(l)) return;
    s_var = mask ? var : -1; s_varMask = mask; s_varVal = val;
    s_ccAlpha = cc < 0 || add ? 255 : (Uint8)((255 * (31 - cc) + 16) / 32);
    s_ccAdd = cc >= 0 && add;
    if (l != 3) {                                       /* line scroll / cell scroll: strips */
        static Vdp2LineScroll ls;
        Vdp2_GetLineScroll(l, &ls);
        if (ls.lines || ls.cells) {
            int cols = ls.cells ? SCREEN_W / 8 : 1, cw = ls.cells ? 8 : SCREEN_W;
            for (int c = 0; c < cols; c++)
                for (int y = 0; y < SCREEN_H; ) {
                    int y1 = y + 1;
                    while (y1 < SCREEN_H && ls.sx[y1] == ls.sx[y]) y1++;
                    blit_region(l, ls.sx[y], ls.sy + ls.dy[c], c * cw, y, cw, y1 - y);
                    y = y1;
                }
            return;
        }
    }
    Vdp2_LayerScroll(l, &sx, &sy);
    blit_region(l, sx, sy, 0, 0, SCREEN_W, SCREEN_H);
}

static uint32_t sprite_key(const RenderSprite *s) { return (uint32_t)s->mode << 16 | s->colr; }

static SDL_Texture *get_texture(const RenderSprite *s)
{
    uint32_t key = sprite_key(s);
    unsigned h = (s->src * 2654435761u ^ key * 40503u) % CACHE_SIZE;
    for (int probe = 0; probe < CACHE_SIZE; probe++, h = (h + 1) % CACHE_SIZE) {
        CacheEntry *e = &s_cache[h];
        if (e->tex && e->src == s->src && e->key == key && e->w == s->w && e->h == s->h) return e->tex;
        if (!e->tex) {
            static uint32_t argb[512 * 512];
            if (s->w <= 0 || s->h <= 0 || s->w * s->h > 512 * 512) return NULL;
            Vdp1_DecodeChar(s, argb);
            SDL_Texture *t = SDL_CreateTexture(s_renderer, SDL_PIXELFORMAT_ARGB8888,
                                               SDL_TEXTUREACCESS_STATIC, s->w, s->h);
            if (!t) return NULL;
            SDL_UpdateTexture(t, NULL, argb, s->w * 4);
            SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
            e->src = s->src; e->key = key; e->w = s->w; e->h = s->h; e->tex = t;
            return t;
        }
    }
    return NULL;
}

void Render_InvalidateVram(uint32_t addr, size_t len)
{
    /* Open addressing: rebuild the table without the overlapping entries. */
    static CacheEntry keep[CACHE_SIZE];
    int nk = 0;
    for (int i = 0; i < CACHE_SIZE; i++) {
        CacheEntry *e = &s_cache[i];
        if (!e->tex) continue;
        uint32_t bytes = (uint32_t)(e->w * e->h * 2);           /* upper bound (RGB) */
        if (e->src < addr + len && addr < e->src + bytes) SDL_DestroyTexture(e->tex);
        else keep[nk++] = *e;
        e->tex = NULL;
    }
    for (int i = 0; i < nk; i++) {
        unsigned h = (keep[i].src * 2654435761u ^ keep[i].key * 40503u) % CACHE_SIZE;
        while (s_cache[h].tex) h = (h + 1) % CACHE_SIZE;
        s_cache[h] = keep[i];
    }
}

void Render_QueueSprite(const RenderSprite *s)
{
    if (s_queued < MAX_QUEUE) s_queue[s_queued++] = *s;
}

void Render_Clear(void)
{
    if (!s_renderer) return;
    /* VDP2 back screen (was a fixed dark blue 24,32,56: the real game shows black, and colour-
     * calculated sprites over it, e.g. the title menu, blend with it) */
    uint32_t back = Vdp2_BackColor();
    SDL_SetRenderDrawColor(s_renderer, (Uint8)(back >> 16), (Uint8)(back >> 8), (Uint8)back, 255);
    SDL_RenderClear(s_renderer);
}

/* VDP2 priorities: Vdp2_GetPriorities / Vdp2_SpritePrioReg (soft_render.c). */

/* VDP1 framebuffer split by VDP2 priority: target p holds the pixels whose last-drawn sprite
 * has priority p (other sprites erase what they cover), so a later sprite hides an earlier
 * one even when it ends up behind a background layer, like the real framebuffer. Needs render
 * targets + a custom blend mode; otherwise sprites are just grouped by priority. */
static SDL_Texture *s_sprLayer[8];
static SDL_BlendMode s_erase, s_premulDraw, s_premulCopy;
static int s_layersOk = -1;

/* One untransformed textured quad through SDL_RenderGeometry (the path draw_sprite uses). */
static int test_quad(SDL_Texture *t, float x, float y, SDL_Color c)
{
    static const int idx[6] = { 0, 1, 2, 0, 2, 3 };
    SDL_Vertex v[4];
    for (int i = 0; i < 4; i++) {
        v[i].position.x = x + ((i == 1 || i == 2) ? 1.0f : 0.0f);
        v[i].position.y = y + (i >= 2 ? 1.0f : 0.0f);
        v[i].color = c;
        v[i].tex_coord.x = (i == 1 || i == 2) ? 1.0f : 0.0f;
        v[i].tex_coord.y = i >= 2 ? 1.0f : 0.0f;
    }
    return SDL_RenderGeometry(s_renderer, t, v, 4, idx, 6);
}

static int near8(uint32_t px, int r, int g, int b)
{
    int pr = (int)(px >> 16 & 255), pg = (int)(px >> 8 & 255), pb = (int)(px & 255);
    return abs(pr - r) <= 6 && abs(pg - g) <= 6 && abs(pb - b) <= 6;
}

/* The sprite-layer composite relies on custom blend modes in render targets, which SDL reports
 * as supported on every GPU backend; check the actual result once (draw + erase + premultiplied
 * colour calculation in a target, premultiplied copy over an opaque target, read back), so a
 * backend that gets it wrong (driver quirk, D3D/GL differences) uses the portable path instead
 * of showing holes or see-through layers. */
static int blend_self_test(void)
{
    SDL_Texture *old = SDL_GetRenderTarget(s_renderer);
    SDL_Texture *red = SDL_CreateTexture(s_renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, 1, 1);
    SDL_Texture *dst = SDL_CreateTexture(s_renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, 4, 1);
    uint32_t px[4] = { 0, 0, 0, 0 }, one = 0xFFFF0000u;
    int ok = red && dst;
    SDL_Color white = { 255, 255, 255, 255 }, half = { 128, 128, 128, 128 };
    SDL_Rect r4 = { 0, 0, 4, 1 };
    if (ok) ok = SDL_UpdateTexture(red, NULL, &one, 4) == 0;
    if (ok) {
        SDL_SetRenderTarget(s_renderer, s_sprLayer[1]);
        SDL_SetRenderDrawBlendMode(s_renderer, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(s_renderer, 0, 0, 0, 0);
        SDL_RenderClear(s_renderer);
        SDL_SetTextureBlendMode(red, SDL_BLENDMODE_BLEND);
        test_quad(red, 0, 0, white);                    /* x0: opaque sprite dot */
        test_quad(red, 1, 0, white);                    /* x1: dot erased by another priority */
        SDL_SetTextureBlendMode(red, s_erase);
        test_quad(red, 1, 0, white);
        SDL_SetTextureBlendMode(red, s_premulDraw);     /* x2: colour-calculated dot, 50 % */
        test_quad(red, 2, 0, half);
        SDL_SetTextureBlendMode(red, SDL_BLENDMODE_BLEND);
        SDL_SetRenderTarget(s_renderer, dst);
        SDL_SetRenderDrawColor(s_renderer, 0, 0, 255, 255);
        SDL_RenderClear(s_renderer);
        SDL_Rect src = { 0, 0, 4, 1 };
        SDL_RenderCopy(s_renderer, s_sprLayer[1], &src, &r4);   /* s_premulCopy */
        if (SDL_RenderReadPixels(s_renderer, &r4, SDL_PIXELFORMAT_ARGB8888, px, 16) != 0) {
            fprintf(stderr, "[render] blend self-test: read back failed (%s), assumed fine\n", SDL_GetError());
        } else {
            ok = near8(px[0], 255, 0, 0) && near8(px[1], 0, 0, 255) && near8(px[2], 128, 0, 127) && near8(px[3], 0, 0, 255);
            if (!ok)
                fprintf(stderr, "[render] blend self-test failed: %06X %06X %06X %06X (want FF0000 0000FF 80007F 0000FF)\n",
                        px[0] & 0xFFFFFFu, px[1] & 0xFFFFFFu, px[2] & 0xFFFFFFu, px[3] & 0xFFFFFFu);
        }
    }
    SDL_SetRenderTarget(s_renderer, old);
    if (red) SDL_DestroyTexture(red);
    if (dst) SDL_DestroyTexture(dst);
    return ok;
}

static int sprite_layers_ok(void)
{
    if (s_layersOk >= 0) return s_layersOk;
    s_layersOk = 0;
    if (!SDL_RenderTargetSupported(s_renderer)) return 0;
    s_erase = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
                                         SDL_BLENDOPERATION_ADD, SDL_BLENDFACTOR_ZERO,
                                         SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD);
    /* the layer targets hold premultiplied colour, so a colour-calculated (translucent) sprite is
     * blended once, at the copy: target = c * a + t * (1 - a), screen = target + screen * (1 - A) */
    s_premulDraw = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
                                              SDL_BLENDOPERATION_ADD, SDL_BLENDFACTOR_ONE,
                                              SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD);
    s_premulCopy = s_premulDraw;
    for (int p = 1; p < 8; p++) {
        s_sprLayer[p] = SDL_CreateTexture(s_renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET,
                                          SCREEN_W, SCREEN_H);
        if (!s_sprLayer[p] || SDL_SetTextureBlendMode(s_sprLayer[p], SDL_BLENDMODE_BLEND) < 0) return 0;
    }
    if (SDL_SetTextureBlendMode(s_sprLayer[1], s_erase) < 0 ||
        SDL_SetTextureBlendMode(s_sprLayer[1], s_premulCopy) < 0) {   /* custom blend unsupported (software) */
        for (int p = 1; p < 8; p++) { SDL_DestroyTexture(s_sprLayer[p]); s_sprLayer[p] = NULL; }
        return 0;
    }
    for (int p = 1; p < 8; p++) SDL_SetTextureBlendMode(s_sprLayer[p], s_premulCopy);
    if (!blend_self_test()) {
        for (int p = 1; p < 8; p++) { SDL_DestroyTexture(s_sprLayer[p]); s_sprLayer[p] = NULL; }
        return 0;
    }
    return s_layersOk = 1;
}

static float fnorm(float *x, float *y, float dx, float dy)
{
    float l = SDL_sqrtf(*x * *x + *y * *y);
    if (l < 1e-3f) { *x = dx; *y = dy; return 1.0f; }
    *x /= l; *y /= l;
    return l;
}

/* Draw one distorted-sprite command as two textured triangles (A,B,C)+(A,C,D). The VDP1
 * vertices are inclusive pixel positions: each corner is moved half a pixel outwards along
 * its two edges so an untransformed w x h sprite covers exactly w x h pixels. erase = draw
 * the sprite's opaque texels as holes (framebuffer overwrite by another priority). */
static void draw_sprite(const RenderSprite *s, SDL_Texture *t, int erase, int cc, int premul)
{
    static const int un[4] = { 1, 0, 3, 2 }, vn[4] = { 3, 2, 1, 0 };   /* u / v neighbour of A,B,C,D */
    static const float usg[4] = { -1, 1, 1, -1 }, vsg[4] = { -1, -1, 1, 1 };
    static const int idx[6] = { 0, 1, 2, 0, 2, 3 };
    SDL_Vertex vx[4];
    SDL_Color col = { 255, 255, 255, 255 };
    if (!erase && ((s->mode >> 3) & 7) == 5) {      /* VDP1 colour calculation: RGB sprites only */
        switch (s->mode & 7) {
        case 1: col.r = col.g = col.b = 0; col.a = 128; break;   /* shadow */
        case 2: col.r = col.g = col.b = 128; break;               /* half luminance */
        case 3: col.a = 128; break;                               /* half transparency */
        default: break;
        }
    }
    int ccmode = 0;
    if (!erase && cc >= 0 && !(((s->mode >> 3) & 7) == 5 && (s->mode & 7))) {
        /* VDP2 sprite colour calculation (title menu dimming, 6-1 clouds, 1-1 moon cloud...;
         * checked against the real game): the sprite is blended with the screen below it at
         * (31 - r) / 32. premul (sprite layer targets): the dot replaces what the target holds
         * (a sprite overwrites the VDP1 framebuffer) as premultiplied c * a, alpha a, and the
         * target copy blends it with the layers. Without targets: in DIRECTOR there is no NBG, so
         * the screen below is the black back screen (colour scaled); else alpha over the scene. */
        Uint8 a = (Uint8)(255 * (31 - cc) / 32);
        if (premul) { draw_sprite(s, t, 1, -1, 1); col.r = col.g = col.b = a; col.a = a; ccmode = 1; }
        else if (Res_OverlayResident() == OVL_DIRECTOR) col.r = col.g = col.b = a;
        else col.a = a;
    }
    for (int i = 0; i < 4; i++) {
        float ux = (float)(s->qx[i] - s->qx[un[i]]), uy = (float)(s->qy[i] - s->qy[un[i]]);
        float wx = (float)(s->qx[i] - s->qx[vn[i]]), wy = (float)(s->qy[i] - s->qy[vn[i]]);
        fnorm(&ux, &uy, usg[i], 0);
        fnorm(&wx, &wy, 0, vsg[i]);
        /* Dir flips: texel corner i goes to the mirrored vertex (same as flipping the UVs,
         * which SDL's software renderer mishandles) */
        int k = i;
        if (s->flipH) k = un[k];
        if (s->flipV) k = vn[k];
        vx[k].position.x = (float)s->qx[i] + 0.5f + 0.5f * (ux + wx);
        vx[k].position.y = (float)s->qy[i] + 0.5f + 0.5f * (uy + wy);
        vx[k].color = col;
        vx[i].tex_coord.x = (i == 1 || i == 2) ? 1.0f : 0.0f;
        vx[i].tex_coord.y = (i >= 2) ? 1.0f : 0.0f;
    }
    if (erase) SDL_SetTextureBlendMode(t, s_erase);
    else if (ccmode) SDL_SetTextureBlendMode(t, s_premulDraw);
    SDL_RenderGeometry(s_renderer, t, vx, 4, idx, 6);
    if (erase || ccmode) SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
}

/* Colour offset with a negative component on renderers without the REV_SUBTRACT blend
 * operation: read the band back from the frame target, clamp(c + offset) on the CPU, draw it
 * back (exact, like the VDP2; e.g. the 7-2 / 8-1 per-line tint, checked against the real game).
 * Needs the 320x224 frame target. Returns 0 when not possible. */
static int offset_readback(int r, int g, int b, const SDL_Rect *band)
{
    static SDL_Texture *strip;
    static uint32_t px[SCREEN_W * SCREEN_H];
    if (!s_scene || band->w != SCREEN_W || band->h <= 0 || band->y < 0 || band->y + band->h > SCREEN_H) return 0;
    if (!strip) {
        strip = SDL_CreateTexture(s_renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, SCREEN_W, SCREEN_H);
        if (!strip) return 0;
        SDL_SetTextureBlendMode(strip, SDL_BLENDMODE_NONE);
    }
    if (SDL_RenderReadPixels(s_renderer, band, SDL_PIXELFORMAT_ARGB8888, px, SCREEN_W * 4) != 0) return 0;
    for (int i = 0; i < SCREEN_W * band->h; i++) {
        int cr = (int)(px[i] >> 16 & 255) + r, cg = (int)(px[i] >> 8 & 255) + g, cb = (int)(px[i] & 255) + b;
        cr = cr < 0 ? 0 : cr > 255 ? 255 : cr; cg = cg < 0 ? 0 : cg > 255 ? 255 : cg; cb = cb < 0 ? 0 : cb > 255 ? 255 : cb;
        px[i] = 0xFF000000u | (uint32_t)cr << 16 | (uint32_t)cg << 8 | (uint32_t)cb;
    }
    SDL_Rect src = { 0, 0, SCREEN_W, band->h };
    if (SDL_UpdateTexture(strip, &src, px, SCREEN_W * 4) != 0) return 0;
    return SDL_RenderCopy(s_renderer, strip, &src, band) == 0;
}

static void apply_color_offset(int r, int g, int b, const SDL_Rect *all)
{
    if (!(r || g || b)) return;
    if (r < 0 || g < 0 || b < 0) {
        SDL_BlendMode sub = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE,
            SDL_BLENDOPERATION_REV_SUBTRACT, SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD);
        if (SDL_SetRenderDrawBlendMode(s_renderer, sub) == 0) {
            SDL_SetRenderDrawColor(s_renderer, (Uint8)(r < 0 ? (-r > 255 ? 255 : -r) : 0),
                                   (Uint8)(g < 0 ? (-g > 255 ? 255 : -g) : 0), (Uint8)(b < 0 ? (-b > 255 ? 255 : -b) : 0), 255);
        } else if (offset_readback(r, g, b, all)) {
            return;                                /* no REV_SUBTRACT (SDL software / some GL): done on the CPU */
        } else {                                   /* last resort: darken with alpha */
            int d = -(r < g ? (r < b ? r : b) : (g < b ? g : b));
            SDL_SetRenderDrawBlendMode(s_renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(s_renderer, 0, 0, 0, (Uint8)(d > 255 ? 255 : d));
        }
        SDL_RenderFillRect(s_renderer, all);
    }
    if (r > 0 || g > 0 || b > 0) {
        SDL_SetRenderDrawBlendMode(s_renderer, SDL_BLENDMODE_ADD);
        SDL_SetRenderDrawColor(s_renderer, (Uint8)(r > 0 ? r : 0), (Uint8)(g > 0 ? g : 0), (Uint8)(b > 0 ? b : 0), 255);
        SDL_RenderFillRect(s_renderer, all);
    }
    SDL_SetRenderDrawBlendMode(s_renderer, SDL_BLENDMODE_NONE);
}

void Render_DrawFrame(void)
{
    if (!s_renderer) { s_queued = 0; return; }
    if (!(R16(0x0603A570u) & 0x8000)) {            /* VDP2 TVMD shadow: DISP off = black screen */
        SDL_SetRenderDrawColor(s_renderer, 0, 0, 0, 255);
        SDL_RenderClear(s_renderer);
        s_queued = 0;
        return;
    }
    if (R16(0x060250A8u)) { Render_InvalidatePalettes(); W16(0x060250A8u, 0); }   /* CRAM changed */

    /* VDP1 command list order: SPR_2 draw priority ascending, submission order on ties
     * (Spr2 lists 0x06031588 / 0x060315F8). Stable insertion sort. */
    static int order[MAX_QUEUE], vp[MAX_QUEUE];
    static SDL_Texture *tex[MAX_QUEUE];
    int n = s_queued;
    for (int i = 0; i < n; i++) {
        int j = i;
        while (j > 0 && s_queue[order[j - 1]].prio > s_queue[i].prio) { order[j] = order[j - 1]; j--; }
        order[j] = i;
    }
    Vdp2Prio pr;
    Vdp2_GetPriorities(&pr);
    unsigned used = 0;
    for (int k = 0; k < n; k++) {
        const RenderSprite *s = &s_queue[order[k]];
        tex[k] = get_texture(s);
        vp[k] = pr.spr[Vdp2_SpritePrioReg(s, pr.sptype)];
        if (tex[k]) used |= 1u << vp[k];
    }
    /* sprite layer targets when several priorities are used (framebuffer overwrite across
     * priorities) or a sprite is colour-calculated against the layers below */
    int anycc = 0;
    for (int k = 0; k < n && !anycc; k++)
        anycc = tex[k] && Vdp2_SpriteColorCalc(&s_queue[order[k]], vp[k]) >= 0 && Res_OverlayResident() != OVL_DIRECTOR;
    int split = ((used & (used - 1)) != 0 || anycc) && sprite_layers_ok();
    if (split) {
        for (int p = 1; p < 8; p++) {
            if (!(used & (1u << p))) continue;
            SDL_SetRenderTarget(s_renderer, s_sprLayer[p]);
            SDL_SetRenderDrawColor(s_renderer, 0, 0, 0, 0);
            SDL_RenderClear(s_renderer);
            for (int k = 0; k < n; k++) {
                const RenderSprite *s = &s_queue[order[k]];
                int shadow = ((s->mode >> 3) & 7) == 5 && (s->mode & 7) == 1;   /* darkens, no overwrite */
                if (tex[k] && (vp[k] == p || !shadow)) draw_sprite(s, tex[k], vp[k] != p, Vdp2_SpriteColorCalc(s, vp[k]), 1);
            }
        }
        SDL_SetRenderTarget(s_renderer, s_scene);      /* back to the frame target (NULL = window) */
    }
    /* per layer: special priority per character (LSB of the priority = SPR bit, 0 = hidden)
     * and colour calculation (whole layer, or the pixels with the SCC bit / CRAM MSB) */
    int lsp[LAYERS], lcc[LAYERS], ladd[LAYERS];
    unsigned lsel[LAYERS];
    for (int l = 0; l < LAYERS; l++) {
        lsp[l] = Vdp2_SpecialPrioMode(l) == 1;
        ladd[l] = 0; lsel[l] = 0;
        lcc[l] = Vdp2_LayerColorCalc(l, &lsel[l], &ladd[l]);
    }
    for (int p = 1; p < 8; p++) {
        for (int l = LAYERS - 1; l >= 0; l--) {
            if (!pr.nbg[l]) continue;
            unsigned ccMask = lcc[l] >= 0 ? lsel[l] : 0, mask = (lsp[l] ? VDP2_PF_SP : 0u) | ccMask;
            for (int v = 0; v < NVAR; v++) {
                int vsp = v & 1, vcc = v >> 1;
                if ((vsp && !lsp[l]) || (vcc && !ccMask)) continue;
                if ((lsp[l] ? ((pr.nbg[l] & 6) | vsp) : pr.nbg[l]) != p) continue;
                int cc = lcc[l] >= 0 && (!ccMask || vcc) ? lcc[l] : -1;
                draw_layer(l, v, mask, (vsp ? VDP2_PF_SP : 0u) | (vcc ? ccMask : 0u), cc, ladd[l]);
            }
        }
        if (!(used & (1u << p))) continue;
        if (split) { SDL_RenderCopy(s_renderer, s_sprLayer[p], NULL, NULL); continue; }
        for (int k = 0; k < n; k++)
            if (tex[k] && vp[k] == p) draw_sprite(&s_queue[order[k]], tex[k], 0, Vdp2_SpriteColorCalc(&s_queue[order[k]], vp[k]), 0);
    }
    /* VDP2 colour offset A (screen fades, A.BIN 0x06007084): result = clamp(c + off). In the
     * stage flow every layer (CLOFEN 0x7F) gets it, so full-width passes are exact; when the
     * HBlank DMA rewrites COAR/G/B every line (stage hooks), one pass per run of equal lines. */
    {
        int r, g, b, m;
        Fade_GetColorOffset(&r, &g, &b, &m);
        if (m) {
            for (int y = 0; y < SCREEN_H; ) {
                int lr = r, lg = g, lb = b, y1 = y + 1;
                if (Vdp2_LineColorOffset(y, &lr, &lg, &lb)) {
                    int nr, ng, nb;
                    while (y1 < SCREEN_H && Vdp2_LineColorOffset(y1, &nr, &ng, &nb) && nr == lr && ng == lg && nb == lb) y1++;
                } else y1 = SCREEN_H;
                SDL_Rect band = { 0, y, SCREEN_W, y1 - y };
                apply_color_offset(lr, lg, lb, &band);
                y = y1;
            }
        }
    }
    if (!s_keepQueue) s_queued = 0;
}

void Render_SetKeepQueue(int keep)
{
    if (s_keepQueue && !keep) s_queued = 0;
    s_keepQueue = keep;
}

void Render_BeginFrame(void)
{
    if (s_renderer && s_scene) SDL_SetRenderTarget(s_renderer, s_scene);
}

/* F12: the 320x224 frame (debug menu included) -> <exe dir>/screenshots/. */
static void take_screenshot(void)
{
    if (!s_scene) { fprintf(stderr, "[render] screenshot unavailable (no render-target support)\n"); return; }
    uint8_t *rgb = (uint8_t *)malloc(SCREEN_W * SCREEN_H * 3);
    char path[1200];
    if (!rgb) return;
    SDL_SetRenderTarget(s_renderer, s_scene);
    if (SDL_RenderReadPixels(s_renderer, NULL, SDL_PIXELFORMAT_RGB24, rgb, SCREEN_W * 3) != 0)
        fprintf(stderr, "[render] screenshot: %s\n", SDL_GetError());
    else if (Screenshot_Save(Config_BaseDir(), rgb, SCREEN_W, SCREEN_H, Config_Get()->screenshot_png, path, sizeof path)) {
        printf("[render] screenshot %s\n", path);
        Crt_Toast("SCREENSHOT SAVED");
    }
    else fprintf(stderr, "[render] cannot write %s\n", path);
    free(rgb);
}

void Render_Present(void)
{
    if (!s_renderer) return;
    if (s_shotPending) { s_shotPending = 0; take_screenshot(); }
    if (s_scene) {
        SDL_SetRenderTarget(s_renderer, NULL);
        SDL_SetRenderDrawColor(s_renderer, 0, 0, 0, 255);
        /* CRT look (crt.c) at the output resolution, else the plain scaled copy */
        if (!Crt_Draw(s_renderer, s_scene)) {
            SDL_RenderClear(s_renderer);                /* black borders */
            SDL_RenderCopy(s_renderer, s_scene, NULL, NULL);
        }
    }
    Crt_Finish(s_renderer);                             /* Shift+F12 screenshot, toast */
    SDL_RenderPresent(s_renderer);
}

void Render_RequestScreenshot(void) { s_shotPending = 1; }
void Render_RequestScreenshotOutput(void) { Crt_RequestOutputShot(); }

void Render_CycleCrt(void)
{
    if (!s_scene) { fprintf(stderr, "[render] CRT filter unavailable (no render-target support)\n"); return; }
    Crt_CycleMode();
}

void Render_ToggleFullscreen(void)
{
    if (!s_window) return;
    int fs = (SDL_GetWindowFlags(s_window) & SDL_WINDOW_FULLSCREEN) != 0;
    if (SDL_SetWindowFullscreen(s_window, fs ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP) != 0)
        fprintf(stderr, "[render] fullscreen toggle failed: %s\n", SDL_GetError());
    else SDL_ShowCursor(fs ? SDL_ENABLE : SDL_DISABLE);
}

void Render_Shutdown(void)
{
    Render_InvalidatePalettes();
    for (int p = 0; p < 8; p++) if (s_sprLayer[p]) { SDL_DestroyTexture(s_sprLayer[p]); s_sprLayer[p] = NULL; }
    s_layersOk = -1;
    Crt_Shutdown();
    if (s_scene) { SDL_DestroyTexture(s_scene); s_scene = NULL; }
    if (s_renderer) { SDL_DestroyRenderer(s_renderer); s_renderer = NULL; }
}
