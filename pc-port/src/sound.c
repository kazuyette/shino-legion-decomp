#include "sound.h"
#include <stdio.h>
#include <string.h>

/* --- Command queue (7 entries), mirrors the Snd_Cmd* ring buffer traced
 * in A.BIN's 0x06030000 cluster. --- */

typedef enum {
    SND_CMD_NONE = 0,
    SND_CMD_STOP_ALL,
    SND_CMD_PLAY_NOTE,
    SND_CMD_SET_PARAM_A,
    SND_CMD_SET_PARAM_B,
    SND_CMD_SET_PARAM_C,
    SND_CMD_SET_PARAM3,
} SndCmdType;

typedef struct {
    SndCmdType type;
    int a, b, c, d;
} SndCommand;

static SndCommand s_queue[SND_QUEUE_SIZE];
static int s_queue_count = 0;

static int Snd_PushCommand(SndCmdType type, int a, int b, int c, int d)
{
    if (s_queue_count >= SND_QUEUE_SIZE) {
        return 0; /* queue full, drop like the original ring buffer would */
    }
    s_queue[s_queue_count].type = type;
    s_queue[s_queue_count].a = a;
    s_queue[s_queue_count].b = b;
    s_queue[s_queue_count].c = c;
    s_queue[s_queue_count].d = d;
    s_queue_count++;
    return 1;
}

int Snd_QueueHasSpace(void)
{
    return s_queue_count < SND_QUEUE_SIZE;
}

void Snd_CmdStopAll(void)
{
    Snd_PushCommand(SND_CMD_STOP_ALL, 0, 0, 0, 0);
}

void Snd_CmdPlayNote(int channel, int note, int volume, int pan)
{
    /* "channel" here is really the sfx/group id passed at the call site
     * (see Snd_ChannelScheduler's real caller in A.BIN) — kept as-is to
     * match the original signature. */
    Snd_PushCommand(SND_CMD_PLAY_NOTE, channel, note, volume, pan);
}

void Snd_CmdSetParamA(int value) { Snd_PushCommand(SND_CMD_SET_PARAM_A, value, 0, 0, 0); }
void Snd_CmdSetParamB(int value) { Snd_PushCommand(SND_CMD_SET_PARAM_B, value, 0, 0, 0); }
void Snd_CmdSetParamC(int value) { Snd_PushCommand(SND_CMD_SET_PARAM_C, value, 0, 0, 0); }
void Snd_CmdSetParam3(int a, int b, int c) { Snd_PushCommand(SND_CMD_SET_PARAM3, a, b, c, 0); }

static const uint16_t *s_sfx_table = NULL;
static int s_sfx_table_count = 0;
static uint16_t s_sfx_table_mask = 0xffff;

void Snd_SetSfxTable(const uint16_t *table, int count, uint16_t mask)
{
    s_sfx_table = table;
    s_sfx_table_count = count;
    s_sfx_table_mask = mask;
}

void Snd_LookupSfxDef(int index, uint16_t *out_masked_word, uint8_t *out_byte)
{
    /* Confirmed algorithm from FUN_06031388's disassembly: entry =
     * table[index]; masked_word = entry & mask; byte = low 8 bits of the
     * SAME entry, unmasked. With no table loaded (real SFX data not
     * extracted yet), every lookup correctly reports "not found" (0/0). */
    uint16_t entry = 0;
    if (s_sfx_table && index >= 0 && index < s_sfx_table_count) {
        entry = s_sfx_table[index];
    }
    if (out_masked_word) *out_masked_word = entry & s_sfx_table_mask;
    if (out_byte) *out_byte = (uint8_t)(entry & 0xff);
}

/* --- 8-channel voice-stealing scheduler --- */

static SndChannel s_channels[SND_NUM_CHANNELS];

const SndChannel *Snd_GetChannel(int index)
{
    if (index < 0 || index >= SND_NUM_CHANNELS) return NULL;
    return &s_channels[index];
}

void Snd_RefreshChannelDefs(void)
{
    /* FUN_06007c14: refresh each channel's definition; mark unused if the
     * definition lookup comes back empty. Real def data isn't available
     * yet (needs the SFX table), so this currently just clears channels
     * whose sfx_id is 0 (matches the "empty def" case). */
    for (int i = 0; i < SND_NUM_CHANNELS; i++) {
        if (s_channels[i].active && s_channels[i].sfx_id == 0) {
            s_channels[i].active = 0;
        }
    }
}

int Snd_FindFreeOrEvictChannel(int priority)
{
    /* FUN_06007c84: scan for a free channel first. */
    for (int i = 0; i < SND_NUM_CHANNELS; i++) {
        if (!s_channels[i].active) {
            return i;
        }
    }
    /* None free — evict the lowest-priority active channel if the new
     * request outranks it. */
    int lowest = 0;
    for (int i = 1; i < SND_NUM_CHANNELS; i++) {
        if (s_channels[i].priority < s_channels[lowest].priority) {
            lowest = i;
        }
    }
    if (priority > s_channels[lowest].priority) {
        return lowest;
    }
    return -1; /* nothing to steal, request dropped */
}

static void Snd_PlayOnChannel(int ch, int sfx_id, int note, int volume, int pan)
{
    if (ch < 0) {
        printf("[snd] no channel available for sfx=%d, dropped\n", sfx_id);
        return;
    }
    s_channels[ch].active = 1;
    s_channels[ch].sfx_id = sfx_id;
    s_channels[ch].note = note;
    s_channels[ch].volume = volume;
    s_channels[ch].pan = pan;
    s_channels[ch].priority = volume; /* placeholder: louder = higher priority */
    printf("[snd] ch=%d play sfx=%d note=%d vol=%d pan=%d\n", ch, sfx_id, note, volume, pan);
}

void Snd_StopMatchingChannels(int id_or_group)
{
    for (int i = 0; i < SND_NUM_CHANNELS; i++) {
        if (s_channels[i].active &&
            (s_channels[i].sfx_id == id_or_group || s_channels[i].group_id == id_or_group)) {
            s_channels[i].active = 0;
            printf("[snd] ch=%d stopped (match %d)\n", i, id_or_group);
        }
    }
}

static void Snd_StopAllChannels(void)
{
    memset(s_channels, 0, sizeof(s_channels));
    printf("[snd] all channels stopped\n");
}

void Snd_ChannelScheduler(void)
{
    /* Drain the command queue (FUN_0600863e's role): apply stop/param
     * commands directly, and run play-note requests through the
     * find-free-or-evict allocator. */
    for (int i = 0; i < s_queue_count; i++) {
        SndCommand *cmd = &s_queue[i];
        switch (cmd->type) {
            case SND_CMD_STOP_ALL:
                Snd_StopAllChannels();
                break;
            case SND_CMD_PLAY_NOTE: {
                int sfx_id = cmd->a, note = cmd->b, volume = cmd->c, pan = cmd->d;
                int ch = Snd_FindFreeOrEvictChannel(volume);
                Snd_PlayOnChannel(ch, sfx_id, note, volume, pan);
                break;
            }
            case SND_CMD_SET_PARAM_A:
            case SND_CMD_SET_PARAM_B:
            case SND_CMD_SET_PARAM_C:
            case SND_CMD_SET_PARAM3:
                /* TODO: exact per-channel parameter semantics not yet
                 * recovered from A.BIN — no-op for now. */
                break;
            default:
                break;
        }
    }
    s_queue_count = 0;

    Snd_RefreshChannelDefs();
}
