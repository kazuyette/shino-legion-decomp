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

/* VDP1 character textures keyed by (src, mode, colr, size). An entry stays while it is used;
 * when its pixels change (VDP1 VRAM rewritten, CRAM colours it uses changed) it is decoded
 * again into the same texture. lo/len = VDP1 VRAM bytes it was decoded from (+ the lookup
 * table of LUT sprites: lut), banks = 16-colour CRAM banks it uses. */
#define CACHE_SIZE 1024
typedef struct {
    uint32_t src, key; int w, h; SDL_Texture *tex;
    uint32_t len, lut; uint64_t banks[2]; int dirty, used;
} CacheEntry;
static CacheEntry s_cache[CACHE_SIZE];
static int s_ncache;
static int s_frame;                 /* Render_DrawFrame count (cache ages, once-per-frame checks) */

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
    /* SDL turns command batching off when a driver is named (SDL_HINT_RENDER_DRIVER): every copy
     * / triangle is then sent to the GPU one at a time. The game only draws through the SDL API,
     * so batching is safe whatever the driver. */
    SDL_SetHint(SDL_HINT_RENDER_BATCHING, "1");
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

/* ---- texture pool: textures of pages / characters no longer needed, reused (SDL_UpdateTexture)
 * instead of destroyed and created again ---- */
#define POOL_MAX 64
static struct { SDL_Texture *t; int w, h; } s_pool[POOL_MAX];
static int s_npool;

static SDL_Texture *tex_acquire(int w, int h)
{
    SDL_Texture *t = NULL;
    for (int i = s_npool - 1; i >= 0 && !t; i--)
        if (s_pool[i].w == w && s_pool[i].h == h) { t = s_pool[i].t; s_pool[i] = s_pool[--s_npool]; }
    if (!t) t = SDL_CreateTexture(s_renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, w, h);
    if (t) {
        SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
        SDL_SetTextureAlphaMod(t, 255);
        SDL_SetTextureColorMod(t, 255, 255, 255);
    }
    return t;
}

static void tex_release(SDL_Texture *t, int w, int h)
{
    if (!t) return;
    if (s_npool == POOL_MAX) { SDL_DestroyTexture(s_pool[0].t); s_pool[0] = s_pool[--s_npool]; }
    s_pool[s_npool].t = t; s_pool[s_npool].w = w; s_pool[s_npool].h = h; s_npool++;
}

/* ---- what changed since the last frame ----
 * CRAM: the renderer's colours (s_lut) are the shadow CRAM as of the last 0x060250A8 "CRAM
 * changed" flag (the game's VBlank upload of the shadow), compared word by word to find the
 * colours that changed. VDP1 / VDP2 VRAM: compared with a copy every frame in 2 KB blocks
 * (VDP2: tiles of the cached pages; VDP1: sprite characters, also invalidated explicitly by
 * Render_InvalidateVram). Only the textures using what changed are rebuilt. */
#define VBLK 2048
#define NVBLK ((int)(VDP2_VRAM_SIZE / VBLK))   /* = VDP1_VRAM_SIZE / VBLK = 256 */
static uint32_t s_lut[2048];                    /* CRAM index -> ARGB (Vdp2_CramArgb) */
static uint8_t  s_lmsb[2048];                   /* VDP2_PF_CMSB if the CRAM word's MSB is set */
static uint16_t s_cramSeen[2048];
static int s_lutOk;
static uint8_t s_v1seen[VDP1_VRAM_SIZE], s_v2seen[VDP2_VRAM_SIZE];
static int s_vramSeenOk;

static void lut_set(int i, uint16_t w)
{
    s_cramSeen[i] = w;
    s_lut[i] = Vdp_Rgb555ToArgb(w);
    s_lmsb[i] = (w & 0x8000) ? VDP2_PF_CMSB : 0;
}

static void lut_build(void)
{
    for (int i = 0; i < 2048; i++) lut_set(i, R16(SHADOW_CRAM + 2u * (uint32_t)i));
    s_lutOk = 1;
}

