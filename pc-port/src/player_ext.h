#ifndef PLAYER_EXT_H
#define PLAYER_EXT_H
#include <stdint.h>
/* Original routines called by the ported player code, implemented in player_stubs.c
 * (ninjutsu ones forward to ninjutsu.c); names = original address. */
void     sub_06007CDE(int sfx);     /* Snd_Play */
void     sub_06007E00(void);        /* Snd_StopAll */
int32_t  sub_0600A604(int32_t a, int32_t b);    /* A.BIN __modsi3: a % b (signed, % 0 = 0) */
uint32_t sub_0600CEF4(uint32_t a, uint32_t b);  /* A.BIN __umodsi3: a % b (unsigned, % 0 = 0) */
void     sub_06040F80(void);        /* debug free-move (cheat) */
void     sub_060410F8(void);        /* on hit: clear flags, hurt / death cry */
int      sub_060424A6(void);        /* posture-4 (hanging) action selector */
void     sub_06042B90(void);        /* command input buffer */
void     sub_06042C88(void);        /* 8-shuriken fan */
void     sub_06042E24(void);        /* damaging terrain */
void     sub_060437C0(void);        /* ninjutsu execution (ninjutsu.c) */
void     sub_06043A44(void);        /* action 0x1C execution */
void     sub_06042D80(int32_t dx, int32_t dy, int angle, int32_t speed); /* spawn shuriken */
void     sub_06044280(void);        /* afterimages (shield / run special) */
void     sub_0604592E(void);        /* sword against a wall: spark */
void     sub_06046782(void);        /* attack anim event -> kind-4 sword flash (ninjutsu.c) */
int      Obj_Spawn(int type, int32_t x, int32_t y, int flags, int param);    /* 0x060621B2 (effects) */
int32_t  sub_0602DDD2(void);          /* A.BIN random: seed 0x0603A4B0 = seed*5 + 0x3573 */
void     sub_06007D60(int sfx, int bank);  /* play banked sound */
#endif
