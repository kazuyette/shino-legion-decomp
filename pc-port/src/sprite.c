/* Sprite submission, ported from A.BIN / SHINOBI. The final VDP1 command is
 * replaced by a PC render queue (render.c). */
#include "sprite.h"
#include "ram.h"
#include "render.h"
#include "player.h"
#include "lz.h"
#include "fixmath.h"

#define SPR16(spr, f) RS16((spr) + 2u * (f))

/* A.BIN 0x06005C30: copy n RGB555 colours into the shadow CRAM. */
void Pal_Copy(int dst, int n, uint32_t src)
{
    for (int i = 0; i < n; i++) W16(SHADOW_CRAM + 2u * (uint32_t)((dst + i) & 0xFFFF), R16(src + 2u * (uint32_t)i));
    W16(0x060250A8u, 1);
}

/* A.BIN 0x06006E64: register a list of characters packed from VRAM offset `off`.
 * list: u16 count_hi, u16 count_lo, then count x { u16 (unused: offset/32), u16 size/32,
 * s16 colour mode, u8 width/8, u8 height }. Char ids base, base+1, ... (u16). */
void Char_RegisterList(int base, uint32_t off, uint32_t list)
{
    uint32_t n = (uint32_t)R16(list) << 16 | R16(list + 2);
    uint32_t e = list + 4;
    for (uint32_t i = 0; i < n; i++, e += 8) {
        Char_Register((int)(uint16_t)(base + (int)i), Ram_Ptr(e + 6)[0] * 8, Ram_Ptr(e + 7)[0], RS16(e + 4), off);
        off += (uint32_t)R16(e + 2) * 32u;
    }
}

/* A.BIN 0x06004EE8: decompress a word-LZ block that is in emulated memory (RAM or VRAM)
 * to another emulated address. Returns the bytes written (0 on a bad block). */
size_t Lz_DecompressRam(uint32_t src, uint32_t dst)
{
    uint32_t srcEnd = ((src & 0x0FF00000u) == LWRAM_BASE) ? LWRAM_BASE + LWRAM_SIZE : HWRAM_BASE + HWRAM_SIZE;
    uint32_t dstEnd = ((dst & 0xFFF80000u) == VDP1_VRAM_BASE) ? VDP1_VRAM_BASE + VDP1_VRAM_SIZE
                    : ((dst & 0xFFF80000u) == VDP2_VRAM_BASE) ? VDP2_VRAM_BASE + VDP2_VRAM_SIZE
                    : ((dst & 0x0FF00000u) == LWRAM_BASE) ? LWRAM_BASE + LWRAM_SIZE : HWRAM_BASE + HWRAM_SIZE;
    size_t n = Lz_Decompress(Ram_Ptr(src), srcEnd - src, Ram_Ptr(dst), dstEnd - dst);
    if ((dst & 0xFFF80000u) == VDP1_VRAM_BASE && n) Render_InvalidateVram(dst, n);
    return n;
}

/* 0x06006DE4: `off` = VDP1 VRAM offset relative to the character area 0x25C04740. */
void Char_Register(int id, int w, int h, int mode, uint32_t off)
{
    uint32_t e = CHAR_TABLE + (uint32_t)(int16_t)id * 10;
    W16(e + 0, (off + 0x4740u) >> 3);            /* CMDSRCA (VRAM address / 8) */
    W16(e + 2, ((uint32_t)(w << 5) & 0xFF00) + (uint32_t)(h & 0xFFFF));
    W16(e + 4, (uint32_t)mode);
    int sz = (uint16_t)h * (uint16_t)w;
    if ((mode & 0xFFFF) == 0x28) sz <<= 1;       /* RGB: 2 bytes per dot */
    if ((mode & 0xFFFF) <= 8) sz >>= 1;          /* 4bpp: 2 dots per byte */
    W16(e + 6, 0);
    W16(e + 8, (uint32_t)((sz + 0x1F) >> 5));
}

/* 0x0602905C (SBL SPR_2DistSpr): writes a 32-byte VDP1 command at *0x0603A550 and, when
 * the draw-priority sort is on (*0x0603A4E4, set every frame by Spr2_OpenCommand
 * 0x060288A8(1)), links it into the per-priority FIFO lists (0x06031588); the close routine
 * 0x060315F8 chains the lists from priority 0 up, so higher `prty` = drawn later = on top.
 * PC: the command is a RenderSprite; Render_DrawFrame does the (stable) priority sort. */
