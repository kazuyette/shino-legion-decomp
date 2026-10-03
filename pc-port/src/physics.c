/* Player/object physics, ported from SHINOBI.dec.bin. Original addresses kept. */
#include "player.h"
#include "player_ext.h"
#include "pad.h"
#include "collision.h"

#define G_WALK_SPEED   0x0601440Cu  /* s32 16.16, A.BIN data (2.25 px/frame) */
#define G_JUMP_RELEASE 0x060EA574u  /* bit0 = jump button released this jump */
#define G_CHEAT_KEY    0x06014414u
#define CHEAT_MAGIC    0x4465268Fu

#define HELD R16(ADDR_GAME_HELD)
#define BTN2 R16(ADDR_ACTION_BTN2)

/* 0x060452A4: generic object integrator (player, enemies, projectiles).
 * Y axis points down; velY > 0 means moving up. */
void Obj_Integrate(uint32_t o)
{
    W32(o + 0x28, R32(o + 0x08));                       /* previous X */
    W32(o + 0x2C, R32(o + 0x0C));                       /* previous Y */
    W32(o + 0x08, (uint32_t)(RS32(o + 0x08) + RS32(o + 0x30) + RS32(o + 0x38) + RS32(o + 0x40)));
    if (!(R16(o + 0x02) & 0x0100)) {                    /* 0x100 = gravity off */
        if (!(R16(o + 0x02) & 0x0400)) {
            W32(o + 0x34, (uint32_t)(RS32(o + 0x34) - 0x5000));   /* gravity */
        } else {                                        /* posture 4: inverted (ceiling?) */
            W32(o + 0x34, (uint32_t)(RS32(o + 0x34) + 0x5000));
            if (!(R16(o + 0x10) & 1) && RS32(o + 0x44) >= 0) W32(o + 0x34, 0);
        }
    }
    if (RS32(o + 0x34) < (int32_t)0xFFF86000) W32(o + 0x34, 0xFFF86000u);   /* terminal fall speed */
    W32(o + 0x0C, (uint32_t)(RS32(o + 0x0C) - RS32(o + 0x34) - RS32(o + 0x3C) - RS32(o + 0x44)));
    W32(o + 0x38, 0); W32(o + 0x3C, 0); W32(o + 0x40, 0); W32(o + 0x44, 0);
}

/* 0x06040DE8: ground horizontal speed from state + facing from the D-pad. */
void Player_GroundVelocity(void)
{
    int moving = 0;
    uint16_t f2 = P16(P_FLAGS2);
    if (!(f2 & 0x2000) && !(P16(P_FLAGS4) & 0x0100)) {
        int a = Player_Action();
        if (!(HELD & 0xC000) || (f2 & PF2_HIT) || (P16(P_FLAGS4) & 0x2000) ||
            a == ACT_GUARD_RECOIL || a == ACT_CROUCH_GUARD_RECOIL || (f2 & 0x0440) == 0x0440) {
            PW16(P_FLAGS2, P16(P_FLAGS2) & 0xFDFF);    /* stop running */
        } else {
            PW32(P_VELX, R32(G_WALK_SPEED));
            if (P16(P_FLAGS2) & PF2_GUARDING) PW32(P_VELX, 0x0001C000);
            if (P16(P_FLAGS2) & PF2_CROUCHED) PW32(P_VELX, 0x0000C000);
            if (P16(P_FLAGS2) & PF2_RUNNING)
                PW32(P_VELX, (P16(P_FLAGS2) & PF2_AIRBORNE) ? 0x0002C000 : 0x00038000);
            moving = 1;
        }
    }
    if ((HELD & 0xC000) &&
        (moving || !(P16(P_FLAGS2) & 0x2000) || (P16(P_ANIM) & 0x20)) &&
        !(P16(P_FLAGS2) & PF2_HIT)) {
        PW16(P_FLAGS2, P16(P_FLAGS2) & 0x7FFF);        /* face right */
        int32_t v = P32(P_VELX);
        PW32(P_VELX, (uint32_t)(v < 0 ? -v : v));
        if (HELD & PAD_LEFT) {
            PW32(P_VELX, (uint32_t)-P32(P_VELX));
            PW16(P_FLAGS2, P16(P_FLAGS2) | PF2_FACE_LEFT);
        }
    }
}

