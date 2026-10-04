/* SCSP sound banks / SBL sound path (src/sfx.c), no SDL.
 * cd pc-port && S=$(cat tests/SOURCES.txt) && gcc -std=c11 -Wall -Wextra -O1 -Isrc -Itests \
 *     tests/test_sfx.c $S -lm -o /tmp/test_sfx && /tmp/test_sfx ../extracted/ [demo_dir]
 * Parses every .SND, walks every song, plays every id, checks the 68K driver arithmetic
 * against the driver's own tables (A.BIN), the SH-2 queue / slot rules, fades, mono, levels
 * (no clipping) and the loop timing; renders SFX to /tmp/sfx_<bank>_<id>.wav and, with
 * demo_dir, a few listening WAVs (docs/sound.md section 7). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include "sfx.h"
#include "audio.h"
#include "input.h"

static InputPort s_fake[2];
const InputPort *Input_GetPort(int p) { return &s_fake[p & 1]; }   /* pad.c link stub */

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static const char *k_files[10] = { "STAGE7.SND", "STAGE2.SND", "STAGE3.SND", "STAGE4.SND", "STAGE5.SND",
                                   "STAGE6.SND", "STAGE1.SND", "STAGE8.SND", "STAGE9.SND", "KAZUMA.SND" };

static void put16(FILE *f, unsigned v) { fputc((int)(v & 255), f); fputc((int)(v >> 8 & 255), f); }
static void put32(FILE *f, unsigned v) { put16(f, v & 0xFFFF); put16(f, v >> 16); }

static void write_wav(const char *path, const int16_t *s, int frames)
{
    FILE *f = fopen(path, "wb");
    int i;
    if (!f) { printf("  cannot write %s\n", path); return; }
    fwrite("RIFF", 1, 4, f); put32(f, 36u + (unsigned)frames * 4u); fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16); put16(f, 1); put16(f, 2); put32(f, SFX_RATE); put32(f, SFX_RATE * 4); put16(f, 4); put16(f, 16);
    fwrite("data", 1, 4, f); put32(f, (unsigned)frames * 4u);
    for (i = 0; i < frames * 2; i++) put16(f, (uint16_t)s[i]);
    fclose(f);
}

static int s_peak32;    /* largest |sample| before clipping since the last reset */

/* renders k frames into buf (16-bit, clipped), tracking the unclipped peak */
static void render(int16_t *buf, int k)
{
    static int32_t acc[2048];
    int i;
    while (k > 0) {
        int n = k > 1024 ? 1024 : k;
        memset(acc, 0, sizeof acc);
        Sfx_Render(acc, n);
        for (i = 0; i < n * 2; i++) {
            int32_t v = acc[i];
            if (abs(v) > s_peak32) s_peak32 = abs(v);
            if (buf) buf[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
        }
        if (buf) buf += n * 2;
        k -= n;
    }
}

static int peak16(const int16_t *s, int frames)
{
    int i, p = 0;
    for (i = 0; i < frames * 2; i++) if (abs(s[i]) > p) p = abs(s[i]);
    return p;
}

/* Renders id until the request ran and the sequence and its voices are done (max `maxsec`).
 * Returns frames, *peak = max |sample|. */
static int render_id(int id, double maxsec, int16_t **out, int *peak)
{
    int cap = (int)(maxsec * SFX_RATE), n = 0, chunk = 512;
    int16_t *buf = (int16_t *)malloc((size_t)cap * 4);
    *peak = 0;
    if (!Sfx_Play(id)) { free(buf); *out = NULL; return 0; }
    while (n < cap) {
        int k = cap - n < chunk ? cap - n : chunk;
        render(buf + n * 2, k);
        n += k;
        if (!Sfx_Pending() && !Sfx_ActiveSlots() && !Sfx_ActiveVoices()) break;
    }
    *peak = peak16(buf, n);
    *out = buf;
    return n;
}

static void settle(void)        /* stop everything, let the scheduler run and the tails end */
{
    Sfx_StopAll();
    render(NULL, SFX_FRAME_SAMPLES * 2);
    Sfx_SetSeqVolume(0, 127, 0);
    render(NULL, SFX_FRAME_SAMPLES * 2);
}

/* Walks a song's events like the driver (format check: every song must end with 0x83).
 * *maxvel = loudest note velocity (unused SE slots are a single velocity-2 note). */
static int walk_song(const uint8_t *song, uint32_t len, int *maxvel)
{
    uint32_t data = (uint32_t)(song[4] << 8 | song[5]), p = data;
    int n = 0;
    *maxvel = 0;
    while (p < len && n < 100000) {
        uint8_t st = song[p];
        if (st < 0x80) { if (p + 2 < len && song[p + 2] > *maxvel) *maxvel = song[p + 2]; p += 5; }
        else if ((st >> 4) == 0xB || (st >> 4) == 0xA) p += 4;
        else if ((st >> 4) == 0xC || (st >> 4) == 0xD || (st >> 4) == 0xE) p += 3;
        else if (st == 0x81) { if ((uint32_t)(data + (song[p + 1] << 8 | song[p + 2])) >= len) return -1; p += 4; }
        else if (st == 0x82) p += 2;
        else if (st == 0x83) return n;
        else if (st >= 0x88) p += 1;
        else return -1;
        n++;
    }
    return -1;
}

static uint8_t *load_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    uint8_t *b;
    long n;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    b = (uint8_t *)malloc((size_t)n);
    if (b && fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); b = NULL; }
    fclose(f);
    *len = (size_t)n;
    return b;
}

