/*
 * cpk.c - Sega FILM (.CPK) demuxer + Cinepak video / PCM (and ADX) audio decoder.
 * See cpk.h for the format summary and API.
 *
 * Cinepak decoding follows the reference behaviour (bit-exact with libavcodec's
 * cinepak decoder in RGB24 mode):
 *   - frame header: u8 flags, u24 size, u16 w, u16 h, u16 strips (+2 Sega pad bytes)
 *   - flags bit0 clear: each strip after the first starts from a copy of the previous
 *     strip's codebooks; set: each strip keeps its own codebooks from the last frame
 *   - strip header: u8 id (0x10 key / 0x11 inter), u24 size, u16 y1, x1, y2, x2;
 *     y1 == 0 -> y2 is a height relative to the previous strip's bottom
 *   - chunks: u8 id, u24 size (incl. 4-byte header)
 *       0x20/0x22 V4/V1 codebook, 6-byte YUV entries, full
 *       0x21/0x23 same, selective (32-bit flag words, MSB first)
 *       0x24..0x27 same with 4-byte (luma only) entries
 *       0x30 intra vectors (flag bit per block: 1 = V4, 0 = V1)
 *       0x31 inter vectors (skip bit, then V4/V1 bit for coded blocks)
 *       0x32 V1-only vectors (one byte per block)
 *     the vector chunk terminates the strip.
 *   YUV -> RGB: r = Y + 2V, g = Y - U/2 - V, b = Y + 2U (U, V signed bytes), clipped.
 */
#include "cpk.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CPK_MAX_STRIPS 32

typedef struct CpkEntry {
    uint32_t offset, size, info1, info2;
} CpkEntry;

typedef struct CpkStrip {
    uint32_t v4[256][4]; /* ARGB, 4 pixels (2x2: TL, TR, BL, BR) */
    uint32_t v1[256][4]; /* ARGB, 4 colours each covering a 2x2 quadrant */
} CpkStrip;

struct CpkMovie {
    /* source */
    FILE *fp;
    const uint8_t *mem;
    size_t mem_size;
    uint32_t data_off;

    /* header */
    char version[5];
    int width, height;
    int pad_w, pad_h; /* rounded up to multiples of 4 */
    int bpp;
    int a_channels, a_bits, a_comp, a_rate;
    uint32_t time_base;
    int count;
    CpkEntry *tab;
    int *vidx;        /* STAB indices of video chunks */
    int nvideo;
    int *aidx;        /* STAB indices of audio chunks */
    int naudio;
    long audio_total; /* frames */
    int video_end_ticks;

    /* video state */
    int vcur;
    uint32_t *fb;     /* pad_w * pad_h ARGB */
    CpkStrip strips[CPK_MAX_STRIPS];
    uint8_t *vbuf;
    size_t vbuf_cap;
    int sega_skip;    /* -1 = not determined yet */

    /* audio state */
    int acur;
    int16_t *apcm;    /* decoded current chunk, interleaved */
    long apcm_frames, apcm_pos, apcm_cap;
    uint8_t *abuf;
    size_t abuf_cap;
    long apos;
    int32_t adx_coef[2];
    int32_t adx_hist[2][2];

    CpkStats st;
};

static const char *g_err = "";
const char *Cpk_LastError(void) { return g_err; }

static uint32_t rb32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static uint32_t rb24(const uint8_t *p) {
    return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
}
static unsigned rb16(const uint8_t *p) { return ((unsigned)p[0] << 8) | p[1]; }

static uint8_t clip8(int v) { return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v); }
static int16_t clip16(int32_t v) {
    return (int16_t)(v < -32768 ? -32768 : v > 32767 ? 32767 : v);
}
static uint32_t argb(int r, int g, int b) {
    return 0xFF000000u | ((uint32_t)clip8(r) << 16) | ((uint32_t)clip8(g) << 8) | clip8(b);
}

/* ------------------------------------------------------------------------ */
/* I/O                                                                      */