/* 0x060439CC: jump take-off at the end of the crouch-before-jump anim. */
void Player_JumpExec(void)
{
    if (P16(P_ANIM) & PANIM_FINISHED) {
        PW16(P_FLAGS2, P16(P_FLAGS2) | 0x1000);        /* jumping */
        PW16(P_1A, 0);
        W16(0x060EA438u, 0);
        PW32(P_VELY, 0x00060000);
        PW16(P_FLAGS2, P16(P_FLAGS2) | 0x0100);        /* gravity off while rising */
        sub_06007CDE(0x102);
        PW16(P_ACTION, ACT_AIRBORNE);
        if (PS16(P_PREVACT) == ACT_RUN_JUMP) PW16(P_ACTION, 0x39);
    }
}

/* 0x06040FF0: variable jump height: holding the jump button keeps the
 * take-off speed (gravity off) for up to 12 air frames. */
void Player_AirJumpHold(void)
{
    if (!(P16(P_FLAGS2) & PF2_AIRBORNE)) return;
    if (PS16(P_1A) == 1) W16(G_JUMP_RELEASE, 0);
    if ((!(HELD & BTN2) && !(P16(P_FLAGS2) & 2)) || !(P16(P_FLAGS2) & 0x1000))
        W16(G_JUMP_RELEASE, R16(G_JUMP_RELEASE) | 1);
    PW16(P_FLAGS2, P16(P_FLAGS2) & 0xFEFF);            /* gravity back on */
    if (P32(P_VELY) >= 0 &&
        (P16(P_1A) < 2 ||
         (((HELD & BTN2) || (P16(P_FLAGS2) & 2)) && (P16(P_FLAGS2) & 0x1000) &&
          R16(G_JUMP_RELEASE) == 0 && P16(P_1A) < 0x0C)) &&
        (P16(P_FLAGS2) & PF2_AIRBORNE)) {
        PW32(P_VELY, 0x00060000);
        if (!(P16(P_FLAGS2) & PF2_RUNNING)) PW32(P_VELY, (uint32_t)(P32(P_VELY) - 0x8000));
        PW16(P_FLAGS2, P16(P_FLAGS2) | 0x0100);
    }
}

/* 0x060450E4: move + collide (skipped during ninjutsu). */
void Player_Physics(void)
{
    int a = Player_Action();
    if (a == ACT_NINJUTSU || a == ACT_CROUCH_NINJUTSU || a == ACT_AIR_NINJUTSU || a == 0x4A) return;
    Player_ClampToScreen();                             /* 0x060451C8 */
    Obj_Integrate(PLAYER);
    int16_t h = 9;                                      /* hitbox half-height */
    if (P16(P_FLAGS2) & PF2_CROUCHED) h = 6;
    if (P16(P_FLAGS2) & PF2_AIR_USED) h = 8;
    if (a == ACT_COMMAND_Q && P16(P_5A) < 0x0D) h = 6;
    if (a == ACT_DEAD) h = 6;
    PW16(0x1E, 4);
    PW16(0x20, (uint16_t)h);
    if (R32(G_CHEAT_KEY) != CHEAT_MAGIC || (HELD & 0xA0) != 0xA0)   /* cheat: RT+Y = no-clip */
        Obj_CollideMap(PLAYER, 4, h);                   /* 0x06045340 */
    if (P16(P_FLAGS2) & 0x10) {
        PW16(P_FLAGS2, P16(P_FLAGS2) & 0xFFFD);
        sub_06007CDE(0x112);
    }
}
