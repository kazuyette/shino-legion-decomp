/* Per-stage layer/VDP2 set-up (SHINOBI table 0x060A8B0C) and the initial layer fill 0x060471D4.
 * See stage_inits.h for the control block / descriptor layout.
 *
 * Every handler writes, in this order: C+0x20 flags, VDP2 priorities (NBG0..3 then S0..S3), the 3
 * page layouts, the layer offsets, the world bounds, the parallax rates, C+0x32 (collision layer)
 * and C+0x34; a few then touch the SBL SclConfig of one NBG (plane size / plate addresses, followed
 * by SCL_SetConfig 0x06029B40, not emulated: the PC renderer draws the layers straight from the
 * MAP pages), the CRAM offsets (0x06005EAC), the special-priority mode (SFPRMD), the player
 * palette tint or the camera's target-screen ranges.
 *
 * Index  game  file      handler
 *   0    1-1  STAGE7_1  0x06047F90  night bamboo forest; layer 2 static; tint (-56,-48,+16);
 *                                   left bound moves with the checkpoint
 *   1    1-2  STAGE7_2  0x060480E6  boss room: layer 1 static, NBG2 hidden
 *   2    2-1  STAGE2_1  0x0604757C  vertical stage (bounds from the origin)
 *   3    2-2  STAGE2_2  0x0604765C  boss room, collision on layer 1
 *   4    3-1  STAGE3_1  0x06047748  layer 2 static on a 2x2 plane, NBG2 uses palette 1; tint
 *   5    3-2  STAGE3_2  0x0604789A  boss room, collision on layer 1, tint
 *   6    4-1  STAGE4_1  0x0604797C  camera target ranges changed
 *   7    4-2  STAGE4_2  0x06047A68  boss: only NBG1 shown, no map collision, no scrolling
 *   8    5-1  STAGE5_1  0x06047B4C  very long stage; no look-up; camera target ranges
 *   9    5-2  STAGE5_2  0x06047C64  layer 1 static (2x2 plane); checkpoint 1 changes the bounds
 *  10    6-1  STAGE6_1  0x06047D8C  layer 0 static backdrop (2x1 plane), NBG1+NBG2 share palette 1,
 *                                   level on layer 1
 *  11    6-2  STAGE6_2  0x06047EA4  boss room: layers 0 and 1 static, NBG2 hidden
 *  12    7-1  STAGE1_1  0x060473B4  harbour; layer 2 static
 *  13    7-2  STAGE1_2  0x0604749C  boss room, NBG2 hidden, C+0x1E = 0
 *  14    8-1  STAGE8_1  0x060481CC  layer 1 static; SFPRMD NBG1/NBG2 per-screen
 *  15    8-2  STAGE8_2  0x06048308  boss room, collision on layer 2
 *  16    9-1  STAGE9_1  0x060483E0  layer 1 static, NBG2 hidden; camera target ranges
 *  17    9-2  STAGE9_2  0x060484D4  last boss: layer 2 static (2x2 plane), SFPRMD cleared */
#include "stage_inits.h"
#include "stage.h"
#include "camera.h"
#include "ram.h"
#include "sprite.h"
#include "collision.h"

#define C STAGE_CTRL_ADDR
#define D 0x060EA588u
#define NBG0_CFG (NBG_CFG_ADDR + 0x00u)
#define NBG1_CFG (NBG_CFG_ADDR + 0x4Cu)
#define NBG2_CFG (NBG_CFG_ADDR + 0x98u)
#define CHECKPOINT R16(0x060EC658u)
#define SFPRMD (VDP2_SHADOW_ADDR + 0x0A)   /* special priority mode, NBGn in bits 2n+1..2n */

static void or_flags(uint16_t f) { W16(C + 0x20, R16(C + 0x20) | f); }

/* C+0x2A..+0x30 = NBG0..NBG3, C+0x22..+0x28 = sprite registers S0..S3 (copied to the VDP2
 * priority registers by 0x060472D2) */
static void set_priorities(int n0, int n1, int n2, int n3, int s0, int s1, int s2, int s3)
{
    W16(C + 0x2A, (uint32_t)n0); W16(C + 0x2C, (uint32_t)n1); W16(C + 0x2E, (uint32_t)n2); W16(C + 0x30, (uint32_t)n3);
    W16(C + 0x22, (uint32_t)s0); W16(C + 0x24, (uint32_t)s1); W16(C + 0x26, (uint32_t)s2); W16(C + 0x28, (uint32_t)s3);
}

