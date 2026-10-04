/* SCSP sound banks + sequence/voice engine, see sfx.h and docs/sound.md. */
#include "sfx.h"
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
        a = type == 0 ? &b->tone[bank] : type == 1 ? &b->seq[bank] : NULL;   /* 2/3 = DSP program/work: unused */
        if (!a) continue;
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
    FILE *f = fopen(path, "rb");
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

/* ================================================================== engine */

/* SCSP EG times in ms for the effective rate 0..63 (MAME scsp.cpp ARTimes/DRTimes;
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

enum { EG_OFF = 0, EG_ATTACK, EG_DECAY1, EG_DECAY2, EG_RELEASE };

typedef struct {
    int      eg;                /* EG_* (EG_OFF = free) */
    int      slot, ch, key;     /* owner (slot -1 = detached) */
    uint32_t gate;              /* ticks left before key off (0 = released) */
    uint32_t serial;
    const uint8_t *pcm;         /* SA */
    int      pcm8;
    uint32_t lsa, lea;
    int      lpctl, dir;
    double   pos, inc;
    int      base;              /* layer base key */
    float    fine;              /* semitones */
    int      bend_range;        /* semitones (voice header byte 0) */
    float    lev;               /* DISDL/TL/velocity */
    int      dipan;
    float    gl, gr;            /* final gains */
    float    att_amp;           /* attack 0..1 */
    float    att_db;            /* EG attenuation 0..96 */
    float    ar_step, d1_step, d2_step, rr_step, dl_db;
} Voice;

typedef struct {
    int      active, ended;
    int      id, prio, music;
    uint32_t serial;
    const uint8_t *song;        /* song header */
    uint32_t len;               /* bytes from song to the end of the seq bank */
    uint32_t data, pc, ret;     /* offsets from `song` */
    int      ref_left;
    int32_t  loop_pc;
    int      loop_tempo;
    uint32_t wait;
    uint32_t gate_ext, step_ext;
    int      ntempo, tempo_idx;
    uint32_t tempo_left;
    uint16_t res;
    double   frac, tpf;         /* tick accumulator, ticks per output frame */
    struct { uint8_t bank, prog, vol, pan, bend; } ch[16];
} Slot;

static SfxBank *s_bank;
static Slot     s_slot[SFX_SLOTS];
static Voice    s_voice[SFX_VOICES];
static uint32_t s_serial;
static float    s_master = 0.4f;    /* one full-level SE peaks at -8 dBFS */

const SfxBank *Sfx_GetBank(void) { return s_bank; }
void Sfx_SetVolume(float v) { s_master = v; }

static float sdl_gain(int level)        /* SCSP send level 0..7: 0 = off, -6 dB/step */
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

static void voice_update(Voice *v)
{
    const Slot *s = v->slot >= 0 ? &s_slot[v->slot] : NULL;
    float g = v->lev * s_master, l = 1.0f, r = 1.0f;
    double semis = (double)(v->key - v->base) + v->fine;
    if (s) {
        float vol = s->ch[v->ch].vol / 127.0f;
        int pan = s->ch[v->ch].pan;
        g *= vol * vol;
        if (pan < 64) r = pan / 64.0f; else if (pan > 64) l = (127 - pan) / 63.0f;
        semis += (s->ch[v->ch].bend - 64) / 64.0 * v->bend_range;
    }
    if (v->dipan & 0x0F) {                          /* layer DIPAN: 3 dB steps, bit 4 = side */
        float a = (v->dipan & 0x0F) == 0x0F ? 0.0f : powf(10.0f, -3.0f * (v->dipan & 0x0F) / 20.0f);
        if (v->dipan & 0x10) l *= a; else r *= a;
    }
    v->gl = g * l;
    v->gr = g * r;
    v->inc = pow(2.0, semis / 12.0);
}

static void voice_release(Voice *v)
{
    if (v->eg == EG_OFF) return;
    v->gate = 0;
    v->eg = EG_RELEASE;
}

static Voice *voice_alloc(void)
{
    Voice *best = NULL;
    int i;
    for (i = 0; i < SFX_VOICES; i++) if (s_voice[i].eg == EG_OFF) return &s_voice[i];
    for (i = 0; i < SFX_VOICES; i++)                        /* oldest released, else oldest */
        if (s_voice[i].eg == EG_RELEASE && (!best || s_voice[i].serial < best->serial)) best = &s_voice[i];
    if (best) return best;
    for (i = 0; i < SFX_VOICES; i++)
        if (!best || s_voice[i].serial < best->serial) best = &s_voice[i];
    return best;
}

