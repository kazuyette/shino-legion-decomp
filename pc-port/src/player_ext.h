#ifndef PLAYER_EXT_H
#define PLAYER_EXT_H
#include <stdint.h>
/* Original functions called by the ported player code but NOT ported yet.
 * Implemented as logging stubs in player_stubs.c; names = original address. */
void     sub_06007CDE(int sfx);     /* play sound effect */
void     sub_06007E00(void);
int      sub_0600A604(void);        /* idle-anim gate (stub returns 1: stay idle) */
void     sub_0600CEF4_rand(void);
int      sub_0600CEF4(void);
void     sub_06040DE8(void);        /* normal movement/physics */
void     sub_06040F80(void);        /* debug free-move (cheat) */
void     sub_06040FF0(void);        /* airborne physics */
void     sub_060410F8(void);        /* on-hit setup */
int      Player_SelectAction_Crouch(void); /* 0x06041B3C */
int      Player_SelectAction_Air(void);    /* 0x06041E48 */
int      sub_0604241E(void);
int      sub_060424A6(void);        /* 4th posture selector */
void     sub_06042B90(void);
void     sub_06042C88(void);
void     sub_06042E24(void);
void     sub_060437C0(void);        /* ninjutsu execution */
void     sub_060439CC(void);        /* jump execution */
void     sub_06043A44(void);
void     sub_06043BC8(void);        /* crouch slash execution */
void     sub_06043C58(void);
void     sub_06042D80(int32_t dx, int32_t dy, int angle, int32_t speed); /* spawn shuriken */
void     Anim_Start(uint32_t state, uint32_t animTbl, uint32_t frameTbl, int z, int anim); /* A.BIN 0x0600648C */
void     sub_06043FDA(void);
void     sub_06044280(void);
void     sub_060450E4(void);
uint16_t sub_0604588E(int32_t x, int32_t y, int r, int16_t p); /* terrain probe */
void     sub_0604592E(void);
void     sub_06046782(void);        /* anim-driven effect trigger */
void     Obj_Spawn(int type, int32_t x, int32_t y, int facingLeft, int param); /* 0x060621B2 */
void     sub_0602DDD2(void);
#endif
