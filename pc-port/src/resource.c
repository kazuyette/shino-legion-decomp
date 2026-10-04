/* File loading of A.BIN (resource requests, queue, overlay loads), see resource.h. */
#include "resource.h"
#include "audio.h"
#include "sfx.h"
#include "ram.h"
#include "lz.h"
#include "render.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char s_assetDir[512] = "assets";
static int  s_sndLoaded;                              /* a .SND bank was loaded (sfx.c) */
static char s_sndName[16];                            /* ... its name */
static int  s_overlay = OVL_NONE;

void Res_SetAssetDir(const char *dir)
{
    snprintf(s_assetDir, sizeof s_assetDir, "%s", dir);
    size_t n = strlen(s_assetDir);
    while (n > 1 && (s_assetDir[n - 1] == '/' || s_assetDir[n - 1] == '\\')) s_assetDir[--n] = 0;
}
const char *Res_GetAssetDir(void) { return s_assetDir; }
int Res_OverlayResident(void) { return s_overlay; }
void Res_SetOverlayResident(int ovl) { s_overlay = ovl; }   /* PC: snapshot transplant (tests/snap_compare.c) */

/* 0x06004418: GFS init (not needed on PC) + request state cleared. */
void Res_Init(void)
{
    W16(RES_QCOUNT, 0);
    W32(0x060169ACu, 0);
    Ram_Ptr(0x060169B0u)[0] = 0;
    W16(0x060169BEu, 0);
    W32(0x060169C0u, 0);
    W16(0x060169D2u, 0);
}

/* 0x060048D8: waits for a pending async read and closes the GFS handle. The PC reads are
 * synchronous and closed at once, so there is never anything open. */
/* PC: CD-DA tracks are WAV files ripped by tools/rip_cdda.py (TITLE.PCM = TRACK02.WAV,
 * ENDING.PCM = TRACK03.WAV). Opening one starts it (played once, like the CD read). */
static int s_cddaOpen;
void Res_CloseIfOpen(void) { if (s_cddaOpen) { Audio_StopMusic(); s_cddaOpen = 0; } }   /* 0x060048D8: 0x0602F626 stops CD-DA */

/* 0x0600498A: copy the file name (max 12 chars) into a request. */
static void copy_name(uint32_t src, uint32_t dst)
{
    int i = 0;
    for (; i < 12 && Ram_Ptr(src + (uint32_t)i)[0]; i++) Ram_Ptr(dst + (uint32_t)i)[0] = Ram_Ptr(src + (uint32_t)i)[0];
    Ram_Ptr(dst + (uint32_t)i)[0] = 0;
}

static void write_req(uint32_t e, int type, int kind, uint32_t a, uint32_t b, uint32_t dest, uint32_t name)
{
    W16(e + 0, (uint32_t)type);
    W16(e + 2, (uint32_t)kind);
    W32(e + 4, a);
    W32(e + 8, b);
    W32(e + 0xC, dest);
    if (name) { Ram_Ptr(e + 0x10)[0] = 0; copy_name(name, e + 0x10); }
}

