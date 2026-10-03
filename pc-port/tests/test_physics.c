/* gcc -std=c11 -Isrc tests/test_physics.c src/player.c src/physics.c src/player_stubs.c src/pad.c src/ram.c */
#include <stdio.h>
#include <assert.h>
#include "pad.h"
#include "input.h"
#include "player.h"
#include "ram.h"
#include "test_world.h"
static InputPort fake[2];
const InputPort *Input_GetPort(int p) { return &fake[p]; }
static int frames_in_act, last_act = -1;
static void tick(uint16_t held)
{
    fake[0].connected = 1; fake[0].raw = (uint16_t)~held;
    Pad_Update(2);
    Game_LatchPad1(P16(P_FLAGS4));
    if (frames_in_act >= 4) OR16(PLAYER + P_ANIM, PANIM_FINISHED);   /* 4-frame fake anims */
    Player_Tick();
    AND16(PLAYER + P_ANIM, (uint16_t)~PANIM_FINISHED);
    if (Player_Action() != last_act) { last_act = Player_Action(); frames_in_act = 0; } else frames_in_act++;
}
static int32_t jump_peak(int hold_frames)
{
    int32_t floor_y = P32(P_Y), peak = floor_y;
    tick(PAD_C);
    for (int i = 0; i < 120; i++) {
        tick(i < hold_frames ? PAD_C : 0);
        if (P32(P_Y) < peak) peak = P32(P_Y);
        if (i > 10 && !(P16(P_FLAGS2) & PF2_AIRBORNE) && P32(P_Y) == floor_y) break;
    }
    tick(0);
    assert((P32(P_Y) >> 16) == (floor_y >> 16) - 1 || P32(P_Y) == floor_y);   /* landed back (snapped) */
    assert(Player_Action() == 0x3B);                   /* landing (set by the real collision) */
    for (int i = 0; i < 10; i++) tick(0);
    assert(Player_Action() == ACT_IDLE);
    return floor_y - peak;
}
int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    Player_Init();
    Test_BuildFlatWorld();
    for (int i = 0; i < 5; i++) tick(0);               /* settle on the floor */
    int32_t x0 = P32(P_X);
    tick(PAD_RIGHT); tick(PAD_RIGHT);
    assert(P32(P_X) - x0 == 2 * 0x24000);              /* walk: 2.25 px/frame */
    assert(!(P16(P_FLAGS2) & PF2_FACE_LEFT));
    x0 = P32(P_X); tick(PAD_LEFT);
    assert(P32(P_X) - x0 == -0x24000 && (P16(P_FLAGS2) & PF2_FACE_LEFT));
    for (int i = 0; i < 80; i++) tick(0);
    tick(PAD_RIGHT); tick(0); tick(0); tick(PAD_RIGHT);   /* double tap */
    x0 = P32(P_X); tick(PAD_RIGHT);
    assert(Player_Action() == ACT_RUN && P32(P_X) - x0 == 0x38000);   /* run: 3.5 px/frame */
    for (int i = 0; i < 80; i++) tick(0);
    int32_t lo = jump_peak(0), hi = jump_peak(30);
    printf("jump peak: tap %.1f px, held %.1f px\n", lo / 65536.0, hi / 65536.0);
    assert(lo > 0 && hi > lo);                         /* variable jump height */

    /* air poses + double jump near the apex */
    int saw_rise = 0, saw_apex = 0, saw_fall = 0, dj = 0;
    tick(PAD_C);
    for (int i = 0; i < 120; i++) {
        int a = Player_Action();
        saw_rise |= a == 0x37; saw_apex |= a == 0x35; saw_fall |= a == 0x3A;
        uint16_t in = PAD_C;
        if (a == 0x35 && !dj) { tick(0); tick(PAD_C); dj = Player_Action() == ACT_WALLJUMP_Q; continue; }
        tick(i < 30 ? in : 0);
        if (!(P16(P_FLAGS2) & PF2_AIRBORNE) && i > 10) break;
    }
    if (!(saw_rise && saw_apex && saw_fall && dj)) printf("rise %d apex %d fall %d dj %d act %02X\n", saw_rise, saw_apex, saw_fall, dj, Player_Action());
    assert(saw_rise && saw_apex && saw_fall && dj);
    for (int i = 0; i < 10; i++) tick(0);

    /* Down+B while falling = down stab (0x16) */
    tick(PAD_C);
    int stab = 0;
    for (int i = 0; i < 120 && !stab; i++) {
        if (P32(P_VELY) < 0 && (P16(P_FLAGS2) & PF2_AIRBORNE)) { tick(PAD_DOWN | PAD_B); stab = Player_Action() == ACT_AIR_16_Q; }
        else tick(0);
    }
    assert(stab);
    for (int i = 0; i < 120; i++) tick(0);

    /* crouch: Down -> crouch, B -> crouch slash, release -> stand up */
    tick(PAD_DOWN); for (int i = 0; i < 6; i++) tick(PAD_DOWN);
    assert(Player_Action() == ACT_CROUCH);
    tick(PAD_DOWN | PAD_B); assert(Player_Action() == ACT_CROUCH_SLASH);
    for (int i = 0; i < 6; i++) tick(PAD_DOWN);
    assert(Player_Action() == ACT_CROUCH_IDLE2);       /* crouch slash recovers */
    for (int i = 0; i < 6; i++) tick(PAD_DOWN);
    tick(0); assert(Player_Action() == ACT_CROUCH_TRANS);   /* release Down: stand up */
    for (int i = 0; i < 6; i++) tick(0);
    assert(Player_Action() == ACT_IDLE && !(P16(P_FLAGS2) & PF2_CROUCHED));
    printf("all physics tests OK\n");
    return 0;
}
