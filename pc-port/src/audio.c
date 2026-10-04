/* PC audio output, see audio.h.
 *
 * Threading: with SDL the mixer (Audio_Render) runs in SDL's audio thread;
 * every state change from the game thread is done under
 * SDL_LockAudioDevice. File loading, freeing and movie resampling happen
 * outside the lock (the resampler state belongs to the game thread). */
/* No SDL headers (unit tests on a machine without SDL): build the device-less mixer. */
#if !defined(AUDIO_NO_SDL) && defined(__has_include)
#  if !__has_include(<SDL2/SDL.h>)
#    define AUDIO_NO_SDL 1
#  endif
#endif
#include "audio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef AUDIO_NO_SDL
#include <SDL2/SDL.h>
static SDL_AudioDeviceID s_dev;
static int s_sdl_subsystem;            /* we called SDL_InitSubSystem(AUDIO) */
#define LOCK()   do { if (s_dev) SDL_LockAudioDevice(s_dev); } while (0)
#define UNLOCK() do { if (s_dev) SDL_UnlockAudioDevice(s_dev); } while (0)
#else
#define LOCK()   do { } while (0)
#define UNLOCK() do { } while (0)
#endif

#define GAIN_RAMP (1.0f / 441.0f)      /* full scale in 10 ms */

static int s_inited;

/* music */
static int16_t *s_music;               /* 44.1 kHz stereo */
static uint32_t s_music_frames, s_music_pos;
static int s_music_loop, s_music_on;
static float s_gain_cur[2] = { 1.0f, 1.0f }, s_gain_tgt[2] = { 1.0f, 1.0f };

/* movie ring (44.1 kHz stereo) */
static int16_t s_ring[AUDIO_MOVIE_RING_FRAMES * 2];
static uint32_t s_ring_r, s_ring_w, s_ring_count;
static uint64_t s_movie_played;
static AudioResampler s_rs;            /* game thread only */
static int s_rs_rate, s_rs_ch;
static int16_t *s_tmp;                 /* resampler output, game thread only */
static int s_tmp_cap;

static int s_mono;

/* ------------------------------------------------------------------ helpers */

static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

float Audio_CdDaLevelToGain(int level)
{
    /* SCSP send level (MAME scsp.cpp SDLT[]): 0 = off, 1..7 = -36..0 dB */
    if (level <= 0) return 0.0f;
    if (level >= 7) return 1.0f;
    return (float)pow(10.0, -6.0 * (7 - level) / 20.0);
}

int Audio_ParseWav(const uint8_t *buf, size_t len, AudioWavInfo *info)
{
    size_t p = 12;
    int have_fmt = 0;
    memset(info, 0, sizeof *info);
    if (len < 12 || memcmp(buf, "RIFF", 4) != 0 || memcmp(buf + 8, "WAVE", 4) != 0)
        return -1;
    while (len - p >= 8) {
        const uint8_t *id = buf + p;
        uint32_t sz = le32(buf + p + 4);
        size_t body = p + 8;
        if (memcmp(id, "fmt ", 4) == 0) {
            uint16_t tag;
            if (sz < 16 || sz > len - body) return -2;
            tag = le16(buf + body);
            info->channels = le16(buf + body + 2);
            info->rate = le32(buf + body + 4);
            info->bits = le16(buf + body + 14);
            if (tag == 0xFFFE && sz >= 26)                 /* WAVE_FORMAT_EXTENSIBLE */
                tag = le16(buf + body + 24);               /* SubFormat GUID, first 2 bytes */
            if (tag != 1 || info->bits != 16 || info->channels < 1 || info->channels > 2 ||
                info->rate < 1000 || info->rate > 384000)
                return -3;
            have_fmt = 1;
        } else if (memcmp(id, "data", 4) == 0) {
            size_t avail = len - body, bytes;
            if (!have_fmt) return -2;
            bytes = sz < avail ? sz : avail;               /* truncated / 0xFFFFFFFF sizes */
            bytes -= bytes % (size_t)(info->channels * 2u);
            info->data_offset = (uint32_t)body;
            info->data_bytes = (uint32_t)bytes;
            info->frames = (uint32_t)(bytes / (info->channels * 2u));
            return 0;
        }
        if (sz > len - body) break;
        p = body + sz + (sz & 1u);                          /* chunks are word aligned */
        if (p > len) break;
    }
    return have_fmt ? -4 : -2;
}

/* ---------------------------------------------------------------- resampler */

void AudioResampler_Init(AudioResampler *r, uint32_t in_rate, uint32_t out_rate)
{
    memset(r, 0, sizeof *r);
    if (in_rate == 0) in_rate = out_rate;
    r->in_rate = in_rate;
    r->out_rate = out_rate;
    r->step = ((uint64_t)in_rate << 32) / out_rate;
    if (r->step == 0) r->step = 1;
}