void Spr2_DistSpr(int prty, uint16_t ctrl, uint16_t pmod, uint16_t colr, uint16_t chr,
                  const int16_t xy[8], uint16_t gour)
{
    (void)gour;                                     /* 0xFFFF -> CMDGRDA 0 (no gouraud) */
    uint32_t e = CHAR_TABLE + (uint32_t)chr * 10;   /* *0x0603A4D0 = 0x0601C508 */
    uint16_t cmode = R16(e + 4);
    pmod = (uint16_t)((pmod & 0xFFC7) | cmode);
    if (colr == 0xFFFF) colr = R16(e + 6);          /* default colour of the character */
    if ((pmod & 0x38) == 0x08) {                    /* 4bpp lookup table: LUT number -> address / 8 */
        uint16_t lut = R16(SPR2_LUT_BASE);
        if (!lut) lut = SPR2_LUT_BASE_DEFAULT;      /* PC: Spr2_Initial 0x06028368 not ported */
        colr = (uint16_t)(lut + colr * 4u);
    }
    RenderSprite rs;
    rs.src = VDP1_VRAM_BASE + ((uint32_t)R16(e + 0) << 3);   /* CMDSRCA */
    rs.mode = (uint16_t)(pmod & ~0x80u);            /* CMDPMOD (ECD dropped: end codes not emulated) */
    rs.colr = colr;                                 /* CMDCOLR */
    rs.w = (R16(e + 2) >> 5) & 0x7F8;               /* CMDSIZE */
    rs.h = R16(e + 2) & 0xFF;
    rs.flipH = (ctrl & 0x10) != 0;                  /* CMDCTRL Dir */
    rs.flipV = (ctrl & 0x20) != 0;
    for (int i = 0; i < 4; i++) { rs.qx[i] = xy[2 * i]; rs.qy[i] = xy[2 * i + 1]; }
    rs.x = rs.qx[0]; rs.y = rs.qy[0];
    if (prty < 0) prty = 0;                         /* 0x06031588 clamps to max-1 (max 0x200) */
    if (prty > 0x1FF) prty = 0x1FF;
    rs.prio = prty;
    Render_QueueSprite(&rs);
}

static int16_t hi16(int32_t v) { return (int16_t)((uint32_t)v >> 16); }
static int32_t mul32(int32_t a, int32_t b) { return (int32_t)((uint32_t)a * (uint32_t)b); }   /* mul.l */

