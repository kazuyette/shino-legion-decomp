/* SCSP sound banks / SFX engine (src/sfx.c), no SDL.
 * cd pc-port && S=$(cat tests/SOURCES.txt) && gcc -std=c11 -Wall -Wextra -O1 -Isrc -Itests \
 *     tests/test_sfx.c $S -lm -o /tmp/test_sfx && /tmp/test_sfx ../extracted/
 * Parses every .SND, walks every song, renders SFX to /tmp/sfx_<bank>_<id>.wav. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "sfx.h"
#include "audio.h"
#include "input.h"

static InputPort s_fake[2];
const InputPort *Input_GetPort(int p) { return &s_fake[p & 1]; }   /* pad.c link stub */

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static const char *k_files[10] = { "STAGE7.SND", "STAGE2.SND", "STAGE3.SND", "STAGE4.SND", "STAGE5.SND",
                                   "STAGE6.SND", "STAGE1.SND", "STAGE8.SND", "STAGE9.SND", "KAZUMA.SND" };

static void put16(FILE *f, unsigned v) { fputc((int)(v & 255), f); fputc((int)(v >> 8 & 255), f); }
static void put32(FILE *f, unsigned v) { put16(f, v & 0xFFFF); put16(f, v >> 16); }

static void write_wav(const char *path, const int16_t *s, int frames)
{
    FILE *f = fopen(path, "wb");
    int i;
    if (!f) return;
    fwrite("RIFF", 1, 4, f); put32(f, 36u + (unsigned)frames * 4u); fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16); put16(f, 1); put16(f, 2); put32(f, SFX_RATE); put32(f, SFX_RATE * 4); put16(f, 4); put16(f, 16);
    fwrite("data", 1, 4, f); put32(f, (unsigned)frames * 4u);
    for (i = 0; i < frames * 2; i++) put16(f, (uint16_t)s[i]);
    fclose(f);
}

/* Renders id until the sequence and its voices are done (max `maxsec`). Returns frames,
 * *peak = max |sample|. */
static int render_id(int id, double maxsec, int16_t **out, int *peak)
{
    int cap = (int)(maxsec * SFX_RATE), n = 0, chunk = 512, i;
    int16_t *buf = (int16_t *)malloc((size_t)cap * 4);
    int32_t acc[1024];
    *peak = 0;
    if (!Sfx_Play(id)) { free(buf); *out = NULL; return 0; }
    while (n < cap) {
        int k = cap - n < chunk ? cap - n : chunk;
        memset(acc, 0, sizeof acc);
        Sfx_Render(acc, k);
        for (i = 0; i < k * 2; i++) {
            int32_t v = acc[i] > 32767 ? 32767 : acc[i] < -32768 ? -32768 : acc[i];
            int a = v < 0 ? -v : v;
            if (a > *peak) *peak = a;
            buf[n * 2 + i] = (int16_t)v;
        }
        n += k;
        if (!Sfx_ActiveSlots() && !Sfx_ActiveVoices()) break;
    }
    *out = buf;
    return n;
}

/* Walks a song's events like the engine (format check: every song must end with 0x83).
 * *maxvel = loudest note velocity (unused SE slots are a single velocity-2 note). */
