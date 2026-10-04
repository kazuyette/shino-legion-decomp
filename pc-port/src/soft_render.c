#include "soft_render.h"
#include "u8path.h"
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

/* Back screen colour: one RGB555 word in VDP2 VRAM at BKTAU/BKTAL (register shadow 0x0603A570
 * + 0xAC/0xAE, VRAM word address); the game leaves them 0 (word 0 of VDP2 VRAM, 0 = black in
 * every real-game dump). */
uint32_t Vdp2_BackColor(void)
{
    uint32_t a = ((uint32_t)(R16(0x0603A570u + 0xACu) & 7) << 16 | R16(0x0603A570u + 0xAEu)) * 2u;
    return Vdp_Rgb555ToArgb(R16(VDP2_VRAM_BASE + (a & (VDP2_VRAM_SIZE - 1))));
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

/* VDP2 colour calculation of a sprite whose priority (register value) is `prio`: the ratio
 * register value r (0..31, result = sprite * (31 - r) / 32 + second screen * (r + 1) / 32), or
 * -1 when no colour calculation applies (CCCTL bit 6, SPCTL condition SPCCCS / SPCCN, colour-calc
 * register from the sprite's colour data; RGB dots in mixed mode use register 0). LUT sprites:
 * colour of dot 1 (like Vdp2_SpritePrioReg). Used by the SDL renderer (per sprite). */
int Vdp2_SpriteColorCalc(const RenderSprite *s, int prio)
{
    static const uint8_t ccs[8] = { 11, 11, 11, 11, 10, 11, 10, 9 }, ccb[8] = { 3, 2, 3, 2, 3, 1, 2, 3 };
    uint16_t spctl = R16(VDP2_SHADOW_ADDR), ccctl = R16(VDP2_SHADOW_ADDR + 0x0C);
    int type = spctl ? (spctl & 0xF) : 3, mixed = spctl ? (spctl >> 5) & 1 : 1;
    int spccs = (spctl >> 12) & 3, spccn = (spctl >> 8) & 7;
    unsigned cm = (s->mode >> 3) & 7;
    uint16_t c = s->colr;
    int reg = 0, msb = 0;
    if (!(ccctl & 0x40) || type > 7) return -1;
    if (cm == 1) { c = R16(VDP1_VRAM_BASE + (uint32_t)s->colr * 8u + 2u); msb = (c & 0x8000) != 0; }
    else if (cm >= 5) msb = 1;
    if (!(msb && mixed)) reg = (c >> ccs[type]) & ((1 << ccb[type]) - 1);
    int cond = spccs == 0 ? prio <= spccn : spccs == 1 ? prio == spccn : spccs == 2 ? prio >= spccn : msb;
    if (!cond) return -1;
    return (R16(VDP2_SHADOW_ADDR + 0x1E + 2u * (uint32_t)(reg >> 1)) >> (8 * (reg & 1))) & 0x1F;
}

/* ---- VDP1 characters ---- */

/* Decode a VDP1 character (CMDPMOD colour modes 0-5) from the emulated VRAM.
 * Dot 0 is transparent unless SPD (CMDPMOD bit 6) is set. */
static uint32_t cram_col(const uint32_t *lut, unsigned idx) { return lut ? lut[idx & 0x7FF] : Vdp2_CramArgb(idx); }

void Vdp1_DecodeCharPal(const RenderSprite *s, uint32_t *argb, const uint32_t *lut)
{
    const uint8_t *v = Ram_Ptr(s->src);
    int n = s->w * s->h, spd = (s->mode & 0x40) != 0;
    for (int i = 0; i < n; i++) {
        unsigned dot;
        switch ((s->mode >> 3) & 7) {
        case 0:                                     /* 4bpp colour bank */
            dot = (i & 1) ? v[i >> 1] & 15 : v[i >> 1] >> 4;
            argb[i] = (dot || spd) ? cram_col(lut, (s->colr & 0xFFF0u) | dot) : 0;
            break;
        case 1: {                                   /* 4bpp lookup table at VRAM colr*8 */
            dot = (i & 1) ? v[i >> 1] & 15 : v[i >> 1] >> 4;
            uint16_t e = R16(VDP1_VRAM_BASE + (uint32_t)s->colr * 8u + dot * 2u);
            if (!dot && !spd) argb[i] = 0;
            else if (e & 0x8000) argb[i] = Vdp_Rgb555ToArgb(e);
            else argb[i] = cram_col(lut, e);
            break; }
        case 2: dot = v[i] & 0x3F; argb[i] = (dot || spd) ? cram_col(lut, (s->colr & 0xFFC0u) | dot) : 0; break;
        case 3: dot = v[i] & 0x7F; argb[i] = (dot || spd) ? cram_col(lut, (s->colr & 0xFF80u) | dot) : 0; break;
        case 4: dot = v[i];        argb[i] = (dot || spd) ? cram_col(lut, (s->colr & 0xFF00u) | dot) : 0; break;
        default: {                                  /* RGB */
            uint16_t e = (uint16_t)(v[2 * i] << 8 | v[2 * i + 1]);
            if (!(e & 0x8000) && !(spd && e == 0)) { argb[i] = 0; break; }
            argb[i] = Vdp_Rgb555ToArgb(e);
            break; }
        }
    }
}

void Vdp1_DecodeChar(const RenderSprite *s, uint32_t *argb) { Vdp1_DecodeCharPal(s, argb, NULL); }

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

/* Layers the stage does not stream (control block +0x20 bit 0x10 << n: 0x06046E54 skips them)
 * show whatever the stage init wrote into their VDP2 name tables (Layer_FillPage / fill_at /
 * Layer_FillVram / Layer_PutTile), through the SclConfig plane: 2x2 planes A-D at the plate
 * addresses, each 1x1, 2x1 or 2x2 pages of 0x1000 bytes, the whole map wrapping. With the
 * usual 1x1 plane and four identical plates one page repeats every 512 px, e.g. the 1-2 / 8-1
 * scrolling cloud bands (NBG1); their MAP layouts are wider ([00 FF], [00 01]) but the extra
 * pages never reach the displayed name table (8-1 fills page 01 into an undisplayed plate). */
static int layer_static(int nbg) { return nbg >= 0 && nbg < 3 && ((R16(STAGE_CTRL + 0x20) >> (4 + nbg)) & 1); }

static uint32_t vram_page_addr(int pageId) { return VDP2_VRAM_BASE + (uint32_t)(pageId - VDP2_VRAM_PAGE0) * 0x1000u; }

int Vdp2_LayerPageAt(int nbg, int px, int py)
{
    if (layer_static(nbg)) {
        uint32_t c = NBG_CFG + 0x4Cu * (uint32_t)nbg;
        unsigned ps = Ram_Ptr(c)[3] & 3;                  /* plate size: 0 1x1, 1 2x1, 3 2x2 pages */
        int pw = (ps & 1) ? 2 : 1, ph = (ps & 2) ? 2 : 1;
        int mx = wrapi(px, 2 * pw), my = wrapi(py, 2 * ph);
        /* the VDP2 ignores the low bit(s) of a plane's map register when the plane is 2x1 / 2x2
         * pages: the plane starts at the plate address aligned down to its size (1-1's NBG2
         * plates A 0x25E68000, B-D 0x25E69000 with 2x1 planes all show 68000 + 69000; reading
         * 6A000 for the right half was the "city lights band" divergence) */
        uint32_t pl = R32(c + 12u + 4u * (uint32_t)((my / ph) * 2 + mx / pw)) & ~((uint32_t)(pw * ph) * 0x1000u - 1u);
        uint32_t a = pl + (uint32_t)(((my % ph) * pw + mx % pw) * 0x1000);
        return VDP2_VRAM_PAGE0 + (int)(((a - VDP2_VRAM_BASE) & (VDP2_VRAM_SIZE - 1)) >> 12);
    }
    uint32_t lay = R32(desc_of(nbg) + 0x20);
    int pw = Ram_Ptr(lay)[0], ph = Ram_Ptr(lay)[1];
    if (pw <= 0 || ph <= 0) return -1;
    uint8_t pid = Ram_Ptr(lay)[2 + wrapi(py, ph) * pw + wrapi(px, pw)];
    return pid == 0xFF ? -1 : pid;
}

uint32_t Vdp2_MapPageChecksum(int nbg, int pageId)
{
    int vram = pageId >= VDP2_VRAM_PAGE0;
    uint32_t a = vram ? vram_page_addr(pageId) : R32(desc_of(nbg) + 0x1C) + (uint32_t)pageId * 0x800u, h = 2166136261u;
    const uint8_t *p = Ram_Ptr(a);
    for (int i = 0; i < (vram ? 0x1000 : 0x800); i++) h = (h ^ p[i]) * 16777619u;
    return h;
}

/* One page = 32x32 MAP words, each a 16x16 character of 2x2 8x8 cells (TL,TR,BL,BR), 256
 * colours (8bpp). The words are what 0x06046B14 turns into VDP2 2-word pattern names:
 *   bit 15 H-flip, bit 14 V-flip, bit 13 special priority (SPR: SFPRMD 0x0015, per character),
 *   bit 12 palette bit 4 (+256 colours), bit 0 special colour calc (SCC: SFCCMD 0x0155, per
 *   character), bits 11-1 character pair: character number = base (desc+0x2C) +
 *   (word & 0xFFE) * 4, in 0x20-byte units (one 16x16 8bpp character = 256 bytes).
 * Colour = CRAM[(CRAOFA(nbg) + palette) * 256 + dot], dot 0 transparent. */
/* A VDP2 name-table page (pageId >= VDP2_VRAM_PAGE0): 32x32 2-word pattern names as written by
 * Layer_PutTile 0x06046B14 (bit 31 V-flip, 30 H-flip, 22-20 256-colour palette, 14-0 character
 * in 0x20-byte units), 16x16 characters of 2x2 8bpp cells. */
/* Per-pixel flags of the decoders below (NULL = not wanted): bit 0 special priority (SPR bit of the
 * pattern name), bit 1 special colour calculation (SCC bit), bit 2 MSB of the dot's CRAM word. */
#define PF_SP   VDP2_PF_SP
#define PF_SCC  VDP2_PF_SCC
#define PF_CMSB VDP2_PF_CMSB
static uint8_t cram_msb(unsigned idx) { return (R16(SHADOW_CRAM + 2u * (idx & 0x7FF)) & 0x8000) ? PF_CMSB : 0; }

/* Optional outputs of the decoders for the SDL renderer's page cache (render.c): px = one
 * VDP2_PIX_* word per pixel (CRAM index + pattern-name flags, 0 = transparent), blk = bit set of
 * the 2 KB VDP2 VRAM blocks holding the characters read (VDP2_VRAM_SIZE / 2048 bits). */
static void mark_blocks(uint32_t *blk, uint32_t chr, uint32_t len)
{
    if (!blk) return;
    for (uint32_t a = chr & ~0x7FFu; a < chr + len; a += 0x800u) {
        uint32_t b = (a & (VDP2_VRAM_SIZE - 1)) >> 11;
        blk[b >> 5] |= 1u << (b & 31);
    }
}
static uint16_t pix_word(unsigned idx, uint8_t f) { return (uint16_t)(VDP2_PIX_OPAQUE | (idx & 0x7FF) | (unsigned)f << 11); }

static void decode_vram_page(int nbg, uint32_t page, uint32_t *argb, uint8_t *fl, uint16_t *px, uint32_t *blk)
{
    unsigned off = craof(nbg) << 8;
    for (int ty = 0; ty < 32; ty++)
        for (int tx = 0; tx < 32; tx++) {
            uint32_t pn = R32(page + (uint32_t)(ty * 32 + tx) * 4);
            uint32_t chr = VDP2_VRAM_BASE + (((pn & 0x7FFFu) * 0x20u) & (VDP2_VRAM_SIZE - 1));
            unsigned pal = off + (((pn >> 20) & 7) << 8);
            int hf = (pn & 0x40000000u) != 0, vf = (pn & 0x80000000u) != 0;
            uint8_t f = (uint8_t)(((pn >> 29) & 1) * PF_SP | ((pn >> 28) & 1) * PF_SCC);
            mark_blocks(blk, chr, 256);
            for (int y = 0; y < 16; y++) {
                int yy = vf ? 15 - y : y;
                int o = (ty * 16 + y) * VDP2_PAGE_PX + tx * 16;
                for (int x = 0; x < 16; x++) {
                    int xx = hf ? 15 - x : x;
                    uint32_t cell = (uint32_t)((yy >> 3) * 2 + (xx >> 3));
                    uint8_t v = Ram_Ptr(chr + cell * 64 + (uint32_t)(yy & 7) * 8 + (uint32_t)(xx & 7))[0];
                    if (argb) argb[o + x] = v ? Vdp2_CramArgb(pal + v) : 0;
                    if (fl) fl[o + x] = v ? (uint8_t)(f | cram_msb(pal + v)) : 0;
                    if (px) px[o + x] = v ? pix_word(pal + v, f) : 0;
                }
            }
        }
}

static void decode_map_page(int nbg, int pageId, uint32_t *argb, uint8_t *fl, uint16_t *px, uint32_t *blk)
{
    if (pageId >= VDP2_VRAM_PAGE0) { decode_vram_page(nbg, vram_page_addr(pageId), argb, fl, px, blk); return; }
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
            uint8_t f = (uint8_t)(((pn >> 13) & 1) * PF_SP | (pn & 1) * PF_SCC);
            mark_blocks(blk, chr, 256);
            for (int y = 0; y < 16; y++) {
                int yy = vf ? 15 - y : y;
                int o = (ty * 16 + y) * VDP2_PAGE_PX + tx * 16;
                for (int x = 0; x < 16; x++) {
                    int xx = hf ? 15 - x : x;
                    uint32_t cell = (uint32_t)((yy >> 3) * 2 + (xx >> 3));
                    uint8_t v = Ram_Ptr(chr + cell * 64 + (uint32_t)(yy & 7) * 8 + (uint32_t)(xx & 7))[0];
                    if (argb) argb[o + x] = v ? Vdp2_CramArgb(pal + v) : 0;
                    if (fl) fl[o + x] = v ? (uint8_t)(f | cram_msb(pal + v)) : 0;
                    if (px) px[o + x] = v ? pix_word(pal + v, f) : 0;
                }
            }
        }
}

