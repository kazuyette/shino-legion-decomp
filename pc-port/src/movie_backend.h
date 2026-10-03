#ifndef MOVIE_BACKEND_H
#define MOVIE_BACKEND_H
#include <stdint.h>
/* What DIRECTOR's movie wrapper (movie.c) needs from a CPK player (the Saturn's CPK library):
 * open a FILM file or an image in RAM, advance once per 60 Hz frame (0 = playing,
 * non-zero = finished), close. Frames go out through Movie_PresentFrame (movie.h).
 * movie_cpk.c = real player (cpk.c decoder + audio.c), movie_stub.c = skip (unit tests). */
int  MovieBk_OpenFile(const char *name);
int  MovieBk_OpenMem(uint32_t addr, uint32_t size);
int  MovieBk_TaskFrame(void);
void MovieBk_Close(void);
#endif