/* page layouts {u8 w, u8 h, page ids} in SHINOBI's data */
static void set_layouts(uint32_t l0, uint32_t l1, uint32_t l2)
{
    W32(D + 0x20, l0); W32(D + 0x50, l1); W32(D + 0x80, l2);
}

/* layer offsets (16.16): layer px = FixMul(camera - 1024, rate) + offset */
static void set_offsets(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2)
{
    W32(D + 0x10, x0); W32(D + 0x14, y0);
    W32(D + 0x40, x1); W32(D + 0x44, y1);
    W32(D + 0x70, x2); W32(D + 0x74, y2);
}

static void set_bounds(uint32_t left, uint32_t top, uint32_t right, uint32_t bottom)
{
    W32(C + 0x00, left); W32(C + 0x04, top); W32(C + 0x08, right); W32(C + 0x0C, bottom);
}

/* parallax rates (0x100 = follows the camera) */
static void set_rates(int x0, int y0, int x1, int y1, int x2, int y2)
{
    W16(D + 0x18, (uint32_t)x0); W16(D + 0x1A, (uint32_t)y0);
    W16(D + 0x48, (uint32_t)x1); W16(D + 0x4A, (uint32_t)y1);
    W16(D + 0x78, (uint32_t)x2); W16(D + 0x7A, (uint32_t)y2);
}

static void set_coll_layer(int layer)
{
    W16(C + 0x32, (uint32_t)layer);
    W16(C + 0x34, 0xFFFF);
}

/* Player palette tint (read by Player_UpdatePalette / Pal_SetFaded) */
static void set_tint(int r, int g, int b)
{
    W16(C + 0x18, (uint32_t)r); W16(C + 0x1A, (uint32_t)g); W16(C + 0x1C, (uint32_t)b);
}

/* SCL_SetConfig 0x06029B40(screen, cfg): pushes an SclConfig to the VDP2 registers (CHCTL,
 * PNC, PLSZ, MPxx). Not emulated (the renderer reads the layer setup from the descriptors). */
static void scl_set_config(int screen, uint32_t cfg) { (void)screen; (void)cfg; }

/* 0x06047F90: game stage 1-1 (STAGE7_1) */
static void init_00(void)
{
    or_flags(0x40);
    W16(C + 0x10, 0x7FFF);
    set_priorities(4, 4, 2, 7, 2, 4, 5, 7);
    set_layouts(0x060CEDB0u, 0x060CEDC6u, 0x060CEDD6u);
    set_offsets(0x04000000u, 0x04000000u, 0x04000000u, 0x04600000u, 0x04000000u, 0x05100000u);
    set_bounds(0x04000000u, 0x04800000u, 0x2C000000u, 0x06000000u);
    set_rates(0x100, 0x100, 0xA0, 0xA0, 0, 0x10);
    set_coll_layer(0);
    Ram_Ptr(NBG2_CFG)[3] = 1;                                 /* plane 2x1 pages */
    W32(NBG2_CFG + 16, 0x25E69000u); W32(NBG2_CFG + 20, 0x25E69000u); W32(NBG2_CFG + 24, 0x25E69000u);
    scl_set_config(0x10, NBG2_CFG);
    set_tint(-56, -48, 16);
    if (CHECKPOINT == 1) W32(C + 0x00, 0x0BC00000u);         /* left bound follows the checkpoint */
    if (CHECKPOINT == 2) W32(C + 0x00, 0x1CD00000u);
}

/* 0x060480E6: 1-2 (STAGE7_2) */
static void init_01(void)
{
    or_flags(0x20);
    set_priorities(4, 2, 0, 7, 2, 4, 6, 7);
    set_layouts(0x060CEDDAu, 0x060CEDDEu, 0x060CEDDEu);
    set_offsets(0x04000000u, 0x04C00000u, 0x04000000u, 0x05000000u, 0x04000000u, 0x04000000u);
    set_bounds(0x04000000u, 0x04000000u, 0x05D00000u, 0x05300000u);
    set_rates(0x100, 0x100, 0, 0, 0, 0);
    set_coll_layer(0);
}

