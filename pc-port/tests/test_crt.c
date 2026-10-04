/* CRT filter overlay maths (src/crt.c, SDL-free part) and its shinobi.ini keys (config.c).
 *   gcc -std=c11 -DCRT_NO_SDL -DCONFIG_NO_SDL -Isrc tests/test_crt.c src/crt.c src/config.c src/u8path.c -lm */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "config.h"
#include "crt.h"

static uint8_t rows[4320], rgb[3840 * 3];

int main(void)
{
    assert(strcmp(Crt_ModeName(CRT_OFF), "off") == 0 && strcmp(Crt_ModeName(CRT_SCANLINES), "scanlines") == 0);
    assert(strcmp(Crt_ModeName(CRT_FULL), "crt") == 0 && strcmp(Crt_MaskName(CRT_MASK_SLOT), "slot") == 0);

    /* 3x: one dark row per source line (the last one), the two others lit and equal */
    Crt_ScanlineRows(rows, 672, 224, 100);
    for (int k = 0; k < 224; k++) {
        assert(rows[3 * k] == rows[0] && rows[3 * k + 1] == rows[1] && rows[3 * k + 2] == rows[2]);
        assert(rows[3 * k] == rows[3 * k + 1] && rows[3 * k + 2] < rows[3 * k] - 60);
    }
    /* 4x: periodic, darkest = last row of each line */
    Crt_ScanlineRows(rows, 896, 224, 70);
    for (int y = 4; y < 896; y++) assert(rows[y] == rows[y - 4]);
    assert(rows[3] < rows[0] && rows[3] < rows[1] && rows[3] < rows[2]);
    /* 1x: no room for gaps; strength 0: unchanged */
    Crt_ScanlineRows(rows, 224, 224, 100);
    for (int y = 0; y < 224; y++) assert(rows[y] == 255);
    Crt_ScanlineRows(rows, 1080, 224, 0);
    for (int y = 0; y < 1080; y++) assert(rows[y] == 255);
    /* non-integer 1080p (4.82 rows per line): one dark row per line, never brighter than lit */
    Crt_ScanlineRows(rows, 1080, 224, 70);
    {
        int dark = 0, minv = 255, maxv = 0;
        for (int y = 0; y < 1080; y++) {
            if (rows[y] < minv) minv = rows[y];
            if (rows[y] > maxv) maxv = rows[y];
        }
        for (int y = 1; y + 1 < 1080; y++)
            if (rows[y] < rows[y - 1] && rows[y] <= rows[y + 1]) dark++;      /* local minima */
        assert(dark >= 220 && dark <= 225);
        assert(maxv >= 250 && minv < 200 && minv > 100);
    }
    /* stronger = darker on average */
    {
        long a = 0, b = 0;
        Crt_ScanlineRows(rows, 2160, 224, 30); for (int y = 0; y < 2160; y++) a += rows[y];
        Crt_ScanlineRows(rows, 2160, 224, 90); for (int y = 0; y < 2160; y++) b += rows[y];
        assert(b < a);
    }

    /* masks: none = 255; aperture 2-px magenta/green below 1200 rows, R G B triads above */
    Crt_MaskRow(rgb, 64, 0, 1080, CRT_MASK_NONE, 100);
    for (int i = 0; i < 64 * 3; i++) assert(rgb[i] == 255);
    assert(Crt_MaskPeriod(1080) == 2 && Crt_MaskPeriod(2160) == 3);
    Crt_MaskRow(rgb, 64, 0, 1080, CRT_MASK_APERTURE, 70);
    assert(rgb[0] == 255 && rgb[1] < 255 && rgb[2] == 255);             /* magenta */
    assert(rgb[3] < 255 && rgb[4] == 255 && rgb[5] < 255);              /* green */
    for (int x = 2; x < 64; x++) assert(memcmp(rgb + 3 * x, rgb + 3 * (x - 2), 3) == 0);
    Crt_MaskRow(rgb, 64, 0, 2160, CRT_MASK_APERTURE, 70);
    assert(rgb[0] == 255 && rgb[1] < 255 && rgb[2] < 255);
    assert(rgb[3] < 255 && rgb[4] == 255 && rgb[5] < 255);
    assert(rgb[6] < 255 && rgb[7] < 255 && rgb[8] == 255);
    {   /* slot: one darker row per cell of 4, staggered on odd triads */
        uint8_t a[3 * 6], b[3 * 6];
        int darkRows = 0;
        Crt_MaskRow(a, 6, 0, 2160, CRT_MASK_APERTURE, 70);
        for (int y = 0; y < 4; y++) {
            Crt_MaskRow(b, 6, y, 2160, CRT_MASK_SLOT, 70);
            if (b[0] < a[0]) darkRows++, assert(b[3 * 3] == a[3 * 3]);   /* triad 1 lit there */
        }
        assert(darkRows == 1);
    }

    /* shinobi.ini keys */
    Config c;
    Config_SetDefaults(&c);
    assert(c.crt == 0 && c.crt_strength == 70 && c.crt_mask == CRT_MASK_APERTURE && c.crt_curvature == 0);
    assert(Config_ParseText(&c, Config_DefaultText(), NULL) == 0);
    assert(Config_ParseText(&c, "[video]\ncrt = scanlines\ncrt_strength = 40\ncrt_mask = slot\ncrt_curvature = 15\n", NULL) == 0);
    assert(c.crt == CRT_SCANLINES && c.crt_strength == 40 && c.crt_mask == CRT_MASK_SLOT && c.crt_curvature == 15);
    assert(Config_ParseText(&c, "[video]\ncrt = CRT\ncrt_mask = none\n", NULL) == 0 && c.crt == CRT_FULL && c.crt_mask == 0);
    assert(Config_ParseText(&c, "[video]\ncrt = trinitron\ncrt_strength = 150\ncrt_mask = shadow\n", NULL) == 3);
    assert(c.crt == CRT_FULL && c.crt_strength == 100 && c.crt_mask == 0);
    printf("test_crt: ok\n");
    return 0;
}