static float s_dbtab[1025];             /* attenuation (96 dB / 1024 steps) -> gain */
static int   s_dbtab_ok;

/* TON voice: u8 bend range (semitones), u8 ?, u8 layers - 1, u8 ?; then 32-byte layers:
 * +00 start key, +01 end key, +02 SCSP reg 0x00 bits (b6-5 LPCTL, b4 PCM8B, b3-0 SA
 * 19-16), +04 SA 15-0 (byte offset in the tone bank), +06 LSA, +08 LEA (samples),
 * +0A reg 0x08 (D2R/D1R/EGHOLD/AR), +0C reg 0x0A (LPSLNK/KRS/DL/RR), +0E reg 0x0C
 * (TL in the low byte), +10 reg 0x0E (modulation), +12 / +14 (0 in this game),
 * +16 reg 0x14 (ISEL/IMXL), +18 DISDL<<5 | DIPAN, +19 base key, +1A fine tune (s8). */
static void layer_on(int si, Slot *s, int ch, int key, int vel, uint32_t gate,
                     const SfxArea *a, const uint8_t *vh, const uint8_t *l)
{
    Voice *v;
    uint16_t w0 = be16(l + 2), r08 = be16(l + 0x0A), r0a = be16(l + 0x0C);
    uint32_t sa = (uint32_t)(w0 & 15) << 16 | be16(l + 4), lsa = be16(l + 6), lea = be16(l + 8), maxs;
    int pcm8 = w0 >> 4 & 1, lpctl = w0 >> 5 & 3, disdl = l[0x18] >> 5, imxl = l[0x17] & 7;
    float velg = vel / 127.0f;
    if (sa >= a->size) return;
    maxs = (a->size - sa) / (pcm8 ? 1u : 2u);
    if (lea > maxs) lea = maxs;
    if (lea < 2) return;
    if (lsa >= lea) lpctl = 0;
    v = voice_alloc();
    if (!v) return;
    memset(v, 0, sizeof *v);
    v->slot = si; v->ch = ch; v->key = key; v->gate = gate; v->serial = ++s_serial;
    v->pcm = a->data + sa; v->pcm8 = pcm8; v->lsa = lsa; v->lea = lea; v->lpctl = lpctl; v->dir = 1;
    v->base = l[0x19];
    v->fine = (int8_t)l[0x1A] / 128.0f;
    v->bend_range = vh[0];
    /* DISDL = direct send; layers with DISDL 0 only feed the DSP (reverb), use IMXL */
    v->lev = sdl_gain(disdl ? disdl : imxl) * powf(10.0f, -0.375f * l[0x0F] / 20.0f) * velg * velg;
    v->dipan = l[0x18] & 0x1F;
    {
        int ar = r08 & 31, d1r = r08 >> 6 & 31, d2r = r08 >> 11 & 31, rr = r0a & 31, dl = r0a >> 5 & 31;
        float ms = ar ? k_ar_ms[2 * ar > 63 ? 63 : 2 * ar] : 100000.0f;
        v->ar_step = ms <= 0.0f ? 1.0f : 1.0f / (ms * (SFX_RATE / 1000.0f));
        v->d1_step = d1r ? per_sample_db(2 * d1r, k_dr_ms) : 0.0f;
        v->d2_step = d2r ? per_sample_db(2 * d2r, k_dr_ms) : 0.0f;
        v->rr_step = rr ? per_sample_db(2 * rr, k_dr_ms) : 0.0f;
        v->dl_db = dl * 3.0f;
    }
    v->eg = EG_ATTACK;
    (void)s;
    voice_update(v);
}

static void note_on(int si, Slot *s, int ch, int key, int vel, uint32_t gate)
{
    int bank = s->ch[ch].bank & 15, prog = s->ch[ch].prog, nl, i;
    const SfxArea *a = &s_bank->tone[bank];
    const uint8_t *vh;
    uint32_t vo;
    if (!a->data || prog >= SfxBank_VoiceCount(s_bank, bank)) return;
    vo = be16(a->data + 8 + 2 * prog);
    if (vo + 4 > a->size) return;
    vh = a->data + vo;
    nl = vh[2] + 1;
    for (i = 0; i < nl; i++) {
        const uint8_t *l = vh + 4 + 32 * i;
        if (vo + 4 + 32u * (uint32_t)(i + 1) > a->size) break;
        if (key >= l[0] && key <= l[1]) layer_on(si, s, ch, key, vel, gate, a, vh, l);
    }
}

