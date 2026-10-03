#ifndef BOOT_H
#define BOOT_H
/* A.BIN boot + main loop (Boot_And_MainLoop 0x06004038), ported without SDL.
 *
 * The original never returns: after the boot settings it queues DIRECTOR.PPB and loops
 *   0x0602CCC4 (wait VBlank) / Sys_SoftReset 0x060042B8 / Res_ProcessQueue 0x0600446A /
 *   0x06004E1C / Spr2_OpenCommand(1) / 0x06005F4C (VDP1 clipping) /
 *   overlay entry 0x06040000 (DIRECTOR or SHINOBI, whichever is resident) /
 *   0x0602894C (close the VDP1 list) / NBG0-3 SCL_SetConfig + scroll uploads /
 *   Fade_Update 0x06007084 / sound scheduler 0x0600863E.
 * On PC main.c owns the frame pacing and calls Boot_Frame() once per frame; the VDP1/VDP2
 * uploads are replaced by the renderer reading the emulated RAM. */

/* 0x06004038, part before the loop. biosLogo: PC choice -- the Saturn BIOS shows the SEGA
 * logo at power-on so DIRECTOR skips its own on the first boot (0x06014404 == 0); with
 * biosLogo = 1 the flag is preset so DIRECTOR's logo stands in for the BIOS one. */
void Boot_Init(int biosLogo);
/* One iteration of the main loop (VBlank side: Pad_Update + frame counter 0x060143EA). */
void Boot_Frame(void);
/* Development shortcut (--stage N): new-game variables, SHOU.CHR, then SHINOBI.PPB with
 * stage index `idx` instead of DIRECTOR. Call right after Boot_Init. */
void Boot_DevStartStage(int idx);

void Sys_SoftReset(void);      /* 0x060042B8: A+B+C+Start, action from 0x060252E0 */
void Vdp2_DisplayOn(void);     /* 0x06005BA0: TVMD shadow 0x0603A570 |= DISP */
void Vdp2_DisplayOff(void);    /* 0x06005BB4 */
void Overlay_Entry(void);      /* jsr 0x06040000: Director_Entry or Shinobi_Entry */

#define VDP2_TVMD 0x0603A570u  /* TVMD shadow, bit 15 = DISP (screen on) */
#define G_DIR_MODE 0x06025268u /* u16 overlay mode shared by DIRECTOR and SHINOBI */
#define G_DIR_SCENE 0x060252CCu /* u32 DIRECTOR scene / request from SHINOBI */
#endif
