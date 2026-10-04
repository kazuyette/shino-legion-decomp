/* SCSP sound banks + the SBL sound path (SH-2 queue/scheduler, 68K driver, SCSP slots),
 * see sfx.h and docs/sound.md (section 6 = the 68K driver as read from A.BIN 0x0600F318). */
#include "sfx.h"
#include "u8path.h"
#include "audio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

/* ======================================================================= bank */

/* Word-LZ block as streamed by A.BIN 0x06004AA0 (kind 1/2 reads): u32 header
 * 0x80000000 | (words - 1), then [u16 flags, MSB first] + 16 items (1 = literal
 * word, 0 = ref: dist = w >> 4 words back in a 4096-word ring, len = (w & 15) + 3).
 * The streaming decoder stops after the item that makes the count reach `words`
 * (a ref may overshoot; the Saturn writes the overshoot too). lz.c's Lz_Decompress
 * now also decodes header + 1 words (A.BIN 0x06004EE8).
 * Returns the bytes produced (malloc'd in *out), 0 on error. */
static size_t lz_block(const uint8_t *src, size_t len, size_t *consumed, uint8_t **out, int *wrefs)
{
    uint32_t total, cnt = 0;
    size_t p = 4;
    uint16_t *o;
    if (len < 4 || src[0] != 0x80) return 0;
    total = ((uint32_t)(be16(src) & 0x7FFF) << 16 | be16(src + 2)) + 1u;
    if (total > 0x80000u) return 0;                       /* > sound RAM */
    o = (uint16_t *)malloc(((size_t)total + 20) * 2);
    if (!o) return 0;
    while (cnt < total) {
        uint16_t f;
        int b;
        if (p + 2 > len) { free(o); return 0; }
        f = be16(src + p); p += 2;
        for (b = 15; b >= 0 && cnt < total; b--) {
            uint16_t w;
            if (p + 2 > len) { free(o); return 0; }
            w = be16(src + p); p += 2;
            if (f >> b & 1) o[cnt++] = w;
            else {
                uint32_t d = w >> 4, n = (uint32_t)(w & 15) + 3, k;
                for (k = 0; k < n; k++, cnt++) {
                    if (d == 0 || d > cnt) { o[cnt] = 0; (*wrefs)++; }   /* ring data of a previous load */
                    else o[cnt] = o[cnt - d];
                }
            }
        }
    }
    {   /* words -> big-endian bytes (the bank stays in Saturn byte order) */
        uint8_t *bytes = (uint8_t *)o;
        uint32_t i;
        for (i = 0; i < cnt; i++) { uint16_t w = o[i]; bytes[i * 2] = (uint8_t)(w >> 8); bytes[i * 2 + 1] = (uint8_t)w; }
    }
    *consumed = p;
    *out = (uint8_t *)o;
    return (size_t)cnt * 2;
}

void SfxBank_Free(SfxBank *b)
{
    int i;
    if (!b) return;
    for (i = 0; i < b->nblk; i++) free(b->blk[i]);
    free(b);
}

/* 0x06004C52: 0x80-byte area map (8-byte entries: id = type << 4 | bank, u24 sound
 * RAM address, flags (bit 7 set by the loader = data present), u24 size; 0xFF ends),
 * then blocks {u16 0x0T0B (type T, bank B), LZ data} until the end of the file. */
SfxBank *SfxBank_Parse(const uint8_t *buf, size_t len, const char *name)
{
    SfxBank *b;
    size_t p = 0x80;
    int i;
    if (!buf || len < 0x86) { printf("[sfx] %s: too short\n", name ? name : "?"); return NULL; }
    b = (SfxBank *)calloc(1, sizeof *b);
    if (!b) return NULL;
    snprintf(b->name, sizeof b->name, "%s", name ? name : "");
    for (i = 0; i < 16 && buf[i * 8] != 0xFF; i++) memcpy(b->map[i], buf + i * 8, 8);
    b->nmap = i;
    while (p + 6 <= len) {
        uint16_t w = be16(buf + p);
        int type = (w >> 8) & 15, bank = w & 15, k;
        size_t used = 0, n;
        uint8_t *data = NULL;
        SfxArea *a;
        n = lz_block(buf + p + 2, len - p - 2, &used, &data, &b->lz_window_refs);
        if (!n) { printf("[sfx] %s: bad LZ block at 0x%zx\n", b->name, p); SfxBank_Free(b); return NULL; }
        if (b->nblk >= 32) { free(data); SfxBank_Free(b); return NULL; }
        b->blk[b->nblk++] = data;
        p += 2 + used;
        a = type == 0 ? &b->tone[bank] : type == 1 ? &b->seq[bank] : NULL;
        if (!a) { b->ndsp++; continue; }            /* 2/3 = DSP program / work RAM (none on the disc) */
        a->data = data;
        a->size = (uint32_t)n;
        for (k = 0; k < b->nmap; k++)
            if (b->map[k][0] == (uint8_t)(type << 4 | bank)) {
                a->addr = (uint32_t)b->map[k][1] << 16 | (uint32_t)b->map[k][2] << 8 | b->map[k][3];
                a->alloc = (uint32_t)b->map[k][5] << 16 | (uint32_t)b->map[k][6] << 8 | b->map[k][7];
            }
    }
    if (p != len) printf("[sfx] %s: %zu trailing bytes\n", b->name, len - p);
    return b;
}

SfxBank *SfxBank_Load(const char *path)
{
    FILE *f = u8_fopen(path, "rb");
    long n;
    uint8_t *buf;
    SfxBank *b;
    const char *base = strrchr(path, '/');
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = (uint8_t *)malloc(n > 0 ? (size_t)n : 1);
    if (!buf || fread(buf, 1, (size_t)(n > 0 ? n : 0), f) != (size_t)n) { fclose(f); free(buf); return NULL; }
    fclose(f);
    b = SfxBank_Parse(buf, (size_t)n, base ? base + 1 : path);
    free(buf);
    return b;
}

/* SEQ bank: u16 song count, u32 song offsets (from the bank start). */
int SfxBank_SongCount(const SfxBank *b, int sb)
{
    if (!b || sb < 0 || sb >= SFX_AREAS || !b->seq[sb].data || b->seq[sb].size < 2) return 0;
    {
        int n = be16(b->seq[sb].data);
        if ((uint32_t)(2 + 4 * n) > b->seq[sb].size) n = (int)((b->seq[sb].size - 2) / 4);
        return n;
    }
}

/* TON bank: u16 offsets {mixer, velocity, PEG, PLFO}, then u16 voice offsets up to the
 * mixer table ((mixer - 8) / 2 voices). */
int SfxBank_VoiceCount(const SfxBank *b, int tb)
{
    const SfxArea *a;
    if (!b || tb < 0 || tb >= SFX_AREAS) return 0;
    a = &b->tone[tb];
    if (!a->data || a->size < 10) return 0;
    {
        int mix = be16(a->data);
        if (mix < 8 || (uint32_t)mix > a->size) return 0;
        return (mix - 8) / 2;
    }
}

/* ================================================================ id table */

