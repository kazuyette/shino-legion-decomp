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
void     sub_06040F80(void);        /* debug free-move (cheat) */
void     sub_060410F8(void);        /* on-hit setup */
int      sub_060424A6(void);        /* 4th posture selector */
void     sub_06042B90(void);
void     sub_06042C88(void);
void     sub_06042E24(void);
void     sub_060437C0(void);        /* ninjutsu execution */
void     sub_06043A44(void);
void     sub_06042D80(int32_t dx, int32_t dy, int angle, int32_t speed); /* spawn shuriken */
void     sub_06044280(void);
void     sub_0604592E(void);
void     sub_06046782(void);        /* anim-driven effect trigger */
int      Obj_Spawn(int type, int32_t x, int32_t y, int flags, int param);    /* 0x060621B2 (effects) */
int32_t  sub_0602DDD2(void);          /* A.BIN random: seed 0x0603A4B0 = seed*5 + 0x3573 */
void     sub_06007D60(int sfx, int bank);  /* play banked sound */
#endif
