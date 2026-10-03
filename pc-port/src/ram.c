#include "ram.h"
#include <stdio.h>
#include <stdlib.h>
#include "lz.h"

uint8_t g_hwram[HWRAM_SIZE];
uint8_t g_lwram[LWRAM_SIZE];
uint8_t g_vdp1vram[VDP1_VRAM_SIZE];
uint8_t g_vdp2vram[VDP2_VRAM_SIZE];

size_t Ram_LoadImage(const char *path, uint32_t addr)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    size_t max = ((addr & 0x0FF00000u) == LWRAM_BASE) ? LWRAM_BASE + LWRAM_SIZE - addr
                                                       : HWRAM_BASE + HWRAM_SIZE - addr;
    size_t n = fread(Ram_Ptr(addr), 1, max, f);
    fclose(f);
    return n;
}

size_t Ram_LoadCompressed(const char *path, uint32_t addr, size_t maxOut)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = (uint8_t *)malloc((size_t)len);
    size_t n = buf ? fread(buf, 1, (size_t)len, f) : 0;
    fclose(f);
    size_t out = n ? Lz_Decompress(buf, n, Ram_Ptr(addr), maxOut) : 0;
    free(buf);
    return out;
}
