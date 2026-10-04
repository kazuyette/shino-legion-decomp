/* PC port: optional CRT look (scanlines, phosphor mask, glow, curvature), see crt.h. */
#include "crt.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------- overlay maths (no SDL) */

static const char *const kModeNames[CRT_NMODES] = { "off", "scanlines", "crt" };
static const char *const kMaskNames[CRT_NMASKS] = { "none", "aperture", "slot" };

const char *Crt_ModeName(int m) { return m >= 0 && m < CRT_NMODES ? kModeNames[m] : "?"; }
const char *Crt_MaskName(int m) { return m >= 0 && m < CRT_NMASKS ? kMaskNames[m] : "?"; }

/* Multiplying an sRGB value by f^(1/2.2) multiplies the light it gives by f: the factors are
 * designed in linear light (what the eye averages over a few pixels), then encoded. */
static uint8_t enc(double lin)
{
    if (lin <= 0.0) return 0;
    if (lin >= 1.0) return 255;
    return (uint8_t)(pow(lin, 1.0 / 2.2) * 255.0 + 0.5);
}

#define BEAM_SIGMA 0.30
/* Light across one source line, t = 0..1 from its top edge: Gaussian beam, 1 at the centre,
 * 1 - amp on the edges (half-way to the next line). */
static double beam(double t, double amp)
{
    double g0 = exp(-0.5 * (0.5 / BEAM_SIGMA) * (0.5 / BEAM_SIGMA));
    double d = (t - 0.5) / BEAM_SIGMA;
    return 1.0 - amp * (1.0 - (exp(-0.5 * d * d) - g0) / (1.0 - g0));
}

void Crt_ScanlineRows(uint8_t *out, int h, int srcLines, int strength)
{
    if (h <= 0) return;
    if (srcLines <= 0) srcLines = 1;
    if (strength < 0) strength = 0;
    if (strength > 100) strength = 100;
    double n = (double)h / srcLines;                  /* output rows per source line */
    double amp = 0.85 * strength / 100.0;
    if (n < 2.0) amp *= n > 1.0 ? n - 1.0 : 0.0;       /* no room for a gap below 2 rows per line */
    /* half a row of phase: the last row of each line sits on the gap (2x, 3x, 4x... = n-1 lit
     * rows + 1 dark row, never two half-dark ones); the beam centre stays inside the line */
    double phase = 0.5 / n;
    for (int y = 0; y < h; y++) {
        double acc = 0.0;
        for (int j = 0; j < 16; j++) {                  /* box filter over the output row */
            double s = (y + (j + 0.5) / 16.0) / n + phase;
            acc += beam(s - floor(s), amp);
        }
        out[y] = enc(acc / 16.0);
    }
}

int Crt_MaskPeriod(int outH) { return outH >= 1200 ? 3 : 2; }

void Crt_MaskRow(uint8_t *rgb, int w, int y, int outH, int mask, int strength)
{
    if (strength < 0) strength = 0;
    if (strength > 100) strength = 100;
    int p = Crt_MaskPeriod(outH), slotH = outH >= 1200 ? 4 : 3;
    double m = 1.0 - 0.5 * strength / 100.0;           /* light through the other phosphors */
    uint8_t on = 255, off = enc(m);
    for (int x = 0; x < w; x++) {
        uint8_t *o = rgb + 3 * x;
        if (mask != CRT_MASK_APERTURE && mask != CRT_MASK_SLOT) { o[0] = o[1] = o[2] = 255; continue; }
        int c = x % p;
        if (p == 2) { o[0] = o[2] = c == 0 ? on : off; o[1] = c == 0 ? off : on; }  /* magenta / green */
        else { o[0] = c == 0 ? on : off; o[1] = c == 1 ? on : off; o[2] = c == 2 ? on : off; }
        /* slot mask: a dark row every slotH rows, staggered by half a cell on odd triads */
        if (mask == CRT_MASK_SLOT && (y + ((x / p) & 1) * (slotH / 2)) % slotH == slotH - 1)
            for (int k = 0; k < 3; k++) o[k] = (uint8_t)((o[k] * off + 127) / 255);
    }
}

