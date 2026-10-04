#include "soft_render.h"
#include "ram.h"
#include "sprite.h"
#include "stage.h"
#include "fade.h"
#include "stage_hooks.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LAYER_DESC   0x060EA588u
#define STAGE_CTRL   0x060EA618u
#define NBG_CFG      0x06020D08u    /* SBL SclConfig[4], 0x4C each: +0 dispenbl, +12 plate_addr[0] */
#define NBG_SCROLL   0x06024058u    /* NBGn scroll/zoom shadow, 16 bytes each: +0 X, +4 Y (16.16) */
#define VDP2_SPCTL   (VDP2_SHADOW_ADDR + 0x00)
#define VDP2_CRAOFA  (VDP2_SHADOW_ADDR + 0x04)
#define VDP2_PRIS    (VDP2_SHADOW_ADDR + 0x10)
#define VDP2_PRIN    (VDP2_SHADOW_ADDR + 0x18)

/* ---- colours ---- */

uint32_t Vdp_Rgb555ToArgb(uint16_t c)
{
    uint32_t r = (c & 31) * 255 / 31, g = ((c >> 5) & 31) * 255 / 31, b = ((c >> 10) & 31) * 255 / 31;
    return 0xFF000000u | r << 16 | g << 8 | b;
}

uint32_t Vdp2_CramArgb(unsigned idx)
{
    return Vdp_Rgb555ToArgb(R16(SHADOW_CRAM + 2u * (idx & 0x7FF)));
}

/* ---- VDP2 priorities ----
 * Boot (SHINOBI 0x060401A0..) sets SPCTL sprite type 3 (dot: bit 15 MSB, 14-13 priority
 * register, 12-11 colour calc, 10-0 colour code); the stage init 0x060472D2 copies the stage
 * control block 0x060EA618 +0x22..+0x28 (S0..S3) and +0x2A..+0x30 (NBG0..NBG3) through
 * SCL_SetPriority 0x0602AFD4 (values per stage: stage_inits.c). */
