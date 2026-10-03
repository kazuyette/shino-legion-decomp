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

typedef struct {
    uint32_t src;        /* VDP1 VRAM address of the pixels */
    uint16_t mode;       /* CMDPMOD colour mode: 0x00 4bpp bank, 0x08 4bpp LUT, 0x10 64, 0x18 128,
                            0x20 256 colours, 0x28 RGB */
    uint16_t colr;       /* CMDCOLR: colour bank (dot | colr, bits 10-0 = CRAM index) */
    int x, y, w, h;
    int flipH, flipV;
    int rotate, angle;   /* angle: 256 units per turn */
    int cx, cy;          /* rotation centre (screen) */
} RenderSprite;

int  Render_Init(struct SDL_Window *window);
void Render_QueueSprite(const RenderSprite *s);
void Render_InvalidatePalettes(void);
/* Drop cached sprite textures whose pixels overlap [addr, addr+len) (VRAM rewritten). */
void Render_InvalidateVram(uint32_t addr, size_t len);
void Render_Clear(void);
void Render_DrawFrame(void);
void Render_Present(void);
void Render_Shutdown(void);

#endif