static uint8_t *read_file(const char *name, size_t *len)
{
    char p[600];
    snprintf(p, sizeof p, "%s/%s", s_assetDir, name);
    FILE *f = fopen(p, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = (uint8_t *)malloc(n > 0 ? (size_t)n : 1);
    size_t got = buf ? fread(buf, 1, (size_t)(n > 0 ? n : 0), f) : 0;
    fclose(f);
    if (!buf) return NULL;
    *len = got;
    return buf;
}

static uint32_t region_end(uint32_t a)
{
    if ((a & 0xFFF80000u) == VDP1_VRAM_BASE) return VDP1_VRAM_BASE + VDP1_VRAM_SIZE;
    if ((a & 0xFFF80000u) == VDP2_VRAM_BASE) return VDP2_VRAM_BASE + VDP2_VRAM_SIZE;
    if ((a & 0x0FF00000u) == LWRAM_BASE) return LWRAM_BASE + LWRAM_SIZE;
    return HWRAM_BASE + HWRAM_SIZE;
}

/* Executes one request (the GFS reads of 0x060046C4 / 0x060047A8 / 0x0600475C). */
static int execute(uint32_t e)
{
    int type = RS16(e), kind = RS16(e + 2);
    uint32_t dest = R32(e + 0xC);
    char name[16];
    memcpy(name, Ram_Ptr(e + 0x10), 13);
    name[13] = 0;
    W16(0x060169BEu, (uint32_t)type);                 /* current request type */
    W16(0x060169D2u, (uint32_t)kind);
    W32(0x060169CCu, dest);
    if (type == 2) {                                  /* open only: CD-DA tracks (TITLE.PCM...) */
        char wav[600];
        const char *track = strstr(name, "ENDING") ? "TRACK03.WAV" : strstr(name, "TITLE") ? "TRACK02.WAV" : NULL;
        if (track) {
            snprintf(wav, sizeof wav, "%s/%s", s_assetDir, track);
            if (Audio_PlayMusic(wav, 0)) { s_cddaOpen = 1; printf("[res] CD-DA %s -> %s\n", name, track); }
            else printf("[res] CD-DA %s: %s missing (tools/rip_cdda.py)\n", name, wav);
        } else printf("[res] open %s (streamed, not handled)\n", name);
        return 1;
    }
    if (type != 1 && type != 3) return 0;
    if (kind == 2) {                                  /* SCSP sound bank (.SND), 0x06004C52 -> sfx.c */
        size_t sl = 0;
        uint8_t *sb = read_file(name, &sl);
        int ok = sb && Sfx_LoadBankData(sb, sl, name);
        free(sb);
        s_sndLoaded |= ok;
        if (ok) snprintf(s_sndName, sizeof s_sndName, "%s", name);
        printf("[res] sound bank %s %s\n", name, ok ? "loaded" : sb ? "bad" : "missing");
        return ok;
    }
    size_t len = 0, n = 0;
    uint8_t *buf = NULL;
    int predecoded = 0;
    size_t nl = strlen(name);
    if (kind == 1 && nl > 4 && strcmp(name + nl - 4, ".PPB") == 0) {   /* PC: X.dec.bin = decompressed X.PPB */
        char alt[32];
        snprintf(alt, sizeof alt, "%.*s.dec.bin", (int)(nl - 4), name);
        buf = read_file(alt, &len);
        predecoded = buf != NULL;
    }
    if (!buf) buf = read_file(name, &len);
    if (!buf) { printf("[res] missing %s/%s\n", s_assetDir, name); return 0; }
    uint32_t max = region_end(dest) - dest;
    if (kind == 1 && !predecoded) {
        n = Lz_Decompress(buf, len, Ram_Ptr(dest), max);
        if (!n) printf("[res] %s: bad LZ data\n", name);
    } else {
        n = len < max ? len : max;
        memcpy(Ram_Ptr(dest), buf, n);
    }
    free(buf);
    if ((dest & 0xFFF80000u) == VDP1_VRAM_BASE) Render_InvalidateVram(dest, n);
    if (dest == OVERLAY_BASE && n) {
        s_overlay = strncmp(name, "DIRECTOR", 8) == 0 ? OVL_DIRECTOR
                  : strncmp(name, "SHINOBI", 7) == 0 ? OVL_SHINOBI : OVL_NONE;
        printf("[res] overlay %s resident at 0x06040000 (%zu bytes)\n", name, n);
    }
    return n != 0;
}

/* 0x06004A1C */
int Res_Request(int type, int kind, uint32_t a, uint32_t b, uint32_t dest, uint32_t name)
{
    write_req(RES_REQ, type, kind, a, b, dest, name);
    return execute(RES_REQ);
}

int Res_RequestFile(int type, int kind, uint32_t dest, const char *name)
{
    write_req(RES_REQ, type, kind, 0, 0xFFFFFFFFu, dest, 0);
    snprintf((char *)Ram_Ptr(RES_REQ + 0x10), 13, "%s", name);
    return execute(RES_REQ);
}

/* 0x060049C4: returns the slot index, -1 when the 8 slots are used. */
int Res_QueueRequest(int type, int kind, uint32_t a, uint32_t b, uint32_t dest, uint32_t name)
{
    int16_t n = RS16(RES_QCOUNT);
    if (n >= 8) return -1;
    write_req(RES_QUEUE + (uint32_t)n * 0x20u, type, kind, a, b, dest, name);
    W16(RES_QCOUNT, (uint32_t)(n + 1));
    return n;
}

/* 0x0600446A: run the queued requests (top of the main loop, before the overlay entry). */
void Res_ProcessQueue(void)
{
    int16_t n = RS16(RES_QCOUNT);
    for (int16_t i = 0; i < n; i++) {
        uint32_t e = RES_QUEUE + (uint32_t)i * 0x20u;
        int t = RS16(e);
        if (t == 1 || t == 2 || t == 3) execute(e);
    }
    if (n) W16(RES_QCOUNT, 0);
}

/* 0x06004E6A / 0x06004EAC: queue the other overlay (retry until a slot is free).
 * File names: A.BIN strings 0x0600EDCC "SHINOBI.PPB", 0x0600EDD8 "DIRECTOR.PPB". */
void Load_ShinobiPPB(void)  { while (Res_QueueRequest(1, 1, 0, 0xFFFFFFFFu, OVERLAY_BASE, 0x0600EDCCu) < 0) Res_ProcessQueue(); }
void Load_DirectorPPB(void) { while (Res_QueueRequest(1, 1, 0, 0xFFFFFFFFu, OVERLAY_BASE, 0x0600EDD8u) < 0) Res_ProcessQueue(); }

/* 0x06004E1C: decompress the LZ block posted by 0x06004E60 (flag 0x0601ABC8 = 1,
 * source 0x0601ABCC) to 0x06040000. */
void Res_PendingDecompress(void)
{
    if (!R16(0x0601ABC8u)) return;
    if (R16(0x0601ABC8u) == 1) {
        uint32_t src = R32(0x0601ABCCu);
        Lz_Decompress(Ram_Ptr(src), region_end(src) - src, Ram_Ptr(OVERLAY_BASE), HWRAM_BASE + HWRAM_SIZE - OVERLAY_BASE);
    }
    W16(0x0601ABC8u, 0);
}

/* ---- sound banks (sfx.c) --------------------------------------------------------------- */

/* SHINOBI 0x0606BB0C (called by Game_LoadStage 0x0606B6EC at 0x0606B70A): bank index =
 * s16 table 0x060DAFE4[stage] (0,0,1,1,...,8,9: 9-2 = KAZUMA.SND), loaded with
 * Res_Request(1, 2, 0, -1, 0, name table 0x060DAFBC[bank]) unless it is already the
 * loaded one (0x060F4D30); then the current stage tune 0x060F4D32 = -1. Needs SHINOBI
 * resident (the tables are overlay data). */
void Res_LoadStageSoundBank(int stage)
{
    int16_t bank;
    uint32_t name;
    if (Res_OverlayResident() != OVL_SHINOBI || stage < 0 || stage > 17) return;
    bank = RS16(0x060DAFE4u + 2u * (uint32_t)stage);
    if (bank < 0 || bank > 9) return;
    name = R32(0x060DAFBCu + 4u * (uint32_t)bank);
    /* PC: compare with the bank really resident (DIRECTOR loads banks too) */
    if (s_sndLoaded && strncmp(s_sndName, (const char *)Ram_Ptr(name), 12) == 0) return;
    W16(0x060F4D30u, (uint32_t)bank);
    Res_Request(1, 2, 0, 0xFFFFFFFFu, 0, name);
    W16(0x060F4D32u, 0xFFFFu);
}

/* PC only: SE requested before any .SND was loaded (title screen after power-on: the
 * Saturn's sound RAM would be empty or hold a previous bank). Load STAGE7.SND (DIRECTOR's
 * bank 0, also loaded by the options screen): seq/tone banks 0 and 1 (system SE) are
 * identical in every .SND. */
void Res_EnsureSoundBank(void)
{
    static int tried;
    if (s_sndLoaded || tried) return;
    tried = 1;
    Res_RequestFile(1, 2, 0, "STAGE7.SND");
}
