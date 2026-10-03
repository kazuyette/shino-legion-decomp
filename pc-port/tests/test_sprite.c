/* Sprite_Draw (A.BIN 0x060068C0) -> Spr2_DistSpr (0x0602905C) -> RenderSprite:
 * normal / flipped / scaled / rotated / distorted sprites, screen cull, draw priority,
 * colour, global zoom; Scl_SetPriority (0x0602AFD4). */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ram.h"
#include "render.h"
#include "sprite.h"
#include "fixmath.h"
#include "input.h"

extern RenderSprite g_lastSprite; extern int g_spriteCount;
static InputPort fake[2];
const InputPort *Input_GetPort(int p) { return &fake[p]; }   /* pad.c needs it */

#define SPR 0x060250ACu
#define CHR 40                              /* free char-table slot */

static void spr_reset(int x, int y, int ox, int oy, uint16_t fl)
{
    for (uint32_t i = 0; i < 0x34; i += 2) W16(SPR + i, 0);
    W16(SPR + 0x00, CHR);
    W16(SPR + 0x02, (uint32_t)x); W16(SPR + 0x04, (uint32_t)y);
    W16(SPR + 0x06, (uint32_t)ox); W16(SPR + 0x08, (uint32_t)oy);
    W16(SPR + 0x0A, 0x40);                  /* colour bank 0x400 */
    W16(SPR + 0x10, fl);
}
static int draw(void) { int n = g_spriteCount; Sprite_Draw(SPR); return g_spriteCount - n; }
static void quad(int ax, int ay, int bx, int by, int cx, int cy, int dx, int dy)
{
    const RenderSprite *s = &g_lastSprite;
    int ok = s->qx[0] == ax && s->qy[0] == ay && s->qx[1] == bx && s->qy[1] == by &&
             s->qx[2] == cx && s->qy[2] == cy && s->qx[3] == dx && s->qy[3] == dy;
    if (!ok) printf("  got A(%d,%d) B(%d,%d) C(%d,%d) D(%d,%d)\n", s->qx[0], s->qy[0], s->qx[1], s->qy[1],
                    s->qx[2], s->qy[2], s->qx[3], s->qy[3]);
    assert(ok);
    assert(s->x == ax && s->y == ay);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "../extracted/";
    char p[512];
    snprintf(p, sizeof p, "%sA.BIN", dir); assert(Ram_LoadImage(p, 0x06004000u));   /* sine table */
    assert(Sin8(0x40) == 0x10000 && Cos8(0x40) == 0);
    Char_Register(CHR, 16, 8, 0x20, 0x1000);                /* 16x8 8bpp at VRAM 0x5740 */
    W32(SPR_ZOOM, 0x10000);

    /* normal: A = anchor + offset, B/C/D inclusive corners */
    spr_reset(100, 50, -8, -4, 0);
    assert(draw() == 1);
    quad(92, 46, 107, 46, 107, 53, 92, 53);
    assert(g_lastSprite.w == 16 && g_lastSprite.h == 8 && g_lastSprite.mode == 0x20);
    assert(g_lastSprite.src == VDP1_VRAM_BASE + 0x5740);
    assert(g_lastSprite.colr == (uint16_t)(0x40 << 4 | 1 << 13));      /* bank 0x400, priority register 1 */
    assert(!g_lastSprite.flipH && !g_lastSprite.flipV && g_lastSprite.prio == 0x100);
    printf("normal: A(92,46) C(107,53) colr %04X prio %X\n", g_lastSprite.colr, g_lastSprite.prio);

    /* H+V flip: mirrored offsets, Dir bits; part flip bit 0 toggles H back */
    spr_reset(100, 50, 4, 2, 0xC000);
    assert(draw() == 1);
    quad(80, 40, 95, 40, 95, 47, 80, 47);
    assert(g_lastSprite.flipH && g_lastSprite.flipV);
    spr_reset(100, 50, 4, 2, 0xC001);
    draw();
    assert(!g_lastSprite.flipH && g_lastSprite.flipV);

    /* scaled X (flag 0x1000, +0x1C = 2.0): offset and width doubled */
    spr_reset(100, 50, -8, -4, 0x1000);
    W32(SPR + 0x1C, 0x20000);
    draw();
    quad(84, 46, 115, 46, 115, 53, 84, 53);
    /* scaled Y (flag 0x0800, +0x20 = 1.5) + draw priority (0x0200, +0x18): water waves 0x1005 use 0x0A00 */
    spr_reset(100, 50, 0, -4, 0x0A00);
    W32(SPR + 0x20, 0x18000);
    W32(SPR + 0x18, 5);
    draw();
    quad(100, 44, 115, 44, 115, 55, 100, 55);
    assert(g_lastSprite.prio == 0x105);
    /* both (0x1800, box halves / statue fragments): 0.5 */
    spr_reset(100, 50, -8, -4, 0x1800);
    W32(SPR + 0x1C, 0x8000); W32(SPR + 0x20, 0x8000);
    draw();
    quad(96, 48, 103, 48, 103, 51, 96, 51);
    /* scaled + H flip: x0 = X - ox*s - w*s */
    spr_reset(100, 50, 2, 0, 0x9000);
    W32(SPR + 0x1C, 0x20000);
    draw();
    quad(64, 50, 95, 50, 95, 57, 64, 57);
    assert(g_lastSprite.flipH);

    /* rotation (0x0400) by 64 (quarter turn) around the anchor:
     * x' = dx cos + dy sin + X, y' = dy cos - dx sin + Y */
    spr_reset(100, 50, -8, -4, 0x0400);
    W16(SPR + 0x14, 0x40);
    draw();
    quad(96, 58, 96, 43, 103, 43, 103, 58);
    /* angle 0 = no change */
    W16(SPR + 0x14, 0);
    draw();
    quad(92, 46, 107, 46, 107, 53, 92, 53);

    /* distorted (0x0004, trap door 0x1003 uses 0x0204): vertices taken as is */
    spr_reset(0, 0, 0, 0, 0x0204);
    int16_t v[8] = { 10, 20, 40, 5, 40, 60, 10, 30 };
    for (int i = 0; i < 8; i++) W16(SPR + 0x24 + 2u * (uint32_t)i, (uint32_t)v[i]);
    W32(SPR + 0x18, -0x20);
    assert(draw() == 1);
    quad(10, 20, 40, 5, 40, 60, 10, 30);
    assert(g_lastSprite.prio == 0xE0 && !g_lastSprite.flipH && g_lastSprite.w == 16);
    /* distorted + part flips -> Dir bits */
    W16(SPR + 0x10, 0x0007);
    draw();
    assert(g_lastSprite.flipH && g_lastSprite.flipV);

    /* draw priority clamp 0..0x1FF */
    spr_reset(100, 50, 0, 0, 0x0200);
    W32(SPR + 0x18, 0x1000); draw(); assert(g_lastSprite.prio == 0x1FF);
    W32(SPR + 0x18, -0x1000); draw(); assert(g_lastSprite.prio == 0);
    W32(SPR + 0x18, 0x7F); draw(); assert(g_lastSprite.prio == 0x17F);
    W16(SPR + 0x10, 0); draw(); assert(g_lastSprite.prio == 0x100);   /* +0x18 ignored */

    /* screen cull: visible iff minX < 320, maxX >= 0, minY < 240, maxY >= 0 */
    spr_reset(-16, 50, 0, 0, 0); assert(draw() == 0);                 /* x -16..-1 */
    spr_reset(-15, 50, 0, 0, 0); assert(draw() == 1);                 /* x -15..0 */
    spr_reset(320, 50, 0, 0, 0); assert(draw() == 0);
    spr_reset(319, 50, 0, 0, 0); assert(draw() == 1);
    spr_reset(100, -8, 0, 0, 0); assert(draw() == 0);
    spr_reset(100, 239, 0, 0, 0); assert(draw() == 1);                /* 224..239 still drawn */
    spr_reset(100, 240, 0, 0, 0); assert(draw() == 0);

    /* colour: bank / priority register / colour-calc bits, explicit CMDCOLR (0x2000) */
    spr_reset(100, 50, 0, 0, 0);
    W16(SPR + 0x0A, 0x123); W16(SPR + 0x0C, 1); W16(SPR + 0x0E, 3);
    draw();
    assert(g_lastSprite.colr == (uint16_t)(3 << 11 | 2 << 13 | 0x123 << 4));
    W16(SPR + 0x0C, 0xFFFF); W16(SPR + 0x0E, 0); draw();
    assert(g_lastSprite.colr == (uint16_t)(0x123 << 4));               /* priority register 0 */
    W16(SPR + 0x10, 0x2000); W16(SPR + 0x12, 0x4567); draw();
    assert(g_lastSprite.colr == 0x4567);
    W16(CHAR_TABLE + CHR * 10 + 6, 0x0ABC);                            /* 0xFFFF -> char default */
    W16(SPR + 0x12, 0xFFFF); draw();
    assert(g_lastSprite.colr == 0x0ABC);
    /* 4bpp LUT: CMDCOLR = LUT base (0x864 = VRAM 0x4320) + n * 4 */
    Char_Register(CHR + 1, 16, 8, 0x08, 0x2000);
    spr_reset(100, 50, 0, 0, 0x2000); W16(SPR, CHR + 1); W16(SPR + 0x12, 3);
    draw();
    assert(g_lastSprite.mode == 0x08 && g_lastSprite.colr == 0x864 + 12);

    /* global zoom 0.5 around (160,112): anchor and size scaled, flag 0x0100 opts out */
    W32(SPR_ZOOM, 0x8000); W32(SPR_ZOOM_X, 160 << 16); W32(SPR_ZOOM_Y, 112 << 16);
    spr_reset(100, 50, -8, -4, 0);
    draw();
    quad(126, 79, 133, 79, 133, 82, 126, 82);
    spr_reset(100, 50, -8, -4, 0x0100);
    draw();
    quad(92, 46, 107, 46, 107, 53, 92, 53);
    /* zoom on top of a sprite's own X scale (2.0 * 0.5 = 1.0) */
    spr_reset(160, 112, -8, -4, 0x1000); W32(SPR + 0x1C, 0x20000);
    draw();
    quad(152, 110, 167, 110, 167, 113, 152, 113);
    W32(SPR_ZOOM, 0x10000);

    /* Scl_SetPriority: sprite registers S0..S3 and NBG0..3 like the 1-1 stage init 0x060472D2 */
    for (uint32_t a = 0x0603B47Cu; a < 0x0603B48Au; a += 2) W16(a, 0);
    Scl_SetPriority(4, 4); Scl_SetPriority(8, 3); Scl_SetPriority(0x10, 2); Scl_SetPriority(0x20, 7);
    Scl_SetPriority(0x100, 3); Scl_SetPriority(0x200, 4); Scl_SetPriority(0x400, 5); Scl_SetPriority(0x800, 7);
    Scl_SetPriority(0x1000, 0); Scl_SetPriority(1, 6);
    assert(R16(0x0603B47Cu) == 0x0403 && R16(0x0603B47Eu) == 0x0705 && R16(0x0603B480u) == 0);
    assert(R16(0x0603B484u) == 0x0304 && R16(0x0603B486u) == 0x0702 && R16(0x0603B488u) == 6);
    Scl_SetPriority(0x10, 2 | 0x100);                                  /* byte argument */
    assert(R16(0x0603B486u) == 0x0702);

    printf("all sprite tests OK\n");
    return 0;
}
