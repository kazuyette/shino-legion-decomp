#ifndef COLLISION_H
#define COLLISION_H
#include <stdint.h>
/* Background (VDP2 map) collision, ported from SHINOBI.dec.bin.
 * Layer descriptors: 0x30 bytes each at LAYER_DESC (see stage.c). */
#define LAYER_DESC 0x060EA588u
#define G_COLL_LAYER 0x060EA64Au   /* s16 layer used for object collision */

uint16_t Map_GetPN(int32_t x, int32_t y, uint32_t desc);                    /* 0x06046F12 */
uint16_t Map_GetAttr(int32_t x, int32_t y, uint32_t desc);                  /* 0x06046FE2 */
uint16_t Coll_AttrFlags(uint16_t attr);                                     /* 0x06045B88 */
uint16_t Coll_ProbeBox(int32_t x, int32_t y, int w, int h, int16_t layer, uint16_t skip); /* 0x06045670 */
void     Coll_PushOut(uint32_t obj, uint16_t dir);                          /* 0x0604562C */
uint16_t Coll_ProbeFloor(int32_t x, int32_t y, int w, int16_t layer);       /* 0x060457EC */
int      Coll_CountFloor(int32_t x, int32_t y, int n, int16_t layer);       /* 0x0604588E */
void     Obj_CollideMap(uint32_t obj, int w, int h);                        /* 0x06045340 */
void     Player_ClampToScreen(void);                                        /* 0x060451C8 */
#endif
