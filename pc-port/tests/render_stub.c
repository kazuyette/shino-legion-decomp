/* Test stand-in for render.c (no SDL): records the last queued sprite and a log of all
 * queued sprites (tests reset g_spriteLogCount themselves). */
#include "render.h"
RenderSprite g_lastSprite; int g_spriteCount;
#define SPRITE_LOG_MAX 4096
RenderSprite g_spriteLog[SPRITE_LOG_MAX]; int g_spriteLogCount;
void Render_QueueSprite(const RenderSprite *s)
{
    g_lastSprite = *s; g_spriteCount++;
    if (g_spriteLogCount < SPRITE_LOG_MAX) g_spriteLog[g_spriteLogCount++] = *s;
}
void Render_InvalidateVram(uint32_t addr, size_t len) { (void)addr; (void)len; }
