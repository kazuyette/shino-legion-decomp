/* DIRECTOR font (work package D3): 39 glyphs 8x16 4bpp, LZ image at 0x06043F9C, char list
 * 0x06044272, 32 colours at 0x060443AE. Glyphs: '0'-'9' 0-9, 'A'-'Z' 10-35, '.' 36,
 * ':' 37, '-' 38; ' ' draws nothing; no lowercase. */
#include "director_int.h"

/* 0x06040318 */
void Font_Init(int charBase, int bank, uint32_t vramOff)
{
    W16(D_FONT_CHAR, (uint32_t)charBase);
    W16(D_FONT_BANK, (uint32_t)bank);
    Lz_DecompressRam(0x06043F9Cu, VDP1_VRAM_BASE + 0x4740u + vramOff);   /* 0x06004EE8 */
    Char_RegisterList(charBase, vramOff, 0x06044272u);                   /* 0x06006E64 */
    Pal_Copy((uint16_t)((uint16_t)bank << 4), 32, 0x060443AEu);          /* 0x06005C30 */
}

/* 0x06040368: c is the (zero-extended 16-bit) character code. Sprite +0x0E/+0x10 are left
 * as the caller set them (dimming / colour calc). */
void Font_DrawChar(int c, int bank, int x, int y)
{
    uint16_t cc = (uint16_t)c;
    uint16_t g = cc > 57 ? (uint16_t)(cc - 55) : (uint16_t)(cc - 48);
    if (cc == '.') g = 36;
    if (cc == ':') g = 37;
    if (cc == '-') g = 38;
    if (cc == ' ') return;
    uint32_t s = SPR_WORK;
    W16(s + 6, 0); W16(s + 8, 0);
    W16(s + 2, (uint32_t)x); W16(s + 4, (uint32_t)y);
    W16(s + 0x0A, (uint32_t)(R16(D_FONT_BANK) + (uint16_t)bank));
    W16(s, (uint32_t)(R16(D_FONT_CHAR) + g));
    Sprite_Draw(s);                                  /* 0x060068C0 */
}

/* 0x060403FA: the characters are read as signed bytes (mov.b) then zero-extended to 16 bits */
void Font_DrawString(uint32_t str, int bank, int x, int y)
{
    for (; Ram_Ptr(str)[0]; str++, x += 8)
        Font_DrawChar((uint16_t)(int8_t)Ram_Ptr(str)[0], bank, x, y);
}

void Font_DrawText(const char *str, int bank, int x, int y)
{
    for (; *str; str++, x += 8) Font_DrawChar((uint16_t)(int8_t)*str, bank, x, y);
}

/* 0x0604042E: `digits` decimal digits; flags bit 0 = leading zeros are not drawn (and do
 * not advance x). Negative values draw nothing. */
void Font_DrawNumber(int32_t v, int bank, int x, int y, int digits, int flags)
{
    if (v < 0) return;
    int32_t div = 1;
    for (uint32_t n = (uint32_t)(int16_t)digits; --n != 0;) div *= 10;
    int blank = flags & 1;
    int32_t seen = 0;
    for (;;) {
        int32_t d = v / div;                         /* sdiv 0x060092BC */
        v -= d * div;
        seen |= d;
        if (!(blank && seen == 0 && div > 1)) {
            Font_DrawChar((uint16_t)(d + 48), bank, x, y);
            x += 8;
        }
        if (div == 1) break;
        div /= 10;
    }
}

/* 0x060404E8: same in hex (unsigned). Digits >= 10 are drawn as d + 65, i.e. 'K'.. for
 * 10..15 (the original adds 'A' instead of 'A' - 10; kept as is). */
void Font_DrawHex(uint32_t v, int bank, int x, int y, int digits, int flags)
{
    uint32_t div = 1;
    for (uint32_t n = (uint32_t)(int16_t)digits; --n != 0;) div <<= 4;
    int blank = flags & 1;
    uint32_t seen = 0;
    for (;;) {
        uint32_t d = v / div;                        /* udiv 0x0600A55C */
        v -= d * div;
        seen |= d;
        if (!(blank && seen == 0 && div > 1)) {
            Font_DrawChar((uint16_t)(d < 10 ? d + 48 : d + 65), bank, x, y);
            x += 8;
        }
        if (div == 1) break;
        div >>= 4;
    }
}
