#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#  define _POSIX_C_SOURCE 200809L
#endif
#if !defined(_WIN32) && !defined(_FILE_OFFSET_BITS)
#  define _FILE_OFFSET_BITS 64
#endif
#include "u8path.h"
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <direct.h>
#  include <io.h>
#  include <wchar.h>

/* UTF-8 (or, if invalid, ANSI code page) -> malloc'd UTF-16 */
static wchar_t *widen(const char *s)
{
    if (!s) return NULL;
    UINT cp = CP_UTF8;
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, NULL, 0);
    if (n <= 0) { cp = CP_ACP; n = MultiByteToWideChar(CP_ACP, 0, s, -1, NULL, 0); }
    if (n <= 0) return NULL;
    wchar_t *w = (wchar_t *)malloc((size_t)n * sizeof(wchar_t));
    if (w) MultiByteToWideChar(cp, cp == CP_UTF8 ? MB_ERR_INVALID_CHARS : 0, s, -1, w, n);
    return w;
}

FILE *u8_fopen(const char *path, const char *mode)
{
    wchar_t *wp = widen(path), *wm = widen(mode);
    FILE *f = (wp && wm) ? _wfopen(wp, wm) : NULL;
    free(wp); free(wm);
    return f;
}

int u8_stat(const char *path, int64_t *size, int *is_dir)
{
    wchar_t *wp = widen(path);
    struct _stat64 st;
    int r = wp ? _wstat64(wp, &st) : -1;
    free(wp);
    if (r != 0) return -1;
    if (size) *size = (int64_t)st.st_size;
    if (is_dir) *is_dir = (st.st_mode & _S_IFMT) == _S_IFDIR;
    return 0;
}

int u8_mkdir(const char *path)
{
    wchar_t *wp = widen(path);
    int r = wp ? _wmkdir(wp) : -1;
    free(wp);
    return r;
}

int u8_remove(const char *path)
{
    wchar_t *wp = widen(path);
    int r = wp ? _wremove(wp) : -1;
    free(wp);
    return r;
}

int u8_rename_replace(const char *from, const char *to)
{
    wchar_t *a = widen(from), *b = widen(to);
    int ok = a && b && MoveFileExW(a, b, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    free(a); free(b);
    return ok ? 0 : -1;
}

int u8_list_files(const char *dir, int (*cb)(const char *, void *), void *ud)
{
    size_t n = strlen(dir && *dir ? dir : ".");
    char *pat = (char *)malloc(n + 3);
    if (!pat) return -1;
    memcpy(pat, dir && *dir ? dir : ".", n);
    if (n && pat[n - 1] != '/' && pat[n - 1] != '\\') pat[n++] = '\\';
    pat[n++] = '*'; pat[n] = 0;
    wchar_t *wp = widen(pat);
    free(pat);
    if (!wp) return -1;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(wp, &fd);
    free(wp);
    if (h == INVALID_HANDLE_VALUE) return -1;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        char name[MAX_PATH * 4];
        if (WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, name, sizeof name, NULL, NULL) <= 0) continue;
        if (cb(name, ud)) break;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return 0;
}

void u8_console_init(void) { SetConsoleOutputCP(CP_UTF8); }

#else  /* POSIX: paths are already bytes (UTF-8 in practice) */
#  include <dirent.h>
#  include <stdio.h>

FILE *u8_fopen(const char *path, const char *mode) { return fopen(path, mode); }

int u8_stat(const char *path, int64_t *size, int *is_dir)
{
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    if (size) *size = (int64_t)st.st_size;
    if (is_dir) *is_dir = S_ISDIR(st.st_mode);
    return 0;
}

int u8_mkdir(const char *path) { return mkdir(path, 0777); }
int u8_remove(const char *path) { return remove(path); }
int u8_rename_replace(const char *from, const char *to) { return rename(from, to); }

int u8_list_files(const char *dir, int (*cb)(const char *, void *), void *ud)
{
    const char *d = dir && *dir ? dir : ".";
    DIR *dp = opendir(d);
    if (!dp) return -1;
    struct dirent *de;
    size_t n = strlen(d);
    while ((de = readdir(dp)) != NULL) {
        char *p = (char *)malloc(n + strlen(de->d_name) + 2);
        if (!p) break;
        sprintf(p, "%s/%s", d, de->d_name);
        struct stat st;
        int reg = stat(p, &st) == 0 && S_ISREG(st.st_mode);
        free(p);
        if (reg && cb(de->d_name, ud)) break;
    }
    closedir(dp);
    return 0;
}

void u8_console_init(void) { }
#endif
