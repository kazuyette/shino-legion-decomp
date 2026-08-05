#ifndef STREAM_H
#define STREAM_H

#include <stddef.h>
#include <stdint.h>

/* Ring buffer, ported from the RingBuf_* cluster in A.BIN
 * (0x0600a000-0x0600b900 — RingBuf_GetContiguousRead/AdvanceRead/
 * AdvanceWrite/ClearWrapTail). */
typedef struct {
    unsigned char *data;
    size_t capacity;
    size_t read_pos;
    size_t write_pos;
    size_t used; /* bytes currently buffered */
} RingBuffer;

void   RingBuf_Init(RingBuffer *rb, unsigned char *storage, size_t capacity);
size_t RingBuf_GetContiguousRead(RingBuffer *rb, unsigned char **out_ptr);
void   RingBuf_AdvanceRead(RingBuffer *rb, size_t n);
size_t RingBuf_GetContiguousWrite(RingBuffer *rb, unsigned char **out_ptr);
void   RingBuf_AdvanceWrite(RingBuffer *rb, size_t n);

/* Streaming audio engine, ported from the Stream_* cluster in A.BIN
 * (docs/jump_table_functions.md, "full streaming audio engine"). Simplified
 * for PC: the original parses a track-table header from DIRECTOR.PPB/
 * SHINOBI.PPB-adjacent data we can't read yet, so this version takes a
 * caller-supplied PCM fill callback instead of parsing that format. */

typedef int (*StreamFillCallback)(void *userdata, int16_t *dst, int num_samples);

typedef enum {
    STREAM_STATE_IDLE = 0,
    STREAM_STATE_INIT,
    STREAM_STATE_PLAYING,
    STREAM_STATE_CLOSED,
} StreamState;

#define STREAM_RING_BYTES (64 * 1024)

typedef struct {
    RingBuffer ring;
    unsigned char storage[STREAM_RING_BYTES];
    StreamState state;
    int volume;      /* 0-128, mirrors Stream_SetVolume */
    int channels;    /* 1 = mono, 2 = stereo */
    int underrun_count;
    StreamFillCallback fill_cb;
    void *fill_userdata;
} AudioStream;

/* FUN_0600b414-equivalent: sets up the stream's ring buffer and fill
 * callback, state -> INIT. */
void Stream_InitFromCallback(AudioStream *stream, StreamFillCallback cb, void *userdata, int channels);

/* Stream_BeginPlayback: marks the stream ready to be pulled by the audio
 * backend (state -> PLAYING). */
void Stream_BeginPlayback(AudioStream *stream);

/* Stream_Update: the per-tick pump (call once per frame from Boot_RunFrame
 * or similar) — pulls PCM from fill_cb into the ring buffer so the audio
 * backend always has data ready. */
void Stream_Update(AudioStream *stream);

/* Stream_FillSilence: buffer-underrun fallback. */
void Stream_FillSilence(int16_t *dst, int num_samples);

void Stream_SetVolume(AudioStream *stream, int volume);
void Stream_Close(AudioStream *stream);

/* SDL audio backend glue — call after SDL_Init(SDL_INIT_AUDIO). Opens the
 * default output device and mixes `stream`'s ring buffer to it. */
int  Stream_OpenAudioDevice(AudioStream *stream, int sample_rate);
void Stream_CloseAudioDevice(void);

#endif
