/* A.BIN boot sequence and main loop body (0x06004038), soft reset (0x060042B8). */
#include "stage_hooks.h"
#include "boot.h"
#include "ram.h"
#include "pad.h"
#include "fade.h"
#include "resource.h"
#include "director.h"
#include "shinobi.h"
#include <stdio.h>

/* 0x06005BA0 */
void Vdp2_DisplayOn(void)
{
    W16(VDP2_TVMD, R16(VDP2_TVMD) | 0x8000);
    W16(0x0603A474u, 1);                         /* VDP2 shadow dirty */
}

/* 0x06005BB4 */
void Vdp2_DisplayOff(void)
{
    W16(VDP2_TVMD, R16(VDP2_TVMD) & 0x7FFF);
    W16(0x0603A474u, 1);
}

void Boot_Init(int biosLogo)
{
    /* 0x06005130, 0x06005840, 0x060057F0, 0x06005B0A (hardware init), 0x06004418: */
    W16(0x0602524Cu, 0);                         /* no movie open (CPK library) */
    Res_Init();                                  /* 0x06004418 */
    /* 0x06004E10: no pending overlay decompression */
    W16(0x0601ABC8u, 0); W32(0x0601ABCCu, 0);
    /* 0x06008CC4 Cpk_Reset, 0x06007BAC (sound) */
    Fade_Reset();                                /* 0x06006F56 */
    /* 0x06005154, 0x06009114 */
    W16(0x0601C498u, 0);                         /* DMA/peripheral flag; 0x25FE0030/50 = 0 */
    /* 0x0600513A, 0x0600550C, 0x06005522 */
    W16(0x0602526Au, 0xFFFF);                    /* stage index -1 */
    W16(0x060252C0u, 0);                         /* control type */
    W16(0x060252C4u, 1);                         /* difficulty NORMAL */
    W16(0x060252B4u, 20);                        /* initial shurikens */
    W32(0x060252E0u, 0xFFFFFFFFu);               /* soft reset: ignored */
    if (R32(0x06000210u) != 0x39303130u || R32(0x06000214u) > 0x05F5E0FFu) {   /* "9010" */
        W32(0x06000214u, 0);                     /* hi-score */
        W32(0x06000210u, 0x39303130u);
    }
    W32(G_DIR_SCENE, 0);
    if (biosLogo) W32(0x06014404u, 1);           /* PC: see boot.h */
    Load_DirectorPPB();                          /* 0x06004EAC */
    W16(G_DIR_MODE, 0);
}

/* jsr @0x06040000 with *0x060145FC = 0x06040000 */
void Overlay_Entry(void)
{
    W32(0x060145FCu, 0x06040000u);
    switch (Res_OverlayResident()) {
    case OVL_DIRECTOR: Director_Entry(); break;
    case OVL_SHINOBI:  Shinobi_Entry();  break;
    default: break;                              /* nothing loaded (missing assets) */
    }
}

void Boot_Frame(void)
{
    /* VBlank interrupt side: peripheral read + frame counter */
    Pad_Update(2);                               /* 0x0600572A */
    W16(0x060143EAu, R16(0x060143EAu) + 1u);
    /* main loop 0x060041C6: 0x0602CCC4 (frame sync) */
    Sys_SoftReset();                             /* 0x060042B8 */
    Res_ProcessQueue();                          /* 0x0600446A */
    Res_PendingDecompress();                     /* 0x06004E1C */
    /* 0x060288A8(1) Spr2_OpenCommand, 0x06005F4C clipping: renderer */
    Overlay_Entry();                             /* 0x06040000 */
    /* 0x0602894C, NBG0..3 config (0x06029B40 with the SclConfig blocks 0x06020D08 +
     * 0x4C*n; byte 0 = display enable, read by render.c) */
    Fade_Update();                               /* 0x06007084 */
    LineTable_VBlank();                          /* VBlank 0x06004FD4/0x060050A4: HBlank line table */
    /* 0x0600863E sound scheduler: sound.c (not part of the headless build) */
}

/* 0x060042B8 */
void Sys_SoftReset(void)
{
    int32_t act = -1;
    if ((R16(0x0601C4A0u) & 0x0700) == 0x0700 && (R16(0x0601C4A4u) & 0x0800))
        act = RS32(0x060252E0u);
    if (act == -1) return;
    if (act == 1) {                              /* jmp *(0x0600026C): back to the BIOS */
        printf("[boot] soft reset -> BIOS (PC: reboot)\n");
        Boot_Init(1);
        return;
    }
    printf("[boot] soft reset -> DIRECTOR title\n");
    W16(0x0601C498u, 0);                         /* + DMA 0x25FE0030/50 = 0 */
    Vdp2_DisplayOff();                           /* 0x06005BB4 */
    W16(VDP2_TVMD, R16(VDP2_TVMD) & 0x7FFF);     /* + direct write to TVMD 0x25F80000 */
    Res_CloseIfOpen();                           /* 0x060048D8 */
    /* 0x0602CCC4, 0x06030A1A (sound stop) */
    Res_Init();                                  /* 0x06004418 */
    W16(0x0601ABC8u, 0); W32(0x0601ABCCu, 0);    /* 0x06004E10 */
    /* 0x06008CC4 Cpk_Reset, 0x06007BAC */
    Fade_Reset();                                /* 0x06006F56 */
    Load_DirectorPPB();                          /* 0x06004EAC */
    W32(G_DIR_SCENE, 1);
    W32(0x060252E0u, 0xFFFFFFFFu);
    W16(0x0602526Au, 0xFFFF);
    W16(G_DIR_MODE, 0);
}

/* PC only (--stage N). Same variables as a GAME START from the title (Title_NewGameDefaults
 * 0x06040F3E + Title_Launch 0x060412D4) and SHOU.CHR in LWRAM like the story scene step 0,
 * then the overlay swap DIRECTOR's stage start does (g_DirMode 2 -> Load_ShinobiPPB). */
void Boot_DevStartStage(int idx)
{
    int d = RS16(0x060252C4u);
    W16(RES_QCOUNT, 0);                          /* drop the queued DIRECTOR.PPB */
    W32(0x060252E0u, 1);
    W16(0x0602529Cu, 0);
    W16(0x060252B2u, 0);
    W16(0x060252B0u, R16(0x06014424u + 2u * (uint32_t)d));   /* HP max */
    W16(0x060252B8u, R16(0x0601442Au + 2u * (uint32_t)d));   /* lives */
    W16(0x060252C2u, R16(0x06014430u + 2u * (uint32_t)d));   /* continues */
    W16(0x060252BAu, 0);
    W32(0x060252C8u, 0);
    /* 0x060252DC: 0 only on the very first stage of a new game (1-1: Sho drops in from the
     * sky); any later stage is entered with 1 and starts at its checkpoint position. With 0
     * Sho fell from y = 1248 into stages that start lower (5-1, 6-1, 8-1...) and died. */
    W16(0x060252DCu, idx == 0 ? 0u : 1u);
    W16(0x060252B6u, R16(0x060252B4u));
    W16(0x060252BEu, 0);
    W16(0x0602529Eu, 0);
    W32(0x060252D0u, 0); W32(0x060252D4u, 5000); W32(0x060252D8u, 1);   /* bonus seeds */
    W16(0x0602526Au, (uint32_t)idx);
    Res_RequestFile(1, 0, LWRAM_BASE, "SHOU.CHR");
    Load_ShinobiPPB();                           /* 0x06004E6A */
    W16(G_DIR_MODE, 0);
}