static void channel_changed(int si, int ch)
{
    int i;
    for (i = 0; i < SFX_VOICES; i++)
        if (s_voice[i].eg != EG_OFF && s_voice[i].slot == si && s_voice[i].ch == ch) voice_update(&s_voice[i]);
}

static void set_tempo(Slot *s, int idx)
{
    const uint8_t *t;
    uint32_t us;
    if (idx < 0 || idx >= s->ntempo) return;
    t = s->song + 8 + 8 * idx;
    us = be32(t + 4);
    if (!us) us = 500000;
    s->tempo_idx = idx;
    s->tempo_left = be32(t);
    s->tpf = (double)s->res * 1e6 / us / SFX_RATE;
}

/* One SEQ event (SBL sequence format, docs/sound.md):
 *  00-7F  note: ch = low nibble, key, vel, gate, step; bit 6 = gate +0x100, bit 5 = step +0x100
 *  Bx     control change: ctrl, value, step (07 volume, 0A pan, 20 tone bank)
 *  Cx     program: voice, step        Ex pitch bend: value (40 = centre), step
 *  Ax/Dx  pressure (ignored)          81 hh ll nn: play nn events at data + hhll, return
 *  82 ss  loop marker (1st = loop start, 2nd = jump back)      83 end of track
 *  88-8B  next gate += 0x200/0x800/0x1000/0x2000; 8C-8F next step += 0x100/0x200/0x800/0x1000
 * Returns 0 when the track ended. */
static int seq_event(int si)
{
    static const uint32_t gext[4] = { 0x200, 0x800, 0x1000, 0x2000 }, sext[4] = { 0x100, 0x200, 0x800, 0x1000 };
    Slot *s = &s_slot[si];
    uint32_t p = s->pc, step = 0;
    const uint8_t *e = s->song + p;
    uint8_t st;
    int counted = 1, c;
#define NEED(n) do { if (p + (n) > s->len) { s->ended = 1; return 0; } } while (0)
    NEED(1);
    st = e[0];
    c = st & 15;
    if (st < 0x80) {
        uint32_t gate;
        NEED(5);
        gate = e[3] + ((st & 0x40) ? 0x100u : 0u) + s->gate_ext;
        step = e[4] + ((st & 0x20) ? 0x100u : 0u) + s->step_ext;
        note_on(si, s, c, e[1], e[2] & 0x7F, gate ? gate : 1);
        p += 5;
    } else switch (st >> 4) {
    case 0xB:
        NEED(4);
        if (e[1] == 0x07) s->ch[c].vol = e[2] & 0x7F;
        else if (e[1] == 0x0A) s->ch[c].pan = e[2] & 0x7F;
        else if (e[1] == 0x20) s->ch[c].bank = e[2] & 15;
        if (e[1] == 0x07 || e[1] == 0x0A) channel_changed(si, c);
        step = e[3] + s->step_ext;
        p += 4;
        break;
    case 0xA:
        NEED(4); step = e[3] + s->step_ext; p += 4; break;
    case 0xC:
        NEED(3); s->ch[c].prog = e[1]; step = e[2] + s->step_ext; p += 3; break;
    case 0xD:
        NEED(3); step = e[2] + s->step_ext; p += 3; break;
    case 0xE:
        NEED(3); s->ch[c].bend = e[1] & 0x7F; channel_changed(si, c); step = e[2] + s->step_ext; p += 3; break;
    case 0x8:
        if (st == 0x81) {                           /* reference (not counted, no nesting) */
            NEED(4);
            counted = 0;
            if (!s->ref_left && e[3] && s->data + be16(e + 1) < s->len) {
                s->ret = p + 4;
                s->ref_left = e[3];
                p = s->data + be16(e + 1);
            } else p += 4;
            break;
        }
        if (st == 0x82) {                           /* loop point */
            NEED(2);
            step = e[1] + s->step_ext;
            p += 2;
            if (s->loop_pc < 0) { s->loop_pc = (int32_t)p; s->loop_tempo = s->tempo_idx + 1; }
            else { p = (uint32_t)s->loop_pc; set_tempo(s, s->loop_tempo); }
            break;
        }
        if (st == 0x83) { s->ended = 1; return 0; }
        if (st >= 0x88 && st <= 0x8B) { s->gate_ext += gext[st - 0x88]; counted = 0; p += 1; break; }
        if (st >= 0x8C) { s->step_ext += sext[st - 0x8C]; counted = 0; p += 1; break; }
        /* fall through */
    default:
        printf("[sfx] seq id %03X: unknown event %02X at +0x%X\n", s->id & 0xFFFF, st, p);
        s->ended = 1;
        return 0;
    }
#undef NEED
    if (counted) {
        s->gate_ext = s->step_ext = 0;
        if (s->ref_left && --s->ref_left == 0) p = s->ret;
    }
    s->pc = p;
    s->wait = step;
    return 1;
}