/* {seq bank, song, priority} = words 0/1/2 of the 8-byte entries */
static const uint8_t k_tab0[6][3] = {          /* 0x0600EF20: ids 0x000-0x005 (flag 1) */
    {2, 0, 0}, {2, 1, 0}, {2, 2, 0}, {2, 3, 0}, {2, 4, 0}, {2, 5, 0},
};
static const uint8_t k_tab1[48][3] = {         /* 0x0600EF50: ids 0x100-0x12F */
    {0, 0,16}, {0, 1,16}, {0,23,16}, {0, 2,12}, {0, 3,16}, {0, 4,16}, {0, 5,16}, {0, 6, 4},
    {0, 7,16}, {0,11,16}, {0,12,16}, {0,19,16}, {0,20,16}, {0,13,16}, {0,15,16}, {0,16,16},
    {0,10,16}, {0,14,16}, {0, 8,16}, {0,27,16}, {0,23,16}, {0,28,16}, {1, 0, 8}, {0,29,16},
    {1, 1, 8}, {0,30,16}, {1, 2, 8}, {1, 3, 8}, {1, 4, 8}, {1, 7, 4}, {1, 8, 4}, {1, 9, 4},
    {1,10, 4}, {0,17,16}, {0,18,16}, {0,22,16}, {0,21,16}, {0,24,16}, {0,25,16}, {0,26,16},
    {0, 0,16}, {0, 0,16}, {0, 0,16}, {0, 0,16}, {0, 0,16}, {0, 0,16}, {0, 0,16}, {0, 0,16},
};
static const uint8_t k_tab2[56][3] = {         /* 0x0600F0D0: ids 0x200-0x237 (stage bank) */
    {3, 0,16}, {3, 1,16}, {3, 2,16}, {3, 3,16}, {3, 4,16}, {3, 5, 8}, {3, 6, 8}, {3, 7, 8},
    {3, 8, 8}, {3, 9,16}, {3,10,16}, {3,11,16}, {3,12,16}, {3,13,16}, {3,14,16}, {3,15,16},
    {3,16,16}, {3,17,16}, {3,18,16}, {3,19,16}, {3,20,16}, {3,21,16}, {3,22,16}, {3,23,16},
    {3,24,16}, {3,25,16}, {3,26,16}, {3,27,16}, {3,28,16}, {3,29,16}, {3,30,16}, {3,31,16},
    {3,32,16}, {3,33,16}, {3,34, 8}, {3,35, 8}, {3,36, 8}, {3,37, 8}, {3,38, 8}, {3,39, 8},
    {3,40, 8}, {3,41, 8}, {3,42, 8}, {3,43, 8}, {3,44,16}, {3,45,16}, {3,46,16}, {3,47,16},
    {3,48,16}, {3,49,16}, {3,50,16}, {3,51,16}, {3,52,16}, {3,53, 8}, {3,54, 8}, {3,55, 8},
};

/* 0x06007CDE: id < 0 rejected; < 0x100 table 0 (flag 1); 0x100.. table 1; 0x200.. table 2.
 * (The Saturn does not bound-check the index; ids past the tables are refused here.) */
int Sfx_Lookup(int id, SfxDef *out)
{
    const uint8_t *e = NULL;
    int music = 0;
    id = (int16_t)id;
    if (id < 0) return 0;
    if (id < 0x100) { if (id < 6) e = k_tab0[id]; music = 1; }
    else if (id < 0x200) { if (id - 0x100 < 48) e = k_tab1[id - 0x100]; }
    else if (id - 0x200 < 56) e = k_tab2[id - 0x200];
    if (!e) return 0;
    out->seq_bank = e[0]; out->song = e[1]; out->prio = e[2]; out->music = (uint8_t)music;
    return 1;
}

/* ============================================================ driver maths */

/* 68K 0x2FD2: velocity table entry = {c0, x1, y1, c1, x2, y2, c2, x3, y3, c3}; segment k
 * starts at (x_k, y_k) (x0 = y0 = 0) and is used while vel <= x_{k+1}. Curve byte: bits 2-0
 * type, bit 3 "x1.5", bits 7-4 shift s; d = vel - x_k:
 *   0/4: y   1: y + (d << s)   2: y + d   3: y + (d >> s)   5: y - (d >> s)   6: y - d
 *   7: y - (d << s)            (1/7 with bit 3: d * 12 << s >> 3 instead of d << s)
 * byte arithmetic; a result 0x80-0xBF clamps to 127, 0xC0-0xFF to 0.
 * Every tone bank of this game has 0A 7F7F08 7F7F08 7F7F08: curve = vel. */
int Sfx_VelocityCurve(const uint8_t *t, int vel)
{
    int x = 0, y = 0, c = t[0], k, d, s, r;
    vel &= 0xFF;
    for (k = 0; k < 3 && vel > t[1 + 3 * k]; k++) { x = t[1 + 3 * k]; y = t[2 + 3 * k]; c = t[3 + 3 * k]; }
    d = (vel - x) & 0xFF;
    s = c >> 4;
    switch (c & 7) {
    case 1:  r = (c & 8) ? (((d & 0x7F) * 12) << s >> 3) + y : ((d << s) & 0xFF) + y; break;
    case 2:  r = d + y; break;
    case 3:  r = (d >> s) + y; break;
    case 5:  r = y - (d >> s); break;
    case 6:  r = y - d; break;
    case 7:  r = y - ((c & 8) ? (((d & 0x7F) * 12) << s >> 3) : ((d << s) & 0xFF)); break;
    default: r = y; break;
    }
    r &= 0xFF;
    if (r & 0x80) r = (r & 0x40) ? 0 : 0x7F;
    return r;
}

/* 68K 0x3094 (key on) */
int Sfx_KeyOnTL(int vcurve, int layer_tl, int chan_vol, int bias)
{
    uint32_t l36 = (uint32_t)(2 * vcurve) * (uint32_t)(255 - (layer_tl & 0xFF));
    int level = (int)((l36 * (uint32_t)((2 * chan_vol) & 0xFF)) >> 16);
    level += (int8_t)bias;
    if (level < 0) level = 0;
    if (level > 255) level = 255;
    return 255 - level;
}

/* 68K 0x311A: x = offset + fine; i = ((x >> 8) + key + 0x60 - base) & 0xFF;
 * table 0x35D6[i] = OCT << 4 | semitone (OCT -8..7 for i < 0xC0: i = 0x60 -> OCT 0);
 * FNS = table 0x3696[(semitone << 7) | (x & 0xFF) >> 1] = floor(1024 (2^(n / 1536) - 1))
 * (verified against all 1536 entries of the driver). */
static int fns_of(int n) { return (int)floor(1024.0 * (pow(2.0, n / 1536.0) - 1.0) + 1e-9); }

uint16_t Sfx_PitchReg(int key, int base, int fine, int offset)
{
    uint16_t x = (uint16_t)(offset + (int8_t)fine);
    int i = ((x >> 8) + key + 0x60 - base) & 0xFF, tb, n;
    if (i < 0xC0) tb = ((i / 12 - 8) & 15) << 4 | i % 12;
    else tb = fns_of(i - 0xC0) & 0xFF;                  /* past the table: FNS table bytes */
    n = (tb & 15) << 7 | (x & 0xFF) >> 1;
    if (n > 0x5FF) n = 0x5FF;
    return (uint16_t)((tb >> 4) << 11 | fns_of(n));
}

double Sfx_PitchRatio(uint16_t r)
{
    int oct = (r >> 11) & 15;
    if (oct & 8) oct -= 16;
    return ldexp(1.0 + (r & 0x3FF) / 1024.0, oct);
}

float Sfx_TLGain(int tl)
{
    static const float w[8] = { 0.4f, 0.8f, 1.5f, 3.0f, 6.0f, 12.0f, 24.0f, 48.0f };
    float db = 0.0f;
    int b;
    for (b = 0; b < 8; b++) if (tl >> b & 1) db += w[b];
    return powf(10.0f, -db / 20.0f);
}

/* ================================================================== engine */

/* SCSP EG times in ms for the effective rate 0..63 (SCSP manual / MAME ARTimes, DRTimes;
 * decay/release = time for the full 96 dB range). */
static const float k_ar_ms[64] = {
    100000, 100000, 8100, 6900, 6000, 4800, 4000, 3400, 3000, 2400, 2000, 1700, 1500,
    1200, 1000, 860, 760, 600, 500, 430, 380, 300, 250, 220, 190, 150, 130, 110, 95,
    76, 63, 55, 47, 38, 31, 27, 24, 19, 15, 13, 12, 9.4f, 7.9f, 6.8f, 6.0f, 4.7f, 3.8f, 3.4f, 3.0f, 2.4f,
    2.0f, 1.8f, 1.6f, 1.3f, 1.1f, 0.93f, 0.85f, 0.65f, 0.53f, 0.44f, 0.40f, 0.35f, 0, 0 };
