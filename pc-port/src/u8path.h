#ifndef U8PATH_H
#define U8PATH_H
/* UTF-8 file paths on every platform. On Windows the C runtime's fopen/stat/mkdir take
 * ANSI (code page) paths, so a path with characters outside that code page (accents on
 * some systems, Cyrillic, CJK...) could not be opened; SDL gives us UTF-8 (argv via
 * SDL_main, SDL_GetBasePath, drop events). These wrappers convert UTF-8 -> UTF-16 and use
 * the wide APIs. A string that is not valid UTF-8 (e.g. a .cue saved in the ANSI code
 * page) falls back to the ANSI code page. Elsewhere they are the plain POSIX calls. */
#include <stdint.h>
#include <stdio.h>

FILE *u8_fopen(const char *path, const char *mode);
int   u8_stat(const char *path, int64_t *size, int *is_dir);  /* 0 = exists */
int   u8_mkdir(const char *path);                              /* 0 = created */
int   u8_remove(const char *path);                             /* 0 = removed */
int   u8_rename_replace(const char *from, const char *to);     /* 0 = ok, replaces 'to' */
/* calls cb(name_utf8, ud) for each regular file in dir; stops if cb returns non-zero */
int   u8_list_files(const char *dir, int (*cb)(const char *name, void *ud), void *ud);
void  u8_console_init(void);   /* Windows: console output in UTF-8 */
#endif