void Vdp2_DecodeMapPage(int nbg, int pageId, uint32_t *argb) { decode_map_page(nbg, pageId, argb, NULL, NULL, NULL); }

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

static void decode_nbg3(uint32_t *argb, uint8_t *fl, uint16_t *px, uint32_t *blk)
{
    uint32_t page = nbg3_page();
    unsigned off = craof(3) << 8;
    for (int cy = 0; cy < 64; cy++)
        for (int cx = 0; cx < 64; cx++) {
            uint32_t pn = R32(page + (uint32_t)(cy * 64 + cx) * 4);
            uint32_t chr = VDP2_VRAM_BASE + (((pn & 0x7FFFu) * 0x20u) & (VDP2_VRAM_SIZE - 1));
            unsigned pal = off + ((pn >> 16) & 0x7F) * 16;
            int hf = (pn & 0x40000000u) != 0, vf = (pn & 0x80000000u) != 0;
            uint8_t f = (uint8_t)(((pn >> 29) & 1) * PF_SP | ((pn >> 28) & 1) * PF_SCC);
            mark_blocks(blk, chr, 32);
            for (int y = 0; y < 8; y++) {
                int yy = vf ? 7 - y : y;
                int o = (cy * 8 + y) * VDP2_PAGE_PX + cx * 8;
                for (int x = 0; x < 8; x++) {
                    int xx = hf ? 7 - x : x;
                    uint8_t b = Ram_Ptr(chr + (uint32_t)yy * 4 + (uint32_t)(xx >> 1))[0];
                    unsigned v = (xx & 1) ? b & 15u : b >> 4;
                    if (argb) argb[o + x] = v ? Vdp2_CramArgb(pal + v) : 0;
                    if (fl) fl[o + x] = v ? (uint8_t)(f | cram_msb(pal + v)) : 0;
                    if (px) px[o + x] = v ? pix_word(pal + v, f) : 0;
                }
            }
        }
}