/* ------------------------------------------------------------------------------ SDL */
#ifndef CRT_NO_SDL
#include <SDL2/SDL.h>
#include "config.h"
#include "screenshot.h"

#define SRC_LINES 224          /* Saturn lines: the scanline pattern has one gap per line */

static struct {
    int mode, strength, mask, curv;
    SDL_Renderer *r;                     /* owner of the textures */
    SDL_Texture *scanTex;                  /* 4 x h: one factor per output row (MOD) */
    SDL_Texture *maskTex;                  /* w x 1 (aperture, stretched) or w x h (slot) (MOD) */
    SDL_Texture *bent;                   /* w x h target, curvature only */
    SDL_Texture *g1, *g2;                /* glow: 160x112, 80x56 */
    int bw, bh, bmode, bmask, bstr, bcurv, built;   /* what the overlays were built for */
    float boost, glow1, glow2;
    int shot;
    char toast[40];
    Uint32 toastEnd;
} C;

void Crt_Init(int mode, int strength, int mask, int curvature)
{
    C.mode = mode >= 0 && mode < CRT_NMODES ? mode : CRT_OFF;
    C.strength = strength < 0 ? 0 : strength > 100 ? 100 : strength;
    C.mask = mask >= 0 && mask < CRT_NMASKS ? mask : CRT_MASK_APERTURE;
    C.curv = curvature < 0 ? 0 : curvature > 100 ? 100 : curvature;
    C.built = 0;
}

int  Crt_GetMode(void) { return C.mode; }
void Crt_SetMode(int mode) { if (mode >= 0 && mode < CRT_NMODES) C.mode = mode; }
void Crt_RequestOutputShot(void) { C.shot = 1; }

void Crt_Toast(const char *text)
{
    snprintf(C.toast, sizeof C.toast, "%s", text);
    C.toastEnd = SDL_GetTicks() + 2000;
}

void Crt_CycleMode(void)
{
    char t[40];
    C.mode = (C.mode + 1) % CRT_NMODES;
    snprintf(t, sizeof t, "FILTER: %s", Crt_ModeName(C.mode));
    Crt_Toast(t);
    printf("[render] CRT filter: %s\n", Crt_ModeName(C.mode));
}

static void free_overlays(void)
{
    if (C.scanTex) SDL_DestroyTexture(C.scanTex);
    if (C.maskTex) SDL_DestroyTexture(C.maskTex);
    if (C.bent) SDL_DestroyTexture(C.bent);
    C.scanTex = C.maskTex = C.bent = NULL;
    C.built = 0;
}

void Crt_Shutdown(void)
{
    free_overlays();
    if (C.g1) SDL_DestroyTexture(C.g1);
    if (C.g2) SDL_DestroyTexture(C.g2);
    C.g1 = C.g2 = NULL;
    C.r = NULL;
}

/* The letterboxed picture rectangle in output pixels, as SDL_RenderSetLogicalSize sets it. */
static void output_rect(SDL_Renderer *r, int lw, int lh, SDL_Rect *d)
{
    int w = 1, h = 1;
    SDL_GetRendererOutputSize(r, &w, &h);
    d->x = d->y = 0; d->w = w; d->h = h;
    if (!lw || !lh) return;
    float want = (float)lw / lh, real = (float)w / h, scale;
    if (SDL_RenderGetIntegerScale(r)) {
        scale = (float)(want > real ? w / lw : h / lh);
        if (scale < 1.0f) scale = 1.0f;
        d->w = (int)SDL_floor(lw * scale); d->x = (w - d->w) / 2;
        d->h = (int)SDL_floor(lh * scale); d->y = (h - d->h) / 2;
    } else if (SDL_fabs(want - real) < 0.0001) {
        return;
    } else if (want > real) {
        scale = (float)w / lw;
        d->h = (int)SDL_floor(lh * scale); d->y = (h - d->h) / 2;
    } else {
        scale = (float)h / lh;
        d->w = (int)SDL_floor(lw * scale); d->x = (w - d->w) / 2;
    }
}

