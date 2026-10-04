#ifndef SFX_H
#define SFX_H
/* SCSP sound banks (.SND) and a replacement for the Saturn sound path: the SH-2 request
 * queue / slot scheduler of A.BIN and the 68000 SBL sound driver (A.BIN 0x0600F318), with
 * the SCSP slots it programs. See docs/sound.md.
 *
 * Saturn side (A.BIN):
 *  - Res_Request(1, kind 2, ..., "STAGEn.SND"): 0x06004C52 streams the file into
 *    sound RAM: 0x80-byte SBL area map (copied to 0x0601AB44, driver = A.BIN
 *    0x0600F318, 0x50CE bytes, SND init 0x060304B0), then blocks {u16 id, word-LZ}
 *    decompressed by 0x06004AA0 to 0x25A00000 + the map address of that id.
 *  - Snd_Play(id) 0x06007CDE: id -> {seq bank, song, priority} (tables 0x0600EF20 /
 *    0x0600EF50 / 0x0600F0D0), queued by 0x06007FA8 (17 x 12 B at 0x06025178);
 *    0x0600863E (once per frame) runs the queue on the 8 sequence slots 0x06025138
 *    (slot 0 = tunes, 1-7 = sound effects) through the SBL commands.
 *  - The 68000 driver plays the SBL sequences (SEQ, 2 ms timer ticks) and keys SCSP
 *    slots from the tone banks (TON layers = SCSP slot register images + PCM).
 *
 * PC: one bank (the last .SND loaded) is resident. Sfx_Render() runs, sample-exactly,
 * the SH-2 scheduler every 735 samples (1/60 s), the driver's timer-B tick every 88
 * samples (sequencer, note table, fades) and its timer-A main-loop pitch refresh every
 * 89 samples, and mixes the 32 SCSP slots.
 *
 * Threading: the Sfx_* engine functions are NOT thread-safe. The game side goes
 * through audio.c (Audio_PlaySfx / Audio_StopSfx take the device lock and call the
 * hooks registered here) or through the *_Locked helpers below. Tests drive the
 * engine directly (Sfx_SetBank / Sfx_Play / Sfx_Render). */
#include <stddef.h>
#include <stdint.h>

#define SFX_RATE   44100
#define SFX_SLOTS  8        /* sequences playing at once (SBL: 8) */
#define SFX_VOICES 32       /* SCSP slots */
#define SFX_AREAS  16       /* bank ids 0..15 per type */
#define SFX_TICK_SAMPLES  88    /* 68K timer B (TBCTL 1, TIMB 0xD4): 44 x 2 samples = 501.1 Hz */
#define SFX_MAIN_SAMPLES  89    /* 68K timer A (TACTL 0, TIMA 0xA7): main-loop pitch refresh */
#define SFX_FRAME_SAMPLES 735   /* SH-2 scheduler 0x0600863E: once per 1/60 s */

/* ---- bank (.SND file) -------------------------------------------------- */
typedef struct {
    const uint8_t *data;    /* decompressed block (NULL: absent) */
    uint32_t size;          /* bytes */
    uint32_t addr, alloc;   /* sound RAM address / size from the area map */
} SfxArea;

typedef struct {
    char    name[16];
    SfxArea seq[SFX_AREAS];     /* area type 1: sequence banks */
    SfxArea tone[SFX_AREAS];    /* area type 0: tone banks (TON) */
    int     ndsp;               /* area type 2/3 blocks (DSP program / work): 0 in every file */
    int     nmap;               /* area map entries */
    uint8_t map[16][8];         /* raw area map */
    int     lz_window_refs;     /* LZ refs before the block start (0 expected) */
    int     nblk;
    uint8_t *blk[32];           /* owned block data */
} SfxBank;

/* NULL on error (message on stdout). */
SfxBank *SfxBank_Parse(const uint8_t *buf, size_t len, const char *name);
SfxBank *SfxBank_Load(const char *path);
void     SfxBank_Free(SfxBank *b);
int      SfxBank_SongCount(const SfxBank *b, int seq_bank);   /* 0 if absent */
int      SfxBank_VoiceCount(const SfxBank *b, int tone_bank);

