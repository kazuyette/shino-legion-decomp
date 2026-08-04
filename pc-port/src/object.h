#ifndef OBJECT_H
#define OBJECT_H

/* Object/sprite transform dispatcher cluster, traced from 0x0602a900-ish
 * in A.BIN (FUN_0602a8c4 and friends). Mode is a bitflag (1,2,4,8,0x10,0x20)
 * selecting which pair of fields gets written on a per-object control block. */
void Obj_SetTransformParam(int mode, int param_a, int param_b);
void Obj_SetReadyFlag(void);
void Obj_ConsumeFlagAndStore(int value);

/* FUN_06006f56 — init two list-head pointers to a shared sentinel node. */
void Obj_InitLinkedLists(void);

/* FUN_06007bac — zero a 17x12-byte array, set an 8x8-byte array to 0xffff
 * (matches 8 SCSP channels). Called on every stage reset. */
void Reset_ObjectAndChannelTables(void);

#endif
