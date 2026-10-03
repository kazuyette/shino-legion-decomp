/* DIRECTOR scenes 5/6: continue / game over (0x06042D18, `braf` table 0x06042D94, 20 steps)
 * and scene 7: staff roll (0x060432C4) -- work package D6.
 *
 * STUB: no movies, no digits; step 5 (Continue_Restart 0x06042FE4, director.c) is real.
 * Flow kept: continues left -> "CONTINUE" for 600 frames, Start = continue (continues - 1,
 * back to act 1 through scene 2), timeout or no continue -> "GAME OVER" (Start or 600
 * frames) -> scene 0. Staff roll -> scene 0 at once. To do: everything in
 * docs/director.md "Scene 5 / 6" and "Scene 7". */
#include "director_int.h"

/* STUB of 0x06042DC2 */
static void Continue_Init_Stub(void)
{
    Dir_SetFadeOffsets(0);
    Font_Init(0x200, 64, 0x8000);
    W16(D_TIMER, 0);
    Dir_DispOn();
    Dir_SetStep(RS16(V_CONTINUES) == 0 ? 16 : 1);
}

static void Continue_Wait_Stub(void)
{
    Font_DrawText("CONTINUE", 0, 128, 104);
    Font_DrawNumber(RS16(V_CONTINUES), 0, 152, 128, 1, 0);
    W16(D_TIMER, R16(D_TIMER) + 1u);
    if ((Dir_Pressed() & 0x0800) && RS16(D_TIMER) >= 50) {
        W16(V_CONTINUES, R16(V_CONTINUES) - 1u);
        Dir_SetStep(5);
    } else if (RS16(D_TIMER) >= 600) {
        W16(D_TIMER, 0);
        Dir_SetStep(16);
    }
}

static void GameOver_Stub(void)
{
    Font_DrawText("GAME OVER", 0, 124, 104);
    W16(D_TIMER, R16(D_TIMER) + 1u);
    if ((Dir_Pressed() & 0x0800) || RS16(D_TIMER) >= 600) Dir_SetStep(19);
}

/* 0x0604327E */
static void GameOver_Exit(void)
{
    Dir_DispOff();
    Dir_SetScene(0);
}

/* 0x06042D18 */
void Scene5_ContinueGameOver(void)
{
    switch (Dir_Step()) {
    case 0:  Continue_Init_Stub(); break;
    case 1:  Continue_Wait_Stub(); break;
    case 5:  Continue_Restart(); break;              /* 0x06042FE4 */
    case 16: GameOver_Stub(); break;
    case 19: GameOver_Exit(); break;
    default: break;
    }
}

/* 0x060432C4 (STUB) */
void Scene7_StaffRoll(void)
{
    Dir_DispOff();
    Dir_SetScene(0);
}
