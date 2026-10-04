/* Renderer profiling / regression harness (not part of the test suite).
 *
 * Runs the real main loop (Boot_Frame + Render_BeginFrame/Clear/DrawFrame + Render_Present,
 * like main.c) for a scripted scenario and reports per frame: logic / DrawFrame / Present time
 * and the work the SDL renderer did (textures created / updated / destroyed, MB uploaded,
 * page and character decodes, page checksums). The counters come from link-time wrappers
 * (-Wl,--wrap=...), so the same harness measures any version of render.c.
 *
 *   perf_render ASSETS SCENARIO FRAMES
 *   SCENARIO: boot            Boot_Init, no input (SEGA logo, opening movie, title, demo)
 *             menu            boot, Start on the title, menu cursor down / up, GAME START (movies)
 *             STAGE[:CP]      Boot_DevStartStage(STAGE) (0 = 1-1, 1 = 1-2, 5 = 3-2, 10 = 6-1,
 *                             13 = 7-2, 14 = 8-1), checkpoint CP; Sho walks right, jumps and
 *                             slashes, HP / lives refilled
 *   env PERF_RENDERER=opengl|software|...  (shinobi.ini renderer), PERF_W/PERF_H window size
 *       PERF_HASH=FILE   per frame: FNV-1a of the 320x224 SDL frame (read back) and of the
 *                        soft_render frame (Soft_ComposeFrame of the same sprite list) -> FILE
 *       PERF_PNG=F,F..   also write FILE.F_sdl.png / FILE.F_soft.png at those frames
 *       PERF_EVERY=N     print a line every N frames (phase, counters)
 *       PERF_LOGCREATE=1 print every SDL_CreateTexture (frame, size)
 *       PERF_CRAMLOG=F0,F1  per frame F0..F1: CRAM-changed flag and the CRAM range that changed
 *   RESULT also counts the frames whose shadow CRAM differs from the last flagged state without
 *   the flag set ("unflagged CRAM changes": colours the game wrote without asking for an upload).
 *
 * Build (pc-port/): the CMakeLists.txt sources without src/main.c / src/input.c, plus this file,
 *   -Wl,--wrap=SDL_CreateTexture,--wrap=SDL_DestroyTexture,--wrap=SDL_UpdateTexture,
 *   --wrap=SDL_LockTexture,--wrap=SDL_RenderCopy,--wrap=SDL_RenderGeometry,--wrap=SDL_RenderClear,
 *   --wrap=SDL_SetRenderTarget,--wrap=SDL_RenderReadPixels,--wrap=SDL_RenderFillRect,
 *   --wrap=Vdp2_MapPageChecksum,--wrap=Vdp2_Nbg3Checksum,--wrap=Vdp2_DecodeMapPage,
 *   --wrap=Vdp2_DecodeNbg3,--wrap=Vdp2_DecodePageSel,--wrap=Vdp1_DecodeChar,
 *   --wrap=Render_QueueSprite (+ --wrap=Vdp2_DecodePagePix,--wrap=Vdp1_DecodeCharPal with -DPERF_PIX)
 *   and SDL2; run under xvfb-run for OpenGL, SDL_VIDEODRIVER=offscreen/dummy for software. */
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "boot.h"
#include "config.h"
#include "input.h"
#include "pad.h"
#include "player.h"
#include "ram.h"
#include "render.h"
#include "resource.h"
#include "soft_render.h"
#include "movie.h"
#include "enemy.h"
#include "objhandlers_c.h"

static InputPort fake[2];
const InputPort *Input_GetPort(int p) { return &fake[p]; }
void Input_Update(void) {}
void Input_Shutdown(void) {}
void Input_ApplyConfig(void) {}
void Input_SuppressKey(int sc) { (void)sc; }

/* ---- counters (link-time wrappers): calls and time spent inside ---- */
static struct { long create, destroy, update, lock, copy, geom, sumMap, sumN3, decPage, decN3, decSel, decPix, decChr, clear, target, readpx, fill; double upMB; } C;
enum { T_CREATE, T_UPDATE, T_COPY, T_GEOM, T_SUM, T_DEC, T_CHR, T_CLEAR, T_TARGET, T_READ, T_FILL, T_N };
static const char *kTName[T_N] = { "create", "update", "copy", "geom", "sum", "pagedec", "chrdec", "clear", "target", "readpx", "fill" };
static double T[T_N];
static Uint64 tnow(void) { return SDL_GetPerformanceCounter(); }
#define TIMED(slot, expr) do { Uint64 t_ = tnow(); expr; T[slot] += (double)(tnow() - t_); } while (0)