/* 0x060068C0 */
void Sprite_Draw(uint32_t spr)
{
    uint16_t fl = R16(spr + 0x10);
    uint16_t dir = 0;                               /* CMDCTRL Dir bits */
    int16_t v[8];                                   /* vertices A, B, C, D */
    int16_t chr = SPR16(spr, 0);
    uint32_t e = CHAR_TABLE + (uint32_t)(int32_t)(int16_t)(chr * 10);
    if (fl & 0x0004) {                              /* distorted: the caller gave the 4 vertices */
        for (int i = 0; i < 8; i++) v[i] = RS16(spr + 0x24 + 2u * (uint32_t)i);
    } else {
        int32_t sclX = RS32(spr + 0x1C), sclY = RS32(spr + 0x20);
        int32_t cx = (int32_t)((uint32_t)(int32_t)SPR16(spr, 1) << 16);
        int32_t cy = (int32_t)((uint32_t)(int32_t)SPR16(spr, 2) << 16);
        int32_t zoom = RS32(SPR_ZOOM);
        if (zoom == 0) zoom = 0x10000;              /* 0x0604455A (camera.c Zoom_Update) keeps it 1.0 in normal play */
        if (zoom != 0x10000 && !(fl & 0x0100)) {    /* global zoom around (*0x060250E4, *0x060250E8) */
            if (!(fl & 0x1000)) sclX = 0x10000;
            if (!(fl & 0x0800)) sclY = 0x10000;
            fl |= 0x1800;
            sclX = FixMul(zoom, sclX);
            sclY = FixMul(zoom, sclY);
            int32_t zx = RS32(SPR_ZOOM_X), zy = RS32(SPR_ZOOM_Y);
            cx = zx + FixMul(cx - zx, zoom);
            cy = zy + FixMul(cy - zy, zoom);
        }
        int32_t cw = (R16(e + 2) >> 5) & 0xFFF8, ch = R16(e + 2) & 0xFF;
        int32_t ox, oy, w, h, x0, y0;
        if ((fl & 0x1000) == 0x1000) { ox = mul32(SPR16(spr, 3), sclX); w = mul32(cw, sclX); }
        else { ox = (int32_t)((uint32_t)(int32_t)SPR16(spr, 3) << 16); w = cw << 16; }
        if ((fl & 0x0800) == 0x0800) { oy = mul32(SPR16(spr, 4), sclY); h = mul32(ch, sclY); }
        else { oy = (int32_t)((uint32_t)(int32_t)SPR16(spr, 4) << 16); h = ch << 16; }
        if ((fl & 0x8000) == 0x8000) { dir = 0x10; x0 = cx - ox - w; } else x0 = ox + cx;
        if ((fl & 0x4000) == 0x4000) { y0 = cy - oy - h; dir |= 0x20; } else y0 = oy + cy;
        int32_t x1 = x0 + w - 0x10000, y1 = y0 + h - 0x10000;
        int32_t p[8] = { x0, y0, x1, y0, x1, y1, x0, y1 };
        if ((fl & 0x0400) == 0x0400) {              /* rotate the 4 corners around (cx, cy) */
            int32_t s = Sin8(SPR16(spr, 10)), c = Cos8(SPR16(spr, 10));
            for (int i = 0; i < 4; i++) {
                int32_t dx = p[2 * i] - cx, dy = p[2 * i + 1] - cy;
                p[2 * i]     = FixMul(dx, c) + FixMul(dy, s) + cx;
                p[2 * i + 1] = FixMul(dy, c) - FixMul(dx, s) + cy;
            }
        }
        for (int i = 0; i < 8; i++) v[i] = hi16(p[i]);
    }
    /* screen cull: visible iff minX < 320 && maxX >= 0 && minY < 240 && maxY >= 0 */
    int16_t mnx = v[0], mxx = v[0], mny = v[1], mxy = v[1];
    for (int i = 1; i < 4; i++) {
        if (v[2 * i] < mnx) mnx = v[2 * i];
        if (v[2 * i] > mxx) mxx = v[2 * i];
        if (v[2 * i + 1] < mny) mny = v[2 * i + 1];
        if (v[2 * i + 1] > mxy) mxy = v[2 * i + 1];
    }
    if ((((uint16_t)(mnx - 320)) & 0x8000) == ((uint16_t)mxx & 0x8000)) return;
    if ((((uint16_t)(mny - 240)) & 0x8000) == ((uint16_t)mxy & 0x8000)) return;
    if (fl & 1) dir ^= 0x10;
    if (fl & 2) dir ^= 0x20;
    /* CMDCOLR: VDP2 sprite type 3 -> bit 15 MSB, 14-13 priority register, 12-11 colour calc */
    uint16_t colr = (fl & 0x2000) ? R16(spr + 0x12)
                  : (uint16_t)((R16(spr + 0x0E) & 0x1F) << 11 | ((R16(spr + 0x0C) + 1u) & 3) << 13 |
                               (R16(spr + 0x0A) & 0xFFF) << 4);
    int32_t z = ((fl & 0x0200) ? RS32(spr + 0x18) : 0) + 0x100;
    if (z < 0) z = 0;
    if (z >= 0x200) z = 0x1FF;
    Spr2_DistSpr(z, dir, (uint16_t)(R16(e + 4) | 0x80), colr, (uint16_t)chr, v, 0xFFFF);
}

/* 0x0602AFD4 (SCL_SetPriority). The original also flags the registers dirty (0x0603B4AC via
 * 0x06009254) for the VBlank flush; the PC renderer reads the shadow directly. */
#define VDP2_PRISA 0x0603B47Cu
#define VDP2_PRINA 0x0603B484u
void Scl_SetPriority(unsigned mask, unsigned prio)
{
    prio &= 0xFF;
    for (int n = 0; n < 8; n++)                      /* sprite registers S0..S7: PRISA..PRISD */
        if (mask & (0x100u << n)) {
            uint32_t a = VDP2_PRISA + 2u * (uint32_t)(n >> 1);
            W16(a, (n & 1) ? (R16(a) & 0xF0FFu) | prio << 8 : (R16(a) & 0xFFF0u) | prio);
        }
    if (mask & 4)    W16(VDP2_PRINA, (R16(VDP2_PRINA) & 0xFF00u) | prio);           /* NBG0 */
    if (mask & 2)    W16(VDP2_PRINA, (R16(VDP2_PRINA) & 0xFF00u) | prio);           /* RBG1 = NBG0 */
    if (mask & 8)    W16(VDP2_PRINA, (R16(VDP2_PRINA) & 0x00FFu) | prio << 8);      /* NBG1 */
    if (mask & 0x80) W16(VDP2_PRINA, (R16(VDP2_PRINA) & 0x00FFu) | prio << 8);
    if (mask & 0x10) W16(VDP2_PRINA + 2, (R16(VDP2_PRINA + 2) & 0xFF00u) | prio);   /* NBG2 */
    if (mask & 0x20) W16(VDP2_PRINA + 2, (R16(VDP2_PRINA + 2) & 0x00FFu) | prio << 8); /* NBG3 */
    if (mask & 1)    W16(VDP2_PRINA + 4, prio);                                      /* PRIR */
}

