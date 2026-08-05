#include <SDL2/SDL.h>
#include <stdio.h>
#include "boot.h"
#include "render.h"
#include "input.h"

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window *window = SDL_CreateWindow(
        "Shinobi Legions - PC port (skeleton)",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        640, 480, SDL_WINDOW_SHOWN);

    if (!window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    Render_Init(window);
    Boot_Init();

    /* Drift-free ~60fps pacing (Saturn's real vblank rate is ~59.94Hz NTSC;
     * 60 is close enough for a debug loop and avoids importing NTSC/PAL
     * timing details we haven't traced). SDL_Delay(16) alone drifts over
     * time since 16ms != 1000/60ms exactly -- this measures actual elapsed
     * time each frame and only sleeps off the remainder. */
    const Uint32 frame_ms = 1000 / 60;
    Uint32 next_frame = SDL_GetTicks();

    int running = 1;
    SDL_Event ev;
    while (running) {
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = 0;
        }

        Input_Update();
        Boot_RunFrame();

        Render_Clear();
        Render_DrawFrame();
        Render_Present();

        next_frame += frame_ms;
        Uint32 now = SDL_GetTicks();
        if (next_frame > now) {
            SDL_Delay(next_frame - now);
        } else {
            /* Running behind (e.g. window drag stalled us) -- resync
             * instead of trying to burn through a backlog of frames. */
            next_frame = now;
        }
    }

    Boot_Shutdown();
    Render_Shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
