/* Audio module (src/audio.c) without SDL: WAV parsing, resampler, mixer.
 * gcc -std=c11 -Wall -Wextra -O1 -DAUDIO_NO_SDL -Isrc tests/test_audio.c src/audio.c -lm -o /tmp/ta && /tmp/ta ../extracted/
 * (the optional argument is the folder holding TRACK02.WAV / TRACK03.WAV from tools/rip_cdda.py) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include "audio.h"

#define PI 3.14159265358979323846

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }

/* WAV image: RIFF, "fmt " (tag, ch, rate, bits), an odd-sized LIST chunk, data */
static size_t make_wav(uint8_t *b, uint16_t tag, uint16_t ch, uint32_t rate, uint16_t bits,
                       const int16_t *s, uint32_t frames, int extensible)
{
    size_t p = 12;
    uint32_t fmt_sz = extensible ? 40 : 16, data_sz = frames * ch * 2u, i;
    memcpy(b, "RIFF", 4); memcpy(b + 8, "WAVE", 4);
    memcpy(b + p, "fmt ", 4); put32(b + p + 4, fmt_sz);
    put16(b + p + 8, extensible ? 0xFFFE : tag); put16(b + p + 10, ch); put32(b + p + 12, rate);
    put32(b + p + 16, rate * ch * 2u); put16(b + p + 20, (uint16_t)(ch * 2)); put16(b + p + 22, bits);
    if (extensible) { memset(b + p + 24, 0, 24); put16(b + p + 24, 22); put16(b + p + 32, tag); }
    p += 8 + fmt_sz;
    memcpy(b + p, "LIST", 4); put32(b + p + 4, 5); memcpy(b + p + 8, "INFOx", 5); b[p + 13] = 0;
    p += 8 + 6;                                         /* 5 bytes + pad */
    memcpy(b + p, "data", 4); put32(b + p + 4, data_sz);
    for (i = 0; i < frames * ch; i++) put16(b + p + 8 + i * 2, (uint16_t)s[i]);
    p += 8 + data_sz;
    put32(b + 4, (uint32_t)(p - 8));
    return p;
}

static void test_levels(void)
{
    int l;
    printf("CD-DA level -> gain:");
    for (l = 0; l <= 7; l++) printf(" %d=%.4f", l, Audio_CdDaLevelToGain(l));
    printf("\n");
    CHECK(Audio_CdDaLevelToGain(0) == 0.0f);
    CHECK(Audio_CdDaLevelToGain(7) == 1.0f);
    CHECK(fabs(Audio_CdDaLevelToGain(6) - 0.501187) < 1e-4);   /* -6 dB */
    CHECK(fabs(Audio_CdDaLevelToGain(1) - 0.015849) < 1e-4);   /* -36 dB */
    CHECK(Audio_CdDaLevelToGain(9) == 1.0f && Audio_CdDaLevelToGain(-3) == 0.0f);
}

