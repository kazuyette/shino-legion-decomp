#include <SDL2/SDL.h>
#include <stdio.h>
#include <string.h>
#include "boot.h"
#include "render.h"
#include "input.h"
#include "pad.h"
#include "player.h"
#include "ram.h"
#include "version.h"
#include "stage.h"
#include "objects.h"
#include "camera.h"

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    printf("%s\n", SL_VERSION_FULL);
    if (argc > 1 && strcmp(argv[1], "--version") == 0)
        return 0;
    if (Ram_LoadImage("assets/A.BIN", 0x06004000u))
        printf("[ram] A.BIN loaded\n");
    int have_game_data = 0;
    if (Ram_LoadImage("assets/SHINOBI.dec.bin", 0x06040000u)) {
        printf("[ram] SHINOBI.dec.bin loaded\n");
        have_game_data = 1;
    }
    if (Ram_LoadImage("assets/SHOU.CHR", 0x00200000u))
        printf("[ram] SHOU.CHR loaded (player graphics)\n");
    else
        have_game_data = 0;
    Player_Init();
    if (have_game_data && Stage_Load("assets", 12))   /* stage 1-1 */
        Stage_SpawnPlayer();

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window *window = SDL_CreateWindow(
        SL_VERSION_FULL,
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
        W16(0x060143EAu, R16(0x060143EAu) + 1u);   /* frame counters (A.BIN vblank / SHINOBI 0x06066958) */
        W16(0x060F4CCCu, R16(0x060F4CCCu) + 1u);
        /* SHINOBI 0x06040B44 (stage tick): Scroll_Update, ..., Game_LatchPad1, ..., Player_Tick */
        Scroll_Update();                     /* SHINOBI 0x060443D8 */
        Game_LatchPad1(P16(P_FLAGS4));       /* SHINOBI 0x06040CAC */
        {
            static int last_act = -1, frames_in_act = 0;
            /* Without the game data the animation scripts are missing: fake
             * "animation finished" after 20 frames so the state machine moves. */
            if (!have_game_data && frames_in_act >= 20) OR16(PLAYER + P_ANIM, PANIM_FINISHED);
            Player_Tick();                   /* SHINOBI 0x06040B72 (draws via Player_Draw) */
            Obj_UpdateAll();                 /* SHINOBI 0x06060014 (frame 0x0606B8FA) */
            if (!have_game_data) AND16(PLAYER + P_ANIM, (uint16_t)~PANIM_FINISHED);
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
    Input_Shutdown();
    Render_Shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