void Vdp2_GetPriorities(Vdp2Prio *p)
{
    uint16_t spctl = R16(VDP2_SPCTL);
    int any = 0;
    p->sptype = spctl ? (spctl & 0xF) : 3;      /* PC: boot VDP2 init not ported -> type 3 */
    for (int n = 0; n < 8; n++) any |= p->spr[n] = (R16(VDP2_PRIS + 2u * (uint32_t)(n >> 1)) >> (8 * (n & 1))) & 7;
    for (int n = 0; n < 4; n++) any |= p->nbg[n] = (R16(VDP2_PRIN + 2u * (uint32_t)(n >> 1)) >> (8 * (n & 1))) & 7;
    if (any) return;
    /* PC: 0x060472D2 not run -> take the values it would copy from the stage control block */
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
int Vdp2_SpritePrioReg(const RenderSprite *s, int type)
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

/* ---- VDP1 characters ---- */

/* Decode a VDP1 character (CMDPMOD colour modes 0-5) from the emulated VRAM.
 * Dot 0 is transparent unless SPD (CMDPMOD bit 6) is set. */
void Vdp1_DecodeChar(const RenderSprite *s, uint32_t *argb)
{
    const uint8_t *v = Ram_Ptr(s->src);
    int n = s->w * s->h, spd = (s->mode & 0x40) != 0;
    for (int i = 0; i < n; i++) {
        unsigned dot;
        switch ((s->mode >> 3) & 7) {
        case 0:                                     /* 4bpp colour bank */
            dot = (i & 1) ? v[i >> 1] & 15 : v[i >> 1] >> 4;
            argb[i] = (dot || spd) ? Vdp2_CramArgb((s->colr & 0xFFF0u) | dot) : 0;
            break;
        case 1: {                                   /* 4bpp lookup table at VRAM colr*8 */
            dot = (i & 1) ? v[i >> 1] & 15 : v[i >> 1] >> 4;
            uint16_t e = R16(VDP1_VRAM_BASE + (uint32_t)s->colr * 8u + dot * 2u);
            if (!dot && !spd) argb[i] = 0;
            else if (e & 0x8000) argb[i] = Vdp_Rgb555ToArgb(e);
            else argb[i] = Vdp2_CramArgb(e);
            break; }
        case 2: dot = v[i] & 0x3F; argb[i] = (dot || spd) ? Vdp2_CramArgb((s->colr & 0xFFC0u) | dot) : 0; break;
        case 3: dot = v[i] & 0x7F; argb[i] = (dot || spd) ? Vdp2_CramArgb((s->colr & 0xFF80u) | dot) : 0; break;
        case 4: dot = v[i];        argb[i] = (dot || spd) ? Vdp2_CramArgb((s->colr & 0xFF00u) | dot) : 0; break;
        default: {                                  /* RGB */
            uint16_t e = (uint16_t)(v[2 * i] << 8 | v[2 * i + 1]);
            if (!(e & 0x8000) && !(spd && e == 0)) { argb[i] = 0; break; }
            argb[i] = Vdp_Rgb555ToArgb(e);
            break; }
        }
    }
}

/* ---- VDP2 background layers ---- */

static uint32_t desc_of(int nbg) { return LAYER_DESC + (uint32_t)nbg * 0x30u; }
static unsigned craof(int nbg) { return (R16(VDP2_CRAOFA) >> (4 * nbg)) & 7; }

int Vdp2_LayerEnabled(int nbg)
{
    if (nbg < 0 || nbg > 3) return 0;
    /* SclConfig byte 0 = display enable: set by SHINOBI's VDP2 init 0x060400B0, cleared by
     * Director_Init 0x06040094 (DIRECTOR = sprites only) */
    if (!Ram_Ptr(NBG_CFG + 0x4Cu * (uint32_t)nbg)[0]) return 0;
    if (nbg == 3) return R32(NBG_CFG + 0x4Cu * 3 + 12) != 0;
    uint32_t d = desc_of(nbg), lay = R32(d + 0x20);
    return lay && R32(d + 0x1C) && Ram_Ptr(lay)[0] && Ram_Ptr(lay)[1];
}

/* NBG0-2: VDP2 scroll register = layer position (desc +0/+4, computed by Scroll_Update /
 * 0x060470A4) - 1024 px. */
void Vdp2_LayerScroll(int nbg, int *sx, int *sy)
{
    if (nbg == 3) {
        *sx = RS32(NBG_SCROLL + 0x30) >> 16;
        *sy = RS32(NBG_SCROLL + 0x34) >> 16;
        return;
    }
    uint32_t d = desc_of(nbg);
    *sx = (RS32(d + 0) >> 16) - 1024;
    *sy = (RS32(d + 4) >> 16) - 1024;
}

static int wrapi(int a, int n) { int r = a % n; return r < 0 ? r + n : r; }

int Vdp2_LayerPageAt(int nbg, int px, int py)
{
    uint32_t lay = R32(desc_of(nbg) + 0x20);
    int pw = Ram_Ptr(lay)[0], ph = Ram_Ptr(lay)[1];
    if (pw <= 0 || ph <= 0) return -1;
    uint8_t pid = Ram_Ptr(lay)[2 + wrapi(py, ph) * pw + wrapi(px, pw)];
    return pid == 0xFF ? -1 : pid;
}

uint32_t Vdp2_MapPageChecksum(int nbg, int pageId)
{
    uint32_t a = R32(desc_of(nbg) + 0x1C) + (uint32_t)pageId * 0x800u, h = 2166136261u;
    const uint8_t *p = Ram_Ptr(a);
    for (int i = 0; i < 0x800; i++) h = (h ^ p[i]) * 16777619u;
    return h;
}

/* One page = 32x32 MAP words, each a 16x16 character of 2x2 8x8 cells (TL,TR,BL,BR), 256
 * colours (8bpp). The words are what 0x06046B14 turns into VDP2 2-word pattern names:
 *   bit 15 H-flip, bit 14 V-flip, bit 13 special priority (unused: SFPRMD stays 0 = per screen),
 *   bit 12 palette bit 4 (+256 colours), bit 0 special colour calc (no effect: colour calc is
 *   off for the stage NBGs), bits 11-1 character pair: character number = base (desc+0x2C) +
 *   (word & 0xFFE) * 4, in 0x20-byte units (one 16x16 8bpp character = 256 bytes).
 * Colour = CRAM[(CRAOFA(nbg) + palette) * 256 + dot], dot 0 transparent. */
void Vdp2_DecodeMapPage(int nbg, int pageId, uint32_t *argb)
{
    uint32_t d = desc_of(nbg);
    uint32_t pages = R32(d + 0x1C) + (uint32_t)pageId * 0x800u;
    uint32_t base = R32(d + 0x2C);
    unsigned off = craof(nbg) << 8;
    for (int ty = 0; ty < 32; ty++)
        for (int tx = 0; tx < 32; tx++) {
            uint16_t pn = R16(pages + (uint32_t)(ty * 32 + tx) * 2);
            uint32_t chr = VDP2_VRAM_BASE + (((base + (((uint32_t)pn & 0x0FFEu) << 2)) * 0x20u) & (VDP2_VRAM_SIZE - 1));
            unsigned pal = off + (((unsigned)pn >> 12 & 1) << 8);
            int hf = (pn & 0x8000) != 0, vf = (pn & 0x4000) != 0;
            for (int y = 0; y < 16; y++) {
                int yy = vf ? 15 - y : y;
                uint32_t *row = argb + (ty * 16 + y) * VDP2_PAGE_PX + tx * 16;
                for (int x = 0; x < 16; x++) {
                    int xx = hf ? 15 - x : x;
                    uint32_t cell = (uint32_t)((yy >> 3) * 2 + (xx >> 3));
                    uint8_t v = Ram_Ptr(chr + cell * 64 + (uint32_t)(yy & 7) * 8 + (uint32_t)(xx & 7))[0];
                    row[x] = v ? Vdp2_CramArgb(pal + v) : 0;
                }
            }
        }
}

/* NBG3 (SHINOBI's VDP2 init 0x060400B0: 1x1 cells, 16 colours, 2-word pattern names, plane =
 * one page of 64x64 cells, all plates at 0x25E6C000; map written by 0x06047244): pattern name
 * bit 31 V-flip, bit 30 H-flip, bits 22-16 palette (16-colour units), bits 14-0 character. */
static uint32_t nbg3_page(void) { return R32(NBG_CFG + 0x4Cu * 3 + 12); }

uint32_t Vdp2_Nbg3Checksum(void)
{
    uint32_t a = nbg3_page(), h = 2166136261u;
    const uint8_t *p = Ram_Ptr(a);
    for (int i = 0; i < 0x4000; i++) h = (h ^ p[i]) * 16777619u;
    return h;
}

void Vdp2_DecodeNbg3(uint32_t *argb)
{
    uint32_t page = nbg3_page();
    unsigned off = craof(3) << 8;
    for (int cy = 0; cy < 64; cy++)
        for (int cx = 0; cx < 64; cx++) {
            uint32_t pn = R32(page + (uint32_t)(cy * 64 + cx) * 4);
            uint32_t chr = VDP2_VRAM_BASE + (((pn & 0x7FFFu) * 0x20u) & (VDP2_VRAM_SIZE - 1));
            unsigned pal = off + ((pn >> 16) & 0x7F) * 16;
            int hf = (pn & 0x40000000u) != 0, vf = (pn & 0x80000000u) != 0;
            for (int y = 0; y < 8; y++) {
                int yy = vf ? 7 - y : y;
                uint32_t *row = argb + (cy * 8 + y) * VDP2_PAGE_PX + cx * 8;
                for (int x = 0; x < 8; x++) {
                    int xx = hf ? 7 - x : x;
                    uint8_t b = Ram_Ptr(chr + (uint32_t)yy * 4 + (uint32_t)(xx >> 1))[0];
                    unsigned v = (xx & 1) ? b & 15u : b >> 4;
                    row[x] = v ? Vdp2_CramArgb(pal + v) : 0;
                }
            }
        }
}

/* ---- line / cell scroll ---- */

#define SH_SCRCTL 0x0603A60Au
#define SH_VCSTA  0x0603A60Cu
#define SH_LSTA0  0x0603A610u
#define SH_LSTA1  0x0603A614u

void Vdp2_GetLineScroll(int nbg, Vdp2LineScroll *s)
{
    int sx, sy;
    const uint32_t m = VDP2_VRAM_SIZE - 1;
    Vdp2_LayerScroll(nbg, &sx, &sy);
    s->sy = sy; s->lines = 0; s->cells = 0;
    for (int y = 0; y < SCREEN_H; y++) s->sx[y] = sx;
    for (int c = 0; c <= SCREEN_W / 8; c++) s->dy[c] = 0;
    if (nbg == 2 && (LineTable_DmaActive() & 0x2000)) {
        for (int y = 0; y < SCREEN_H; y++) s->sx[y] = RS16(LINE_HBL_W + (uint32_t)y * 8u);
        s->lines = 1;
        return;
    }
    if (nbg < 0 || nbg > 1) return;
    unsigned scr = (R16(SH_SCRCTL) >> (8 * nbg)) & 0xFF;
    if (scr & 2) {                                         /* LSCX */
        uint32_t base = (R32(nbg ? SH_LSTA1 : SH_LSTA0) << 1) & m;
        uint32_t size = 4u * (((scr >> 1) & 1) + ((scr >> 2) & 1) + ((scr >> 3) & 1));
        int iv = 1 << ((scr >> 4) & 3);
        for (int y = 0; y < SCREEN_H; y++)
            s->sx[y] = sx + (RS32(VDP2_VRAM_BASE | ((base + (uint32_t)(y / iv) * size) & m)) >> 16);
        s->lines = 1;
    }
    if (scr & 1) {                                         /* VCSEN */
        uint32_t base = (R32(SH_VCSTA) << 1) & m;
        int both = (R16(SH_SCRCTL) & 0x101) == 0x101;
        uint32_t stride = both ? 8u : 4u, off = (both && nbg == 1) ? 4u : 0u;
        for (int c = 0; c <= SCREEN_W / 8; c++)
            s->dy[c] = RS32(VDP2_VRAM_BASE | ((base + (uint32_t)c * stride + off) & m)) >> 16;
        s->cells = 1;
    }
}

int Vdp2_LineColorOffset(int line, int *r, int *g, int *b)
{
    if (!(LineTable_DmaActive() & 0x4000) || line < 0 || line >= 0x108) return 0;
    uint32_t w = LINE_HBL_W + (uint32_t)line * 8u;
    *r = RS16(w + 2); *g = RS16(w + 4); *b = RS16(w + 6);
    return 1;
}

/* ---- reference frame ---- */

static uint32_t s_page[VDP2_PAGE_PX * VDP2_PAGE_PX];

static void blit_page(uint32_t *fb, int dx, int dy)
{
    for (int y = 0; y < VDP2_PAGE_PX; y++) {
        int sy = dy + y;
        if (sy < 0 || sy >= SCREEN_H) continue;
        for (int x = 0; x < VDP2_PAGE_PX; x++) {
            int sx = dx + x;
            if (sx < 0 || sx >= SCREEN_W) continue;
            uint32_t c = s_page[y * VDP2_PAGE_PX + x];
            if (c) fb[sy * SCREEN_W + sx] = c;
        }
    }
}

static int floordiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

/* NBG0-2 with line / cell scroll: per pixel, decoded pages cached */
static void soft_layer_lines(uint32_t *fb, int nbg, const Vdp2LineScroll *ls)
{
    enum { NC = 6 };
    static uint32_t *cache[NC];
    int cpid[NC], next = 0;
    for (int i = 0; i < NC; i++) {
        cpid[i] = -1;
        if (!cache[i]) cache[i] = (uint32_t *)malloc((size_t)VDP2_PAGE_PX * VDP2_PAGE_PX * sizeof(uint32_t));
        if (!cache[i]) return;
    }
    for (int y = 0; y < SCREEN_H; y++)
        for (int x = 0; x < SCREEN_W; x++) {
            int wx = ls->sx[y] + x, wy = ls->sy + ls->dy[x >> 3] + y;
            int px = floordiv(wx, VDP2_PAGE_PX), py = floordiv(wy, VDP2_PAGE_PX);
            int pid = Vdp2_LayerPageAt(nbg, px, py), k;
            if (pid < 0) continue;
            for (k = 0; k < NC && cpid[k] != pid; k++) ;
            if (k == NC) {
                k = next; next = (next + 1) % NC;
                Vdp2_DecodeMapPage(nbg, pid, cache[k]);
                cpid[k] = pid;
            }
            uint32_t c = cache[k][(wy - py * VDP2_PAGE_PX) * VDP2_PAGE_PX + (wx - px * VDP2_PAGE_PX)];
            if (c) fb[y * SCREEN_W + x] = c;
        }
}

static void soft_layer(uint32_t *fb, int nbg)
{
    int sx, sy;
    if (!Vdp2_LayerEnabled(nbg)) return;
    if (nbg != 3) {
        static Vdp2LineScroll ls;
        Vdp2_GetLineScroll(nbg, &ls);
        if (ls.lines || ls.cells) { soft_layer_lines(fb, nbg, &ls); return; }
    }
    Vdp2_LayerScroll(nbg, &sx, &sy);
    if (nbg == 3) {
        Vdp2_DecodeNbg3(s_page);
        for (int py = floordiv(sy, VDP2_PAGE_PX); py * VDP2_PAGE_PX < sy + SCREEN_H; py++)
            for (int px = floordiv(sx, VDP2_PAGE_PX); px * VDP2_PAGE_PX < sx + SCREEN_W; px++)
                blit_page(fb, px * VDP2_PAGE_PX - sx, py * VDP2_PAGE_PX - sy);
        return;
    }
    for (int py = floordiv(sy, VDP2_PAGE_PX); py * VDP2_PAGE_PX < sy + SCREEN_H; py++)
        for (int px = floordiv(sx, VDP2_PAGE_PX); px * VDP2_PAGE_PX < sx + SCREEN_W; px++) {
            int pid = Vdp2_LayerPageAt(nbg, px, py);
            if (pid < 0) continue;
            Vdp2_DecodeMapPage(nbg, pid, s_page);
            blit_page(fb, px * VDP2_PAGE_PX - sx, py * VDP2_PAGE_PX - sy);
        }
}

static uint32_t blend_half(uint32_t a, uint32_t b)
{
    return 0xFF000000u | (((a & 0xFEFEFEu) >> 1) + ((b & 0xFEFEFEu) >> 1));
}

/* Affine mapping of the texture through vertices A (0,0), B (w-1,0), D (0,h-1). */
static void soft_sprite(uint32_t *fb, const RenderSprite *s)
{
    if (s->w <= 0 || s->h <= 0 || s->w * s->h > 512 * 512) return;
    uint32_t *tex = (uint32_t *)malloc((size_t)s->w * (size_t)s->h * sizeof *tex);
    if (!tex) return;
    Vdp1_DecodeChar(s, tex);
    double ux = s->qx[1] - s->qx[0], uy = s->qy[1] - s->qy[0];
    double vx = s->qx[3] - s->qx[0], vy = s->qy[3] - s->qy[0];
    if (ux == 0 && uy == 0) ux = 1;
    if (vx == 0 && vy == 0) vy = 1;
    double det = ux * vy - uy * vx;
    if (det == 0) { free(tex); return; }
    int x0 = SCREEN_W, x1 = -1, y0 = SCREEN_H, y1 = -1;
    for (int i = 0; i < 4; i++) {
        if (s->qx[i] < x0) x0 = s->qx[i];
        if (s->qx[i] > x1) x1 = s->qx[i];
        if (s->qy[i] < y0) y0 = s->qy[i];
        if (s->qy[i] > y1) y1 = s->qy[i];
    }
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= SCREEN_W) x1 = SCREEN_W - 1;
    if (y1 >= SCREEN_H) y1 = SCREEN_H - 1;
    int cc = ((s->mode >> 3) & 7) == 5 ? (s->mode & 7) : 0;
    double sw = s->w > 1 ? s->w - 1 : 1, sh = s->h > 1 ? s->h - 1 : 1;
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            double px = x - s->qx[0], py = y - s->qy[0];
            double a = (px * vy - py * vx) / det, b = (ux * py - uy * px) / det;
            int tx = (int)(a * sw + 0.5), ty = (int)(b * sh + 0.5);
            if (a < -0.5 / sw || b < -0.5 / sh || tx < 0 || ty < 0 || tx >= s->w || ty >= s->h) continue;
            if (s->flipH) tx = s->w - 1 - tx;
            if (s->flipV) ty = s->h - 1 - ty;
            uint32_t c = tex[ty * s->w + tx];
            if (!c) continue;
            uint32_t *d = &fb[y * SCREEN_W + x];
            switch (cc) {
            case 1: *d = blend_half(*d, 0xFF000000u); break;               /* shadow */
            case 2: *d = blend_half(c, 0xFF000000u); break;                /* half luminance */
            case 3: *d = blend_half(*d, c); break;                         /* half transparency */
            default: *d = c; break;
            }
        }
    free(tex);
}

