#include "stage.h"
#include "ram.h"
#include "lz.h"
#include "sprite.h"
#include "collision.h"
#include "camera.h"
#include "player.h"
#include "objects.h"
#include <stdio.h>

/* VDP2 register shadow (A.BIN): +4 = CRAOFA, NBGn colour RAM offset in bits 4n..4n+2. */

/* 0x06005EAC */
void Vdp2_SetColorRamOffsets(int n0, int n1, int n2, int n3)
{
    uint32_t a = VDP2_SHADOW_ADDR + 4;
    if (n0 >= 0) W16(a, (R16(a) & 0xFFF8) | (n0 & 7));
    if (n1 >= 0) W16(a, (R16(a) & 0xFF8F) | (n1 & 7) << 4);
    if (n2 >= 0) W16(a, (R16(a) & 0xF8FF) | (n2 & 7) << 8);
    if (n3 >= 0) W16(a, (R16(a) & 0x8FFF) | (n3 & 7) << 12);
}

#define MAP_ADDR   0x002B0000u           /* LWRAM, *(0x060EA650) */
#define D          LAYER_DESC            /* 3 layer descriptors, 0x30 bytes */
#define C          0x060EA618u           /* scroll/screen control block */

static const char *kStageNames[] = { "STAGE7_1", "STAGE7_2", "STAGE2_1", "STAGE2_2", "STAGE3_1", "STAGE3_2",
    "STAGE4_1", "STAGE4_2", "STAGE5_1", "STAGE5_2", "STAGE6_1", "STAGE6_2", "STAGE1_1", "STAGE1_2" };

/* 0x060473B4: stage 1-1 specific layer setup. */
static void stage1_1_init(void)
{
    W16(C + 0x20, R16(C + 0x20) | 0x40);
    W16(C + 0x2A, 4); W16(C + 0x2C, 3); W16(C + 0x2E, 2); W16(C + 0x30, 7);
    W16(C + 0x22, 3); W16(C + 0x24, 4); W16(C + 0x26, 5); W16(C + 0x28, 7);
    W32(D + 0x20, 0x060CEAD8u); W32(D + 0x50, 0x060CEB0Eu); W32(D + 0x80, 0x060CEB20u);   /* page layouts */
    W32(D + 0x10, 0x04000000u); W32(D + 0x14, 0x04000000u);
    W32(D + 0x40, 0x04000000u); W32(D + 0x44, 0x04580000u);
    W32(D + 0x70, 0x04000000u); W32(D + 0x74, 0x04F00000u);
    W32(C + 0x00, 0x04000000u); W32(C + 0x04, 0x04000000u);
    W32(C + 0x08, R32(C) + 0x34000000u); W32(C + 0x0C, R32(C + 4) + 0x01F00000u);      /* world bounds */
    W16(D + 0x18, 0x100); W16(D + 0x1A, 0x100);     /* parallax rates (0x100 = 1.0) */
    W16(D + 0x48, 0x77);  W16(D + 0x4A, 0xB0);
    W16(D + 0x78, 0x18);  W16(D + 0x7A, 0x19);
    W16(C + 0x32, 0); W16(C + 0x34, 0xFFFF);
}

/* 0x06047128: stage palettes from the MAP file into the shadow CRAM. */
static void stage_palettes(void)
{
    uint32_t m = R32(0x060EA650u);
    for (int i = 0; i < 0x300; i++) W16(SHADOW_CRAM + 2u * i, R16(m + 0xC00 + 2u * i));
    size_t n = Lz_OutputSize(Ram_Ptr(0x060CEA3Eu));      /* 0x06004EE8 = LZ decompress */
    if (n && n <= 0x200) Lz_Decompress(Ram_Ptr(0x060CEA3Eu), 0x400, Ram_Ptr(SHADOW_CRAM + 0x600), 0x200);
    W16(0x060250A8u, 1);
}

int Stage_Load(const char *dir, int idx)
{
    char p[512];
    if (idx < 0 || idx >= (int)(sizeof kStageNames / sizeof *kStageNames)) return 0;
    W16(G_STAGE_INDEX, (uint32_t)idx);
    snprintf(p, sizeof p, "%s/%s.CHR", dir, kStageNames[idx]);
    if (!Ram_LoadCompressed(p, VDP2_VRAM_BASE, VDP2_VRAM_SIZE)) { printf("[stage] missing %s\n", p); return 0; }
    snprintf(p, sizeof p, "%s/%s.MAP", dir, kStageNames[idx]);
    if (!Ram_LoadCompressed(p, MAP_ADDR, 0x20000)) { printf("[stage] missing %s\n", p); return 0; }
    W32(0x060EA650u, MAP_ADDR);

    /* 0x0604861C (common part) */
    uint32_t m = MAP_ADDR;
    W32(D + 0x1C, m + 0x1220 + (R32(m + 0x1200) >> 1) * 2);
    W32(D + 0x4C, m + 0x1220 + (R32(m + 0x1204) >> 1) * 2);
    W32(D + 0x7C, m + 0x1220 + (R32(m + 0x1208) >> 1) * 2);
    W32(D + 0x28, m); W32(D + 0x58, m + 0x400); W32(D + 0x88, m + 0x800);   /* collision attrs */
    W16(C + 0x10, 0x18); W16(C + 0x12, 0x7FFF); W16(C + 0x14, 0x130); W16(C + 0x16, 0x10);
    W32(D + 0x2C, 0); W32(D + 0x5C, 0x1000); W32(D + 0x8C, 0x2000); W32(D + 0xBC, 0x2C00);  /* char bases */
    W16(C + 0x18, 0); W16(C + 0x1A, 0); W16(C + 0x1C, 0);
    Vdp2_SetColorRamOffsets(0, 1, 2, 3);                   /* 0x06005EAC: NBG0..3 CRAM offsets */
    W16(C + 0x20, 0);
    W32(0x060EA550u, 0x005C0000u); W32(0x060EA554u, 0x00B40000u);
    W32(0x060EA568u, 0x00D20000u); W32(0x060EA564u, 0x006E0000u);
    W32(0x060EA55Cu, 0x006E0000u); W32(0x060EA560u, 0x00AA0000u);
    W32(0x060EA56Cu, 0x00F00000u); W32(0x060EA570u, 0x00280000u);
    W16(C + 0x1E, 0xFFFF);
    W16(G_COLL_LAYER, 0);
    if (idx == 12) stage1_1_init();
    else printf("[stage] per-stage init for index %d not ported yet\n", idx);
    stage_palettes();
    Obj_ClearAll();
    int n = Stage_LoadObjects();                           /* 0x06048BD0 */
    printf("[stage] %s loaded, %d objects placed (%d active slots)\n", kStageNames[idx], n, Obj_Count());
    return 1;
}

/* PC: the original start position comes from stage scripts not traced yet.
 * Drop Sho near the start of 1-1 (world origin is 1024,1024). */
void Stage_SpawnPlayer(void)
{
    PW32(P_X, (1024u + 120u) << 16);
    PW32(P_Y, (1024u + 300u) << 16);
    W32(CAM_X, (uint32_t)(P32(P_X) - RS32(0x060EA550u)));
    W32(CAM_Y, (uint32_t)(P32(P_Y) - RS32(0x060EA554u)));
    W32(0x060252A8u, R32(CAM_X));
    W32(0x060252ACu, R32(CAM_Y));
    Scroll_Update();
}