int AudioResampler_MaxOut(const AudioResampler *r, int in_frames)
{
    uint64_t pos = r->primed ? r->pos : (uint64_t)1 << 32;
    uint64_t end = (uint64_t)(in_frames > 0 ? in_frames : 0) << 32;
    if (pos >= end) return 1;
    return (int)((end - pos - 1) / r->step) + 2;
}

static int16_t smp(const int16_t *in, int ch, uint64_t k, int c)
{
    return ch == 1 ? in[k] : in[k * 2 + (uint64_t)c];
}

int AudioResampler_Process(AudioResampler *r, const int16_t *in, int in_frames,
                           int in_channels, int16_t *out)
{
    int o = 0, c;
    uint64_t n;
    if (in_frames <= 0) return 0;
    if (in_channels != 1) in_channels = 2;
    n = (uint64_t)in_frames;
    if (!r->primed) {                     /* first output = in[0] */
        r->last[0] = smp(in, in_channels, 0, 0);
        r->last[1] = smp(in, in_channels, 0, 1);
        r->pos = (uint64_t)1 << 32;
        r->primed = 1;
    }
    for (;;) {
        uint64_t i = r->pos >> 32;
        int64_t f = (int64_t)((r->pos >> 16) & 0xFFFF);
        if (i >= n) break;
        for (c = 0; c < 2; c++) {
            int32_t a = i == 0 ? r->last[c] : smp(in, in_channels, i - 1, c);
            int32_t b = smp(in, in_channels, i, c);
            out[o * 2 + c] = (int16_t)(a + (int32_t)(((int64_t)(b - a) * f) >> 16));
        }
        o++;
        r->pos += r->step;
    }
    r->pos -= n << 32;
    r->last[0] = smp(in, in_channels, n - 1, 0);
    r->last[1] = smp(in, in_channels, n - 1, 1);
    return o;
}

/* ------------------------------------------------------------------- loading */

int16_t *Audio_LoadPcmFile(const char *path, uint32_t *frames_out)
{
    FILE *f = fopen(path, "rb");
    uint8_t *buf;
    long len;
    AudioWavInfo wi;
    int16_t *pcm = NULL, *out;
    uint32_t i, frames;
    int rc;

    *frames_out = 0;
    if (!f) { fprintf(stderr, "[audio] cannot open %s\n", path); return NULL; }
    if (fseek(f, 0, SEEK_END) != 0 || (len = ftell(f)) < 4 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        fprintf(stderr, "[audio] %s: empty or unreadable\n", path);
        return NULL;
    }
    buf = (uint8_t *)malloc((size_t)len);
    if (!buf || fread(buf, 1, (size_t)len, f) != (size_t)len) {
        fclose(f); free(buf);
        fprintf(stderr, "[audio] %s: read error\n", path);
        return NULL;
    }
    fclose(f);

    rc = Audio_ParseWav(buf, (size_t)len, &wi);
    if (rc == -1) {                       /* headerless: raw CD-DA sectors */
        wi.rate = AUDIO_RATE; wi.channels = 2; wi.bits = 16;
        wi.data_offset = 0;
        wi.frames = (uint32_t)((size_t)len / 4);
        wi.data_bytes = wi.frames * 4;
    } else if (rc != 0) {
        fprintf(stderr, "[audio] %s: unsupported WAV (error %d; need 16-bit PCM, 1-2 ch)\n", path, rc);
        free(buf);
        return NULL;
    }
    if (wi.frames == 0) { free(buf); fprintf(stderr, "[audio] %s: no samples\n", path); return NULL; }

    pcm = (int16_t *)malloc((size_t)wi.frames * wi.channels * sizeof(int16_t));
    if (!pcm) { free(buf); return NULL; }
    for (i = 0; i < wi.frames * wi.channels; i++)       /* little-endian, any host */
        pcm[i] = (int16_t)le16(buf + wi.data_offset + (size_t)i * 2);
    free(buf);

    if (wi.rate == AUDIO_RATE && wi.channels == 2) {
        *frames_out = wi.frames;
        return pcm;
    }
    {
        AudioResampler r;
        AudioResampler_Init(&r, wi.rate, AUDIO_RATE);
        out = (int16_t *)malloc((size_t)AudioResampler_MaxOut(&r, (int)wi.frames) * 2 * sizeof(int16_t));
        if (!out) { free(pcm); return NULL; }
        frames = (uint32_t)AudioResampler_Process(&r, pcm, (int)wi.frames, wi.channels, out);
        free(pcm);
        *frames_out = frames;
        return out;
    }
}