static void slot_detach(int si, int release)
{
    int i;
    for (i = 0; i < SFX_VOICES; i++)
        if (s_voice[i].slot == si) {
            if (release && s_voice[i].gate) voice_release(&s_voice[i]);
            s_voice[i].slot = -1;
        }
}

static void slot_tick(int si)
{
    Slot *s = &s_slot[si];
    int i, guard = 0, held = 0;
    for (i = 0; i < SFX_VOICES; i++) {
        Voice *v = &s_voice[i];
        if (v->slot == si && v->gate && v->eg != EG_OFF && --v->gate == 0) voice_release(v);
    }
    if (s->tempo_left && --s->tempo_left == 0 && s->tempo_idx + 1 < s->ntempo) set_tempo(s, s->tempo_idx + 1);
    if (!s->ended) {
        if (s->wait) s->wait--;
        while (!s->wait && !s->ended && guard++ < 4096) seq_event(si);
        if (guard >= 4096) s->ended = 1;            /* loop without time: stop */
    }
    if (s->ended) {
        for (i = 0; i < SFX_VOICES; i++)
            if (s_voice[i].slot == si && s_voice[i].gate && s_voice[i].eg != EG_OFF) held = 1;
        if (!held) { s->active = 0; slot_detach(si, 0); }
    }
}

static void slot_stop(int si)
{
    s_slot[si].active = 0;
    slot_detach(si, 1);
}

SfxBank *Sfx_SetBank(SfxBank *b)
{
    SfxBank *old = s_bank;
    int i;
    for (i = 0; i < SFX_SLOTS; i++) s_slot[i].active = 0;
    for (i = 0; i < SFX_VOICES; i++) { s_voice[i].eg = EG_OFF; s_voice[i].slot = -1; }
    s_bank = b;
    return old;
}

int Sfx_PlaySong(int seq_bank, int song, int prio, int id, int music)
{
    const SfxArea *a;
    const uint8_t *sp;
    uint32_t off, len;
    int i, si = -1, c;
    Slot *s;
    if (!s_dbtab_ok) {
        for (i = 0; i <= 1024; i++) s_dbtab[i] = powf(10.0f, -(i * 96.0f / 1024.0f) / 20.0f);
        s_dbtab_ok = 1;
    }
    if (!s_bank || song < 0 || song >= SfxBank_SongCount(s_bank, seq_bank)) return 0;
    a = &s_bank->seq[seq_bank];
    off = be32(a->data + 2 + 4 * song);
    if (off >= a->size || a->size - off < 16) return 0;
    sp = a->data + off;
    len = a->size - off;
    if (8u + 8u * be16(sp + 2) > len || be16(sp + 4) >= len) return 0;
    if (music)                                      /* one tune at a time */
        for (i = 0; i < SFX_SLOTS; i++) if (s_slot[i].active && s_slot[i].music) slot_stop(i);
    for (i = 0; i < SFX_SLOTS && si < 0; i++) if (s_slot[i].active && s_slot[i].id == id) si = i;   /* restart */
    for (i = 0; i < SFX_SLOTS && si < 0; i++) if (!s_slot[i].active) si = i;
    if (si < 0)                                     /* steal: lowest priority, then oldest */
        for (i = 0; i < SFX_SLOTS; i++) {
            const Slot *t = &s_slot[i];
            if (t->music || t->prio > prio) continue;
            if (si < 0 || t->prio < s_slot[si].prio || (t->prio == s_slot[si].prio && t->serial < s_slot[si].serial)) si = i;
        }
    if (si < 0) return 0;
    slot_stop(si);
    s = &s_slot[si];
    memset(s, 0, sizeof *s);
    s->id = id; s->prio = prio; s->music = music; s->serial = ++s_serial;
    s->song = sp; s->len = len;
    s->res = be16(sp);
    s->ntempo = be16(sp + 2);
    s->data = s->pc = be16(sp + 4);
    s->loop_pc = -1;
    for (c = 0; c < 16; c++) { s->ch[c].vol = 127; s->ch[c].pan = 64; s->ch[c].bend = 64; }
    s->tpf = (double)(s->res ? s->res : 480) * 1e6 / 500000.0 / SFX_RATE;
    if (!s->res) s->res = 480;
    set_tempo(s, 0);
    s->frac = 1.0;                                  /* first tick on the next frame */
    s->active = 1;
    return 1;
}