/* the 68K driver's own tables (A.BIN 0x0600F318 + 0x35D6 / + 0x3696) vs Sfx_PitchReg */
static void check_driver_tables(const char *dir)
{
    char path[600];
    size_t len;
    uint8_t *a;
    int i, bad = 0;
    snprintf(path, sizeof path, "%s/A.BIN", dir);
    a = load_file(path, &len);
    if (!a || len < 0xF318 - 0x4000 + 0x50CE) { printf("skip driver tables (no A.BIN)\n"); free(a); return; }
    {
        const uint8_t *drv = a + (0x0600F318 - 0x06004000), *oct = drv + 0x35D6, *fns = drv + 0x3696;
        CHECK(drv[0x400] == 0 && drv[0x404 + 3] == 0x00 && drv[2] == 0xA0);    /* 68K vectors: SP 0xA000 */
        for (i = 0; i < 0xC0; i++) {            /* key - base + 0x60 = i, offset 0 */
            int key = i, base = 0x60, n = (oct[i] & 15) << 7;
            int f = fns[n] + (n >= 0x1EF) * 0x100 + (n >= 0x383) * 0x100 + (n >= 0x4D9) * 0x100;
            uint16_t want = (uint16_t)((oct[i] >> 4) << 11 | f);
            if (Sfx_PitchReg(key, base, 0, 0) != want) bad++;
        }
        for (i = 0; i < 0x600; i++) {           /* every FNS entry through the fraction byte */
            int semi = i >> 7, frac = (i & 127) << 1;
            int f = fns[i] + (i >= 0x1EF) * 0x100 + (i >= 0x383) * 0x100 + (i >= 0x4D9) * 0x100;
            if ((Sfx_PitchReg(0x60 + semi, 0x60, 0, frac) & 0x3FF) != f) bad++;
        }
        printf("driver pitch tables: %d mismatches\n", bad);
        CHECK(bad == 0);
    }
    free(a);
}

/* sample-exact loop check: the tune's events repeat every loop length of the tempo table;
 * compares the audio of two consecutive loop iterations (normalised correlation) */