/* --------------------------------------------------------------------- mixer */

#define SFX_CHUNK 1024
static AudioSfxHooks s_sfx;            /* SFX hook, see Audio_SetSfxHooks */

void Audio_Render(int16_t *out, int frames)
{
    int k, c;
    static int32_t sfx[SFX_CHUNK * 2];  /* SFX hook: sfx.c voices for this chunk */
    while (frames > SFX_CHUNK) {
        Audio_Render(out, SFX_CHUNK);
        out += SFX_CHUNK * 2;
        frames -= SFX_CHUNK;
    }
    if (s_sfx.render) {
        memset(sfx, 0, (size_t)frames * 2 * sizeof sfx[0]);
        s_sfx.render(sfx, frames);
    }
    for (k = 0; k < frames; k++) {
        int32_t s[2] = { 0, 0 };
        if (s_sfx.render) { s[0] = sfx[k * 2]; s[1] = sfx[k * 2 + 1]; }   /* SFX hook */
        for (c = 0; c < 2; c++) {                         /* level ramp */
            float d = s_gain_tgt[c] - s_gain_cur[c];
            if (d > GAIN_RAMP) s_gain_cur[c] += GAIN_RAMP;
            else if (d < -GAIN_RAMP) s_gain_cur[c] -= GAIN_RAMP;
            else s_gain_cur[c] = s_gain_tgt[c];
        }
        if (s_music_on) {
            for (c = 0; c < 2; c++)
                s[c] += (int32_t)lrintf((float)s_music[s_music_pos * 2 + (uint32_t)c] * s_gain_cur[c]);
            if (++s_music_pos >= s_music_frames) {
                s_music_pos = 0;
                if (!s_music_loop) s_music_on = 0;
            }
        }
        if (s_ring_count) {
            s[0] += s_ring[s_ring_r * 2];
            s[1] += s_ring[s_ring_r * 2 + 1];
            s_ring_r = (s_ring_r + 1) % AUDIO_MOVIE_RING_FRAMES;
            s_ring_count--;
            s_movie_played++;
        }
        if (s_mono) s[0] = s[1] = (s[0] + s[1]) / 2;
        for (c = 0; c < 2; c++)
            out[k * 2 + c] = (int16_t)(s[c] > 32767 ? 32767 : s[c] < -32768 ? -32768 : s[c]);
    }
}

#ifndef AUDIO_NO_SDL
static void SDLCALL audio_callback(void *userdata, Uint8 *stream, int len)
{
    (void)userdata;
    Audio_Render((int16_t *)(void *)stream, len / (int)(AUDIO_CHANNELS * sizeof(int16_t)));
}
#endif

/* -------------------------------------------------------------------- device */

int Audio_Init(void)
{
    if (s_inited) return 1;
#ifndef AUDIO_NO_SDL
    {
        SDL_AudioSpec want, have;
        if (!SDL_WasInit(SDL_INIT_AUDIO)) {
            if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
                fprintf(stderr, "[audio] SDL audio init failed: %s (running silent)\n", SDL_GetError());
                return 0;
            }
            s_sdl_subsystem = 1;
        }
        SDL_zero(want);
        want.freq = AUDIO_RATE;
        want.format = AUDIO_S16SYS;
        want.channels = AUDIO_CHANNELS;
        want.samples = 1024;                      /* ~23 ms */
        want.callback = audio_callback;
        /* allowed_changes = 0: SDL converts if the hardware wants something else */
        s_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
        if (!s_dev) {
            fprintf(stderr, "[audio] SDL_OpenAudioDevice failed: %s (running silent)\n", SDL_GetError());
            if (s_sdl_subsystem) { SDL_QuitSubSystem(SDL_INIT_AUDIO); s_sdl_subsystem = 0; }
            return 0;
        }
        printf("[audio] device open: %d Hz, %d ch, buffer %d frames\n", have.freq, have.channels, have.samples);
    }
#endif
    s_ring_r = s_ring_w = s_ring_count = 0;
    s_movie_played = 0;
    s_rs_rate = s_rs_ch = 0;
    s_inited = 1;
#ifndef AUDIO_NO_SDL
    SDL_PauseAudioDevice(s_dev, 0);
#endif
    return 1;
}

void Audio_Shutdown(void)
{
    if (!s_inited) return;
#ifndef AUDIO_NO_SDL
    if (s_dev) { SDL_CloseAudioDevice(s_dev); s_dev = 0; }
    if (s_sdl_subsystem) { SDL_QuitSubSystem(SDL_INIT_AUDIO); s_sdl_subsystem = 0; }
#endif
    free(s_music); s_music = NULL;
    s_music_on = 0; s_music_frames = s_music_pos = 0;
    free(s_tmp); s_tmp = NULL; s_tmp_cap = 0;
    s_ring_r = s_ring_w = s_ring_count = 0;
    s_inited = 0;
}

