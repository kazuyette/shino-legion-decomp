/* Asset extraction from the player's own disc image, see extract.h. Plain C11, no SDL. */
#if !defined(_WIN32)
#  ifndef _POSIX_C_SOURCE
#    define _POSIX_C_SOURCE 200809L
#  endif
#  ifndef _FILE_OFFSET_BITS
#    define _FILE_OFFSET_BITS 64
#  endif
#endif
#include "extract.h"
#include "asset_manifest.h"
#include "lz.h"
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <direct.h>
#else
#  include <dirent.h>
#endif

#define RAW            2352          /* raw CD sector / CD-DA frame (588 stereo samples) */
#define MAX_TRACKS     99
#define MAX_FILES      99
#define MAX_ISO_FILES  4096
#define PATHLEN        1024
#define CHUNK          64            /* sectors per read */

/* IP.BIN (sector 0) of Shinobi Legions (USA): product number, area code 'U'. */
static const char k_product[] = "T-2301H";

static char s_err[1024];
static void set_err(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s_err, sizeof s_err, fmt, ap);
    va_end(ap);
}
const char *Extract_LastError(void) { return s_err; }

const char *Extract_ResultName(int code)
{
    switch (code) {
    case EXTRACT_OK:                 return "ok";
    case EXTRACT_WARN_NO_AUDIO:      return "game files ok, music tracks missing";
    case EXTRACT_WARN_AUDIO_DIFFERS: return "game files ok, music differs from the reference";
    case EXTRACT_ERR_ARGS:           return "bad arguments";
    case EXTRACT_ERR_OPEN:           return "cannot open the disc image";
    case EXTRACT_ERR_CUE:            return "unusable cue sheet";
    case EXTRACT_ERR_NOT_SATURN:     return "not a Sega Saturn disc image";
    case EXTRACT_ERR_WRONG_DISC:     return "wrong disc (not Shinobi Legions)";
    case EXTRACT_ERR_REGION:         return "other region or version of the game";
    case EXTRACT_ERR_BAD_DUMP:       return "bad or incomplete dump";
    case EXTRACT_ERR_WRITE:          return "cannot write the assets folder";
    case EXTRACT_ERR_NOMEM:          return "out of memory";
    default:                         return "unknown error";
    }
}

/* ---- CRC-32 (zlib polynomial) ------------------------------------------------------------ */

uint32_t Extract_Crc32(uint32_t crc, const void *data, size_t len)
{
    static uint32_t tab[256];
    static int init;
    const uint8_t *p = (const uint8_t *)data;
    if (!init) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            tab[i] = c;
        }
        init = 1;
    }
    crc = ~crc;
    while (len--) crc = tab[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

int         Extract_ManifestCount(void)       { return ASSET_MANIFEST_COUNT; }
const char *Extract_ManifestName(int i)       { return i >= 0 && i < ASSET_MANIFEST_COUNT ? g_assetManifest[i].name : NULL; }
int         Extract_ManifestOptional(int i)   { return i >= 0 && i < ASSET_MANIFEST_COUNT ? g_assetManifest[i].optional : 0; }

/* ---- paths / files ----------------------------------------------------------------------- */

static int is_sep(char c) { return c == '/' || c == '\\'; }

static int ieq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++)
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
    return *a == *b;
}

static int has_ext(const char *path, const char *ext)
{
    size_t n = strlen(path), e = strlen(ext);
    return n > e && ieq(path + n - e, ext);
}

int Extract_IsImagePath(const char *path)
{
    return path && (has_ext(path, ".cue") || has_ext(path, ".bin") || has_ext(path, ".iso"));
}

static const char *base_of(const char *path)
{
    const char *b = path;
    for (const char *p = path; *p; p++) if (is_sep(*p)) b = p + 1;
    return b;
}

/* directory part of path, with its trailing separator ("" if none) */
static void dir_of(const char *path, char *out, size_t n)
{
    size_t len = (size_t)(base_of(path) - path);
    if (len >= n) len = n - 1;
    memcpy(out, path, len);
    out[len] = 0;
}

static void join(char *out, size_t n, const char *dir, const char *name)
{
    size_t d = strlen(dir);
    if (!d) snprintf(out, n, "%s", name);
    else if (is_sep(dir[d - 1])) snprintf(out, n, "%s%s", dir, name);
    else snprintf(out, n, "%s/%s", dir, name);
}

static int is_abs(const char *p)
{
    return is_sep(p[0]) || (isalpha((unsigned char)p[0]) && p[1] == ':');
}

/* size of a regular file, -1 if missing */
static int64_t file_size(const char *path)
{
#ifdef _WIN32
    struct _stat64 st;
    if (_stat64(path, &st) != 0 || (st.st_mode & _S_IFMT) != _S_IFREG) return -1;
    return (int64_t)st.st_size;
#else
    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) return -1;
    return (int64_t)st.st_size;
