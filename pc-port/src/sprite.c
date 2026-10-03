/* Sprite submission, ported from A.BIN / SHINOBI. The final VDP1 command is
 * replaced by a PC render queue (render.c). */
#include "sprite.h"
#include "ram.h"
#include "render.h"
#include "player.h"

#define SPR16(spr, f) RS16((spr) + 2u * (f))

void Char_Register(int id, int w, int h, int mode, uint32_t src)
{
    uint32_t e = CHAR_TABLE + (uint32_t)id * 10;
    W16(e + 0, src >> 16);                       /* PC: source address hi */
    W16(e + 2, ((uint32_t)(w << 5) & 0x3F00) + (uint32_t)h);
    W16(e + 4, (uint32_t)mode);
    W16(e + 6, src & 0xFFFF);                    /* PC: source address lo */
    int sz = w * h;
    if (mode == 0x28) sz *= 2;
    if (mode < 9) sz >>= 1;
    W16(e + 8, (uint32_t)((sz + 0x1F) >> 5));
}

void Sprite_Draw(uint32_t spr)
{
    uint16_t fl = R16(spr + 0x10);
    int16_t chr = SPR16(spr, 0);
    if (fl & 0x0004) return;                     /* pre-transformed quad path: not used by the player */
    uint32_t e = CHAR_TABLE + (uint32_t)chr * 10;
    int w = (R16(e + 2) >> 5) & 0xFFF8, h = R16(e + 2) & 0xFF;
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
    rs.src = (uint32_t)R16(e + 0) << 16 | R16(e + 6);
    rs.w = w; rs.h = h; rs.x = x0; rs.y = y0;
    rs.flipH = flipH; rs.flipV = flipV;
    rs.palette = SHADOW_CRAM + 2u * (colr & 0x0700);
    rs.rotate = (fl & 0x0400) != 0;
    rs.angle = SPR16(spr, 10);
    rs.cx = sx; rs.cy = sy;
    Render_QueueSprite(&rs);
}

#define SHOU_FRAMES  0x060CB020u   /* {u32 offset in SHOU.CHR, s16 w, s16 h} */
#define SHOU_BASE    0x00200000u   /* SHOU.CHR loaded in LWRAM */
#define SLOT_CHARS   0x060EA422u   /* char currently in slot 0/1 */
#define SLOT_CUR     0x060EA420u

void Player_SubmitSprite(uint32_t spr)
{
    uint16_t c = R16(spr);
    unsigned slot = 0xFFFF;
    if (R16(SLOT_CHARS) == c) slot = 0;
    if (R16(SLOT_CHARS + 2) == c) slot = 1;
    if (slot == 0xFFFF) {                        /* not resident: upload to the other slot */
        W16(SLOT_CUR, (R16(SLOT_CUR) + 1) & 1);
        W16(SLOT_CHARS + 2u * R16(SLOT_CUR), c);
    } else {
        W16(SLOT_CUR, slot);
    }
    uint16_t cur = R16(SLOT_CUR);
    uint32_t f = SHOU_FRAMES + (uint32_t)c * 8;
    Char_Register(cur + 1, RS16(f + 4), RS16(f + 6), 0x20, SHOU_BASE + R32(f));
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
