/* Placeholder CPK backend (cpk.h): no player yet, every movie is reported finished on its
 * first frame. Used by the unit tests; the game links movie_cpk.c (real player). */
#include "movie_backend.h"
#include <stdio.h>

int MovieBk_OpenFile(const char *name)
{
    printf("[movie] %s (no player: skipped)\n", name);
    return 1;
}

int MovieBk_OpenMem(uint32_t addr, uint32_t size)
{
    printf("[movie] movie in memory %08X (%u bytes) (no player: skipped)\n", (unsigned)addr, (unsigned)size);
    return 1;
}

int  MovieBk_TaskFrame(void) { return 1; }
void MovieBk_Close(void) { }
