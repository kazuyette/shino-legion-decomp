/* Test stand-in for render.c (no SDL): records the last queued sprite. */
#include "render.h"
RenderSprite g_lastSprite; int g_spriteCount;
void Render_QueueSprite(const RenderSprite *s) { g_lastSprite = *s; g_spriteCount++; }
void Render_InvalidateVram(uint32_t addr, size_t len) { (void)addr; (void)len; }