/* Returns pointer to chunk bytes (into memory, or into *buf after reading). */
static const uint8_t *read_chunk(CpkMovie *m, const CpkEntry *e, uint8_t **buf, size_t *cap) {
    uint64_t off = (uint64_t)m->data_off + e->offset;
    if (m->mem) {
        if (off + e->size > m->mem_size) return NULL;
        return m->mem + off;
    }
    if (*cap < e->size || !*buf) {
        size_t nc = e->size + 4096;
        uint8_t *nb = (uint8_t *)realloc(*buf, nc);
        if (!nb) return NULL;
        *buf = nb;
        *cap = nc;
    }
    if (fseek(m->fp, (long)off, SEEK_SET) != 0) return NULL;
    if (e->size && fread(*buf, 1, e->size, m->fp) != e->size) return NULL;
    return *buf;
}

/* ------------------------------------------------------------------------ */
/* ADX (CRI) - not used by Shinobi Legions; implemented after libavcodec.    */

static double cpk_cos(double x) { /* |x| small (<= pi) */
    double x2 = x * x, term = 1.0, sum = 1.0;
    for (int i = 1; i < 20; i++) {
        term *= -x2 / ((2.0 * i - 1.0) * (2.0 * i));
        sum += term;
    }
    return sum;
}
static double cpk_sqrt(double v) {
    if (v <= 0) return 0;
    double r = v > 1 ? v : 1;
    for (int i = 0; i < 60; i++) r = 0.5 * (r + v / r);
    return r;
}
static int32_t cpk_lrint(double v) { return (int32_t)(v < 0 ? v - 0.5 : v + 0.5); }

static void adx_init(CpkMovie *m) {
    const double PI = 3.14159265358979323846, SQRT2 = 1.41421356237309504880;
    double a = SQRT2 - cpk_cos(2.0 * PI * 500.0 / (m->a_rate ? m->a_rate : 22050));
    double b = SQRT2 - 1.0;
    double c = (a - cpk_sqrt((a + b) * (a - b))) / b;
    m->adx_coef[0] = cpk_lrint(c * 2.0 * 4096);
    m->adx_coef[1] = cpk_lrint(-(c * c) * 4096);
    memset(m->adx_hist, 0, sizeof m->adx_hist);
}

static void adx_block(CpkMovie *m, int ch, const uint8_t *in, int16_t *out, int stride) {
    int32_t scale = (int32_t)rb16(in);
    int32_t s1 = m->adx_hist[ch][0], s2 = m->adx_hist[ch][1];
    if (scale & 0x8000) { /* end marker: silence */
        for (int i = 0; i < 32; i++) out[i * stride] = 0;
        return;
    }
    for (int i = 0; i < 32; i++) {
        int nib = (i & 1) ? (in[2 + i / 2] & 15) : (in[2 + i / 2] >> 4);
        int d = nib >= 8 ? nib - 16 : nib;
        int32_t s0 = d * scale + ((m->adx_coef[0] * s1 + m->adx_coef[1] * s2) >> 12);
        s2 = s1;
        s1 = clip16(s0);
        out[i * stride] = (int16_t)s1;
    }
    m->adx_hist[ch][0] = s1;
    m->adx_hist[ch][1] = s2;
}

static long audio_chunk_frames(const CpkMovie *m, uint32_t size) {
    int ch = m->a_channels;
    if (ch <= 0) return 0;
    if (m->a_comp == CPK_AUDIO_ADX) return (long)(size / (18u * ch)) * 32;
    if (m->a_bits == 8) return (long)(size / ch);
    return (long)(size / (2u * ch));
}

/* ------------------------------------------------------------------------ */
/* open / close                                                             */