/* 0x0604757C: 2-1 (STAGE2_1) */
static void init_02(void)
{
    W16(C + 0x20, 0);
    set_priorities(4, 2, 2, 7, 3, 4, 6, 7);
    set_layouts(0x060CEB36u, 0x060CEB4Au, 0x060CEB5Eu);
    set_offsets(0x04000000u, 0x04000000u, 0x04000000u, 0x04000000u, 0x04000000u, 0x04800000u);
    W32(C + 0x00, 0x04000000u);
    W32(C + 0x04, 0x04E00000u);
    W32(C + 0x08, R32(C + 0x00) + 0x03C00000u);
    W32(C + 0x0C, R32(C + 0x04) + 0x11200000u);
    set_rates(0x100, 0x100, 0x90, 0x100, 0x38, 6);
    set_coll_layer(0);
}

/* 0x0604765C: 2-2 (STAGE2_2) */
static void init_03(void)
{
    W16(C + 0x20, 0);
    set_priorities(6, 4, 2, 7, 3, 5, 6, 7);
    set_layouts(0x060CEB62u, 0x060CEB66u, 0x060CEB6Au);
    set_offsets(0x04000000u, 0x04E80000u, 0x04000000u, 0x04E00000u, 0x04000000u, 0x05000000u);
    set_bounds(0x04000000u, 0x04100000u, 0x05900000u, 0x05080000u);
    set_rates(0x100, 0x100, 0x100, 0x100, 0, 0);
    set_coll_layer(1);
    W16(C + 0x10, 0x7FFF);
}

/* 0x06047748: 3-1 (STAGE3_1) */
static void init_04(void)
{
    or_flags(0x40);
    set_priorities(2, 2, 4, 7, 3, 2, 6, 7);
    set_layouts(0x060CEB6Eu, 0x060CEB8Eu, 0x060CEB96u);
    set_offsets(0x04000000u, 0x04E00000u, 0x04000000u, 0x04A00000u, 0x08000000u, 0x05400000u);
    set_bounds(0x04000000u, 0x04000000u, 0x18000000u, 0x07100000u);
    set_rates(0x100, 0x100, 0x99, 0x3A, 0, 0);
    set_coll_layer(0);
    Ram_Ptr(NBG2_CFG)[3] = 3;                                 /* plane 2x2 pages */
    W32(NBG2_CFG + 12, 0x25E68000u); W32(NBG2_CFG + 16, 0x25E70000u);
    scl_set_config(0x10, NBG2_CFG);
    set_tint(-56, -56, -40);
    Vdp2_SetColorRamOffsets(-1, -1, 1, -1);                   /* NBG2 uses the layer-1 palette */
    if (CHECKPOINT == 1) W32(C + 0x00, 0x0CC00000u);
    W16(C + 0x10, 0x7FFF);
}

/* 0x0604789A: 3-2 (STAGE3_2) */
static void init_05(void)
{
    W16(C + 0x20, 0);
    set_priorities(6, 4, 6, 7, 2, 4, 6, 7);
    set_layouts(0x060CEBA0u, 0x060CEBA4u, 0x060CEBA8u);
    set_offsets(0x04000000u, 0x04000000u, 0x04000000u, 0x04000000u, 0x04000000u, 0x04000000u);
    set_bounds(0x04000000u, 0x04400000u, 0x05E00000u, 0x05F80000u);
    set_rates(0x100, 0x100, 0x100, 0x100, 0x100, 0x100);
    set_coll_layer(1);
    set_tint(-56, -56, -56);
}

/* 0x0604797C: 4-1 (STAGE4_1) */
static void init_06(void)
{
    W16(C + 0x20, 0);
    set_priorities(6, 4, 6, 7, 2, 4, 6, 7);
    set_layouts(0x060CEBACu, 0x060CEBC2u, 0x060CEBD8u);
    set_offsets(0x04000000u, 0x04000000u, 0x04000000u, 0x04000000u, 0x04000000u, 0x04000000u);
    set_bounds(0x04000000u, 0x04900000u, 0x18000000u, 0x08000000u);
    set_rates(0x100, 0x100, 0x100, 0x100, 0x100, 0x100);
    set_coll_layer(0);
    W32(0x060EA55Cu, 0x00A00000u);                            /* player target screen Y range */
    W32(0x060EA560u, 0x00D00000u);
}

/* 0x06047A68: 4-2 (STAGE4_2) */
static void init_07(void)
{
    or_flags(0x20);
    set_priorities(0, 3, 0, 7, 3, 4, 6, 7);
    set_layouts(0x060CEBEEu, 0x060CEBF2u, 0x060CEBF6u);
    set_offsets(0x04000000u, 0x04000000u, 0x04000000u, 0x05200000u, 0x04000000u, 0x04400000u);
    set_bounds(0x04000000u, 0x04000000u, 0x05410000u, 0x04E10000u);
    set_rates(0, 0, 0, 0, 0, 0);
    set_coll_layer(-1);
    W16(C + 0x10, 0x7FFF);
}

