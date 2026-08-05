#include "object.h"
#include <string.h>

/* Placeholder storage until the real control-block struct layout is
 * recovered from the 0x0602a970-ish region. */
static unsigned char s_object_channel_table[17][12];
static unsigned short s_channel_table[8][8];

static ObjTransformBlock s_transform_block;

void Obj_SetTransformParam(int mode, int param_a, int param_b)
{
    switch (mode) {
        case 4:
            s_transform_block.slot0_a = param_a;
            s_transform_block.slot0_b = param_b;
            break;
        case 8:
            s_transform_block.slot1_a = param_a;
            s_transform_block.slot1_b = param_b;
            break;
        case 0x10:
            s_transform_block.slot2_a = (int16_t)((uint32_t)param_a >> 16);
            s_transform_block.slot2_b = (int16_t)((uint32_t)param_b >> 16);
            break;
        case 0x20:
            s_transform_block.slot3_a = (int16_t)((uint32_t)param_a >> 16);
            s_transform_block.slot3_b = (int16_t)((uint32_t)param_b >> 16);
            break;
        default:
            /* modes 1/2: SGL-callback path, not ported (see object.h). */
            break;
    }
}

const ObjTransformBlock *Obj_GetTransformBlock(void)
{
    return &s_transform_block;
}

/* Producer/consumer handshake flag, matches Obj_SetReadyFlag (0602a8a8) /
 * Obj_ConsumeFlagAndStore (0602a894) exactly: one raises it, the other
 * clears it on read and stores the mode that was in flight. */
static short s_ready_flag = 0;
static int s_last_mode = 0;

void Obj_SetReadyFlag(void)
{
    if (s_ready_flag == 0) {
        s_ready_flag = 1;
    }
}

void Obj_ConsumeFlagAndStore(int value)
{
    if (s_ready_flag == 1) {
        s_ready_flag = 0;
    }
    s_last_mode = value;
}

int Obj_GetLastMode(void)
{
    return s_last_mode;
}

static ObjListNode s_active_list_sentinel;
static ObjListNode s_free_list_sentinel;

void ObjList_InitSentinel(ObjListNode *sentinel)
{
    sentinel->next = sentinel;
    sentinel->prev = sentinel;
}

void ObjList_PushBack(ObjListNode *sentinel, ObjListNode *node)
{
    node->prev = sentinel->prev;
    node->next = sentinel;
    sentinel->prev->next = node;
    sentinel->prev = node;
}

void ObjList_Remove(ObjListNode *node)
{
    node->prev->next = node->next;
    node->next->prev = node->prev;
    node->next = node;
    node->prev = node;
}

void Obj_InitLinkedLists(void)
{
    /* FUN_06006f56: init two list-head pointers to a shared sentinel node
     * (active-object list and free-object list). */
    ObjList_InitSentinel(&s_active_list_sentinel);
    ObjList_InitSentinel(&s_free_list_sentinel);
}

ObjListNode *Obj_GetActiveListHead(void)
{
    return &s_active_list_sentinel;
}

ObjListNode *Obj_GetFreeListHead(void)
{
    return &s_free_list_sentinel;
}

void Reset_ObjectAndChannelTables(void)
{
    memset(s_object_channel_table, 0, sizeof(s_object_channel_table));
    for (int i = 0; i < 8; i++)
        for (int j = 0; j < 8; j++)
            s_channel_table[i][j] = 0xffff;
}