static CpkMovie *open_common(CpkMovie *m) {
    uint8_t h[64];
    if (m->mem) {
        if (m->mem_size < 64) { g_err = "file too small"; goto fail; }
        memcpy(h, m->mem, 64);
    } else {
        if (fread(h, 1, 64, m->fp) != 64) { g_err = "file too small"; goto fail; }
    }
    if (memcmp(h, "FILM", 4) != 0) { g_err = "not a FILM file"; goto fail; }
    m->data_off = rb32(h + 4);
    memcpy(m->version, h + 8, 4);
    m->version[4] = 0;
    if (memcmp(h + 16, "FDSC", 4) != 0) { g_err = "no FDSC chunk"; goto fail; }
    uint32_t fdsc_len = rb32(h + 20);
    if (fdsc_len < 0x1C || fdsc_len > 0x100) { g_err = "bad FDSC length"; goto fail; }
    if (memcmp(h + 24, "cvid", 4) != 0) { g_err = "video codec is not cvid"; goto fail; }
    m->height = (int)rb32(h + 28);
    m->width = (int)rb32(h + 32);
    m->bpp = h[36];
    m->a_channels = h[37];
    m->a_bits = h[38];
    m->a_comp = h[39];
    m->a_rate = (int)rb16(h + 40);
    if (m->width <= 0 || m->height <= 0 || m->width > 4096 || m->height > 4096) {
        g_err = "bad dimensions";
        goto fail;
    }
    if (m->a_channels > 2) { g_err = "unsupported audio channel count"; goto fail; }
    if (m->a_channels && m->a_comp != CPK_AUDIO_ADX &&
        !(m->a_comp == CPK_AUDIO_PCM && (m->a_bits == 8 || m->a_bits == 16))) {
        g_err = "unsupported audio format";
        goto fail;
    }

    uint32_t st = 16 + fdsc_len;
    uint8_t sh[16];
    if (m->mem) {
        if (st + 16 > m->mem_size) { g_err = "truncated"; goto fail; }
        memcpy(sh, m->mem + st, 16);
    } else {
        if (fseek(m->fp, (long)st, SEEK_SET) || fread(sh, 1, 16, m->fp) != 16) {
            g_err = "truncated";
            goto fail;
        }
    }
    if (memcmp(sh, "STAB", 4) != 0) { g_err = "no STAB chunk"; goto fail; }
    m->time_base = rb32(sh + 8);
    uint32_t cnt = rb32(sh + 12);
    if (!m->time_base) { g_err = "zero time base"; goto fail; }
    if (cnt > 1000000 || st + 16 + (uint64_t)cnt * 16 > m->data_off) {
        g_err = "bad STAB entry count";
        goto fail;
    }
    m->count = (int)cnt;
    m->tab = (CpkEntry *)malloc(sizeof(CpkEntry) * (cnt ? cnt : 1));
    m->vidx = (int *)malloc(sizeof(int) * (cnt ? cnt : 1));
    m->aidx = (int *)malloc(sizeof(int) * (cnt ? cnt : 1));
    uint8_t *raw = (uint8_t *)malloc((size_t)cnt * 16 + 1);
    if (!m->tab || !m->vidx || !m->aidx || !raw) { free(raw); g_err = "out of memory"; goto fail; }
    if (m->mem) {
        memcpy(raw, m->mem + st + 16, (size_t)cnt * 16);
    } else if (cnt && fread(raw, 16, cnt, m->fp) != cnt) {
        free(raw);
        g_err = "truncated STAB";
        goto fail;
    }
    uint64_t file_size = m->mem_size;
    if (!m->mem) {
        fseek(m->fp, 0, SEEK_END);
        file_size = (uint64_t)ftell(m->fp);
    }
    for (uint32_t i = 0; i < cnt; i++) {
        CpkEntry *e = &m->tab[i];
        e->offset = rb32(raw + i * 16);
        e->size = rb32(raw + i * 16 + 4);
        e->info1 = rb32(raw + i * 16 + 8);
        e->info2 = rb32(raw + i * 16 + 12);
        if ((uint64_t)m->data_off + e->offset + e->size > file_size) {
            free(raw);
            g_err = "STAB entry points past end of file";
            goto fail;
        }
        if (e->info1 == 0xFFFFFFFFu) {
            m->aidx[m->naudio++] = (int)i;
            m->audio_total += audio_chunk_frames(m, e->size);
        } else {
            m->vidx[m->nvideo++] = (int)i;
            int end = (int)((e->info1 & 0x7FFFFFFFu) + e->info2);
            if (end > m->video_end_ticks) m->video_end_ticks = end;
        }
    }
    free(raw);
    if (!m->a_channels) m->naudio = 0, m->audio_total = 0;

    m->pad_w = (m->width + 3) & ~3;
    m->pad_h = (m->height + 3) & ~3;
    m->fb = (uint32_t *)malloc((size_t)m->pad_w * m->pad_h * 4);
    if (!m->fb) { g_err = "out of memory"; goto fail; }
    if (m->a_comp == CPK_AUDIO_ADX) adx_init(m);
    Cpk_Rewind(m);
    return m;
fail:
    Cpk_Close(m);
    return NULL;
}

