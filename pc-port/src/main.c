#include "audio.h"
#include "u8path.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "boot.h"
#include "config.h"
#include "render.h"
#include "debug_menu.h"
#include "extract.h"
#include "input.h"
#include "ram.h"
#include "resource.h"
#include "shinobi.h"
#include "version.h"

/* PC host: SDL window, input, frame pacing. The game itself is A.BIN's main loop
 * (boot.c: Boot_Init + one Boot_Frame per frame), which runs whichever overlay is resident
 * at 0x06040000: DIRECTOR.PPB (logo, title, movies, tally...) or SHINOBI.PPB (stages).
 *
 * Options:
 *   --version       print the version and exit
 *   --assets DIR    asset directory (default: "assets" in the working directory when it
 *                   holds A.BIN, else "assets" next to the executable)
 *   --extract IMG   build the asset directory from the player's disc image (.cue, .bin or
 *                   .iso, extract.c), console progress; exit 0 = ok, 2 = ok without music, 1 = error
 *   IMG             (a .cue/.bin/.iso as first argument = file dropped on the exe) extract, then play
 *   --no-asset-check  skip the startup check of the asset directory against the manifest
 *   --skip-intro    no SEGA logo at boot (as on the Saturn, where the BIOS shows it)
 *   F1 / pad Back   PC debug/cheat menu (lives, energy, shurikens, ninjutsu, stage clear/select)
 *   --stage N       development: boot straight into stage index N (0 = game 1-1 = STAGE7_1,
 *                   ... 12 = 7-1 = STAGE1_1, 17 = 9-2) with new-game variables
 *   --verbose       log player action changes
 *   --config FILE   settings file (default: shinobi.ini next to the executable, created
 *                   with commented defaults when missing; see config.c / README)
 *   Alt+Enter / F11 fullscreen on/off, F12 screenshot (screenshots/ next to the executable),
 *   Shift+F12 screenshot as shown (output size, CRT filter), F10 CRT filter off/scanlines/crt.
 *   Esc is not a quit key: closing the window quits. */
static void usage(const char *exe)
{
    printf("usage: %s [--assets DIR] [--config FILE] [--skip-intro] [--stage N] [--verbose] [--version]\n"
           "       %s --extract DISC.cue|.bin|.iso [--assets DIR]   (build the game files from your disc)\n"
           "       %s DISC.cue|.bin|.iso                            (same, then play)\n", exe, exe, exe);
}

/* ---- PC: game files from the player's own disc image (extract.c) ---------------------------- */

static void show_box(Uint32 flags, const char *text)
{
    if (SDL_ShowSimpleMessageBox(flags, "Shinobi Legions", text, NULL) != 0)
        fprintf(stderr, "%s\n", text);
}

