#include "lz.h"

static uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }

/* The header holds (words - 1): A.BIN 0x06004EE8 loops while count < header + 1
 * (`add #1,r3` at 0x06004F0E), and a final back-reference is written in full. Every
 * game file decodes to exactly header + 1 words (SHINOBI.PPB's last word, 0xA254, is
 * the low half of type 0xC's anim 0xD script pointer 0x060EA2A8 -> 0x060EA254). */
size_t Lz_OutputSize(const uint8_t *src)
{
    if (src[0] != 0x80) return 0;
    return (((size_t)src[1] << 16 | (size_t)src[2] << 8 | src[3]) + 1) * 2;
}

size_t Lz_Decompress(const uint8_t *src, size_t srcLen, uint8_t *dst, size_t dstMax)
{
    size_t total = Lz_OutputSize(src), o = 0, p = 4;
    if (!total) return 0;
    if (total > dstMax) total = dstMax & ~(size_t)1;               /* PC: never past the buffer */
    while (o < total) {
        if (p + 2 > srcLen) return 0;
        uint16_t f = be16(src + p); p += 2;
        for (int b = 15; b >= 0 && o < total; b--) {
            if (p + 2 > srcLen) return 0;
            uint16_t w = be16(src + p); p += 2;
            if (f >> b & 1) {
                dst[o] = (uint8_t)(w >> 8); dst[o + 1] = (uint8_t)w; o += 2;
            } else {
                size_t dist = (size_t)(w >> 4) * 2, n = ((size_t)(w & 15) + 3) * 2;
                if (!dist || dist > o) return 0;
                for (size_t i = 0; i < n && o < dstMax; i++, o++) dst[o] = dst[o - dist];   /* overshoot kept */
            }
        }
    }
    return o;
}