#define SHOU_FRAMES  0x060CB020u   /* {u32 offset in SHOU.CHR, s16 w, s16 h} */
#define SHOU_BASE    0x00200000u   /* SHOU.CHR loaded in LWRAM */
#define SLOT_OFFS    0x060EA2ACu   /* VRAM offset of slot 0/1 (u32) */
#define SLOT_CHARS   0x060EA422u   /* char currently in slot 0/1 */
#define SLOT_CUR     0x060EA420u

/* 0x06044A78: the two 128x128 8bpp slots Sho's frames are streamed into. */
void Player_InitChars(void)
{
    W32(SLOT_OFFS, 0);
    Char_Register(1, 0x80, 0x80, 0x20, R32(SLOT_OFFS));
    W32(SLOT_OFFS + 4, 0x4000);
    Char_Register(2, 0x80, 0x80, 0x20, R32(SLOT_OFFS + 4));
    W16(SLOT_CHARS, 0xFFFF); W16(SLOT_CHARS + 2, 0xFFFF);
    W16(0x060EA426u, 0xFFFF);
    Player_UpdatePalette();
}

void Player_SubmitSprite(uint32_t spr)
{
    uint16_t c = R16(spr);
    unsigned slot = 0xFFFF;
    if (R16(SLOT_CHARS) == c) slot = 0;
    if (R16(SLOT_CHARS + 2) == c) slot = 1;
    uint32_t f = SHOU_FRAMES + (uint32_t)c * 8;
    if (slot == 0xFFFF) {                        /* not resident: decompress into the other slot */
        W16(SLOT_CUR, (R16(SLOT_CUR) + 1) & 1);
        uint32_t dst = VDP1_VRAM_BASE + 0x4740u + R32(SLOT_OFFS + 4u * R16(SLOT_CUR));
        uint32_t src = SHOU_BASE + R32(f);
        size_t n = Lz_Decompress(Ram_Ptr(src), 0x00100000u - (src & 0xFFFFFu), Ram_Ptr(dst), 0x4000);
        Render_InvalidateVram(dst, n ? n : 0x4000);
        W16(SLOT_CHARS + 2u * R16(SLOT_CUR), c);
    } else {
        W16(SLOT_CUR, slot);
    }
    uint16_t cur = R16(SLOT_CUR);
    Char_Register(cur + 1, RS16(f + 4), RS16(f + 6), 0x20, R32(SLOT_OFFS + 4u * cur));
    uint32_t hz = R32(0x060EA41Cu);              /* post-hit invulnerability: blink */
    if (R16(hz)) {
        if (!(R16(0x0602529Eu) & 0x8000)) W16(hz, R16(hz) - 1);
        if (R16(0x060143EAu) & 1) return;
    }
    if (R16(0x060EA2B8u) & 0x80) return;         /* flags4 & 0x80: hidden */
    W16(spr, cur + 1u);
    Sprite_Draw(spr);
}

void Pal_SetFaded(int dst, int n, uint32_t src, int dr, int dg, int db)
{
    if (dr >= 0x100 && dr <= 0x1FF) dr -= 0x200;      /* 9-bit signed offsets */
    if (dg >= 0x100 && dg <= 0x1FF) dg -= 0x200;
    if (db >= 0x100 && db <= 0x1FF) db -= 0x200;
    uint32_t out = SHADOW_CRAM + (uint32_t)(dst & 0xFFFF) * 2;
    for (; n; n--, src += 2, out += 2) {
        uint16_t c = R16(src);
        int r = (int)(((c & 0x1F) * 8 + dr) >> 3);
        int g = (int)((((c >> 5) & 0x1F) * 8 + dg) >> 3);
        int b = (int)((((c >> 10) & 0x1F) * 8 + db) >> 3);
        r = r > 31 ? 31 : r < 0 ? 0 : r;
        g = g > 31 ? 31 : g < 0 ? 0 : g;
        b = b > 31 ? 31 : b < 0 ? 0 : b;
        W16(out, (uint32_t)(b << 10 | g << 5 | r));
    }
    W16(0x060250A8u, 1);                          /* CRAM dirty */
}

void Player_UpdatePalette(void)
{
    uint32_t pal[2] = { R32(0x060CA6F8u), R32(0x060CA6FCu) };
    int alt = 0;
    uint16_t mf = R16(0x060252BAu);
    if ((mf & 0xF0) == 0x40 && (mf & 0x4000) && !(R16(0x060143EAu) & 2)) alt = 1;
    if (alt != RS16(0x060EA426u)) {
        Pal_SetFaded(0x400, 0x100, pal[alt],
                     RS16(0x060EA618u + 0x18), RS16(0x060EA618u + 0x1A), RS16(0x060EA618u + 0x1C));
        W16(0x060EA426u, (uint32_t)alt);
    }
}
