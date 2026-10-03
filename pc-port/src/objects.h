#ifndef OBJECTS_H
#define OBJECTS_H
#include <stdint.h>
/* Stage object table (SHINOBI): 0xC0 slots of 0x78 bytes at 0x060EC65C.
 *  +0x00 type (0xFFFF = free; 0x1000+ = handler table 0x060AC820[type-0x1000])
 *  +0x02 state (1 = just spawned)   +0x08/+0x0C X/Y (16.16 world)
 *  +0x12 flags (|= per-type defaults from 0x060A926C)   +0x18 list param
 *  +0x58 animation state (same layout as the player's)
 * Per-stage placement lists: table 0x060D1074[stage] -> 8-byte records
 * {u16 type, u16 x, u16 y, u16 param} in layer pixels, ended by 0xFFFF.
 * (1-1: 0x060CEF74; type 0x1004 = floating platforms on the water.) */
#define OBJ_TABLE  0x060EC65Cu
#define OBJ_SIZE   0x78u
#define OBJ_SLOTS  0xC0
#define OBJ_FREE   0xFFFF

void Obj_ClearSlot(int slot);                                         /* 0x06048A64 */
int  Obj_FindFree(void);                                              /* 0x06048B28 */
int  Obj_Register(uint16_t type, int32_t x, int32_t y, uint16_t flags, uint16_t param); /* 0x06048B64 */
int  Stage_LoadObjects(void);                                         /* 0x06048BD0 */
void Obj_ClearAll(void);
int  Obj_Count(void);
#endif
