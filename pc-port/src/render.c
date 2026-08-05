#include "render.h"
#include "object.h"
#include <stdio.h>

static SDL_Renderer *s_renderer = NULL;

int Render_Init(SDL_Window *window)
{
    s_renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!s_renderer) {
        s_renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!s_renderer) {
        fprintf(stderr, "[render] SDL_CreateRenderer failed: %s\n", SDL_GetError());
        return 0;
    }
    return 1;
}

void Render_Clear(void)
{
    if (!s_renderer) return;
    /* Stand-in for Vdp1_EraseFrameBuffer -- real hardware writes the VDP1
     * erase-window registers, SDL just clears the backbuffer. */
    SDL_SetRenderDrawColor(s_renderer, 16, 16, 32, 255);
    SDL_RenderClear(s_renderer);
}

static void DrawSlotMarker(int32_t raw_x, int32_t raw_y, int size, Uint8 r, Uint8 g, Uint8 b)
{
    /* Slot values are raw traced fixed-point fields of unknown exact scale
     * (see docs/jump_table_functions.md, Obj_SetTransformParam). Treat the
     * upper 16 bits as the integer part (SGL convention) and wrap into the
     * window so *something* moves on screen when the slot data changes --
     * purely a debug aid until the real coordinate format is confirmed. */
    int sx = 320 + (((raw_x >> 16) % 280) + 280) % 280 - 140;
    int sy = 240 + (((raw_y >> 16) % 200) + 200) % 200 - 100;
    SDL_Rect rect = { sx - size / 2, sy - size / 2, size, size };
    SDL_SetRenderDrawColor(s_renderer, r, g, b, 255);
    SDL_RenderFillRect(s_renderer, &rect);
}

void Render_DrawFrame(void)
{
    if (!s_renderer) return;
    const ObjTransformBlock *tb = Obj_GetTransformBlock();
    /* mode 4 slot -> red marker, mode 8 slot -> green marker. Modes
     * 0x10/0x20 (angle/scale, high-16-only fields) aren't drawable as a
     * position, so they're skipped here. */
    DrawSlotMarker(tb->slot0_a, tb->slot0_b, 14, 220, 70, 70);
    DrawSlotMarker(tb->slot1_a, tb->slot1_b, 14, 70, 220, 90);
}

void Render_Present(void)
{
    if (s_renderer) {
        SDL_RenderPresent(s_renderer);
    }
}

void Render_Shutdown(void)
{
    if (s_renderer) {
        SDL_DestroyRenderer(s_renderer);
        s_renderer = NULL;
    }
}