static double loop_corr(const int16_t *s, int frames, int a, int len)
{
    enum { W = 441 };           /* 10 ms RMS windows: robust to the 2 ms event quantisation */
    double xy = 0, xx = 0, yy = 0, mx = 0, my = 0;
    int i, k, nw = len / W;
    double *ex, *ey;
    if (a + 2 * len > frames || nw < 2) return -2;
    ex = (double *)malloc(sizeof(double) * (size_t)nw);
    ey = (double *)malloc(sizeof(double) * (size_t)nw);
    for (i = 0; i < nw; i++) {
        double sx = 0, sy = 0;
        for (k = 0; k < W * 2; k++) {
            double x = s[(a + i * W) * 2 + k], y = s[(a + len + i * W) * 2 + k];
            sx += x * x; sy += y * y;
        }
        ex[i] = sqrt(sx / (W * 2)); ey[i] = sqrt(sy / (W * 2));
        mx += ex[i]; my += ey[i];
    }
    mx /= nw; my /= nw;
    for (i = 0; i < nw; i++) {
        double x = ex[i] - mx, y = ey[i] - my;
        xy += x * y; xx += x * x; yy += y * y;
    }
    free(ex); free(ey);
    return xx > 0 && yy > 0 ? xy / sqrt(xx * yy) : -1;
}

static void demos(const char *dir, const char *out)
{
    char path[700];
    SfxBank *b;
    int16_t *buf = (int16_t *)malloc((size_t)SFX_RATE * 22 * 4);
    int n, i;
    snprintf(path, sizeof path, "%s/STAGE7.SND", dir);
    if (!(b = SfxBank_Load(path)) || !buf) { free(buf); return; }
    Sfx_SetBank(b);
#define DEMO(name, frames) do { snprintf(path, sizeof path, "%s/%s", out, name); write_wav(path, buf, frames); \
        printf("  demo %-28s %5.1f s peak %d\n", name, (frames) / (double)SFX_RATE, peak16(buf, frames)); } while (0)
    /* 1-1 act 1 BGM (id 3), 20 s */
    Sfx_Play(0x003); render(buf, SFX_RATE * 20); DEMO("bgm_1-1_act1.wav", SFX_RATE * 20); settle();
    Sfx_Play(0x004); render(buf, SFX_RATE * 20); DEMO("bgm_1-1_act2.wav", SFX_RATE * 20); settle();
    Sfx_Play(0x000); render(buf, SFX_RATE * 8); DEMO("jingle_stage_start.wav", SFX_RATE * 8); settle();
    Sfx_Play(0x001); render(buf, SFX_RATE * 10); DEMO("jingle_stage_clear.wav", SFX_RATE * 10); settle();
    {   /* sound effects, one every 0.7 s */
        static const int ids[] = { 0x108, 0x10C, 0x104, 0x105, 0x111, 0x107, 0x116, 0x125, 0x126,
                                   0x200, 0x201, 0x202, 0x203, 0x205, 0x206, 0x207, 0x209, 0x20A };
        int16_t *p = buf;
        for (i = 0; i < (int)(sizeof ids / sizeof ids[0]); i++) { Sfx_Play(ids[i]); render(p, SFX_RATE * 7 / 10); p += SFX_RATE * 7 / 10 * 2; }
        n = (int)(p - buf) / 2;
        DEMO("se_sequence.wav", n);
        settle();
    }
    /* the game's music fade (0x0607F8C2: 0x06008154(0, 0, 0x80)) after 4 s */
    Sfx_Play(0x003); render(buf, SFX_RATE * 4);
    Sfx_SetSeqVolume(0, 0, 0x80);
    render(buf + SFX_RATE * 4 * 2, SFX_RATE * 16);
    DEMO("bgm_fade_out_0x80.wav", SFX_RATE * 20);
    settle();
    /* stereo 8 s, then the driver mono flag (options AUDIO = MONO) */
    Sfx_Play(0x003); render(buf, SFX_RATE * 8);
    Sfx_SetMono(1); render(buf + SFX_RATE * 8 * 2, SFX_RATE * 8); Sfx_SetMono(0);
    DEMO("bgm_stereo_then_mono.wav", SFX_RATE * 16);
    settle();
    SfxBank_Free(Sfx_SetBank(NULL));
    snprintf(path, sizeof path, "%s/KAZUMA.SND", dir);
    if ((b = SfxBank_Load(path))) {
        Sfx_SetBank(b);
        Sfx_Play(0x003); render(buf, SFX_RATE * 20); DEMO("bgm_9-2_kazuma.wav", SFX_RATE * 20); settle();
        SfxBank_Free(Sfx_SetBank(NULL));
    }