/* 0x06047B4C: 5-1 (STAGE5_1) */
static void init_08(void)
{
    or_flags(0x100);
    set_priorities(4, 2, 2, 7, 2, 4, 6, 7);
    set_layouts(0x060CEBFAu, 0x060CEC74u, 0x060CECAAu);
    set_offsets(0x04000000u, 0x04000000u, 0x04000000u, 0x04800000u, 0x04000000u, 0x04F00000u);
    set_bounds(0x04000000u, 0x06000000u, 0x7C000000u, 0x08000000u);
    set_rates(0x100, 0x100, 0x80, 0x33, 0x20, 0x10);
    set_coll_layer(0);
    W32(0x060EA55Cu, 0x00A00000u);                            /* player target screen Y / X ranges */
    W32(0x060EA560u, 0x00A80000u);
    W32(0x060EA568u, 0x00C00000u);
    W32(0x060EA564u, 0x006E0000u);
}

/* 0x06047C64: 5-2 (STAGE5_2) */
static void init_09(void)
{
    or_flags(0x20);
    set_priorities(4, 2, 4, 7, 4, 4, 6, 7);
    set_layouts(0x060CECD4u, 0x060CED02u, 0x060CED08u);
    set_offsets(0x04C00000u, 0x04600000u, 0x04C00000u, 0x04000000u, 0x04A00000u, 0x04F00000u);
    set_bounds(0x04000000u, 0x04000000u, 0x06810000u, 0x04E10000u);
    if (CHECKPOINT == 1) {
        W32(C + 0x0C, 0x2FA00000u);
        W32(C + 0x00, 0x053F0000u);
    }
    set_rates(0x100, 0x100, 0x100, 0, 0x100, 0x100);
    set_coll_layer(0);
    Ram_Ptr(NBG1_CFG)[3] = 3;                                 /* plane 2x2 pages (no SCL_SetConfig) */
}

/* 0x06047D8C: 6-1 (STAGE6_1) */
static void init_10(void)
{
    or_flags(0x10);
    Vdp2_SetColorRamOffsets(0, 1, 1, -1);                     /* NBG1 and NBG2 share palette 1 */
    set_priorities(2, 4, 6, 7, 3, 4, 6, 7);
    set_layouts(0x060CED0Cu, 0x060CED10u, 0x060CED5Au);
    set_offsets(0x04000000u, 0x04600000u, 0x04000000u, 0x04000000u, 0x04000000u, 0x04000000u);
    set_bounds(0x04000000u, 0x04000000u, 0x28000000u, 0x0A000000u);
    set_rates(0, 0x24, 0x100, 0x100, 0x100, 0x100);
    set_coll_layer(1);
    Ram_Ptr(NBG0_CFG)[3] = 1;                                 /* plane 2x1 pages (no SCL_SetConfig) */
    W32(0x060EA55Cu, 0x00900000u);
    W32(0x060EA560u, 0x00A00000u);
}

/* 0x06047EA4: 6-2 (STAGE6_2) */
static void init_11(void)
{
    or_flags(0x10);
    or_flags(0x20);
    set_priorities(6, 4, 0, 7, 2, 4, 6, 7);
    set_layouts(0x060CEB2Au, 0x060CEB2Eu, 0x060CEB32u);
    set_offsets(0x04000000u, 0x04000000u, 0x04000000u, 0x05000000u, 0x04000000u, 0x04000000u);
    set_bounds(0x04000000u, 0x04CA0000u, 0x058A0000u, 0x06000000u);
    set_rates(0x100, 0x100, 0, 0x24, 0, 0);
    set_coll_layer(0);
}

/* 0x060473B4: 7-1 (STAGE1_1) */
static void init_12(void)
{
    or_flags(0x40);
    set_priorities(4, 3, 2, 7, 3, 4, 5, 7);
    set_layouts(0x060CEAD8u, 0x060CEB0Eu, 0x060CEB20u);
    set_offsets(0x04000000u, 0x04000000u, 0x04000000u, 0x04580000u, 0x04000000u, 0x04F00000u);
    W32(C + 0x00, 0x04000000u);
    W32(C + 0x04, 0x04000000u);
    W32(C + 0x08, R32(C + 0x00) + 0x34000000u);
    W32(C + 0x0C, R32(C + 0x04) + 0x01F00000u);
    set_rates(0x100, 0x100, 0x77, 0xB0, 0x18, 0x19);
    set_coll_layer(0);
}