static const float k_dr_ms[64] = {
    100000, 100000, 118200, 101300, 88600, 70900, 59100, 50700, 44300, 35500, 29600, 25300, 22200, 17700,
    14800, 12700, 11100, 8900, 7400, 6300, 5500, 4400, 3700, 3200, 2800, 2200, 1800, 1600, 1400, 1100,
    920, 790, 690, 550, 460, 390, 340, 270, 230, 200, 170, 140, 110, 98, 85, 68, 57, 49, 43, 34,
    28, 25, 22, 18, 14, 12, 11, 8.5f, 7.1f, 6.1f, 5.4f, 4.3f, 3.6f, 3.1f };
/* LFO (reg 0x12): LFOF -> Hz, PLFOS -> cents, ALFOS -> dB (SCSP manual) */
static const float k_lfo_hz[32] = {
    0.17f, 0.19f, 0.23f, 0.27f, 0.34f, 0.39f, 0.45f, 0.55f, 0.68f, 0.78f, 0.92f, 1.10f, 1.39f, 1.60f, 1.87f, 2.27f,
    2.87f, 3.31f, 3.92f, 4.79f, 6.15f, 7.18f, 8.60f, 10.8f, 14.4f, 17.2f, 21.5f, 28.7f, 43.1f, 57.4f, 86.1f, 172.3f };
static const float k_plfo_cents[8] = { 0, 7.0f, 13.5f, 27.0f, 55.0f, 112.0f, 230.0f, 494.0f };
static const float k_alfo_db[8] = { 0, 0.4f, 0.8f, 1.5f, 3.0f, 6.0f, 12.0f, 24.0f };

enum { EG_OFF = 0, EG_ATTACK, EG_DECAY1, EG_DECAY2, EG_RELEASE };

typedef struct {                /* SCSP slot + its 68K slot work (0x7000 + 0x40 n) */
    int      eg;                /* EG_* (EG_OFF = silent) */
    int      keyed;             /* KYONB (key on, not released) */
    int      seq, ch, key;      /* owner: sequence slot, MIDI channel 0-31, key (kept after key off) */
    const uint8_t *layer;       /* layer image (+$8 of the slot work), NULL = never used */
    uint32_t on_serial, off_serial;
    const uint8_t *pcm;         /* SA */
    int      pcm8;
    uint32_t lsa, lea;
    int      lpctl, dir;
    double   pos, inc;
    int      base, fine, krs;
    uint16_t pitch;             /* reg 0x10 */
    int      disdl, pan, pan_saved;     /* reg 0x16 DISDL / DIPAN, DIPAN at key on (+4 of the work) */
    int      tl, velpath;       /* reg 0x0D, slot work +5 bit 0 */
    uint32_t lvl36;             /* slot work +$36: 2 vcurve (255 - layer TL) */
    uint16_t lfo;               /* reg 0x12 */
    double   lfo_phase, lfo_inc;
    uint32_t noise;
    float    gl, gr;            /* TL x DISDL x DIPAN x master */
    float    att_amp;           /* attack 0..1 */
    float    att_db;            /* EG attenuation 0..96 */
    float    ar_step, d1_step, d2_step, rr_step, dl_db;
} Voice;

typedef struct {                /* 68K channel struct ($6000 + seq 0x200 + ch 0x10) */
    uint8_t  prog;              /* +0 */
    uint8_t  panf;              /* +1: b7 pan set by CC 10, b6 locked by the host, b4-0 DIPAN */
    uint8_t  vol;               /* +2: CC 7 / CC 11 */
    int8_t   bias;              /* +E: voice header byte 3 of the last note */
    int      tone;              /* +4: tone bank (-1 none) */
    int16_t  bend;              /* +8: 1/256 semitone */
} Chan;

#define NCH   32
#define NOTES 32
#define QMAX  17

typedef struct {                /* 68K sequence slot ($7F00 + 0x60 n) */
    int      state;             /* +2: 0 idle, 1 play, 2 play (fade), 3 pause, 4 pause (fade) */
    int      flags;             /* +0: b7 running, b6 paused, b4 fading, b0 end of track */
    const uint8_t *song;
    uint32_t len, data, pc, ret, loop_pc, tempo_next, tempo_loop;
    int      ref_left;
    uint32_t tempo_timer;       /* +$1C (8.8 ticks, unsigned as on the 68K) */
    int32_t  ev_timer;          /* +$18 (8.8 ticks) */
    int32_t  step;              /* +$14: ticks per timer-B tick (8.8) */
    uint16_t res;
    int      pend_st, pend_ch, pend_d1, pend_d2;   /* fetched event ($2C-$2F) */
    uint32_t pend_gate;         /* +$28 */
    uint8_t  att;               /* +$22: 127 - volume */
    uint16_t fade_cur, fade_step;   /* +$24 (8.8), +$26 */
    int      fade_div;          /* +$35 */
    int      notes;             /* +$20 */
    Chan     ch[NCH];
} Seq;

typedef struct {                /* note table entry (0x7C00 + 12 n) */
    int      active, seq, ch, key, vel;
    int32_t  gate;              /* 8.8 */
} Note;

typedef struct {                /* request (0x06025178 + 12 n) */
    int      kind;              /* +0: 0 stop, 1 volume 127, 2 volume, 3 start, -1 done */
    int      id, music;         /* +4, +6 */
    int      bank, song, prio;  /* +8, +9, +10 */
    int      slot, vol, rate;   /* +6, +7, +8 for kinds 0-2 */
    uint32_t order;
} Req;

typedef struct { int id, prio; } SlotEnt;   /* 0x06025138: +0 id (-1 free), +2 priority */

static SfxBank *s_bank;
static Voice    s_voice[SFX_VOICES];
static Seq      s_seq[SFX_SLOTS];
static Note     s_note[NOTES];
static Req      s_req[QMAX];
static int      s_nreq;
static SlotEnt  s_slot[SFX_SLOTS];
static uint32_t s_serial, s_order;
static int      s_mono;
static float    s_master = 0.9f;        /* 1.0 = one full-level slot at full scale; SE 0x122 peaks at 1.03 */
static int      s_tick_left = SFX_TICK_SAMPLES, s_main_left = SFX_MAIN_SAMPLES, s_frame_left = 0;
static int      s_main_group;
static float    s_dbtab[1025];          /* attenuation (96 dB / 1024 steps) -> gain */
static int      s_dbtab_ok;

const SfxBank *Sfx_GetBank(void) { return s_bank; }
void Sfx_SetVolume(float v) { s_master = v; }

static float sdl_gain(int level)        /* SCSP DISDL 0..7: 0 = off, -6 dB/step */
{
    if (level <= 0) return 0.0f;
    return powf(2.0f, (float)(level - 7));
}

static float per_sample_db(int rate, const float *tab)
{
    float ms;
    if (rate < 2) return 0.0f;
    ms = tab[rate > 63 ? 63 : rate];
    if (ms <= 0.0f) return 96.0f;
    return 96.0f / (ms * (SFX_RATE / 1000.0f));
}

static void voice_gains(Voice *v)
{
    float g = sdl_gain(v->disdl) * Sfx_TLGain(v->tl) * s_master, l = g, r = g;
    int pan = s_mono ? 0 : v->pan;                  /* driver mono flag: DIPAN 0 everywhere */
    if (pan & 0x0F) {                               /* DIPAN: 3 dB steps, 0xF = off */
        float a = (pan & 0x0F) == 0x0F ? 0.0f : powf(10.0f, -3.0f * (pan & 0x0F) / 20.0f);
        if (pan & 0x10) r *= a; else l *= a;        /* < 0x10: left attenuated (sound right) */
    }
    v->gl = l;
    v->gr = r;
}

/* EG rates with key rate scaling (KRS 0xF = off): rate = 2 R + OCT + 2 KRS + FNS bit 9 */
static int eg_rate(const Voice *v, int r)
{
    int rate, oct = (v->pitch >> 11) & 15;
    if (!r) return 0;
    if (oct & 8) oct -= 16;
    rate = 2 * r + (v->krs != 15 ? oct + 2 * v->krs + ((v->pitch >> 9) & 1) : 0);
    return rate < 0 ? 0 : rate > 63 ? 63 : rate;
}

static void voice_pitch(Voice *v, int offset)
{
    v->pitch = Sfx_PitchReg(v->key, v->base, v->fine, offset);
    v->inc = Sfx_PitchRatio(v->pitch);
}