SDL_Texture *__real_SDL_CreateTexture(SDL_Renderer *r, Uint32 f, int a, int w, int h);
static int s_curFrame, s_logCreate;
SDL_Texture *__wrap_SDL_CreateTexture(SDL_Renderer *r, Uint32 f, int a, int w, int h)
{
    SDL_Texture *t;
    C.create++;
    if (s_logCreate) printf("  create f%d %dx%d\n", s_curFrame, w, h);
    TIMED(T_CREATE, t = __real_SDL_CreateTexture(r, f, a, w, h));
    return t;
}
void __real_SDL_DestroyTexture(SDL_Texture *t);
void __wrap_SDL_DestroyTexture(SDL_Texture *t) { C.destroy++; __real_SDL_DestroyTexture(t); }
int __real_SDL_UpdateTexture(SDL_Texture *t, const SDL_Rect *r, const void *p, int pitch);
int __wrap_SDL_UpdateTexture(SDL_Texture *t, const SDL_Rect *r, const void *p, int pitch)
{
    int w = 0, h = 0, rc;
    if (r) { w = r->w; h = r->h; } else SDL_QueryTexture(t, NULL, NULL, &w, &h);
    C.update++; C.upMB += (double)w * h * 4 / 1048576.0;
    TIMED(T_UPDATE, rc = __real_SDL_UpdateTexture(t, r, p, pitch));
    return rc;
}
int __real_SDL_LockTexture(SDL_Texture *t, const SDL_Rect *r, void **p, int *pitch);
int __wrap_SDL_LockTexture(SDL_Texture *t, const SDL_Rect *r, void **p, int *pitch) { C.lock++; return __real_SDL_LockTexture(t, r, p, pitch); }
int __real_SDL_RenderCopy(SDL_Renderer *r, SDL_Texture *t, const SDL_Rect *s, const SDL_Rect *d);
int __wrap_SDL_RenderCopy(SDL_Renderer *r, SDL_Texture *t, const SDL_Rect *s, const SDL_Rect *d) { int rc; C.copy++; TIMED(T_COPY, rc = __real_SDL_RenderCopy(r, t, s, d)); return rc; }
int __real_SDL_RenderGeometry(SDL_Renderer *r, SDL_Texture *t, const SDL_Vertex *v, int nv, const int *i, int ni);
int __wrap_SDL_RenderGeometry(SDL_Renderer *r, SDL_Texture *t, const SDL_Vertex *v, int nv, const int *i, int ni) { int rc; C.geom++; TIMED(T_GEOM, rc = __real_SDL_RenderGeometry(r, t, v, nv, i, ni)); return rc; }
int __real_SDL_RenderClear(SDL_Renderer *r);
int __wrap_SDL_RenderClear(SDL_Renderer *r) { int rc; C.clear++; TIMED(T_CLEAR, rc = __real_SDL_RenderClear(r)); return rc; }
int __real_SDL_SetRenderTarget(SDL_Renderer *r, SDL_Texture *t);
int __wrap_SDL_SetRenderTarget(SDL_Renderer *r, SDL_Texture *t) { int rc; C.target++; TIMED(T_TARGET, rc = __real_SDL_SetRenderTarget(r, t)); return rc; }
int __real_SDL_RenderReadPixels(SDL_Renderer *r, const SDL_Rect *rc_, Uint32 f, void *p, int pitch);
int __wrap_SDL_RenderReadPixels(SDL_Renderer *r, const SDL_Rect *rc_, Uint32 f, void *p, int pitch) { int rc; C.readpx++; TIMED(T_READ, rc = __real_SDL_RenderReadPixels(r, rc_, f, p, pitch)); return rc; }
int __real_SDL_RenderFillRect(SDL_Renderer *r, const SDL_Rect *rc_);
int __wrap_SDL_RenderFillRect(SDL_Renderer *r, const SDL_Rect *rc_) { int rc; C.fill++; TIMED(T_FILL, rc = __real_SDL_RenderFillRect(r, rc_)); return rc; }
uint32_t __real_Vdp2_MapPageChecksum(int nbg, int pid);
uint32_t __wrap_Vdp2_MapPageChecksum(int nbg, int pid) { uint32_t h; C.sumMap++; TIMED(T_SUM, h = __real_Vdp2_MapPageChecksum(nbg, pid)); return h; }
uint32_t __real_Vdp2_Nbg3Checksum(void);
uint32_t __wrap_Vdp2_Nbg3Checksum(void) { uint32_t h; C.sumN3++; TIMED(T_SUM, h = __real_Vdp2_Nbg3Checksum()); return h; }
void __real_Vdp2_DecodeMapPage(int nbg, int pid, uint32_t *argb);
void __wrap_Vdp2_DecodeMapPage(int nbg, int pid, uint32_t *argb) { C.decPage++; TIMED(T_DEC, __real_Vdp2_DecodeMapPage(nbg, pid, argb)); }
void __real_Vdp2_DecodeNbg3(uint32_t *argb);
void __wrap_Vdp2_DecodeNbg3(uint32_t *argb) { C.decN3++; TIMED(T_DEC, __real_Vdp2_DecodeNbg3(argb)); }
void __real_Vdp2_DecodePageSel(int nbg, int pid, uint32_t *argb, unsigned m, unsigned v);
void __wrap_Vdp2_DecodePageSel(int nbg, int pid, uint32_t *argb, unsigned m, unsigned v) { C.decSel++; TIMED(T_DEC, __real_Vdp2_DecodePageSel(nbg, pid, argb, m, v)); }
#ifdef PERF_PIX
void __real_Vdp2_DecodePagePix(int nbg, int pid, uint16_t *pix, uint32_t *blk);
void __wrap_Vdp2_DecodePagePix(int nbg, int pid, uint16_t *pix, uint32_t *blk) { C.decPix++; TIMED(T_DEC, __real_Vdp2_DecodePagePix(nbg, pid, pix, blk)); }
void __real_Vdp1_DecodeCharPal(const RenderSprite *s, uint32_t *argb, const uint32_t *lut);
void __wrap_Vdp1_DecodeCharPal(const RenderSprite *s, uint32_t *argb, const uint32_t *lut) { C.decChr++; TIMED(T_CHR, __real_Vdp1_DecodeCharPal(s, argb, lut)); }
#endif
void __real_Vdp1_DecodeChar(const RenderSprite *s, uint32_t *argb);
void __wrap_Vdp1_DecodeChar(const RenderSprite *s, uint32_t *argb) { C.decChr++; TIMED(T_CHR, __real_Vdp1_DecodeChar(s, argb)); }