/* 2 KB blocks of mem that differ from seen (copied over): bits in dirty[NVBLK / 32]. */
static int vram_diff(const uint8_t *mem, uint8_t *seen, uint32_t *dirty)
{
    int any = 0;
    for (int b = 0; b < NVBLK; b++) {
        dirty[b >> 5] &= ~(1u << (b & 31));
        if (memcmp(mem + b * VBLK, seen + b * VBLK, VBLK) == 0) continue;
        memcpy(seen + b * VBLK, mem + b * VBLK, VBLK);
        dirty[b >> 5] |= 1u << (b & 31);
        any = 1;
    }
    return any;
}

/* Any 2 KB block of [addr, addr + len) (VDP1 VRAM addresses) set in dirty. */
static int range_dirty(const uint32_t *dirty, uint32_t addr, uint32_t len)
{
    if ((addr & 0xFFF80000u) != VDP1_VRAM_BASE || !len) return 0;
    uint32_t a = addr & (VDP1_VRAM_SIZE - 1), e = a + len - 1;
    if (e >= VDP1_VRAM_SIZE) e = VDP1_VRAM_SIZE - 1;
    for (uint32_t b = a / VBLK; b <= e / VBLK; b++) if (dirty[b >> 5] & (1u << (b & 31))) return 1;
    return 0;
}

/* ---- VDP2 background layers ----
 * NBG0..NBG2 = the stage map layers, drawn page by page from the MAP data (layout desc+0x20,
 * pages desc+0x1C) with the pattern-name format of 0x06046B14 (decoder in soft_render.c), or,
 * for the layers the stage does not stream, from their VDP2 name tables (Vdp2_LayerPageAt);
 * NBG3 = the status-bar layer, drawn from its VDP2 VRAM page (map built by 0x06047244).
 * Each page used is decoded once into CRAM indices + pattern-name flags (Vdp2_DecodePagePix)
 * and kept while it is in use (LRU, MAX_PAGES); its textures are built from that:
 *  - slot 0: the whole page;
 *  - slot 1 + v: page variant v = the pixels of the page selected by their flags:
 *    - special priority per character (SFPRMD mode 1, all stages): split by the SPR bit, drawn
 *      at priority (p & 6) and (p & 6) | 1 (2-1's grass in front of Sho's feet, 1-1's temple
 *      pillar in front of Sho; checked against the real game);
 *    - colour calculation per character / colour MSB (SFCCMD 1 / 3): split into the blended
 *      and the opaque pixels.
 *    Variant index = SPR value | CC value << 1; mask / val = its pixel selection.
 * A page is decoded again when its name table (compared with a copy, once per frame), its CRAM
 * offset / character base or the VDP2 VRAM blocks of its characters change. A CRAM change only
 * recolours: the 64x64 tiles holding a changed colour are rebuilt in the textures
 * (SDL_UpdateTexture of those rectangles), nothing is decoded again. */
#define LAYERS 4
#define NVAR 4
#define NSLOT (1 + NVAR)
#define TILE 64
#define TILES (VDP2_PAGE_PX / TILE)            /* 8 x 8 tiles = the bits of a uint64_t */
#define MAX_PAGES 64
typedef struct {
    int l, pid, used, checked, stale;
    uint16_t *pix;                              /* VDP2_PIX_* words, 512 x 512 */
    uint8_t *nt; uint32_t ntLen;                /* name table as decoded */
    uint64_t params;
    uint32_t blk[NVBLK / 32];                   /* VDP2 VRAM blocks of its characters */
    uint64_t col[32];                           /* CRAM indices used (2048 bits) */
    uint64_t (*tcol)[32];                       /* the same per tile */
    struct { SDL_Texture *t; unsigned mask, val; uint64_t dirty, filled; } slot[NSLOT];   /* filled: tiles with a visible pixel */
} PageEnt;
static PageEnt s_pages[MAX_PAGES];
static int16_t s_pageOf[LAYERS][VDP2_PAGE_IDS];  /* index in s_pages + 1, 0 = none */

static void page_drop(PageEnt *e)
{
    for (int k = 0; k < NSLOT; k++) { tex_release(e->slot[k].t, VDP2_PAGE_PX, VDP2_PAGE_PX); e->slot[k].t = NULL; }
    if (e->pix) s_pageOf[e->l][e->pid] = 0;
    e->l = -1;
}