CpkMovie *Cpk_Open(const char *path) {
    CpkMovie *m = (CpkMovie *)calloc(1, sizeof *m);
    if (!m) { g_err = "out of memory"; return NULL; }
    m->fp = fopen(path, "rb");
    if (!m->fp) { free(m); g_err = "cannot open file"; return NULL; }
    return open_common(m);
}

CpkMovie *Cpk_OpenMem(const void *data, size_t size) {
    if (!data) { g_err = "null buffer"; return NULL; }
    CpkMovie *m = (CpkMovie *)calloc(1, sizeof *m);
    if (!m) { g_err = "out of memory"; return NULL; }
    m->mem = (const uint8_t *)data;
    m->mem_size = size;
    return open_common(m);
}

void Cpk_Close(CpkMovie *m) {
    if (!m) return;
    if (m->fp) fclose(m->fp);
    free(m->tab);
    free(m->vidx);
    free(m->aidx);
    free(m->fb);
    free(m->vbuf);
    free(m->abuf);
    free(m->apcm);
    free(m);
}

void Cpk_Rewind(CpkMovie *m) {
    m->vcur = 0;
    m->acur = 0;
    m->apos = 0;
    m->apcm_frames = m->apcm_pos = 0;
    m->sega_skip = -1;
    memset(m->strips, 0, sizeof m->strips);
    for (size_t i = 0, n = (size_t)m->pad_w * m->pad_h; i < n; i++) m->fb[i] = 0xFF000000u;
    memset(m->adx_hist, 0, sizeof m->adx_hist);
}

/* ------------------------------------------------------------------------ */
/* info                                                                     */

int Cpk_Width(const CpkMovie *m) { return m->width; }
int Cpk_Height(const CpkMovie *m) { return m->height; }
int Cpk_FrameCount(const CpkMovie *m) { return m->nvideo; }
int Cpk_TimeBase(const CpkMovie *m) { return (int)m->time_base; }
double Cpk_FrameRate(const CpkMovie *m) {
    if (!m->video_end_ticks) return 0.0;
    return (double)m->nvideo * m->time_base / m->video_end_ticks;
}
int Cpk_DurationMs(const CpkMovie *m) {
    long v = (long)((int64_t)m->video_end_ticks * 1000 / m->time_base);
    long a = m->a_rate ? (long)((int64_t)m->audio_total * 1000 / m->a_rate) : 0;
    return (int)(v > a ? v : a);
}
int Cpk_AudioRate(const CpkMovie *m) { return m->a_channels ? m->a_rate : 0; }
int Cpk_AudioChannels(const CpkMovie *m) { return m->a_channels; }
int Cpk_AudioBits(const CpkMovie *m) { return m->a_bits; }
int Cpk_AudioCompression(const CpkMovie *m) { return m->a_comp; }
long Cpk_AudioFrames(const CpkMovie *m) { return m->audio_total; }
long Cpk_AudioPosition(const CpkMovie *m) { return m->apos; }
int Cpk_FrameIndex(const CpkMovie *m) { return m->vcur; }
int Cpk_SampleCount(const CpkMovie *m) { return m->count; }
uint32_t Cpk_DataOffset(const CpkMovie *m) { return m->data_off; }
const char *Cpk_Version(const CpkMovie *m) { return m->version; }
void Cpk_GetStats(const CpkMovie *m, CpkStats *out) {
    *out = m->st;
    out->sega_skip = m->sega_skip;
}
int Cpk_GetSample(const CpkMovie *m, int i, uint32_t *offset, uint32_t *size, uint32_t *info1,
                  uint32_t *info2) {
    if (i < 0 || i >= m->count) return 0;
    if (offset) *offset = m->tab[i].offset;
    if (size) *size = m->tab[i].size;
    if (info1) *info1 = m->tab[i].info1;
    if (info2) *info2 = m->tab[i].info2;
    return 1;
}

