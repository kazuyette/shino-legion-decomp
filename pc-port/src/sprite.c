/* Sprite submission, ported from A.BIN / SHINOBI. The final VDP1 command is
 * replaced by a PC render queue (render.c). */
#include "sprite.h"
#include "ram.h"
#include "render.h"
#include "player.h"
#include "lz.h"

#define SPR16(spr, f) RS16((spr) + 2u * (f))

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

void Sprite_Draw(uint32_t spr)
{
    uint16_t fl = R16(spr + 0x10);
    int16_t chr = SPR16(spr, 0);
    if (fl & 0x0004) return;                     /* pre-transformed quad path: not ported yet */
    uint32_t e = CHAR_TABLE + (uint32_t)chr * 10;
    int w = (R16(e + 2) >> 5) & 0x7F8, h = R16(e + 2) & 0xFF;
    int sx = SPR16(spr, 1), sy = SPR16(spr, 2), ox = SPR16(spr, 3), oy = SPR16(spr, 4);
    int flipH = 0, flipV = 0, x0, y0;
    if (fl & 0x8000) { flipH = 1; x0 = sx - ox - w; } else x0 = sx + ox;
    if (fl & 0x4000) { flipV = 1; y0 = sy - oy - h; } else y0 = sy + oy;
    if (fl & 1) flipH ^= 1;
    if (fl & 2) flipV ^= 1;
    /* colour: CMDCOLR from the sprite struct (bank in CRAM) */
    uint16_t colr = (fl & 0x2000) ? R16(spr + 0x12)
                  : (uint16_t)((R16(spr + 0x0E) & 0x1F) << 11 | ((R16(spr + 0x0C) + 1u) & 3) << 13 |
                               (R16(spr + 0x0A) & 0xFFF) << 4);
    RenderSprite rs;
    rs.src = VDP1_VRAM_BASE + ((uint32_t)R16(e + 0) << 3);
    rs.mode = R16(e + 4);
    rs.colr = colr;
    rs.w = w; rs.h = h; rs.x = x0; rs.y = y0;
    rs.flipH = flipH; rs.flipV = flipV;
    rs.rotate = (fl & 0x0400) != 0;
    rs.angle = SPR16(spr, 10);
    rs.cx = sx; rs.cy = sy;
    Render_QueueSprite(&rs);
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