static int walk_song(const uint8_t *song, uint32_t len, int *maxvel)
{
    uint32_t data = (uint32_t)(song[4] << 8 | song[5]), p = data;
    int n = 0;
    *maxvel = 0;
    while (p < len && n < 100000) {
        uint8_t st = song[p];
        if (st < 0x80) { if (p + 2 < len && song[p + 2] > *maxvel) *maxvel = song[p + 2]; p += 5; }
        else if ((st >> 4) == 0xB || (st >> 4) == 0xA) p += 4;
        else if ((st >> 4) == 0xC || (st >> 4) == 0xD || (st >> 4) == 0xE) p += 3;
        else if (st == 0x81) { if ((uint32_t)(data + (song[p + 1] << 8 | song[p + 2])) >= len) return -1; p += 4; }
        else if (st == 0x82) p += 2;
        else if (st == 0x83) return n;
        else if (st >= 0x88) p += 1;
        else return -1;
        n++;
    }
    return -1;
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "../extracted/";
    char path[600];
    int f, loaded = 0;
    SfxDef d;

    /* id table (A.BIN 0x0600EF20 / EF50 / F0D0) */
    CHECK(Sfx_Lookup(0x001, &d) && d.seq_bank == 2 && d.song == 1 && d.music);
    CHECK(Sfx_Lookup(0x108, &d) && d.seq_bank == 0 && d.song == 7 && d.prio == 16 && !d.music);
    CHECK(Sfx_Lookup(0x107, &d) && d.seq_bank == 0 && d.song == 6 && d.prio == 4);
    CHECK(Sfx_Lookup(0x116, &d) && d.seq_bank == 1 && d.song == 0);
    CHECK(Sfx_Lookup(0x237, &d) && d.seq_bank == 3 && d.song == 55);
    CHECK(!Sfx_Lookup(0x238, &d) && !Sfx_Lookup(-1, &d) && !Sfx_Lookup(6, &d));

    for (f = 0; f < 10; f++) {
        SfxBank *b;
        int sb, tb, s, id, silent = 0, played = 0, songs_ok = 0, songs = 0, dummy = 0;
        snprintf(path, sizeof path, "%s/%s", dir, k_files[f]);
        b = SfxBank_Load(path);
        if (!b) { printf("skip %s (missing)\n", path); continue; }
        loaded++;
        CHECK(b->nmap == 8);
        CHECK(b->lz_window_refs == 0);
        for (sb = 0; sb < 4; sb++) {
            CHECK(b->seq[sb].data != NULL && b->seq[sb].size <= b->seq[sb].alloc);
            for (s = 0; s < SfxBank_SongCount(b, sb); s++) {
                const uint8_t *q = b->seq[sb].data;
                uint32_t off = (uint32_t)q[2 + 4 * s] << 24 | (uint32_t)q[3 + 4 * s] << 16 | (uint32_t)q[4 + 4 * s] << 8 | q[5 + 4 * s];
                int mv;
                songs++;
                if (off < b->seq[sb].size && walk_song(q + off, b->seq[sb].size - off, &mv) >= 0) songs_ok++;   /* (seq 1 songs 5/6 are empty) */
                else printf("  %s seq %d song %d: bad event stream (offset 0x%X)\n", k_files[f], sb, s, off);
            }
        }
        for (tb = 0; tb < 4; tb++) CHECK(b->tone[tb].data != NULL && SfxBank_VoiceCount(b, tb) > 0);
        CHECK(songs_ok == songs);
        printf("%-10s songs %d/%d (seq0 %d seq1 %d seq2 %d seq3 %d) voices %d/%d/%d/%d\n", k_files[f], songs_ok, songs,
               SfxBank_SongCount(b, 0), SfxBank_SongCount(b, 1), SfxBank_SongCount(b, 2), SfxBank_SongCount(b, 3),
               SfxBank_VoiceCount(b, 0), SfxBank_VoiceCount(b, 1), SfxBank_VoiceCount(b, 2), SfxBank_VoiceCount(b, 3));
        Sfx_SetBank(b);
        /* every SE id of the tables: must start and make sound (bank 3 ids only up to its song count) */
        for (id = 0x100; id < 0x238; id++) {
            int16_t *pcm;
            int peak, n;
            if (id == 0x130) id = 0x200;
            if (!Sfx_Lookup(id, &d) || d.song >= SfxBank_SongCount(b, d.seq_bank)) continue;
            {
                const uint8_t *q = b->seq[d.seq_bank].data;
                uint32_t off = (uint32_t)q[2 + 4 * d.song] << 24 | (uint32_t)q[3 + 4 * d.song] << 16 | (uint32_t)q[4 + 4 * d.song] << 8 | q[5 + 4 * d.song];
                int mv = 0;
                walk_song(q + off, b->seq[d.seq_bank].size - off, &mv);
                if (mv <= 4) { dummy++; continue; }      /* unused id: one velocity-2 note */
            }
            n = render_id(id, 4.0, &pcm, &peak);
            played++;
            if (f == 0) printf("  id %03X seq %d/%2d: %.2f s peak %d\n", id, d.seq_bank, d.song, n / (double)SFX_RATE, peak);
            if (peak < 256) { silent++; printf("  %s id %03X (seq %d/%d): silent\n", k_files[f], id, d.seq_bank, d.song); }
            if (f == 0 && pcm && (id <= 0x10C || id == 0x111 || id == 0x116 || id == 0x125 || id == 0x126 || (id >= 0x200 && id < 0x208))) {
                snprintf(path, sizeof path, "/tmp/sfx_%s_%03X.wav", "STAGE7", id);
                write_wav(path, pcm, n);
            }
            free(pcm);
            Sfx_StopAll();
            { int32_t acc[2048]; memset(acc, 0, sizeof acc); Sfx_Render(acc, 1024); }   /* release tails */
        }
        printf("  %d SE played, %d silent, %d unused (velocity-2 dummies)\n", played, silent, dummy);
        CHECK(played > 50 && silent == 0);
        /* music jingles of bank 2 (stage clear) */
        if (f == 0) {
            int16_t *pcm;
            int peak, n = render_id(0x001, 8.0, &pcm, &peak);
            printf("  tune 001 (stage clear): %.2f s, peak %d\n", n / (double)SFX_RATE, peak);
            CHECK(peak > 1000);
            if (pcm) { write_wav("/tmp/sfx_STAGE7_001.wav", pcm, n); free(pcm); }
            Sfx_StopAll();
            n = render_id(0x003, 6.0, &pcm, &peak);        /* stage BGM (loops) */
            printf("  tune 003 (stage music): rendered %.2f s, peak %d, still playing %d\n", n / (double)SFX_RATE, peak, Sfx_ActiveSlots());
            CHECK(peak > 1000 && Sfx_ActiveSlots() == 1);
            if (pcm) { write_wav("/tmp/sfx_STAGE7_003.wav", pcm, n); free(pcm); }
            Sfx_StopAll();
        }
        SfxBank_Free(Sfx_SetBank(NULL));
    }
    CHECK(loaded == 10);

    /* engine semantics + audio.c hook (AUDIO_NO_SDL mixer) */
    snprintf(path, sizeof path, "%s/%s", dir, "STAGE7.SND");
    {
        FILE *fp = fopen(path, "rb");
        if (fp) {
            static uint8_t buf[1 << 20];
            size_t len = fread(buf, 1, sizeof buf, fp);
            int16_t out[2048];
            int i, peak = 0;
            fclose(fp);
            CHECK(Sfx_LoadBankData(buf, len, "STAGE7.SND"));
            CHECK(Sfx_HasBankLocked());
            Audio_PlaySfx(0x108);
            CHECK(Sfx_CountLocked(0x108) == 1);
            Audio_PlaySfx(0x108);                      /* same id restarts its slot */
            CHECK(Sfx_CountLocked(0x108) == 1);
            Audio_PlaySfx(0x10C);
            CHECK(Sfx_ActiveSlots() == 2);
            for (i = 0; i < 8; i++) {
                int k;
                Audio_Render(out, 1024);
                for (k = 0; k < 2048; k++) if (abs(out[k]) > peak) peak = abs(out[k]);
            }
            printf("Audio_Render with SFX hook: peak %d\n", peak);
            CHECK(peak > 1000);
            Audio_StopSfx();
            CHECK(Sfx_ActiveSlots() == 0);
            Audio_Render(out, 1024);                   /* release (RR 31: a few ms) */
            Audio_Render(out, 1024);
            peak = 0;
            for (i = 0; i < 2048; i++) if (abs(out[i]) > peak) peak = abs(out[i]);
            CHECK(peak == 0 && Sfx_ActiveVoices() == 0);
            /* 8 slots: a 9th SE with lower priority is refused, music never stolen */
            Audio_PlaySfx(0x003);
            for (i = 0x200; i < 0x208; i++) Audio_PlaySfx(i);
            CHECK(Sfx_ActiveSlots() == 8 && Sfx_Count(0x003) == 1);
            Audio_StopSfx();
        }
    }
    printf(fails ? "test_sfx: %d FAILURES\n" : "test_sfx: all passed\n", fails);
    return fails != 0;
}
