/*
 * test_cpk.c - decode Sega FILM (.CPK) movies with cpk.c and report.
 *
 *   test_cpk [-o outdir] [-f frame] [-F ffmpeg] [-m] file.CPK...
 *     -o dir   write <name>_f<frame>.png (and <name>.wav) to dir
 *     -f N     frame index to dump (default 60; clamped to the last frame)
 *     -F path  compare every frame / all audio against `path -i file` (PSNR)
 *     -m       open from memory (Cpk_OpenMem) instead of the file API
 *
 * Build: gcc -std=c11 -Wall -Wextra -O2 -Isrc tests/test_cpk.c src/cpk.c -o test_cpk
 */
#define _POSIX_C_SOURCE 200809L
#include "cpk.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

/* ---- minimal PNG writer (stored deflate blocks, no zlib) ---- */
static uint32_t crc_tab[256];
static void crc_init(void) {
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc_tab[n] = c;
    }
}
static uint32_t crc_upd(uint32_t c, const uint8_t *b, size_t n) {
    for (size_t i = 0; i < n; i++) c = crc_tab[(c ^ b[i]) & 0xFF] ^ (c >> 8);
    return c;
}
static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}
static void png_chunk(FILE *f, const char *type, const uint8_t *data, uint32_t len) {
    uint8_t hdr[8];
    put32(hdr, len);
    memcpy(hdr + 4, type, 4);
    fwrite(hdr, 1, 8, f);
    if (len) fwrite(data, 1, len, f);
    uint32_t c = crc_upd(0xFFFFFFFFu, (const uint8_t *)type, 4);
    c = crc_upd(c, data, len) ^ 0xFFFFFFFFu;
    put32(hdr, c);
    fwrite(hdr, 1, 4, f);
}
static int write_png(const char *path, const uint32_t *argb, int w, int h) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    size_t rawlen = (size_t)h * (1 + (size_t)w * 3);
    uint8_t *raw = malloc(rawlen);
    size_t p = 0;
    for (int y = 0; y < h; y++) {
        raw[p++] = 0;
        for (int x = 0; x < w; x++) {
            uint32_t c = argb[(size_t)y * w + x];
            raw[p++] = (uint8_t)(c >> 16); raw[p++] = (uint8_t)(c >> 8); raw[p++] = (uint8_t)c;
        }
    }
    size_t nblk = (rawlen + 65534) / 65535;
    size_t zlen = 2 + rawlen + nblk * 5 + 4;
    uint8_t *z = malloc(zlen);
    size_t q = 0;
    z[q++] = 0x78; z[q++] = 0x01;
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < rawlen; i++) { a = (a + raw[i]) % 65521; b = (b + a) % 65521; }
    for (size_t off = 0; off < rawlen; off += 65535) {
        size_t n = rawlen - off > 65535 ? 65535 : rawlen - off;
        z[q++] = off + n >= rawlen ? 1 : 0;
        z[q++] = (uint8_t)n; z[q++] = (uint8_t)(n >> 8);
        z[q++] = (uint8_t)~n; z[q++] = (uint8_t)(~n >> 8);
        memcpy(z + q, raw + off, n);
        q += n;
    }
    put32(z + q, (b << 16) | a);
    q += 4;
    static const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    fwrite(sig, 1, 8, f);
    uint8_t ihdr[13];
    put32(ihdr, (uint32_t)w); put32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    png_chunk(f, "IHDR", ihdr, 13);
    png_chunk(f, "IDAT", z, (uint32_t)q);
    png_chunk(f, "IEND", NULL, 0);
    fclose(f);
    free(raw);
    free(z);
    return 0;
}

static void write_wav(const char *path, const int16_t *pcm, long frames, int ch, int rate) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    uint32_t data = (uint32_t)(frames * ch * 2);
    uint8_t h[44];
    memcpy(h, "RIFF", 4);
    uint32_t v[] = {36 + data};
    memcpy(h + 4, v, 4); /* little-endian host assumed (x86) */
    memcpy(h + 8, "WAVEfmt ", 8);
    uint32_t fmtlen = 16; memcpy(h + 16, &fmtlen, 4);
    uint16_t fmt = 1, c = (uint16_t)ch, ba = (uint16_t)(ch * 2), bps = 16;
    uint32_t r = (uint32_t)rate, br = (uint32_t)(rate * ch * 2);
    memcpy(h + 20, &fmt, 2); memcpy(h + 22, &c, 2); memcpy(h + 24, &r, 4);
    memcpy(h + 28, &br, 4); memcpy(h + 32, &ba, 2); memcpy(h + 34, &bps, 2);
    memcpy(h + 36, "data", 4); memcpy(h + 40, &data, 4);
    fwrite(h, 1, 44, f);
    fwrite(pcm, 2, (size_t)frames * ch, f);
    fclose(f);
}

