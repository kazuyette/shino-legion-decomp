#ifndef RENDER_H
#define RENDER_H

#include <SDL2/SDL.h>

/* Minimal SDL2 renderer. This is a placeholder visualization, NOT an
 * accurate VDP1/VDP2 reimplementation -- we don't have real sprite/tile
 * data yet (that lives in DIRECTOR.PPB/SHINOBI.PPB, still unreadable). Its
 * only job right now is to prove the data path works: values written by
 * Obj_SetTransformParam show up as moving markers on screen. Real rendering
 * needs the actual sprite/pattern data first.
 *
 * Conceptually mirrors two traced A.BIN functions without reimplementing
 * their register-level behavior: Render_Clear ~= Vdp1_EraseFrameBuffer's
 * role (start-of-frame clear), Render_DrawFrame ~= consuming what
 * Obj_SetTransformParam/Vdp1_Emit*Cmd would have fed to real hardware. */

int  Render_Init(SDL_Window *window);
void Render_Clear(void);
void Render_DrawFrame(void);
void Render_Present(void);
void Render_Shutdown(void);

#endif
