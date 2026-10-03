#ifndef RENDER_H
#define RENDER_H

#include <stdint.h>

struct SDL_Window;

/* SDL2 renderer standing in for VDP1/VDP2. The game queues sprites through
 * Sprite_Draw (sprite.c, port of A.BIN 0x060068C0); each queued sprite is
 * an 8bpp LZ-compressed character in emulated RAM + a palette in the
 * shadow CRAM. Logical resolution = Saturn NTSC 320x224. */

#define SCREEN_W 320
#define SCREEN_H 224

typedef struct {
    uint32_t src;        /* RAM address of the LZ-compressed 8bpp pixels */
    uint32_t palette;    /* RAM address of 256 RGB555 colours */
    int x, y, w, h;
    int flipH, flipV;
    int rotate, angle;   /* angle: 256 units per turn */
    int cx, cy;          /* rotation centre (screen) */
} RenderSprite;

int  Render_Init(struct SDL_Window *window);
void Render_QueueSprite(const RenderSprite *s);
void Render_InvalidatePalettes(void);
void Render_Clear(void);
void Render_DrawFrame(void);
void Render_Present(void);
void Render_Shutdown(void);

#endif
