#ifndef OBJECT_H
#define OBJECT_H

#include <stdint.h>

/* Object/sprite transform dispatcher, fully decompiled from Obj_SetTransformParam
 * (0x0602a8c4). Modes 4/8/0x10/0x20 -- the only ones the real per-frame loop
 * actually uses (see boot.c's kObjectSlotModes) -- write two fields each into
 * ONE shared control block at fixed offsets, with no side effects; that part
 * is ported faithfully below. Modes 1/2 write into two SEPARATE control
 * blocks and conditionally fire a callback through a function pointer
 * (PTR_FUN_0602a984/0602aa30) -- that looks like a real SGL render/commit
 * call, isn't used by the observed call site, and is NOT ported. */
void Obj_SetTransformParam(int mode, int param_a, int param_b);
void Obj_SetReadyFlag(void);
void Obj_ConsumeFlagAndStore(int value);
int  Obj_GetLastMode(void);

/* Obj_SetTransformParamSecondary (0x0602ab1c) -- structural twin of
 * Obj_SetTransformParam found during the VDP2 sweep. Confirmed: mode 4
 * writes into the SAME control block as the primary dispatcher, at offsets
 * +0x4c/+0x50 -- exactly 8 bytes past the primary's mode-4 offsets
 * (+0x44/+0x48), i.e. it lands in what this port modeled as padding
 * (_pad0). Reads like a second data channel per object slot (velocity/
 * rotation next to position) but that's a guess -- only the offset is
 * confirmed. Modes 1/2/8/0x10/0x20 for this secondary dispatcher were NOT
 * traced (the doc only confirms mode 4's offset pair) and are not ported,
 * same caution as the primary's modes 1/2. The "min/max-clamp pre-pass via
 * two callbacks" mentioned in the decompile is also not ported -- the
 * clamp bounds were never traced, so inventing them would be a guess. */
void Obj_SetTransformParamSecondary(int mode, int param_a, int param_b);

/* Raw layout of the shared control block written by modes 4/8/0x10/0x20,
 * offsets match the decompile exactly (0x00/0x04, 0x10/0x14 as full 32-bit
 * writes; 0x20/0x22, 0x24/0x26 as the high 16 bits only -- SGL fixed-point
 * integer part, consistent with angle/scale fields). Exposed for inspection/
 * future rendering code. */
typedef struct {
    int32_t slot0_a, slot0_b; /* mode 4  -> offsets 0x00, 0x04 */
    int32_t secondary_a, secondary_b; /* Obj_SetTransformParamSecondary mode 4
                                        * -> offsets 0x08, 0x0c (confirmed:
                                        * +0x4c/+0x50 absolute, 8 bytes past
                                        * the primary's mode-4 offsets) */
    int32_t slot1_a, slot1_b; /* mode 8  -> offsets 0x10, 0x14 */
    unsigned char _pad1[8];   /* 0x18-0x1f, unused by this dispatcher */
    int16_t slot2_a, slot2_b; /* mode 0x10 -> offsets 0x20, 0x22 (hi16 of params) */
    int16_t slot3_a, slot3_b; /* mode 0x20 -> offsets 0x24, 0x26 (hi16 of params) */
} ObjTransformBlock;

const ObjTransformBlock *Obj_GetTransformBlock(void);

/* Generic intrusive doubly-linked list node, matching the sentinel-node
 * pattern from FUN_06006f56: an empty list is a node whose next/prev both
 * point back to itself. */
typedef struct ObjListNode {
    struct ObjListNode *next;
    struct ObjListNode *prev;
} ObjListNode;

/* FUN_06006f56 — init two list-head pointers to a shared sentinel node.
 * On real hardware both lists shared one sentinel; here each gets its own
 * (functionally equivalent — the original sharing was likely just a memory
 * optimization, not behaviorally significant). */
void Obj_InitLinkedLists(void);

/* Accessors for the two object lists set up by Obj_InitLinkedLists. */
ObjListNode *Obj_GetActiveListHead(void);
ObjListNode *Obj_GetFreeListHead(void);

void ObjList_InitSentinel(ObjListNode *sentinel);
void ObjList_PushBack(ObjListNode *sentinel, ObjListNode *node);
void ObjList_Remove(ObjListNode *node);

/* FUN_06007bac — zero a 17x12-byte array, set an 8x8-byte array to 0xffff
 * (matches 8 SCSP channels). Called on every stage reset. */
void Reset_ObjectAndChannelTables(void);

#endif
