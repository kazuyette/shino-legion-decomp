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
#include "enemy.h"
#include "fx.h"
#include "objhandlers_c.h"
#include "fade.h"
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
    W16(LIVES, 3);                                     /* PC: set by DIRECTOR's game start */
    W16(0x060252DCu, 0);                               /* first entry: Sho drops in */
    W16(G_GAME_MODE, 6);                               /* stage frames */
    if (have_game_data && Stage_Load("assets", 12)) { /* stage 1-1 */
        Stage_SpawnPlayer();
        Fade_StageIntro();                             /* 16-frame fade-in (Game_LoadStage 0x0606B6EC) */
    }

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
            Fade_Reset();                    /* A.BIN 0x06004360 */
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
            int16_t stage_before = RS16(G_STAGE_INDEX);
            Player_Tick();                   /* SHINOBI 0x06040B72 (draws via Player_Draw) */
            Stage_UpdateState();             /* SHINOBI 0x06046190: fall death, exit, end sequence */
            /* PC approximation of 0x0606E440: the drop-in ends when Sho lands */
            if (R16(0x060252DCu) == 0 && !(P16(P_FLAGS2) & 0x4000)) {
                W16(0x060252DCu, 1);
                PW16(P_FLAGS4, P16(P_FLAGS4) & 0xFFEF);
            }
            if (RS16(G_STAGE_INDEX) == stage_before) {   /* 0x0606B8FA skips these on a stage change */
                Obj_UpdateEnemies();         /* SHINOBI 0x06048954 */
                Obj_UpdateAll();             /* SHINOBI 0x06060014 */
                Projectiles_Update();        /* SHINOBI 0x0606088C */
                Effects_Update();            /* SHINOBI 0x06061FA0 */
                Items_Update();              /* SHINOBI 0x06062894 */
            }
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
        /* SHINOBI mode dispatcher 0x0606B54C (only what exists on PC so far) */
        if (have_game_data && R16(G_GAME_MODE) != 6) {
            uint16_t mode = R16(G_GAME_MODE);
            if (mode == 5) {                                  /* 0x0606B6EC: (re)load the stage */
                int16_t st = RS16(G_STAGE_INDEX);
                printf("[game] load stage index %d (checkpoint %d, respawn %d, lives %d)\n",
                       st, R16(G_CHECKPOINT), R16(G_RESPAWN), RS16(LIVES));
                Player_Init();
                if (!Stage_Load("assets", st)) {
                    printf("[game] stage %d not available yet: back to 1-1\n", st);
                    W16(G_CHECKPOINT, 0);
                    Stage_Load("assets", 12);
                }
                Stage_SpawnPlayer();
                Fade_StageIntro();
            } else {
                printf("[game] mode %d (%s) not ported yet: restarting 1-1\n", mode,
                       mode == 7 ? "stage-clear tally" : mode == 9 ? "game over" : "DIRECTOR");
                if (mode == 9) W16(LIVES, 3);
                W16(G_CHECKPOINT, 0);
                W16(0x060252DCu, 0);
                Player_Init();
                Stage_Load("assets", 12);
                Stage_SpawnPlayer();
                Fade_StageIntro();
            }
            W16(G_STAGE_STATE, R16(G_STAGE_STATE) & 0x7FF0);
            W16(G_GAME_MODE, 6);
        }
        Fade_Update();                       /* A.BIN main loop 0x06004038 -> 0x06007084 */

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
