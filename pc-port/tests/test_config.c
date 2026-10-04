/* shinobi.ini parser + screenshot writers (no SDL):
 *   gcc -std=c11 -DCONFIG_NO_SDL -Isrc tests/test_config.c src/config.c src/screenshot.c */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"
#include "screenshot.h"

static int same_binding(const CfgBinding *a, const CfgBinding *b)
{
    if (a->n != b->n) return 0;
    for (int i = 0; i < a->n; i++) if (strcmp(a->name[i], b->name[i])) return 0;
    return 1;
}

int main(void)
{
    Config d, c;
    Config_SetDefaults(&d);
    assert(d.scale == 3 && d.vsync == 1 && !d.fullscreen && !d.bilinear && d.deadzone == 37);
    assert(d.keys[CFG_BTN_START].n == 2 && !strcmp(d.keys[CFG_BTN_START].name[1], "Keypad Enter"));
    assert(!strcmp(d.pad[CFG_BTN_C].name[0], "a") && !strcmp(d.keys[CFG_BTN_A].name[0], "Z"));

    /* the generated default file parses without warnings into exactly the defaults */
    Config_SetDefaults(&c);
    memset(&c.keys, 0, sizeof c.keys);
    memset(&c.pad, 0, sizeof c.pad);
    c.scale = 1; c.vsync = 0; c.deadzone = 50; c.screenshot_png = 0;
    assert(Config_ParseText(&c, Config_DefaultText(), NULL) == 0);
    assert(memcmp(&c, &d, sizeof c) == 0);

    /* robustness: BOM, CRLF, case, spaces, comments, quotes, bad values, unknown keys */
    Config_SetDefaults(&c);
    const char *txt =
        "\xEF\xBB\xBF; comment\r\n"
        "  [VIDEO]  \r\n"
        "Scale=5 ; inline comment\r\n"
        "fullscreen = yes\r\n"
        "integer_scale = on\r\n"
        "vsync = maybe\r\n"                    /* warning, default kept */
        "aspect = 4:3\r\n"
        "bilinear=1#c\r\n"                     /* '#' not after a space: part of the value -> warning */
        "nokey = 1\r\n"                        /* warning */
        "[audio]\n"
        "master_volume = 150\n"                /* clamped to 100, warning */
        "music_volume = 40\n"
        "sfx_volume = -3\n"                    /* clamped to 0, warning */
        "[keyboard]\n"
        "a = Space,  Left Shift ,\"Keypad 0\"\n"
        "b = \";\", \"#\" ; semicolon and hash keys\n"
        "C = none\n"
        "Start =\n"
        "R = 1,2,3,4,5\n"                      /* 5th ignored, warning */
        "[controller]\n"
        "Z = paddle1\n"
        "deadzone = 99\n"                      /* clamped to 95, warning */
        "stick_as_dpad = off\n"
        "Q = a\n"                              /* unknown button, warning */
        "[misc]\n"
        "screenshot_format = BMP\n"
        "[other]\n"                            /* warning, its keys are ignored */
        "scale = 1\n"
        "garbage line\n";                      /* ignored inside an unknown section */
    int w = Config_ParseText(&c, txt, "test");
    assert(w == 9);
    assert(c.scale == 5 && c.fullscreen == 1 && c.integer_scaling == 1 && c.vsync == 1);
    assert(c.aspect43 == 1 && c.bilinear == 0);
    assert(c.master_volume == 100 && c.music_volume == 40 && c.sfx_volume == 0);
    assert(c.keys[CFG_BTN_A].n == 3 && !strcmp(c.keys[CFG_BTN_A].name[0], "Space")
           && !strcmp(c.keys[CFG_BTN_A].name[1], "Left Shift") && !strcmp(c.keys[CFG_BTN_A].name[2], "Keypad 0"));
    assert(c.keys[CFG_BTN_B].n == 2 && !strcmp(c.keys[CFG_BTN_B].name[0], ";") && !strcmp(c.keys[CFG_BTN_B].name[1], "#"));
    assert(c.keys[CFG_BTN_C].n == 0 && c.keys[CFG_BTN_START].n == 0);
    assert(c.keys[CFG_BTN_R].n == 4 && !strcmp(c.keys[CFG_BTN_R].name[3], "4"));
    assert(same_binding(&c.keys[CFG_BTN_UP], &d.keys[CFG_BTN_UP]));     /* untouched */
    assert(c.pad[CFG_BTN_Z].n == 1 && !strcmp(c.pad[CFG_BTN_Z].name[0], "paddle1"));
    assert(c.deadzone == 95 && c.stick_as_dpad == 0 && c.screenshot_png == 0);

    /* [video] renderer: SDL driver names, auto = "" (SDL's choice), bad value keeps the old one */
    Config_SetDefaults(&c);
    assert(c.renderer[0] == 0);
    assert(Config_ParseText(&c, "[video]\nrenderer = Direct3D11\n", NULL) == 0 && !strcmp(c.renderer, "direct3d11"));
    assert(Config_ParseText(&c, "[video]\nrenderer = vulkan\n", NULL) == 1 && !strcmp(c.renderer, "direct3d11"));
    assert(Config_ParseText(&c, "[video]\nrenderer = opengl\n", NULL) == 0 && !strcmp(c.renderer, "opengl"));
    assert(Config_ParseText(&c, "[video]\nrenderer = auto\n", NULL) == 0 && c.renderer[0] == 0);

    /* key outside a section, missing '=', missing ']' */
    Config_SetDefaults(&c);
    assert(Config_ParseText(&c, "scale = 2\n[video\nscale = 2\n", NULL) == 2 && c.scale == 3);
    assert(Config_ParseText(&c, "[video]\nscale 2\nscale = 2x\n", NULL) == 2 && c.scale == 3);

    /* file round trip */
    const char *path = "test_config_tmp.ini";
    assert(Config_WriteDefault(path));
    Config_SetDefaults(&c); c.scale = 6;
    assert(Config_LoadFile(&c, path) == 1 && memcmp(&c, &d, sizeof c) == 0);
    remove(path);
    assert(Config_LoadFile(&c, "does/not/exist.ini") == 0);

    /* screenshot writers: 3x2 image */
    static const uint8_t rgb[3 * 2 * 3] = { 255,0,0, 0,255,0, 0,0,255, 1,2,3, 4,5,6, 7,8,9 };
    assert(Screenshot_WriteBmp("test_shot_tmp.bmp", rgb, 3, 2));
    FILE *f = fopen("test_shot_tmp.bmp", "rb");
    uint8_t b[128];
    size_t n = fread(b, 1, sizeof b, f); fclose(f); remove("test_shot_tmp.bmp");
    assert(n == 54 + 12 * 2 && b[0] == 'B' && b[1] == 'M' && b[28] == 24);
    assert(b[54] == 3 && b[55] == 2 && b[56] == 1);           /* bottom row first, BGR */
    assert(b[66] == 0 && b[67] == 0 && b[68] == 255);         /* top-left red */
    assert(Screenshot_WritePng("test_shot_tmp.png", rgb, 3, 2));
    f = fopen("test_shot_tmp.png", "rb");
    n = fread(b, 1, sizeof b, f); fclose(f); remove("test_shot_tmp.png");
    assert(n == 8 + 25 + 12 + 2 + 5 + 20 + 4 + 12);
    assert(b[1] == 'P' && !memcmp(b + 12, "IHDR", 4) && b[19] == 3 && b[23] == 2 && !memcmp(b + 37, "IDAT", 4));
    assert(b[41] == 0x78 && b[43] == 1 && b[44] == 20 && b[48] == 0 && b[49] == 255);   /* stored block, row 0 */

    printf("all config tests OK\n");
    return 0;
}
