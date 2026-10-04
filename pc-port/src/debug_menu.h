#ifndef DEBUG_MENU_H
#define DEBUG_MENU_H
/* PC-only debug / cheat menu (not in the Saturn game).
 * F1 (keyboard) or Back/Select (controller) opens and closes it; the game is paused
 * while it is open. Lives, energy, shurikens, ninjutsu, stage clear, stage select. */
union SDL_Event;
struct SDL_Renderer;

void DebugMenu_HandleEvent(const union SDL_Event *ev);   /* call for every SDL event */
int  DebugMenu_IsOpen(void);
void DebugMenu_BeforeFrame(void);  /* after Input_Update: persistent cheats + input mute */
void DebugMenu_Draw(struct SDL_Renderer *r);              /* after Render_DrawFrame */

/* actions (also used by the tests) */
int  DebugMenu_ClearStage(void);   /* posts "stage clear" like a boss death; 0 if not in a stage */
void DebugMenu_GotoStage(int idx); /* stage index 0..17 (0 = 1-1, 1 = 1-2 ... 17 = 9-2) */
void DebugMenu_SetInfinite(int energy, int shurikens);
#endif
