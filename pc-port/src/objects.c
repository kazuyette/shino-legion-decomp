#include "objects.h"
#include "ram.h"
#include "player_ext.h"
#include "anim.h"
#include "objhandlers.h"
#include <stdio.h>

static uint32_t slot_addr(int s) { return OBJ_TABLE + (uint32_t)(int16_t)(s * (int)OBJ_SIZE); }

void Obj_ClearSlot(int s)
{
    uint32_t o = slot_addr(s);
    for (uint32_t i = 0; i < OBJ_SIZE; i += 2) W16(o + i, 0);
    W16(o + 0x00, OBJ_FREE);
    W16(o + 0x18, OBJ_FREE);
    Anim_Reset(o + 0x58);                        /* 0x06006464 */
}

int Obj_FindFree(void)
{
    for (int s = 0; s < OBJ_SLOTS; s++)
        if (R16(slot_addr(s)) == OBJ_FREE) return s;
    return -1;
}

int Obj_Register(uint16_t type, int32_t x, int32_t y, uint16_t flags, uint16_t param)
{
    int s = Obj_FindFree();
    if (s >= 0) {
        Obj_ClearSlot(s);
        if (type < 0x1000) flags |= R16(0x060A926Cu + 2u * type);     /* per-type defaults */
        uint32_t o = slot_addr(s);
        W16(o + 0x00, type);
        W32(o + 0x08, (uint32_t)x);
        W32(o + 0x0C, (uint32_t)y);
        W16(o + 0x12, flags);
        W16(o + 0x18, param);
        W16(o + 0x02, 1);
    }
    return s;
}

int Stage_LoadObjects(void)
{
    uint32_t list = R32(0x060D1074u + 4u * (uint32_t)(int16_t)R16(0x0602526Au));
    int i = 0;
    for (; (int16_t)i < 0xC0; i++) {
        uint32_t e = list + (uint32_t)(int16_t)i * 8;
        uint16_t type = R16(e);
        if (type == 0xFFFF) return i;
        int32_t x = (int16_t)R16(e + 2) * 0x10000 + 0x04000000;     /* layer px -> world */
        int32_t y = (int16_t)R16(e + 4) * 0x10000 + 0x04000000;
        if (type < 0x8000)
            Obj_Register(type, x, y, Ram_Ptr(e + 6)[0], (uint16_t)(int8_t)Ram_Ptr(e + 6)[1]);
        else
            Obj_Spawn(type - 0x8000, x, y, Ram_Ptr(e + 6)[0] << 8, (int8_t)Ram_Ptr(e + 7)[0]);
    }
    return i;
}

void Obj_ClearAll(void)
{
    for (int s = 0; s < OBJ_SLOTS; s++) Obj_ClearSlot(s);
}

int Obj_Count(void)
{
    int n = 0;
    for (int s = 0; s < OBJ_SLOTS; s++) n += R16(slot_addr(s)) != OBJ_FREE;
    return n;
}

/* 0x06060014: run the handler of every active "system" object (types 0x1000-0x1FFF,
 * table 0x060AC820). Enemies (types < 0x1000) go through 0x06048954 (not ported). */
void Obj_UpdateAll(void)
{
    for (W16(OBJ_CUR_IDX, 0); RS16(OBJ_CUR_IDX) < OBJ_SLOTS; W16(OBJ_CUR_IDX, R16(OBJ_CUR_IDX) + 1)) {
        uint32_t o = slot_addr(R16(OBJ_CUR_IDX));
        W32(OBJ_CUR, o);
        uint16_t t = R16(o);
        if (t == OBJ_FREE || t < 0x1000 || t >= 0x2000) continue;
        if (R32(0x060AC820u + 4u * (t - 0x1000u)) == 0) continue;
        if (!(R16(0x0602529Eu) & 0x8000)) {
            W16(o + 0x24, R16(o + 0x10));                 /* previous contact flags */
            AND16(o + 0x10, 0xEFFF);
            AND16(o + 0x10, 0xFF7F);
        }
        ObjHandler_Call(t);
    }
}
