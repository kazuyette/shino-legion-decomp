#ifndef OBJHANDLERS_H
#define OBJHANDLERS_H
#include <stdint.h>

/* Stage "system" object handlers (SHINOBI table 0x060AC820, type - 0x1000) and
 * the object/player contact helpers they share.
 * Object fields used here: +0x10 contact flags (0x1000 touching, 0x2000 standing
 * on something, 0x0400 crushed, 0x0080 hit), +0x12 placement flags, +0x14 timer,
 * +0x18 list param, +0x1E linked object slot, +0x24 previous +0x10,
 * +0x2C previous Y, +0x3C carried dY, +0x40/+0x44 accumulated push X/Y
 * (applied by Obj_Integrate), +0x58 animation state (+0x68 = hitbox list). */

typedef struct { int32_t l, t, r, b; } ObjRect;

void    Obj_Push(uint32_t o, int32_t dx, int32_t dy);                   /* 0x0606061C (dy > 0 = up) */
int     Rect_Overlap(const ObjRect *a, const ObjRect *b);               /* 0x06048E84 */
int     Rect_Intersect(const ObjRect *a, const ObjRect *b, int32_t d[2]); /* 0x060606D0 */
int     Obj_PlayerContact(uint16_t mode);                               /* 0x06060268 */
int     Obj_CarryContact(uint32_t self, uint32_t other, int w, int h);  /* 0x0604BE64 */

void    ObjHandler_1004(void);                                          /* 0x0604B9F8 floating platform */
/* Dispatch for Obj_UpdateAll; unported types are skipped (logged once). */
void    ObjHandler_Call(uint16_t type);
#endif
