#include <SDL2/SDL.h>
#include "render.h"
#include "fade.h"
#include "ram.h"
#include "sprite.h"
#include "stage.h"
#include <stdio.h>
#include <string.h>

static SDL_Renderer *s_renderer = NULL;

#define MAX_QUEUE 256
static RenderSprite s_queue[MAX_QUEUE];
static int s_queued;

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

/* ---- VDP2 background layers (stage maps) ---- */
#define LAYERS     3
#define LAYER_DESC 0x060EA588u
static SDL_Texture *s_pageTex[LAYERS][256];
static uint32_t s_pageSum[LAYERS][256];   /* PC: checksum of the page's name table (map cells change at runtime) */

static uint32_t page_sum(int l, int pid)
{
    uint32_t d = LAYER_DESC + (uint32_t)l * 0x30, a = R32(d + 0x1C) + (uint32_t)pid * 0x800, h = 2166136261u;
    const uint8_t *p = Ram_Ptr(a);
    for (int i = 0; i < 0x800; i++) h = (h ^ p[i]) * 16777619u;
    return h;
}

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
    /* SclConfig of NBGl (0x06020D08 + 0x4C*l), byte 0 = display enable: set by SHINOBI's
     * VDP2 init 0x060400B0, cleared by Director_Init 0x06040094 (DIRECTOR = sprites only) */
    if (!Ram_Ptr(0x06020D08u + 0x4Cu * (uint32_t)l)[0]) return;
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
            uint32_t sum = page_sum(l, pid);
            if (s_pageTex[l][pid] && s_pageSum[l][pid] != sum) { SDL_DestroyTexture(s_pageTex[l][pid]); s_pageTex[l][pid] = NULL; }
            s_pageSum[l][pid] = sum;
            SDL_Texture *t = page_texture(l, pid);
            if (!t) continue;
            SDL_Rect dst = { px * 512 - sx, py * 512 - sy, 512, 512 };
            SDL_RenderCopy(s_renderer, t, NULL, &dst);
        }
}

static uint32_t sprite_key(const RenderSprite *s) { return (uint32_t)s->mode << 16 | s->colr; }

static uint32_t cram_argb(unsigned idx)
{
    uint16_t c = R16(SHADOW_CRAM + 2u * (idx & 0x7FF));
    uint32_t r = (c & 31) * 255 / 31, g = ((c >> 5) & 31) * 255 / 31, b = ((c >> 10) & 31) * 255 / 31;
    return 0xFF000000u | r << 16 | g << 8 | b;
}

/* Decode a VDP1 character (CMDPMOD colour modes 0-5) from the emulated VRAM.
 * Dot 0 is transparent unless SPD (CMDPMOD bit 6) is set. */
static void decode_char(const RenderSprite *s, uint32_t *argb)
{
    const uint8_t *v = Ram_Ptr(s->src);
    int n = s->w * s->h, spd = (s->mode & 0x40) != 0;
    for (int i = 0; i < n; i++) {
        unsigned dot;
        switch ((s->mode >> 3) & 7) {
        case 0:                                     /* 4bpp colour bank */
            dot = (i & 1) ? v[i >> 1] & 15 : v[i >> 1] >> 4;
            argb[i] = (dot || spd) ? cram_argb((s->colr & 0xFFF0u) | dot) : 0;
            break;
        case 1: {                                   /* 4bpp lookup table at VRAM colr*8 */
            dot = (i & 1) ? v[i >> 1] & 15 : v[i >> 1] >> 4;
            uint16_t e = R16(VDP1_VRAM_BASE + (uint32_t)s->colr * 8u + dot * 2u);
            if (!dot && !spd) argb[i] = 0;
            else if (e & 0x8000) {
                uint32_t r = (e & 31) * 255 / 31, g = ((e >> 5) & 31) * 255 / 31, b = ((e >> 10) & 31) * 255 / 31;
                argb[i] = 0xFF000000u | r << 16 | g << 8 | b;
            } else argb[i] = cram_argb(e);
            break; }
        case 2: dot = v[i] & 0x3F; argb[i] = (dot || spd) ? cram_argb((s->colr & 0xFFC0u) | dot) : 0; break;
        case 3: dot = v[i] & 0x7F; argb[i] = (dot || spd) ? cram_argb((s->colr & 0xFF80u) | dot) : 0; break;
        case 4: dot = v[i];        argb[i] = (dot || spd) ? cram_argb((s->colr & 0xFF00u) | dot) : 0; break;
        default: {                                  /* RGB */
            uint16_t e = (uint16_t)(v[2 * i] << 8 | v[2 * i + 1]);
            if (!(e & 0x8000) && !(spd && e == 0)) { argb[i] = 0; break; }
            uint32_t r = (e & 31) * 255 / 31, g = ((e >> 5) & 31) * 255 / 31, b = ((e >> 10) & 31) * 255 / 31;
            argb[i] = 0xFF000000u | r << 16 | g << 8 | b;
            break; }
        }
    }
}

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
            decode_char(s, argb);
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