static void voice_eg_rates(Voice *v)
{
    const uint8_t *l = v->layer;
    uint16_t r08 = be16(l + 0x0A), r0a = be16(l + 0x0C);
    int ar = eg_rate(v, r08 & 31);
    float ms = ar ? k_ar_ms[ar] : 100000.0f;
    v->ar_step = ms <= 0.0f ? 1.0f : 1.0f / (ms * (SFX_RATE / 1000.0f));
    v->d1_step = per_sample_db(eg_rate(v, r08 >> 6 & 31), k_dr_ms);
    v->d2_step = per_sample_db(eg_rate(v, r08 >> 11 & 31), k_dr_ms);
    v->rr_step = per_sample_db(eg_rate(v, r0a & 31), k_dr_ms);
    v->dl_db = (r0a >> 5 & 31) * 3.0f;
}

static void voice_keyoff(Voice *v)
{
    if (!v->keyed) return;
    v->keyed = 0;
    v->off_serial = ++s_serial;
    if (v->eg != EG_OFF) {
        v->eg = EG_RELEASE;
        voice_eg_rates(v);
    }
}

/* 68K ring of free slots: the slot keyed off longest ago, else (none free) the oldest keyed */
static Voice *voice_alloc(void)
{
    Voice *best = NULL;
    int i;
    for (i = 0; i < SFX_VOICES; i++)
        if (!s_voice[i].keyed && (!best || s_voice[i].off_serial < best->off_serial)) best = &s_voice[i];
    if (best) return best;
    for (i = 0; i < SFX_VOICES; i++)
        if (!best || s_voice[i].on_serial < best->on_serial) best = &s_voice[i];
    return best;
}

/* 68K 0x2EFE: writes the layer's slot registers and keys the slot on. */
static void layer_on(Voice *v, int si, int c, int key, int vel, const SfxArea *a, const uint8_t *l)
{
    Seq *s = &s_seq[si];
    Chan *ch = &s->ch[c];
    uint16_t w0 = be16(l + 2);
    uint32_t sa = (uint32_t)(w0 & 15) << 16 | be16(l + 4), lsa = be16(l + 6), lea = be16(l + 8), maxs;
    int d6;
    v->keyed = 0;
    v->eg = EG_OFF;
    v->seq = si; v->ch = c; v->key = key; v->layer = l;
    if (sa >= a->size) return;
    v->pcm8 = w0 >> 4 & 1;
    v->lpctl = w0 >> 5 & 3;
    maxs = (a->size - sa) / (v->pcm8 ? 1u : 2u);
    if (lea > maxs) lea = maxs;
    if (lea < 2) return;
    if (lsa >= lea) v->lpctl = 0;
    v->pcm = a->data + sa; v->lsa = lsa; v->lea = lea; v->dir = 1; v->pos = 0.0;
    v->base = l[0x19];
    v->fine = l[0x1A];
    v->krs = be16(l + 0x0C) >> 10 & 15;
    v->lfo = be16(l + 0x14);
    if (l[0x0E] & 0x80) v->lfo &= 0xFF18;           /* reg 0x0C bit 7: PLFOS / ALFOS cleared */
    v->lfo &= 0x7FFF;                               /* LFORE cleared at key on (0x310A) */
    v->lfo_phase = 0.0;
    v->lfo_inc = k_lfo_hz[v->lfo >> 10 & 31] / SFX_RATE;
    v->noise = 0x12345u + (uint32_t)(v - s_voice);
    /* reg 0x16: DISDL / DIPAN of the layer, the channel pan once a CC 10 was received */
    d6 = l[0x18];
    if (s_mono) d6 &= 0xE0;
    else if (ch->panf & 0xC0) d6 = (d6 & 0xE0) | (ch->panf & 0x1F);
    v->disdl = d6 >> 5;
    v->pan = v->pan_saved = d6 & 0x1F;
    /* TL: velocity path when the layer outputs (+2 bit 5, IMXL or DISDL), else the layer TL */
    v->velpath = (l[2] & 0x20) || (l[0x17] & 7) || (l[0x18] & 0xE0);
    if (v->velpath) {
        const uint8_t *tb = s_bank->tone[ch->tone].data;
        uint32_t vt = be16(tb + 2) + 10u * l[0x1D];
        int vc = vt + 10 <= s_bank->tone[ch->tone].size ? Sfx_VelocityCurve(tb + vt, vel) : vel & 0x7F;
        v->lvl36 = (uint32_t)(2 * vc) * (uint32_t)(255 - l[0x0F]);
        v->tl = Sfx_KeyOnTL(vc, l[0x0F], ch->vol, ch->bias);
    } else v->tl = l[0x0F];
    /* (layer +2 bits 7/6 = the driver's software PEG / PLFO: never set in this game) */
    voice_pitch(v, 0);                              /* bend: next main-loop refresh */
    voice_gains(v);
    voice_eg_rates(v);
    v->att_amp = 0.0f;
    v->att_db = 0.0f;
    v->eg = EG_ATTACK;
    v->keyed = 1;
    v->on_serial = ++s_serial;
}

/* ------------------------------------------------------- 68K MIDI processing */

static void midi_note_off(int si, int c, int key)  /* 0x3252 (poly) */
{
    int i;
    for (i = 0; i < SFX_VOICES; i++) {
        Voice *v = &s_voice[i];
        if (v->keyed && v->seq == si && v->ch == c && v->key == key) voice_keyoff(v);
    }
}

static void midi_note_on(int si, int c, int key, int vel)     /* 0x2C28 */
{
    Seq *s = &s_seq[si];
    Chan *ch = &s->ch[c];
    const SfxArea *a;
    const uint8_t *vh, *lay[32];
    uint32_t vo;
    int nl, n = 0, i, k = 0;
    if (!vel) { midi_note_off(si, c, key); return; }
    if (ch->tone < 0 || !s_bank) return;            /* no bank selected */
    a = &s_bank->tone[ch->tone];
    if (!a->data || ch->prog >= SfxBank_VoiceCount(s_bank, ch->tone)) return;
    vo = be16(a->data + 8 + 2 * ch->prog);
    if (vo + 4 > a->size) return;
    vh = a->data + vo;
    if (vh[2] & 0x80) return;
    nl = vh[2] + 1;
    for (i = 0; i < nl && n < 32; i++) {
        const uint8_t *l = vh + 4 + 32 * i;
        if (vo + 4 + 32u * (uint32_t)(i + 1) > a->size) break;
        if (key >= l[0] && key <= l[1]) lay[n++] = l;
    }
    if (!n) return;
    ch->bias = (int8_t)vh[3];
    /* (vh[0] bits 6-4: mono / legato voice modes, 0 in every voice of this game) */
    for (i = 0; i < SFX_VOICES && k < n; i++) {     /* same key already sounding: reuse its slot */
        Voice *v = &s_voice[i];
        if (v->keyed && v->seq == si && v->ch == c && v->key == key) layer_on(v, si, c, key, vel, a, lay[k++]);
    }
    for (; k < n; k++) layer_on(voice_alloc(), si, c, key, vel, a, lay[k]);
}

static void midi_program(int si, int c, int prog)  /* 0x159C */
{
    Chan *ch = &s_seq[si].ch[c];
    if (ch->tone < 0 || prog >= SfxBank_VoiceCount(s_bank, ch->tone)) return;
    ch->prog = (uint8_t)prog;
    ch->bend = 0;
    if (!(ch->panf & 0x40)) ch->panf &= 0x7F;
}

