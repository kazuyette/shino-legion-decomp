/* Snapshot transplant (not part of the test suite; see docs/ground_truth.md).
 *
 * Loads memory dumps of the real game taken in BizHawk (tools/bizhawk/snap.sh NAME:
 * NAME_hw.bin HWRAM, NAME_lw.bin LWRAM, NAME_v1.bin VDP1 RAM, NAME_v2.bin VDP2 RAM, NAME_cr.bin
 * CRAM) into the port's emulated memory -- the port keeps the original RAM layout, so the
 * dump is directly the port's state -- and then either
 *   render: composes the frame with soft_render (the VDP1 command list of the dumped VDP1 RAM
 *           becomes the sprite list) -> OUT.png, plus OUT_n0..n3/spr.png (each screen alone)
 *           and OUT_src.bin (320x224 bytes: bit n = screen n (4 = sprites) changes the pixel);
 *   step N PAD: runs N main-loop frames (Boot_Frame) of the port from that state with the pad
 *           held (hex, PAD_* bits of pad.h, or a schedule HEX:count,HEX:count...) and writes OUT_hw.bin / OUT_lw.bin to diff with the
 *           real game's dump N frames later (tools/bizhawk/ramdiff.py); with "png" each frame's
 *           soft_render PNG is written too, with "from=F" HWRAM dumps OUT_<f>_hw.bin of every
 *           frame f >= F (bisecting a divergence: tools/bizhawk/ls_bisect.py).
 *
 *   S=$(cat tests/SOURCES.txt); gcc -std=c11 -O2 -Isrc -Itests tests/snap_compare.c $S -lm -o snap
 *   ./snap ASSETS DUMPPREFIX OUT [render [cram]] | [step N PAD [png]]
 * cram = use the dumped CRAM instead of the game's CRAM shadow 0x06024098. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ram.h"
#include "render.h"
#include "soft_render.h"
#include "resource.h"
#include "stage.h"
#include "stage_hooks.h"
#include "sprite.h"
#include "boot.h"
#include "input.h"
#include "pad.h"

#ifdef SNAP_SDL
/* SDL build (render.c instead of tests/render_stub.c): mode "sdl" draws the transplanted frame
 * with the game's SDL renderer (dummy video driver, software renderer) -> OUT_sdl.png.
 *   gcc -std=c11 -O2 -DSNAP_SDL -Isrc -Itests -I<SDL include> tests/snap_compare.c <SOURCES.txt
 *   without tests/render_stub.c> src/render.c <SDL2 libs> -lm -o snap_sdl */
#include <SDL2/SDL.h>
#include "screenshot.h"
RenderSprite g_spriteLog[4096]; int g_spriteLogCount;
#else
extern RenderSprite g_spriteLog[]; extern int g_spriteLogCount;
#endif
static InputPort fake[2];
const InputPort *Input_GetPort(int p) { return &fake[p]; }

static uint8_t s_cram[0x1000];

static int load(const char *pre, const char *suf, void *dst, size_t n)
{
    char p[1024];
    snprintf(p, sizeof p, "%s_%s.bin", pre, suf);
    FILE *f = fopen(p, "rb");
    if (!f) { printf("missing %s\n", p); return 0; }
    size_t r = fread(dst, 1, n, f);
    fclose(f);
    if (r != n) { printf("%s: %zu of %zu bytes\n", p, r, n); return 0; }
    return 1;
}

static int save(const char *pre, const char *suf, const void *src, size_t n)
{
    char p[1024];
    snprintf(p, sizeof p, "%s_%s.bin", pre, suf);
    FILE *f = fopen(p, "wb");
    if (!f) return 0;
    fwrite(src, 1, n, f);
    fclose(f);
    return 1;
}

/* Which overlay the HWRAM image holds at 0x06040000 (first 4 KB vs the decompressed files). */
static int detect_overlay(const char *assets)
{
    static const char *names[2] = { "DIRECTOR.dec.bin", "SHINOBI.dec.bin" };
    int best = OVL_NONE, bestn = 0;
    for (int k = 0; k < 2; k++) {
        char p[1024];
        uint8_t b[4096];
        snprintf(p, sizeof p, "%s/%s", assets, names[k]);
        FILE *f = fopen(p, "rb");
        if (!f) continue;
        size_t n = fread(b, 1, sizeof b, f);
        fclose(f);
        int m = 0;
        for (size_t i = 0; i < n; i++) m += g_hwram[0x40000 + i] == b[i];
        if (m > bestn) { bestn = m; best = k ? OVL_SHINOBI : OVL_DIRECTOR; }
    }
    return bestn > 3500 ? best : OVL_NONE;
}