static void test_wav_parse(void)
{
    static uint8_t b[4096];
    int16_t s[8] = { 1, -2, 3, -4, 5, -6, 7, -8 };
    AudioWavInfo wi;
    size_t n = make_wav(b, 1, 2, 44100, 16, s, 4, 0);
    CHECK(Audio_ParseWav(b, n, &wi) == 0);
    CHECK(wi.rate == 44100 && wi.channels == 2 && wi.bits == 16 && wi.frames == 4);
    CHECK(wi.data_offset == 12 + 24 + 14 + 8);
    CHECK((int16_t)(b[wi.data_offset + 2] | b[wi.data_offset + 3] << 8) == -2);
    printf("WAV parse: rate %u ch %u frames %u data at %u\n", wi.rate, wi.channels, wi.frames, wi.data_offset);

    n = make_wav(b, 1, 1, 22050, 16, s, 8, 1);           /* WAVE_FORMAT_EXTENSIBLE */
    CHECK(Audio_ParseWav(b, n, &wi) == 0 && wi.channels == 1 && wi.frames == 8 && wi.rate == 22050);
    CHECK(Audio_ParseWav(b, n - 3, &wi) == 0 && wi.frames == 6);   /* truncated data: whole frames */
    n = make_wav(b, 1, 2, 44100, 8, s, 4, 0);
    CHECK(Audio_ParseWav(b, n, &wi) == -3);              /* 8-bit */
    n = make_wav(b, 3, 2, 44100, 16, s, 4, 0);
    CHECK(Audio_ParseWav(b, n, &wi) == -3);              /* float */
    CHECK(Audio_ParseWav((const uint8_t *)"RIFX....WAVE", 12, &wi) == -1);
    CHECK(Audio_ParseWav(b, 30, &wi) == -2 || Audio_ParseWav(b, 30, &wi) == -4);
    n = make_wav(b, 1, 2, 44100, 16, s, 4, 0);
    CHECK(Audio_ParseWav(b, 12 + 24 + 14, &wi) == -4);    /* fmt but no data */
}

static void test_resampler(void)
{
    AudioResampler r;
    int16_t in[2000], one[8000], chunked[8000], tmp[8000];
    int i, n1, n2 = 0, pos = 0, sizes[] = { 1, 7, 100, 3, 333, 2, 554 }, k;
    int zc = 0, maxerr = 0;

    /* ramp 22050 mono -> 44100: in0, (in0+in1)/2, in1, ... */
    for (i = 0; i < 1000; i++) in[i] = (int16_t)(i * 30 - 15000);
    AudioResampler_Init(&r, 22050, 44100);
    n1 = AudioResampler_Process(&r, in, 1000, 1, one);
    printf("resampler 22050->44100: 1000 in -> %d out (max %d)\n", n1, AudioResampler_MaxOut(&r, 1000));
    CHECK(n1 == 1998);                                   /* in[999] waits for the next call */
    CHECK(one[0] == in[0] && one[1] == in[0]);
    CHECK(one[2] == in[0] + 15 && one[4] == in[1] && one[3] == one[2]);
    for (i = 0; i < n1; i++) CHECK(one[i * 2] == (int16_t)(-15000 + i * 15));

    /* chunked processing == one call */
    AudioResampler_Init(&r, 22255, 44100);
    n1 = AudioResampler_Process(&r, in, 1000, 1, one);
    AudioResampler_Init(&r, 22255, 44100);
    for (k = 0; pos < 1000; k = (k + 1) % 7) {
        int m = sizes[k] < 1000 - pos ? sizes[k] : 1000 - pos;
        int cap = AudioResampler_MaxOut(&r, m), got = AudioResampler_Process(&r, in + pos, m, 1, tmp);
        CHECK(got <= cap);
        memcpy(chunked + n2 * 2, tmp, (size_t)got * 4);
        n2 += got;
        pos += m;
    }
    printf("resampler 22255->44100: 1000 in -> %d out in one call, %d chunked\n", n1, n2);
    CHECK(n1 == n2 && memcmp(one, chunked, (size_t)n1 * 4) == 0);
    CHECK(abs(n1 - (int)(1000.0 * 44100 / 22255)) <= 2);

    /* 1 kHz sine, 22050 stereo -> 44100: frequency and amplitude preserved */
    for (i = 0; i < 1000; i++) {
        in[i * 2] = (int16_t)lrint(10000 * sin(2 * PI * 1000 * i / 22050.0));
        in[i * 2 + 1] = (int16_t)(-in[i * 2]);
    }
    AudioResampler_Init(&r, 22050, 44100);
    n1 = AudioResampler_Process(&r, in, 1000, 2, one);
    for (i = 1; i < n1; i++) {
        int e = abs(one[i * 2] - (int)lrint(10000 * sin(2 * PI * 1000 * i / 44100.0)));
        if (e > maxerr) maxerr = e;
        if ((one[(i - 1) * 2] < 0) != (one[i * 2] < 0)) zc++;
        CHECK(one[i * 2 + 1] == -one[i * 2] || one[i * 2 + 1] == -one[i * 2] - 1 || one[i * 2 + 1] == -one[i * 2] + 1);
    }
    printf("1 kHz sine: %d zero crossings in %.4f s (%.1f Hz), max interp error %d / 10000\n",
           zc, n1 / 44100.0, zc / 2.0 / (n1 / 44100.0), maxerr);
    CHECK(fabs(zc / 2.0 / (n1 / 44100.0) - 1000) < 25);
    CHECK(maxerr < 300);                                 /* linear interp of a 22-sample period */

    /* downsample 48000 -> 44100 */
    AudioResampler_Init(&r, 48000, 44100);
    n1 = AudioResampler_Process(&r, in, 1000, 1, one);
    CHECK(abs(n1 - 919) <= 1);
}

