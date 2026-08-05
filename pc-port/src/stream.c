#include "stream.h"
#include <string.h>
#include <stdio.h>
#include <SDL2/SDL.h>

/* --- Ring buffer, ported from RingBuf_* --- */

void RingBuf_Init(RingBuffer *rb, unsigned char *storage, size_t capacity)
{
    rb->data = storage;
    rb->capacity = capacity;
    rb->read_pos = 0;
    rb->write_pos = 0;
    rb->used = 0;
}

size_t RingBuf_GetContiguousRead(RingBuffer *rb, unsigned char **out_ptr)
{
    if (rb->used == 0) {
        *out_ptr = NULL;
        return 0;
    }
    size_t until_wrap = rb->capacity - rb->read_pos;
    size_t avail = (rb->used < until_wrap) ? rb->used : until_wrap;
    *out_ptr = rb->data + rb->read_pos;
    return avail;
}

void RingBuf_AdvanceRead(RingBuffer *rb, size_t n)
{
    if (n > rb->used) n = rb->used;
    rb->read_pos = (rb->read_pos + n) % rb->capacity;
    rb->used -= n;
}

size_t RingBuf_GetContiguousWrite(RingBuffer *rb, unsigned char **out_ptr)
{
    size_t free_bytes = rb->capacity - rb->used;
    if (free_bytes == 0) {
        *out_ptr = NULL;
        return 0;
    }
    size_t until_wrap = rb->capacity - rb->write_pos;
    size_t avail = (free_bytes < until_wrap) ? free_bytes : until_wrap;
    *out_ptr = rb->data + rb->write_pos;
    return avail;
}

void RingBuf_AdvanceWrite(RingBuffer *rb, size_t n)
{
    size_t free_bytes = rb->capacity - rb->used;
    if (n > free_bytes) n = free_bytes;
    rb->write_pos = (rb->write_pos + n) % rb->capacity;
    rb->used += n;
}

/* --- Streaming audio engine, ported from the Stream_* cluster --- */

void Stream_InitFromCallback(AudioStream *stream, StreamFillCallback cb, void *userdata, int channels)
{
    memset(stream, 0, sizeof(*stream));
    RingBuf_Init(&stream->ring, stream->storage, sizeof(stream->storage));
    stream->fill_cb = cb;
    stream->fill_userdata = userdata;
    stream->channels = (channels > 0) ? channels : 1;
    stream->volume = 128;
    stream->state = STREAM_STATE_INIT;
}

void Stream_BeginPlayback(AudioStream *stream)
{
    if (stream->state == STREAM_STATE_INIT || stream->state == STREAM_STATE_IDLE) {
        stream->state = STREAM_STATE_PLAYING;
    }
}

void Stream_Update(AudioStream *stream)
{
    if (stream->state != STREAM_STATE_PLAYING || !stream->fill_cb) {
        return;
    }

    size_t bytes_per_frame = (size_t)stream->channels * sizeof(int16_t);

    for (;;) {
        unsigned char *dst;
        size_t space = RingBuf_GetContiguousWrite(&stream->ring, &dst);
        size_t num_samples = space / bytes_per_frame;
        if (num_samples == 0) {
            break; /* ring full (or remaining contiguous run too small) */
        }

        int filled = stream->fill_cb(stream->fill_userdata, (int16_t *)dst, (int)num_samples);
        if (filled <= 0) {
            break; /* nothing more to produce right now */
        }

        RingBuf_AdvanceWrite(&stream->ring, (size_t)filled * bytes_per_frame);

        if ((size_t)filled < num_samples) {
            break; /* callback gave a partial chunk, try again next tick */
        }
    }
}

void Stream_FillSilence(int16_t *dst, int num_samples)
{
    memset(dst, 0, (size_t)num_samples * sizeof(int16_t));
}

void Stream_SetVolume(AudioStream *stream, int volume)
{
    if (volume < 0) volume = 0;
    if (volume > 128) volume = 128;
    stream->volume = volume;
}

void Stream_Close(AudioStream *stream)
{
    stream->state = STREAM_STATE_CLOSED;
}

/* --- SDL2 audio backend glue --- */

static SDL_AudioDeviceID s_device = 0;

static void Stream_AudioCallback(void *userdata, Uint8 *out, int len)
{
    AudioStream *stream = (AudioStream *)userdata;
    int written = 0;

    while (written < len) {
        unsigned char *src;
        size_t avail = RingBuf_GetContiguousRead(&stream->ring, &src);
        if (avail == 0) {
            /* Underrun: pad the rest of the buffer with silence. */
            Stream_FillSilence((int16_t *)(out + written), (len - written) / (int)sizeof(int16_t));
            stream->underrun_count++;
            break;
        }
        int chunk = (int)avail;
        if (chunk > len - written) chunk = len - written;

        if (stream->volume >= 128) {
            memcpy(out + written, src, (size_t)chunk);
        } else {
            /* Simple linear volume scale (0-128). */
            int16_t *s16 = (int16_t *)src;
            int16_t *d16 = (int16_t *)(out + written);
            int count = chunk / (int)sizeof(int16_t);
            for (int i = 0; i < count; i++) {
                d16[i] = (int16_t)((s16[i] * stream->volume) / 128);
            }
        }

        RingBuf_AdvanceRead(&stream->ring, (size_t)chunk);
        written += chunk;
    }
}

int Stream_OpenAudioDevice(AudioStream *stream, int sample_rate)
{
    SDL_AudioSpec want, have;
    memset(&want, 0, sizeof(want));
    want.freq = sample_rate;
    want.format = AUDIO_S16SYS;
    want.channels = (Uint8)stream->channels;
    want.samples = 1024;
    want.callback = Stream_AudioCallback;
    want.userdata = stream;

    s_device = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (s_device == 0) {
        fprintf(stderr, "[stream] SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
        return 0;
    }
    SDL_PauseAudioDevice(s_device, 0);
    return 1;
}

void Stream_CloseAudioDevice(void)
{
    if (s_device != 0) {
        SDL_CloseAudioDevice(s_device);
        s_device = 0;
    }
}