/* ---- VDP1 command list (dumped VDP1 RAM) -> RenderSprite list ---- */
static RenderSprite s_spr[4096];
static int s_nspr, s_ncmd[16], s_skipped;

static int16_t sx11(uint32_t a) { int v = R16(a) & 0x7FF; return (int16_t)(v & 0x400 ? v - 0x800 : v); }

static void vdp1_parse(void)
{
    uint32_t a = 0, ret = 0;
    int lx = 0, ly = 0, guard = 0;
    s_nspr = 0; s_skipped = 0;
    memset(s_ncmd, 0, sizeof s_ncmd);
    while (guard++ < 4000) {
        uint32_t c = VDP1_VRAM_BASE + a;
        uint16_t ctrl = R16(c);
        if (ctrl & 0x8000) break;                       /* END */
        int jp = (ctrl >> 12) & 7, cmd = ctrl & 0xF;
        if (!(jp & 4)) {                                /* not skipped */
            s_ncmd[cmd]++;
            if (cmd == 0xA) { lx = sx11(c + 0x0C); ly = sx11(c + 0x0E); }
            else if (cmd <= 3 && s_nspr < 4096) {
                RenderSprite *s = &s_spr[s_nspr];
                memset(s, 0, sizeof *s);
                uint16_t pmod = R16(c + 4), size = R16(c + 0x0A);
                s->src = VDP1_VRAM_BASE + (uint32_t)R16(c + 8) * 8u;
                s->mode = pmod & 0x7F;
                s->colr = R16(c + 6);
                s->w = ((size >> 8) & 0x3F) * 8; s->h = size & 0xFF;
                s->flipH = (ctrl >> 4) & 1; s->flipV = (ctrl >> 5) & 1;
                int xa = sx11(c + 0x0C) + lx, ya = sx11(c + 0x0E) + ly;
                if (cmd == 0) {
                    s->qx[0] = xa; s->qy[0] = ya; s->qx[1] = xa + s->w - 1; s->qy[1] = ya;
                    s->qx[2] = xa + s->w - 1; s->qy[2] = ya + s->h - 1; s->qx[3] = xa; s->qy[3] = ya + s->h - 1;
                } else if (cmd == 1) {
                    int zp = (ctrl >> 8) & 0xF, x0, y0, x1, y1;
                    if (zp == 0) { x0 = xa; y0 = ya; x1 = sx11(c + 0x14) + lx; y1 = sx11(c + 0x16) + ly; }
                    else {
                        int w = sx11(c + 0x10), h = sx11(c + 0x12);
                        int hx = zp & 3, vy = (zp >> 2) & 3;   /* 1 left/top, 2 centre, 3 right/bottom */
                        x0 = hx == 1 ? xa : hx == 2 ? xa - w / 2 : xa - w;
                        y0 = vy == 1 ? ya : vy == 2 ? ya - h / 2 : ya - h;
                        x1 = x0 + w; y1 = y0 + h;
                    }
                    s->qx[0] = x0; s->qy[0] = y0; s->qx[1] = x1; s->qy[1] = y0;
                    s->qx[2] = x1; s->qy[2] = y1; s->qx[3] = x0; s->qy[3] = y1;
                } else {
                    for (int k = 0; k < 4; k++) { s->qx[k] = sx11(c + 0x0C + 4u * (uint32_t)k) + lx; s->qy[k] = sx11(c + 0x0E + 4u * (uint32_t)k) + ly; }
                }
                s->x = s->qx[0]; s->y = s->qy[0];
                s->prio = s_nspr;
                s_nspr++;
            } else if (cmd >= 4 && cmd <= 7) s_skipped++;   /* polygons / lines: not drawn */
        }
        switch (jp & 3) {
        case 0: a += 0x20; break;
        case 1: a = (uint32_t)R16(c + 2) * 8u; break;
        case 2: ret = a + 0x20; a = (uint32_t)R16(c + 2) * 8u; break;
        case 3: a = ret; break;
        }
        a &= VDP1_VRAM_SIZE - 1;
    }
}

static void use_cram(void)
{
    /* CRAM mode 1 (2048 x RGB555) = the 4 KB shadow 0x06024098 */
    memcpy(Ram_Ptr(SHADOW_CRAM), s_cram, sizeof s_cram);
}

static uint32_t back_colour(void) { return Vdp2_BackColor(); }

