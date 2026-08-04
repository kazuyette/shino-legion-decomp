#include "sound.h"
#include <stdio.h>

/* Minimal skeleton: log calls so we can see the game "trying" to make
 * sound while the real logic is ported function by function. */

void Snd_CmdStopAll(void)
{
    printf("[snd] StopAll\n");
}

void Snd_CmdPlayNote(int channel, int note, int volume, int pan)
{
    printf("[snd] PlayNote ch=%d note=%d vol=%d pan=%d\n", channel, note, volume, pan);
}

void Snd_CmdSetParamA(int value) { printf("[snd] SetParamA %d\n", value); }
void Snd_CmdSetParamB(int value) { printf("[snd] SetParamB %d\n", value); }
void Snd_CmdSetParamC(int value) { printf("[snd] SetParamC %d\n", value); }
void Snd_CmdSetParam3(int a, int b, int c) { printf("[snd] SetParam3 %d %d %d\n", a, b, c); }

int Snd_QueueHasSpace(void)
{
    /* Stub queue is unbounded on PC, always report space available. */
    return 1;
}

void Snd_ChannelScheduler(void)
{
    /* TODO: port FUN_0600863e (8-channel SCSP scheduler). */
}

void Snd_StopMatchingChannels(int id_or_group)
{
    (void)id_or_group;
    /* TODO: port FUN_06008c08. */
}

void Snd_LookupSfxDef(int index, uint16_t *out_masked_word, uint8_t *out_byte)
{
    (void)index;
    /* TODO: port FUN_06031388 once the SFX definition table is dumped. */
    if (out_masked_word) *out_masked_word = 0;
    if (out_byte) *out_byte = 0;
}