static uint8_t clamp8(int v) { return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v); }

static void soft_offset(uint32_t *fb, const uint8_t *src, int layer)
{
    int r, g, b;
    if (!Fade_GetLayerOffset(layer, &r, &g, &b)) return;
    int lineA = !((R16(0x0603B49Cu) >> layer) & 1);          /* CLOFSL: offset A -> per-line DMA */
    for (int y = 0; y < SCREEN_H; y++) {
        int lr = r, lg = g, lb = b;
        if (lineA) Vdp2_LineColorOffset(y, &lr, &lg, &lb);
        if (!(lr || lg || lb)) continue;
        for (int i = y * SCREEN_W; i < (y + 1) * SCREEN_W; i++) {
            if (src[i] != layer) continue;
            uint32_t c = fb[i];
            fb[i] = 0xFF000000u | (uint32_t)clamp8((int)(c >> 16 & 255) + lr) << 16 |
                    (uint32_t)clamp8((int)(c >> 8 & 255) + lg) << 8 | clamp8((int)(c & 255) + lb);
        }
    }
}

void Soft_ComposeFrame(uint32_t *fb, const RenderSprite *spr, int n, uint32_t backArgb)
{
    static uint8_t src[SCREEN_W * SCREEN_H];     /* which screen produced each pixel (CLOFEN bit) */
    static uint32_t tmp[SCREEN_W * SCREEN_H];
    Vdp2Prio pr;
    Vdp2_GetPriorities(&pr);
    for (int i = 0; i < SCREEN_W * SCREEN_H; i++) { fb[i] = backArgb; src[i] = 5; }
    if (!(R16(0x0603A570u) & 0x8000)) {          /* VDP2 TVMD shadow: DISP off = black screen */
        for (int i = 0; i < SCREEN_W * SCREEN_H; i++) fb[i] = 0xFF000000u;
        return;
    }
    int *order = (int *)malloc((size_t)(n > 0 ? n : 1) * sizeof *order);
    if (!order) return;
    for (int i = 0; i < n; i++) {                 /* stable sort by SPR_2 draw priority */
        int j = i;
        while (j > 0 && spr[order[j - 1]].prio > spr[i].prio) { order[j] = order[j - 1]; j--; }
        order[j] = i;
    }
    for (int p = 1; p < 8; p++) {
        for (int l = 3; l >= 0; l--) {
            if (pr.nbg[l] != p) continue;
            memcpy(tmp, fb, sizeof tmp);
            soft_layer(fb, l);
            for (int i = 0; i < SCREEN_W * SCREEN_H; i++) if (fb[i] != tmp[i]) src[i] = (uint8_t)l;
        }
        for (int k = 0; k < n; k++) {
            const RenderSprite *s = &spr[order[k]];
            if (pr.spr[Vdp2_SpritePrioReg(s, pr.sptype)] != p) continue;
            memcpy(tmp, fb, sizeof tmp);
            soft_sprite(fb, s);
            for (int i = 0; i < SCREEN_W * SCREEN_H; i++) if (fb[i] != tmp[i]) src[i] = 6;
        }
    }
    free(order);
    for (int l = 0; l < 7; l++) soft_offset(fb, src, l);
}