#endif
}

static int is_dir(const char *path)
{
#ifdef _WIN32
    struct _stat64 st;
    return _stat64(path, &st) == 0 && (st.st_mode & _S_IFMT) == _S_IFDIR;
#else
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

static void mkdir1(const char *p)
{
#ifdef _WIN32
    _mkdir(p);
#else
    mkdir(p, 0777);
#endif
}

static int make_dirs(const char *path)
{
    char tmp[PATHLEN];
    snprintf(tmp, sizeof tmp, "%s", path);
    size_t n = strlen(tmp);
    while (n > 1 && is_sep(tmp[n - 1])) tmp[--n] = 0;
    for (size_t i = 1; i < n; i++) {
        if (is_sep(tmp[i]) && tmp[i - 1] != ':' && !is_sep(tmp[i - 1])) {
            char c = tmp[i];
            tmp[i] = 0;
            if (!is_dir(tmp)) mkdir1(tmp);
            tmp[i] = c;
        }
    }
    if (!is_dir(tmp)) mkdir1(tmp);
    return is_dir(tmp);
}

static int seek64(FILE *f, int64_t off)
{
#ifdef _WIN32
    return _fseeki64(f, off, SEEK_SET);
#else
    return fseeko(f, (off_t)off, SEEK_SET);
#endif
}

static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

/* ---- disc image (cue semantics of tools/rip_cdda.py) -------------------------------------- */

typedef struct {
    char    path[PATHLEN];
    FILE   *fp;          /* NULL = missing */
    int64_t frames;      /* file size / sector size of its first track */
    int     sector;
} ImgFile;

typedef struct {
    int     num, audio, sector, dataoff, file;
    int     has0, has1, nidx;
    int64_t idx0, idx1, minidx;   /* INDEX frames inside the file */
    int64_t pregap, postgap;      /* PREGAP / POSTGAP: silence NOT stored in the file */
    int64_t start, end;           /* INDEX 01 (or 00) and exclusive end, frames in the file */
    int64_t fbase;                /* frame offset of the file in the concatenated files */
    int64_t lba, shift;           /* disc LBA of INDEX 01; disc LBA - concatenated frame */
} Track;

typedef struct {
    ImgFile files[MAX_FILES];
    int     nfiles;
    Track   tracks[MAX_TRACKS];
    int     ntracks;
    int     data;                 /* index of the data track, -1 = none */
} Image;

static int set_mode(Track *t, const char *mode)
{
    t->audio = 0;
    if (ieq(mode, "AUDIO"))           { t->audio = 1; t->sector = RAW; t->dataoff = 0; }
    else if (ieq(mode, "MODE1/2352")) { t->sector = RAW;  t->dataoff = 16; }
    else if (ieq(mode, "MODE1/2048")) { t->sector = 2048; t->dataoff = 0; }
    else if (ieq(mode, "MODE2/2352")) { t->sector = RAW;  t->dataoff = 24; }
    else if (ieq(mode, "MODE2/2336")) { t->sector = 2336; t->dataoff = 8; }
    else if (ieq(mode, "MODE2/2048")) { t->sector = 2048; t->dataoff = 0; }
    else return 0;
    return 1;
}

static int parse_msf(const char *s, int64_t *out)
{
    int m, sec, f;
    if (sscanf(s, "%d:%d:%d", &m, &sec, &f) != 3 || m < 0 || sec < 0 || f < 0) return 0;
    *out = ((int64_t)m * 60 + sec) * 75 + f;
    return 1;
}

/* keyword at the start of s (any case) followed by a blank: returns the rest, else NULL */
static char *kw(char *s, const char *k)
{
    size_t n = strlen(k);
    for (size_t i = 0; i < n; i++)
        if (toupper((unsigned char)s[i]) != k[i]) return NULL;
    if (s[n] && !isspace((unsigned char)s[n])) return NULL;
    s += n;
    while (*s && isspace((unsigned char)*s)) s++;
    return s;
}

static int parse_cue(Image *im, const char *cuePath)
{
    FILE *f = fopen(cuePath, "rb");
    if (!f) { set_err("cannot open %s", cuePath); return EXTRACT_ERR_OPEN; }
    char dir[PATHLEN], line[PATHLEN + 128];
    dir_of(cuePath, dir, sizeof dir);
    int cur = -1, lineNo = 0, rc = 0;
    while (!rc && fgets(line, sizeof line, f)) {
        char *s = line, *r;
        if (++lineNo == 1 && (uint8_t)s[0] == 0xEF && (uint8_t)s[1] == 0xBB && (uint8_t)s[2] == 0xBF) s += 3;
        size_t n = strlen(s);
        while (n && isspace((unsigned char)s[n - 1])) s[--n] = 0;
        while (*s && isspace((unsigned char)*s)) s++;
        if (!*s) continue;
        if ((r = kw(s, "FILE")) != NULL) {
            char name[PATHLEN];
            if (*r == '"') {
                char *e = strchr(r + 1, '"');
                if (!e) { rc = 1; break; }
                snprintf(name, sizeof name, "%.*s", (int)(e - r - 1), r + 1);
            } else {                                  /* unquoted: up to the file type */
                char *e = r + strlen(r);
                while (e > r && !isspace((unsigned char)e[-1])) e--;
                while (e > r && isspace((unsigned char)e[-1])) e--;
                if (e == r) e = r + strlen(r);
                snprintf(name, sizeof name, "%.*s", (int)(e - r), r);
            }
            if (im->nfiles >= MAX_FILES) { rc = 1; break; }
            cur = im->nfiles++;
            if (is_abs(name)) snprintf(im->files[cur].path, PATHLEN, "%s", name);
            else join(im->files[cur].path, PATHLEN, dir, name);
        } else if ((r = kw(s, "TRACK")) != NULL) {
            int num;
            char mode[32];
            if (cur < 0 || im->ntracks >= MAX_TRACKS || sscanf(r, "%d %31s", &num, mode) != 2) { rc = 1; break; }
            Track *t = &im->tracks[im->ntracks++];
            memset(t, 0, sizeof *t);
            t->num = num;
            t->file = cur;
            if (!set_mode(t, mode)) {
                fclose(f);
                set_err("%s line %d: unsupported track mode %s", base_of(cuePath), lineNo, mode);
                return EXTRACT_ERR_CUE;
            }
            if (!im->files[cur].sector) im->files[cur].sector = t->sector;
        } else if ((r = kw(s, "INDEX")) != NULL) {
            int num;
            char msf[32];
            int64_t fr;
            if (!im->ntracks || sscanf(r, "%d %31s", &num, msf) != 2 || !parse_msf(msf, &fr)) { rc = 1; break; }
            Track *t = &im->tracks[im->ntracks - 1];
            if (num == 0) { t->idx0 = fr; t->has0 = 1; }
            if (num == 1) { t->idx1 = fr; t->has1 = 1; }
            if (!t->nidx || fr < t->minidx) t->minidx = fr;
            t->nidx++;
        } else if ((r = kw(s, "PREGAP")) != NULL || (r = kw(s, "POSTGAP")) != NULL) {
            int64_t fr;
            if (!im->ntracks || !parse_msf(r, &fr)) { rc = 1; break; }
            Track *t = &im->tracks[im->ntracks - 1];
            if (toupper((unsigned char)s[1]) == 'R') t->pregap = fr; else t->postgap = fr;
        }                                             /* REM, CATALOG, FLAGS, TITLE...: ignored */
    }
    fclose(f);
    if (rc) { set_err("%s line %d: cannot parse the cue sheet", base_of(cuePath), lineNo); return EXTRACT_ERR_CUE; }
    if (!im->ntracks) { set_err("%s: no TRACK in the cue sheet", base_of(cuePath)); return EXTRACT_ERR_CUE; }
    for (int i = 0; i < im->ntracks; i++)
        if (!im->tracks[i].nidx) { set_err("%s: track %d has no INDEX", base_of(cuePath), im->tracks[i].num); return EXTRACT_ERR_CUE; }

    /* open the files; a renamed image (cue still naming the old .bin) is found by name */
    for (int i = 0; i < im->nfiles; i++) {
        ImgFile *fl = &im->files[i];
        char alt[PATHLEN];
        int64_t sz = file_size(fl->path);
        if (sz < 0) {
            join(alt, sizeof alt, dir, base_of(fl->path));
            if ((sz = file_size(alt)) >= 0) snprintf(fl->path, PATHLEN, "%s", alt);
        }
        if (sz < 0 && im->nfiles == 1) {
            size_t n = strlen(cuePath);
            snprintf(alt, sizeof alt, "%.*s.bin", (int)(n > 4 ? n - 4 : n), cuePath);
            if ((sz = file_size(alt)) >= 0) snprintf(fl->path, PATHLEN, "%s", alt);
        }
        if (sz >= 0) fl->fp = fopen(fl->path, "rb");
        if (!fl->sector) fl->sector = RAW;
        fl->frames = fl->fp ? sz / fl->sector : 0;
    }

    /* track extents and disc LBAs, as rip_cdda.parse_cue */
    int64_t acc = 0, base[MAX_FILES], shift = 0;
    for (int i = 0; i < im->nfiles; i++) { base[i] = acc; acc += im->files[i].frames; }
    for (int i = 0; i < im->ntracks; i++) {
        Track *t = &im->tracks[i], *nx = i + 1 < im->ntracks ? &im->tracks[i + 1] : NULL;
        t->fbase = base[t->file];
        t->start = t->has1 ? t->idx1 : t->has0 ? t->idx0 : 0;
        t->end = nx && nx->file == t->file ? nx->minidx : im->files[t->file].frames;
        shift += t->pregap;
        t->lba = t->fbase + t->start + shift;
        t->shift = shift;
        shift += t->postgap;
        if (im->data < 0 && !t->audio) im->data = i;
    }
    if (im->data < 0) { set_err("%s: no data track (TRACK 01 MODE1/...)", base_of(cuePath)); return EXTRACT_ERR_CUE; }
    if (!im->files[im->tracks[im->data].file].fp) {
        set_err("data track file missing: %s", im->files[im->tracks[im->data].file].path);
        return EXTRACT_ERR_OPEN;
    }
    return 0;
}

/* A lone .bin / .iso: the data track only (2048-byte or raw 2352-byte sectors). */
static int open_raw(Image *im, const char *path)
{
    ImgFile *fl = &im->files[0];
    Track *t = &im->tracks[0];
    uint8_t s[RAW];
    snprintf(fl->path, PATHLEN, "%s", path);
    int64_t sz = file_size(path);
    if (sz < 0 || !(fl->fp = fopen(path, "rb"))) { set_err("cannot open %s", path); return EXTRACT_ERR_OPEN; }
    im->nfiles = im->ntracks = 1;
    memset(t, 0, sizeof *t);
    t->num = 1;
    if (seek64(fl->fp, 16 * 2048) == 0 && fread(s, 1, 6, fl->fp) == 6 && memcmp(s + 1, "CD001", 5) == 0) {
        t->sector = 2048;
    } else {
        static const uint8_t sync[12] = { 0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0 };
        if (seek64(fl->fp, 16 * RAW) != 0 || fread(s, 1, RAW, fl->fp) != RAW || memcmp(s, sync, 12) != 0) {
            set_err("%s: no ISO9660 file system (not a CD image?)", base_of(path));
            return EXTRACT_ERR_NOT_SATURN;
        }
        t->sector = RAW;
        t->dataoff = s[15] == 2 ? 24 : 16;
    }
    fl->sector = t->sector;
    fl->frames = sz / t->sector;
    t->end = fl->frames;
    im->data = 0;
    return 0;
}

static int open_image(Image *im, const char *in)
{
    if (has_ext(in, ".cue")) return parse_cue(im, in);
    if (has_ext(in, ".bin")) {                        /* its cue, when next to it */
        char cue[PATHLEN];
        size_t n = strlen(in) - 4;
        snprintf(cue, sizeof cue, "%.*s.cue", (int)n, in);
        if (file_size(cue) >= 0) return parse_cue(im, cue);
        snprintf(cue, sizeof cue, "%.*s.CUE", (int)n, in);
        if (file_size(cue) >= 0) return parse_cue(im, cue);
    }
    return open_raw(im, in);
}

static void close_image(Image *im)
{
    for (int i = 0; i < im->nfiles; i++) if (im->files[i].fp) fclose(im->files[i].fp);
}

/* count user-data sectors of the data track from disc LBA lba -> dst (count * 2048) */
static int read_user(Image *im, int64_t lba, uint32_t count, uint8_t *dst)
{
    const Track *t = &im->tracks[im->data];
    ImgFile *f = &im->files[t->file];
    int64_t frame = lba - t->shift - t->fbase;
    if (frame < 0 || frame + count > f->frames) return 0;
    if (seek64(f->fp, frame * t->sector) != 0) return 0;
    if (t->sector == 2048) return fread(dst, 2048, count, f->fp) == count;
    uint8_t raw[RAW];
    for (uint32_t i = 0; i < count; i++) {
        if (fread(raw, (size_t)t->sector, 1, f->fp) != 1) return 0;
        memcpy(dst + (size_t)i * 2048, raw + t->dataoff, 2048);
    }
    return 1;
}

/* ---- ISO9660 ------------------------------------------------------------------------------- */

typedef struct {
    char     path[128];          /* "NAME.EXT" or "DIR/NAME.EXT" (no ";1") */
    uint32_t lba, size;
} IsoEntry;

typedef struct {
    IsoEntry *e;
    int       n;
} IsoDir;

static void read_dir(Image *im, IsoDir *iso, uint32_t lba, uint32_t size, const char *prefix, int depth)
{
    if (depth > 8 || size == 0 || size > (1u << 22)) return;
    uint32_t ns = (size + 2047) / 2048;
    uint8_t *d = (uint8_t *)malloc((size_t)ns * 2048);
    if (!d) return;
    if (!read_user(im, lba, ns, d)) { free(d); return; }
    uint32_t p = 0;
    while (p < size) {
        uint8_t n = d[p];
        if (n == 0) { p = (p / 2048 + 1) * 2048; continue; }
        if (n < 34 || p + n > ns * 2048) break;
        const uint8_t *e = d + p;
        uint8_t nl = e[32];
        if (33u + nl <= n && !(nl == 1 && (e[33] == 0 || e[33] == 1))) {
            char name[64];
            int k = 0;
            for (; k < nl && k < 63 && e[33 + k] != ';'; k++) name[k] = (char)e[33 + k];
            while (k > 0 && name[k - 1] == '.') k--;       /* "NAME." = no extension */
            name[k] = 0;
            if (e[25] & 2) {
                char sub[128];
                snprintf(sub, sizeof sub, "%s%s/", prefix, name);
                read_dir(im, iso, le32(e + 2), le32(e + 10), sub, depth + 1);
            } else if (iso->n < MAX_ISO_FILES) {
                IsoEntry *x = &iso->e[iso->n++];
                snprintf(x->path, sizeof x->path, "%s%s", prefix, name);
                x->lba = le32(e + 2);
                x->size = le32(e + 10);                       /* u32: >2 GB extents wrap */
            }
        }
        p += n;
    }
    free(d);
}

static const IsoEntry *iso_find(const IsoDir *iso, const char *name)
{
    for (int i = 0; i < iso->n; i++) if (ieq(iso->e[i].path, name)) return &iso->e[i];
    for (int i = 0; i < iso->n; i++) if (ieq(base_of(iso->e[i].path), name)) return &iso->e[i];
    return NULL;
}

static void trim_field(const uint8_t *src, size_t n, char *out)
{
    memcpy(out, src, n);
    out[n] = 0;
    while (n && (out[n - 1] == ' ' || out[n - 1] == 0)) out[--n] = 0;
    for (size_t i = 0; i < n; i++) if ((unsigned char)out[i] < 0x20 || (unsigned char)out[i] > 0x7E) out[i] = '?';
}

/* Saturn header (IP.BIN) and ISO9660 directory. */
static int check_disc(Image *im, IsoDir *iso)
{
    uint8_t s[2048];
    char prod[16], area[16], title[120], upper[120];
    if (!read_user(im, 0, 1, s) || memcmp(s, "SEGA SEGASATURN ", 16) != 0) {
        set_err("not a Sega Saturn disc image (no SEGA SEGASATURN header in sector 0)");
        return EXTRACT_ERR_NOT_SATURN;
    }
    trim_field(s + 0x20, 10, prod);
    trim_field(s + 0x40, 10, area);
    trim_field(s + 0x60, 112, title);
    if (strcmp(prod, k_product) != 0 || !strchr(area, 'U')) {
        size_t i = 0;
        for (; title[i]; i++) upper[i] = (char)toupper((unsigned char)title[i]);
        upper[i] = 0;
        if (strstr(upper, "SHINOBI")) {
            set_err("this is \"%s\" (%s, area %s): only the USA release Shinobi Legions (%s) is supported",
                    title, prod, area, k_product);
            return EXTRACT_ERR_REGION;
        }
        set_err("this disc is \"%s\" (%s), not Shinobi Legions", title, prod);
        return EXTRACT_ERR_WRONG_DISC;
    }
    if (!read_user(im, 16, 1, s) || memcmp(s + 1, "CD001", 5) != 0) {
        set_err("bad dump: no ISO9660 volume descriptor in the data track");
        return EXTRACT_ERR_BAD_DUMP;
    }
    read_dir(im, iso, le32(s + 156 + 2), le32(s + 156 + 10), "", 0);
    if (!iso->n) { set_err("bad dump: empty or unreadable ISO9660 root directory"); return EXTRACT_ERR_BAD_DUMP; }
    for (int i = 0; i < ASSET_MANIFEST_COUNT; i++) {
        const AssetManifestEntry *m = &g_assetManifest[i];
        if (m->kind == AM_CDDA) continue;
        const IsoEntry *x = iso_find(iso, m->src);
        if (!x) { set_err("bad dump or other version: %s is not on the disc", m->src); return EXTRACT_ERR_BAD_DUMP; }
        if (m->kind == AM_ISO && x->size != m->size) {
            set_err("other version or bad dump: %s is %u bytes on the disc, expected %u", m->src, x->size, m->size);
            return EXTRACT_ERR_BAD_DUMP;
        }
    }
    return 0;
}

/* ---- CD-DA ranges (rip_cdda.py --source auto) ---------------------------------------------- */

typedef struct {
    int     ok, file;
    int64_t frame, count;        /* in the image file, 2352-byte frames */
} AudioRange;

static int cmp_lba(const void *a, const void *b)
{
    const IsoEntry *x = *(const IsoEntry *const *)a, *y = *(const IsoEntry *const *)b;
    return x->lba < y->lba ? -1 : x->lba > y->lba;
}

static void audio_ranges(Image *im, const IsoDir *iso, AudioRange r[100])
{
    int allFiles = 1, na = 0, audio[MAX_TRACKS];
    memset(r, 0, 100 * sizeof *r);
    for (int i = 0; i < im->nfiles; i++) if (!im->files[i].fp) allFiles = 0;
    for (int i = 0; i < im->ntracks; i++) {
        Track *t = &im->tracks[i];
        if (!t->audio) continue;
        audio[na++] = i;
        if (t->num > 0 && t->num < 100 && im->files[t->file].fp && t->end > t->start) {
            r[t->num].ok = 1; r[t->num].file = t->file;
            r[t->num].frame = t->start; r[t->num].count = t->end - t->start;
        }
    }
    if (!na || !allFiles) return;           /* LBAs are only meaningful with every file present */
    /* The ISO entries lying in the audio area (TITLE.PCM, ENDING.PCM), in LBA order, are the
     * k-th audio tracks: their extents are what the game asks the CD block to play. */
    int64_t first = -1;
    for (int k = 0; k < na; k++) {
        const Track *t = &im->tracks[audio[k]];
        if (first < 0 || t->lba - t->pregap < first) first = t->lba - t->pregap;
    }
    const IsoEntry **in = (const IsoEntry **)malloc(sizeof *in * (size_t)(iso->n ? iso->n : 1));
    int ni = 0;
    if (!in) return;
    for (int i = 0; i < iso->n; i++) if ((int64_t)iso->e[i].lba >= first) in[ni++] = &iso->e[i];
    qsort(in, (size_t)ni, sizeof *in, cmp_lba);
    for (int k = 0; k < ni && k < na; k++) {
        int64_t lba = in[k]->lba, nsect = ((int64_t)in[k]->size + 2047) / 2048;
        int best = -1;
        for (int i = 0; i < im->ntracks; i++)
            if (lba >= im->tracks[i].lba - im->tracks[i].pregap) best = i;
        int num = im->tracks[audio[k]].num;
        if (best < 0 || num <= 0 || num >= 100) continue;
        const Track *b = &im->tracks[best];
        r[num].ok = 1;
        r[num].file = b->file;
        r[num].frame = lba - b->shift - b->fbase;
        r[num].count = nsect;
    }
    free(in);
}

/* ---- output -------------------------------------------------------------------------------- */

typedef struct {
    FILE    *f;
    uint32_t crc;
    uint64_t n;
    char     tmp[PATHLEN + 8], dst[PATHLEN];
} OutFile;

static int out_open(OutFile *o, const char *dir, const char *name)
{
    join(o->dst, sizeof o->dst, dir, name);
    snprintf(o->tmp, sizeof o->tmp, "%s.part", o->dst);
    o->crc = 0;
    o->n = 0;
    o->f = fopen(o->tmp, "wb");
    return o->f != NULL;
}

static int out_write(OutFile *o, const void *p, size_t len)
{
    o->crc = Extract_Crc32(o->crc, p, len);
    o->n += len;
    return fwrite(p, 1, len, o->f) == len;
}

static void out_abort(OutFile *o)
{
    if (o->f) fclose(o->f);
    o->f = NULL;
    remove(o->tmp);
}

static int out_commit(OutFile *o)
{
    int ok = fflush(o->f) == 0;
    ok &= fclose(o->f) == 0;
    o->f = NULL;
    if (ok) {
#ifdef _WIN32
        ok = MoveFileExA(o->tmp, o->dst, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
        ok = rename(o->tmp, o->dst) == 0;
#endif
    }
    if (!ok) remove(o->tmp);
    return ok;
}

static struct {
    ExtractProgressFn fn;
    uint64_t done, total;
    int last;
} s_prog;

static void progress(const char *msg, uint64_t add, int force)
{
    s_prog.done += add;
    int pct = s_prog.total ? (int)(s_prog.done * 100 / s_prog.total) : 0;
    if (pct > 100) pct = 100;
    if (s_prog.fn && (force || pct != s_prog.last)) s_prog.fn(msg, pct);
    s_prog.last = pct;
}

static uint8_t s_buf[CHUNK * RAW];

/* 1 = written, 0 = read error (truncated image), -1 = write error */
static int copy_iso(Image *im, const IsoEntry *x, OutFile *o, const char *name)
{
    uint32_t left = x->size;
    int64_t lba = x->lba;
    while (left) {
        uint32_t ns = (left + 2047) / 2048;
        if (ns > CHUNK) ns = CHUNK;
        if (!read_user(im, lba, ns, s_buf)) return 0;
        uint32_t take = ns * 2048 < left ? ns * 2048 : left;
        if (!out_write(o, s_buf, take)) return -1;
        left -= take;
        lba += ns;
        progress(name, take, 0);
    }
    return 1;
}

static int write_lz(Image *im, const IsoEntry *x, OutFile *o, uint32_t expect)
{
    uint32_t ns = (x->size + 2047) / 2048;
    uint8_t *src = (uint8_t *)malloc((size_t)ns * 2048 + 4), *dst = NULL;
    int rc = 0;
    if (!src) return -2;
    if (!read_user(im, x->lba, ns, src)) { free(src); return 0; }
    size_t outLen = x->size >= 4 ? Lz_OutputSize(src) : 0;
    if (outLen != expect) { free(src); return -3; }
    dst = (uint8_t *)malloc(outLen + 64);                 /* a final back-reference may overshoot */
    if (!dst) { free(src); return -2; }
    size_t got = Lz_Decompress(src, x->size, dst, outLen + 64);
    if (got < outLen) rc = -3;
    else rc = out_write(o, dst, outLen) ? 1 : -1;
    free(src);
    free(dst);
    return rc;
}

static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }

static int write_wav(Image *im, const AudioRange *r, OutFile *o, const char *name)
{
    ImgFile *f = &im->files[r->file];
    if (f->sector != RAW || r->frame < 0 || r->frame + r->count > f->frames) return 0;
    uint32_t data = (uint32_t)(r->count * RAW);
    uint8_t h[44];
    memcpy(h, "RIFF", 4); put32(h + 4, 36 + data); memcpy(h + 8, "WAVEfmt ", 8);
    put32(h + 16, 16); put16(h + 20, 1); put16(h + 22, 2); put32(h + 24, 44100);
    put32(h + 28, 44100 * 4); put16(h + 32, 4); put16(h + 34, 16);
    memcpy(h + 36, "data", 4); put32(h + 40, data);
    if (!out_write(o, h, sizeof h)) return -1;
    progress(name, sizeof h, 0);
    if (seek64(f->fp, r->frame * RAW) != 0) return 0;
    for (int64_t left = r->count; left > 0;) {
        size_t n = left > CHUNK ? CHUNK : (size_t)left;
        if (fread(s_buf, RAW, n, f->fp) != n) return 0;
        if (!out_write(o, s_buf, n * RAW)) return -1;    /* CD-DA = 16-bit LE stereo already */
        left -= (int64_t)n;
        progress(name, n * RAW, 0);
    }
    return 1;
}

/* ---- entry point --------------------------------------------------------------------------- */

int Extract_FromCue(const char *in, const char *outDir, ExtractProgressFn fn)
{
    s_err[0] = 0;
    if (!in || !*in || !outDir || !*outDir) { set_err("no disc image or output folder given"); return EXTRACT_ERR_ARGS; }
    Image *im = (Image *)calloc(1, sizeof *im);
    IsoDir iso = { (IsoEntry *)calloc(MAX_ISO_FILES, sizeof(IsoEntry)), 0 };
    static AudioRange ranges[100];
    int rc;
    if (!im || !iso.e) { free(im); free(iso.e); set_err("out of memory"); return EXTRACT_ERR_NOMEM; }
    im->data = -1;
    memset(&s_prog, 0, sizeof s_prog);
    s_prog.fn = fn;
    s_prog.last = -1;
    progress("reading the disc image", 0, 1);

    if ((rc = open_image(im, in)) != 0 || (rc = check_disc(im, &iso)) != 0) goto done;
    audio_ranges(im, &iso, ranges);
    if (!make_dirs(outDir)) { set_err("cannot create the folder %s", outDir); rc = EXTRACT_ERR_WRITE; goto done; }

    for (int i = 0; i < ASSET_MANIFEST_COUNT; i++) {
        const AssetManifestEntry *m = &g_assetManifest[i];
        if (m->kind != AM_CDDA || ranges[m->track].ok) s_prog.total += m->size;
    }

    char bad[400] = "", noAudio[200] = "", diffAudio[200] = "";
    int nbad = 0;
    for (int i = 0; i < ASSET_MANIFEST_COUNT && rc >= 0; i++) {
        const AssetManifestEntry *m = &g_assetManifest[i];
        OutFile o;
        int w;
        if (m->kind == AM_CDDA && !ranges[m->track].ok) {
            size_t l = strlen(noAudio);
            snprintf(noAudio + l, sizeof noAudio - l, "%s%s", l ? ", " : "", m->name);
            continue;
        }
        progress(m->name, 0, 1);
        if (!out_open(&o, outDir, m->name)) { set_err("cannot write %s/%s", outDir, m->name); rc = EXTRACT_ERR_WRITE; break; }
        if (m->kind == AM_ISO)     w = copy_iso(im, iso_find(&iso, m->src), &o, m->name);
        else if (m->kind == AM_LZ) { w = write_lz(im, iso_find(&iso, m->src), &o, m->size); progress(m->name, m->size, 0); }
        else                       w = write_wav(im, &ranges[m->track], &o, m->name);
        if (w == 1 && (o.n != m->size || o.crc != m->crc32)) w = m->kind == AM_CDDA ? 2 : -3;
        if (w == 1 || w == 2) {
            if (!out_commit(&o)) { set_err("cannot write %s/%s", outDir, m->name); rc = EXTRACT_ERR_WRITE; break; }
            if (w == 2) {
                size_t l = strlen(diffAudio);
                snprintf(diffAudio + l, sizeof diffAudio - l, "%s%s", l ? ", " : "", m->name);
            }
            continue;
        }
        out_abort(&o);
        if (w == -1) { set_err("cannot write %s/%s (disk full?)", outDir, m->name); rc = EXTRACT_ERR_WRITE; break; }
        if (w == -2) { set_err("out of memory"); rc = EXTRACT_ERR_NOMEM; break; }
        if (w == 0 && m->kind == AM_CDDA) {                /* audio track shorter than expected */
            size_t l = strlen(noAudio);
            snprintf(noAudio + l, sizeof noAudio - l, "%s%s", l ? ", " : "", m->name);
            continue;
        }
        if (w == 0) { set_err("bad dump: the image is truncated (reading %s)", m->src); rc = EXTRACT_ERR_BAD_DUMP; break; }
        nbad++;                                            /* checksum mismatch: not installed */
        size_t l = strlen(bad);
        if (l < sizeof bad - 40) snprintf(bad + l, sizeof bad - l, "%s%s", l ? ", " : "", m->name);
    }
    if (rc < 0) goto done;
    if (nbad) {
        set_err("bad dump or other version: %d file(s) differ from the original and were not installed: %s",
                nbad, bad);
        rc = EXTRACT_ERR_BAD_DUMP;
    } else if (noAudio[0]) {
        set_err("game files extracted; no music: %s missing (%s)", noAudio,
                im->ntracks > 1 ? "CD audio tracks missing or truncated in the image"
                                : "the image has no CD audio tracks: use the .cue");
        rc = EXTRACT_WARN_NO_AUDIO;
    } else if (diffAudio[0]) {
        set_err("game files extracted; %s differ(s) from the reference rip (audio offset of the dump?), installed anyway",
                diffAudio);
        rc = EXTRACT_WARN_AUDIO_DIFFERS;
    } else {
        set_err("%d files extracted and verified", ASSET_MANIFEST_COUNT);
        rc = EXTRACT_OK;
    }
    progress("done", 0, 1);
done:
    close_image(im);
    free(im);
    free(iso.e);
    return rc;
}

/* ---- asset folder check / image lookup ------------------------------------------------------ */

int Extract_CheckAssets(const char *dir, char *firstBad, size_t n)
{
    int bad = 0;
    char p[PATHLEN];
    if (firstBad && n) firstBad[0] = 0;
    for (int i = 0; i < ASSET_MANIFEST_COUNT; i++) {
        const AssetManifestEntry *m = &g_assetManifest[i];
        if (m->optional) continue;
        join(p, sizeof p, dir, m->name);
        if (file_size(p) != (int64_t)m->size) {
            if (!bad && firstBad && n) snprintf(firstBad, n, "%s", m->name);
            bad++;
        }
    }
    return bad;
}

int Extract_FindImage(const char *dir, char *out, size_t n)
{
    char best[PATHLEN] = "";
    const char *d = dir && *dir ? dir : ".";
#ifdef _WIN32
    char pat[PATHLEN];
    WIN32_FIND_DATAA fd;
    join(pat, sizeof pat, d, "*.cue");
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && has_ext(fd.cFileName, ".cue")
            && (!best[0] || strcmp(fd.cFileName, best) < 0))
            snprintf(best, sizeof best, "%s", fd.cFileName);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR *dp = opendir(d);
    struct dirent *de;
    if (!dp) return 0;
    while ((de = readdir(dp)) != NULL) {
        if (!has_ext(de->d_name, ".cue") || (best[0] && strcmp(de->d_name, best) >= 0)) continue;
        char p[PATHLEN];
        join(p, sizeof p, d, de->d_name);
        if (file_size(p) >= 0) snprintf(best, sizeof best, "%s", de->d_name);
    }
    closedir(dp);
#endif
    if (!best[0]) return 0;
    join(out, n, dir && *dir ? dir : "", best);
    return 1;
}