/* Window target in plain output pixels (logical size off) while the effect is drawn. */
typedef struct { int lw, lh; } PixelSpace;
static PixelSpace px_begin(SDL_Renderer *r)
{
    PixelSpace p;
    SDL_RenderGetLogicalSize(r, &p.lw, &p.lh);
    SDL_RenderSetLogicalSize(r, 0, 0);                 /* also viewport = all, scale = 1 */
    return p;
}
static void px_end(SDL_Renderer *r, PixelSpace p)
{
    if (p.lw && p.lh) SDL_RenderSetLogicalSize(r, p.lw, p.lh);
}

static SDL_Texture *overlay_texture(SDL_Renderer *r, int w, int h, const uint32_t *px)
{
    SDL_Texture *t = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, w, h);
    if (!t) return NULL;
    SDL_UpdateTexture(t, NULL, px, w * 4);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_MOD);
    SDL_SetTextureScaleMode(t, SDL_ScaleModeNearest);
    return t;
}

/* Overlays for a w x h picture; 0 = failed (texture too large...). */
static int build(SDL_Renderer *r, int w, int h)
{
    int full = C.mode == CRT_FULL, mask = full ? C.mask : CRT_MASK_NONE;
    int mh = mask == CRT_MASK_SLOT ? h : 1;
    free_overlays();
    C.bw = w; C.bh = h; C.bmode = C.mode; C.bmask = C.mask; C.bstr = C.strength; C.bcurv = C.curv;
    size_t big = (size_t)w * mh > (size_t)4 * h ? (size_t)w * mh : (size_t)4 * h;
    uint8_t *rows = (uint8_t *)malloc((size_t)h), *mrow = (uint8_t *)malloc((size_t)w * 3);
    uint32_t *px = (uint32_t *)malloc(big * 4);
    int ok = rows && mrow && px;
    double avgScan = 1.0, avgMask = 1.0;
    if (ok) {
        Crt_ScanlineRows(rows, h, SRC_LINES, C.strength);
        double sum = 0;
        for (int y = 0; y < h; y++) {
            uint32_t v = rows[y];
            sum += v;
            for (int x = 0; x < 4; x++) px[y * 4 + x] = 0xFF000000u | v << 16 | v << 8 | v;
        }
        avgScan = sum / (255.0 * h);
        ok = (C.scanTex = overlay_texture(r, 4, h, px)) != NULL;
    }
    if (ok && mask != CRT_MASK_NONE) {
        double sum = 0;
        for (int y = 0; y < mh; y++) {
            Crt_MaskRow(mrow, w, y, h, mask, C.strength);
            for (int x = 0; x < w; x++) {
                const uint8_t *m = mrow + 3 * x;
                px[(size_t)y * w + x] = 0xFF000000u | (uint32_t)m[0] << 16 | (uint32_t)m[1] << 8 | m[2];
                sum += m[0] + m[1] + m[2];
            }
        }
        avgMask = sum / (3.0 * 255.0 * w * mh);
        ok = (C.maskTex = overlay_texture(r, w, mh, px)) != NULL;
    }
    if (ok && full && C.curv > 0) {
        C.bent = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, w, h);
        ok = C.bent != NULL;
        if (ok) { SDL_SetTextureBlendMode(C.bent, SDL_BLENDMODE_NONE); SDL_SetTextureScaleMode(C.bent, SDL_ScaleModeLinear); }
    }
    free(rows); free(mrow); free(px);
    if (!ok) { fprintf(stderr, "[render] CRT filter: cannot create the %dx%d overlays: %s\n", w, h, SDL_GetError()); free_overlays(); return 0; }
    /* brightness: the overlay darkens mid tones by avg; add the picture back (x 1 + boost) */
    float s = C.strength / 100.0f;
    double avg = avgScan * avgMask;
    C.boost = (float)((1.0 / avg - 1.0) * (full ? 0.75 : 1.0));
    if (C.boost > 1.0f) C.boost = 1.0f;
    C.glow1 = full ? 0.12f * s : 0.0f;
    C.glow2 = full ? 0.10f * s : 0.0f;
    C.built = 1;
    return 1;
}