static const char *basename_of(const char *p) {
    const char *s = strrchr(p, '/');
    return s ? s + 1 : p;
}

static int run(const char *path, const char *outdir, int dump, const char *ffmpeg, int use_mem) {
    void *mem = NULL;
    CpkMovie *m;
    if (use_mem) {
        FILE *f = fopen(path, "rb");
        if (!f) { printf("%s: cannot open\n", path); return 1; }
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        mem = malloc((size_t)sz);
        if (fread(mem, 1, (size_t)sz, f) != (size_t)sz) { fclose(f); free(mem); return 1; }
        fclose(f);
        m = Cpk_OpenMem(mem, (size_t)sz);
    } else {
        m = Cpk_Open(path);
    }
    if (!m) { printf("%s: open failed: %s\n", path, Cpk_LastError()); free(mem); return 1; }

    const char *name = basename_of(path);
    int w = Cpk_Width(m), h = Cpk_Height(m), nf = Cpk_FrameCount(m);
    printf("== %s  FILM v%s  %dx%d cvid  STAB %d entries (video %d, audio %d chunks) time base %d\n",
           name, Cpk_Version(m), w, h, Cpk_SampleCount(m), nf, Cpk_SampleCount(m) - nf,
           Cpk_TimeBase(m));
    printf("   video: %.3f fps, audio: %d Hz %d ch %d-bit comp=%d (%s), %ld frames = %.2f s; duration %.2f s\n",
           Cpk_FrameRate(m), Cpk_AudioRate(m), Cpk_AudioChannels(m), Cpk_AudioBits(m),
           Cpk_AudioCompression(m),
           Cpk_AudioCompression(m) == CPK_AUDIO_ADX ? "ADX" : Cpk_AudioBits(m) == 16 ? "s16be planar" : "s8 planar",
           Cpk_AudioFrames(m), Cpk_AudioRate(m) ? (double)Cpk_AudioFrames(m) / Cpk_AudioRate(m) : 0.0,
           Cpk_DurationMs(m) / 1000.0);

    FILE *ff = NULL;
    if (ffmpeg) {
        char cmd[2048];
        snprintf(cmd, sizeof cmd, "%s -v error -i '%s' -map 0:v -vsync passthrough -f rawvideo -pix_fmt rgb24 -", ffmpeg, path);
        ff = popen(cmd, "r");
    }
    uint32_t *fb = malloc((size_t)w * h * 4);
    uint8_t *ref = malloc((size_t)w * h * 3);
    if (dump >= nf) dump = nf - 1;
    double t_total = 0, t_max = 0, sse_total = 0;
    long ndiff_frames = 0, ref_frames = 0;
    double min_psnr = 1e9;
    int ts, i = 0, rc, prev_ts = -1, ts_bad = 0;
    for (;;) {
        double t0 = now_s();
        rc = Cpk_NextFrame(m, fb, &ts);
        double dt = now_s() - t0;
        if (rc == 0) break;
        t_total += dt;
        if (dt > t_max) t_max = dt;
        if (ts <= prev_ts) ts_bad++;
        prev_ts = ts;
        if (rc < 0) printf("   frame %d: decode error\n", i);
        if (i == dump && outdir) {
            char p[1024];
            snprintf(p, sizeof p, "%s/%s_f%03d.png", outdir, name, i);
            write_png(p, fb, w, h);
            printf("   wrote %s (t=%d ms)\n", p, ts);
        }
        if (ff && fread(ref, 1, (size_t)w * h * 3, ff) == (size_t)w * h * 3) {
            double sse = 0;
            for (int k = 0; k < w * h; k++) {
                uint32_t c = fb[k];
                int dr = (int)((c >> 16) & 255) - ref[3 * k], dg = (int)((c >> 8) & 255) - ref[3 * k + 1],
                    db = (int)(c & 255) - ref[3 * k + 2];
                sse += dr * dr + dg * dg + db * db;
            }
            if (sse > 0) {
                ndiff_frames++;
                double psnr = 10 * log10(255.0 * 255.0 / (sse / (w * h * 3.0)));
                if (psnr < min_psnr) min_psnr = psnr;
            }
            sse_total += sse;
            ref_frames++;
        }
        i++;
    }
    if (ff) {
        while (fread(ref, 1, (size_t)w * h * 3, ff) == (size_t)w * h * 3) ref_frames = -labs(ref_frames) - 1000000;
        pclose(ff);
    }
    CpkStats st;
    Cpk_GetStats(m, &st);
    printf("   decoded %d frames (%d bad, %d keyframes, %d strips) in %.1f ms: avg %.3f ms/frame, max %.3f ms -> %.0f fps capable\n",
           st.frames_decoded, st.frames_bad, st.keyframes, st.strips, t_total * 1e3,
           t_total * 1e3 / (i ? i : 1), t_max * 1e3, i / (t_total > 0 ? t_total : 1));
    printf("   parse: sega_skip=%d frame_size_mismatch=%d strip_size_mismatch=%d cb_slack=%d vec_slack=%d ts_nonmonotonic=%d\n",
           st.sega_skip, st.frame_size_mismatch, st.strip_size_mismatch, st.cb_slack, st.vector_slack, ts_bad);
    printf("   chunks: 20:%d 21:%d 22:%d 23:%d 24-27:%d/%d/%d/%d 30:%d 31:%d 32:%d other:%d\n",
           st.chunk_hist[0], st.chunk_hist[1], st.chunk_hist[2], st.chunk_hist[3], st.chunk_hist[4],
           st.chunk_hist[5], st.chunk_hist[6], st.chunk_hist[7], st.chunk_hist[8], st.chunk_hist[9],
           st.chunk_hist[10], st.chunk_hist[15]);
    if (ffmpeg) {
        if (ref_frames < 0)
            printf("   ffmpeg: produced MORE frames than we decoded\n");
        else if (sse_total == 0 && ref_frames == i)
            printf("   ffmpeg video: %ld/%d frames compared, all bit-exact (PSNR inf)\n", ref_frames, i);
        else
            printf("   ffmpeg video: %ld/%d frames compared, %ld differ, min PSNR %.2f dB, global PSNR %.2f dB\n",
                   ref_frames, i, ndiff_frames, min_psnr,
                   10 * log10(255.0 * 255.0 / (sse_total / ((double)ref_frames * w * h * 3 + 1e-9) + 1e-12)));
    }

    /* audio */
    int ch = Cpk_AudioChannels(m);
    long total = Cpk_AudioFrames(m), got = 0;
    int16_t *pcm = malloc(sizeof(int16_t) * (size_t)(total + 4096) * (ch ? ch : 1));
    double ta0 = now_s();
    int n;
    while (ch && (n = Cpk_ReadAudio(m, pcm + got * ch, 1000)) > 0) got += n; /* odd block size on purpose */
    double ta = now_s() - ta0;
    long peak = 0;
    for (long k = 0; k < got * ch; k++) if (labs(pcm[k]) > peak) peak = labs(pcm[k]);
    printf("   audio: read %ld/%ld frames in %.2f ms, peak %ld\n", got, total, ta * 1e3, peak);
    if (outdir && ch) {
        char p[1024];
        snprintf(p, sizeof p, "%s/%s.wav", outdir, name);
        write_wav(p, pcm, got, ch, Cpk_AudioRate(m));
    }
    if (ffmpeg && ch) {
        char cmd[2048];
        snprintf(cmd, sizeof cmd, "%s -v error -i '%s' -map 0:a -f s16le -", ffmpeg, path);
        FILE *fa = popen(cmd, "r");
        long k = 0, diff = 0;
        int16_t s;
        while (fa && fread(&s, 2, 1, fa) == 1) {
            if (k < got * ch && s != pcm[k]) diff++;
            k++;
        }
        if (fa) pclose(fa);
        printf("   ffmpeg audio: %ld samples vs ours %ld, %ld differ%s\n", k, got * ch, diff,
               (k == got * ch && !diff) ? " (bit-exact)" : "");
    }

    /* rewind sanity: first frame again must equal a fresh decode */
    Cpk_Rewind(m);
    if (Cpk_NextFrame(m, fb, &ts) != 1 || ts != 0) printf("   rewind: FAILED\n");

    free(pcm);
    free(fb);
    free(ref);
    Cpk_Close(m);
    free(mem);
    return st.frames_bad || st.frame_size_mismatch || st.strip_size_mismatch || got != total;
}

int main(int argc, char **argv) {
    const char *outdir = NULL, *ffmpeg = NULL;
    int dump = 60, use_mem = 0, fails = 0, files = 0;
    crc_init();
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-o") && i + 1 < argc) outdir = argv[++i];
        else if (!strcmp(argv[i], "-f") && i + 1 < argc) dump = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-F") && i + 1 < argc) ffmpeg = argv[++i];
        else if (!strcmp(argv[i], "-m")) use_mem = 1;
        else { fails += run(argv[i], outdir, dump, ffmpeg, use_mem); files++; }
    }
    if (!files) {
        fprintf(stderr, "usage: %s [-o outdir] [-f frame] [-F ffmpeg] [-m] file.CPK...\n", argv[0]);
        return 2;
    }
    printf("%d file(s), %d with problems\n", files, fails);
    return fails != 0;
}
