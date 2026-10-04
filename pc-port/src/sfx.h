#ifndef SFX_H
#define SFX_H
/* SCSP sound banks (.SND) and a small replacement for the Saturn sound driver:
 * sequence player + PCM voices, mixed into audio.c's output (docs/sound.md).
 *
 * Saturn side (A.BIN):
 *  - Res_Request(1, kind 2, ..., "STAGEn.SND"): 0x06004C52 streams the file into
 *    sound RAM: 0x80-byte SBL area map (copied to 0x0601AB44, driver = A.BIN
 *    0x0600F318, 0x50CE bytes, SND init 0x060304B0), then blocks {u16 id, word-LZ}
 *    decompressed by 0x06004AA0 to 0x25A00000 + the map address of that id.
 *  - Snd_Play(id) 0x06007CDE: id -> {seq bank, song, priority} (tables 0x0600EF20 /
 *    0x0600EF50 / 0x0600F0D0), queued by 0x06007FA8 (17 x 10 B at 0x06025178) and
 *    started on one of 8 sequence slots (0x06025138) by 0x0600863E.
 *  - The driver plays SBL sequences (SEQ) whose notes key on voices of the tone banks
 *    (TON layers = SCSP slot register images + PCM in the same bank).
 *
 * PC: one bank (the last .SND loaded) is resident. Sfx_Play() starts the sequence
 * of the id on one of SFX_SLOTS sequence slots, notes play on SFX_VOICES voices
 * (linear interpolation, loops, SCSP EG rates, volume/pan/pitch bend).
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
SfxBank *Sfx_SetBank(SfxBank *b);       /* stops everything, returns the previous bank */
const SfxBank *Sfx_GetBank(void);
int  Sfx_Play(int id);                  /* 1 = started */
int  Sfx_PlaySong(int seq_bank, int song, int prio, int id, int music);
void Sfx_StopAll(void);
void Sfx_StopId(int id);
int  Sfx_Count(int id);                 /* slots playing id (0x06007D60 check) */
int  Sfx_ActiveSlots(void);
int  Sfx_ActiveVoices(void);
void Sfx_SetVolume(float v);            /* master, default 0.4 */
/* Adds `frames` 44.1 kHz stereo frames into acc (interleaved L/R). */
void Sfx_Render(int32_t *acc, int frames);

/* ---- game side (takes the audio device lock) ---------------------------- */
/* Parses a .SND image and makes it the resident bank (registers the audio.c hooks
 * on first use). 1 = ok. */
int  Sfx_LoadBankData(const uint8_t *buf, size_t len, const char *name);
int  Sfx_HasBankLocked(void);
int  Sfx_CountLocked(int id);

#endif