static void add_glow(SDL_Renderer *r, SDL_Texture *g, float k, const SDL_Rect *dst)
{
    Uint8 c = (Uint8)(k * 255.0f + 0.5f);
    if (!g || !c) return;
    SDL_SetTextureBlendMode(g, SDL_BLENDMODE_ADD);
    SDL_SetTextureColorMod(g, c, c, c);
    SDL_RenderCopy(r, g, NULL, dst);
    SDL_SetTextureColorMod(g, 255, 255, 255);
    SDL_SetTextureBlendMode(g, SDL_BLENDMODE_NONE);
}

static SDL_Texture *glow_target(SDL_Renderer *r, int w, int h)
{
    SDL_Texture *t = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, w, h);
    if (t) { SDL_SetTextureBlendMode(t, SDL_BLENDMODE_NONE); SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear); }
    return t;
}

/* Curvature: the picture as a bent mesh (barrel: each edge bows out by up to 9 % of the half
 * size at crt_curvature 100), corners darkened. */
#define MESH_X 32
#define MESH_Y 24
static void draw_bent(SDL_Renderer *r, SDL_Texture *t, const SDL_Rect *d)
{
    static SDL_Vertex v[(MESH_X + 1) * (MESH_Y + 1)];
    static int idx[MESH_X * MESH_Y * 6];
    float k = C.curv / 100.0f * 0.09f, vig = C.curv / 100.0f * 0.6f;
    for (int j = 0; j <= MESH_Y; j++)
        for (int i = 0; i <= MESH_X; i++) {
            SDL_Vertex *o = &v[j * (MESH_X + 1) + i];
            float u = (float)i / MESH_X, w = (float)j / MESH_Y, sx = 2 * u - 1, sy = 2 * w - 1;
            float qx = sx * (1 - k * sy * sy), qy = sy * (1 - k * sx * sx);
            float c2 = sx * sx * sy * sy, f = 1 - vig * c2 * c2;
            Uint8 c = (Uint8)(f * 255.0f + 0.5f);
            o->position.x = d->x + (qx + 1) * 0.5f * d->w;
            o->position.y = d->y + (qy + 1) * 0.5f * d->h;
            o->tex_coord.x = u; o->tex_coord.y = w;
            o->color.r = o->color.g = o->color.b = c; o->color.a = 255;
        }
    int n = 0;
    for (int j = 0; j < MESH_Y; j++)
        for (int i = 0; i < MESH_X; i++) {
            int a = j * (MESH_X + 1) + i, b = a + 1, c = a + MESH_X + 1, e = c + 1;
            idx[n++] = a; idx[n++] = b; idx[n++] = e;
            idx[n++] = a; idx[n++] = e; idx[n++] = c;
        }
    SDL_RenderGeometry(r, t, v, (MESH_X + 1) * (MESH_Y + 1), idx, n);
}

