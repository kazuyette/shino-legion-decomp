#include "resource.h"
#include <stdio.h>
#include <string.h>

void Res_CloseIfOpen(void)
{
    /* TODO: port FUN_060048d8 once the resource-handle struct is known. */
}

int Load_DirectorPPB(const char *disc_root, void *out_buffer, unsigned int buffer_size)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/DIRECTOR.PPB", disc_root);

    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "[res] could not open %s\n", path);
        return -1;
    }

    size_t n = fread(out_buffer, 1, buffer_size, f);
    fclose(f);
    printf("[res] loaded DIRECTOR.PPB (%zu bytes)\n", n);
    return (int)n;
}
