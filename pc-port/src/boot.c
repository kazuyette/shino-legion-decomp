#include "boot.h"
#include "sound.h"
#include "object.h"
#include "mode.h"
#include "resource.h"
#include "sys.h"
#include "stream.h"
#include <stdio.h>
#include <math.h>

static unsigned char s_director_buffer[256 * 1024];

/* --- Streaming audio wiring ---
 * We don't have real PPB audio/track data yet (blocked on DIRECTOR.PPB
 * being readable -- see docs), so this feeds Stream_Update a synthesized
 * 440Hz test tone through the exact same ring-buffer/SDL2 path the real
 * streaming engine (docs: Stream_InitFromHeader/Stream_Update/RingBuf_*)
 * would use. Proves the port of that subsystem actually works end-to-end;
 * swap TestToneFillCallback for a real decoder once track data is available. */
static AudioStream s_music_stream;
static double s_tone_phase = 0.0;

static int TestToneFillCallback(void *userdata, int16_t *dst, int num_samples)
{
    (void)userdata;
    const double freq = 440.0;
    const double sample_rate = 44100.0;
    for (int i = 0; i < num_samples; i++) {
        int16_t sample = (int16_t)(3000.0 * sin(s_tone_phase));
        dst[i * 2 + 0] = sample; /* left */
        dst[i * 2 + 1] = sample; /* right */
        s_tone_phase += 2.0 * 3.14159265358979323846 * freq / sample_rate;
        if (s_tone_phase > 2.0 * 3.14159265358979323846) {
            s_tone_phase -= 2.0 * 3.14159265358979323846;
        }
    }
    return num_samples;
}

void Boot_Init(void)
{
    printf("[boot] init\n");
    Obj_InitLinkedLists();
    Reset_ObjectAndChannelTables();
    /* "assets" is the extracted disc file tree, see README for layout. */
    Load_DirectorPPB("assets", s_director_buffer, sizeof(s_director_buffer));

    Stream_InitFromCallback(&s_music_stream, TestToneFillCallback, NULL, 2);
    Stream_BeginPlayback(&s_music_stream);
    if (!Stream_OpenAudioDevice(&s_music_stream, 44100)) {
        fprintf(stderr, "[boot] audio device failed to open, continuing without sound\n");
    }
}

void Boot_Shutdown(void)
{
    Stream_CloseAudioDevice();
}

/* The 4 fixed object "slots" processed every frame in the real main loop,
 * identified by their mode bitflags (docs/jump_table_functions.md, "Big
 * find: the real per-frame main loop body"). Slots 1-2 get an extra
 * param-copy step in the original (FUN_060042ac, SGL-internals territory,
 * not ported here) that slots 3-4 skip. */
static const int kObjectSlotModes[4] = {4, 8, 0x10, 0x20};

void Boot_RunFrame(void)
{
    Sys_SignalStopAndWaitAck();

    /* NOTE: the real Boot_And_MainLoop calls Stage_ResetAndLoadDirector()
     * here every single frame, but that function internally gates almost
     * all of its work behind a CD/peripheral-ready check and only does
     * real work on an actual stage transition (see docs). We don't have
     * that gating condition ported yet, so we deliberately do NOT call it
     * from here — call it explicitly when a real stage-transition event
     * fires, to avoid spamming a reload every frame on PC. */

    for (int i = 0; i < 4; i++) {
        int mode = kObjectSlotModes[i];
        Obj_ConsumeFlagAndStore(mode);
        Obj_SetTransformParam(mode, 0, 0); /* TODO: real per-slot data once DIRECTOR.PPB is readable */
        Obj_SetReadyFlag();
    }

    Snd_ChannelScheduler();
    Stream_Update(&s_music_stream);
}

void Stage_ResetAndLoadDirector(void)
{
    printf("[boot] stage reset\n");
    Sys_SignalStopAndWaitAck();
    Snd_CmdStopAll();
    Res_CloseIfOpen();
    Mode_EnterIdle();
    Reset_ObjectAndChannelTables();
    Load_DirectorPPB("assets", s_director_buffer, sizeof(s_director_buffer));
}
