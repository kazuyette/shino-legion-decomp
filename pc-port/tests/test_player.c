/* Drives the ported player state machine with scripted input.
 * gcc -std=c11 -Isrc tests/test_player.c src/player.c src/player_stubs.c src/pad.c src/ram.c */
#include <stdio.h>
#include <assert.h>
#include "pad.h"
#include "input.h"
#include "player.h"
#include "ram.h"
static InputPort fake[2];
const InputPort *Input_GetPort(int p) { return &fake[p]; }
static void tick(uint16_t held, int anim_done)
{
    fake[0].connected = 1; fake[0].raw = (uint16_t)~held;
    Pad_Update(2);
    Game_LatchPad1(P16(P_FLAGS4));
    if (anim_done) OR16(PLAYER + P_ANIM, PANIM_FINISHED);
    Player_Tick();
    AND16(PLAYER + P_ANIM, (uint16_t)~PANIM_FINISHED);
    PW16(0x5A, P16(0x5A) + 1);          /* stand-in for the anim player's frame counter */
}
#define EXPECT(a) do { if (Player_Action() != (a)) { printf("line %d: action %02X, expected %02X\n", __LINE__, Player_Action(), (a)); return 1; } } while (0)
int main(void)
{
    Player_Init();
    tick(0, 0);                        EXPECT(ACT_IDLE);
    tick(PAD_B, 0);                    EXPECT(ACT_SLASH);          /* B tap = slash */
    tick(0, 1);                        EXPECT(ACT_RECOVER);        /* anim end -> recover */
    tick(0, 1);                        EXPECT(ACT_IDLE);
    tick(PAD_B, 0);                    EXPECT(ACT_SLASH);
    tick(0, 0); tick(PAD_B, 0);        /* re-press during slash -> combo */
    tick(0, 1);                        EXPECT(ACT_SLASH_UP);
    tick(0, 1);                        EXPECT(ACT_RECOVER);
    tick(0, 1);                        EXPECT(ACT_IDLE);
    tick(PAD_UP, 0); tick(PAD_UP | PAD_B, 0); EXPECT(ACT_SLASH_UP);
    tick(0, 1); tick(0, 1);            EXPECT(ACT_IDLE);
    tick(PAD_Y, 0);                    EXPECT(ACT_GUARD_START);    /* Y = guard */
    tick(PAD_Y, 1);                    EXPECT(ACT_GUARD);
    tick(PAD_Y, 0);                    EXPECT(ACT_GUARD);
    tick(0, 0);                        EXPECT(ACT_RECOVER);        /* release */
    tick(0, 1);                        EXPECT(ACT_IDLE);
    tick(PAD_A, 0);                    EXPECT(ACT_SHURIKEN);       /* A = shuriken */
    OR16(PLAYER + P_ANIM, 1); tick(0, 0);                          /* release frame */
    assert(R16(0x060252B6u) == 19);                                 /* one shuriken used */
    tick(0, 1);                        EXPECT(ACT_IDLE);
    tick(PAD_RIGHT, 0);                EXPECT(ACT_WALK);           /* walk */
    for (int i = 0; i < 0x40; i++) tick(0, 0);
    EXPECT(ACT_WALK);                                              /* stop anim lingers */
    tick(0, 0);                        EXPECT(ACT_IDLE);
    tick(PAD_RIGHT, 0);                EXPECT(ACT_WALK);           /* double tap -> run */
    tick(0, 0); tick(0, 0);
    tick(PAD_RIGHT, 0);                EXPECT(ACT_RUN);
    assert(P16(P_FLAGS2) & PF2_RUNNING);
    tick(0, 1);                        EXPECT(ACT_IDLE);           /* release stops run */
    for (int i = 0; i < 20; i++) tick(0, 0);
    tick(PAD_DOWN, 0);                 EXPECT(ACT_CROUCH_TRANS);   /* down = crouch */
    tick(PAD_DOWN, 1);                 EXPECT(ACT_CROUCH);
    assert(P16(P_FLAGS2) & PF2_CROUCHED);
    AND16(PLAYER + P_FLAGS2, 0xF7FF); PW16(P_ACTION, ACT_IDLE);
    tick(0, 0);
    tick(PAD_C, 0);                    EXPECT(ACT_JUMP);           /* C = jump */
    PW16(P_ACTION, ACT_IDLE); tick(0, 0);
    W16(0x060252BEu, 1);
    tick(PAD_X, 0);                    EXPECT(ACT_NINJUTSU);       /* X = ninjutsu */
    assert(R16(0x0602529Eu) & 0x8000);                             /* world frozen */
    printf("all player tests OK\n");
    return 0;
}
