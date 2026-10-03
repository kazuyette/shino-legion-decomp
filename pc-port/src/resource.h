#ifndef RESOURCE_H
#define RESOURCE_H
#include <stdint.h>

/* File loading of A.BIN (0x06004418-0x06004EE8), ported for a PC file tree.
 *
 * On the Saturn every disc read goes through Res_Request (0x06004A1C, synchronous) or the
 * 8-slot queue (Res_QueueRequest 0x060049C4 -> slots at 0x060189D4, count 0x06018AD4,
 * processed by Res_ProcessQueue 0x0600446A at the top of every main-loop frame). A request
 * is 32 bytes: +0 u16 type (1 load, 2 open only, 3 async load), +2 u16 kind (0 raw copy,
 * 1 word-LZ decompressed while reading, 2 SCSP sound bank), +4/+8 two u32 the GFS layer
 * uses (always 0 / -1 here), +0xC u32 destination, +0x10 file name (12 chars max + 0,
 * copied by 0x0600498A). The requests are kept in emulated RAM at those addresses.
 *
 * PC: files are read from the asset directory (Res_SetAssetDir, default "assets").
 * "X.PPB" requested with kind 1 is taken from "X.dec.bin" (already decompressed) when that
 * file exists, else "X.PPB" is decompressed. Kind 2 (.SND banks) and type 2 (CD-DA tracks
 * TITLE.PCM / ENDING.PCM) are only logged for now (work packages D8/D9).
 *
 * Overlays: DIRECTOR.PPB and SHINOBI.PPB both load at 0x06040000 (Load_DirectorPPB /
 * Load_ShinobiPPB queue the request; it is executed on the next frame before the overlay
 * entry is called). Res_OverlayResident() tells which one is in RAM. */

#define RES_REQ       0x06018AD8u   /* the synchronous request */
#define RES_QUEUE     0x060189D4u   /* 8 x 0x20 queued requests */
#define RES_QCOUNT    0x06018AD4u   /* s16 number of queued requests */
#define OVERLAY_BASE  0x06040000u

enum { OVL_NONE = 0, OVL_DIRECTOR = 1, OVL_SHINOBI = 2 };

void        Res_SetAssetDir(const char *dir);
const char *Res_GetAssetDir(void);

void Res_Init(void);                                         /* 0x06004418 (queue part) */
/* 0x06004A1C: name = emulated address of a 0-terminated file name. Returns 1 if done. */
int  Res_Request(int type, int kind, uint32_t a, uint32_t b, uint32_t dest, uint32_t name);
/* Same, with a host string (PC callers). */
int  Res_RequestFile(int type, int kind, uint32_t dest, const char *name);
int  Res_QueueRequest(int type, int kind, uint32_t a, uint32_t b, uint32_t dest, uint32_t name); /* 0x060049C4 */
void Res_ProcessQueue(void);                                 /* 0x0600446A */
void Res_CloseIfOpen(void);                                  /* 0x060048D8: nothing stays open on PC */
void Load_ShinobiPPB(void);                                  /* 0x06004E6A */
void Load_DirectorPPB(void);                                 /* 0x06004EAC */
void Res_PendingDecompress(void);                            /* 0x06004E1C */

int  Res_OverlayResident(void);                              /* OVL_* */

#endif