/* 0x0604749C: 7-2 (STAGE1_2) */
static void init_13(void)
{
    W16(C + 0x20, 0);
    set_priorities(4, 2, 0, 7, 3, 4, 5, 7);
    set_layouts(0x060CEB2Au, 0x060CEB2Eu, 0x060CEB32u);
    set_offsets(0x04000000u, 0x04000000u, 0x04000000u, 0x04000000u, 0x04000000u, 0x04000000u);
    W32(C + 0x00, 0x04100000u);
    W32(C + 0x04, 0x04E00000u);
    W32(C + 0x08, R32(C + 0x00) + 0x01D00000u);
    W32(C + 0x0C, R32(C + 0x04) + 0x011F0000u);
    set_rates(0x100, 0x100, 0x4C, 0x100, 0, 0);
    set_coll_layer(0);
    W16(C + 0x1E, 0);
}

/* 0x060481CC: 8-1 (STAGE8_1). The SFPRMD writes are preceded by the SBL register-dirty
 * macro 0x06009254 (PC: the shadow is read directly). */
static void init_14(void)
{
    W16(C + 0x20, 0);
    or_flags(0x20);
    W16(C + 0x10, 0x7FFF);
    set_priorities(4, 1, 2, 7, 2, 4, 6, 7);
    set_layouts(0x060CEDE2u, 0x060CEE5Cu, 0x060CEE60u);
    set_offsets(0x04000000u, 0x04000000u, 0x04000000u, 0x04B00000u, 0x04000000u, 0x04500000u);
    set_bounds(0x04000000u, 0x04000000u, 0x2BF80000u, 0x10000000u);
    set_rates(0x100, 0x100, 0, 0x40, 0xA0, 0xA0);
    set_coll_layer(0);
    W16(SFPRMD, R16(SFPRMD) & 0xFFF3);                        /* NBG1: special priority per screen */
    W16(SFPRMD, R16(SFPRMD) & 0xFFCF);                        /* NBG2 */
}

/* 0x06048308: 8-2 (STAGE8_2) */
static void init_15(void)
{
    W16(C + 0x20, 0);
    set_priorities(6, 2, 4, 7, 2, 4, 6, 7);
    set_layouts(0x060CEE76u, 0x060CEE7Au, 0x060CEE7Eu);
    set_offsets(0x04000000u, 0x04B00000u, 0x04000000u, 0x04F00000u, 0x04000000u, 0x04B00000u);
    set_bounds(0x04000000u, 0x04000000u, 0x05900000u, 0x05500000u);
    set_rates(0x100, 0x100, 0x6D, 0x66, 0x100, 0x100);
    set_coll_layer(2);
}

/* 0x060483E0: 9-1 (STAGE9_1) */
static void init_16(void)
{
    or_flags(0x20);
    set_priorities(4, 2, 0, 7, 2, 4, 6, 7);
    set_layouts(0x060CEE82u, 0x060CEECCu, 0x060CEED0u);
    set_offsets(0x04000000u, 0x04000000u, 0x04000000u, 0x04E00000u, 0x04000000u, 0x04E00000u);
    set_bounds(0x04000000u, 0x04000000u, 0x34000000u, 0x08000000u);
    set_rates(0x100, 0x100, 0x60, 0x100, 0x100, 0x100);
    set_coll_layer(0);
    W32(0x060EA55Cu, 0x00A80000u);
    W32(0x060EA560u, 0x00C00000u);
}

/* 0x060484D4: 9-2 (STAGE9_2) */
static void init_17(void)
{
    or_flags(0x40);
    set_priorities(4, 2, 2, 7, 3, 4, 6, 7);
    set_layouts(0x060CEED4u, 0x060CEED8u, 0x060CEEDCu);
    set_offsets(0x04000000u, 0x05200000u, 0x04000000u, 0x05200000u, 0x04000000u, 0x05200000u);
    set_bounds(0x04000000u, 0x04000000u, 0x05900000u, 0x04E10000u);
    set_rates(0x100, 0x100, 0, 0, 0, 0);
    set_coll_layer(0);
    Ram_Ptr(NBG2_CFG)[3] = 3;                                 /* plane 2x2 pages */
    scl_set_config(0x10, NBG2_CFG);
    W16(SFPRMD, R16(SFPRMD) & 0xFFF3);
    W16(SFPRMD, R16(SFPRMD) & 0xFFCF);
    W16(C + 0x10, 0x7FFF);
}