static void page_decode(PageEnt *e)
{
    uint32_t len, a = Vdp2_PageNameTable(e->l, e->pid, &len);
    e->ntLen = len;
    memcpy(e->nt, Ram_Ptr(a), len);
    e->params = Vdp2_PageParams(e->l, e->pid);
    memset(e->blk, 0, sizeof e->blk);
    Vdp2_DecodePagePix(e->l, e->pid, e->pix, e->blk);
    memset(e->col, 0, sizeof e->col);
    memset(e->tcol, 0, sizeof(uint64_t) * 32 * TILES * TILES);
    for (int y = 0; y < VDP2_PAGE_PX; y++) {
        const uint16_t *row = e->pix + y * VDP2_PAGE_PX;
        for (int x = 0; x < VDP2_PAGE_PX; x++) {
            if (!(row[x] & VDP2_PIX_OPAQUE)) continue;
            unsigned i = row[x] & VDP2_PIX_INDEX;
            e->tcol[(y / TILE) * TILES + x / TILE][i >> 6] |= 1ull << (i & 63);
        }
    }
    for (int t = 0; t < TILES * TILES; t++)
        for (int w = 0; w < 32; w++) e->col[w] |= e->tcol[t][w];
    for (int k = 0; k < NSLOT; k++) e->slot[k].dirty = ~0ull;
    e->stale = 0;
    e->checked = s_frame;
}

/* Once per frame: decode again if the name table, the parameters or the characters changed. */
static void page_validate(PageEnt *e)
{
    if (e->checked == s_frame && !e->stale) return;
    e->checked = s_frame;
    uint32_t len, a = Vdp2_PageNameTable(e->l, e->pid, &len);
    if (e->stale || len != e->ntLen || memcmp(Ram_Ptr(a), e->nt, len) != 0 || Vdp2_PageParams(e->l, e->pid) != e->params)
        page_decode(e);
}

static PageEnt *page_get(int l, int pid)
{
    int i = s_pageOf[l][pid] - 1;
    PageEnt *e;
    if (i >= 0) { e = &s_pages[i]; page_validate(e); e->used = s_frame; return e; }
    /* a free entry, else the least recently used one not drawn this frame */
    e = NULL;
    for (int k = 0; k < MAX_PAGES; k++) {
        PageEnt *c = &s_pages[k];
        if (!c->pix || c->l < 0) { e = c; break; }
        if (c->used != s_frame && (!e || c->used < e->used)) e = c;
    }
    if (!e) return NULL;
    if (!e->pix) {
        e->pix = (uint16_t *)malloc(sizeof(uint16_t) * VDP2_PAGE_PX * VDP2_PAGE_PX);
        e->nt = (uint8_t *)malloc(0x4000);
        e->tcol = malloc(sizeof(uint64_t) * 32 * TILES * TILES);
        if (!e->pix || !e->nt || !e->tcol) {
            free(e->pix); free(e->nt); free(e->tcol); e->pix = NULL; e->nt = NULL; e->tcol = NULL;
            return NULL;
        }
        e->l = -1;
    }
    if (e->l >= 0) page_drop(e);
    e->l = l; e->pid = pid;
    s_pageOf[l][pid] = (int16_t)(e - s_pages + 1);
    page_decode(e);
    e->used = s_frame;
    return e;
}

/* Page pixel -> ARGB, with the variant selection (old Vdp2_DecodePageSel: flags & mask == val). */
static inline uint32_t pix_argb(uint16_t w, unsigned mask, unsigned val)
{
    if (!(w & VDP2_PIX_OPAQUE)) return 0;
    unsigned i = w & VDP2_PIX_INDEX;
    if (mask && ((((unsigned)w >> VDP2_PIX_FSHIFT & 3u) | s_lmsb[i]) & mask) != (val & mask)) return 0;
    return s_lut[i];
}

/* Bring a slot's texture up to date: the dirty tiles, one rectangle per run of tiles in a row.
 * Returns the new `filled` bits (tiles holding at least one visible pixel). */