/* ---- VDP2 priorities ----
 * VDP2 register shadow 0x0603B46C mirrors 0x25F800E0..: +0x00 SPCTL, +0x04 CRAOFA, +0x0C CCCTL,
 * +0x10..+0x16 PRISA..PRISD (sprite registers S0..S7), +0x18 PRINA (NBG0 | NBG1 << 8),
 * +0x1A PRINB (NBG2 | NBG3 << 8), +0x1C PRIR, +0x1E CCRSA...
 * Boot (SHINOBI 0x060401A0..) sets SPCTL sprite type 3 (dot: bit 15 MSB, 14-13 priority
 * register, 12-11 colour calc, 10-0 colour code); the stage init 0x060472D2 copies the stage
 * control block 0x060EA618 +0x22..+0x28 (S0..S3) and +0x2A..+0x30 (NBG0..NBG3) through
 * SCL_SetPriority 0x0602AFD4 (1-1: S0..S3 = 3,4,5,7; NBG0..3 = 4,3,2,7).
 * Higher priority is in front; 0 = not displayed; on a tie sprite > NBG0 > NBG1 > NBG2 > NBG3. */
#define VDP2_SPCTL   (VDP2_SHADOW_ADDR + 0x00)
#define VDP2_PRIS    (VDP2_SHADOW_ADDR + 0x10)
#define VDP2_PRIN    (VDP2_SHADOW_ADDR + 0x18)
#define STAGE_CTRL   0x060EA618u

typedef struct { int nbg[4], spr[8], sptype; } Vdp2Prio;

static void vdp2_priorities(Vdp2Prio *p)
{
    uint16_t spctl = R16(VDP2_SPCTL);
    int any = 0;
    p->sptype = spctl ? (spctl & 0xF) : 3;      /* PC: boot VDP2 init not ported -> type 3 */
    for (int n = 0; n < 8; n++) any |= p->spr[n] = (R16(VDP2_PRIS + 2u * (uint32_t)(n >> 1)) >> (8 * (n & 1))) & 7;
    for (int n = 0; n < 4; n++) any |= p->nbg[n] = (R16(VDP2_PRIN + 2u * (uint32_t)(n >> 1)) >> (8 * (n & 1))) & 7;
    if (any) return;
    /* PC: 0x060472D2 not ported -> take the values it would copy from the stage control block */
    for (int n = 0; n < 4; n++) {
        any |= p->spr[n] = R16(STAGE_CTRL + 0x22 + 2u * (uint32_t)n) & 7;
        any |= p->nbg[n] = R16(STAGE_CTRL + 0x2A + 2u * (uint32_t)n) & 7;
        p->spr[n + 4] = 0;
    }
    if (any) return;
    for (int n = 0; n < 8; n++) p->spr[n] = 7;  /* nothing set up: layers 2,1,0 then sprites */
    for (int n = 0; n < 4; n++) p->nbg[n] = 1;
}

/* Priority register number held in a sprite's colour data for the VDP2 sprite type
 * (types 0-7, 16-bit dots). RGB dots use register 0. LUT sprites: colour of dot 1. */
static int sprite_prio_reg(const RenderSprite *s, int type)
{
    static const uint8_t shift[8] = { 14, 13, 14, 13, 13, 12, 12, 12 }, bits[8] = { 2, 3, 1, 2, 2, 3, 3, 3 };
    unsigned cm = (s->mode >> 3) & 7;
    uint16_t c = s->colr;
    if (cm >= 5 || type > 7) return 0;
    if (cm == 1) {
        c = R16(VDP1_VRAM_BASE + (uint32_t)s->colr * 8u + 2u);
        if (c & 0x8000) return 0;
    }
    return (c >> shift[type]) & ((1 << bits[type]) - 1);
}

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
    vdp2_priorities(&pr);
    unsigned used = 0;
    for (int k = 0; k < n; k++) {
        const RenderSprite *s = &s_queue[order[k]];
        tex[k] = get_texture(s);
        vp[k] = pr.spr[sprite_prio_reg(s, pr.sptype)];
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
     * stage flow every layer (CLOFEN 0x7F) gets it, so one full-screen pass is exact. */
    {
        int r, g, b, m;
        Fade_GetColorOffset(&r, &g, &b, &m);
        if (m && (r || g || b)) {
            SDL_Rect all = { 0, 0, SCREEN_W, SCREEN_H };
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
                SDL_RenderFillRect(s_renderer, &all);
            }
            if (r > 0 || g > 0 || b > 0) {
                SDL_SetRenderDrawBlendMode(s_renderer, SDL_BLENDMODE_ADD);
                SDL_SetRenderDrawColor(s_renderer, (Uint8)(r > 0 ? r : 0), (Uint8)(g > 0 ? g : 0), (Uint8)(b > 0 ? b : 0), 255);
                SDL_RenderFillRect(s_renderer, &all);
            }
            SDL_SetRenderDrawBlendMode(s_renderer, SDL_BLENDMODE_NONE);
        }
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
    for (int p = 0; p < 8; p++) if (s_sprLayer[p]) { SDL_DestroyTexture(s_sprLayer[p]); s_sprLayer[p] = NULL; }
    s_layersOk = -1;
    if (s_renderer) { SDL_DestroyRenderer(s_renderer); s_renderer = NULL; }
}