static const char *tmpfile_path(const char *name)
{
    static char p[4][256];
    static int k;
    k = (k + 1) & 3;
    snprintf(p[k], sizeof p[k], "/tmp/%s", name);
    return p[k];
}

static void write_file(const char *path, const void *d, size_t n)
{
    FILE *f = fopen(path, "wb");
    assert(f);
    fwrite(d, 1, n, f);
    fclose(f);
}

static void test_mixer(void)
{
    static uint8_t b[1 << 16];
    int16_t s[200], out[1200], mv[100];
    uint32_t frames;
    int16_t *pcm;
    int i, q;
    const char *wav = tmpfile_path("ta_music.wav"), *raw = tmpfile_path("ta_music.raw");

    for (i = 0; i < 100; i++) { s[i * 2] = (int16_t)(1000 + i); s[i * 2 + 1] = (int16_t)(-1000 - i); }
    write_file(wav, b, make_wav(b, 1, 2, 44100, 16, s, 100, 0));
    write_file(raw, s, sizeof s);                       /* host is little-endian here */

    pcm = Audio_LoadPcmFile(raw, &frames);
    CHECK(pcm && frames == 100 && pcm[0] == 1000 && pcm[199] == -1099);
    free(pcm);
    CHECK(Audio_LoadPcmFile(tmpfile_path("does_not_exist.wav"), &frames) == NULL);

    CHECK(Audio_Init() == 1);
    CHECK(Audio_PlayMusic(wav, 1) == 1 && Audio_IsMusicPlaying());
    Audio_Render(out, 250);                             /* loops: frame k = source k % 100 */
    CHECK(out[0] == 1000 && out[1] == -1000 && out[198] == 1099 && out[200] == 1000 && out[499] == -1049);
    CHECK(Audio_IsMusicPlaying());

    CHECK(Audio_PlayMusic(wav, 0) == 1);
    Audio_Render(out, 150);                             /* one shot: silence after 100 frames */
    CHECK(out[198] == 1099 && out[200] == 0 && out[299] == 0);
    CHECK(!Audio_IsMusicPlaying());

    /* level 6 = -6 dB, reached after the 10 ms ramp */
    CHECK(Audio_PlayMusic(wav, 1) == 1);
    Audio_SetMusicVolume(6);
    Audio_Render(out, 500);
    Audio_Render(out, 100);                             /* frames 600..699 = source 0..99 */
    printf("level 6: source 1000 -> %d, -1000 -> %d\n", out[0], out[1]);
    CHECK(abs(out[0] - 501) <= 1 && abs(out[1] + 501) <= 1);
    Audio_SetMusicLevels(0, 7);
    Audio_Render(out, 500);
    Audio_Render(out, 100);
    CHECK(out[0] == 0 && out[1] == -1000);
    Audio_SetMono(1);
    Audio_Render(out, 100);
    CHECK(out[0] == out[1] && out[0] == -500);
    Audio_SetMono(0);
    Audio_StopMusic();
    CHECK(!Audio_IsMusicPlaying());
    Audio_SetMusicVolume(7);

    /* movie: 50 mono frames at 22050 -> ~100 frames, duplicated on L/R */
    for (i = 0; i < 100; i++) mv[i] = (int16_t)(i * 100);
    q = Audio_QueueMovie(mv, 50, 22050, 1);
    q += Audio_QueueMovie(mv + 50, 50, 22050, 1);
    printf("movie: 100 frames @22050 -> %d queued (%d pending)\n", q, Audio_MovieQueuedFrames());
    CHECK(q == 198 && Audio_MovieQueuedFrames() == 198);
    Audio_Render(out, 300);
    CHECK(out[0] == 0 && out[2] == 50 && out[4] == 100 && out[3] == out[2]);
    CHECK(out[197 * 2] == 9850 && out[197 * 2 + 1] == 9850);
    CHECK(out[198 * 2] == 0 && Audio_MovieQueuedFrames() == 0 && Audio_MoviePlayedFrames() == 198);
    /* overflow drops the excess */
    {
        static int16_t big[AUDIO_MOVIE_RING_FRAMES];
        Audio_StopMovie();
        q = Audio_QueueMovie(big, AUDIO_MOVIE_RING_FRAMES, 44100, 1);
        q += Audio_QueueMovie(big, 1000, 44100, 1);
        CHECK(q == AUDIO_MOVIE_RING_FRAMES && Audio_MovieQueuedFrames() == AUDIO_MOVIE_RING_FRAMES);
        Audio_StopMovie();
        CHECK(Audio_MovieQueuedFrames() == 0 && Audio_MoviePlayedFrames() == 0);
    }
    /* music + movie mix and clipping */
    for (i = 0; i < 100; i++) { s[i * 2] = 30000; s[i * 2 + 1] = -30000; }
    write_file(wav, b, make_wav(b, 1, 2, 44100, 16, s, 100, 0));
    CHECK(Audio_PlayMusic(wav, 1) == 1);
    Audio_Render(out, 500);                             /* left level ramp 0 -> 7 done */
    for (i = 0; i < 100; i++) mv[i] = 10000;
    Audio_QueueMovie(mv, 100, 44100, 1);
    Audio_Render(out, 10);
    CHECK(out[2] == 32767 && out[3] == -20000);
    Audio_PlaySfx(0x108);
    Audio_Shutdown();
    remove(wav);
    remove(raw);
}

