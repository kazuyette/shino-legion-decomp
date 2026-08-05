#include "resource.h"
#include <stdio.h>
#include <stdlib.h>
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

/* --- Res_LoadFileByName: small fixed-size cache, round-robin eviction --- */

#define RES_CACHE_SLOTS 8
#define RES_MAX_NAME 64

typedef struct {
    char name[RES_MAX_NAME];
    unsigned char *data;
    size_t size;
    int valid;
} ResCacheEntry;

static ResCacheEntry s_cache[RES_CACHE_SLOTS];
static int s_cache_next = 0;

static ResCacheEntry *Res_FindCached(const char *name)
{
    for (int i = 0; i < RES_CACHE_SLOTS; i++) {
        if (s_cache[i].valid && strcmp(s_cache[i].name, name) == 0) {
            return &s_cache[i];
        }
    }
    return NULL;
}

static void Res_FreeSlot(ResCacheEntry *slot)
{
    if (slot->valid && slot->data) {
        free(slot->data);
    }
    slot->data = NULL;
    slot->valid = 0;
}

int Res_LoadFileByName(const char *disc_root, const char *filename, void *out_buffer, unsigned int buffer_size)
{
    ResCacheEntry *hit = Res_FindCached(filename);
    if (hit) {
        size_t n = (hit->size < buffer_size) ? hit->size : buffer_size;
        memcpy(out_buffer, hit->data, n);
        printf("[res] cache hit %s (%zu bytes)\n", filename, n);
        return (int)n;
    }

    char path[512];
    snprintf(path, sizeof(path), "%s/%s", disc_root, filename);

    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "[res] could not open %s\n", path);
        return -1;
    }

    unsigned char *buf = (unsigned char *)malloc(buffer_size);
    if (!buf) {
        fclose(f);
        fprintf(stderr, "[res] out of memory loading %s\n", filename);
        return -1;
    }

    size_t n = fread(buf, 1, buffer_size, f);
    fclose(f);

    ResCacheEntry *slot = &s_cache[s_cache_next];
    Res_FreeSlot(slot);
    strncpy(slot->name, filename, RES_MAX_NAME - 1);
    slot->name[RES_MAX_NAME - 1] = '\0';
    slot->data = buf;
    slot->size = n;
    slot->valid = 1;
    s_cache_next = (s_cache_next + 1) % RES_CACHE_SLOTS;

    size_t copy_n = (n < buffer_size) ? n : buffer_size;
    memcpy(out_buffer, buf, copy_n);
    printf("[res] loaded %s (%zu bytes)\n", filename, n);
    return (int)n;
}

void Res_ClearCache(void)
{
    for (int i = 0; i < RES_CACHE_SLOTS; i++) {
        Res_FreeSlot(&s_cache[i]);
    }
}