static uint64_t slot_upload(const PageEnt *e, SDL_Texture *t, uint64_t dirty, uint64_t filled, unsigned mask, unsigned val)
{
    static uint32_t argb[VDP2_PAGE_PX * VDP2_PAGE_PX];
    for (int ty = 0; ty < TILES; ty++) {
        unsigned row = (unsigned)(dirty >> (ty * TILES)) & ((1u << TILES) - 1);
        int y0 = ty * TILE, h = TILE;
        if (dirty == ~0ull) { if (ty) break; row = (1u << TILES) - 1; h = VDP2_PAGE_PX; }
        for (int tx = 0; tx < TILES; ) {
            if (!(row & (1u << tx))) { tx++; continue; }
            int tx1 = tx;
            while (tx1 < TILES && (row & (1u << tx1))) tx1++;
            SDL_Rect r = { tx * TILE, y0, (tx1 - tx) * TILE, h };
            uint64_t vis = 0;
            for (int y = 0; y < r.h; y++) {
                const uint16_t *src = e->pix + (r.y + y) * VDP2_PAGE_PX + r.x;
                uint32_t *dst = argb + y * r.w;
                uint64_t tb = 1ull << (((r.y + y) / TILE) * TILES + tx);
                for (int x = 0; x < r.w; x++)
                    if ((dst[x] = pix_argb(src[x], mask, val)) != 0) vis |= tb << (x / TILE);
            }
            for (int yy = r.y / TILE; yy < (r.y + r.h) / TILE; yy++)
                for (int xx = tx; xx < tx1; xx++) filled &= ~(1ull << (yy * TILES + xx));
            filled |= vis;
            SDL_UpdateTexture(t, &r, argb, r.w * 4);
            tx = tx1;
        }
    }
    return filled;
}

/* var < 0: the whole page; else page variant var with its selection mask / val. *filled = its
 * tiles holding a visible pixel (the others need not be drawn: fully transparent). */
static SDL_Texture *page_texture(int l, int pid, int var, unsigned mask, unsigned val, uint64_t *filled)
{
    if (l == 3) pid = 0;                        /* NBG3: one page, all plates the same */
    PageEnt *e = page_get(l, pid);
    if (!e) return NULL;
    int k = var < 0 ? 0 : 1 + var;
    if (var < 0) mask = val = 0;
    if (!e->slot[k].t) {
        e->slot[k].t = tex_acquire(VDP2_PAGE_PX, VDP2_PAGE_PX);
        if (!e->slot[k].t) return NULL;
        e->slot[k].dirty = ~0ull;
    }
    if (e->slot[k].mask != mask || e->slot[k].val != val) { e->slot[k].mask = mask; e->slot[k].val = val; e->slot[k].dirty = ~0ull; }
    if (e->slot[k].dirty) {
        e->slot[k].filled = slot_upload(e, e->slot[k].t, e->slot[k].dirty, e->slot[k].filled, mask, val);
        e->slot[k].dirty = 0;
    }
    *filled = e->slot[k].filled;
    return e->slot[k].t;
}

/* ---- VDP1 characters ---- */

static void sprite_banks(const RenderSprite *s, uint64_t b[2])
{
    unsigned cm = (s->mode >> 3) & 7, base = 0, n = 0;
    b[0] = b[1] = 0;
    switch (cm) {
    case 0: base = s->colr & 0xFFF0u; n = 16; break;
    case 2: base = s->colr & 0xFFC0u; n = 64; break;
    case 3: base = s->colr & 0xFF80u; n = 128; break;
    case 4: base = s->colr & 0xFF00u; n = 256; break;
    case 1:
        for (uint32_t d = 0; d < 16; d++) {
            uint16_t e = R16(VDP1_VRAM_BASE + (uint32_t)s->colr * 8u + d * 2u);
            if (!(e & 0x8000)) { unsigned k = (e & 0x7FFu) >> 4; b[k >> 6] |= 1ull << (k & 63); }
        }
        return;
    default: return;                            /* RGB: no CRAM */
    }
    for (unsigned i = 0; i < n; i += 16) { unsigned k = ((base + i) & 0x7FFu) >> 4; b[k >> 6] |= 1ull << (k & 63); }
}

static void char_decode(CacheEntry *e, const RenderSprite *s)
{
    static uint32_t argb[512 * 512];
    Vdp1_DecodeCharPal(s, argb, s_lut);         /* colours flushed at the last CRAM flag */
    SDL_UpdateTexture(e->tex, NULL, argb, s->w * 4);
    sprite_banks(s, e->banks);
    e->dirty = 0;
}

