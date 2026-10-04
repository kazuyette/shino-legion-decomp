#ifndef SOFT_RENDER_H
#define SOFT_RENDER_H
/* Software (SDL-free) side of the VDP1/VDP2 emulation: colour decoding of VDP1 characters,
 * of the VDP2 background layers (NBG0..NBG2 from the stage MAP pages, NBG3 from VDP2 VRAM),
 * the VDP2 priority registers, and a reference rasteriser that composes a whole frame into an
 * ARGB buffer (used by tests to write PNG screenshots without SDL). render.c builds its SDL
 * textures with the same decoders. */
#include <stdint.h>
#include "render.h"

#define VDP2_PAGE_PX 512    /* one page: 32x32 16x16 characters (NBG0-2) / 64x64 8x8 cells (NBG3) */

/* VDP2 register shadow 0x0603B46C mirrors 0x25F800E0..: +0x00 SPCTL, +0x04 CRAOFA, +0x0A SFPRMD,
 * +0x0C CCCTL, +0x10..+0x16 PRISA..PRISD (sprite registers S0..S7), +0x18 PRINA (NBG0 | NBG1 << 8),
 * +0x1A PRINB (NBG2 | NBG3 << 8), +0x1C PRIR. Higher priority is in front; 0 = not displayed;
 * on a tie sprite > NBG0 > NBG1 > NBG2 > NBG3. */
typedef struct { int nbg[4], spr[8], sptype; } Vdp2Prio;
void Vdp2_GetPriorities(Vdp2Prio *p);
/* Priority register number held in a sprite's colour data for the VDP2 sprite type. */
int  Vdp2_SpritePrioReg(const RenderSprite *s, int type);

uint32_t Vdp_Rgb555ToArgb(uint16_t c);            /* opaque ARGB8888 */
uint32_t Vdp2_CramArgb(unsigned idx);             /* shadow CRAM colour idx (0..0x7FF) */

/* VDP1 character (CMDPMOD colour modes 0-5) -> w*h ARGB, 0 = transparent. */
void Vdp1_DecodeChar(const RenderSprite *s, uint32_t *argb);

/* NBG0..NBG2: displayed (SclConfig dispenbl) and set up (layout + pages). NBG3: dispenbl. */
int  Vdp2_LayerEnabled(int nbg);
/* Top-left screen pixel of the layer in its plane (NBG0-2: descriptor position - 1024 px,
 * NBG3: the VDP2 scroll shadow 0x06024088). */
void Vdp2_LayerScroll(int nbg, int *sx, int *sy);
/* Page id at page coordinates (px,py) of NBG0-2 (layout wraps), -1 = empty. */
int  Vdp2_LayerPageAt(int nbg, int px, int py);
/* One 512x512 page of NBG0..NBG2 decoded from the MAP (pattern-name format of 0x06046B14). */
void Vdp2_DecodeMapPage(int nbg, int pageId, uint32_t *argb);
uint32_t Vdp2_MapPageChecksum(int nbg, int pageId);
/* NBG3 (status bar): 512x512 from the VRAM page at its SclConfig plate address. */
void Vdp2_DecodeNbg3(uint32_t *argb);
uint32_t Vdp2_Nbg3Checksum(void);

/* Reference frame: back colour, VDP2 layers and the queued sprites by priority (sprites
 * mapped affinely through vertices A,B,D; VDP1 colour calculation approximated), then the
 * VDP2 colour offsets. fb = SCREEN_W x SCREEN_H ARGB. */
void Soft_ComposeFrame(uint32_t *fb, const RenderSprite *spr, int n, uint32_t backArgb);
/* 8-bit RGB PNG (stored deflate blocks). 1 on success. */
int  Soft_WritePng(const char *path, const uint32_t *argb, int w, int h);
#endif
