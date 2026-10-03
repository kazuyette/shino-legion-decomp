#include "ram.h"
#include <stdio.h>

uint8_t g_hwram[HWRAM_SIZE];

size_t Ram_LoadImage(const char *path, uint32_t addr)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    size_t max = HWRAM_BASE + HWRAM_SIZE - addr;
    size_t n = fread(Ram_Ptr(addr), 1, max, f);
    fclose(f);
    return n;
}
