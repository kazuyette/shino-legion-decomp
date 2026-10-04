#ifndef SCREENSHOT_H
#define SCREENSHOT_H
#include <stddef.h>
#include <stdint.h>
/* PC port screenshots (F12): plain C image writers, no SDL (tests/test_config.c).
 * rgb = w*h pixels, 3 bytes each (R, G, B), top row first. */

/* 24-bit BMP. 1 = ok. */
int Screenshot_WriteBmp(const char *path, const uint8_t *rgb, int w, int h);
/* PNG, truecolour 8-bit, zlib "stored" blocks (no compression library needed). 1 = ok. */
int Screenshot_WritePng(const char *path, const uint8_t *rgb, int w, int h);
/* Creates <dir>screenshots/ (dir = "" or ends with a separator) and saves
 * shinobi_YYYYMMDD_HHMMSS[_n].png|.bmp in it. The path used goes to out (may be NULL).
 * 1 = ok. */
int Screenshot_Save(const char *dir, const uint8_t *rgb, int w, int h, int png,
                    char *out, size_t outSize);
#endif