/* ---- Snd_Play id table (A.BIN 0x0600EF20 / 0x0600EF50 / 0x0600F0D0) ------ */
typedef struct {
    uint8_t seq_bank, song, prio;
    uint8_t music;          /* ids < 0x100: Snd_Play passes flag 1 (music/jingles) */
} SfxDef;
int Sfx_Lookup(int id, SfxDef *out);    /* 1 = id known */

/* ---- engine (not thread-safe) ------------------------------------------ */
SfxBank *Sfx_SetBank(SfxBank *b);       /* SND_Init: resets driver + queue, returns the previous bank */
const SfxBank *Sfx_GetBank(void);
/* SH-2 side (requests run by the scheduler at the next 1/60 s boundary of Sfx_Render) */
int  Sfx_Play(int id);                  /* Snd_Play 0x06007CDE: 1 = queued (or already pending) */
int  Sfx_PlaySong(int seq_bank, int song, int prio, int id, int music);   /* 0x06007FA8 */
void Sfx_StopAll(void);                 /* Snd_StopAll 0x06007E00 */
int  Sfx_StopSlot(int slot);            /* 0x060084BE: queue a stop of a slot */
void Sfx_StopId(int id);                /* PC: 0x060084BE on every slot playing id */
int  Sfx_SetSeqVolume(int slot, int vol, int rate);  /* 0x06008154 (vol 0..127, rate x 0.1 s) */
void Sfx_SetAllSeqVolume(int vol, int rate);         /* 0x06007DBA: 0x06008154 on the busy slots */
int  Sfx_SlotId(int slot);              /* 0x06025138[slot] +0 (-1 = free) */
int  Sfx_Count(int id);                 /* slots playing id (0x06007D60 check) */
int  Sfx_Pending(void);                 /* queued requests (0x06025244) */
/* driver side */
int  Sfx_ActiveSlots(void);             /* sequences running in the driver */
int  Sfx_ActiveVoices(void);            /* SCSP slots sounding */
int  Sfx_SlotAttenuation(int slot);     /* sequence attenuation 0..127 (fade position) */
void Sfx_SetMono(int mono);             /* driver mono flag (68K 0x483, option AUDIO = MONO) */
void Sfx_SetVolume(float v);            /* PC master gain, default 0.9 (1.0: one full slot = full scale) */
/* Adds `frames` 44.1 kHz stereo frames into acc (interleaved L/R). */
void Sfx_Render(int32_t *acc, int frames);
/* Direct SBL calls (sound test, no queue): SND_StartSeq 0x06030A82 / SND_StopSeq 0x06030B24. */
int  Sfx_StartSeqDirect(int slot, int seq_bank, int song);
void Sfx_StopSeqDirect(int slot);

/* ---- driver arithmetic (exposed for tests) ------------------------------- */
/* 68K 0x2FD2: velocity table entry (10 bytes) applied to a note velocity -> 0..127 */
int      Sfx_VelocityCurve(const uint8_t *vtab, int vel);
/* 68K 0x3094: TL register of a key-on: level = (2 vcurve (255 - layer TL)) (2 vol) >> 16
 * + bias (clamped 0..255), TL = 255 - level */
int      Sfx_KeyOnTL(int vcurve, int layer_tl, int chan_vol, int bias);
/* 68K 0x311A: SCSP reg 0x10 (OCT << 11 | FNS) of key with base key, fine tune (s8, 1/256
 * semitone) and pitch offset (bend, 1/256 semitone) */
uint16_t Sfx_PitchReg(int key, int base, int fine, int offset);
double   Sfx_PitchRatio(uint16_t reg10);         /* 2^OCT (1 + FNS / 1024) */
float    Sfx_TLGain(int tl);                     /* SCSP TL: 0.4 / 0.8 / 1.5 / 3 / 6 / 12 / 24 / 48 dB bits */

/* ---- game side (takes the audio device lock) ---------------------------- */
/* Parses a .SND image and makes it the resident bank (registers the audio.c hooks
 * on first use). 1 = ok. */
int  Sfx_LoadBankData(const uint8_t *buf, size_t len, const char *name);
int  Sfx_HasBankLocked(void);
int  Sfx_CountLocked(int id);
int  Sfx_SlotIdLocked(int slot);
void Sfx_SetSeqVolumeLocked(int slot, int vol, int rate);
void Sfx_SetAllSeqVolumeLocked(int vol, int rate);
void Sfx_StopSlotLocked(int slot);

#endif