/* keep the characters used in the last `age` frames, the others' textures go to the pool */
static void cache_rehash(int age)
{
    static CacheEntry keep[CACHE_SIZE];
    int nk = 0;
    for (int i = 0; i < CACHE_SIZE; i++) {
        CacheEntry *e = &s_cache[i];
        if (!e->tex) continue;
        if (e->used >= s_frame - age) keep[nk++] = *e;
        else tex_release(e->tex, e->w, e->h);
        e->tex = NULL;
    }
    for (int i = 0; i < nk; i++) {
        unsigned h = (keep[i].src * 2654435761u ^ keep[i].key * 40503u) % CACHE_SIZE;
        while (s_cache[h].tex) h = (h + 1) % CACHE_SIZE;
        s_cache[h] = keep[i];
    }
    s_ncache = nk;
}

/* Everything is decoded again before its next use (same pixels as a fresh start). */
void Render_InvalidatePalettes(void)
{
    s_lutOk = 0;
    for (int i = 0; i < CACHE_SIZE; i++) s_cache[i].dirty = 1;
    for (int k = 0; k < MAX_PAGES; k++) s_pages[k].stale = 1;
}

static void free_all(void)
{
    for (int i = 0; i < CACHE_SIZE; i++) {
        if (s_cache[i].tex) SDL_DestroyTexture(s_cache[i].tex);
        s_cache[i].tex = NULL;
    }
    s_ncache = 0;
    for (int k = 0; k < MAX_PAGES; k++) {
        PageEnt *e = &s_pages[k];
        for (int j = 0; j < NSLOT; j++) { if (e->slot[j].t) SDL_DestroyTexture(e->slot[j].t); e->slot[j].t = NULL; }
        free(e->pix); free(e->nt); free(e->tcol);
        memset(e, 0, sizeof *e);
    }
    memset(s_pageOf, 0, sizeof s_pageOf);
    for (int i = 0; i < s_npool; i++) SDL_DestroyTexture(s_pool[i].t);
    s_npool = 0;
    s_lutOk = 0;
    s_vramSeenOk = 0;
}

/* Start of a frame: VRAM changes since the last frame, CRAM changes when the game flagged its
 * shadow CRAM as changed (0x060250A8): mark the affected pages / tiles / characters. */
static void track_changes(void)
{
    static uint32_t d1[NVBLK / 32], d2[NVBLK / 32];
    if (!s_vramSeenOk) {
        memcpy(s_v1seen, g_vdp1vram, sizeof s_v1seen);
        memcpy(s_v2seen, g_vdp2vram, sizeof s_v2seen);
        s_vramSeenOk = 1;
    } else {
        if (vram_diff(g_vdp1vram, s_v1seen, d1))
            for (int i = 0; i < CACHE_SIZE; i++) {
                CacheEntry *e = &s_cache[i];
                if (e->tex && (range_dirty(d1, e->src, e->len) || range_dirty(d1, e->lut, e->lut ? 32 : 0))) e->dirty = 1;
            }
        if (vram_diff(g_vdp2vram, s_v2seen, d2))
            for (int k = 0; k < MAX_PAGES; k++) {
                PageEnt *e = &s_pages[k];
                if (!e->pix || e->l < 0) continue;
                for (int w = 0; w < NVBLK / 32; w++) if (e->blk[w] & d2[w]) { e->stale = 1; break; }
            }
    }
    if (!s_lutOk) { lut_build(); W16(0x060250A8u, 0); return; }    /* everything is marked */
    if (!R16(0x060250A8u)) return;
    W16(0x060250A8u, 0);
    uint64_t ch[32] = { 0 }, banks[2] = { 0 };
    int any = 0;
    for (int i = 0; i < 2048; i++) {
        uint16_t w = R16(SHADOW_CRAM + 2u * (uint32_t)i);
        if (w == s_cramSeen[i]) continue;
        lut_set(i, w);
        ch[i >> 6] |= 1ull << (i & 63);
        banks[i >> 10] |= 1ull << ((i >> 4) & 63);
        any = 1;
    }
    if (!any) return;
    for (int i = 0; i < CACHE_SIZE; i++) {
        CacheEntry *e = &s_cache[i];
        if (e->tex && ((e->banks[0] & banks[0]) || (e->banks[1] & banks[1]))) e->dirty = 1;
    }
    for (int k = 0; k < MAX_PAGES; k++) {
        PageEnt *e = &s_pages[k];
        if (!e->pix || e->l < 0 || e->stale) continue;
        int hit = 0;
        for (int w = 0; w < 32 && !hit; w++) hit = (e->col[w] & ch[w]) != 0;
        if (!hit) continue;
        uint64_t tiles = 0;
        for (int t = 0; t < TILES * TILES; t++)
            for (int w = 0; w < 32; w++) if (e->tcol[t][w] & ch[w]) { tiles |= 1ull << t; break; }
        for (int j = 0; j < NSLOT; j++) e->slot[j].dirty |= tiles;
    }
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
            int pid = l == 3 ? 0 : Vdp2_LayerPageAt(l, px, py);
            if (pid < 0) continue;
            uint64_t filled;
            SDL_Texture *t = page_texture(l, pid, s_var, s_varMask, s_varVal, &filled);
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
            uint64_t under = 0;                     /* tiles under src: nothing visible = nothing to draw */
            for (int ty = src.y / TILE; ty <= (src.y + src.h - 1) / TILE; ty++)
                under |= (uint64_t)((0xFFu >> (7 - (src.x + src.w - 1) / TILE)) & (0xFFu << (src.x / TILE))) << (ty * TILES);
            if (!(filled & under)) continue;
            SDL_RenderCopy(s_renderer, t, &src, &dst);
        }
}

