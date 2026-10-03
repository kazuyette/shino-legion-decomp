#ifndef LZ_H
#define LZ_H
#include <stdint.h>
#include <stddef.h>
/* Shinobi Legions word-LZ (A.BIN 0x060091C8 decompresses straight to VRAM).
 * u32 BE header: 0x80 | output size in 16-bit words; then [u16 flag (MSB
 * first)] + 16 items; bit 1 = literal word, bit 0 = ref (dist = w>>4 words,
 * len = (w&15)+3 words). Returns bytes written, 0 on error. */
size_t Lz_Decompress(const uint8_t *src, size_t srcLen, uint8_t *dst, size_t dstMax);
/* Output size in bytes announced by the header (0 if not compressed). */
size_t Lz_OutputSize(const uint8_t *src);
#endif