static int ticks_to_ms(const CpkMovie *m, uint32_t t) {
    return (int)((int64_t)t * 1000 / m->time_base);
}
int Cpk_PeekFrameTime(const CpkMovie *m) {
    if (m->vcur >= m->nvideo) return -1;
    return ticks_to_ms(m, m->tab[m->vidx[m->vcur]].info1 & 0x7FFFFFFFu);
}

/* ------------------------------------------------------------------------ */
/* Cinepak                                                                  */

static void hist(CpkMovie *m, int id) {
    int k = (id >= 0x20 && id <= 0x27) ? id - 0x20 : (id >= 0x30 && id <= 0x32) ? id - 0x30 + 8 : 15;
    m->st.chunk_hist[k]++;
}

static void decode_codebook(CpkMovie *m, uint32_t (*cb)[4], int id, const uint8_t *d, int size) {
    const uint8_t *eod = d + size;
    int n = (id & 0x04) ? 4 : 6;
    int selective = id & 0x01;
    uint32_t flag = 0, mask = 0;
    for (int i = 0; i < 256; i++) {
        if (selective && !(mask >>= 1)) {
            if (d + 4 > eod) break;
            flag = rb32(d);
            d += 4;
            mask = 0x80000000u;
        }
        if (!selective || (flag & mask)) {
            if (d + n > eod) break;
            if (n == 4) {
                for (int k = 0; k < 4; k++) cb[i][k] = argb(d[k], d[k], d[k]);
            } else {
                int u = (int8_t)d[4], v = (int8_t)d[5];
                int dr = v * 2, dg = -(u / 2) - v, db = u * 2;
                for (int k = 0; k < 4; k++) cb[i][k] = argb(d[k] + dr, d[k] + dg, d[k] + db);
            }
            d += n;
        }
    }
    if (eod - d > 3) m->st.cb_slack++;
}

static int decode_vectors(CpkMovie *m, CpkStrip *s, int id, const uint8_t *d, int size, int x1,
                          int y1, int x2, int y2) {
    const uint8_t *eod = d + size;
    uint32_t flag = 0, mask = 0;
    const int W = m->pad_w;
    for (int y = y1; y < y2; y += 4) {
        uint32_t *r0 = m->fb + (size_t)y * W;
        uint32_t *r1 = r0 + W, *r2 = r1 + W, *r3 = r2 + W;
        for (int x = x1; x < x2; x += 4) {
            if ((id & 0x01) && !(mask >>= 1)) {
                if (d + 4 > eod) return -1;
                flag = rb32(d);
                d += 4;
                mask = 0x80000000u;
            }
            if (!(id & 0x01) || (flag & mask)) {
                if (!(id & 0x02) && !(mask >>= 1)) {
                    if (d + 4 > eod) return -1;
                    flag = rb32(d);
                    d += 4;
                    mask = 0x80000000u;
                }
                if ((id & 0x02) || (~flag & mask)) {
                    if (d >= eod) return -1;
                    const uint32_t *c = s->v1[*d++];
                    r0[x] = r0[x + 1] = r1[x] = r1[x + 1] = c[0];
                    r0[x + 2] = r0[x + 3] = r1[x + 2] = r1[x + 3] = c[1];
                    r2[x] = r2[x + 1] = r3[x] = r3[x + 1] = c[2];
                    r2[x + 2] = r2[x + 3] = r3[x + 2] = r3[x + 3] = c[3];
                } else if (flag & mask) {
                    if (d + 4 > eod) return -1;
                    const uint32_t *c0 = s->v4[d[0]], *c1 = s->v4[d[1]];
                    const uint32_t *c2 = s->v4[d[2]], *c3 = s->v4[d[3]];
                    d += 4;
                    r0[x] = c0[0]; r0[x + 1] = c0[1]; r0[x + 2] = c1[0]; r0[x + 3] = c1[1];
                    r1[x] = c0[2]; r1[x + 1] = c0[3]; r1[x + 2] = c1[2]; r1[x + 3] = c1[3];
                    r2[x] = c2[0]; r2[x + 1] = c2[1]; r2[x + 2] = c3[0]; r2[x + 3] = c3[1];
                    r3[x] = c2[2]; r3[x + 1] = c2[3]; r3[x + 2] = c3[2]; r3[x + 3] = c3[3];
                }
            }
        }
    }
    if (eod - d > 3) m->st.vector_slack++;
    return 0;
}