/* --------------------------------------------------------------------- music */

int Audio_PlayMusic(const char *path, int loop)
{
    uint32_t frames;
    int16_t *pcm, *old;
    if (!s_inited) return 0;
    pcm = Audio_LoadPcmFile(path, &frames);
    if (!pcm) return 0;
    LOCK();
    old = s_music;
    s_music = pcm;
    s_music_frames = frames;
    s_music_pos = 0;
    s_music_loop = loop != 0;
    s_music_on = 1;
    UNLOCK();
    free(old);
    printf("[audio] music %s: %.2f s%s\n", path, frames / (double)AUDIO_RATE, loop ? " (loop)" : "");
    return 1;
}

void Audio_StopMusic(void)
{
    int16_t *old;
    LOCK();
    old = s_music;
    s_music = NULL;
    s_music_on = 0;
    s_music_frames = s_music_pos = 0;
    UNLOCK();
    free(old);
}

int Audio_IsMusicPlaying(void)
{
    int on;
    LOCK();
    on = s_music_on;
    UNLOCK();
    return on;
}

void Audio_SetMusicLevels(int left, int right)
{
    float l = Audio_CdDaLevelToGain(left), r = Audio_CdDaLevelToGain(right);
    LOCK();
    s_gain_tgt[0] = l;
    s_gain_tgt[1] = r;
    UNLOCK();
}

void Audio_SetMusicVolume(int level) { Audio_SetMusicLevels(level, level); }

void Audio_SetMono(int mono)
{
    LOCK();
    s_mono = mono != 0;
    UNLOCK();
}

/* --------------------------------------------------------------------- movie */

int Audio_QueueMovie(const int16_t *pcm, int samples, int rate, int channels)
{
    int n, k, room;
    if (!s_inited || !pcm || samples <= 0 || rate <= 0) return 0;
    if (channels != 1) channels = 2;
    if (rate != s_rs_rate || channels != s_rs_ch) {
        AudioResampler_Init(&s_rs, (uint32_t)rate, AUDIO_RATE);
        s_rs_rate = rate;
        s_rs_ch = channels;
    }
    n = AudioResampler_MaxOut(&s_rs, samples);
    if (n > s_tmp_cap) {
        int16_t *t = (int16_t *)realloc(s_tmp, (size_t)n * 2 * sizeof(int16_t));
        if (!t) return 0;
        s_tmp = t;
        s_tmp_cap = n;
    }
    n = AudioResampler_Process(&s_rs, pcm, samples, channels, s_tmp);

    LOCK();
    room = (int)(AUDIO_MOVIE_RING_FRAMES - s_ring_count);
    if (n > room) n = room;
    for (k = 0; k < n; k++) {
        s_ring[s_ring_w * 2] = s_tmp[k * 2];
        s_ring[s_ring_w * 2 + 1] = s_tmp[k * 2 + 1];
        s_ring_w = (s_ring_w + 1) % AUDIO_MOVIE_RING_FRAMES;
    }
    s_ring_count += (uint32_t)n;
    UNLOCK();
    return n;
}

void Audio_StopMovie(void)
{
    LOCK();
    s_ring_r = s_ring_w = s_ring_count = 0;
    s_movie_played = 0;
    UNLOCK();
    s_rs_rate = s_rs_ch = 0;          /* next queue re-primes the resampler */
}

int Audio_MovieQueuedFrames(void)
{
    uint32_t n;
    LOCK();
    n = s_ring_count;
    UNLOCK();
    return (int)n;
}

uint64_t Audio_MoviePlayedFrames(void)
{
    uint64_t n;
    LOCK();
    n = s_movie_played;
    UNLOCK();
    return n;
}

/* ---------------------------------------------------------------------- sfx */

/* SFX hook (sfx.c registers itself when a .SND bank is loaded); s_sfx is above the mixer */

void Audio_SetSfxHooks(const AudioSfxHooks *hooks)
{
    if (hooks) s_sfx = *hooks;
    else memset(&s_sfx, 0, sizeof s_sfx);
}

void Audio_Lock(void)   { LOCK(); }
void Audio_Unlock(void) { UNLOCK(); }

void Audio_PlaySfx(int id)
{
    LOCK();
    if (s_sfx.play) s_sfx.play(id);
    else printf("[sfx] %03X\n", id & 0xFFFF);   /* no .SND bank loaded */
    UNLOCK();
}

void Audio_StopSfx(void)
{
    LOCK();
    if (s_sfx.stop) s_sfx.stop();
    UNLOCK();
}
