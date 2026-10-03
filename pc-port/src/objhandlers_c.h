#ifndef OBJHANDLERS_C_H
#define OBJHANDLERS_C_H
/* Stage state checks run at the end of the stage tick (SHINOBI 0x06040B44, last call
 * 0x06046190), ported in objhandlers_c.c.
 * G_STAGE_STATE 0x0602529E: 0x8000 world frozen; bit 3 = request just posted;
 * bits 0-2 = end-of-stage sequence: 1 death, 2 stage clear, 3 demo over.
 * G_GAME_MODE 0x060DAF72: SHINOBI top-level mode (dispatcher 0x0606B54C):
 * 5 load stage (0x0606B6EC), 6 stage frames (0x0606B8FA), 7 stage-clear tally
 * (0x0606BA08), 9 game over (0x0606B58C), 11 back to DIRECTOR (0x0606BAA4).
 * G_DIRECTOR_REQ 0x060252CC: what DIRECTOR shows next (8 STGCLEAR.PBC, 5 game over,
 * 4 ending, 0/1 demo end). */
#define G_STAGE_STATE  0x0602529Eu
#define G_GAME_MODE    0x060DAF72u
#define G_DIRECTOR_REQ 0x060252CCu
#define G_CHECKPOINT   0x060EC658u   /* u16 restart point 0..3 (table 0x060A8D94) */
#define G_RESPAWN      0x060EC65Au   /* u16 1 = restarting after a death */

int  Stage_CheckExit(void);    /* 0x06045F64: 1 when Sho stands in the exit zone (posts state 2|8) */
void Player_CheckFall(void);   /* 0x06045EE4: fell below the screen -> HP 0 (death) */
void Stage_UpdateState(void);  /* 0x06046190: the two checks above + death/clear/demo sequence */
#endif
