/* SHINOBI.PPB in-game HUD (status bar), see hud.h / docs/hud.md.
 * Numbers and bars are cells of the NBG3 map (palette = pattern-name bits 22-16, characters
 * from 0x2C00 = last 32 KB of the stage CHR); the HP-segment ticks and the ninjutsu icon are
 * VDP1 sprites (chars 0x4DD / 0x4E5). */
#include "hud.h"
#include "ram.h"
#include "sprite.h"
#include "player.h"
#include "stage.h"

#define HP_MAX      0x060252B0u
#define ONEUP_TOKS  0x060252B2u
#define SHURIKENS   0x060252B6u
#define LIVES_CNT   0x060252B8u
#define MAGIC_AVAIL 0x060252BEu

/* A.BIN 0x060092BC (__divsi3): division by zero returns 0 (and sets an error word) */
static int32_t sdiv(int32_t a, int32_t b) { return b ? a / b : 0; }

/* 0x06061D28: one digit = 2 cells (top at row y, bottom at row y+1) */
void Hud_PutDigit(int16_t x, int16_t y, int16_t digit, uint16_t attr)
{
    uint32_t cx = (uint32_t)(x & 63), cy = (uint32_t)(y & 63);
    uint32_t t = HUD_DIGITS_TBL + (uint32_t)((int32_t)digit * 4);
    uint32_t hi = (uint32_t)attr << 16;
    W32(HUD_NBG3_PAGE + cy * 256u + cx * 4u, R16(t) | hi | 0x2C00u);
    W32(HUD_NBG3_PAGE + (cy + 1u) * 256u + cx * 4u, R16(t + 2) | hi | 0x2C00u);
}

/* 0x06061D8E */
void Hud_DrawNumber(int16_t x, int16_t y, int32_t value, uint16_t digits, uint16_t flags)
{
    uint16_t dg[8] = {0};
    int started = 0;
    uint32_t v = (uint32_t)value, m = v % 10u;            /* unsigned div/mod 0x0600CEF4 / 0x0600A55C */
    dg[0] = (uint16_t)m;
    v -= m;
    uint32_t p = 10;
    for (int i = 1; i < (int)digits && i < 8; i++) {
        uint32_t q = p * 10u, r = v % q;
        dg[i] = (uint16_t)(r / p);
        v -= r;
        p = q;
    }
    int k = (int)digits - 1;                              /* most significant digit first */
    int left = digits;
    if ((flags & 2) && !(flags & 1)) {                    /* left-aligned, blanks after */
        uint16_t attr = (flags & 4) ? 8 : 0;
        for (int i = (int)digits - 1; i >= 0; i--, k--) {
            if (!started) {
                started = dg[k] != 0;
                if (!started && i != 0) continue;         /* leading zero: not drawn */
            }
            Hud_PutDigit(x++, y, (int16_t)dg[k], attr);
            left--;
        }
        while (left-- > 0) Hud_PutDigit(x++, y, 10, 0);
        return;
    }
    int i = (int)digits - 1;                              /* right-aligned */
    while (i >= 0) {
        if (flags & 1) break;                             /* zero-padded */
        if (dg[k] != 0) break;
        if (i == 0) break;
        Hud_PutDigit(x++, y, 10, 0);                      /* leading zero -> blank */
        i--; k--;
    }
    uint16_t attr = (flags & 4) ? 8 : 0;
    for (; i >= 0; i--, k--) Hud_PutDigit(x++, y, (int16_t)dg[k], attr);
}

/* 0x06061C6E: 12 two-row cells of 8 units (fill 0..95) + the end cap (cells 0x25/0x35) */
void Hud_DrawBar(int16_t x, int16_t y, int16_t fill, int16_t colour)
{
    int32_t f = fill > 95 ? 95 : fill;
    if (colour > 3) colour = 3;
    if (colour < 0) colour = 0;
    uint32_t a = HUD_NBG3_PAGE + (uint32_t)(y & 63) * 256u + (uint32_t)(x & 63) * 4u;
    uint32_t tbl = R32(HUD_BAR_TBLS + (uint32_t)colour * 4u);
    for (int n = 0; n < 12; n++, a += 4) {
        int32_t part;
        if (f >= 8) { part = 8; f -= 8; } else { part = f; f = 0; }
        W32(a, R16(tbl + (uint32_t)(part * 4)) | 0x2C00u);
        W32(a + 0x100, R16(tbl + (uint32_t)(part * 4 + 2)) | 0x2C00u);
    }
    W32(a, 0x2C25u);
    W32(a + 0x100, 0x2C35u);
}

