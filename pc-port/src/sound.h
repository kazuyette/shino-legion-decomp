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

/* FUN_06031388 — confirmed algorithm (disassembly, not just decompile):
 * table entry = sfx_table[index] (16-bit, table pointed to by a fixed
 * literal); out_masked_word = entry & mask (mask constant confirmed
 * present in the literal pool, exact value not pinned down -- doesn't
 * matter yet, see below); out_byte = low 8 bits of that SAME entry,
 * unmasked. Called both from Snd_RefreshChannelDefs (every channel, every
 * scheduler pass) and Snd_ChannelScheduler's state-3 path, in both cases
 * to test "does this sfx id resolve to a real definition" (0 = no).
 * Algorithm is ported below; the actual sfx_table contents are game data
 * we haven't extracted yet, so an empty table correctly yields "not
 * found" (0/0) for every lookup until real data is wired in via
 * Snd_SetSfxTable. */
void Snd_LookupSfxDef(int index, uint16_t *out_masked_word, uint8_t *out_byte);

/* Wires in the real SFX definition table once extracted from the disc
 * (array of `count` 16-bit entries) and its mask constant. Until called,
 * Snd_LookupSfxDef behaves as if the table is empty (every lookup "not
 * found"), which is the correct/safe default. */
void Snd_SetSfxTable(const uint16_t *table, int count, uint16_t mask);

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