static void render(const char *out, int cram)
{
    static uint32_t fb[SCREEN_W * SCREEN_H], fb2[SCREEN_W * SCREEN_H];
    static uint8_t srcmap[SCREEN_W * SCREEN_H];
    char p[1024];
    if (cram) use_cram();
    vdp1_parse();
    uint32_t back = back_colour();
    Vdp2Prio pr;
    Vdp2_GetPriorities(&pr);
    printf("overlay %d  TVMD %04X  back %06X  sptype %d  prio N0..3 %d %d %d %d  S0..7 %d%d%d%d%d%d%d%d\n",
           Res_OverlayResident(), R16(0x0603A570u), back & 0xFFFFFF, pr.sptype, pr.nbg[0], pr.nbg[1], pr.nbg[2], pr.nbg[3],
           pr.spr[0], pr.spr[1], pr.spr[2], pr.spr[3], pr.spr[4], pr.spr[5], pr.spr[6], pr.spr[7]);
    printf("VDP1: %d sprites (normal %d scaled %d distorted %d) polygons/lines skipped %d, local %d\n",
           s_nspr, s_ncmd[0], s_ncmd[1], s_ncmd[2] + s_ncmd[3], s_skipped, s_ncmd[10]);
    for (int l = 0; l < 4; l++) {
        int sx, sy;
        Vdp2_LayerScroll(l, &sx, &sy);
        printf("NBG%d enabled %d scroll %d,%d\n", l, Vdp2_LayerEnabled(l), sx, sy);
    }
    printf("CLOFEN %04X CLOFSL %04X COA %04X %04X %04X  line DMA %04X\n", R16(0x0603B49Au), R16(0x0603B49Cu),
           R16(0x0603B49Eu), R16(0x0603B4A0u), R16(0x0603B4A2u), LineTable_DmaActive());
    Soft_ComposeFrame(fb, s_spr, s_nspr, back);
    snprintf(p, sizeof p, "%s.png", out);
    Soft_WritePng(p, fb, SCREEN_W, SCREEN_H);
    /* contribution of each screen: render without it */
    memset(srcmap, 0, sizeof srcmap);
    uint16_t prin[2] = { R16(VDP2_SHADOW_ADDR + 0x18), R16(VDP2_SHADOW_ADDR + 0x1A) };
    for (int l = 0; l < 5; l++) {
        if (l < 4) {
            W16(VDP2_SHADOW_ADDR + 0x18 + 2u * (uint32_t)(l >> 1), prin[l >> 1] & ~(7u << (8 * (l & 1))));
            Soft_ComposeFrame(fb2, s_spr, s_nspr, back);
            W16(VDP2_SHADOW_ADDR + 0x18, prin[0]); W16(VDP2_SHADOW_ADDR + 0x1A, prin[1]);
        } else Soft_ComposeFrame(fb2, s_spr, 0, back);
        for (int i = 0; i < SCREEN_W * SCREEN_H; i++) if (fb2[i] != fb[i]) srcmap[i] |= (uint8_t)(1 << l);
    }
    snprintf(p, sizeof p, "%s_src.bin", out);
    FILE *f = fopen(p, "wb");
    if (f) { fwrite(srcmap, 1, sizeof srcmap, f); fclose(f); }
    /* each screen alone on black */
    for (int l = 0; l < 5; l++) {
        for (int k = 0; k < 4; k++)
            if (k != l) W16(VDP2_SHADOW_ADDR + 0x18 + 2u * (uint32_t)(k >> 1), R16(VDP2_SHADOW_ADDR + 0x18 + 2u * (uint32_t)(k >> 1)) & ~(7u << (8 * (k & 1))));
        Soft_ComposeFrame(fb2, s_spr, l == 4 ? s_nspr : 0, 0xFF000000u);
        W16(VDP2_SHADOW_ADDR + 0x18, prin[0]); W16(VDP2_SHADOW_ADDR + 0x1A, prin[1]);
        if (l < 4) snprintf(p, sizeof p, "%s_n%d.png", out, l); else snprintf(p, sizeof p, "%s_spr.png", out);
        Soft_WritePng(p, fb2, SCREEN_W, SCREEN_H);
    }
}

