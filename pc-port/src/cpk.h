/*
 * cpk.h - Sega FILM (.CPK) demuxer + Cinepak video / PCM audio decoder.
 *
 * Self-contained C11, no external dependencies (no SDL, no libm).
 *
 * Container (all fields big-endian):
 *   "FILM" u32 header_len  char version[4] ("1.01"/"1.06")  u32 0
 *   "FDSC" u32 0x20  "cvid"  u32 height  u32 width  u8 bpp(24)
 *          u8 audio_channels  u8 audio_bits  u8 audio_compression  u16 rate  u8 pad[6]
 *   "STAB" u32 len  u32 time_base  u32 count  { u32 offset, u32 size, u32 info1, u32 info2 }[count]
 *   chunk data follows at file offset header_len (offsets are relative to it).
 *   info1 == 0xFFFFFFFF -> audio chunk (info2 = 1)
 *   otherwise video: pts = info1 & 0x7FFFFFFF (time_base units), bit31 set = NOT a
 *   keyframe, info2 = frame duration (time_base units).
 *
 * Video chunk = 10-byte Cinepak frame header + 2 Sega padding bytes + strips.
 *   (The Cinepak 24-bit size field equals chunk_size - 8 in Sega's files.)
 *
 * Audio: PCM, 8-bit signed or 16-bit big-endian, stereo stored planar per chunk
 *   (all left samples then all right samples). compression==2 -> CRI ADX (untested:
 *   not used by Shinobi Legions).
 *
 * Usage (player):
 *   CpkMovie *m = Cpk_Open("STGSTRT1.CPK");
 *   uint32_t *fb = malloc(Cpk_Width(m) * Cpk_Height(m) * 4);
 *   int t; while (Cpk_NextFrame(m, fb, &t) > 0) { wait until clock >= t; present fb; }
 *   audio thread / callback: n = Cpk_ReadAudio(m, buf, frames);  (independent cursor)
 *   Cpk_Close(m);
 *
 * Not thread-safe per movie: if audio is pulled from another thread, serialise the
 * calls on the same CpkMovie (a mutex), or pre-buffer audio on the main thread.
 */
#ifndef CPK_H
#define CPK_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CpkMovie CpkMovie;

/* Audio compression codes (FDSC byte 0x17). */
enum { CPK_AUDIO_PCM = 0, CPK_AUDIO_ADX = 2 };

/* Open from a file path. Chunks are read on demand (two independent file cursors
 * are emulated with fseek: video and audio). Returns NULL on error (see Cpk_LastError). */
CpkMovie *Cpk_Open(const char *path);

/* Open from memory. The buffer is NOT copied and must outlive the movie. */
CpkMovie *Cpk_OpenMem(const void *data, size_t size);

void Cpk_Close(CpkMovie *m);

/* Last open error (static string), for Cpk_Open/Cpk_OpenMem returning NULL. */
const char *Cpk_LastError(void);

/* ---- stream info ---- */
int Cpk_Width(const CpkMovie *m);
int Cpk_Height(const CpkMovie *m);
int Cpk_FrameCount(const CpkMovie *m);   /* number of video chunks */
int Cpk_TimeBase(const CpkMovie *m);     /* STAB ticks per second (24, 30 or 600 here) */
double Cpk_FrameRate(const CpkMovie *m); /* frames per second (count / video duration) */
int Cpk_DurationMs(const CpkMovie *m);   /* max(video end, audio end) */

int Cpk_AudioRate(const CpkMovie *m);    /* Hz, 0 if no audio */
int Cpk_AudioChannels(const CpkMovie *m);/* 0 if no audio */
int Cpk_AudioBits(const CpkMovie *m);    /* bits per sample in the file (8/16) */
int Cpk_AudioCompression(const CpkMovie *m); /* CPK_AUDIO_PCM / CPK_AUDIO_ADX / other */
long Cpk_AudioFrames(const CpkMovie *m); /* total sample frames (per channel) */

/* ---- video ---- */
/* Decode the next video frame (presentation order == file order in FILM).
 * argb: Width*Height pixels, 0xAARRGGBB (A=0xFF), row-major, no padding; may be NULL
 *       (frame is still decoded - inter frames depend on it - but not copied out).
 * timestamp_ms: presentation time of this frame in ms from movie start (may be NULL).
 * Returns 1 = frame decoded, 0 = end of movie, -1 = corrupt frame (skipped, previous
 * image kept and copied out, playback can continue). */
int Cpk_NextFrame(CpkMovie *m, uint32_t *argb, int *timestamp_ms);

/* Timestamp (ms) of the frame Cpk_NextFrame would return next, -1 at end.
 * Lets a player drop/hold frames without decoding. */
int Cpk_PeekFrameTime(const CpkMovie *m);

/* Index (0-based) of the next video frame. */
int Cpk_FrameIndex(const CpkMovie *m);

/* ---- audio ---- */
/* Copy up to maxSamples sample FRAMES (per channel; for stereo 2*maxSamples int16
 * values are written interleaved L,R) of native-endian s16 PCM, in presentation
 * order, from an audio cursor independent of the video cursor.
 * Returns the number of frames written, 0 at end of audio. */
int Cpk_ReadAudio(CpkMovie *m, int16_t *out, int maxSamples);

/* Audio frames already returned by Cpk_ReadAudio (audio clock = this / rate). */
long Cpk_AudioPosition(const CpkMovie *m);

/* Restart video and audio from the beginning (codebooks and image reset). */
void Cpk_Rewind(CpkMovie *m);

/* ---- diagnostics (used by tests) ---- */
typedef struct CpkStats {
    int frames_decoded;
    int frames_bad;          /* frames rejected by the decoder */
    int keyframes;           /* frames whose STAB entry has bit31 clear */
    int strips;
    int strip_size_mismatch; /* strips whose chunks don't sum exactly to the strip size */
    int frame_size_mismatch; /* frames whose strips don't sum exactly to the chunk size */
    int vector_slack;        /* vector chunks with unused trailing bytes (>3) */
    int cb_slack;            /* codebook chunks with unused trailing bytes (>3) */
    int chunk_hist[16];      /* by (id>>8)&0xF: 0x20..0x27 -> [0..7], 0x30..0x32 -> [8..10],
                                anything else -> [15] */
    int sega_skip;           /* bytes skipped after the 10-byte frame header (2 here) */
} CpkStats;
void Cpk_GetStats(const CpkMovie *m, CpkStats *out);

/* Raw STAB access (diagnostics). Returns 0 if index out of range. */
int Cpk_GetSample(const CpkMovie *m, int index, uint32_t *offset, uint32_t *size,
                  uint32_t *info1, uint32_t *info2);
int Cpk_SampleCount(const CpkMovie *m);
uint32_t Cpk_DataOffset(const CpkMovie *m); /* file offset of chunk data */
const char *Cpk_Version(const CpkMovie *m); /* "1.01", "1.06", ... */

#ifdef __cplusplus
}
#endif
#endif /* CPK_H */