#undef DEMO
    free(buf);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "../extracted/";
    char path[600];
    int f, loaded = 0, maxpeak = 0;
    SfxDef d;

    /* id table (A.BIN 0x0600EF20 / EF50 / F0D0) */
    CHECK(Sfx_Lookup(0x001, &d) && d.seq_bank == 2 && d.song == 1 && d.music);
    CHECK(Sfx_Lookup(0x108, &d) && d.seq_bank == 0 && d.song == 7 && d.prio == 16 && !d.music);
    CHECK(Sfx_Lookup(0x107, &d) && d.seq_bank == 0 && d.song == 6 && d.prio == 4);
    CHECK(Sfx_Lookup(0x116, &d) && d.seq_bank == 1 && d.song == 0);
    CHECK(Sfx_Lookup(0x237, &d) && d.seq_bank == 3 && d.song == 55);
    CHECK(!Sfx_Lookup(0x238, &d) && !Sfx_Lookup(-1, &d) && !Sfx_Lookup(6, &d));

    /* driver arithmetic */
    {
        static const uint8_t vt[10] = { 0x0A, 0x7F, 0x7F, 0x08, 0x7F, 0x7F, 0x08, 0x7F, 0x7F, 0x08 };
        static const uint8_t vt2[10] = { 0x11, 0x40, 0x60, 0x00, 0x7F, 0x7F, 0x00, 0x7F, 0x7F, 0x00 };
        int v, ok = 1;
        for (v = 0; v < 128; v++) ok &= Sfx_VelocityCurve(vt, v) == v;  /* the game's table */
        CHECK(ok);
        CHECK(Sfx_VelocityCurve(vt2, 20) == 40 && Sfx_VelocityCurve(vt2, 64) == 0x7F);  /* d << 1, clamp */
        CHECK(Sfx_VelocityCurve(vt2, 100) == 0x60);
        CHECK(Sfx_KeyOnTL(127, 0, 127, 0) == 4);        /* full scale: -1.5 dB */
        CHECK(Sfx_KeyOnTL(100, 0, 100, 0) == 100);      /* (200 x 255 x 200) >> 16 = 155 */
        CHECK(Sfx_KeyOnTL(115, 0, 127, -10) == 255 - ((230 * 255 * 254 >> 16) - 10));
        CHECK(Sfx_KeyOnTL(0, 0, 127, 0) == 255);
        CHECK(Sfx_PitchReg(60, 60, 0, 0) == 0 && Sfx_PitchReg(72, 60, 0, 0) == 0x0800 && Sfx_PitchReg(48, 60, 0, 0) == 0x7800);
        CHECK(fabs(Sfx_PitchRatio(Sfx_PitchReg(67, 60, 0, 0)) - pow(2.0, 7 / 12.0)) < 0.001);
        CHECK(Sfx_PitchReg(60, 60, 111, 0) == Sfx_PitchReg(60, 60, 0, 111));   /* fine = 1/256 semitone */
        CHECK(fabs(Sfx_PitchRatio(Sfx_PitchReg(60, 60, 111, 0)) - pow(2.0, 110 / 256.0 / 12.0)) < 0.0015);
        CHECK(fabs(Sfx_TLGain(16) - 0.5012f) < 0.001f && Sfx_TLGain(0) == 1.0f);
    }
    check_driver_tables(dir);

    for (f = 0; f < 10; f++) {
        SfxBank *b;
        int sb, tb, s, id, silent = 0, played = 0, songs_ok = 0, songs = 0, dummy = 0;
        snprintf(path, sizeof path, "%s/%s", dir, k_files[f]);
        b = SfxBank_Load(path);
        if (!b) { printf("skip %s (missing)\n", path); continue; }
        loaded++;
        CHECK(b->nmap == 8);
        CHECK(b->lz_window_refs == 0);
        CHECK(b->ndsp == 0);                            /* no DSP program / work area on the disc */
        for (sb = 0; sb < 4; sb++) {
            CHECK(b->seq[sb].data != NULL && b->seq[sb].size <= b->seq[sb].alloc);
            for (s = 0; s < SfxBank_SongCount(b, sb); s++) {
                const uint8_t *q = b->seq[sb].data;
                uint32_t off = (uint32_t)q[2 + 4 * s] << 24 | (uint32_t)q[3 + 4 * s] << 16 | (uint32_t)q[4 + 4 * s] << 8 | q[5 + 4 * s];
                int mv;
                songs++;
                if (off < b->seq[sb].size && walk_song(q + off, b->seq[sb].size - off, &mv) >= 0) songs_ok++;   /* (seq 1 songs 5/6 are empty) */
                else printf("  %s seq %d song %d: bad event stream (offset 0x%X)\n", k_files[f], sb, s, off);
            }
        }
        for (tb = 0; tb < 4; tb++) {
            const SfxArea *a = &b->tone[tb];
            int i, mixzero = 1;
            CHECK(a->data != NULL && SfxBank_VoiceCount(b, tb) > 0);
            /* mixer table (EFSDL/EFPAN of the 18 DSP outputs): all 0 = effect returns muted */
            for (i = 0; a->data && i < 18; i++) mixzero &= a->data[(a->data[0] << 8 | a->data[1]) + i] == 0;
            CHECK(mixzero);
        }
        CHECK(songs_ok == songs);
        printf("%-10s songs %d/%d (seq0 %d seq1 %d seq2 %d seq3 %d) voices %d/%d/%d/%d\n", k_files[f], songs_ok, songs,
               SfxBank_SongCount(b, 0), SfxBank_SongCount(b, 1), SfxBank_SongCount(b, 2), SfxBank_SongCount(b, 3),
               SfxBank_VoiceCount(b, 0), SfxBank_VoiceCount(b, 1), SfxBank_VoiceCount(b, 2), SfxBank_VoiceCount(b, 3));
        Sfx_SetBank(b);
        /* every SE id of the tables: must start and make sound (bank 3 ids only up to its song count) */
        for (id = 0x100; id < 0x238; id++) {
            int16_t *pcm;
            int peak, n;
            if (id == 0x130) id = 0x200;
            if (!Sfx_Lookup(id, &d) || d.song >= SfxBank_SongCount(b, d.seq_bank)) continue;
            {
                const uint8_t *q = b->seq[d.seq_bank].data;
                uint32_t off = (uint32_t)q[2 + 4 * d.song] << 24 | (uint32_t)q[3 + 4 * d.song] << 16 | (uint32_t)q[4 + 4 * d.song] << 8 | q[5 + 4 * d.song];
                int mv = 0;
                walk_song(q + off, b->seq[d.seq_bank].size - off, &mv);
                if (mv <= 4) { dummy++; continue; }      /* unused id: one velocity-2 note */
            }
            s_peak32 = 0;
            n = render_id(id, 4.0, &pcm, &peak);
            played++;
            if (s_peak32 > maxpeak) maxpeak = s_peak32;
            if (f == 0) printf("  id %03X seq %d/%2d: %.2f s peak %d\n", id, d.seq_bank, d.song, n / (double)SFX_RATE, peak);
            if (s_peak32 > 32767) printf("  %s id %03X clips: peak %d\n", k_files[f], id, s_peak32);
            if (peak < 64) { silent++; printf("  %s id %03X (seq %d/%d): silent\n", k_files[f], id, d.seq_bank, d.song); }
            if (f == 0 && pcm && (id <= 0x10C || id == 0x111 || id == 0x116 || id == 0x125 || id == 0x126 || (id >= 0x200 && id < 0x208))) {
                snprintf(path, sizeof path, "/tmp/sfx_%s_%03X.wav", "STAGE7", id);
                write_wav(path, pcm, n);
            }
            free(pcm);
            settle();
        }
        printf("  %d SE played, %d silent, %d unused (velocity-2 dummies)\n", played, silent, dummy);
        CHECK(played > 50 && silent == 0);
        /* tunes: peak level over 8 s (no clipping at the default gain) */
        for (id = 0; id < 6; id++) {
            if (!Sfx_Lookup(id, &d) || d.song >= SfxBank_SongCount(b, d.seq_bank)) continue;
            s_peak32 = 0;
            Sfx_Play(id);
            render(NULL, SFX_RATE * 8);
            if (s_peak32 > maxpeak) maxpeak = s_peak32;
            if (s_peak32 > 32767) printf("  %s tune %d clips: peak %d\n", k_files[f], id, s_peak32);
            settle();
        }
        if (f == 0) {
            int16_t *pcm;
            int peak, n = render_id(0x001, 8.0, &pcm, &peak);
            printf("  tune 001 (stage clear): %.2f s, peak %d\n", n / (double)SFX_RATE, peak);
            CHECK(peak > 1000);
            if (pcm) { write_wav("/tmp/sfx_STAGE7_001.wav", pcm, n); free(pcm); }
            settle();
            n = render_id(0x003, 6.0, &pcm, &peak);        /* stage BGM (loops) */
            printf("  tune 003 (stage music): rendered %.2f s, peak %d, still playing %d\n", n / (double)SFX_RATE, peak, Sfx_ActiveSlots());
            CHECK(peak > 1000 && Sfx_ActiveSlots() == 1 && Sfx_SlotId(0) == 3);
            if (pcm) { write_wav("/tmp/sfx_STAGE7_003.wav", pcm, n); free(pcm); }
            settle();
        }
        if (f == 0) {   /* loop timing: two loop iterations of tune 3 sound the same */
            const uint8_t *q = b->seq[2].data;
            uint32_t off = (uint32_t)q[2 + 4 * 3] << 24 | (uint32_t)q[3 + 4 * 3] << 16 | (uint32_t)q[4 + 4 * 3] << 8 | q[5 + 4 * 3];
            const uint8_t *sp = q + off;
            int res = sp[0] << 8 | sp[1], nt = sp[2] << 8 | sp[3];
            if (nt == 2) {
                double t0 = 0, t1 = 0;
                uint32_t k;
                int frames, a, len;
                int16_t *buf;
                for (k = 0; k < 2; k++) {
                    uint32_t ticks = (uint32_t)sp[8 + 8 * k] << 24 | (uint32_t)sp[9 + 8 * k] << 16 | (uint32_t)sp[10 + 8 * k] << 8 | sp[11 + 8 * k];
                    uint32_t us = (uint32_t)sp[12 + 8 * k] << 24 | (uint32_t)sp[13 + 8 * k] << 16 | (uint32_t)sp[14 + 8 * k] << 8 | sp[15 + 8 * k];
                    /* driver time: ticks / (step / 256) timer-B periods of 88 samples */
                    double step = (double)(512000u / (us / (uint32_t)res)) / 256.0;
                    double samples = ticks / step * SFX_TICK_SAMPLES;
                    if (k == 0) t0 = samples; else t1 = samples;
                }
                a = (int)t0 + SFX_FRAME_SAMPLES; len = (int)t1;
                frames = a + 2 * len + SFX_RATE;
                printf("  tune 003: intro %.2f s, loop %.2f s\n", t0 / SFX_RATE, t1 / SFX_RATE);
                buf = (int16_t *)malloc((size_t)frames * 4);
                if (buf && frames < SFX_RATE * 200) {
                    double c, c_off;
                    Sfx_Play(0x003);
                    render(buf, frames);
                    c = loop_corr(buf, frames, a, len);
                    c_off = loop_corr(buf, frames, a, len + SFX_RATE / 7);
                    printf("  loop iterations: envelope correlation %.4f (loop length + 1/7 s: %.4f)\n", c, c_off);
                    CHECK(c > 0.98 && c > c_off + 0.1);
                    settle();
                }
                free(buf);
            }
        }
        SfxBank_Free(Sfx_SetBank(NULL));
    }
    CHECK(loaded == 10);
    printf("largest unclipped peak over all SE / 8 s of every tune: %d\n", maxpeak);
    CHECK(maxpeak <= 32767);

    /* SH-2 queue / scheduler / driver commands + audio.c hook (AUDIO_NO_SDL mixer) */
    snprintf(path, sizeof path, "%s/%s", dir, "STAGE7.SND");
    {
        size_t len;
        uint8_t *buf = load_file(path, &len);
        if (buf) {
            int16_t out[2048];
            int i, peak = 0;
            CHECK(Sfx_LoadBankData(buf, len, "STAGE7.SND"));
            free(buf);
            CHECK(Sfx_HasBankLocked());
            Audio_Render(out, 1024);                   /* scheduler phase: frame boundary at 0 */
            Audio_PlaySfx(0x108);
            CHECK(Sfx_Pending() == 1 && Sfx_CountLocked(0x108) == 0);   /* queued until the next frame */
            Audio_PlaySfx(0x108);                      /* a pending duplicate is dropped */
            CHECK(Sfx_Pending() == 1);
            Audio_PlaySfx(0x10C);
            for (i = 0; i < 8; i++) {
                int k;
                Audio_Render(out, 1024);
                for (k = 0; k < 2048; k++) if (abs(out[k]) > peak) peak = abs(out[k]);
                if (i == 0) {
                    CHECK(Sfx_Pending() == 0 && Sfx_CountLocked(0x108) == 1 && Sfx_CountLocked(0x10C) == 1);
                    CHECK(Sfx_SlotIdLocked(0) == -1 && Sfx_SlotIdLocked(1) == 0x108 && Sfx_SlotIdLocked(2) == 0x10C);
                    Audio_PlaySfx(0x108);              /* no restart: a 2nd instance (0x06007D60 limits) */
                }
                if (i == 1) CHECK(Sfx_Count(0x108) == 2);
            }
            printf("Audio_Render with SFX hook: peak %d\n", peak);
            CHECK(peak > 1000);
            Audio_StopSfx();                           /* stops are queued too */
            Audio_Render(out, 1024);
            CHECK(Sfx_ActiveSlots() == 0 && Sfx_Count(0x108) == 0);
            Audio_Render(out, 1024);                   /* release (RR 31: a few ms) */
            Audio_Render(out, 1024);
            CHECK(peak16(out, 1024) == 0 && Sfx_ActiveVoices() == 0);
            /* slot 0 = tunes; 7 SE slots: starts sorted by priority, the lowest refused */
            Audio_PlaySfx(0x003);
            for (i = 0x200; i < 0x209; i++) Audio_PlaySfx(i);
            Audio_Render(out, SFX_FRAME_SAMPLES);      /* exactly one scheduler run */
            {
                static const int want[8] = { 0x003, 0x205, 0x206, 0x207, 0x208, 0x200, 0x201, 0x202 };
                int k, ok = 1;
                for (k = 0; k < 8; k++) ok &= Sfx_SlotId(k) == want[k];
                CHECK(ok && Sfx_Count(0x203) == 0 && Sfx_Count(0x204) == 0 && Sfx_Pending() == 0);
            }
            Audio_StopSfx();
            for (i = 0; i < 4; i++) Audio_Render(out, 1024);
            {   /* 7 long SEs (0x227 priority 8, first; others 16), then 0x107 (priority 4) stops slot 1
                 * (the first slot whose priority value is larger) and starts there a frame later */
                static const int ids[7] = { 0x10E, 0x227, 0x10A, 0x21E, 0x10F, 0x113, 0x204 };
                int k;
                for (k = 0; k < 7; k++) Audio_PlaySfx(ids[k]);
                Audio_Render(out, SFX_FRAME_SAMPLES);
                CHECK(Sfx_ActiveSlots() == 7 && Sfx_SlotId(1) == 0x227 && Sfx_SlotId(2) == 0x10E && Sfx_SlotId(7) == 0x204);
                Audio_PlaySfx(0x10B);                  /* same priority: no slot, dropped */
                Audio_PlaySfx(0x107);
                Audio_Render(out, SFX_FRAME_SAMPLES);
                CHECK(Sfx_SlotId(1) == 0x227 && Sfx_ActiveSlots() == 6 && Sfx_Pending() == 1);
                Audio_Render(out, SFX_FRAME_SAMPLES);
                CHECK(Sfx_SlotId(1) == 0x107 && Sfx_Count(0x10B) == 0 && Sfx_Pending() == 0);
            }
            Audio_StopSfx();
            for (i = 0; i < 4; i++) Audio_Render(out, 1024);
            Audio_PlaySfx(0x003);
            for (i = 0; i < 4; i++) Audio_Render(out, 1024);
            /* the game's music fade (0x06008154(0, 0, 0x80)): att 0 -> 127 in 128 x 50 ticks */
            Sfx_SetSeqVolumeLocked(0, 0, 0x80);
            for (i = 0; i < 70; i++) Audio_Render(out, 1024);     /* 1.6 s */
            {
                int att = Sfx_SlotAttenuation(0);
                printf("fade 0x80: attenuation after 1.6 s = %d\n", att);
                CHECK(att >= 13 && att <= 18 && Sfx_SlotId(0) == 0x003);
            }
            Sfx_SetSeqVolumeLocked(0, 64, 0);          /* rate 0 while fading: fade cancelled, att 63 */
            Audio_Render(out, 1024);
            CHECK(Sfx_SlotAttenuation(0) == 63);
            Sfx_SetSeqVolumeLocked(0, 0, 5);           /* 63 -> 127 in 5 x 0.1 s, then the tune stops */
            for (i = 0; i < 20; i++) Audio_Render(out, 1024);
            CHECK(Sfx_SlotId(0) == 0x003 && Sfx_SlotAttenuation(0) > 63);
            for (i = 0; i < 40; i++) Audio_Render(out, 1024);
            CHECK(Sfx_SlotId(0) == -1 && Sfx_Count(0x003) == 0);
            Sfx_SetSeqVolumeLocked(0, 127, 0);         /* restore (0x06084B56), else the next tune is silent */
            Audio_Render(out, 1024);
            CHECK(Sfx_SlotAttenuation(0) == 0);
            Audio_StopSfx();
            for (i = 0; i < 4; i++) Audio_Render(out, 1024);
            /* mono flag: every slot centred, L == R */
            Audio_SetMono(1);
            Audio_PlaySfx(0x003);
            for (i = 0; i < 40; i++) Audio_Render(out, 1024);
            {
                int k, same = 1;
                for (k = 0; k < 1024; k++) same &= out[k * 2] == out[k * 2 + 1];
                CHECK(same && peak16(out, 1024) > 500);
            }
            Audio_SetMono(0);
            for (i = 0; i < 10; i++) Audio_Render(out, 1024);
            {
                int k, same = 1;
                for (k = 0; k < 1024; k++) same &= out[k * 2] == out[k * 2 + 1];
                CHECK(!same);
            }
            Audio_StopSfx();
        }
    }
    if (argc > 2) demos(dir, argv[2]);
    printf(fails ? "test_sfx: %d FAILURES\n" : "test_sfx: all passed\n", fails);
    return fails != 0;
}