static void midi_cc(int si, int c, int ctrl, int val)
{
    Seq *s = &s_seq[si];
    Chan *ch = &s->ch[c];
    int i;
    switch (ctrl) {
    case 0x07: case 0x0B:                           /* 0x28C2: volume / expression */
        ch->vol = (uint8_t)(val & 0x7F);
        for (i = 0; i < SFX_VOICES; i++) {
            Voice *v = &s_voice[i];
            if (v->layer && v->seq == si && v->ch == c && v->velpath) {
                v->tl = 255 - (int)((v->lvl36 * (uint32_t)((2 * ch->vol) & 0xFF)) >> 16);
                voice_gains(v);
            }
        }
        break;
    case 0x0A: {                                    /* 0x291A: pan -> DIPAN */
        int d = (val & 0x7F) >> 2;
        d = d >= 16 ? d - 16 : 0x10 | (15 - d);
        ch->panf |= 0x80;
        if (ch->panf & 0x40) { if (!s_mono) break; d = 0; }
        else if (s_mono) d = 0;
        else ch->panf = (uint8_t)(0x80 | d);
        /* the driver compares the slot work's sequence base (+6) with the channel struct
         * offset (sequence base + ch x 16): only channel 0's sounding notes follow a pan
         * change, the other channels' new pan applies from their next note */
        if (c == 0)
            for (i = 0; i < SFX_VOICES; i++) {
                Voice *v = &s_voice[i];
                if (v->layer && v->seq == si && v->ch == c) { v->pan = d; voice_gains(v); }
            }
        break;
    }
    case 0x20: {                                    /* 0x157C: tone bank select */
        int b = val & 15;
        if (!s_bank || !s_bank->tone[b].data) break;
        ch->tone = b;
        ch->vol = 127;
        midi_program(si, c, 0);
        break;
    }
    default:                                        /* (0x40 sustain, 0x5B effect: unused) */
        break;
    }
}

static int asr(int x, int n) { return x >= 0 ? x >> n : -((-x + (1 << n) - 1) >> n); }   /* 68K asr */

/* 0x2B8A: value 0..127 (0x40 centre) -> b = value << 7 - 0x2000, scaled to 1/256 semitone
 * by the voice's bend range (header byte 0, low nibble) with the driver's shift chains
 * (range r: b x r / 32, truncated as on the 68K). */
static void midi_bend(int si, int c, int value)
{
    Chan *ch = &s_seq[si].ch[c];
    int b = ((value & 0x7F) << 7) - 0x2000, range = 0, x;
    if (ch->tone >= 0 && ch->prog < SfxBank_VoiceCount(s_bank, ch->tone)) {
        const SfxArea *a = &s_bank->tone[ch->tone];
        uint32_t vo = be16(a->data + 8 + 2 * ch->prog);
        if (vo < a->size) range = a->data[vo] & 15;
    }
    switch (range) {
    case 0:  x = 0; break;
    case 1:  x = asr(b, 5); break;
    case 2:  x = asr(b, 4); break;
    case 3:  x = asr(b, 4); x += asr(x, 1); break;
    case 4:  x = asr(b, 3); break;
    case 5:  x = asr(b, 3); x += asr(x, 2); break;
    case 6:  x = asr(b, 3); x += asr(x, 1); break;
    case 7:  x = asr(b, 3); { int y = asr(x, 1); x += y + asr(y, 1); } break;
    case 8:  x = asr(b, 2); break;
    case 9:  x = asr(b, 2); x += asr(x, 3); break;
    case 10: x = asr(b, 2); x += asr(x, 2); break;
    case 11: x = asr(b, 2); { int y = asr(x, 2); x += y + asr(y, 1); } break;
    case 12: x = asr(b, 2); x += asr(x, 1); break;
    case 13: x = asr(b, 1); x += asr(x, 1); break;      /* (as coded: 24 semitones) */
    default: x = b * range / 32; break;                  /* (jumps into code on the 68K) */
    }
    ch->bend = (int16_t)x;
}

/* 0x4B6E: a sequencer message; note-on velocity minus the sequence attenuation. */
static void midi_msg(int si, int st, int c, int d1, int d2)
{
    Seq *s = &s_seq[si];
    switch (st) {
    case 0x80: midi_note_off(si, c, d1); break;
    case 0x90: {
        int att = (s->flags & 0x10) ? s->fade_cur >> 8 : s->att;
        d2 -= att;
        if (d2 < 0) d2 = 0;
        midi_note_on(si, c, d1, d2);
        break;
    }
    case 0xB0: midi_cc(si, c, d1, d2); break;
    case 0xC0: midi_program(si, c, d1); break;
    case 0xE0: midi_bend(si, c, d2); break;
    default: break;                                 /* A0 / D0: pressure, unsupported */
    }
}

/* ---------------------------------------------------------- 68K sequencer */

static void seq_status_stop(Seq *s) { s->flags = 0; s->state = 0; }

/* 0x4A44: note off for the sequence's note table entries (kill = also free them) */
static void seq_notes_off(int si)
{
    int i;
    for (i = 0; i < NOTES; i++)
        if (s_note[i].active && s_note[i].seq == si) {
            s_note[i].active = 0;
            midi_msg(si, 0x80, s_note[i].ch, s_note[i].key, s_note[i].vel);
        }
}

/* 0x50C2 / 0x4602: next tempo entry; 0 = none */
static int seq_tempo(Seq *s, int first)
{
    const uint8_t *t;
    uint32_t us, q;
    if (!first && s->tempo_next >= s->data) {       /* end of the tempo list */
        if (!s->tempo_loop) return 0;
        s->tempo_next = s->tempo_loop;
    }
    if (s->tempo_next + 8 > s->len) return 0;
    t = s->song + s->tempo_next;
    s->tempo_next += 8;
    us = be32(t + 4);
    if (us < 200000u || us > 1500000u) return -1;   /* driver error 0x83 */
    q = us / s->res;
    s->tempo_timer += be32(t) << 8;
    s->step = (int32_t)(512000u / q);               /* 0x7D000: 2000 us x 256 per tick */
    return 1;
}

/* 0x4CDA: fetches one event. 0 = event in pend_*, step in *step; 1 = end; -1 = error. */
static int seq_fetch(Seq *s, uint32_t *step)
{
    static const uint16_t ext[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0x200, 0x800, 0x1000, 0x2000, 0x100, 0x200, 0x800, 0x1000 };
    uint32_t p = s->pc, gate = 0, guard = 0;
    *step = 0;
    for (;;) {
        const uint8_t *e = s->song + p;
        uint8_t st;
        if (p >= s->len || ++guard > 4096) return -1;
        st = e[0];
        if (st < 0x80) {                            /* note: ch = bits 4-0, key, vel, gate, step */
            if (p + 5 > s->len) return -1;
            s->pend_st = 0x90; s->pend_ch = st & 0x1F; s->pend_d1 = e[1]; s->pend_d2 = e[2];
            gate += e[3] + ((st & 0x40) ? 0x100u : 0u);
            *step += e[4] + ((st & 0x20) ? 0x100u : 0u);
            p += 5;
        } else if (st >= 0xA0) {                    /* A0-BF 3 bytes, C0-FF 2 bytes; data bit 7 = ch + 16 */
            int n = st < 0xC0 ? 3 : 2;
            if (p + 1u + (uint32_t)n > s->len) return -1;
            s->pend_st = st & 0xF0;                 /* F0-FF: type 7, unsupported by the driver */
            s->pend_ch = (st & 15) | ((e[1] & 0x80) ? 0x10 : 0);
            if (st < 0xC0) { s->pend_d1 = e[1] & 0x7F; s->pend_d2 = e[2]; }
            else if (st < 0xE0) { s->pend_d1 = e[1] & 0x7F; s->pend_d2 = 0; }
            else { s->pend_d1 = 0; s->pend_d2 = e[1] & 0x7F; }
            *step += e[n];
            p += 1u + (uint32_t)n;
        } else if (st == 0x81) {                    /* reference: n events at data + hhll */
            if (p + 4 > s->len) return -1;
            s->ret = p + 4;
            s->ref_left = e[3];
            p = s->data + be16(e + 1);
            continue;
        } else if (st == 0x82) {                    /* loop point: step adds to the next event */
            if (p + 2 > s->len) return -1;
            *step += e[1];
            p += 2;
            if (s->loop_pc) p = s->loop_pc; else s->loop_pc = p;
            continue;
        } else if (st == 0x83) { s->pc = p; return 1; }
        else if (st >= 0x88 && st <= 0x8B) { gate += ext[st - 0x80]; p++; continue; }
        else if (st >= 0x8C && st <= 0x8F) { *step += ext[st - 0x80]; p++; continue; }
        else return -1;
        /* a counted event: references return after n of them */
        if (s->ref_left && --s->ref_left == 0) p = s->ret;
        s->pc = p;
        s->pend_gate = gate;
        return 0;
    }
}

