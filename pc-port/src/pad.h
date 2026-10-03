#ifndef PAD_H
#define PAD_H
#include <stdint.h>

/* Saturn digital pad bits, active-HIGH (after Pad_Update's inversion). */
#define PAD_RIGHT 0x8000
#define PAD_LEFT  0x4000
#define PAD_DOWN  0x2000
#define PAD_UP    0x1000
#define PAD_START 0x0800
#define PAD_A     0x0400
#define PAD_C     0x0200
#define PAD_B     0x0100
#define PAD_RT    0x0080
#define PAD_X     0x0040
#define PAD_Y     0x0020
#define PAD_Z     0x0010
#define PAD_LT    0x0008

/* ---- A.BIN layer: hardware pad table @ 0x0601C4A0, 8 bytes/port ---- */
typedef struct {
    uint16_t held;          /* +0 */
    uint16_t held_prev;     /* +2 */
    uint16_t pressed;       /* +4  held & ~held_prev */
    uint16_t pressed_prev;  /* +6 */
} PadPort;

extern PadPort g_padPorts[2];

void Pad_Clear(void);               /* A.BIN 0x06005706 */
int  Pad_Update(unsigned nports);   /* A.BIN 0x0600572A, returns 0 if nports > 2 */

/* A.BIN 0x060042B8: A+B+C held and Start pressed on port 0.
 * Original then reads mode word 0x060252E0 (-1: ignore, 1: jump to BIOS
 * vector @0x0600026C, else further handling). We just report the request. */
int  Sys_CheckSoftReset(void);

/* ---- SHINOBI layer: game pad vars ---- */
extern uint16_t g_gameHeld;          /* 0x060EA428 */
extern uint16_t g_gamePressed;       /* 0x060EA42A */
extern uint16_t g_gamePressedPrev;   /* 0x060EA42C */
extern uint16_t g_actionBtn[3];      /* 0x060EA548/54A/54C, from config table */
extern int16_t  g_buttonConfig;      /* 0x060252C0, option-menu index 0..5 */
extern uint32_t g_demoFlags;         /* 0x06014408, bit 0x8000 = demo (forces cfg 0) */

/* Player flags word read by the latch (player struct 0x060EA2B4 + 4). */
#define PFLAG4_INPUT_OFF   0x0400
#define PFLAG4_MIRROR_LR   0x0200
#define PFLAG4_ALT_INPUT   0x0010   /* tail-jumps 0x060463DC, not ported */

/* SHINOBI 0x06040CAC. Returns 0 normally, 1 if the original would tail-call
 * the 0x060463DC input path, 2 if the demo path (0x06046A98). */
int Game_LatchPad1(uint16_t playerFlags4);

#endif