/* fill 0..95 and colour (3 = almost empty .. 0 = full) of the bar for value/max */
static void bar_params(int32_t value, int32_t max, int16_t *fill, int16_t *colour)
{
    int32_t f = sdiv(value << 8, max);
    f = sdiv(f * 95 + 0x80, 0x100);
    *fill = (int16_t)f;
    *colour = (int16_t)(3 - sdiv(f, 23));
}

/* 0x06061A56: player life bar, row 1 from column 6 */
void Hud_DrawLifeBar(void)
{
    int32_t hp = RS16(PLAYER + P_HP);
    if (hp < 0) hp = 0;
    int16_t fill, colour;
    bar_params(hp, RS16(HP_MAX), &fill, &colour);
    Hud_DrawBar(6, 1, fill, colour);
}

/* 0x06061B58: boss life bar (row 3) + its label (rows 3-4, columns 1-5). Stage index 17 has
 * no bar and another last label column. */
void Hud_DrawBossBar(int16_t max, int16_t value)
{
    int32_t v = value;
    if (v < 0) v = 0;
    if (v > max) v = max;
    int16_t fill, colour;
    bar_params(v, max, &fill, &colour);
    int st17 = RS16(G_STAGE_INDEX) == 17;
    if (!st17) Hud_DrawBar(6, 3, fill, colour);
    uint32_t a = HUD_NBG3_PAGE + 3u * 256u + 1u * 4u;     /* 0x25E6C304 */
    for (uint32_t c = 0; c < 4; c++, a += 4) {
        W32(a + 0x100, R16(HUD_LABEL_TBL + 10u + 2u * c) | 0x2C00u);
        W32(a, R16(HUD_LABEL_TBL + 2u * c) | 0x2C00u);
    }
    if (st17) {
        W32(a + 0x100, R16(HUD_LABEL_TBL17 + 2u) | 0x2C00u);
        W32(a, R16(HUD_LABEL_TBL17) | 0x2C00u);
    } else {
        W32(a + 0x100, R16(HUD_LABEL_TBL + 18u) | 0x2C00u);
        W32(a, R16(HUD_LABEL_TBL + 8u) | 0x2C00u);
    }
}

/* 0x06061AC0: a tick sprite (char 0x4DD) on the life bar between HP units: x = 48 + i*95/max */
void Hud_DrawHpTicks(void)
{
    const uint32_t s = SPR_WORK;
    int32_t step = sdiv(0x005F0000, RS16(HP_MAX)), acc = step;
    for (int16_t i = 1; i < RS16(HP_MAX); i++, acc += step) {
        W16(s + 0x10, 0x300);
        W16(s + 0x0E, 0);
        W32(s + 0x18, 0xFF);
        W16(s + 2, 0); W16(s + 4, 0);
        W16(s + 0x0C, 2);
        W16(s + 0, 0x4DD);
        W16(s + 6, (uint16_t)((int16_t)((uint32_t)acc >> 16) + 48));
        W16(s + 8, 12);
        W16(s + 0x0A, 112);
        Sprite_Draw(s);                                   /* 0x060068C0 */
    }
}

/* 0x060632D8: ninjutsu-available icon (char 0x4E5 at 152,8) */
void Hud_DrawMagicIcon(void)
{
    if (R16(MAGIC_AVAIL) == 0) return;
    const uint32_t s = SPR_WORK;
    W16(s, 0x4E5); W16(s + 2, 0x98); W16(s + 4, 8);
    W16(s + 6, 0); W16(s + 8, 0); W16(s + 0x0A, 123);
    W16(s + 0x10, 0x300); W32(s + 0x18, 0xFF);
    W16(s + 0x0C, 2); W16(s + 0x0E, 0);
    Sprite_Draw(s);
}

/* 0x06061A10 */
void Hud_Update(void)
{
    Hud_DrawLifeBar();
    Hud_DrawNumber(25, 1, RS16(SHURIKENS), 3, 2);
    Hud_DrawNumber(32, 1, RS16(ONEUP_TOKS), 1, 2);
    Hud_DrawNumber(37, 1, RS16(LIVES_CNT), 2, 2);
    Hud_DrawHpTicks();
    Hud_DrawMagicIcon();                                  /* tail call */
}