void Vdp2_DecodeNbg3(uint32_t *argb) { decode_nbg3(argb, NULL, NULL, NULL); }

void Vdp2_DecodePagePix(int nbg, int pageId, uint16_t *px, uint32_t *blk)
{
    if (nbg == 3) decode_nbg3(NULL, NULL, px, blk); else decode_map_page(nbg, pageId, NULL, NULL, px, blk);
}

/* Name table of a page and its length in bytes (MAP page: 32x32 words, VDP2 VRAM page: 32x32
 * longs, NBG3: 64x64 longs). */
uint32_t Vdp2_PageNameTable(int nbg, int pageId, uint32_t *len)
{
    if (nbg == 3) { *len = 0x4000; return nbg3_page(); }
    if (pageId >= VDP2_VRAM_PAGE0) { *len = 0x1000; return vram_page_addr(pageId); }
    *len = 0x800;
    return R32(desc_of(nbg) + 0x1C) + (uint32_t)pageId * 0x800u;
}

/* What else a decoded page depends on besides its name table, the characters and CRAM: the
 * layer's CRAM offset (CRAOFA/B) and, for MAP pages, the character base (desc +0x2C). */
uint64_t Vdp2_PageParams(int nbg, int pageId)
{
    uint64_t k = craof(nbg);
    if (nbg != 3 && pageId < VDP2_VRAM_PAGE0) k |= (uint64_t)R32(desc_of(nbg) + 0x2C) << 3;
    return k;
}

