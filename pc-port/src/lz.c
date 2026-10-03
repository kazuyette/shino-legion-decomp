#include "lz.h"

static uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }

size_t Lz_OutputSize(const uint8_t *src)
{
    if (src[0] != 0x80) return 0;
    return ((size_t)src[1] << 16 | (size_t)src[2] << 8 | src[3]) * 2;
}

size_t Lz_Decompress(const uint8_t *src, size_t srcLen, uint8_t *dst, size_t dstMax)
{
    size_t total = Lz_OutputSize(src), o = 0, p = 4;
    if (!total || total > dstMax) return 0;
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
                for (size_t i = 0; i < n && o < total; i++, o++) dst[o] = dst[o - dist];
            }
        }
    }
    return o;
}
