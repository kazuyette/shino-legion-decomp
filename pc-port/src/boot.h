#ifndef BOOT_H
#define BOOT_H

/* FUN_06004038 — boot init sequence + per-VBlank main loop body on real
 * hardware. On PC, the outer SDL loop in main.c drives frames, and this
 * runs the per-frame logic. */
void Boot_Init(void);
void Boot_RunFrame(void);

/* FUN_060042b8 — stage teardown: disable DMA, reset VDP2 TVMD, run the
 * 7 reset callbacks, stop sound, reload DIRECTOR.PPB. */
void Stage_ResetAndLoadDirector(void);

#endif
