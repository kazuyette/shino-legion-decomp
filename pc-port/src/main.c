#include "audio.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "boot.h"
#include "config.h"
#include "render.h"
#include "debug_menu.h"
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
 *   --assets DIR    asset directory (default "assets" next to the working directory)
 *   --skip-intro    no SEGA logo at boot (as on the Saturn, where the BIOS shows it)
 *   F1 / pad Back   PC debug/cheat menu (lives, energy, shurikens, ninjutsu, stage clear/select)
 *   --stage N       development: boot straight into stage index N (0 = game 1-1 = STAGE7_1,
 *                   ... 12 = 7-1 = STAGE1_1, 17 = 9-2) with new-game variables
 *   --verbose       log player action changes
 *   --config FILE   settings file (default: shinobi.ini next to the executable, created
 *                   with commented defaults when missing; see config.c / README)
 *   Alt+Enter / F11 fullscreen on/off, F12 screenshot (screenshots/ next to the executable).
 *   Esc is not a quit key: closing the window quits. */
static void usage(const char *exe)
{
    printf("usage: %s [--assets DIR] [--config FILE] [--skip-intro] [--stage N] [--verbose] [--version]\n", exe);
}

int main(int argc, char **argv)
{
    const char *assets = "assets";
    const char *configPath = NULL;
    int stage = -1, skipIntro = 0;

    printf("%s\n", SL_VERSION_FULL);
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--version") == 0) return 0;
        else if (strcmp(argv[i], "--assets") == 0 && i + 1 < argc) assets = argv[++i];
        else if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) configPath = argv[++i];
        else if (strcmp(argv[i], "--stage") == 0 && i + 1 < argc) stage = atoi(argv[++i]);
        else if (strcmp(argv[i], "--skip-intro") == 0) skipIntro = 1;
        else if (strcmp(argv[i], "--verbose") == 0) g_shinobiVerbose = 1;
        else { usage(argv[0]); return 1; }
    }
    if (stage > 17) { fprintf(stderr, "--stage: index 0..17\n"); return 1; }

    /* The BIOS loads A.BIN at 0x06004000 and jumps to it. */
    char p[600];
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
                if (sc == SDL_SCANCODE_F12) { Render_RequestScreenshot(); continue; }
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