static RenderSprite s_log[4096];
static int s_nlog;
void __real_Render_QueueSprite(const RenderSprite *s);
void __wrap_Render_QueueSprite(const RenderSprite *s) { if (s_nlog < 4096) s_log[s_nlog++] = *s; __real_Render_QueueSprite(s); }

static uint32_t fnv(const uint32_t *p, int n)
{
    uint32_t h = 2166136261u;
    for (int i = 0; i < n; i++) { h = (h ^ (p[i] & 0xFFFFFFu)) * 16777619u; }
    return h;
}

static int in_list(const char *list, int f)
{
    if (!list) return 0;
    for (const char *p = list; *p; ) {
        if (atoi(p) == f) return 1;
        while (*p && *p != ',') p++;
        if (*p) p++;
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 4) { fprintf(stderr, "usage: perf_render ASSETS SCENARIO FRAMES\n"); return 1; }
    const char *assets = argv[1], *scen = argv[2];
    int frames = atoi(argv[3]);
    char p[1200];
    snprintf(p, sizeof p, "%s/A.BIN", assets);
    if (!Ram_LoadImage(p, 0x06004000u)) { fprintf(stderr, "%s missing\n", p); return 1; }
    Res_SetAssetDir(assets);
    Config_SetDefaults(&g_config);
    g_config.vsync = 0;
    if (getenv("PERF_RENDERER")) snprintf(g_config.renderer, sizeof g_config.renderer, "%s", getenv("PERF_RENDERER"));
    for (int i = 0; i < 2; i++) { fake[i].connected = 1; fake[i].raw = 0xFFFF; }
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
    int ww = getenv("PERF_W") ? atoi(getenv("PERF_W")) : 960, wh = getenv("PERF_H") ? atoi(getenv("PERF_H")) : 672;
    SDL_Window *win = SDL_CreateWindow("perf", 0, 0, ww, wh, 0);
    if (!win || !Render_Init(win)) { fprintf(stderr, "window: %s\n", SDL_GetError()); return 1; }
    SDL_Renderer *ren = SDL_GetRenderer(win);

    int stage = -1, cp = -1;
    if (!strcmp(scen, "boot") || !strcmp(scen, "menu")) Boot_Init(0);
    else {
        stage = atoi(scen);
        const char *c = strchr(scen, ':');
        if (c) cp = atoi(c + 1);
        Boot_Init(0);
        Boot_DevStartStage(stage);
    }
    const char *hashPath = getenv("PERF_HASH"), *pngList = getenv("PERF_PNG");
    FILE *hf = hashPath ? fopen(hashPath, "w") : NULL;
    s_logCreate = getenv("PERF_LOGCREATE") != NULL;
    int every = getenv("PERF_EVERY") ? atoi(getenv("PERF_EVERY")) : 0;
    const Uint64 fq = SDL_GetPerformanceFrequency();
    double tLogic = 0, tDraw = 0, tPres = 0, maxDraw = 0;
    long steadyCreate = 0; int steadyFrames = 0, loads = 0, lastMode = -1;
    static uint32_t fb[SCREEN_W * SCREEN_H], sfb[SCREEN_W * SCREEN_H];
    int movieFrames = 0, silentCram = 0, cramLog = 0, cramLog0 = 0, cramLog1 = 0;
    if (getenv("PERF_CRAMLOG")) { cramLog = 1; sscanf(getenv("PERF_CRAMLOG"), "%d,%d", &cramLog0, &cramLog1); }
    for (int f = 0; f < frames; f++) {
        uint16_t held = 0;
        int inGame = Res_OverlayResident() == OVL_SHINOBI && R16(G_GAME_MODE) == 6;
        if (stage >= 0 && inGame) {
            held = PAD_RIGHT;
            if (f % 48 < 14) held |= PAD_C;
            if (f % 10 < 2) held |= PAD_B;
        } else if (!strcmp(scen, "menu")) {
            /* title: Start, menu cursor down / up, Start = GAME START (story + stage-start movies) */
            if (f >= 600 && f < 602) held = PAD_START;
            if (f >= 720 && f < 722) held = PAD_DOWN;
            if (f >= 800 && f < 802) held = PAD_UP;
            if (f >= 900 && f < 902) held = PAD_START;
        }
        fake[0].connected = 1; fake[0].raw = (uint16_t)~held;
        s_nlog = 0; s_curFrame = f;
        Uint64 t0 = SDL_GetPerformanceCounter();
        Boot_Frame();
        Uint64 t1 = SDL_GetPerformanceCounter();
        if (stage >= 0) {
            int m = Res_OverlayResident() == OVL_SHINOBI ? R16(G_GAME_MODE) : 100;
            if (m != lastMode) { if (m == 5 && ++loads == 1 && cp >= 0) W16(G_CHECKPOINT, (uint16_t)cp); lastMode = m; }
            if (inGame) { if (RS16(PLAYER + P_HP) > 0) W16(PLAYER + P_HP, R16(0x060252B0u)); W16(LIVES, 3); }
        }
        if (Res_OverlayResident() == OVL_DIRECTOR && RS32(MOVIE_STATE) >= 0) movieFrames++;
        /* shadow CRAM vs the last flagged upload: changes the game did not flag (0x060250A8) */
        {
            static uint16_t flagged[2048], prev[2048];
            static int init;
            int flag = R16(0x060250A8u) != 0, silent = 0, lo = -1, hi = -1;
            for (int i = 0; i < 2048; i++) {
                uint16_t w = R16(0x06024098u + 2u * (uint32_t)i);
                if (init && !flag && w != flagged[i]) silent = 1;
                if (init && w != prev[i]) { if (lo < 0) lo = i; hi = i; }
                prev[i] = w;
                if (flag || !init) flagged[i] = w;
            }
            init = 1;
            silentCram += silent;
            if (cramLog && f >= cramLog0 && f <= cramLog1)
                printf("  cram f%d flag %d changed %03X..%03X%s\n", f, flag, lo < 0 ? 0 : lo, hi < 0 ? 0 : hi, silent ? " SILENT" : "");
        }
        long c0 = C.create;
        if (hf) Soft_ComposeFrame(sfb, s_log, s_nlog, Vdp2_BackColor());
        Uint64 t2 = SDL_GetPerformanceCounter();
        Render_BeginFrame(); Render_Clear(); Render_DrawFrame();
        Uint64 t3 = SDL_GetPerformanceCounter();
        if (hf) {
            SDL_Rect all = { 0, 0, SCREEN_W, SCREEN_H };
            SDL_RenderReadPixels(ren, &all, SDL_PIXELFORMAT_ARGB8888, fb, SCREEN_W * 4);
            fprintf(hf, "%d %08X %08X\n", f, fnv(fb, SCREEN_W * SCREEN_H), fnv(sfb, SCREEN_W * SCREEN_H));
            if (in_list(pngList, f)) {
                snprintf(p, sizeof p, "%s.%d_sdl.png", hashPath, f); Soft_WritePng(p, fb, SCREEN_W, SCREEN_H);
                snprintf(p, sizeof p, "%s.%d_soft.png", hashPath, f); Soft_WritePng(p, sfb, SCREEN_W, SCREEN_H);
            }
        }
        Uint64 t4 = SDL_GetPerformanceCounter();
        Render_Present();
        Uint64 t5 = SDL_GetPerformanceCounter();
        double d = (double)(t3 - t2) * 1000.0 / fq;
        tLogic += (double)(t1 - t0) * 1000.0 / fq; tDraw += d; tPres += (double)(t5 - t4) * 1000.0 / fq;
        if (d > maxDraw && f >= 60) maxDraw = d;
        if (f >= 60) { steadyCreate += C.create - c0; steadyFrames++; }
        if (every && f % every == every - 1)
            printf("  f%-5d ovl %d mode %d dir %d movie %d x %d | create %ld update %ld up %.0f MB sums %ld dec %ld/%ld/%ld/%ld chr %ld\n", f,
                   Res_OverlayResident(), R16(G_GAME_MODE), (int)R32(G_DIR_SCENE), (int)RS32(MOVIE_STATE),
                   (int32_t)R32(PLAYER + P_X) >> 16, C.create, C.update, C.upMB, C.sumMap + C.sumN3, C.decPage, C.decSel, C.decN3, C.decPix, C.decChr);
    }
    if (hf) fclose(hf);
    printf("TIMES  %-8s ms/frame inside:", scen);
    for (int i = 0; i < T_N; i++) printf(" %s %.2f", kTName[i], T[i] * 1000.0 / fq / frames);
    printf(" | clears %.1f targets %.1f readbacks %.1f fills %.1f per frame\n", (double)C.clear / frames, (double)C.target / frames, (double)C.readpx / frames, (double)C.fill / frames);
    SDL_RendererInfo info; SDL_GetRendererInfo(ren, &info);
    double n = frames;
    printf("RESULT %-8s %-9s %5d fr | logic %.2f draw %.2f present %.2f total %.2f ms (max draw %.1f) | per frame: create %.2f (steady %.2f) destroy %.2f update %.2f (%.2f MB) copy %.0f geom %.0f sums %.1f decodes page %.2f sel %.2f nbg3 %.2f pix %.2f chr %.2f | movie frames %d, unflagged CRAM changes %d\n",
           scen, info.name, frames, tLogic / n, tDraw / n, tPres / n, (tLogic + tDraw + tPres) / n, maxDraw,
           C.create / n, steadyFrames ? (double)steadyCreate / steadyFrames : 0.0, C.destroy / n, C.update / n, C.upMB / n,
           C.copy / n, C.geom / n, (C.sumMap + C.sumN3) / n, C.decPage / n, C.decSel / n, C.decN3 / n, C.decPix / n, C.decChr / n, movieFrames, silentCram);
    Render_Shutdown();
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
