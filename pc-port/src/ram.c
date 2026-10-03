#include "ram.h"
#include <stdio.h>

uint8_t g_hwram[HWRAM_SIZE];
uint8_t g_lwram[LWRAM_SIZE];

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
