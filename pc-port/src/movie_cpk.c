/* CPK movie backend for the game: Sega FILM / Cinepak decoder (cpk.c) + audio (audio.c).
 * Plays at the movie's own timestamps, keeps ~0.3 s of audio queued, and hands every
 * decoded frame to Movie_PresentFrame (RGB VDP1 character). The video clock is the audio
 * actually played (audio master): a 60 Hz tick clock drifted on long movies (ENDING.CPK)
 * because the PC loop is not exactly 60 Hz. Without audio, or once it has run out, the
 * clock continues with 60 Hz ticks from the last audio time. */
#include "movie_backend.h"
#include "movie.h"
#include "cpk.h"
#include "audio.h"
#include "ram.h"
#include "resource.h"
#include <stdio.h>
#include <stdlib.h>

static CpkMovie *s_m;
static uint32_t *s_argb;
static uint16_t *s_rgb;
static int s_ticks, s_audioDone, s_endTick;
static long long s_queuedOut;            /* output frames handed to Audio_QueueMovie */
static int s_clockMs, s_clockTick;       /* last audio-derived time and the tick it was taken */

static int start(CpkMovie *m, const char *what)
{
    if (!m) { printf("[movie] cannot open %s: %s\n", what, Cpk_LastError()); return 0; }
    s_m = m;
    int n = Cpk_Width(m) * Cpk_Height(m);
    s_argb = (uint32_t *)malloc((size_t)n * 4);
    s_rgb = (uint16_t *)malloc((size_t)n * 2);
    s_ticks = 0; s_endTick = -1;
    s_queuedOut = 0; s_clockMs = 0; s_clockTick = 0;
    s_audioDone = Cpk_AudioRate(m) <= 0;
    printf("[movie] %s %dx%d %.2f fps, %d frames, audio %d Hz x%d\n", what, Cpk_Width(m), Cpk_Height(m),
           Cpk_FrameRate(m), Cpk_FrameCount(m), Cpk_AudioRate(m), Cpk_AudioChannels(m));
    return 1;
}

int MovieBk_OpenFile(const char *name)
{
    char p[600];
    MovieBk_Close();
    snprintf(p, sizeof p, "%s/%s", Res_GetAssetDir(), name);
    return start(Cpk_Open(p), name);
}

int MovieBk_OpenMem(uint32_t addr, uint32_t size)
{
    MovieBk_Close();
    return start(Cpk_OpenMem(Ram_Ptr(addr), size), "movie in RAM");
}

static void feed_audio(void)
{
    static int16_t buf[4096 * 2];
    while (!s_audioDone && Audio_MovieQueuedFrames() < 44100 * 3 / 10) {
        int got = Cpk_ReadAudio(s_m, buf, 4096);
        if (got <= 0) { s_audioDone = 1; break; }
        s_queuedOut += Audio_QueueMovie(buf, got, Cpk_AudioRate(s_m), Cpk_AudioChannels(s_m));
    }
}

int MovieBk_TaskFrame(void)
{
    if (!s_m) return 1;
    feed_audio();
    int queued = Audio_MovieQueuedFrames();
    if (Cpk_AudioRate(s_m) > 0 && s_queuedOut > 0 && !(s_audioDone && queued == 0)) {
        s_clockMs = (int)((s_queuedOut - queued) * 1000 / AUDIO_RATE);   /* audio played so far */
        s_clockTick = s_ticks;
    }
    int now_ms = s_clockMs + (s_ticks - s_clockTick) * 1000 / 60;
    s_ticks++;
    int t, shown = 0;
    while (Cpk_PeekFrameTime(s_m) >= 0 && Cpk_PeekFrameTime(s_m) <= now_ms) {
        if (Cpk_NextFrame(s_m, s_argb, &t) < 0) continue;      /* corrupt: keep the last image */
        shown = 1;
    }
    if (shown) {
        int n = Cpk_Width(s_m) * Cpk_Height(s_m);
        for (int i = 0; i < n; i++) {
            uint32_t c = s_argb[i];
            s_rgb[i] = (uint16_t)(((c >> 19) & 31) | ((c >> 11) & 31) << 5 | ((c >> 3) & 31) << 10);
        }
        Movie_PresentFrame(s_rgb, Cpk_Width(s_m), Cpk_Height(s_m));
    }
    if (Cpk_PeekFrameTime(s_m) < 0) {                          /* last frame shown */
        if (s_endTick < 0) s_endTick = s_ticks;
        if ((s_audioDone && Audio_MovieQueuedFrames() == 0) || s_ticks - s_endTick > 30) return 1;
    }
    return 0;
}

void MovieBk_Close(void)
{
    if (s_m) { Cpk_Close(s_m); s_m = NULL; }
    free(s_argb); s_argb = NULL;
    free(s_rgb); s_rgb = NULL;
    Audio_StopMovie();
}
