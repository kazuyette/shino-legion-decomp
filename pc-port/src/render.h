#ifndef RENDER_H
#define RENDER_H

#include <stdint.h>
#include <stddef.h>

struct SDL_Window;

/* SDL2 renderer standing in for VDP1/VDP2. The game queues sprites through
 * Sprite_Draw (sprite.c, port of A.BIN 0x060068C0); each queued sprite is a
 * character in the emulated VDP1 VRAM + a CMDCOLR colour in the shadow CRAM.
 * Logical resolution = Saturn NTSC 320x224. */

#define SCREEN_W 320
#define SCREEN_H 224

/* One VDP1 distorted-sprite command as built by Spr2_DistSpr (0x0602905C): every sprite the
 * game draws through Sprite_Draw (0x060068C0) is a textured quad A,B,C,D (A = top-left texel,
 * B = top-right, C = bottom-right, D = bottom-left; vertices are inclusive pixel positions). */
typedef struct {
    uint32_t src;        /* CMDSRCA: VDP1 VRAM address of the pixels */
    uint16_t mode;       /* CMDPMOD: bits 5-3 colour mode (0 4bpp bank, 1 4bpp LUT, 2 64, 3 128, 4 256,
                            5 RGB), bit 6 SPD (colour 0 opaque), bits 2-0 colour calc (1 shadow,
                            2 half luminance, 3 half transparent); ECD (0x80) is not kept */
    uint16_t colr;       /* CMDCOLR: colour bank (bank modes, also carries the VDP2 sprite priority /
                            colour-calc bits) or lookup table address / 8 (LUT mode) */
    int w, h;            /* CMDSIZE: character size */
    int x, y;            /* vertex A (top-left corner of an unrotated, unscaled sprite) */
    int flipH, flipV;    /* CMDCTRL Dir: texture flipped inside the quad */
    int qx[4], qy[4];    /* CMDXA..CMDYD: vertices A, B, C, D (screen pixels) */
    int prio;            /* SPR_2 draw priority 0..0x1FF: VDP1 command order (ascending, FIFO on ties) */
} RenderSprite;

int  Render_Init(struct SDL_Window *window);
void Render_QueueSprite(const RenderSprite *s);
void Render_InvalidatePalettes(void);
/* Drop cached sprite textures whose pixels overlap [addr, addr+len) (VRAM rewritten). */
void Render_InvalidateVram(uint32_t addr, size_t len);
void Render_Clear(void);
void Render_DrawFrame(void);
void Render_Present(void);
/* PC window helpers (shinobi.ini [video]): call Render_BeginFrame before Render_Clear (selects
 * the 320x224 frame target that Render_Present scales to the window). */
void Render_BeginFrame(void);
void Render_ToggleFullscreen(void);      /* Alt+Enter / F11: desktop fullscreen <-> window */
void Render_RequestScreenshot(void);     /* F12: saved by the next Render_Present */
void Render_Shutdown(void);
void Render_SetKeepQueue(int keep);   /* 1: keep the sprite queue (frame paused) */

#endif