static void test_real_tracks(const char *dir)
{
    static const struct { const char *name; uint32_t sectors; } t[] = {
        { "TRACK02.WAV", 2632 }, { "TRACK03.WAV", 7584 } };
    char p[512];
    int k;
    for (k = 0; k < 2; k++) {
        uint32_t frames, i;
        double s2 = 0;
        int16_t *pcm;
        snprintf(p, sizeof p, "%s%s", dir, t[k].name);
        pcm = Audio_LoadPcmFile(p, &frames);
        if (!pcm) { printf("%s: not found, skipped (run tools/rip_cdda.py)\n", p); continue; }
        for (i = 0; i < frames * 2; i++) s2 += (double)pcm[i] * pcm[i];
        printf("%s: %u frames (%.2f s), rms %.0f\n", t[k].name, frames, frames / 44100.0, sqrt(s2 / (frames * 2.0)));
        CHECK(frames == t[k].sectors * 588u);
        CHECK(sqrt(s2 / (frames * 2.0)) > 1000);
        free(pcm);
    }
}

int main(int argc, char **argv)
{
    test_levels();
    test_wav_parse();
    test_resampler();
    test_mixer();
    test_real_tracks(argc > 1 ? argv[1] : "../extracted/");
    printf(fails ? "%d FAILURE(S)\n" : "all audio tests passed\n", fails);
    return fails != 0;
}
