/* Asset extractor (src/extract.c, no SDL): error paths on small synthetic images, then the
 * real disc image extracted to a temporary folder and compared byte for byte with the
 * reference assets.
 *   gcc -std=c11 -O2 -Isrc tests/test_extract.c src/extract.c src/lz.c -o test_extract
 *   ./test_extract [--iso TRACK1.ISO] [--keep] [IMAGE.cue|.bin] [REFERENCE_DIR] [OUT_DIR]
 * Run from pc-port/. Defaults: "../Shinobi Legions (USA) (DW0536)/Shinobi Legions (USA)
 * (DW0536).cue", bin/windows/assets, <temp>/sl_extract_test. Without the image the disc
 * part is skipped. --iso also extracts from a plain 2048-byte data-track image (music
 * missing: EXTRACT_WARN_NO_AUDIO, every other file identical). */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#  define _POSIX_C_SOURCE 200809L
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "extract.h"
#ifdef _WIN32
#  include <direct.h>
#  define RMDIR _rmdir
#else
#  include <sys/stat.h>
#  include <unistd.h>
#  define RMDIR rmdir
#endif

static char s_tmp[512];

static void path(char *out, size_t n, const char *dir, const char *name) { snprintf(out, n, "%s/%s", dir, name); }

static void write_file(const char *p, const void *data, size_t len)
{
    FILE *f = fopen(p, "wb");
    assert(f);
    if (len) assert(fwrite(data, 1, len, f) == len);
    fclose(f);
}

static int s_lastPct = -1;
static void on_progress(const char *msg, int pct)
{
    if (pct / 10 != s_lastPct / 10 || pct == 100) printf("  [%3d%%] %s\n", pct, msg);
    s_lastPct = pct;
    fflush(stdout);
}

/* 1 = same bytes */
static int same_file(const char *a, const char *b)
{
    FILE *fa = fopen(a, "rb"), *fb = fopen(b, "rb");
    static unsigned char ba[1 << 16], bb[1 << 16];
    int same = fa && fb;
    while (same) {
        size_t na = fread(ba, 1, sizeof ba, fa), nb = fread(bb, 1, sizeof bb, fb);
        if (na != nb || memcmp(ba, bb, na)) same = 0;
        if (!na) break;
    }
    if (fa) fclose(fa);
    if (fb) fclose(fb);
    return same;
}

static void clean_dir(const char *dir)
{
    char p[1100];
    for (int i = 0; i < Extract_ManifestCount(); i++) {
        path(p, sizeof p, dir, Extract_ManifestName(i));
        remove(p);
    }
    RMDIR(dir);
}

/* A 17-sector 2048-byte image with a Saturn header (product, area, title) and a PVD. */
static void fake_iso(const char *p, const char *product, const char *area, const char *title)
{
    static unsigned char img[17 * 2048];
    memset(img, 0, sizeof img);
    memcpy(img, "SEGA SEGASATURN ", 16);
    memset(img + 0x20, ' ', 0x60 + 112 - 0x20);
    memcpy(img + 0x20, product, strlen(product));
    memcpy(img + 0x40, area, strlen(area));
    memcpy(img + 0x60, title, strlen(title));
    memcpy(img + 16 * 2048, "\001CD001", 6);
    write_file(p, img, sizeof img);
}

static int compare_all(const char *out, const char *ref, int expectAudio)
{
    int same = 0, n = Extract_ManifestCount();
    char a[1100], b[1100];
    for (int i = 0; i < n; i++) {
        const char *name = Extract_ManifestName(i);
        path(a, sizeof a, out, name);
        path(b, sizeof b, ref, name);
        int ok = same_file(a, b);
        if (!ok && (expectAudio || !Extract_ManifestOptional(i))) printf("  DIFFERS: %s\n", name);
        same += ok;
        snprintf(a + strlen(a), sizeof a - strlen(a), ".part");
        FILE *f = fopen(a, "rb");
        if (f) { fclose(f); printf("  leftover %s\n", a); same = -1000; }
    }
    return same;
}

int main(int argc, char **argv)
{
    const char *image = "../Shinobi Legions (USA) (DW0536)/Shinobi Legions (USA) (DW0536).cue";
    const char *ref = "bin/windows/assets", *out = NULL, *iso = NULL;
    int keep = 0, pos = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--iso") && i + 1 < argc) iso = argv[++i];
        else if (!strcmp(argv[i], "--keep")) keep = 1;
        else if (pos == 0) { image = argv[i]; pos++; }
        else if (pos == 1) { ref = argv[i]; pos++; }
        else out = argv[i];
    }
    const char *t = getenv("TMPDIR");
    if (t && strlen(t) > 400) t = NULL;
    if (!t) t = getenv("TEMP");
    if (!t) t = "/tmp";
    snprintf(s_tmp, sizeof s_tmp, "%s/sl_extract_test", t);
    if (!out) out = s_tmp;

    /* ---- unit checks ---- */
    assert(Extract_Crc32(0, "123456789", 9) == 0xCBF43926u);
    assert(Extract_IsImagePath("a/B.CUE") && Extract_IsImagePath("x.bin") && Extract_IsImagePath("y.Iso"));
    assert(!Extract_IsImagePath("x.wav") && !Extract_IsImagePath(".cue") && !Extract_IsImagePath(NULL));
    assert(Extract_ManifestCount() == 115);
    int required = 0;
    for (int i = 0; i < Extract_ManifestCount(); i++) required += !Extract_ManifestOptional(i);
    assert(required == 113);

    char scratch[600], p[1100], q[1100];
    snprintf(scratch, sizeof scratch, "%s/sl_extract_unit", t);