static int ask_yes_no(const char *text)
{
    static const SDL_MessageBoxButtonData buttons[] = {
        { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Yes" },
        { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "No" },
    };
    const SDL_MessageBoxData box = {
        SDL_MESSAGEBOX_INFORMATION | SDL_MESSAGEBOX_BUTTONS_LEFT_TO_RIGHT, NULL, "Shinobi Legions", text,
        SDL_arraysize(buttons), buttons, NULL
    };
    int hit = 0;
    if (SDL_ShowMessageBox(&box, &hit) != 0) {          /* no display: ask on the console */
        char line[16];
        printf("%s [y/N] ", text);
        fflush(stdout);
        return fgets(line, sizeof line, stdin) && (line[0] == 'y' || line[0] == 'Y');
    }
    return hit == 1;
}

static int s_extractPct = -1;
static void extract_progress(const char *msg, int pct)
{
    if (pct == s_extractPct) return;
    s_extractPct = pct;
    printf("\r[extract] %3d%%  %-24.24s", pct, msg);
    fflush(stdout);
}

/* Returns the Extract_FromCue code; gui = report the result in a message box too. */
static int run_extract(const char *image, const char *assets, int gui)
{
    char text[1600];
    printf("[extract] %s -> %s\n", image, assets);
    s_extractPct = -1;
    int rc = Extract_FromCue(image, assets, extract_progress);
    printf("\n[extract] %s: %s\n", Extract_ResultName(rc), Extract_LastError());
    if (gui && rc < 0) {
        snprintf(text, sizeof text, "Could not extract the game files from\n%s\n\n%s.\n%s",
                 image, Extract_ResultName(rc), Extract_LastError());
        show_box(SDL_MESSAGEBOX_ERROR, text);
    } else if (gui && rc > 0) {
        snprintf(text, sizeof text, "Game files extracted, but:\n%s", Extract_LastError());
        show_box(SDL_MESSAGEBOX_WARNING, text);
    }
    return rc;
}

/* "assets" in the working directory when it holds A.BIN (development), else next to the exe. */
static void default_assets(char *out, size_t n)
{
    FILE *f = u8_fopen("assets/A.BIN", "rb");
    if (f) { fclose(f); snprintf(out, n, "assets"); return; }
    snprintf(out, n, "%sassets", Config_BaseDir());
}

/* Startup: assets incomplete -> offer the *.cue next to the exe (or in the working directory),
 * else explain how to provide one. 1 = play, 0 = quit (declined / no image), -1 = failed. */
static int ensure_assets(const char *assets)
{
    char bad[64], image[1100], text[1600];
    int missing = Extract_CheckAssets(assets, bad, sizeof bad);
    if (!missing) return 1;
    printf("[assets] %s: %d game file(s) missing or of the wrong size (first: %s)\n", assets, missing, bad);
    if (Extract_FindImage(Config_BaseDir(), image, sizeof image) || Extract_FindImage("", image, sizeof image)) {
        snprintf(text, sizeof text, "The game files are missing.\n\nExtract them now from your disc image\n%s ?\n\n"
                 "(about 400 MB in %s, takes a minute)", image, assets);
        if (!ask_yes_no(text)) return 0;
        return run_extract(image, assets, 1) >= 0 ? 1 : -1;
    }
    snprintf(text, sizeof text,
             "Game files not found (%s).\n\n"
             "This program needs your own Shinobi Legions (USA) Sega Saturn disc image:\n"
             "- put its .cue and .bin files next to the program and start it again, or\n"
             "- drag the .cue (or .bin / .iso) file onto the program.\n\n"
             "See README.txt.", assets);
    show_box(SDL_MESSAGEBOX_INFORMATION, text);
    return 0;
}

int main(int argc, char **argv)
{
    u8_console_init();                       /* Windows: UTF-8 console output */
    const char *assetsArg = NULL, *extractImg = NULL, *droppedImg = NULL;
    const char *configPath = NULL;
    int stage = -1, skipIntro = 0, assetCheck = 1;
    char assets[1100];

    printf("%s\n", SL_VERSION_FULL);
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--version") == 0) return 0;
        else if (strcmp(argv[i], "--assets") == 0 && i + 1 < argc) assetsArg = argv[++i];
        else if (strcmp(argv[i], "--extract") == 0 && i + 1 < argc) extractImg = argv[++i];
        else if (strcmp(argv[i], "--no-asset-check") == 0) assetCheck = 0;
        else if (argv[i][0] != '-' && Extract_IsImagePath(argv[i])) {   /* dropped on the exe */
            size_t l = strlen(argv[i]);                 /* several files dropped: prefer the .cue */
            if (!droppedImg || (l > 4 && (strcmp(argv[i] + l - 4, ".cue") == 0 || strcmp(argv[i] + l - 4, ".CUE") == 0)))
                droppedImg = argv[i];
        }
        else if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) configPath = argv[++i];
        else if (strcmp(argv[i], "--stage") == 0 && i + 1 < argc) stage = atoi(argv[++i]);
        else if (strcmp(argv[i], "--skip-intro") == 0) skipIntro = 1;
        else if (strcmp(argv[i], "--verbose") == 0) g_shinobiVerbose = 1;
        else { usage(argv[0]); return 1; }
    }
    if (stage > 17) { fprintf(stderr, "--stage: index 0..17\n"); return 1; }
    if (assetsArg) snprintf(assets, sizeof assets, "%s", assetsArg);
    else if (extractImg || droppedImg) snprintf(assets, sizeof assets, "%sassets", Config_BaseDir());
    else default_assets(assets, sizeof assets);

    if (extractImg) {                                   /* command-line extraction only */
        int rc = run_extract(extractImg, assets, 0);
        return rc < 0 ? 1 : rc > 0 ? 2 : 0;
    }
    if (droppedImg && run_extract(droppedImg, assets, 1) < 0) return 1;
    if (assetCheck) {
        int ok = ensure_assets(assets);
        if (ok <= 0) return ok < 0 ? 1 : 0;
    }

    /* The BIOS loads A.BIN at 0x06004000 and jumps to it. */
    char p[1200];
    snprintf(p, sizeof p, "%s/A.BIN", assets);
    if (Ram_LoadImage(p, 0x06004000u)) printf("[ram] A.BIN loaded\n");
    else fprintf(stderr, "[ram] %s missing: nothing will run (see README, Assets)\n", p);
    Res_SetAssetDir(assets);

    Config_LoadOrCreate(configPath);                    /* shinobi.ini -> g_config */
    const Config *cfg = Config_Get();

    SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");   /* sharp pixels at 125/150 % */
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    /* Window = scale x 320x224 (x 320x240 in 4:3), reduced to fit the desktop; resizable. */
    int scale = cfg->scale, winH = cfg->aspect43 ? SCREEN_W * 3 / 4 : SCREEN_H;
    SDL_Rect desk;
    if (SDL_GetDisplayUsableBounds(0, &desk) == 0)
        while (scale > 1 && (SCREEN_W * scale > desk.w || winH * scale + 32 > desk.h)) scale--;
    Uint32 wflags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | (cfg->fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    SDL_Window *window = SDL_CreateWindow(SL_VERSION_FULL, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                          SCREEN_W * scale, winH * scale, wflags);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_SetWindowMinimumSize(window, SCREEN_W, SCREEN_H);
    Render_Init(window);
    Audio_Init();                                      /* SDL audio: CD-DA tracks, movie sound */
    Audio_SetVolumes(cfg->master_volume, cfg->music_volume, cfg->sfx_volume);
    Input_ApplyConfig();                               /* [keyboard] / [controller] bindings */

    Boot_Init(!skipIntro);                   /* A.BIN 0x06004038: queues DIRECTOR.PPB */
    if (stage >= 0) Boot_DevStartStage(stage);

    /* Drift-free 60 Hz pacing on the high-resolution counter (the old integer 16 ms step
     * ran the game at 62.5 fps): sleep off the remainder of each frame. */
    const Uint64 freq = SDL_GetPerformanceFrequency();
    const Uint64 frame_ticks = freq / 60;
    Uint64 next_frame = SDL_GetPerformanceCounter();
    int running = 1;
    SDL_Event ev;
    while (running) {
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = 0;
            if (ev.type == SDL_KEYDOWN && !ev.key.repeat) {        /* PC hotkeys */
                SDL_Scancode sc = ev.key.keysym.scancode;
                if (sc == SDL_SCANCODE_F11 || ((sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER)
                                               && (ev.key.keysym.mod & KMOD_ALT))) {
                    Render_ToggleFullscreen();
                    Input_SuppressKey(sc);   /* Alt+Enter must not press Start too */
                    continue;
                }
                if (sc == SDL_SCANCODE_F12) {           /* Shift+F12: as shown (CRT filter) */
                    if (ev.key.keysym.mod & KMOD_SHIFT) Render_RequestScreenshotOutput();
                    else Render_RequestScreenshot();
                    continue;
                }
                if (sc == SDL_SCANCODE_F10) { Render_CycleCrt(); continue; }   /* CRT filter */
            }
            DebugMenu_HandleEvent(&ev);      /* PC debug/cheat menu: F1 or pad Back */
        }

        int paused = DebugMenu_IsOpen();
        Render_SetKeepQueue(paused);
        if (!paused) {
            Input_Update();                  /* SMPC report for Pad_Update */
            DebugMenu_BeforeFrame();
            Boot_Frame();                    /* one iteration of the A.BIN main loop */
        }

        Render_BeginFrame();                 /* 320x224 frame target, scaled by Render_Present */
        Render_Clear();
        Render_DrawFrame();
        DebugMenu_Draw(SDL_GetRenderer(window));
        Render_Present();

        next_frame += frame_ticks;
        Uint64 now = SDL_GetPerformanceCounter();
        if (next_frame > now) {
            Uint32 ms = (Uint32)((next_frame - now) * 1000 / freq);
            if (ms > 1) SDL_Delay(ms - 1);   /* coarse sleep, then spin the last ~1 ms */
            while (SDL_GetPerformanceCounter() < next_frame) { }
        } else if (now - next_frame > frame_ticks * 4) {
            next_frame = now;                /* far behind (window dragged...): resync */
        }
    }

    Input_Shutdown();
    Audio_Shutdown();
    Render_Shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