/* 0x060A8B0C */
const StageInitFn g_StageInits[STAGE_INIT_COUNT] = {
    init_00, init_01, init_02, init_03, init_04, init_05, init_06, init_07, init_08,
    init_09, init_10, init_11, init_12, init_13, init_14, init_15, init_16, init_17,
};

void StageInit_Run(int stageIndex)
{
    if (stageIndex >= 0 && stageIndex < STAGE_INIT_COUNT) g_StageInits[stageIndex]();
}

/* 0x06046B14: write the VDP2 2-word pattern name of the map tile under (x,y) (layer space,
 * 16.16) into the layer's 32x32 page at desc+0x24 (16x16 characters, ring-wrapped).
 * MAP word -> pattern name: bit 15 H-flip -> bit 30, bit 14 V-flip -> bit 31, bit 13 special
 * priority -> bit 29, bit 0 special colour calc -> bit 28, bit 12 palette bit 4 (= +256 colours)
 * -> bit 20, bits 11-1 = 16x16 tile -> character number (tile * 8) + char base desc+0x2C. */
void Layer_PutTile(int32_t x, int32_t y, uint32_t desc)
{
    uint32_t pn = Map_GetPN(x, y, desc);
    uint32_t vram = R32(desc + 0x24);
    int32_t tx = (int16_t)((uint32_t)((x + (int32_t)0xFC000000) >> 4) >> 16) & 31;
    int32_t ty = (int16_t)((uint32_t)((y + (int32_t)0xFC000000) >> 4) >> 16) & 31;
    uint32_t v = ((pn & 0x0FFEu) << 2) & 0x7FFEu;
    v |= (pn & 0x2000u) << 16;
    v |= (pn & 0x8000u) << 15;
    v |= (pn & 0x4000u) << 17;
    v |= (pn & 0x0001u) << 28;
    v |= (pn & 0x1000u) << 8;
    v |= R32(desc + 0x2C);
    W32(vram + (uint32_t)ty * 128u + (uint32_t)tx * 4u, v);
}

/* 0x06046EBC: 22 x 18 tiles from the layer position (screen + one tile margin) */
void Layer_FillVram(uint32_t desc)
{
    for (int32_t y = 0; y < 0x01200000; y += 0x00100000)
        for (int32_t x = 0; x < 0x01600000; ) {
            Layer_PutTile(RS32(desc) + x, RS32(desc + 4) + y, desc);
            x += 0x00100000;
            Layer_PutTile(RS32(desc) + x, RS32(desc + 4) + y, desc);
            x += 0x00100000;
        }
}

/* 0x06047244: NBG3 = status-bar background (16-colour 8x8 cells at character 0x2C00, i.e. the
 * last 32 KB of the CHR file, palette CRAM 0x300 from 0x060CEA3E): LZ map 0x060CE5B8 ->
 * 0x060EA658, then 0x800 2-word pattern names (64 x 32 cells) at VRAM 0x25E6C000. */
void Hud_InitNbg3Map(void)
{
    Lz_DecompressRam(0x060CE5B8u, HUD_MAP_BUF);               /* 0x06004EE8 */
    for (uint32_t i = 0; i < 0x800; i++) {
        uint16_t v = R16(HUD_MAP_BUF + i * 2u);
        W32(NBG3_PN_ADDR + i * 4u, ((uint32_t)(v & 0xF000u) << 4) | (v & 0x03FFu) | 0x2C00u);
    }
}

/* 0x060471D4 (game mode 5, after the player is placed): layers from the camera, previous
 * positions, VRAM pattern names of the visible area, then the NBG3 map (tail call). */
void Stage_InitLayers(void)
{
    const uint32_t d0 = D, d1 = D + 0x30, d2 = D + 0x60;
    Layer_FromCamera(RS32(CAM_X), RS32(CAM_Y), d0);
    Layer_FromCamera(RS32(CAM_X), RS32(CAM_Y), d1);
    Layer_FromCamera(RS32(CAM_X), RS32(CAM_Y), d2);
    W32(0x060252ACu, R32(CAM_Y));
    W32(d0 + 8, R32(d0)); W32(d1 + 8, R32(d1)); W32(d2 + 8, R32(d2));
    W32(d0 + 12, R32(d0 + 4)); W32(d1 + 12, R32(d1 + 4)); W32(d2 + 12, R32(d2 + 4));
    Layer_FillVram(d0);
    Layer_FillVram(d1);
    Layer_FillVram(d2);
    Hud_InitNbg3Map();
}