int Vdp2_SpecialPrioMode(int nbg) { return nbg >= 0 && nbg < 4 ? (R16(VDP2_SHADOW_ADDR + 0x0A) >> (2 * nbg)) & 3 : 0; }

void Vdp2_DecodePageSel(int nbg, int pageId, uint32_t *argb, unsigned mask, unsigned val)
{
    static uint8_t fl[VDP2_PAGE_PX * VDP2_PAGE_PX];
    if (nbg == 3) decode_nbg3(argb, fl, NULL, NULL); else decode_map_page(nbg, pageId, argb, fl, NULL, NULL);
    if (!mask) return;
    for (int i = 0; i < VDP2_PAGE_PX * VDP2_PAGE_PX; i++)
        if ((fl[i] & mask) != (val & mask)) argb[i] = 0;
}

void Vdp2_DecodePageSp(int nbg, int pageId, uint32_t *argb, int sp)
{
    Vdp2_DecodePageSel(nbg, pageId, argb, PF_SP, sp ? PF_SP : 0);
}

int Vdp2_LayerColorCalc(int nbg, unsigned *sel, int *add)
{
    uint16_t ccctl = R16(VDP2_SHADOW_ADDR + 0x0C);
    if (nbg < 0 || nbg > 3 || !((ccctl >> nbg) & 1)) return -1;
    int mode = (R16(VDP2_SHADOW_ADDR + 0x0E) >> (2 * nbg)) & 3;
    if (mode == 2) return -1;
    *sel = mode == 0 ? 0u : mode == 1 ? (unsigned)PF_SCC : (unsigned)PF_CMSB;
    *add = (ccctl >> 8) & 1;
    return (R16(VDP2_SHADOW_ADDR + 0x26 + 2u * (uint32_t)(nbg >> 1)) >> (8 * (nbg & 1))) & 0x1F;
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
    /* the VDP2 adds the 16.16 values before taking the integer pixel (checked against the real
     * game: 9-1's NBG1 was 1 px off with integer parts added separately) */
    int32_t fx = RS32(desc_of(nbg) + 0) - 0x04000000, fy = RS32(desc_of(nbg) + 4) - 0x04000000;
    if (scr & 2) {                                         /* LSCX */
        uint32_t base = (R32(nbg ? SH_LSTA1 : SH_LSTA0) << 1) & m;
        uint32_t size = 4u * (((scr >> 1) & 1) + ((scr >> 2) & 1) + ((scr >> 3) & 1));
        int iv = 1 << ((scr >> 4) & 3);
        for (int y = 0; y < SCREEN_H; y++)
            s->sx[y] = (int)((int32_t)((uint32_t)fx + (uint32_t)RS32(VDP2_VRAM_BASE | ((base + (uint32_t)(y / iv) * size) & m))) >> 16);
        s->lines = 1;
    }
    if (scr & 1) {                                         /* VCSEN */
        uint32_t base = (R32(SH_VCSTA) << 1) & m;
        int both = (R16(SH_SCRCTL) & 0x101) == 0x101;
        uint32_t stride = both ? 8u : 4u, off = (both && nbg == 1) ? 4u : 0u;
        for (int c = 0; c <= SCREEN_W / 8; c++)            /* dy = row offset from sy, same rule */
            s->dy[c] = ((int32_t)((uint32_t)fy + (uint32_t)RS32(VDP2_VRAM_BASE | ((base + (uint32_t)c * stride + off) & m))) >> 16) - sy;
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

/* ---- reference frame ----
 * Composed like the hardware (checked against the real game, docs/ground_truth.md):
 *  1. each NBG is rendered into its own buffer (+ per-pixel special priority / colour-calc flags);
 *  2. the sprites are drawn in VDP1 command order into a 16-bit framebuffer holding the raw
 *     sprite data (colour bank | dot, LUT entry or RGB), so a later sprite overwrites an earlier
 *     one whatever their VDP2 priorities;
 *  3. per pixel the VDP2 picks the top and second screens by priority (special priority per
 *     character, SFPRMD), applies colour calculation to the top one (CCCTL / SFCCMD / sprite
 *     condition SPCTL, ratio registers: result = top * (31 - r) / 32 + second * (r + 1) / 32)
 *     and finally the colour offset of the top screen. */

#define SH_REG(o) (VDP2_SHADOW_ADDR + (o))
static uint32_t s_lay[4][SCREEN_W * SCREEN_H];
static uint8_t  s_lfl[4][SCREEN_W * SCREEN_H];
static uint16_t s_fb1[SCREEN_W * SCREEN_H];      /* VDP1 framebuffer: raw sprite data, 0 = transparent */
static uint8_t  s_fb1sh[SCREEN_W * SCREEN_H];    /* shadow dot (VDP1 colour calc 1) over nothing: prio reg + 1 */

static int floordiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

/* NBG0-2 (whole-layer or line / cell scroll): per pixel, decoded pages cached */
static void soft_layer_nbg(int nbg, const Vdp2LineScroll *ls, uint32_t *out, uint8_t *ofl)
{
    enum { NC = 6 };
    static uint32_t *cache[NC];
    static uint8_t *cfl[NC];
    int cpid[NC], next = 0;
    for (int i = 0; i < NC; i++) {
        cpid[i] = -1;
        if (!cache[i]) cache[i] = (uint32_t *)malloc((size_t)VDP2_PAGE_PX * VDP2_PAGE_PX * sizeof(uint32_t));
        if (!cfl[i]) cfl[i] = (uint8_t *)malloc((size_t)VDP2_PAGE_PX * VDP2_PAGE_PX);
        if (!cache[i] || !cfl[i]) return;
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
                decode_map_page(nbg, pid, cache[k], cfl[k], NULL, NULL);
                cpid[k] = pid;
            }
            int o = (wy - py * VDP2_PAGE_PX) * VDP2_PAGE_PX + (wx - px * VDP2_PAGE_PX);
            out[y * SCREEN_W + x] = cache[k][o];
            ofl[y * SCREEN_W + x] = cfl[k][o];
        }
}

static void soft_layer(int nbg, uint32_t *out, uint8_t *ofl)
{
    static uint32_t page[VDP2_PAGE_PX * VDP2_PAGE_PX];
    static uint8_t pfl[VDP2_PAGE_PX * VDP2_PAGE_PX];
    memset(out, 0, sizeof(uint32_t) * SCREEN_W * SCREEN_H);
    memset(ofl, 0, SCREEN_W * SCREEN_H);
    if (!Vdp2_LayerEnabled(nbg)) return;
    if (nbg != 3) {
        static Vdp2LineScroll ls;
        Vdp2_GetLineScroll(nbg, &ls);
        soft_layer_nbg(nbg, &ls, out, ofl);
        return;
    }
    int sx, sy;
    Vdp2_LayerScroll(3, &sx, &sy);
    decode_nbg3(page, pfl, NULL, NULL);              /* one 512x512 page repeated over the plane */
    for (int y = 0; y < SCREEN_H; y++)
        for (int x = 0; x < SCREEN_W; x++) {
            int o = wrapi(sy + y, VDP2_PAGE_PX) * VDP2_PAGE_PX + wrapi(sx + x, VDP2_PAGE_PX);
            out[y * SCREEN_W + x] = page[o];
            ofl[y * SCREEN_W + x] = pfl[o];
        }
}

/* Raw VDP1 framebuffer data of a character's dots (0 = transparent dot). */
static void vdp1_raw(const RenderSprite *s, uint16_t *raw)
{
    const uint8_t *v = Ram_Ptr(s->src);
    int n = s->w * s->h, spd = (s->mode & 0x40) != 0;
    for (int i = 0; i < n; i++) {
        unsigned dot;
        switch ((s->mode >> 3) & 7) {
        case 0: dot = (i & 1) ? v[i >> 1] & 15 : v[i >> 1] >> 4; raw[i] = (dot || spd) ? (uint16_t)((s->colr & 0xFFF0u) | dot) : 0; break;
        case 1:
            dot = (i & 1) ? v[i >> 1] & 15 : v[i >> 1] >> 4;
            raw[i] = (dot || spd) ? R16(VDP1_VRAM_BASE + (uint32_t)s->colr * 8u + dot * 2u) : 0;
            break;
        case 2: dot = v[i] & 0x3F; raw[i] = (dot || spd) ? (uint16_t)((s->colr & 0xFFC0u) | dot) : 0; break;
        case 3: dot = v[i] & 0x7F; raw[i] = (dot || spd) ? (uint16_t)((s->colr & 0xFF80u) | dot) : 0; break;
        case 4: dot = v[i];        raw[i] = (dot || spd) ? (uint16_t)((s->colr & 0xFF00u) | dot) : 0; break;
        default: {
            uint16_t e = (uint16_t)(v[2 * i] << 8 | v[2 * i + 1]);
            raw[i] = ((e & 0x8000) || (spd && e == 0)) ? (uint16_t)(e | 0x8000) : 0;
            break; }
        }
    }
}

static uint16_t rgb_half(uint16_t c) { return (uint16_t)(0x8000 | ((c >> 1) & 0x3DEF)); }
static uint16_t rgb_avg(uint16_t a, uint16_t b)
{
    unsigned r = ((a & 31) + (b & 31)) >> 1, g = (((a >> 5) & 31) + ((b >> 5) & 31)) >> 1, bl = (((a >> 10) & 31) + ((b >> 10) & 31)) >> 1;
    return (uint16_t)(0x8000 | bl << 10 | g << 5 | r);
}

/* Affine mapping of the texture through vertices A (0,0), B (w-1,0), D (0,h-1) into the VDP1
 * framebuffer. VDP1 colour calculation (RGB data only): 1 shadow, 2 half luminance, 3 half
 * transparency with the framebuffer dot (drawn as is over a non-RGB / empty dot). */
static void soft_sprite(const RenderSprite *s, int type)
{
    if (s->w <= 0 || s->h <= 0 || s->w * s->h > 512 * 512) return;
    uint16_t *tex = (uint16_t *)malloc((size_t)s->w * (size_t)s->h * sizeof *tex);
    if (!tex) return;
    vdp1_raw(s, tex);
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
            uint16_t c = tex[ty * s->w + tx];
            if (!c) continue;
            int i = y * SCREEN_W + x;
            uint16_t d = s_fb1[i];
            switch (cc) {
            case 1:                                  /* shadow: darkens the dot below */
                if (d & 0x8000) s_fb1[i] = rgb_half(d);
                else if (!d) s_fb1sh[i] = 1;         /* PC: over the VDP2 screens (as before) */
                continue;
            case 2: c = rgb_half(c); break;
            case 3: if (d & 0x8000) c = rgb_avg(c, d); break;
            default: break;
            }
            s_fb1[i] = c;
            s_fb1sh[i] = 0;
        }
    (void)type;
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

static uint32_t cc_mix(uint32_t top, uint32_t sec, int r, int add)
{
    uint32_t o = 0xFF000000u;
    for (int sh = 0; sh <= 16; sh += 8) {
        int a = (int)(top >> sh & 255), b = (int)(sec >> sh & 255);
        int v = add ? a + b : (a * (31 - r) + b * (r + 1)) / 32;
        o |= (uint32_t)clamp8(v) << sh;
    }
    return o;
}

/* Sprite data type tables (SPCTL types 0-7, 16-bit dots): priority / colour-calc register fields
 * and the colour code mask. */
static const uint8_t k_prs[8] = { 14, 13, 14, 13, 13, 12, 12, 12 }, k_prb[8] = { 2, 3, 1, 2, 2, 3, 3, 3 };
static const uint8_t k_ccs[8] = { 11, 11, 11, 11, 10, 11, 10, 9 }, k_ccb[8] = { 3, 2, 3, 2, 3, 1, 2, 3 };
static const uint16_t k_ccode[8] = { 0x7FF, 0x7FF, 0x7FF, 0x7FF, 0x3FF, 0x7FF, 0x3FF, 0x1FF };

void Soft_ComposeFrame(uint32_t *fb, const RenderSprite *spr, int n, uint32_t backArgb)
{
    static uint8_t src[SCREEN_W * SCREEN_H];     /* which screen is on top at each pixel (CLOFEN bit) */
    Vdp2Prio pr;
    Vdp2_GetPriorities(&pr);
    if (!(R16(0x0603A570u) & 0x8000)) {          /* VDP2 TVMD shadow: DISP off = black screen */
        for (int i = 0; i < SCREEN_W * SCREEN_H; i++) fb[i] = 0xFF000000u;
        return;
    }
    for (int l = 0; l < 4; l++) {
        if (pr.nbg[l]) soft_layer(l, s_lay[l], s_lfl[l]);
        else { memset(s_lay[l], 0, sizeof s_lay[l]); memset(s_lfl[l], 0, sizeof s_lfl[l]); }
    }
    /* VDP1 framebuffer, command order (SPR_2 draw priority ascending, FIFO on ties) */
    memset(s_fb1, 0, sizeof s_fb1);
    memset(s_fb1sh, 0, sizeof s_fb1sh);
    int *order = (int *)malloc((size_t)(n > 0 ? n : 1) * sizeof *order);
    if (!order) return;
    for (int i = 0; i < n; i++) {
        int j = i;
        while (j > 0 && spr[order[j - 1]].prio > spr[i].prio) { order[j] = order[j - 1]; j--; }
        order[j] = i;
    }
    for (int k = 0; k < n; k++) soft_sprite(&spr[order[k]], pr.sptype);
    free(order);

    uint16_t spctl = R16(SH_REG(0x00)), ccctl = R16(SH_REG(0x0C)), sfprmd = R16(SH_REG(0x0A));
    int lcc[4], ladd[4] = { 0, 0, 0, 0 };
    unsigned lsel[4] = { 0, 0, 0, 0 };
    for (int l = 0; l < 4; l++) lcc[l] = Vdp2_LayerColorCalc(l, &lsel[l], &ladd[l]);
    int type = pr.sptype & 7, mixed = spctl ? (spctl >> 5) & 1 : 1;
    int spccs = (spctl >> 12) & 3, spccn = (spctl >> 8) & 7, ccadd = (ccctl >> 8) & 1;
    unsigned sprofs = ((R16(SH_REG(0x06)) >> 4) & 7) << 8;   /* CRAOFB: sprite colour RAM offset */
    for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
        /* candidates: 0..3 NBG, 4 sprite */
        uint32_t col[5];
        int pri[5], ok[5], top = -1, sec = -1;
        for (int l = 0; l < 4; l++) {
            ok[l] = s_lay[l][i] != 0;
            if (!ok[l]) continue;
            col[l] = s_lay[l][i];
            pri[l] = pr.nbg[l];
            if (((sfprmd >> (2 * l)) & 3) == 1) pri[l] = (pri[l] & 6) | (s_lfl[l][i] & PF_SP);
            if (!pri[l]) ok[l] = 0;
        }
        uint16_t d = s_fb1[i];
        int sreg = 0, sccr = 0;
        ok[4] = 0;
        if (d) {
            if ((d & 0x8000) && mixed) { col[4] = Vdp_Rgb555ToArgb(d); sreg = 0; sccr = 0; }
            else {
                col[4] = Vdp2_CramArgb(((unsigned)d & k_ccode[type]) + sprofs);
                sreg = (d >> k_prs[type]) & ((1 << k_prb[type]) - 1);
                sccr = (d >> k_ccs[type]) & ((1 << k_ccb[type]) - 1);
            }
            pri[4] = pr.spr[sreg];
            ok[4] = pri[4] != 0;
        }
        static const int tie[5] = { 4, 0, 1, 2, 3 };    /* equal priority: sprite > NBG0 > ... > NBG3 */
        for (int t = 0; t < 5; t++) {
            int c = tie[t];
            if (!ok[c]) continue;
            if (top < 0 || pri[c] > pri[top]) { sec = top; top = c; }
            else if (sec < 0 || pri[c] > pri[sec]) sec = c;
        }
        uint32_t topc = top < 0 ? backArgb : col[top], secc = sec < 0 ? backArgb : col[sec];
        uint32_t out = topc;
        if (top == 4) {
            int cond = spccs == 0 ? pri[4] <= spccn : spccs == 1 ? pri[4] == spccn : spccs == 2 ? pri[4] >= spccn : (d & 0x8000) != 0;
            if ((ccctl & 0x40) && cond) {
                int r = (R16(SH_REG(0x1E) + 2u * (uint32_t)(sccr >> 1)) >> (8 * (sccr & 1))) & 0x1F;
                out = cc_mix(topc, secc, r, ccadd);
            }
        } else if (top >= 0 && lcc[top] >= 0 && (!lsel[top] || (s_lfl[top][i] & lsel[top]))) {
            out = cc_mix(topc, secc, lcc[top], ladd[top]);
        }
        if (s_fb1sh[i] && top != 4) out = (0xFF000000u | ((out & 0xFEFEFEu) >> 1));
        fb[i] = out;
        src[i] = (uint8_t)(top < 0 ? 5 : top == 4 ? 6 : top);
    }
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
    f = u8_fopen(path, "wb");
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