int Sfx_Play(int id)
{
    SfxDef d;
    if (!Sfx_Lookup(id, &d)) return 0;
    return Sfx_PlaySong(d.seq_bank, d.song, d.prio, (int16_t)id, d.music);
}

void Sfx_StopAll(void)
{
    int i;
    for (i = 0; i < SFX_SLOTS; i++) slot_stop(i);
}

void Sfx_StopId(int id)
{
    int i;
    for (i = 0; i < SFX_SLOTS; i++) if (s_slot[i].active && s_slot[i].id == (int16_t)id) slot_stop(i);
}

int Sfx_Count(int id)
{
    int i, n = 0;
    for (i = 0; i < SFX_SLOTS; i++) if (s_slot[i].active && s_slot[i].id == (int16_t)id) n++;
    return n;
}

int Sfx_ActiveSlots(void)
{
    int i, n = 0;
    for (i = 0; i < SFX_SLOTS; i++) n += s_slot[i].active != 0;
    return n;
}

int Sfx_ActiveVoices(void)
{
    int i, n = 0;
    for (i = 0; i < SFX_VOICES; i++) n += s_voice[i].eg != EG_OFF;
    return n;
}

static float rd(const Voice *v, uint32_t i)
{
    if (v->pcm8) return (float)((int8_t)v->pcm[i] * 256);
    return (float)(int16_t)be16(v->pcm + 2 * i);
}

static float voice_sample(Voice *v)
{
    uint32_t i = (uint32_t)v->pos, j;
    float fr, s0, s1, out, env;
    if (i >= v->lea) i = v->lea - 1;
    fr = (float)(v->pos - i);
    j = i + 1;
    if (j >= v->lea) j = v->lpctl == 1 ? v->lsa : i;
    s0 = rd(v, i);
    s1 = rd(v, j);
    out = s0 + (s1 - s0) * fr;
    /* address generator (SCSP LPCTL: 0 off, 1 forward, 2 reverse, 3 alternating) */
    if (v->dir > 0) {
        v->pos += v->inc;
        if (v->pos >= v->lea) {
            double span = (double)(v->lea - v->lsa), over = v->pos - v->lea;
            switch (v->lpctl) {
            case 0: v->eg = EG_OFF; break;
            case 1: v->pos = v->lsa + fmod(over, span); break;
            case 2: v->dir = -1; v->pos = v->lea - fmod(over, span) - 1e-6; break;
            default: v->dir = -1; v->pos = v->lea - fmod(over, span) - 1e-6; break;
            }
        }
    } else {
        v->pos -= v->inc;
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
    if (v->att_db >= 96.0f) { v->eg = EG_OFF; v->slot = -1; return 0.0f; }
    env = v->att_amp * s_dbtab[(int)(v->att_db * (1024.0f / 96.0f))];
    return out * env;
}

void Sfx_Render(int32_t *acc, int frames)
{
    int f, i;
    if (!s_bank) return;
    for (f = 0; f < frames; f++) {
        float l = 0.0f, r = 0.0f;
        for (i = 0; i < SFX_SLOTS; i++) {
            Slot *s = &s_slot[i];
            if (!s->active) continue;
            s->frac += s->tpf;
            while (s->frac >= 1.0 && s->active) { s->frac -= 1.0; slot_tick(i); }
        }
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

/* ================================================================ game side */

static void hook_play(int id)
{
    if (!Sfx_Play(id)) printf("[sfx] %03X: no sequence in %s\n", id & 0xFFFF, s_bank ? s_bank->name : "(no bank)");
}

static const AudioSfxHooks k_hooks = { hook_play, Sfx_StopAll, Sfx_Render };

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