static int decode_strip(CpkMovie *m, CpkStrip *s, const uint8_t *d, int size, int x1, int y1,
                        int x2, int y2) {
    const uint8_t *eod = d + size;
    if (x1 < 0 || y1 < 0 || x2 > m->pad_w || y2 > m->pad_h || x1 >= x2 || y1 >= y2 || (x1 & 3) ||
        (y1 & 3))
        return -1;
    /* blocks are 4x4: round the far edges up inside the padded buffer */
    x2 = (x2 + 3) & ~3;
    y2 = (y2 + 3) & ~3;
    while (d + 4 <= eod) {
        int id = d[0];
        int csize = (int)rb24(d + 1) - 4;
        if (csize < 0) return -1;
        d += 4;
        if (d + csize > eod) {
            m->st.strip_size_mismatch++;
            csize = (int)(eod - d);
        }
        hist(m, id);
        switch (id) {
        case 0x20: case 0x21: case 0x24: case 0x25:
            decode_codebook(m, s->v4, id, d, csize);
            break;
        case 0x22: case 0x23: case 0x26: case 0x27:
            decode_codebook(m, s->v1, id, d, csize);
            break;
        case 0x30: case 0x31: case 0x32:
            if (d + csize != eod) m->st.strip_size_mismatch++;
            return decode_vectors(m, s, id, d, csize, x1, y1, x2, y2);
        default:
            break;
        }
        d += csize;
    }
    return -1; /* no vector chunk */
}

static int decode_frame(CpkMovie *m, const uint8_t *d, int size) {
    const uint8_t *eod = d + size;
    if (size < 10) return -1;
    int flags = d[0];
    int enc_size = (int)rb24(d + 1);
    int nstrips = (int)rb16(d + 8);
    if (m->sega_skip < 0) {
        /* Sega FILM quirk (same heuristic as libavcodec): the Cinepak size field does
         * not match the container size -> 2 (or 6) extra bytes follow the header. */
        if (enc_size != size && enc_size && (size % enc_size) != 0)
            m->sega_skip = (size >= 16 && d[10] == 0xFE && d[11] == 0 && d[12] == 0 &&
                            d[13] == 6 && d[14] == 0 && d[15] == 0)
                               ? 6
                               : 2;
        else
            m->sega_skip = 0;
    }
    d += 10 + m->sega_skip;
    if (nstrips > CPK_MAX_STRIPS) nstrips = CPK_MAX_STRIPS;
    int y0 = 0;
    for (int i = 0; i < nstrips; i++) {
        if (d + 12 > eod) return -1;
        int y1 = (int)rb16(d + 4), x1 = (int)rb16(d + 6);
        int y2 = (int)rb16(d + 8), x2 = (int)rb16(d + 10);
        if (!y1) { /* relative to previous strip */
            y1 = y0;
            y2 += y0;
        }
        int ssize = (int)rb24(d + 1) - 12;
        if (ssize < 0) return -1;
        d += 12;
        if (d + ssize > eod) {
            m->st.frame_size_mismatch++;
            ssize = (int)(eod - d);
        }
        if (i > 0 && !(flags & 0x01)) {
            memcpy(&m->strips[i], &m->strips[i - 1], sizeof(CpkStrip));
        }
        m->st.strips++;
        if (decode_strip(m, &m->strips[i], d, ssize, x1, y1, x2, y2) != 0) return -1;
        d += ssize;
        y0 = y2;
    }
    if (d != eod) m->st.frame_size_mismatch++;
    return 0;
}