int Crt_Draw(SDL_Renderer *r, SDL_Texture *scene)
{
    if (C.mode == CRT_OFF || !scene || !r) return 0;
    if (C.r != r) { Crt_Shutdown(); C.r = r; }
    PixelSpace p = px_begin(r);
    SDL_Rect d;
    output_rect(r, p.lw, p.lh, &d);
    if (d.w < 16 || d.h < 16) { px_end(r, p); return 0; }      /* minimised: plain path */
    if (!C.built || C.bw != d.w || C.bh != d.h || C.bmode != C.mode || C.bmask != C.mask ||
        C.bstr != C.strength || C.bcurv != C.curv) {
        if (!build(r, d.w, d.h)) { C.mode = CRT_OFF; px_end(r, p); return 0; }
        static int warned;
        SDL_RendererInfo info;
        if (!warned && SDL_GetRendererInfo(r, &info) == 0 && (info.flags & SDL_RENDERER_SOFTWARE)) {
            warned = 1;
            fprintf(stderr, "[render] CRT filter on the software renderer (no GPU): slow at large sizes\n");
        }
    }
    int full = C.mode == CRT_FULL, bent = full && C.bent;
    SDL_ScaleMode sceneMode = Config_Get()->bilinear ? SDL_ScaleModeLinear : SDL_ScaleModeNearest;

    /* glow source: the frame averaged down (a bilinear halving = exact 2x2 box) */
    if (full && (C.glow1 > 0 || C.glow2 > 0)) {
        if (!C.g1) C.g1 = glow_target(r, 160, 112);
        if (!C.g2) C.g2 = glow_target(r, 80, 56);
        SDL_SetTextureScaleMode(scene, SDL_ScaleModeLinear);
        SDL_SetTextureBlendMode(scene, SDL_BLENDMODE_NONE);
        if (C.g1) { SDL_SetRenderTarget(r, C.g1); SDL_RenderCopy(r, scene, NULL, NULL); }
        if (C.g1 && C.g2) { SDL_SetRenderTarget(r, C.g2); SDL_RenderCopy(r, C.g1, NULL, NULL); }
        SDL_SetRenderTarget(r, NULL);
    }

    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderClear(r);                                 /* black borders */
    SDL_Rect dst = d;
    if (bent) { SDL_SetRenderTarget(r, C.bent); dst.x = dst.y = 0; }
    /* 1. picture (crt: bilinear = soft horizontal beam) 2. + boost 3. x scanlines */
    SDL_SetTextureScaleMode(scene, full ? SDL_ScaleModeLinear : sceneMode);
    SDL_SetTextureBlendMode(scene, SDL_BLENDMODE_NONE);
    SDL_RenderCopy(r, scene, NULL, &dst);
    Uint8 b = (Uint8)(C.boost * 255.0f + 0.5f);
    if (b) {
        SDL_SetTextureBlendMode(scene, SDL_BLENDMODE_ADD);
        SDL_SetTextureColorMod(scene, b, b, b);
        SDL_RenderCopy(r, scene, NULL, &dst);
        SDL_SetTextureColorMod(scene, 255, 255, 255);
        SDL_SetTextureBlendMode(scene, SDL_BLENDMODE_NONE);
    }
    SDL_RenderCopy(r, C.scanTex, NULL, &dst);
    /* 4. glow fills the gaps where the picture is bright 5. x phosphor mask (screen space) */
    if (full) { add_glow(r, C.g1, C.glow1, &dst); add_glow(r, C.g2, C.glow2, &dst); }
    if (bent) { SDL_SetRenderTarget(r, NULL); draw_bent(r, C.bent, &d); }
    if (C.maskTex) SDL_RenderCopy(r, C.maskTex, NULL, &d);
    SDL_SetTextureScaleMode(scene, sceneMode);
    px_end(r, p);
    return 1;
}

