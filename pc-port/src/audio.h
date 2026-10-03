#ifndef AUDIO_H
#define AUDIO_H
/* PC audio output: CD-DA music stream, movie PCM stream, SFX placeholder.
 *
 * Independent of the game code (no ram.h / Saturn addresses). One output
 * device: 44100 Hz, stereo, signed 16-bit, opened with SDL2
 * (SDL_OpenAudioDevice + callback). Built with -DAUDIO_NO_SDL there is no
 * device: Audio_Render() is the mixer and tests drive it directly.
 *
 * Saturn behaviour this models (docs/director.md 3.2/3.3, A.BIN SBL code):
 *  - TITLE.PCM / ENDING.PCM are CD-DA tracks 2 / 3. Res_Request(2, ...,"TITLE.PCM")
 *    (A.BIN 0x06004A1C -> 0x060047A8) only does GFS_Open + a CD read of the file's
 *    ceil(size/2048) sectors: the track plays ONCE (no restart code found);
 *    rip with tools/rip_cdda.py (extracted/TRACK02.WAV, TRACK03.WAV).
 *  - Snd_SetCdDaLevel(l, r) 0x0603125C: SBL sound-driver command 0x80, levels
 *    0..7 written as level<<5 = SCSP EFSDL of the CD-DA inputs: 7 = 0 dB,
 *    each step -6 dB, 0 = muted.  -> Audio_SetMusicLevels().
 *  - 0x06030710(15) is driver command 0x82 = master volume (0..15), not a loop.
 */
#include <stddef.h>
#include <stdint.h>

#define AUDIO_RATE      44100
#define AUDIO_CHANNELS  2

/* ---- device ------------------------------------------------------------ */
int  Audio_Init(void);          /* 1 = ok. Inits SDL's audio subsystem if needed.
                                   On failure the game keeps running silently. */
void Audio_Shutdown(void);

/* ---- music (CD-DA stand-in) ------------------------------------------- */
/* Loads the whole file into memory (TRACK03 = 17.8 MB) and starts it.
 * Accepted: RIFF WAV, 16-bit PCM, 1-2 channels, any rate (converted to
 * 44.1 kHz stereo at load), or a headerless file = raw CD-DA
 * (44.1 kHz stereo s16le). loop != 0: wraps to the start at the end
 * (the Saturn plays the tracks once; looping is a port option).
 * Returns 1 if playing, 0 on error (previous music keeps playing). */
int  Audio_PlayMusic(const char *path, int loop);
void Audio_StopMusic(void);
int  Audio_IsMusicPlaying(void);
/* Snd_SetCdDaLevel(l, r): 0..7 per channel (7 = 0 dB, -6 dB/step, 0 = off).
 * Changes are ramped over ~10 ms (no clicks during Music_FadeStep). */
void Audio_SetMusicLevels(int left, int right);
void Audio_SetMusicVolume(int level);           /* same level on both channels */
float Audio_CdDaLevelToGain(int level);         /* 0..7 -> linear gain */

/* ---- movie audio (CPK / Sega FILM PCM) -------------------------------- */
/* pcm: `samples` sample frames, interleaved if channels == 2, native-endian
 * s16 (the decoder converts the FILM big-endian data). Resampled to 44.1 kHz
 * stereo with a stateful linear resampler (continuous across calls; a change
 * of rate/channels restarts it). Returns the number of 44.1 kHz frames
 * queued; when the ring (AUDIO_MOVIE_RING_FRAMES) is full the excess is
 * dropped, so keep Audio_MovieQueuedFrames() below ~0.5 s. */
#define AUDIO_MOVIE_RING_FRAMES (AUDIO_RATE * 4)
int      Audio_QueueMovie(const int16_t *pcm, int samples, int rate, int channels);
void     Audio_StopMovie(void);                 /* flush ring + reset resampler */
int      Audio_MovieQueuedFrames(void);         /* output frames not yet played */
uint64_t Audio_MoviePlayedFrames(void);         /* frames played since the last
                                                   Audio_StopMovie (A/V clock) */

/* ---- sound effects ---------------------------------------------------- */
/* Snd_Play(id) A.BIN 0x06007CDE. SCSP / .SND playback is not emulated yet:
 * logs "[sfx] %03X". */
void Audio_PlaySfx(int id);

/* ---- misc -------------------------------------------------------------- */
void Audio_SetMono(int mono);   /* options AUDIO = MONO (0x06025246): L = R = (L+R)/2 */

/* Mixer: fills `frames` stereo frames. Called by the SDL callback; public
 * for AUDIO_NO_SDL tests / offline rendering. Do not call it while an SDL
 * device is open. */
void Audio_Render(int16_t *out, int frames);

/* ---- helpers exposed for tests ---------------------------------------- */
typedef struct {
    uint32_t rate;
    uint16_t channels;
    uint16_t bits;
    uint32_t data_offset;       /* byte offset of the samples in the file */
    uint32_t data_bytes;        /* clamped to the file size, whole frames */
    uint32_t frames;
} AudioWavInfo;
/* 0 = ok; -1 not RIFF/WAVE, -2 no fmt, -3 unsupported format, -4 no data. */
int Audio_ParseWav(const uint8_t *buf, size_t len, AudioWavInfo *info);

/* Loads a WAV / raw CD-DA file as 44.1 kHz stereo s16 (malloc'd, caller
 * frees). NULL on error. */
int16_t *Audio_LoadPcmFile(const char *path, uint32_t *frames_out);

typedef struct {
    uint32_t in_rate, out_rate;
    uint64_t step;              /* in_rate / out_rate, 32.32 fixed point */
    uint64_t pos;               /* position in v[] (v[0] = last frame of the
                                   previous call, v[k] = in[k-1]), 32.32 */
    int16_t  last[2];
    int      primed;
} AudioResampler;
void AudioResampler_Init(AudioResampler *r, uint32_t in_rate, uint32_t out_rate);
int  AudioResampler_MaxOut(const AudioResampler *r, int in_frames);
/* in: in_frames frames of in_channels (1 or 2); out: stereo, room for
 * AudioResampler_MaxOut() frames. Returns the frames written. */
int  AudioResampler_Process(AudioResampler *r, const int16_t *in, int in_frames,
                            int in_channels, int16_t *out);

#endif