static void copy_out(const CpkMovie *m, uint32_t *out) {
    if (!out) return;
    for (int y = 0; y < m->height; y++)
        memcpy(out + (size_t)y * m->width, m->fb + (size_t)y * m->pad_w, (size_t)m->width * 4);
}

int Cpk_NextFrame(CpkMovie *m, uint32_t *out, int *timestamp_ms) {
    if (m->vcur >= m->nvideo) return 0;
    const CpkEntry *e = &m->tab[m->vidx[m->vcur++]];
    if (timestamp_ms) *timestamp_ms = ticks_to_ms(m, e->info1 & 0x7FFFFFFFu);
    if (!(e->info1 & 0x80000000u)) m->st.keyframes++;
    int rc = -1;
    const uint8_t *d = read_chunk(m, e, &m->vbuf, &m->vbuf_cap);
    if (d) rc = decode_frame(m, d, (int)e->size);
    if (rc == 0) m->st.frames_decoded++;
    else m->st.frames_bad++;
    copy_out(m, out);
    return rc == 0 ? 1 : -1;
}

/* ------------------------------------------------------------------------ */
/* audio                                                                    */

static int decode_audio_chunk(CpkMovie *m) {
    while (m->acur < m->naudio) {
        const CpkEntry *e = &m->tab[m->aidx[m->acur++]];
        long frames = audio_chunk_frames(m, e->size);
        if (frames <= 0) continue;
        const uint8_t *d = read_chunk(m, e, &m->abuf, &m->abuf_cap);
        if (!d) continue;
        int ch = m->a_channels;
        if (m->apcm_cap < frames * ch) {
            int16_t *np = (int16_t *)realloc(m->apcm, sizeof(int16_t) * (size_t)frames * ch);
            if (!np) return 0;
            m->apcm = np;
            m->apcm_cap = frames * ch;
        }
        int16_t *o = m->apcm;
        if (m->a_comp == CPK_AUDIO_ADX) {
            /* blocks of 18 bytes, channels interleaved per block */
            long nblk = frames / 32;
            for (long b = 0; b < nblk; b++)
                for (int c = 0; c < ch; c++)
                    adx_block(m, c, d + (b * ch + c) * 18, o + b * 32 * ch + c, ch);
        } else if (m->a_bits == 8) { /* signed 8-bit, planar */
            for (int c = 0; c < ch; c++)
                for (long i = 0; i < frames; i++)
                    o[i * ch + c] = (int16_t)((int8_t)d[c * frames + i] * 256);
        } else { /* signed 16-bit big-endian, planar */
            for (int c = 0; c < ch; c++) {
                const uint8_t *p = d + (size_t)c * frames * 2;
                for (long i = 0; i < frames; i++)
                    o[i * ch + c] = (int16_t)(uint16_t)((p[2 * i] << 8) | p[2 * i + 1]);
            }
        }
        m->apcm_frames = frames;
        m->apcm_pos = 0;
        return 1;
    }
    return 0;
}

int Cpk_ReadAudio(CpkMovie *m, int16_t *out, int maxSamples) {
    int ch = m->a_channels, done = 0;
    if (!ch) return 0;
    while (done < maxSamples) {
        if (m->apcm_pos >= m->apcm_frames && !decode_audio_chunk(m)) break;
        long n = m->apcm_frames - m->apcm_pos;
        if (n > maxSamples - done) n = maxSamples - done;
        memcpy(out + (size_t)done * ch, m->apcm + m->apcm_pos * ch, sizeof(int16_t) * (size_t)n * ch);
        m->apcm_pos += n;
        done += (int)n;
    }
    m->apos += done;
    return done;
}