static void seq_emit(int si)                       /* 0x46FA: the pending event */
{
    Seq *s = &s_seq[si];
    midi_msg(si, s->pend_st, s->pend_ch, s->pend_d1, s->pend_d2);
    if (s->pend_st == 0x90) {
        int i;
        for (i = 0; i < NOTES; i++)                 /* 0x4B22 (table full: note never released) */
            if (!s_note[i].active) {
                Note *n = &s_note[i];
                n->active = 1; n->seq = si; n->ch = s->pend_ch; n->key = s->pend_d1; n->vel = s->pend_d2;
                n->gate = (int32_t)(s->pend_gate << 8) + s->ev_timer;
                break;
            }
        s->notes++;
    }
}

static void seq_tick(int si)                       /* 0x45B2 */
{
    Seq *s = &s_seq[si];
    uint32_t t;
    int r;
    if (!(s->flags & 0x80) || (s->flags & 0x40)) return;
    t = s->tempo_timer;
    s->tempo_timer -= (uint32_t)s->step;
    if (t < (uint32_t)s->step && seq_tempo(s, 0) < 0) { seq_notes_off(si); seq_status_stop(s); return; }
    if (s->flags & 0x10) {                          /* fade, every 50 ticks */
        if (--s->fade_div < 0) {
            uint16_t tgt = (uint16_t)(s->att << 8);
            int done = 0;
            s->fade_div = 49;
            if (s->fade_cur >= tgt) {
                if (s->fade_cur < s->fade_step || (uint16_t)(s->fade_cur - s->fade_step) <= tgt) done = 1;
                s->fade_cur -= s->fade_step;
            } else {
                if ((uint32_t)s->fade_cur + s->fade_step > 0xFFFF || (uint16_t)(s->fade_cur + s->fade_step) >= tgt) {
                    if (tgt == 0x7F00) {            /* faded out: the sequence stops */
                        seq_notes_off(si);
                        seq_status_stop(s);
                        return;
                    }
                    done = 1;
                }
                s->fade_cur += s->fade_step;
            }
            if (done) { s->flags &= ~0x10; s->state = 1; }
        }
    }
    if (s->flags & 1) return;
    s->ev_timer -= s->step;
    if (s->ev_timer > 0) return;
    seq_emit(si);
    for (;;) {
        uint32_t step;
        r = seq_fetch(s, &step);
        if (r < 0) { seq_notes_off(si); seq_status_stop(s); return; }
        if (r == 1) {
            s->flags |= 1;
            if (!s->notes) seq_status_stop(s);
            return;
        }
        s->ev_timer += (int32_t)(step << 8);
        if (s->ev_timer > 0) return;
        seq_emit(si);
    }
}

static void notes_tick(void)                       /* 0x4808 x 32 */
{
    int i;
    for (i = 0; i < NOTES; i++) {
        Note *n = &s_note[i];
        Seq *s;
        if (!n->active) continue;
        s = &s_seq[n->seq];
        if (s->flags & 0x40) continue;
        n->gate -= s->step;
        if (n->gate > 0) continue;
        midi_msg(n->seq, 0x80, n->ch, n->key, n->vel);
        n->active = 0;
        if (--s->notes == 0 && (s->flags & 1)) seq_status_stop(s);
    }
}

static void driver_tick(void)                      /* timer B interrupt 0x13A0 -> 0x4136 */
{
    int i;
    notes_tick();
    for (i = 0; i < SFX_SLOTS; i++) seq_tick(i);
}

static void driver_main(void)                      /* timer A: 0x1134 pitch refresh, 8 slots */
{
    int i;
    for (i = s_main_group * 8; i < s_main_group * 8 + 8; i++) {
        Voice *v = &s_voice[i];
        if (v->layer && v->eg != EG_OFF) {
            voice_pitch(v, s_seq[v->seq].ch[v->ch].bend);
            v->lfo_inc = k_lfo_hz[v->lfo >> 10 & 31] / SFX_RATE;
        }
    }
    s_main_group = (s_main_group + 1) & 3;
}

/* 0x42A2 (0x4288 stops a running one first): SND_StartSeq */
static int drv_start(int si, int seq_bank, int song)
{
    Seq *s = &s_seq[si];
    const SfxArea *a;
    const uint8_t *sp;
    uint32_t off, len, step;
    int r;
    if (!s_bank || song < 0 || song >= SfxBank_SongCount(s_bank, seq_bank)) return 0;
    a = &s_bank->seq[seq_bank];
    off = be32(a->data + 2 + 4 * song);
    if (off >= a->size || a->size - off < 16) return 0;
    sp = a->data + off;
    len = a->size - off;
    if (s->state) { seq_notes_off(si); s->flags = 0; }
    s->notes = 0;
    s->song = sp; s->len = len;
    s->res = be16(sp);
    s->data = be16(sp + 4);
    s->tempo_loop = be16(sp + 6) ? be16(sp + 6) : 0;
    s->tempo_next = 8;
    s->loop_pc = 0; s->ref_left = 0;
    s->pc = s->data;
    s->tempo_timer = 0;
    s->state = (s->flags & 0x10) ? 2 : 1;
    if (s->res < 0x18 || s->res > 0x3C0 || s->data >= len || seq_tempo(s, 1) <= 0) { seq_status_stop(s); return 0; }
    r = seq_fetch(s, &step);
    if (r != 0) { seq_status_stop(s); return 0; }
    s->ev_timer = (int32_t)(step << 8);
    s->flags = (s->flags & 0x10) | 0x80;
    return 1;
}

static void drv_stop(int si)                       /* 0x4450: SND_StopSeq */
{
    Seq *s = &s_seq[si];
    s->flags = 0;
    seq_notes_off(si);
    s->state = 0;
}

static uint16_t fade_calc(int from, int to, int rate)  /* 0x4C36 */
{
    int d = from > to ? from - to : to - from;
    rate &= 0xFF;
    return (uint16_t)(((d << 8) & 0x7FFF) / (rate ? rate : 1));
}

static void drv_seq_volume(int si, int vol, int rate)  /* 0x409C: SND_SetSeqVl */
{
    Seq *s = &s_seq[si];
    int att = 127 - (vol & 0x7F), old = s->att;
    switch (s->state) {
    case 0:
        if (!rate) { s->att = (uint8_t)att; break; }
        if (att == old) break;
        s->fade_cur = 0x7F00;                       /* fade-in of the next start */
        s->att = (uint8_t)att; s->fade_step = fade_calc(old, att, rate); s->flags |= 0x10;
        break;
    case 1:
        if (!rate) { s->att = (uint8_t)att; break; }
        s->state = 2;
        if (att == old) break;
        s->fade_cur = (uint16_t)(old << 8);
        s->att = (uint8_t)att; s->fade_step = fade_calc(old, att, rate); s->flags |= 0x10;
        break;
    case 2:
        if (!rate) { s->flags &= ~0x10; s->state = 1; s->att = (uint8_t)att; break; }
        if (att == old) break;
        s->att = (uint8_t)att; s->fade_step = fade_calc(old, att, rate); s->flags |= 0x10;
        break;
    case 3:
        if (!rate) s->att = (uint8_t)att;
        break;
    case 4:
        if (!rate) { s->flags &= ~0x10; s->state = 3; s->att = (uint8_t)att; }
        break;
    }
}

/* ------------------------------------------------------ SH-2 queue + scheduler */

static int req_add(const Req *r)                   /* append, or replace (queue full) the pending
                                                      non-music start with the largest priority value
                                                      above r's (0x06007FA8 / 0x06008154 / 0x060084BE) */
{
    int i, best = -1, bp = r->kind == 3 ? r->prio : -1;
    if (s_nreq < QMAX) { s_req[s_nreq] = *r; s_req[s_nreq].order = ++s_order; s_nreq++; return 1; }
    for (i = 0; i < QMAX; i++)
        if (s_req[i].kind == 3 && !s_req[i].music && s_req[i].prio > bp) { best = i; bp = s_req[i].prio; }
    if (best < 0) return 0;
    s_req[best] = *r;
    s_req[best].order = ++s_order;
    return 1;
}

