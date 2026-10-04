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
/* Game pad vars, in emulated RAM (ram.h) at their original addresses. */
#define ADDR_GAME_HELD         0x060EA428u
#define ADDR_GAME_PRESSED      0x060EA42Au
#define ADDR_GAME_PRESSED_PREV 0x060EA42Cu
#define ADDR_ACTION_BTN0       0x060EA548u   /* slot0 (default A: shuriken) */
#define ADDR_ACTION_BTN1       0x060EA54Au   /* slot1 (default B: sword/guard) */
#define ADDR_ACTION_BTN2       0x060EA54Cu   /* slot2 (default C: jump) */
#define ADDR_BUTTON_CONFIG     0x060252C0u   /* s16, option-menu index 0..5 */
#define ADDR_DEMO_FLAGS        0x06014408u   /* u32, bit 0x8000 = demo (forces cfg 0) */

/* Player flags word read by the latch (player struct 0x060EA2B4 + 4). */
#define PFLAG4_INPUT_OFF   0x0400
#define PFLAG4_MIRROR_LR   0x0200
#define PFLAG4_ALT_INPUT   0x0010   /* scripted input 0x060463DC (Game_ScriptedInput, pad.c) */

/* SHINOBI 0x06040CAC. Returns 0 normally, 1 if the original tail-called the 0x060463DC
 * scripted input, 2 if it tail-called the attract-demo playback 0x06046A98 (both ported). */
int Game_LatchPad1(uint16_t playerFlags4);

/* Attract demo playback (SHINOBI): recorded pad = {u16 held, u16 frames} records in the
 * overlay's data (one table per demo stage, ending with {0, 0xFFFF}). */
#define ADDR_DEMO_CNT   0x060EA580u   /* u32 frames spent on the current record */
#define ADDR_DEMO_PTR   0x060EA584u   /* u32 current record */
#define ADDR_DEMO_PAD   0x060EA576u   /* u16 x4: held, previous held, pressed, previous pressed */
#define DEMO_TABLE_S0   0x060CA71Cu   /* stage 0 (1-1) */
#define DEMO_TABLE_S2   0x060CA8E4u   /* stage 2 (2-1) */
#define DEMO_TABLE_S4   0x060CAAE4u   /* stage 4 (3-1) */
#define DEMO_TABLE_S6   0x060CAD28u   /* stage 6 (4-1) */
#define DEMO_TABLE_S8   0x060CAED4u   /* stage 8 (5-1) */
void Game_DemoInputInit(void);        /* 0x060469FE (tail of Player_InitForStage 0x06044E18) */
void Game_DemoInput(void);            /* 0x06046A98 */

#endif
