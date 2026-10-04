#ifndef OBJHANDLERS_D_H
#define OBJHANDLERS_D_H
#include <stdint.h>
/* Stage 4-2 (file STAGE4_2, stage index 7) objects, ported in objhandlers_d.c.
 * The stage has no map collision (C+0x32 = -1) and a fixed camera: the "climb" is faked by
 * stage objects that slide DOWN the screen and wrap around, started by the boss
 * (enemy type 0x20, enemy_boss42.c) once its intro ends (+0x48 = 1 on 0x1025/0x1026,
 * +0x4A = 1 on 0x1028) and stopped when it dies.
 * Speed class = placement flags & 3 (0x1026: flags & 0xC): 0 -> 1/8 px, 1 -> 3/8 px,
 * 2 -> 1/4 px per frame. Sprites are drawn at world - 1024 (no camera: it never moves).
 *  0x1025 ledge (Sho stands on it: one-way top, Obj_PlayerContact(1)); wraps by 1024 px.
 *    +0x48 moving, +0x4A in the boss's landing band (ymin-48 < y < ymin+168),
 *    +0x4C low (y > ymin+200), +0x4E Sho standing on it, +0x50 glow state (1 glowing:
 *    spawns a 0x1027 overlay and fades palette 1 to 0x060D2620; 2 lit; 3 fading back
 *    to palette 0, then 0), +0x52 overlay slot + 1. Placement flags & 4: the boss's
 *    first landing ledge.
 *  0x1026 background beam (anim 3 + (flags & 3)), wraps by 512 px, no collision.
 *  0x1027 glow overlay on a ledge (+0x48 = ledge slot), anim 2 blinking, until it ends.
 *  0x1028 lantern (anim 7): Sho's sword breaks it (item +0x18 dropped, explosion part),
 *    leaving its base (anim 8) and two pieces: a lid flying off (0x1028 flags 4, anim 9,
 *    50 frames, +0x30/+0x34 velocity, +0x48 angle) and a flame (flags 8, anim 10,
 *    follows the base +0x48, grows to scale 1). Wraps by 5120 px and is rebuilt.
 * Object table addresses: see objects.h. */
void ObjHandler_1025(void);   /* 0x06056408 */
void ObjHandler_1026(void);   /* 0x06056260 */
void ObjHandler_1027(void);   /* 0x0605678C */
void ObjHandler_1028(void);   /* 0x06056902 */
/* A.BIN 0x06006000: move n shadow-CRAM colours from index dst one step (per 5-bit
 * component) towards the colours at src; 1 when they all match. */
int  Pal_StepTowards(int dst, int n, uint32_t src, int step);
#endif