/* var < 0: the whole layer; else the page variant var (see PageEnt), mask / val its pixel
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
    if (s->w <= 0 || s->h <= 0 || s->w * s->h > 512 * 512) return NULL;
    static int purged = -1;
    if (s_ncache >= CACHE_SIZE * 3 / 4 && purged != s_frame) { cache_rehash(1); purged = s_frame; }   /* table nearly full */
    unsigned h = (s->src * 2654435761u ^ key * 40503u) % CACHE_SIZE;
    for (int probe = 0; probe < CACHE_SIZE; probe++, h = (h + 1) % CACHE_SIZE) {
        CacheEntry *e = &s_cache[h];
        if (e->tex && e->src == s->src && e->key == key && e->w == s->w && e->h == s->h) {
            if (e->dirty) char_decode(e, s);
            e->used = s_frame;
            return e->tex;
        }
        if (!e->tex) {
            SDL_Texture *t = tex_acquire(s->w, s->h);
            if (!t) return NULL;
            e->src = s->src; e->key = key; e->w = s->w; e->h = s->h; e->tex = t;
            e->len = (uint32_t)(s->w * s->h * 2);                  /* upper bound (RGB) */
            e->lut = ((s->mode >> 3) & 7) == 1 ? VDP1_VRAM_BASE + (uint32_t)s->colr * 8u : 0;
            char_decode(e, s);
            e->used = s_frame;
            s_ncache++;
            return t;
        }
    }
    return NULL;
}

/* VRAM rewritten by the game: the characters reading [addr, addr+len) are decoded again at
 * their next use (VDP1 VRAM is also compared every frame, track_changes). */
