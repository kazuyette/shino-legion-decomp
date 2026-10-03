#ifndef PLAYER_H
#define PLAYER_H
#include <stdint.h>

/* Player struct, SHINOBI global @ 0x060EA2B4 (partial layout, see docs).
 * Field offsets kept in comments so ported code can be checked against
 * the disassembly. Unknown gaps are left as raw bytes. */
/* Action ids (Player.action), see docs "Player actions decoded".
 * Names marked _Q are low-confidence guesses. */
enum {
    ACT_IDLE = 0x00, ACT_WALK = 0x01,
    ACT_SLASH = 0x04, ACT_SLASH_UP = 0x05, ACT_SHURIKEN = 0x07,
    ACT_CROUCH_SLASH = 0x09, ACT_CROUCH_SHURIKEN = 0x0B,
    ACT_AIR_SLASH = 0x0C, ACT_AIR_SLASH_UP = 0x0D, ACT_AIR_SHURIKEN = 0x0E,
    ACT_HURT = 0x12, ACT_DEAD = 0x13, ACT_REACT_Q = 0x14, ACT_SPECIAL_Q = 0x15,
    ACT_IDLE_FIDGET1 = 0x1D, ACT_IDLE_FIDGET2 = 0x1E,
    ACT_GUARD_END = 0x1F, ACT_GUARD_START = 0x20, ACT_GUARD = 0x21,
    ACT_SLASH_HELD = 0x24, ACT_GUARD_RECOIL = 0x26,
    ACT_CROUCH_TRANS = 0x27, ACT_CROUCH = 0x28,
    ACT_CROUCH_HURT = 0x29, ACT_CROUCH_DEAD = 0x2A,
    ACT_CROUCH_GUARD_START = 0x2D, ACT_CROUCH_GUARD = 0x2E, ACT_CROUCH_GUARD_END = 0x2F,
    ACT_CROUCH_SLASH_HELD = 0x31, ACT_CROUCH_GUARD_RECOIL = 0x33,
    ACT_AIR_SPECIAL_Q = 0x3E, ACT_AIR_DEAD = 0x3F,
    ACT_AIR_GUARD_START = 0x43, ACT_AIR_GUARD = 0x44, ACT_AIR_GUARD_END = 0x45,
    ACT_AIR_GUARD_RECOIL = 0x46, ACT_EDGE_IDLE_Q = 0x4F, ACT_BC_SPECIAL_Q = 0x50,
};

#define PFLAG2_HIT       0x0008
#define PFLAG2_GUARDING  0x0040
#define PFLAG2_AIR_USED  0x0080
#define PFLAG2_CROUCHED  0x0800
#define PFLAG2_FACE_LEFT 0x8000
#define PANIM_FINISHED   0x8000   /* flags58 */

typedef struct {
    uint16_t unk00;        /* +0x00 */
    uint16_t flags2;       /* +0x02 PFLAG2_* */
    uint16_t flags4;       /* +0x04 (see PFLAG4_* in pad.h) */
    int16_t  action;       /* +0x06 current action id */
    int32_t  x;            /* +0x08 */
    int32_t  y;            /* +0x0C */
    uint8_t  _pad10[0x06];
    int16_t  hp;           /* +0x16 (<1 -> action 0x13) */
    uint8_t  _pad18[0x0A];
    int16_t  prevAction;   /* +0x22 */
    uint8_t  _pad24[0x0C];
    int32_t  velX;         /* +0x30 16.16 fixed, negated when facing left */
    int32_t  velY;         /* +0x34 16.16 fixed */
    uint8_t  _pad38[0x20];
    uint16_t flags58;      /* +0x58 */
} Player;

extern Player   g_player;      /* 0x060EA2B4 */
extern uint16_t g_idleFrames;  /* 0x060EA430 */

#endif
