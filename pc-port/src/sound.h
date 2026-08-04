#ifndef SOUND_H
#define SOUND_H

#include <stdint.h>

/* Ring-buffer command queue producers/consumer, traced from the
 * 0x06030000-0x06032000 cluster in A.BIN. On real hardware these push
 * 16-byte tagged commands for the SCSP's onboard M68K driver to consume;
 * that driver isn't in A.BIN, so on PC this queue needs a real consumer
 * (an SDL_mixer/OpenAL backend) — stubbed for now. */

void Snd_CmdStopAll(void);
void Snd_CmdPlayNote(int channel, int note, int volume, int pan);
void Snd_CmdSetParamA(int value);
void Snd_CmdSetParamB(int value);
void Snd_CmdSetParamC(int value);
void Snd_CmdSetParam3(int a, int b, int c);

int  Snd_QueueHasSpace(void);
void Snd_ChannelScheduler(void);
void Snd_StopMatchingChannels(int id_or_group);
void Snd_LookupSfxDef(int index, uint16_t *out_masked_word, uint8_t *out_byte);

#endif