int Sfx_PlaySong(int seq_bank, int song, int prio, int id, int music)
{
    Req r;
    int i;
    for (i = 0; i < s_nreq; i++) if (s_req[i].kind == 3 && s_req[i].id == (int16_t)id) return 1;
    memset(&r, 0, sizeof r);
    r.kind = 3; r.id = (int16_t)id; r.music = music != 0;
    r.bank = seq_bank & 0xFF; r.song = song & 0xFF; r.prio = prio & 0xFF;
    return req_add(&r);
}

int Sfx_Play(int id)
{
    SfxDef d;
    if (!Sfx_Lookup(id, &d)) return 0;
    return Sfx_PlaySong(d.seq_bank, d.song, d.prio, (int16_t)id, d.music);
}

int Sfx_StopSlot(int slot)                         /* 0x060084BE */
{
    Req r;
    int i;
    if (slot < 0 || slot >= SFX_SLOTS) return 0;
    for (i = 0; i < s_nreq; i++) {
        if (s_req[i].kind == 0 && s_req[i].slot == slot) return 1;
        if (s_req[i].kind == -1) { memset(&s_req[i], 0, sizeof s_req[i]); s_req[i].slot = slot; s_req[i].order = ++s_order; return 1; }
    }
    memset(&r, 0, sizeof r);
    r.kind = 0; r.slot = slot;
    return req_add(&r);
}

int Sfx_SetSeqVolume(int slot, int vol, int rate)  /* 0x06008154 */
{
    Req r;
    int i;
    if (slot < 0 || slot >= SFX_SLOTS) return 0;
    vol &= 0xFF; rate &= 0xFF;
    if (vol == 127 && rate == 0) {
        for (i = 0; i < s_nreq; i++) {
            if (s_req[i].kind == 1 && s_req[i].slot == slot) return 1;
            if (s_req[i].kind == 2 && s_req[i].slot == slot) { s_req[i].kind = 1; return 1; }
        }
        memset(&r, 0, sizeof r);
        r.kind = 1; r.slot = slot;
        return req_add(&r);
    }
    for (i = 0; i < s_nreq; i++) {
        if (s_req[i].kind == 2 && s_req[i].slot == slot) { s_req[i].vol = vol; s_req[i].rate = rate; return 1; }
        if (s_req[i].kind == 1 && s_req[i].slot == slot) return 0;
    }
    memset(&r, 0, sizeof r);
    r.kind = 2; r.slot = slot; r.vol = vol; r.rate = rate;
    return req_add(&r);
}

void Sfx_SetAllSeqVolume(int vol, int rate)        /* 0x06007DBA */
{
    int i;
    for (i = 0; i < SFX_SLOTS; i++) if (s_seq[i].state) Sfx_SetSeqVolume(i, vol, rate);
}

void Sfx_StopAll(void)                             /* 0x06007E00 */
{
    int i;
    for (i = 0; i < s_nreq; i++) if (s_req[i].kind == 3) s_req[i].kind = -1;
    for (i = 0; i < SFX_SLOTS; i++) if (s_seq[i].state) Sfx_StopSlot(i);
}

void Sfx_StopId(int id)
{
    int i;
    for (i = 0; i < SFX_SLOTS; i++) if (s_slot[i].id == (int16_t)id && s_seq[i].state) Sfx_StopSlot(i);
}

static int req_cmp(const Req *a, const Req *b)     /* 0x060089D4 by kind, 0x06008AF0 starts by priority */
{
    int ka = a->kind < 0 ? 99 : a->kind, kb = b->kind < 0 ? 99 : b->kind;
    if (ka != kb) return ka - kb;
    if (ka == 3 && a->prio != b->prio) return a->prio - b->prio;
    return a->order < b->order ? -1 : a->order > b->order;
}

static int find_slot(int from, int prio)           /* 0x06007C84 */
{
    int i;
    for (i = from; i < SFX_SLOTS; i++) if (s_slot[i].id == -1) return i;
    for (i = from; i < SFX_SLOTS; i++) if (s_slot[i].prio > prio) return i | 0x8000;
    return -1;
}

static void scheduler(void)                        /* 0x0600863E */
{
    int i, j, slot, keep = 0, n;
    for (i = 1; i < s_nreq; i++) {                  /* stable sort (the Saturn's is a quicksort) */
        Req t = s_req[i];
        for (j = i; j > 0 && req_cmp(&s_req[j - 1], &t) > 0; j--) s_req[j] = s_req[j - 1];
        s_req[j] = t;
    }
    for (i = 0; i < SFX_SLOTS; i++) if (!s_seq[i].state) s_slot[i].id = -1;   /* 0x06007C14 */
    for (slot = 0; slot < SFX_SLOTS; slot++) {
        for (i = keep; i < s_nreq; i++) {
            Req *r = &s_req[i];
            if (r->kind == 0 && r->slot == slot) {
                drv_stop(slot); r->kind = -1; s_slot[slot].id = -1; break;
            }
            if (r->kind == 1 && r->slot == slot) { drv_seq_volume(slot, 127, 0); r->kind = -1; break; }
            if (r->kind == 2 && r->slot == slot) { drv_seq_volume(slot, r->vol, r->rate); r->kind = -1; break; }
            if (r->kind == 3) {
                int f;
                if (r->music ? slot != 0 : slot == 0) continue;     /* slot 0 = tunes only */
                f = find_slot(slot, r->prio);
                if (f == slot) {
                    s_slot[slot].id = r->id;
                    s_slot[slot].prio = r->prio;
                    drv_start(slot, r->bank, r->song);
                    r->kind = -1;
                    break;
                }
                if ((f & 0x7FFF) == slot && f != -1) {               /* steal: stop now, start next frame */
                    Req t = *r;
                    drv_stop(slot);
                    r->kind = -1;
                    s_req[keep++] = t;
                    break;
                }
            }
        }
    }
    /* compaction: drop done entries and the SE starts that found no slot (tunes stay queued) */
    n = keep;
    for (i = keep; i < s_nreq; i++) {
        if (s_req[i].kind == -1 || (s_req[i].kind == 3 && !s_req[i].music)) continue;
        s_req[n++] = s_req[i];
    }
    s_nreq = n;
}

int Sfx_SlotId(int slot) { return slot >= 0 && slot < SFX_SLOTS ? s_slot[slot].id : -1; }
int Sfx_Pending(void) { return s_nreq; }
int Sfx_SlotAttenuation(int slot)
{
    const Seq *s;
    if (slot < 0 || slot >= SFX_SLOTS) return 0;
    s = &s_seq[slot];
    return (s->flags & 0x10) ? s->fade_cur >> 8 : s->att;
}

int Sfx_Count(int id)
{
    int i, n = 0;
    for (i = 0; i < SFX_SLOTS; i++) if (s_slot[i].id == (int16_t)id) n++;
    return n;
}

int Sfx_ActiveSlots(void)
{
    int i, n = 0;
    for (i = 0; i < SFX_SLOTS; i++) n += s_seq[i].state != 0;
    return n;
}

int Sfx_ActiveVoices(void)
{
    int i, n = 0;
    for (i = 0; i < SFX_VOICES; i++) n += s_voice[i].eg != EG_OFF;
    return n;
}

void Sfx_SetMono(int mono)                         /* 0x10B6: every DIPAN to 0 / back */
{
    int i;
    mono = mono != 0;
    if (mono == s_mono) return;
    s_mono = mono;
    for (i = 0; i < SFX_VOICES; i++) {
        if (!mono) s_voice[i].pan = s_voice[i].pan_saved;
        voice_gains(&s_voice[i]);
    }
}

int Sfx_StartSeqDirect(int slot, int seq_bank, int song)
{
    if (slot < 0 || slot >= SFX_SLOTS) return 0;
    return drv_start(slot, seq_bank, song);
}

void Sfx_StopSeqDirect(int slot)
{
    if (slot >= 0 && slot < SFX_SLOTS) drv_stop(slot);
}

