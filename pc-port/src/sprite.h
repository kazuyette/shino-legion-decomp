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
/* Sprite struct (e.g. 0x060250AC), s16 unless noted:
 *  +0x00 char   +0x02/+0x04 screen X/Y (anchor, rotation/scale centre)   +0x06/+0x08 part offset
 *  +0x0A colour bank, +0x0C VDP2 sprite priority register - 1, +0x0E colour-calc/MSB bits
 *  +0x10 flags: 0x8000 H-flip, 0x4000 V-flip, 0x2000 CMDCOLR = +0x12, 0x1000 scale X by +0x1C,
 *        0x0800 scale Y by +0x20, 0x0400 rotate by +0x14, 0x0200 draw priority += +0x18,
 *        0x0100 ignore the global zoom, 0x0004 distorted (4 vertices at +0x24), 0x0001/0x0002 flips
 *  +0x12 CMDCOLR   +0x14 angle (256/turn)   +0x18 s32 draw priority offset
 *  +0x1C/+0x20 s32 16.16 scale X/Y   +0x24..+0x32 vertices A,B,C,D (x,y) for the distorted path */
#define SPR_ZOOM      0x060250E0u   /* s32 16.16 global sprite zoom (0x10000 = off), set by 0x0604455A */
#define SPR_ZOOM_X    0x060250E4u   /* zoom centre, screen 16.16 */
#define SPR_ZOOM_Y    0x060250E8u
#define SPR2_LUT_BASE 0x0603A51Cu   /* SPR_2 colour lookup tables, VDP1 VRAM address / 8 */
#define SPR2_LUT_BASE_DEFAULT 0x0864u /* value set by Spr2_Initial 0x06028368 (VRAM 0x4320) */

/* 0x060068C0: build the 4 vertices of a sprite (normal / scaled / rotated / distorted),
 * cull it against the screen and queue it (SPR_2DistSpr 0x0602905C). */
void Sprite_Draw(uint32_t spr);
/* 0x0602905C (SBL SPR_2DistSpr): one VDP1 distorted-sprite command, linked into the
 * draw-priority list (z sort 0x06031588) -> Render_QueueSprite. xy = 4 vertices A..D. */
void Spr2_DistSpr(int prty, uint16_t ctrl, uint16_t pmod, uint16_t colr, uint16_t chr,
                  const int16_t xy[8], uint16_t gour);
/* 0x0602AFD4 (SBL SCL_SetPriority): set the VDP2 priority of the screens in `mask`
 * (1 RBG0, 2/4 NBG0, 8/0x80 NBG1, 0x10 NBG2, 0x20 NBG3, 0x100 << n sprite register n)
 * in the VDP2 register shadow (PRISA 0x0603B47C.., PRINA 0x0603B484, PRIR 0x0603B488). */
void Scl_SetPriority(unsigned mask, unsigned prio);
/* 0x06044AF8: stream the player frame (double-buffered char slots 1/2), then draw. */
void Player_SubmitSprite(uint32_t spr);
/* 0x06005C8C: copy `n` RGB555 colours to the shadow CRAM with an RGB offset. */
void Pal_SetFaded(int dst, int n, uint32_t src, int dr, int dg, int db);
/* 0x06044D78: load Sho's palette (normal / special-aura) into CRAM 0x400. */
void Player_UpdatePalette(void);
#endif