#ifdef _WIN32
    _mkdir(scratch);
#else
    mkdir(scratch, 0777);
#endif
    char bad[64];
    assert(Extract_CheckAssets(scratch, bad, sizeof bad) == 113 && !strcmp(bad, "A.BIN"));
    assert(Extract_FindImage(scratch, p, sizeof p) == 0);

    path(p, sizeof p, scratch, "missing.cue");
    assert(Extract_FromCue(p, scratch, NULL) == EXTRACT_ERR_OPEN);
    assert(Extract_FromCue(NULL, scratch, NULL) == EXTRACT_ERR_ARGS);

    path(p, sizeof p, scratch, "zero.iso");                       /* no ISO9660 */
    static unsigned char zero[20 * 2352];
    write_file(p, zero, sizeof zero);
    assert(Extract_FromCue(p, scratch, NULL) == EXTRACT_ERR_NOT_SATURN);

    path(p, sizeof p, scratch, "other.iso");                      /* another Saturn game */
    fake_iso(p, "T-1234H", "JTUE", "SOME OTHER GAME");
    assert(Extract_FromCue(p, scratch, NULL) == EXTRACT_ERR_WRONG_DISC);
    printf("wrong disc: %s\n", Extract_LastError());
    path(p, sizeof p, scratch, "jp.iso");                         /* Shinobiden (Japan) */
    fake_iso(p, "T-2301G", "J", "SHIN SHINOBIDEN");
    assert(Extract_FromCue(p, scratch, NULL) == EXTRACT_ERR_REGION);
    printf("region: %s\n", Extract_LastError());
    path(p, sizeof p, scratch, "usa.iso");                        /* right header, no files */
    fake_iso(p, "T-2301H", "U", "SHINOBI LEGIONS");
    assert(Extract_FromCue(p, scratch, NULL) == EXTRACT_ERR_BAD_DUMP);
    printf("bad dump: %s\n", Extract_LastError());

    path(p, sizeof p, scratch, "notrack.cue");                    /* cue sheets */
    write_file(p, "REM nothing\r\n", 13);
    assert(Extract_FromCue(p, scratch, NULL) == EXTRACT_ERR_CUE);
    path(p, sizeof p, scratch, "nobin.cue");
    const char *cue = "\xEF\xBB\xBF" "FILE \"nobin_data.bin\" BINARY\r\n  TRACK 01 MODE1/2352\r\n    INDEX 01 00:00:00\r\n";
    write_file(p, cue, strlen(cue));
    assert(Extract_FromCue(p, scratch, NULL) == EXTRACT_ERR_OPEN);
    printf("missing bin: %s\n", Extract_LastError());
    path(q, sizeof q, scratch, "audio.cue");
    cue = "FILE a.bin BINARY\nTRACK 01 AUDIO\nINDEX 01 00:00:00\n";
    write_file(q, cue, strlen(cue));
    assert(Extract_FromCue(q, scratch, NULL) == EXTRACT_ERR_CUE);
    assert(Extract_FindImage(scratch, q, sizeof q) == 1 && strstr(q, "audio.cue"));   /* alphabetical */
    const char *junk[] = { "missing.cue", "zero.iso", "other.iso", "jp.iso", "usa.iso", "notrack.cue",
                           "nobin.cue", "audio.cue" };
    for (size_t i = 0; i < sizeof junk / sizeof *junk; i++) { path(p, sizeof p, scratch, junk[i]); remove(p); }
    RMDIR(scratch);
    printf("unit checks: ok\n");

    /* ---- real disc image ---- */
    FILE *f = fopen(image, "rb");
    if (!f) { printf("SKIP: %s not found (disc image test)\n", image); return 0; }
    fclose(f);
    assert(Extract_CheckAssets(ref, bad, sizeof bad) == 0);

    clean_dir(out);
    time_t t0 = time(NULL);
    printf("extracting %s -> %s\n", image, out);
    int rc = Extract_FromCue(image, out, on_progress);
    printf("result %d (%s): %s, %ld s\n", rc, Extract_ResultName(rc), Extract_LastError(), (long)(time(NULL) - t0));
    int same = compare_all(out, ref, 1);
    printf("%d/%d files identical to %s\n", same, Extract_ManifestCount(), ref);
    assert(Extract_CheckAssets(out, bad, sizeof bad) == 0);
    assert(Extract_FindImage(out, p, sizeof p) == 0);
    if (!keep) clean_dir(out);
    int fail = rc != EXTRACT_OK || same != Extract_ManifestCount();

    if (iso) {
        s_lastPct = -1;
        clean_dir(out);
        printf("extracting %s -> %s\n", iso, out);
        rc = Extract_FromCue(iso, out, on_progress);
        printf("result %d (%s): %s\n", rc, Extract_ResultName(rc), Extract_LastError());
        same = compare_all(out, ref, 0);
        printf("%d/%d files identical (plain .iso: no music)\n", same, Extract_ManifestCount());
        fail |= rc != EXTRACT_WARN_NO_AUDIO || same != Extract_ManifestCount() - 2;
        if (!keep) clean_dir(out);
    }
    printf("%s\n", fail ? "FAIL" : "PASS");
    return fail;
}
