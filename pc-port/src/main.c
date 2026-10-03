#include <SDL2/SDL.h>
#include <stdio.h>
#include "boot.h"
#include "render.h"
#include "input.h"
#include "pad.h"
#include "player.h"
#include "ram.h"

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    if (Ram_LoadImage("assets/A.BIN", 0x06004000u))
        printf("[ram] A.BIN loaded\n");
    if (Ram_LoadImage("assets/SHINOBI.dec.bin", 0x06040000u))
        printf("[ram] SHINOBI.dec.bin loaded\n");
    Player_Init();

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
        Pad_Update(2);                       /* A.BIN 0x0600572A */
        if (Sys_CheckSoftReset()) {          /* A.BIN 0x060042B8 */
            printf("[pad] soft reset (A+B+C+Start)\n");
            Pad_Clear();
            Stage_ResetAndLoadDirector();
        }
        Game_LatchPad1(P16(P_FLAGS4));       /* SHINOBI 0x06040CAC */
        {
            static int last_act = -1, frames_in_act = 0;
            /* No animation system yet: fake "animation finished" after 20
             * frames in the same action so the state machine keeps moving. */
            if (frames_in_act >= 20) OR16(PLAYER + P_ANIM, PANIM_FINISHED);
            Player_Tick();                   /* SHINOBI 0x06040B72 */
            AND16(PLAYER + P_ANIM, (uint16_t)~PANIM_FINISHED);
            if (Player_Action() != last_act) {
                printf("[player] action %02X -> %02X (pad %04X)\n",
                       last_act & 0xFF, Player_Action(), R16(ADDR_GAME_HELD));
                last_act = Player_Action();
                frames_in_act = 0;
            } else {
                frames_in_act++;
            }
        }
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