/* ---- PNG ---- */

static uint32_t s_crc[256];

static uint32_t crc32_upd(uint32_t c, const uint8_t *p, size_t n)
{
    if (!s_crc[1])
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t v = i;
            for (int k = 0; k < 8; k++) v = (v & 1) ? 0xEDB88320u ^ (v >> 1) : v >> 1;
            s_crc[i] = v;
        }
    for (size_t i = 0; i < n; i++) c = s_crc[(c ^ p[i]) & 255] ^ (c >> 8);
    return c;
}

static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }

static int chunk(FILE *f, const char *type, const uint8_t *data, size_t n)
{
    uint8_t h[8];
    put32(h, (uint32_t)n);
    memcpy(h + 4, type, 4);
    uint32_t c = crc32_upd(0xFFFFFFFFu, h + 4, 4);
    c = crc32_upd(c, data, n) ^ 0xFFFFFFFFu;
    uint8_t t[4];
    put32(t, c);
    return fwrite(h, 1, 8, f) == 8 && (n == 0 || fwrite(data, 1, n, f) == n) && fwrite(t, 1, 4, f) == 4;
}

int Soft_WritePng(const char *path, const uint32_t *argb, int w, int h)
{
    static const uint8_t sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    size_t raw = (size_t)h * ((size_t)w * 3 + 1);
    size_t blocks = (raw + 65534) / 65535;
    size_t zn = 2 + raw + blocks * 5 + 4;
    uint8_t *img = (uint8_t *)malloc(raw), *z = (uint8_t *)malloc(zn);
    FILE *f = NULL;
    int ok = 0;
    if (!img || !z) goto done;
    for (int y = 0; y < h; y++) {
        uint8_t *r = img + (size_t)y * ((size_t)w * 3 + 1);
        r[0] = 0;
        for (int x = 0; x < w; x++) {
            uint32_t c = argb[y * w + x];
            r[1 + 3 * x] = (uint8_t)(c >> 16); r[2 + 3 * x] = (uint8_t)(c >> 8); r[3 + 3 * x] = (uint8_t)c;
        }
    }
    size_t o = 0;
    z[o++] = 0x78; z[o++] = 0x01;
    uint32_t s1 = 1, s2 = 0;
    for (size_t i = 0; i < raw; i++) { s1 = (s1 + img[i]) % 65521u; s2 = (s2 + s1) % 65521u; }
    for (size_t p = 0; p < raw; ) {
        size_t n = raw - p > 65535 ? 65535 : raw - p;
        z[o++] = (uint8_t)(p + n == raw);
        z[o++] = (uint8_t)n; z[o++] = (uint8_t)(n >> 8);
        z[o++] = (uint8_t)~n; z[o++] = (uint8_t)(~n >> 8);
        memcpy(z + o, img + p, n);
        o += n; p += n;
    }
    put32(z + o, s2 << 16 | s1); o += 4;
    f = fopen(path, "wb");
    if (!f) goto done;
    uint8_t ihdr[13];
    put32(ihdr, (uint32_t)w); put32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    ok = fwrite(sig, 1, 8, f) == 8 && chunk(f, "IHDR", ihdr, 13) && chunk(f, "IDAT", z, o) && chunk(f, "IEND", NULL, 0);
done:
    if (f) fclose(f);
    free(img);
    free(z);
    return ok;
}