int main(int argc, char **argv)
{
    if (argc < 4) { printf("usage: snap ASSETS DUMPPREFIX OUT [render [cram]] | [step N PAD [png]]\n"); return 2; }
    const char *assets = argv[1], *pre = argv[2], *out = argv[3];
    const char *mode = argc > 4 ? argv[4] : "render";
    Res_SetAssetDir(assets);
    if (!load(pre, "hw", g_hwram, HWRAM_SIZE) || !load(pre, "lw", g_lwram, LWRAM_SIZE) ||
        !load(pre, "v1", g_vdp1vram, VDP1_VRAM_SIZE) || !load(pre, "v2", g_vdp2vram, VDP2_VRAM_SIZE) ||
        !load(pre, "cr", s_cram, sizeof s_cram)) return 1;
    Res_SetOverlayResident(detect_overlay(assets));
    /* The dumps are taken between VBlank-IN and VBlank-OUT (0x0603A4A4 = 1, 0x0601C49C = 0): the
     * HBlank DMA tables (0x0601BC58) were converted at VBlank-IN and VBlank-OUT 0x060051C8 arms
     * the DMA for the next frame when LINE_FLAGS bit 15 is set. */
    LineTable_SetDmaActive((R16(LINE_FLAGS) & 0x8000) ? R16(LINE_FLAGS) : 0);
#ifdef SNAP_SDL
    if (!strcmp(mode, "sdl")) {
        SDL_setenv("SDL_VIDEODRIVER", "dummy", 0);         /* keep a driver given by the caller (x11 under xvfb-run: GL renderer) */
        if (SDL_Init(SDL_INIT_VIDEO) != 0) { printf("SDL_Init: %s\n", SDL_GetError()); return 1; }
        SDL_Window *w = SDL_CreateWindow("snap", 0, 0, SCREEN_W, SCREEN_H, 0);
        if (!w || !Render_Init(w)) { printf("no SDL renderer\n"); return 1; }
        vdp1_parse();
        for (int i = 0; i < s_nspr; i++) Render_QueueSprite(&s_spr[i]);
        Render_BeginFrame();
        Render_Clear();
        Render_DrawFrame();
        static uint32_t px[SCREEN_W * SCREEN_H];
        SDL_Renderer *r = SDL_GetRenderer(w);
        SDL_Rect all = { 0, 0, SCREEN_W, SCREEN_H };
        if (SDL_RenderReadPixels(r, &all, SDL_PIXELFORMAT_ARGB8888, px, SCREEN_W * 4) != 0) { printf("read: %s\n", SDL_GetError()); return 1; }
        char p[1024];
        snprintf(p, sizeof p, "%s_sdl.png", out);
        Soft_WritePng(p, px, SCREEN_W, SCREEN_H);
        printf("SDL frame: %d sprites -> %s\n", s_nspr, p);
        return 0;
    }
#endif
    if (!strcmp(mode, "render")) {
        render(out, argc > 5 && !strcmp(argv[5], "cram"));
        return 0;
    }
    if (!strcmp(mode, "step") && argc > 6) {
        /* PAD: "HEX" held for all N frames, or a schedule "HEX:count,HEX:count,..." (the last
         * one repeats). The real game reads the pad at VBlank-OUT from the SMPC data fetched
         * during the previous frame, so what BizHawk holds during frame f is seen by the logic of
         * frame f + 1: the port's frame 0 gets the pad held before the dump (none). */
        int n = atoi(argv[5]);
        static uint16_t sched[100000];
        int ns = 0;
        for (const char *q = argv[6]; *q && ns < n; ) {
            char *e;
            uint16_t v = (uint16_t)strtoul(q, &e, 16);
            int cnt = n;
            if (*e == ':') cnt = (int)strtol(e + 1, &e, 10);
            for (int k = 0; k < cnt && ns < n; k++) sched[ns++] = v;
            q = *e == ',' ? e + 1 : e;
        }
        while (ns < n) { sched[ns] = ns ? sched[ns - 1] : 0; ns++; }
        int png = argc > 7 && !strcmp(argv[7], "png");
        int from = argc > 7 && !strncmp(argv[7], "from=", 5) ? atoi(argv[7] + 5) : -1;   /* OUT_<f>_hw.bin from frame f */
        static uint32_t fb[SCREEN_W * SCREEN_H];
        char p[1024];
        fake[0].connected = 1;
        for (int f = 0; f < n; f++) {
            uint16_t held = f ? sched[f - 1] : 0;
            fake[0].raw = (uint16_t)~held;
            g_spriteLogCount = 0;
            Boot_Frame();
            if (png) {
                Soft_ComposeFrame(fb, g_spriteLog, g_spriteLogCount, back_colour());
                snprintf(p, sizeof p, "%s_%03d.png", out, f + 1);
                Soft_WritePng(p, fb, SCREEN_W, SCREEN_H);
            }
            if (from >= 0 && f + 1 >= from) {
                snprintf(p, sizeof p, "%s_%d", out, f + 1);
                save(p, "hw", g_hwram, HWRAM_SIZE);
            }
        }
        save(out, "hw", g_hwram, HWRAM_SIZE);
        save(out, "lw", g_lwram, LWRAM_SIZE);
        save(out, "v2", g_vdp2vram, VDP2_VRAM_SIZE);
        printf("stepped %d frames, overlay %d\n", n, Res_OverlayResident());
        return 0;
    }
    printf("bad mode\n");
    return 2;
}
