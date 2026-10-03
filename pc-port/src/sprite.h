#ifndef SPRITE_H
#define SPRITE_H
#include <stdint.h>

/* Character table @ 0x0601C508, 10 bytes per char (A.BIN):
 *  +0 VDP1 VRAM address >> 3 (emulated VRAM at 0x25C00000)
 *  +2 size: (width/8) << 8 | height      +4 colour mode (0x20 = 8bpp bank)
 *  +8 size in 32-byte units */
#define CHAR_TABLE  0x0601C508u
#define SHADOW_CRAM 0x06024098u   /* 1024 RGB555 colours, copied to CRAM on vblank */

/* 0x06006DE4: `off` = VRAM offset from the character area 0x25C04740.
 * mode = VDP1 CMDPMOD colour bits: 0x00 4bpp bank, 0x08 4bpp LUT, 0x20 8bpp, 0x28 RGB. */
void Char_Register(int id, int w, int h, int mode, uint32_t off);
/* 0x06044A78 */
void Player_InitChars(void);
/* 0x060068C0 (no scaling path yet): queue a VDP1 sprite from a sprite struct. */
void Sprite_Draw(uint32_t spr);
/* 0x06044AF8: stream the player frame (double-buffered char slots 1/2), then draw. */
void Player_SubmitSprite(uint32_t spr);
/* 0x06005C8C: copy `n` RGB555 colours to the shadow CRAM with an RGB offset. */
void Pal_SetFaded(int dst, int n, uint32_t src, int dr, int dg, int db);
/* 0x06044D78: load Sho's palette (normal / special-aura) into CRAM 0x400. */
void Player_UpdatePalette(void);
#endif
