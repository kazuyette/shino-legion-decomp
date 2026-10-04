#ifndef STAGE_HOOKS_H
#define STAGE_HOOKS_H
/* Per-stage frame hooks and the layer animation system of SHINOBI.
 *
 * Stage load (game mode 5, 0x0606B7D2): StageHooks_Init 0x0606C618 = LayerAnim_Reset 0x0606F408,
 * then the per-stage init of table 0x060B1114 (18 entries, 0 = none) and 0x0601C498 |= 0x4000.
 * Stage frame (game mode 6, 0x0606B9B8): Stage_FrameHooks 0x0606BCD0 = per-stage handler of
 * table 0x060B10CC (18 entries, index 3 = 0), LayerAnim_Update 0x0606F6AC, LayerAnim_Commit
 * 0x0606EF9C; then Ninjutsu_Update 0x06063554.
 *
 * Per-stage handler variables: 0x060F4D38/3A/3C (s16, cleared at stage load) and the 10 longs at
 * 0x060F4D40 (meaning depends on the stage; cleared by some inits).
 *
 * Layer animation block LA = 0x060F4D80:
 *   +0x00/+0x02 u16 wave mode of NBG0/NBG1 (1 static amplitude, 2 growing, 4 shrinking; bit 0
 *         = done; 0x0606F67E sets 16), +0x06 u16 (0x0606F664), +0x08 u16 bit 0 = line tables
 *         dirty, +0x0A u16 bit 0 = vertical cell scroll tables dirty
 *   +0x0C u16 flags: 0x0008 wave on NBG0, 0x0800 wave on NBG1, 0x0010 line list 0 -> NBG0,
 *         0x0020 line list 1 -> NBG1, 0x4000 line list 2 -> NBG2 (HBlank table 0x0601ABD8),
 *         0x0001 / 0x0100 vertical cell scroll lists; (flags & 0xF7C7) = VDP2 SCRCTL
 *   +0x0E/+0x10 s16 wave phase, +0x16/+0x18 wave period (frames), +0x1C/+0x1E timers,
 *   +0x24/+0x28 wave length (16.16: 256 / (v>>16) angle units per line), +0x30/+0x34 amplitude,
 *   +0x3C/+0x40, +0x48/+0x4C per-step increments, +0x5A/+0x5C steps left,
 *   +0x60/+0x64 VRAM address of the NBG0/NBG1 line scroll tables, +0x68 of the cell scroll table,
 *   +0x6C line table NBG0 (0x100 x {s32, s32 X}), +0x86C NBG1, +0x106C/+0x110C cell scroll
 *   NBG0/NBG1 (40 x s32), +0x11AC line lists 0/1/2 (0x28 x {s16 type, s16 lines, s32 value}:
 *   type 0x4000 value, 0x2000 FixMul(camera X - 1024, value), 0x8000 end), +0x156C cell
 *   scroll lists 0/1 (same format, columns; 0x2000 uses camera Y).
 *
 * HBlank line table 0x0601ABD8 (A.BIN; flags 0x0601C498: 0x8000 = updated this frame, 0x2000 =
 * DMA NBG2 X scroll, 0x4000 = DMA colour offset A): 0x108 lines x {s32 NBG2 X (16.16), s32 R,
 * G, B}; at VBlank-IN 0x0600527C converts it to words at +0x1080 (8 bytes/line: SCXIN2, COAR =
 * fade + R, COAG, COAB clamped to -256..255) and clears it; the HBlank DMA of VBlank-OUT
 * 0x060051C8 writes them to the VDP2 every line. */
#include <stdint.h>

#define LANIM       0x060F4D80u
#define LINE_HBL    0x0601ABD8u   /* 0x108 x 16 bytes */
#define LINE_HBL_W  0x0601CC58u   /* converted words, 0x108 x 8 bytes */
#define LINE_FLAGS  0x0601C498u

void StageHooks_Init(void);          /* 0x0606C618 */
void Stage_FrameHooks(void);         /* 0x0606BCD0 */

void LayerAnim_Reset(void);          /* 0x0606F408 */
void LayerAnim_Update(void);         /* 0x0606F6AC */
void LayerAnim_Commit(void);         /* 0x0606EF9C */
/* 0x0606F5C4: sel bit 0 = NBG0, bit 1 = NBG1 */
void LayerAnim_SetWave(int sel, int mode, int period, int steps, int32_t a5, int32_t a6);
void LineList_Set(int list, int idx, uint16_t type, int16_t lines, int32_t value);   /* 0x0606FDFC */
void CellList_Set(int list, int idx, uint16_t type, int16_t cols, int32_t value);    /* 0x0606FE38 */
/* 0x0606FA48: adds a sine (cos if flags & 1) wave to the line table of NBG0/NBG1/NBG2 */
void LineWave_Apply(int layer, int phase, int start, int lines, int32_t len, int32_t amp, int flags);

void Ninjutsu_Init(void);            /* 0x06063538 (ninjutsu.c) */
int  Ninjutsu_Update(void);          /* 0x06063554 (ninjutsu.c) */
void Layer_FillPage(uint32_t desc);  /* 0x0606F8D8: write a layer's whole 32x32-tile page */

/* A.BIN VBlank side of the HBlank line table (0x06004FD4 -> 0x0600527C, 0x060051C8).
 * Call once per frame after the game frame and the fader (Boot_Frame). */
void LineTable_VBlank(void);
/* DMA tables live for the frame being displayed: 0, or 0x2000 (NBG2 X) | 0x4000 (COA). */
int  LineTable_DmaActive(void);
#endif
