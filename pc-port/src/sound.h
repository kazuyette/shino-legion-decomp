#ifndef SOUND_H
#define SOUND_H

#include <stdint.h>

/* Ring-buffer command queue producers/consumer, traced from the
 * 0x06030000-0x06032000 cluster in A.BIN (7-entry x 16-byte ring buffer on
 * real hardware, feeding the SCSP's onboard M68K driver). On PC the queue
 * feeds Snd_ChannelScheduler directly instead — see sound.c. */

#define SND_QUEUE_SIZE 7
#define SND_NUM_CHANNELS 8

void Snd_CmdStopAll(void);
void Snd_CmdPlayNote(int channel, int note, int volume, int pan);
void Snd_CmdSetParamA(int value);
void Snd_CmdSetParamB(int value);
void Snd_CmdSetParamC(int value);
void Snd_CmdSetParam3(int a, int b, int c);

int  Snd_QueueHasSpace(void);

/* Real 8-channel voice-stealing scheduler, ported from FUN_0600863e /
 * Snd_ChannelScheduler + its helpers (Snd_FindFreeOrEvictChannel,
 * Snd_RefreshChannelDefs, Snd_QuickSortByPriority/Field10). Drains the
 * command queue: for each pending play request, finds a free channel or
 * evicts the lowest-priority one, and marks it active. */
void Snd_ChannelScheduler(void);

/* FUN_06008c08 — stop all channels whose sfx id or group id matches
 * id_or_group (matches whichever the caller means; real hardware
 * disambiguates via a separate flag we haven't recovered yet). */
void Snd_StopMatchingChannels(int id_or_group);

void Snd_LookupSfxDef(int index, uint16_t *out_masked_word, uint8_t *out_byte);

/* Voice allocator building blocks, exposed for testing/inspection. */
typedef struct {
    int active;      /* 0 = free, 1 = playing */
    int sfx_id;
    int group_id;
    int priority;     /* higher wins on contention, matches Snd_QuickSortByPriority */
    int note, volume, pan;
} SndChannel;

int Snd_FindFreeOrEvictChannel(int priority);
void Snd_RefreshChannelDefs(void);
const SndChannel *Snd_GetChannel(int index);

#endif
