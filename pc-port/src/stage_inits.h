#ifndef STAGE_INITS_H
#define STAGE_INITS_H
/* Per-stage VDP2/layer set-up: the 18 handlers of SHINOBI's table 0x060A8B0C (called at the end
 * of the common stage init 0x0604861C, one per stage index 0x0602526A) and the initial layer
 * fill 0x060471D4 (+ NBG3 status-bar map 0x06047244).
 *
 * Stage control block C = 0x060EA618:
 *   +0x00/+0x04/+0x08/+0x0C s32 world bounds left/top/right/bottom (16.16, world = layer + 1024 px)
 *   +0x10..+0x16 player screen clamp (top, -, right, left; 0x7FFF = no top clamp)
 *   +0x18/+0x1A/+0x1C s16 R/G/B tint of the player palette (Pal_SetFaded, night stages)
 *   +0x1E s16 (common init 0xFFFF, index 13 sets 0)
 *   +0x20 u16 flags: 1/2/4/8 camera locks, 0x10/0x20/0x40 layer 0/1/2 is static (not re-streamed
 *         into VRAM each frame by 0x06046E54), 0x80 screen shake, 0x100/0x200 no look up/down
 *   +0x22..+0x28 VDP2 priority of sprite registers S0..S3, +0x2A..+0x30 of NBG0..NBG3 (0 = hidden)
 *   +0x32 s16 object collision layer (G_COLL_LAYER, -1 = none)    +0x34 s16 (always 0xFFFF)
 * Layer descriptors D = 0x060EA588 (+0x30 each, NBG0..NBG2; NBG3 = +0x90, only +0x24/+0x2C used):
 *   +0x00/+0x04 position, +0x08/+0x0C previous, +0x10/+0x14 offset, +0x18/+0x1A parallax rate
 *   (0x100 = 1), +0x1C pages, +0x20 page layout, +0x24 VDP2 pattern-name page, +0x28 collision attrs,
 *   +0x2C character base (in 0x20-byte character units). */
#include <stdint.h>

#define STAGE_CTRL_ADDR  0x060EA618u
#define NBG_CFG_ADDR     0x06020D08u    /* SBL SclConfig[4] (0x4C each): +0 dispenbl, +1 charsize,
                                           +2 pnamesize, +3 platesize, +5 coltype, +12 plate_addr[] */
#define HUD_MAP_BUF      0x060EA658u    /* 0x06047244: decompressed NBG3 map (0x800 u16) */
#define NBG3_PN_ADDR     0x25E6C000u

#define STAGE_INIT_COUNT 18
typedef void (*StageInitFn)(void);
extern const StageInitFn g_StageInits[STAGE_INIT_COUNT];   /* 0x060A8B0C */

void StageInit_Run(int stageIndex);       /* jsr @(0x060A8B0C[stage]) at the end of 0x0604861C */
void Stage_InitLayers(void);              /* 0x060471D4: layer positions + VRAM fill + NBG3 map */
void Layer_FillVram(uint32_t desc);       /* 0x06046EBC: 22x18 tiles around the layer position */
void Layer_PutTile(int32_t x, int32_t y, uint32_t desc);   /* 0x06046B14 */
void Hud_InitNbg3Map(void);               /* 0x06047244 */
#endif
