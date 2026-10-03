#ifndef PLAYER_H
#define PLAYER_H
#include <stdint.h>
#include "ram.h"

/* Player object lives in emulated RAM @ 0x060EA2B4 (see ram.h). */
#define PLAYER 0x060EA2B4u

/* Field offsets (confirmed against the disassembly, see docs). */
#define P_FLAGS2   0x02   /* PF2_* */
#define P_FLAGS4   0x04   /* PFLAG4_* (pad.h) + 0x20/0x40/0x1000/0x2000/0x4000 */
#define P_ACTION   0x06   /* ACT_* */
#define P_X        0x08   /* s32 16.16 */
#define P_Y        0x0C   /* s32 16.16 */
#define P_FLAGS10  0x10   /* collision/environment flags */
#define P_HP       0x16
#define P_1A       0x1A
#define P_PREVACT  0x22
#define P_X_PREV   0x28   /* compared to X to detect "blocked" */
#define P_VELX     0x30   /* s32 16.16, cleared every tick */
#define P_VELY     0x34   /* s32 16.16 */
#define P_PUSHX    0x38   /* s32 16.16, slope push */
#define P_ANIM     0x58   /* PANIM_* */
#define P_5A       0x5A

#define PF2_HIT        0x0008
#define PF2_JUMPTHRU   0x0020
#define PF2_GUARDING   0x0040
#define PF2_AIR_USED   0x0080
#define PF2_RUNNING    0x0200
#define PF2_POSTURE4   0x0400   /* 4th posture (selector 0x060424A6), TBD */
#define PF2_CROUCHED   0x0800
#define PF2_AIRBORNE   0x4000
#define PF2_FACE_LEFT  0x8000

#define PANIM_EVENT_MASK 0x0003
#define PANIM_FINISHED   0x8000

/* Action ids. _Q = low confidence. */
enum {
    ACT_IDLE = 0x00, ACT_WALK = 0x01, ACT_RUN = 0x02, ACT_JUMP_EXEC_Q = 0x03,
    ACT_SLASH = 0x04, ACT_SLASH_UP = 0x05, ACT_WALLJUMP_Q = 0x06, ACT_SHURIKEN = 0x07,
    ACT_CROUCH_SLASH = 0x09, ACT_CROUCH_SHURIKEN = 0x0B,
    ACT_AIR_SLASH = 0x0C, ACT_AIR_SLASH_UP = 0x0D, ACT_AIR_SHURIKEN = 0x0E,
    ACT_GUARD_WALK = 0x0F, ACT_CROUCH_GUARD_WALK = 0x10,
    ACT_HURT = 0x12, ACT_DEAD = 0x13, ACT_REACT_Q = 0x14, ACT_NINJUTSU = 0x15,
    ACT_AIR_16_Q = 0x16, ACT_DOUBLEJUMP_Q = 0x17, ACT_AIR_FLIP_ATTACK_Q = 0x1A,
    ACT_RUN_SLASH = 0x1B, ACT_IDLE_FIDGET1 = 0x1D, ACT_IDLE_FIDGET2 = 0x1E,
    ACT_RECOVER = 0x1F, ACT_GUARD_START = 0x20, ACT_GUARD = 0x21,
    ACT_LUNGE_Q = 0x23, ACT_SLASH_DIAG = 0x24, ACT_COMMAND_Q = 0x25,
    ACT_GUARD_RECOIL = 0x26, ACT_CROUCH_TRANS = 0x27, ACT_CROUCH = 0x28,
    ACT_CROUCH_HURT = 0x29, ACT_CROUCH_DEAD = 0x2A, ACT_CROUCH_NINJUTSU = 0x2C,
    ACT_CROUCH_GUARD_START = 0x2D, ACT_CROUCH_GUARD = 0x2E, ACT_CROUCH_GUARD_END = 0x2F,
    ACT_CROUCH_IDLE2 = 0x30, ACT_CROUCH_SLASH_HELD = 0x31, ACT_CROUCH_GUARD_RECOIL = 0x33,
    ACT_JUMP = 0x34, ACT_RUN_JUMP = 0x36, ACT_AIRBORNE = 0x37,
    ACT_HAZARD_HIT_Q = 0x3B, ACT_LANDING_Q = 0x3D, ACT_AIR_NINJUTSU = 0x3E, ACT_AIR_DEAD = 0x3F,
    ACT_POSTURE4_IDLE = 0x40,
    ACT_AIR_GUARD_START = 0x43, ACT_AIR_GUARD = 0x44, ACT_AIR_GUARD_END = 0x45,
    ACT_AIR_GUARD_RECOIL = 0x46, ACT_AIR_RECOVER = 0x47, ACT_LAND_ON_ENEMY_Q = 0x4D,
    ACT_COMMAND_SLASH_Q = 0x4E, ACT_EDGE_IDLE_Q = 0x4F, ACT_RUN_SPECIAL = 0x50,
};

/* Player accessors. */
static inline uint16_t P16(uint32_t off)            { return R16(PLAYER + off); }
static inline int16_t  PS16(uint32_t off)           { return RS16(PLAYER + off); }
static inline int32_t  P32(uint32_t off)            { return RS32(PLAYER + off); }
static inline void     PW16(uint32_t off, uint32_t v) { W16(PLAYER + off, v); }
static inline void     PW32(uint32_t off, uint32_t v) { W32(PLAYER + off, v); }
static inline int16_t  Player_Action(void)          { return PS16(P_ACTION); }

void Player_Init(void);               /* PC-side */
void Player_Tick(void);               /* 0x06040B72 */
void Player_SelectPosture(void);      /* 0x06042910 */
int  Player_SelectAction_Stand(void); /* 0x060411FC */
void Player_UpdateAction(void);       /* 0x06042E66 */
/* physics.c */
void Obj_Integrate(uint32_t obj);     /* 0x060452A4 */
void Player_GroundVelocity(void);     /* 0x06040DE8 */
void Player_JumpExec(void);           /* 0x060439CC */
void Player_AirJumpHold(void);        /* 0x06040FF0 */
void Player_Physics(void);            /* 0x060450E4 */

#endif
