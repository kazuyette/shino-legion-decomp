/* PC port screenshots, see screenshot.h. */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L          /* mkdir with -std=c11 */
#endif
#include "screenshot.h"
#include "u8path.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <direct.h>
#define MAKE_DIR(p) u8_mkdir(p)
#else
#include <sys/stat.h>
#include <sys/types.h>
#define MAKE_DIR(p) u8_mkdir(p)
#endif

static void put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, v); put16(p + 2, v >> 16); }
static void be32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }

int Screenshot_WriteBmp(const char *path, const uint8_t *rgb, int w, int h)
{
    if (w <= 0 || h <= 0) return 0;
    uint32_t stride = ((uint32_t)w * 3u + 3u) & ~3u, img = stride * (uint32_t)h;
    uint8_t hd[54] = { 'B', 'M' };
    put32(hd + 2, 54 + img);
    put32(hd + 10, 54);
    put32(hd + 14, 40);
    put32(hd + 18, (uint32_t)w);
    put32(hd + 22, (uint32_t)h);          /* positive: bottom-up rows */
    put16(hd + 26, 1);
    put16(hd + 28, 24);
    put32(hd + 34, img);
    put32(hd + 38, 2835);                  /* 72 dpi */
    put32(hd + 42, 2835);
    FILE *f = u8_fopen(path, "wb");
    if (!f) return 0;
    uint8_t *row = (uint8_t *)calloc(stride, 1);
    int ok = row && fwrite(hd, 1, 54, f) == 54;
    for (int y = h - 1; ok && y >= 0; y--) {
        const uint8_t *s = rgb + (size_t)y * (size_t)w * 3;
        for (int x = 0; x < w; x++) { row[x * 3] = s[x * 3 + 2]; row[x * 3 + 1] = s[x * 3 + 1]; row[x * 3 + 2] = s[x * 3]; }
        ok = fwrite(row, 1, stride, f) == stride;
    }
    free(row);
    if (fclose(f) != 0) ok = 0;
    return ok;
}

static uint32_t s_crcTab[256];
static uint32_t crc32_update(uint32_t c, const uint8_t *p, size_t n)
{
    if (!s_crcTab[1])
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t v = i;
            for (int k = 0; k < 8; k++) v = v & 1 ? 0xEDB88320u ^ (v >> 1) : v >> 1;
            s_crcTab[i] = v;
        }
    for (size_t i = 0; i < n; i++) c = s_crcTab[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c;
}

static int chunk(FILE *f, const char *type, const uint8_t *data, uint32_t len)
{
    uint8_t b[8];
    be32(b, len);
    memcpy(b + 4, type, 4);
    uint32_t crc = crc32_update(0xFFFFFFFFu, b + 4, 4);
    crc = crc32_update(crc, data, len) ^ 0xFFFFFFFFu;
    uint8_t c[4];
    be32(c, crc);
    return fwrite(b, 1, 8, f) == 8 && (len == 0 || fwrite(data, 1, len, f) == len) && fwrite(c, 1, 4, f) == 4;
}

int Screenshot_WritePng(const char *path, const uint8_t *rgb, int w, int h)
{
    if (w <= 0 || h <= 0) return 0;
    size_t rowLen = (size_t)w * 3 + 1, raw = rowLen * (size_t)h;     /* filter byte 0 + pixels */
    size_t nblk = (raw + 65534) / 65535;
    size_t zlen = 2 + raw + nblk * 5 + 4;
    uint8_t *z = (uint8_t *)malloc(zlen), *src = (uint8_t *)malloc(raw);
    if (!z || !src) { free(z); free(src); return 0; }
    for (int y = 0; y < h; y++) {
        src[(size_t)y * rowLen] = 0;
        memcpy(src + (size_t)y * rowLen + 1, rgb + (size_t)y * (size_t)w * 3, (size_t)w * 3);
    }
    uint32_t a = 1, b = 0;                 /* adler-32 */
    for (size_t i = 0; i < raw; i++) { a = (a + src[i]) % 65521u; b = (b + a) % 65521u; }
    size_t o = 0, left = raw, pos = 0;
    z[o++] = 0x78; z[o++] = 0x01;          /* zlib header, no compression */
    while (left) {
        uint32_t n = left > 65535 ? 65535 : (uint32_t)left;
        left -= n;
        z[o++] = left ? 0 : 1;             /* BFINAL, BTYPE 00 = stored */
        put16(z + o, n); put16(z + o + 2, ~n & 0xFFFF); o += 4;
        memcpy(z + o, src + pos, n); o += n; pos += n;
    }
    be32(z + o, (b << 16) | a); o += 4;
    uint8_t ihdr[13];
    be32(ihdr, (uint32_t)w); be32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = ihdr[11] = ihdr[12] = 0;   /* 8-bit RGB */
    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    FILE *f = u8_fopen(path, "wb");
    int ok = f != NULL;
    if (ok) ok = fwrite(sig, 1, 8, f) == 8 && chunk(f, "IHDR", ihdr, 13) && chunk(f, "IDAT", z, (uint32_t)o) && chunk(f, "IEND", NULL, 0);
    if (f && fclose(f) != 0) ok = 0;
    free(z); free(src);
    return ok;
}

int Screenshot_Save(const char *dir, const uint8_t *rgb, int w, int h, int png, char *out, size_t outSize)
{
    char folder[1100], path[1200];
    snprintf(folder, sizeof folder, "%sscreenshots", dir ? dir : "");
    MAKE_DIR(folder);                      /* fails harmlessly if it exists */
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    char stamp[32] = "shot";
    if (tm) strftime(stamp, sizeof stamp, "%Y%m%d_%H%M%S", tm);
    const char *ext = png ? "png" : "bmp";
    for (int n = 1; n < 1000; n++) {
        if (n == 1) snprintf(path, sizeof path, "%s/shinobi_%s.%s", folder, stamp, ext);
        else snprintf(path, sizeof path, "%s/shinobi_%s_%d.%s", folder, stamp, n, ext);
        FILE *f = u8_fopen(path, "rb");
        if (!f) break;
        fclose(f);
    }
    int ok = png ? Screenshot_WritePng(path, rgb, w, h) : Screenshot_WriteBmp(path, rgb, w, h);
    if (out && outSize) snprintf(out, outSize, "%s", path);
    return ok;
}
