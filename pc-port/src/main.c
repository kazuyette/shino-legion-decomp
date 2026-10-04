#include "audio.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "boot.h"
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
 *   --verbose       log player action changes */
static void usage(const char *exe)
{
    printf("usage: %s [--assets DIR] [--skip-intro] [--stage N] [--verbose] [--version]\n", exe);
}

int main(int argc, char **argv)
{
    const char *assets = "assets";
    int stage = -1, skipIntro = 0;

    printf("%s\n", SL_VERSION_FULL);
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--version") == 0) return 0;
        else if (strcmp(argv[i], "--assets") == 0 && i + 1 < argc) assets = argv[++i];
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

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window *window = SDL_CreateWindow(SL_VERSION_FULL, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                          640, 480, SDL_WINDOW_SHOWN);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    Render_Init(window);
    Audio_Init();                                      /* SDL audio: CD-DA tracks, movie sound */

    Boot_Init(!skipIntro);                   /* A.BIN 0x06004038: queues DIRECTOR.PPB */
    if (stage >= 0) Boot_DevStartStage(stage);

    /* Drift-free ~60fps pacing (NTSC is 59.94 Hz): sleep off the remainder of each frame. */
    const Uint32 frame_ms = 1000 / 60;
    Uint32 next_frame = SDL_GetTicks();
    int running = 1;
    SDL_Event ev;
    while (running) {
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = 0;
            DebugMenu_HandleEvent(&ev);      /* PC debug/cheat menu: F1 or pad Back */
        }

        int paused = DebugMenu_IsOpen();
        Render_SetKeepQueue(paused);
        if (!paused) {
            Input_Update();                  /* SMPC report for Pad_Update */
            DebugMenu_BeforeFrame();
            Boot_Frame();                    /* one iteration of the A.BIN main loop */
        }

        Render_Clear();
        Render_DrawFrame();
        DebugMenu_Draw(SDL_GetRenderer(window));
        Render_Present();

        next_frame += frame_ms;
        Uint32 now = SDL_GetTicks();
        if (next_frame > now) SDL_Delay(next_frame - now);
        else next_frame = now;               /* running behind: resync */
    }

    Input_Shutdown();
    Audio_Shutdown();
    Render_Shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
