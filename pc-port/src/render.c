#include <SDL2/SDL.h>
#include "render.h"
#include "fade.h"
#include "ram.h"
#include "sprite.h"
#include "stage.h"
#include "soft_render.h"
#include "resource.h"
#include "stage_hooks.h"
#include <stdio.h>
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

/* ---- VDP2 background layers ----
 * NBG0..NBG2 = the stage map layers, drawn page by page from the MAP data (layout desc+0x20,
 * pages desc+0x1C) with the pattern-name format of 0x06046B14 (decoder in soft_render.c);
 * NBG3 = the status-bar layer, drawn from its VDP2 VRAM page (map built by 0x06047244). */
#define LAYERS 4
static SDL_Texture *s_pageTex[3][256];
static uint32_t s_pageSum[3][256];   /* PC: checksum of the page's name table (map cells change at runtime) */
static SDL_Texture *s_nbg3Tex;
static uint32_t s_nbg3Sum;

void Render_InvalidatePalettes(void)
{
    for (int i = 0; i < CACHE_SIZE; i++) {
        if (s_cache[i].tex) SDL_DestroyTexture(s_cache[i].tex);
        s_cache[i].tex = NULL;
    }
    for (int l = 0; l < 3; l++)
        for (int p = 0; p < 256; p++)
            if (s_pageTex[l][p]) { SDL_DestroyTexture(s_pageTex[l][p]); s_pageTex[l][p] = NULL; }
    if (s_nbg3Tex) { SDL_DestroyTexture(s_nbg3Tex); s_nbg3Tex = NULL; }
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

static int floordiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

/* Screen rect [x0,x0+w) x [y0,y0+h) showing layer pixels (sx + x, sy + y). */
static void blit_region(int l, int sx, int sy, int x0, int y0, int w, int h)
{
    int wx0 = sx + x0, wy0 = sy + y0, wx1 = wx0 + w, wy1 = wy0 + h;
    for (int py = floordiv(wy0, VDP2_PAGE_PX); py * VDP2_PAGE_PX < wy1; py++)
        for (int px = floordiv(wx0, VDP2_PAGE_PX); px * VDP2_PAGE_PX < wx1; px++) {
            SDL_Texture *t;
            if (l == 3) t = nbg3_texture();                 /* one page, all plates the same */
            else {
                int pid = Vdp2_LayerPageAt(l, px, py);
                if (pid < 0) continue;
                t = page_texture(l, pid);
            }
            if (!t) continue;
            int ax = wx0 > px * VDP2_PAGE_PX ? wx0 : px * VDP2_PAGE_PX;
            int ay = wy0 > py * VDP2_PAGE_PX ? wy0 : py * VDP2_PAGE_PX;
            int bx = wx1 < (px + 1) * VDP2_PAGE_PX ? wx1 : (px + 1) * VDP2_PAGE_PX;
            int by = wy1 < (py + 1) * VDP2_PAGE_PX ? wy1 : (py + 1) * VDP2_PAGE_PX;
            SDL_Rect src = { ax - px * VDP2_PAGE_PX, ay - py * VDP2_PAGE_PX, bx - ax, by - ay };
            SDL_Rect dst = { ax - sx, ay - sy, bx - ax, by - ay };
            SDL_RenderCopy(s_renderer, t, &src, &dst);
        }
}

static void draw_layer(int l)
{
    int sx, sy;
    if (!Vdp2_LayerEnabled(l)) return;
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
    SDL_SetRenderDrawColor(s_renderer, 24, 32, 56, 255);
    SDL_RenderClear(s_renderer);
}

/* VDP2 priorities: Vdp2_GetPriorities / Vdp2_SpritePrioReg (soft_render.c). */

/* VDP1 framebuffer split by VDP2 priority: target p holds the pixels whose last-drawn sprite
 * has priority p (other sprites erase what they cover), so a later sprite hides an earlier
 * one even when it ends up behind a background layer, like the real framebuffer. Needs render
 * targets + a custom blend mode; otherwise sprites are just grouped by priority. */
static SDL_Texture *s_sprLayer[8];
static SDL_BlendMode s_erase;
static int s_layersOk = -1;

static int sprite_layers_ok(void)
{
    if (s_layersOk >= 0) return s_layersOk;
    s_layersOk = 0;
    if (!SDL_RenderTargetSupported(s_renderer)) return 0;
    s_erase = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
                                         SDL_BLENDOPERATION_ADD, SDL_BLENDFACTOR_ZERO,
                                         SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD);
    for (int p = 1; p < 8; p++) {
        s_sprLayer[p] = SDL_CreateTexture(s_renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET,
                                          SCREEN_W, SCREEN_H);
        if (!s_sprLayer[p] || SDL_SetTextureBlendMode(s_sprLayer[p], SDL_BLENDMODE_BLEND) < 0) return 0;
    }
    if (SDL_SetTextureBlendMode(s_sprLayer[1], s_erase) < 0) {   /* custom blend unsupported (software) */
        for (int p = 1; p < 8; p++) { SDL_DestroyTexture(s_sprLayer[p]); s_sprLayer[p] = NULL; }
        return 0;
    }
    SDL_SetTextureBlendMode(s_sprLayer[1], SDL_BLENDMODE_BLEND);
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
static void draw_sprite(const RenderSprite *s, SDL_Texture *t, int erase)
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
    } else if (!erase && ((s->mode >> 3) & 7) < 5 && Res_OverlayResident() == OVL_DIRECTOR) {
        /* VDP2 sprite colour calculation (title menus dim unselected items) */
        int reg = (s->colr >> 11) & 3;
        int ratio = (R16(VDP2_SHADOW_ADDR + 0x1E + 2u * (unsigned)(reg >> 1)) >> (8 * (reg & 1))) & 0x1F;
        col.r = col.g = col.b = (Uint8)(255 * (32 - ratio) / 32);
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
    SDL_RenderGeometry(s_renderer, t, vx, 4, idx, 6);
    if (erase) SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
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
        } else {                                   /* no REV_SUBTRACT: darken with alpha */
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
    int split = (used & (used - 1)) != 0 && sprite_layers_ok();
    if (split) {
        for (int p = 1; p < 8; p++) {
            if (!(used & (1u << p))) continue;
            SDL_SetRenderTarget(s_renderer, s_sprLayer[p]);
            SDL_SetRenderDrawColor(s_renderer, 0, 0, 0, 0);
            SDL_RenderClear(s_renderer);
            for (int k = 0; k < n; k++) {
                const RenderSprite *s = &s_queue[order[k]];
                int shadow = ((s->mode >> 3) & 7) == 5 && (s->mode & 7) == 1;   /* darkens, no overwrite */
                if (tex[k] && (vp[k] == p || !shadow)) draw_sprite(s, tex[k], vp[k] != p);
            }
        }
        SDL_SetRenderTarget(s_renderer, NULL);
    }
    for (int p = 1; p < 8; p++) {
        for (int l = LAYERS - 1; l >= 0; l--)
            if (pr.nbg[l] == p) draw_layer(l);
        if (!(used & (1u << p))) continue;
        if (split) { SDL_RenderCopy(s_renderer, s_sprLayer[p], NULL, NULL); continue; }
        for (int k = 0; k < n; k++)
            if (tex[k] && vp[k] == p) draw_sprite(&s_queue[order[k]], tex[k], 0);
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

void Render_Present(void)
{
    if (s_renderer) SDL_RenderPresent(s_renderer);
}

void Render_Shutdown(void)
{
    Render_InvalidatePalettes();
    for (int p = 0; p < 8; p++) if (s_sprLayer[p]) { SDL_DestroyTexture(s_sprLayer[p]); s_sprLayer[p] = NULL; }
    s_layersOk = -1;
    if (s_renderer) { SDL_DestroyRenderer(s_renderer); s_renderer = NULL; }
}