/* ---- toast: 5x7 capitals drawn at the game's pixel size, bottom left of the picture ---- */
static const char kGlyphChars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789:-%./";
static const uint8_t kGlyphs[][7] = {
    {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}, {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}, /* A B */
    {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}, {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}, /* C D */
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}, /* E F */
    {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}, {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}, /* G H */
    {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}, {0x07,0x02,0x02,0x02,0x02,0x12,0x0C}, /* I J */
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}, /* K L */
    {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}, {0x11,0x11,0x19,0x15,0x13,0x11,0x11}, /* M N */
    {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}, /* O P */
    {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}, {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}, /* Q R */
    {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}, {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}, /* S T */
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, {0x11,0x11,0x11,0x11,0x11,0x0A,0x04}, /* U V */
    {0x11,0x11,0x11,0x15,0x15,0x15,0x0A}, {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}, /* W X */
    {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}, {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}, /* Y Z */
    {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}, /* 0 1 */
    {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}, {0x1F,0x02,0x04,0x02,0x01,0x11,0x0E}, /* 2 3 */
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}, /* 4 5 */
    {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}, {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}, /* 6 7 */
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}, /* 8 9 */
    {0x00,0x0C,0x0C,0x00,0x0C,0x0C,0x00}, {0x00,0x00,0x00,0x1F,0x00,0x00,0x00}, /* : - */
    {0x18,0x19,0x02,0x04,0x08,0x13,0x03}, {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C}, /* % . */
    {0x00,0x01,0x02,0x04,0x08,0x10,0x00},                                       /* /   */
};

static void draw_toast(SDL_Renderer *r, const SDL_Rect *d, Uint32 left)
{
    static SDL_Rect dots[sizeof C.toast * 35];
    int u = d->h / SRC_LINES > 0 ? d->h / SRC_LINES : 1, n = 0, len = (int)strlen(C.toast);
    int x0 = d->x + 8 * u, y0 = d->y + d->h - 15 * u, pad = 3 * u;   /* clear of the HUD */
    Uint8 a = (Uint8)(left < 400 ? left * 255 / 400 : 255);
    for (int i = 0; i < len; i++) {
        const char *g = strchr(kGlyphChars, toupper((unsigned char)C.toast[i]));
        if (!g || !*g) continue;                       /* space / unknown */
        const uint8_t *rows = kGlyphs[g - kGlyphChars];
        for (int y = 0; y < 7; y++)
            for (int x = 0; x < 5; x++)
                if (rows[y] & (0x10 >> x)) {
                    SDL_Rect q = { x0 + (i * 6 + x) * u, y0 + y * u, u, u };
                    dots[n++] = q;
                }
    }
    SDL_Rect box = { x0 - pad, y0 - pad, (len * 6 - 1) * u + 2 * pad, 7 * u + 2 * pad };
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, (Uint8)(a * 3 / 4));
    SDL_RenderFillRect(r, &box);
    SDL_SetRenderDrawColor(r, 255, 255, 255, a);
    if (n) SDL_RenderFillRects(r, dots, n);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}

/* Shift+F12: the window picture as shown (filtered), at its output size. */
static void save_output(SDL_Renderer *r, const SDL_Rect *d)
{
    char path[1200];
    uint8_t *rgb = (uint8_t *)malloc((size_t)d->w * d->h * 3);
    if (!rgb) return;
    if (SDL_RenderReadPixels(r, d, SDL_PIXELFORMAT_RGB24, rgb, d->w * 3) != 0)
        fprintf(stderr, "[render] screenshot: %s\n", SDL_GetError());
    else if (Screenshot_Save(Config_BaseDir(), rgb, d->w, d->h, Config_Get()->screenshot_png, path, sizeof path)) {
        printf("[render] screenshot %s (%dx%d, as shown)\n", path, d->w, d->h);
        Crt_Toast("SCREENSHOT SAVED");
    } else fprintf(stderr, "[render] cannot write %s\n", path);
    free(rgb);
}

void Crt_Finish(SDL_Renderer *r)
{
    if (!r) return;
    Uint32 now = SDL_GetTicks();
    int toast = C.toast[0] && !SDL_TICKS_PASSED(now, C.toastEnd);
    if (!toast) C.toast[0] = 0;
    if (!C.shot && !toast) return;
    PixelSpace p = px_begin(r);
    SDL_Rect d;
    output_rect(r, p.lw, p.lh, &d);
    if (C.shot) { C.shot = 0; save_output(r, &d); }
    if (toast) draw_toast(r, &d, C.toastEnd - now);
    px_end(r, p);
}
#endif
