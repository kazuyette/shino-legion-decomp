#include "objects.h"
#include "ram.h"
#include "player_ext.h"

static uint32_t slot_addr(int s) { return OBJ_TABLE + (uint32_t)(int16_t)(s * (int)OBJ_SIZE); }

void Obj_ClearSlot(int s)
{
    uint32_t o = slot_addr(s);
    for (uint32_t i = 0; i < OBJ_SIZE; i += 2) W16(o + i, 0);
    W16(o + 0x00, OBJ_FREE);
    W16(o + 0x18, OBJ_FREE);
    /* 0x06006464(o + 0x58): animation state reset (all zero here) */
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
            Obj_Register(type, x, y, (uint16_t)(Ram_Ptr(e + 6)[0] << 8), Ram_Ptr(e + 6)[1]);
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