void Render_InvalidateVram(uint32_t addr, size_t len)
{
    for (int i = 0; i < CACHE_SIZE; i++) {
        CacheEntry *e = &s_cache[i];
        if (!e->tex) continue;
        if (e->src < addr + len && addr < e->src + e->len) e->dirty = 1;
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

/* CPU colour-offset strip (320x224, streaming), destroyed with the renderer */
static SDL_Texture *s_offStrip;
static SDL_Texture *offset_strip(void)
{
    if (!s_offStrip) {
        s_offStrip = SDL_CreateTexture(s_renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, SCREEN_W, SCREEN_H);
        if (s_offStrip) SDL_SetTextureBlendMode(s_offStrip, SDL_BLENDMODE_NONE);
    }
    return s_offStrip;
}

/* Colour offset with a negative component on renderers without the REV_SUBTRACT blend
 * operation: read the band back from the frame target, clamp(c + offset) on the CPU, draw it
 * back (exact, like the VDP2; e.g. the 7-2 / 8-1 per-line tint, checked against the real game).
 * Needs the 320x224 frame target. Returns 0 when not possible. */
static int offset_readback(int r, int g, int b, const SDL_Rect *band)
{
    static uint32_t px[SCREEN_W * SCREEN_H];
    if (!s_scene || band->w != SCREEN_W || band->h <= 0 || band->y < 0 || band->y + band->h > SCREEN_H) return 0;
    SDL_Texture *strip = offset_strip();
    if (!strip) return 0;
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

/* All the colour-offset bands of a frame that need the CPU path, with one read-back (each
 * read-back waits for the GPU; 7-2 / 8-1 have ~28 bands a frame). b[i] = { y0, y1, r, g, b }.
 * Returns 0 if not possible (then the bands are done one by one). */
static int offset_readback_bands(const int (*bd)[5], int n)
{
    static uint32_t px[SCREEN_W * SCREEN_H];
    if (!s_scene || n <= 0) return 0;
    int ya = bd[0][0], yb = bd[n - 1][1];
    if (ya < 0 || yb > SCREEN_H) return 0;
    SDL_Texture *strip = offset_strip();
    if (!strip) return 0;
    SDL_Rect rd = { 0, ya, SCREEN_W, yb - ya };
    if (SDL_RenderReadPixels(s_renderer, &rd, SDL_PIXELFORMAT_ARGB8888, px, SCREEN_W * 4) != 0) return 0;
    for (int k = 0; k < n; k++)
        for (int i = (bd[k][0] - ya) * SCREEN_W; i < (bd[k][1] - ya) * SCREEN_W; i++) {
            int cr = (int)(px[i] >> 16 & 255) + bd[k][2], cg = (int)(px[i] >> 8 & 255) + bd[k][3], cb = (int)(px[i] & 255) + bd[k][4];
            cr = cr < 0 ? 0 : cr > 255 ? 255 : cr; cg = cg < 0 ? 0 : cg > 255 ? 255 : cg; cb = cb < 0 ? 0 : cb > 255 ? 255 : cb;
            px[i] = 0xFF000000u | (uint32_t)cr << 16 | (uint32_t)cg << 8 | (uint32_t)cb;
        }
    /* written back per run of adjacent bands (rows of other bands are left alone) */
    for (int k = 0; k < n; ) {
        int k1 = k + 1;
        while (k1 < n && bd[k1][0] == bd[k1 - 1][1]) k1++;
        SDL_Rect r = { 0, bd[k][0] - ya, SCREEN_W, bd[k1 - 1][1] - bd[k][0] };
        SDL_Rect dst = { 0, bd[k][0], SCREEN_W, r.h };
        if (SDL_UpdateTexture(strip, &r, px + r.y * SCREEN_W, SCREEN_W * 4) != 0) return 0;
        if (SDL_RenderCopy(s_renderer, strip, &r, &dst) != 0) return 0;
        k = k1;
    }
    return 1;
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
    s_frame++;
    track_changes();                                /* CRAM (flag 0x060250A8) / VRAM changes */
    if (!(s_frame & 63)) {                          /* every 64 frames: what was not used for ~2 s */
        cache_rehash(120);                          /* characters: textures to the pool */
        for (int k = 0; k < MAX_PAGES; k++)         /* pages: textures to the pool (pixels kept) */
            if (s_pages[k].pix && s_pages[k].l >= 0 && s_pages[k].used < s_frame - 120)
                for (int j = 0; j < NSLOT; j++)
                    if (s_pages[k].slot[j].t) { tex_release(s_pages[k].slot[j].t, VDP2_PAGE_PX, VDP2_PAGE_PX); s_pages[k].slot[j].t = NULL; }
    }

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
    static int box[MAX_QUEUE][4];                   /* screen box of each sprite (+2 px margin) */
    if (split)
        for (int k = 0; k < n; k++) {
            const RenderSprite *s = &s_queue[order[k]];
            box[k][0] = box[k][2] = s->qx[0]; box[k][1] = box[k][3] = s->qy[0];
            for (int i = 1; i < 4; i++) {
                if (s->qx[i] < box[k][0]) box[k][0] = s->qx[i];
                if (s->qx[i] > box[k][2]) box[k][2] = s->qx[i];
                if (s->qy[i] < box[k][1]) box[k][1] = s->qy[i];
                if (s->qy[i] > box[k][3]) box[k][3] = s->qy[i];
            }
            box[k][0] -= 2; box[k][1] -= 2; box[k][2] += 2; box[k][3] += 2;
        }
    if (split) {
        for (int p = 1; p < 8; p++) {
            if (!(used & (1u << p))) continue;
            SDL_SetRenderTarget(s_renderer, s_sprLayer[p]);
            SDL_SetRenderDrawColor(s_renderer, 0, 0, 0, 0);
            SDL_RenderClear(s_renderer);
            /* a sprite of another priority only erases what this target already holds: none
             * before its first sprite, nothing outside the box of those drawn so far */
            int bx0 = SCREEN_W, by0 = SCREEN_H, bx1 = -1, by1 = -1;
            for (int k = 0; k < n; k++) {
                const RenderSprite *s = &s_queue[order[k]];
                int shadow = ((s->mode >> 3) & 7) == 5 && (s->mode & 7) == 1;   /* darkens, no overwrite */
                if (!tex[k] || (vp[k] != p && shadow)) continue;
                if (vp[k] != p) {
                    if (bx1 < 0 || box[k][2] < bx0 || box[k][0] > bx1 || box[k][3] < by0 || box[k][1] > by1) continue;
                } else {
                    if (box[k][0] < bx0) bx0 = box[k][0];
                    if (box[k][1] < by0) by0 = box[k][1];
                    if (box[k][2] > bx1) bx1 = box[k][2];
                    if (box[k][3] > by1) by1 = box[k][3];
                }
                draw_sprite(s, tex[k], vp[k] != p, Vdp2_SpriteColorCalc(s, vp[k]), 1);
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
            static int band[SCREEN_H][5], cpu[SCREEN_H][5];
            int nb = 0, ncpu = 0;
            for (int y = 0; y < SCREEN_H; ) {
                int lr = r, lg = g, lb = b, y1 = y + 1;
                if (Vdp2_LineColorOffset(y, &lr, &lg, &lb)) {
                    int nr, ng, nb2;
                    while (y1 < SCREEN_H && Vdp2_LineColorOffset(y1, &nr, &ng, &nb2) && nr == lr && ng == lg && nb2 == lb) y1++;
                } else y1 = SCREEN_H;
                band[nb][0] = y; band[nb][1] = y1; band[nb][2] = lr; band[nb][3] = lg; band[nb][4] = lb; nb++;
                y = y1;
            }
            /* negative offsets without REV_SUBTRACT: CPU, one read-back for all such bands */
            SDL_BlendMode sub = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE,
                SDL_BLENDOPERATION_REV_SUBTRACT, SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD);
            int subOk = SDL_SetRenderDrawBlendMode(s_renderer, sub) == 0;
            SDL_SetRenderDrawBlendMode(s_renderer, SDL_BLENDMODE_NONE);
            if (!subOk)
                for (int k = 0; k < nb; k++)
                    if (band[k][2] < 0 || band[k][3] < 0 || band[k][4] < 0) memcpy(cpu[ncpu++], band[k], sizeof band[k]);
            int cpuDone = ncpu && offset_readback_bands((const int (*)[5])cpu, ncpu);
            for (int k = 0; k < nb; k++) {
                if (cpuDone && (band[k][2] < 0 || band[k][3] < 0 || band[k][4] < 0)) continue;
                SDL_Rect rc = { 0, band[k][0], SCREEN_W, band[k][1] - band[k][0] };
                apply_color_offset(band[k][2], band[k][3], band[k][4], &rc);
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
    free_all();
    if (s_offStrip) { SDL_DestroyTexture(s_offStrip); s_offStrip = NULL; }
    for (int p = 0; p < 8; p++) if (s_sprLayer[p]) { SDL_DestroyTexture(s_sprLayer[p]); s_sprLayer[p] = NULL; }
    s_layersOk = -1;
    Crt_Shutdown();
    if (s_scene) { SDL_DestroyTexture(s_scene); s_scene = NULL; }
    if (s_renderer) { SDL_DestroyRenderer(s_renderer); s_renderer = NULL; }
}
