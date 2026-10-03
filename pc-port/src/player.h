#ifndef PLAYER_H
#define PLAYER_H
#include <stdint.h>

/* Player struct, SHINOBI global @ 0x060EA2B4 (partial layout, see docs).
 * Field offsets kept in comments so ported code can be checked against
 * the disassembly. Unknown gaps are left as raw bytes. */
typedef struct {
    uint16_t unk00;        /* +0x00 */
    uint16_t flags2;       /* +0x02 (0x08, 0x10, 0x200, 0x2000 seen) */
    uint16_t flags4;       /* +0x04 (see PFLAG4_* in pad.h) */
    int16_t  action;       /* +0x06 current action id */
    uint8_t  _pad08[0x0E];
    int16_t  hp;           /* +0x16 (<1 -> action 0x13) */
    uint8_t  _pad18[0x0A];
    int16_t  prevAction;   /* +0x22 */
    uint8_t  _pad24[0x10];
    uint32_t actionParam;  /* +0x34 set per action (speed/timer?) */
    uint8_t  _pad38[0x20];
    uint16_t flags58;      /* +0x58 */
} Player;

extern Player   g_player;      /* 0x060EA2B4 */
extern uint16_t g_idleFrames;  /* 0x060EA430 */

#endif