SfxBank *Sfx_SetBank(SfxBank *b)                   /* SND_Init + 0x06007BAC / 0x06007E96 */
{
    SfxBank *old = s_bank;
    int i, c;
    if (!s_dbtab_ok) {
        for (i = 0; i <= 1024; i++) s_dbtab[i] = powf(10.0f, -(i * 96.0f / 1024.0f) / 20.0f);
        s_dbtab_ok = 1;
    }
    memset(s_voice, 0, sizeof s_voice);
    memset(s_seq, 0, sizeof s_seq);
    memset(s_note, 0, sizeof s_note);
    for (i = 0; i < SFX_SLOTS; i++) {
        s_slot[i].id = -1; s_slot[i].prio = 0;
        for (c = 0; c < NCH; c++) s_seq[i].ch[c].tone = -1;
    }
    s_nreq = 0;
    s_tick_left = SFX_TICK_SAMPLES; s_main_left = SFX_MAIN_SAMPLES; s_frame_left = 0; s_main_group = 0;
    s_bank = b;
    return old;
}

/* -------------------------------------------------------------------- SCSP */

static float rd(const Voice *v, uint32_t i)
{
    if (v->pcm8) return (float)((int8_t)v->pcm[i] * 256);
    return (float)(int16_t)be16(v->pcm + 2 * i);
}

static float lfo_wave(Voice *v, int ws, int bipolar)
{
    double p = v->lfo_phase;
    float w;
    switch (ws) {
    case 0: w = (float)p; break;                                   /* saw */
    case 1: w = p < 0.5 ? 1.0f : 0.0f; break;                      /* square */
    case 2: w = (float)(p < 0.5 ? 2.0 * p : 2.0 - 2.0 * p); break; /* triangle */
    default: w = (float)(v->noise >> 8 & 0xFF) / 255.0f; break;    /* noise */
    }
    return bipolar ? 2.0f * w - 1.0f : w;
}

static float voice_sample(Voice *v)
{
    uint32_t i = (uint32_t)v->pos, j;
    float fr, s0, s1, out, env, lfo_db = 0.0f;
    double inc = v->inc;
    if (v->lfo & 0x00E7) {                          /* PLFOS / ALFOS (unused by this game) */
        if (v->lfo & 0x00E0) inc *= pow(2.0, k_plfo_cents[v->lfo >> 5 & 7] * lfo_wave(v, v->lfo >> 8 & 3, 1) / 1200.0);
        if (v->lfo & 0x0007) lfo_db = k_alfo_db[v->lfo & 7] * lfo_wave(v, v->lfo >> 3 & 3, 0);
        v->lfo_phase += v->lfo_inc;
        if (v->lfo_phase >= 1.0) { v->lfo_phase -= 1.0; v->noise = v->noise * 1103515245u + 12345u; }
    }
    if (i >= v->lea) i = v->lea - 1;
    fr = (float)(v->pos - i);
    j = i + 1;
    if (j >= v->lea) j = v->lpctl == 1 ? v->lsa : i;
    s0 = rd(v, i);
    s1 = rd(v, j);
    out = s0 + (s1 - s0) * fr;
    /* address generator (SCSP LPCTL: 0 off, 1 forward, 2 reverse, 3 alternating) */
    if (v->dir > 0) {
        v->pos += inc;
        if (v->pos >= v->lea) {
            double span = (double)(v->lea - v->lsa), over = v->pos - v->lea;
            switch (v->lpctl) {
            case 0: v->eg = EG_OFF; v->keyed = 0; return 0.0f;
            case 1: v->pos = v->lsa + fmod(over, span); break;
            default: v->dir = -1; v->pos = v->lea - fmod(over, span) - 1e-6; break;
            }
        }
    } else {
        v->pos -= inc;
        if (v->pos < v->lsa) {
            double span = (double)(v->lea - v->lsa), under = v->lsa - v->pos;
            if (v->lpctl == 3) { v->dir = 1; v->pos = v->lsa + fmod(under, span); }
            else v->pos = v->lea - fmod(under, span) - 1e-6;
        }
    }
    /* EG */
    switch (v->eg) {
    case EG_ATTACK:
        v->att_amp += v->ar_step;
        if (v->att_amp >= 1.0f) { v->att_amp = 1.0f; v->eg = EG_DECAY1; }
        break;
    case EG_DECAY1:
        v->att_db += v->d1_step;
        if (v->att_db >= v->dl_db) v->eg = EG_DECAY2;
        break;
    case EG_DECAY2: v->att_db += v->d2_step; break;
    case EG_RELEASE: v->att_db += v->rr_step; break;
    default: return 0.0f;
    }
    if (v->att_db >= 96.0f) { v->eg = EG_OFF; v->keyed = 0; return 0.0f; }
    {
        float a = v->att_db + lfo_db;
        env = v->att_amp * s_dbtab[a >= 96.0f ? 1024 : (int)(a * (1024.0f / 96.0f))];
    }
    return out * env;
}

static void mix(int32_t *acc, int frames)
{
    int f, i;
    for (f = 0; f < frames; f++) {
        float l = 0.0f, r = 0.0f;
        for (i = 0; i < SFX_VOICES; i++) {
            Voice *v = &s_voice[i];
            float x;
            if (v->eg == EG_OFF) continue;
            x = voice_sample(v);
            l += x * v->gl;
            r += x * v->gr;
        }
        acc[f * 2] += (int32_t)lrintf(l);
        acc[f * 2 + 1] += (int32_t)lrintf(r);
    }
}

void Sfx_Render(int32_t *acc, int frames)
{
    if (!s_bank) return;
    while (frames > 0) {
        int n = frames;
        if (s_frame_left <= 0) { scheduler(); s_frame_left = SFX_FRAME_SAMPLES; }
        if (s_tick_left <= 0) { driver_tick(); s_tick_left = SFX_TICK_SAMPLES; }
        if (s_main_left <= 0) { driver_main(); s_main_left = SFX_MAIN_SAMPLES; }
        if (n > s_frame_left) n = s_frame_left;
        if (n > s_tick_left) n = s_tick_left;
        if (n > s_main_left) n = s_main_left;
        mix(acc, n);
        acc += n * 2;
        frames -= n;
        s_frame_left -= n; s_tick_left -= n; s_main_left -= n;
    }
}

/* ================================================================ game side */

static void hook_play(int id)
{
    if (!Sfx_Play(id)) printf("[sfx] %03X: request refused (%s)\n", id & 0xFFFF, s_bank ? s_bank->name : "no bank");
}

static const AudioSfxHooks k_hooks = { hook_play, Sfx_StopAll, Sfx_Render, Sfx_SetMono };

int Sfx_LoadBankData(const uint8_t *buf, size_t len, const char *name)
{
    SfxBank *nb = SfxBank_Parse(buf, len, name), *old;
    if (!nb) return 0;
    Audio_Lock();
    old = Sfx_SetBank(nb);
    Audio_SetSfxHooks(&k_hooks);
    Audio_Unlock();
    SfxBank_Free(old);
    return 1;
}

int Sfx_HasBankLocked(void)
{
    int r;
    Audio_Lock();
    r = s_bank != NULL;
    Audio_Unlock();
    return r;
}

int Sfx_CountLocked(int id)
{
    int r;
    Audio_Lock();
    r = Sfx_Count(id);
    Audio_Unlock();
    return r;
}

int Sfx_SlotIdLocked(int slot)
{
    int r;
    Audio_Lock();
    r = Sfx_SlotId(slot);
    Audio_Unlock();
    return r;
}

void Sfx_SetSeqVolumeLocked(int slot, int vol, int rate)
{
    Audio_Lock();
    Sfx_SetSeqVolume(slot, vol, rate);
    Audio_Unlock();
}

void Sfx_SetAllSeqVolumeLocked(int vol, int rate)
{
    Audio_Lock();
    Sfx_SetAllSeqVolume(vol, rate);
    Audio_Unlock();
}

void Sfx_StopSlotLocked(int slot)
{
    Audio_Lock();
    Sfx_StopSlot(slot);
    Audio_Unlock();
}
