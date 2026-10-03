/* Player logic, ported from SHINOBI.dec.bin (base 0x06040000).
 * Faithful translation of the Ghidra decompilation; original addresses are
 * kept for every global so each line can be checked against the binary.
 * Calls into code not yet ported go through player_ext.h stubs. */
#include "player.h"
#include "player_ext.h"
#include "pad.h"

/* Globals touched by the player code (addresses in emulated RAM). */
#define G_SYSFLAGS     0x0602529Eu  /* 0x8000 = world frozen (ninjutsu), low 3 bits = game-over seq */
#define G_CHEAT_FLAGS  0x0602529Cu  /* bit0 = invincible (needs cheat key) */
#define G_STAGE        0x0602526Au
#define G_HP_MAX       0x060252B0u
#define G_SHURIKENS    0x060252B6u
#define G_MAGICFLAGS   0x060252BAu  /* 0x2000 ninjutsu active, 0x4000 run-special armed */
#define G_MAGIC_PTS    0x060252BCu
#define G_MAGIC_AVAIL  0x060252BEu
#define G_CHEAT_KEY    0x06014414u
#define CHEAT_MAGIC    0x4465268Fu
#define G_DIR_TAP      0x060EA42Eu  /* last direction tap: 0x8000 = side, low = age (double-tap run) */
#define G_IDLE_CTR     0x060EA430u
#define G_CMD_BUF      0x060EA432u  /* command-input buffer (0xF00 field) */
#define G_RUNSPECIAL_V 0x060EA434u
#define G_JUMP_T1      0x060EA436u  /* jump-buffer timers */
#define G_JUMP_T0      0x060EA438u
#define G_WALK_CTR     0x060EA43Au
#define G_FIDGET_TMR   0x060EA444u
#define G_PROBE_PARAM  0x060EA64Au
#define G_HAZARD_PTR   0x060EA41Cu  /* pointer to hazard/invuln counters */
#define G_STAGE11_OBJ  0x060EC65Cu

#define HELD    R16(ADDR_GAME_HELD)
#define PRESSED R16(ADDR_GAME_PRESSED)
#define PPREV   R16(ADDR_GAME_PRESSED_PREV)
#define BTN0    R16(ADDR_ACTION_BTN0)
#define BTN1    R16(ADDR_ACTION_BTN1)
#define BTN2    R16(ADDR_ACTION_BTN2)

#define DIR_LR   0xC000u
#define DIR_DOWN 0x2000u
#define DIR_UP   0x1000u

static void set_act(int a) { PW16(P_ACTION, (uint16_t)a); }
static void p2_or(uint16_t m)  { OR16(PLAYER + P_FLAGS2, m); }
static void p2_and(uint16_t m) { AND16(PLAYER + P_FLAGS2, m); }
static int  anim_done(void)    { return (P16(P_ANIM) & PANIM_FINISHED) != 0; }
/* velX = v, negated when facing left (recurring original idiom) */
static void set_velx_facing(int32_t v) { PW32(P_VELX, (uint32_t)((P16(P_FLAGS2) & PF2_FACE_LEFT) ? -v : v)); }

/* 0x06041154: slope push while standing still or crouched. */
static int32_t Player_SlopePush(void)
{
    int32_t d = 0;
    if (!(R16(G_SYSFLAGS) & 0x8000)) {
        uint16_t s = sub_0604588E(P32(P_X), P32(P_Y), 5, RS16(G_PROBE_PARAM));
        if ((s == 1 || s == 2) && (!(HELD & DIR_LR) || (P16(P_FLAGS2) & PF2_CROUCHED))) {
            d = (P16(P_FLAGS2) & PF2_FACE_LEFT) ? (int32_t)0xFFFE0000 : 0x00020000;
            PW32(P_PUSHX, (uint32_t)(P32(P_PUSHX) + d));
        }
    }
    return d;
}

/* 0x06041ABA: air state 0x16 touching something -> bounce (0x4D). */
static int Player_CheckBounce(void)
{
    if (Player_Action() == ACT_AIR_16_Q &&
        ((P16(P_FLAGS2) & 0x10) || (P16(P_FLAGS4) & 0x0800))) {
        p2_or(0x0002);
        set_act(ACT_LAND_ON_ENEMY_Q);
        PW16(P_1A, 0);
        W16(G_JUMP_T0, 0);
        PW32(P_VELY, 0x00060000);
        p2_or(0x1000);
        p2_or(0x0100);
        sub_06007CDE(0x114);
        return 1;
    }
    return 0;
}

/* 0x06041990: jump start (slot2, with input buffer), or drop through floor (Down+jump). */
static int Player_TryJump(void)
{
    int d = (int)R16(G_JUMP_T1) - (int)R16(G_JUMP_T0);
    int act = Player_Action();
    if (((PRESSED & BTN2) || (d < 8 && d > 0 && R16(G_JUMP_T0) != 0)) &&
        act != ACT_JUMP && act != ACT_RUN_JUMP && !(P16(P_FLAGS10) & 0x8000)) {
        if ((HELD & DIR_DOWN) && (P16(P_FLAGS10) & 0x4800)) {
            p2_or(PF2_JUMPTHRU);
            p2_and(0xFBFF);
            p2_and(0xF7FF);
            AND16(PLAYER + P_FLAGS10, 0xDFFF);
            PW32(P_VELY, (uint32_t)(P32(P_VELY) - 0x8000));
            return 1;
        }
        set_act(ACT_JUMP);
        if (PS16(P_PREVACT) == ACT_RUN) set_act(ACT_RUN_JUMP);
        p2_and(0xFBFF);
        p2_and(0xF7FF);
        PW16(P_1A, 0);
        W16(G_JUMP_T0, 0);
        return 1;
    }
    return 0;
}

/* 0x060411FC */
int Player_SelectAction_Stand(void)
{
    int v, a;
    Player_SlopePush();

    a = Player_Action();
    int idle = (a == ACT_IDLE || a == ACT_IDLE_FIDGET1 || a == ACT_IDLE_FIDGET2 || a == ACT_EDGE_IDLE_Q);
    if (idle || (a == ACT_WALK && R16(G_WALK_CTR) != 0)) {
        if (R16(G_IDLE_CTR) < 0x7FFF) W16(G_IDLE_CTR, R16(G_IDLE_CTR) + 1);
    } else {
        W16(G_IDLE_CTR, 0);
    }

    if (P16(P_FLAGS2) & PF2_HIT) {                      /* got hit */
        a = Player_Action();
        if (a != ACT_HURT && a != ACT_DEAD) {
            if (a != ACT_AIR_DEAD) sub_060410F8();
            set_act(ACT_HURT);
            if (!(P16(P_FLAGS4) & 0x40)) PW32(P_VELY, 0x00030000);
        }
        if (PS16(P_HP) < 1 && Player_Action() != ACT_DEAD) {
            if (PS16(P_PREVACT) != ACT_AIR_DEAD) sub_060410F8();
            set_act(ACT_DEAD);
            PW32(P_VELY, 0);
        }
        v = P16(P_FLAGS2) & 0xFBFF;
        goto store_flags2;
    }
    if (P16(P_FLAGS4) & 0x4000) {
        set_act(ACT_REACT_Q);
        v = P16(P_FLAGS2) & 0xFBFF;
        goto store_flags2;
    }

    if (Player_Action() == ACT_NINJUTSU) return ACT_NINJUTSU;
    if ((R16(G_MAGICFLAGS) & 0x2000) || (R16(G_MAGIC_AVAIL) != 0 && (PRESSED & PAD_X))) {
        set_act(ACT_NINJUTSU);
        sub_06007CDE(0x116);
        OR16(G_SYSFLAGS, 0x8000);                       /* freeze the world */
        if (R16(G_MAGICFLAGS) & 0x2000) return ACT_NINJUTSU;
        W16(G_MAGICFLAGS, (R16(G_MAGICFLAGS) & 0xFFF0) | 0x2002);
        return ACT_NINJUTSU;
    }
    if ((v = Player_CheckBounce()) != 0) return v;
    if (Player_Action() == ACT_RUN_SPECIAL) return ACT_RUN_SPECIAL;

    a = Player_Action();
    if ((a == ACT_RUN || PS16(P_PREVACT) == ACT_RUN_SLASH || (P16(P_FLAGS2) & PF2_RUNNING)) &&
        ((PRESSED | PPREV) & 0x0300) == 0x0300 &&           /* B+C together */
        (R16(G_MAGICFLAGS) & 0x40F0) == 0x4040) {
        set_act(ACT_RUN_SPECIAL);
        PW32(P_VELY, 0x00034000);
        W16(G_MAGIC_PTS, R16(G_MAGIC_PTS) - 2);
        if (RS16(G_MAGIC_PTS) > 0) return ACT_RUN_SPECIAL;
        W16(G_MAGIC_PTS, 0);
        AND16(G_MAGICFLAGS, 0xBFFF);
        return ACT_RUN_SPECIAL;
    }
    if (a == ACT_HAZARD_HIT_Q) {
        v = Player_TryJump();
        W16(G_JUMP_T0, 0);
        return v;
    }
    if ((P16(P_FLAGS2) & 0x10) && a != ACT_RUN_SLASH) {
        set_act(ACT_HAZARD_HIT_Q);
        if (!(P16(P_FLAGS2) & PF2_RUNNING)) return (int16_t)P16(P_FLAGS2);
        set_act(ACT_RUN);
        W16(G_DIR_TAP, 0);
        return ACT_RUN;
    }

    if ((a == ACT_GUARD || a == ACT_GUARD_WALK) && (P16(P_FLAGS4) & 0x1000)) {
        v = ACT_GUARD_RECOIL;
        goto store_act;
    }
    if (a == ACT_COMMAND_Q && R16(G_SHURIKENS) != 0 && (P16(P_ANIM) & 1) && (HELD & BTN0)) {
        sub_06042C88();
        AND16(PLAYER + P_ANIM, 0xFFFC);
        return 0x58;
    }
    if (a == ACT_COMMAND_Q && (P16(P_ANIM) & 2) && (PRESSED & BTN1)) {
        v = ACT_COMMAND_SLASH_Q;
        goto store_act;
    }
    if (a == ACT_CROUCH_TRANS && ((PRESSED | PPREV) & BTN1)) {
        set_act(ACT_CROUCH_SLASH_HELD);
        v = P16(P_FLAGS2) | 0x0800;
        goto store_flags2;
    }
    if (a == ACT_RUN_SLASH || a == ACT_COMMAND_Q || a == ACT_GUARD_RECOIL || a == ACT_CROUCH_TRANS)
        return a;
    if (P16(P_FLAGS2) & 0x1000) return (int16_t)P16(P_FLAGS2);
    if (a == ACT_COMMAND_SLASH_Q || a == ACT_JUMP || a == ACT_RUN_JUMP) return a;
    if ((v = Player_TryJump()) != 0) return v;

    if ((R16(G_CMD_BUF) & 0x0F00) == 0x0300) { v = ACT_COMMAND_Q; goto store_act; }
    if ((PRESSED & BTN0) && R16(G_SHURIKENS) != 0) { v = ACT_SHURIKEN; goto store_act; }

    if ((PRESSED & BTN1) &&
        (R16(G_STAGE) != 8 || !(P16(P_FLAGS10) & 0x2000)) &&
        (P16(P_FLAGS2) & PF2_RUNNING)) {
        PW32(P_VELY, 0x00030000);
        set_act(ACT_RUN_SLASH);
        v = P16(P_FLAGS2) & 0xFDFF;
        goto store_flags2;
    }
    a = Player_Action();
    if (((HELD & 0x30) && (idle || a == ACT_RECOVER || a == ACT_WALK || a == ACT_RUN)) ||
        (PRESSED & 0x30)) {                               /* Y/Z = guard */
        v = ACT_GUARD_START;
        goto store_act;
    }
    if (a == ACT_SLASH_UP || a == ACT_SLASH || a == ACT_SLASH_DIAG || a == ACT_LUNGE_Q || a == 0x1C)
        return a;
    if (((PRESSED | PPREV) & DIR_LR) && (BTN1 & (PRESSED | PPREV))) {
        v = ACT_LUNGE_Q;                                  /* direction + B together */
        goto store_act;
    }
    if (PRESSED & BTN1) {
        set_act(ACT_SLASH);
        if (HELD & DIR_UP) set_act(ACT_SLASH_UP);
        if (!(HELD & DIR_LR) || !(HELD & DIR_UP)) return Player_Action();
        set_act(ACT_SLASH_DIAG);
        return ACT_SLASH_DIAG;
    }
    if ((HELD & BTN1) && (idle || a == ACT_RECOVER || a == ACT_WALK || a == ACT_RUN)) {
        v = ACT_GUARD_START;                              /* hold B = guard */
        goto store_act;
    }
    {
        uint16_t tap = R16(G_DIR_TAP);
        uint16_t ic = R16(G_IDLE_CTR);
        if (tap != 0 &&
            (((tap & 0x7FFF) < 0x0D && ic < 0x0D && ic != 0) ||
             ((tap & 0x8000) != (P16(P_FLAGS2) & 0x8000) && (tap & 0x7FFF) < 0x10 && ic < 0x10)) &&
            (PRESSED & DIR_LR)) {                         /* double tap -> run */
            set_act(ACT_RUN);
            v = P16(P_FLAGS2) | PF2_RUNNING;
            PW16(P_FLAGS2, (uint16_t)v);
            W16(G_DIR_TAP, 0);
            return v;
        }
    }
    if (HELD & DIR_DOWN) { v = ACT_CROUCH_TRANS; goto store_act; }
    if ((HELD & DIR_LR) && Player_Action() == ACT_GUARD && ((BTN1 + 0x30) & HELD)) {
        v = ACT_GUARD_WALK;
        goto store_act;
    }
    a = Player_Action();
    if ((HELD & DIR_LR) && (idle || a == ACT_RECOVER)) {
        set_act(ACT_WALK);
        W16(G_WALK_CTR, 0);
        return ACT_WALK;
    }
    if (!idle) return a;
    if (a == ACT_EDGE_IDLE_Q) return a;
    if (a == ACT_IDLE && R16(G_IDLE_CTR) > 0x1E) set_act(ACT_IDLE_FIDGET1);
    if (R16(G_STAGE) == 0x11 && RS16(G_STAGE11_OBJ + 0x16) < 1) return RS16(G_STAGE11_OBJ + 0x16);
    if ((v = sub_0600A604()) != 0) return v;
    set_act(ACT_IDLE_FIDGET2);
    {
        int16_t t = 0xB4;
        if (RS16(G_FIDGET_TMR) > 0x40) {
            sub_0602DDD2();
            t = (int16_t)(sub_0600CEF4() * 0x60 + 0x20);
        }
        W16(G_FIDGET_TMR, (uint16_t)t);
    }
    return ACT_IDLE_FIDGET2;

store_act:
    set_act(v);
    return v;
store_flags2:
    PW16(P_FLAGS2, (uint16_t)v);
    return v;
}

/* 0x06043DE0: runs before and after Player_UpdateAction. On action change:
 * remember previous action, (re)start the animation, set the "attack hitbox
 * active" flag (flags2 & 4) for attacking actions, clear combo bits. */
static void Player_OnActionChange(void)
{
    int16_t old5a = PS16(P_5A);
    int a = Player_Action(), prev = PS16(P_PREVACT);
    if (a == prev) return;
    PW16(P_PREVACT, (uint16_t)a);
    Anim_Start(PLAYER + P_ANIM, 0x060A522Cu, 0x060CA5ACu, 0, a);   /* A.BIN 0x0600648C */
    if (prev == ACT_RECOVER && a == ACT_GUARD_START)              PW16(P_5A, (uint16_t)(5 - old5a));
    if (prev == ACT_CROUCH_IDLE2 && a == ACT_CROUCH_GUARD_START) PW16(P_5A, (uint16_t)(4 - old5a));
    if (prev == ACT_AIR_RECOVER && a == ACT_AIR_GUARD_START)    PW16(P_5A, (uint16_t)(5 - old5a));
    if (a == ACT_DEAD && prev == ACT_AIR_DEAD)                   PW16(P_5A, 7);
    if (a == ACT_WALK && (PRESSED & DIR_LR))
        W16(G_DIR_TAP, (P16(P_FLAGS2) & 0x8000) | 1);             /* start double-tap window */
    else if (a != ACT_IDLE)
        W16(G_DIR_TAP, 0);
    if (a != ACT_RUN && !(P16(P_FLAGS2) & 0x1000) && a != ACT_RUN_JUMP && a != 0x3A && a != 0x38)
        p2_and(0xFDFF);                                            /* leaving run */
    p2_and(0xFFFB);
    switch (a) {
    case 0x04: case 0x05: case 0x09: case 0x0A: case 0x0C: case 0x0D: case 0x17: case 0x18:
    case 0x1B: case 0x1C: case 0x24: case 0x32: case 0x3C: case 0x49:
        p2_or(0x0004);                                             /* attack active */
        break;
    }
    if (a != ACT_SLASH_UP && a != ACT_SLASH && a != 0x1C)
        AND16(PLAYER + P_FLAGS4, 0xFFFC);
}

/* 0x06043944: walk execution. After the direction is released the walk
 * action lingers (stop animation) for up to 0x40 frames. */
static void Player_WalkExec(void)
{
    if (!(HELD & DIR_LR)) {
        W16(G_WALK_CTR, R16(G_WALK_CTR) + 1);
        if (R16(G_WALK_CTR) > 0x40 || Player_Action() == 8) {
            set_act(ACT_IDLE);
            W16(G_WALK_CTR, 0);
        }
    } else {
        W16(G_WALK_CTR, 0);
        if (Player_Action() == ACT_WALK && (PRESSED & DIR_LR))
            W16(G_DIR_TAP, (P16(P_FLAGS2) & 0x8000) | 1);
    }
}

/* 0x06043AA0: standing slash execution + combo chaining (4 -> 5 -> 0x1C). */
static void Player_SlashExec(void)
{
    if (!(P16(P_ANIM) & 1) && (PRESSED & BTN1) &&
        (R16(G_STAGE) != 8 || !(P16(P_FLAGS10) & 0x2000)) &&
        (uint32_t)P16(0x72) + (uint32_t)P16(P_5A) != 0) {          /* re-press during the slash */
        if (Player_Action() == ACT_SLASH) OR16(PLAYER + P_FLAGS4, 1);
        else if (Player_Action() == ACT_SLASH_UP && (P16(P_FLAGS4) & 1)) OR16(PLAYER + P_FLAGS4, 2);
    }
    if (Player_Action() == ACT_SLASH_UP && !(P16(P_ANIM) & 1) && (P16(P_FLAGS4) & 1))
        set_velx_facing(0x00030000);                                /* combo step forward */
    if (anim_done() || (P16(P_FLAGS4) & 0x20)) {
        int prev = Player_Action();
        set_act(ACT_RECOVER);
        if (prev == ACT_SLASH) { if (P16(P_FLAGS4) & 1) set_act(ACT_SLASH_UP); }
        else if (prev == ACT_SLASH_UP && (P16(P_FLAGS4) & 2)) set_act(0x1C);
        if (Player_Action() == ACT_RECOVER) AND16(PLAYER + P_FLAGS4, 0xFFFC);
    }
}

/* 0x06043CB8: shuriken throw on the release frame (anim event bit 0). */
static void Player_ShurikenExec(void)
{
    int fire = 0;
    int32_t dx = 0x00180000, dy = 0;
    int a = Player_Action();
    if ((P16(P_ANIM) & 1) && (a == ACT_SHURIKEN || a == ACT_AIR_SHURIKEN)) { fire = 1; dy = (int32_t)0xFFD00000; }
    if ((P16(P_ANIM) & 1) && a == ACT_CROUCH_SHURIKEN)                    { fire = 1; dy = (int32_t)0xFFE00000; }
    if ((P16(P_ANIM) & 1) && a == 0x19)                                   { fire = 1; dy = (int32_t)0xFFD60000; }
    if (fire) {
        int angle = 0;
        if (P16(P_FLAGS2) & PF2_FACE_LEFT) {
            int t = angle + 0x80;
            dx = -dx;
            angle = (t < 0 ? (-angle - 0x80) : t) & 0xFF;
        }
        sub_06042D80(dx, dy, angle, 0x00064000);                    /* spawn projectile */
        sub_06007CDE(0x100);
        W16(G_SHURIKENS, R16(G_SHURIKENS) - 1);
        AND16(PLAYER + P_ANIM, 0xFFFE);
    }
    if (anim_done()) set_act(ACT_IDLE);
}

/* 0x06042E66: executes the current action (81-case switch in the original). */
void Player_UpdateAction(void)
{
    const uint32_t GUARD_MASK = (uint32_t)BTN1 + 0x30;   /* B (slot1) or Y/Z */

    switch (Player_Action()) {
    case 0x00: break;
    case 0x01: case 0x08: Player_WalkExec(); break;
    case 0x02:
        if ((P16(P_ANIM) & 3) == 0 &&
            (!(HELD & DIR_LR) ||
             (R32(PLAYER + P_X) == R32(PLAYER + P_X_PREV) && RS16(PLAYER + P_5A) > 4))) {
            OR16(PLAYER + P_ANIM, 0x80);
            p2_and(0xFDFF);                                  /* stop running */
        }
        if (anim_done()) set_act(ACT_IDLE);
        break;
    case 0x03: case 0x34: case 0x36: Player_JumpExec(); break;
    case 0x04: case 0x05: Player_SlashExec(); sub_06046782(); break;
    case 0x06: case 0x17: sub_06046782(); sub_06043C58(); break;
    case 0x07: case 0x0B: case 0x0E: case 0x19: Player_ShurikenExec(); break;
    case 0x09: sub_06046782(); sub_06043BC8(); break;
    case 0x0C: case 0x0D: case 0x3C:
        sub_06046782();
        if (anim_done() || (P16(P_FLAGS4) & 0x20)) set_act(ACT_LANDING_Q);
        break;
    case 0x0F: case 0x10:
        p2_or(PF2_GUARDING);
        if (!(HELD & DIR_LR) || !(GUARD_MASK & HELD))
            set_act(Player_Action() == 0x0F ? ACT_GUARD : ACT_CROUCH_GUARD);
        break;
    case 0x11:
        if ((HELD & DIR_LR) || R16(PLAYER + P_5A) == 0) {
            PW32(P_VELX, (uint32_t)((P16(P_ANIM) & 0xF) * 0x10000 + 0x10));
            AND16(PLAYER + P_ANIM, 0xFFF0);
            if (P16(P_FLAGS2) & PF2_FACE_LEFT) PW32(P_VELX, (uint32_t)-P32(P_VELX));
            break;
        }
        set_act(ACT_IDLE);
        break;
    case 0x12: case 0x29: case 0x4B:
        if (!(P16(P_FLAGS4) & 0x40) && !(P16(P_ANIM) & 1)) set_velx_facing((int32_t)0xFFFE0000);
        if (anim_done()) {
            set_act(ACT_IDLE);
            W16(R32(G_HAZARD_PTR), 0x60);                    /* post-hit invulnerability? */
            p2_and(0xFFF7);
            p2_and(0xEFFF);
            AND16(PLAYER + P_FLAGS4, 0xFFBF);
        }
        break;
    case 0x13: case 0x2A: case 0x3F:
        if (!(P16(P_FLAGS2) & 0x2000)) set_velx_facing((int32_t)0xFFFF0000);
        if (P16(P_ANIM) & 3) {
            sub_06007E00();
            sub_06007CDE(2);
            AND16(PLAYER + P_ANIM, 0xFFFC);
        }
        if (anim_done() && (R16(G_SYSFLAGS) & 7) == 0) {
            W16(G_SYSFLAGS, (R16(G_SYSFLAGS) & 0xFFF8) | 9);  /* start death sequence */
            AND16(G_MAGICFLAGS, 0xBFFF);
        }
        break;
    case 0x14: case 0x2B:
        if (anim_done()) { set_act(ACT_IDLE); AND16(PLAYER + P_FLAGS4, 0xBFFF); }
        break;
    case 0x15: case 0x2C: case 0x3E: case 0x4A: sub_060437C0(); break;
    case 0x16: sub_06046782(); break;
    case 0x18: case 0x48: case 0x49:
        sub_06046782();
        if (anim_done() || (P16(P_FLAGS4) & 0x20)) set_act(ACT_AIR_RECOVER);
        break;
    case 0x1A:
        PW32(P_VELY, 0x5000);
        W16(G_JUMP_T0, 0);
        if (P16(P_ANIM) & 3) {
            PW32(P_VELY, 0x00050000);
            set_velx_facing((int32_t)0xFFFD0000);
            if ((P16(P_ANIM) & 3) == 1) {
                Obj_Spawn(4, P32(P_X), P32(P_Y) + (int32_t)0xFFE00000, P16(P_FLAGS2) & PF2_FACE_LEFT, 4);
                OR16(PLAYER + P_ANIM, 2);
                sub_06007CDE(0x11F);
            }
        }
        if (anim_done() || ((P16(P_FLAGS10) & 0xF) && (P16(P_ANIM) & 3))) {
            set_act(ACT_AIRBORNE);
            p2_or(0x1000);
            PW16(P_1A, 0x0C);
            PW16(P_FLAGS2, P16(P_FLAGS2) ^ PF2_FACE_LEFT);
        }
        break;
    case 0x1B:
        sub_06046782();
        if ((P16(P_ANIM) & 1) == 1) {
            Obj_Spawn(4, P32(P_X), P32(P_Y), P16(P_FLAGS2) & PF2_FACE_LEFT, 0);
            AND16(PLAYER + P_ANIM, 0xFFFC);
        }
        if ((P16(P_ANIM) & 4) != 4) set_velx_facing(0x00038000);
        if (anim_done() || (P16(P_FLAGS4) & 0x20)) set_act(ACT_IDLE);
        break;
    case 0x1C: sub_06046782(); sub_06043A44(); break;
    case 0x20: if (anim_done()) set_act(ACT_GUARD); break;
    case 0x21:
        p2_or(PF2_GUARDING);
        if (!(GUARD_MASK & HELD)) { p2_and(0xFFBF); set_act(ACT_RECOVER); }
        break;
    case 0x22: if (anim_done()) set_act(ACT_RECOVER); break;
    case 0x23: case 0x24:
        sub_06046782();
        if (anim_done() || (P16(P_FLAGS4) & 0x20)) set_act(ACT_RECOVER);
        break;
    case 0x25:
        OR16(PLAYER + P_FLAGS4, 0x2000);
        if (!(P16(P_FLAGS2) & 0x2000)) set_velx_facing(0x00038000);
        if (anim_done()) set_act(ACT_IDLE);
        break;
    case 0x26: case 0x33: case 0x46:
        if (anim_done()) {
            if (Player_Action() == 0x26) set_act(ACT_GUARD);
            if (Player_Action() == 0x33) set_act(ACT_CROUCH_GUARD);
            if (Player_Action() == 0x46) set_act(ACT_AIR_GUARD);
        }
        if (!(P16(P_FLAGS2) & 0x2000)) set_velx_facing((int32_t)0xFFFF0000);
        break;
    case 0x27:
        if (anim_done()) {
            if (!(P16(P_FLAGS2) & PF2_CROUCHED)) { p2_or(PF2_CROUCHED); set_act(ACT_CROUCH); }
            else { p2_and(0xF7FF); set_act(ACT_IDLE); }
        }
        break;
    case 0x2D: if (anim_done()) set_act(ACT_CROUCH_GUARD); break;
    case 0x2E:
        p2_or(PF2_GUARDING);
        if (!(GUARD_MASK & HELD)) set_act(ACT_CROUCH_GUARD_END);
        break;
    case 0x2F: if (anim_done()) set_act(ACT_CROUCH_IDLE2); break;
    case 0x31: case 0x32:
        sub_06046782();
        if (anim_done() || (P16(P_FLAGS4) & 0x20)) set_act(ACT_CROUCH_IDLE2);
        break;
    case 0x35: case 0x37: case 0x38: case 0x39: case 0x3A: Player_AirJumpHold(); break;
    case 0x43: if (anim_done()) set_act(ACT_AIR_GUARD); break;
    case 0x44:
        p2_or(PF2_GUARDING);
        if (!(GUARD_MASK & HELD)) set_act(ACT_AIR_GUARD_END);
        break;
    case 0x45: if (anim_done()) set_act(ACT_AIR_RECOVER); break;
    case 0x4C: set_act(ACT_IDLE); break;
    case 0x4D:
        Player_AirJumpHold();
        if (anim_done()) set_act(ACT_LANDING_Q);
        break;
    case 0x4E:
        sub_06046782();
        if (!(P16(P_ANIM) & 1)) set_velx_facing(0x00038000);
        if (P16(P_ANIM) & 4) {
            Obj_Spawn(4, P32(P_X), P32(P_Y), P16(P_FLAGS2) & PF2_FACE_LEFT, 0);
            AND16(PLAYER + P_ANIM, 0xFFFB);
        }
        if (anim_done() || (P16(P_FLAGS4) & 0x20)) set_act(ACT_RECOVER);
        break;
    case 0x50:
        sub_06046782();
        if (P16(P_ANIM) & 1) {
            PW32(P_VELY, (uint32_t)(P32(P_VELY) + 0x3400));
            PW32(P_VELX, (uint32_t)(P32(P_VELX) + ((P16(P_FLAGS2) & PF2_FACE_LEFT) ? -0x70000 : 0x70000)));
        }
        if (!(P16(P_ANIM) & 1)) W16(G_RUNSPECIAL_V, 0);
        else W16(G_RUNSPECIAL_V, R16(G_RUNSPECIAL_V) - 0xF);
        if (anim_done()) {
            set_act(ACT_IDLE);
            p2_and(0xFF7F);
            W16(G_RUNSPECIAL_V, 0);
        }
        break;
    default:
        if (anim_done()) set_act(ACT_IDLE);
        break;
    }

    if (Player_Action() == ACT_IDLE) {
        if (P16(P_FLAGS2) & PF2_CROUCHED) set_act(ACT_CROUCH);
        if (P16(P_FLAGS2) & PF2_AIRBORNE) sub_0604241E();
        if (P16(P_FLAGS2) & PF2_POSTURE4) set_act(ACT_POSTURE4_IDLE);
    }
    if (Player_Action() == ACT_IDLE &&
        sub_0604588E(P32(P_X), P32(P_Y), 10, RS16(G_PROBE_PARAM)) < 10)
        set_act(ACT_EDGE_IDLE_Q);
}

/* 0x06042910: per-tick housekeeping, then pick the posture selector. */
void Player_SelectPosture(void)
{
    if (R16(G_DIR_TAP) != 0 && Player_Action() == ACT_WALK && R16(G_WALK_CTR) == 0)
        W16(G_DIR_TAP, R16(G_DIR_TAP) + 1);
    p2_and(0xFFBF);
    AND16(PLAYER + P_FLAGS4, 0xDFFF);
    p2_and(0xFF7F);
    p2_and(0xFEFF);
    sub_06042E24();
    sub_0604592E();
    if (!(P16(P_FLAGS10) & 0x10)) p2_and(0xFBFF);
    if (!(P16(P_FLAGS10) & 0x10)) AND16(PLAYER + P_FLAGS10, 0xFEFF);
    if ((R16(G_CHEAT_FLAGS) & 1) && R32(G_CHEAT_KEY) == CHEAT_MAGIC) {   /* cheat: invincible */
        p2_and(0xFFF7);
        PW16(P_HP, R16(G_HP_MAX));
    }
    if (!(P16(P_FLAGS10) & 0x0400)) {
        W16(R32(G_HAZARD_PTR) + 2, 0);
    } else {                                                /* standing in a hazard */
        int16_t lim = 1;
        int32_t x9 = P32(P_X) + (int32_t)0xFC000000, y8 = (int32_t)0xFC000000 + P32(P_Y);
        if (R16(G_STAGE) == 0x10 ||
            (R16(G_STAGE) == 6 && 0x0A600000 < x9 && x9 < 0x0D800000 && 0x01A00000 < y8 && y8 < 0x02F00000))
            lim = 0;
        int16_t c = (int16_t)(RS16(R32(G_HAZARD_PTR) + 2) + 1);
        W16(R32(G_HAZARD_PTR) + 2, (uint16_t)c);
        if (lim < c && (!(R16(G_CHEAT_FLAGS) & 1) || R32(G_CHEAT_KEY) != CHEAT_MAGIC)) {
            PW16(P_HP, 0);
            p2_or(PF2_HIT);
        }
    }
    AND16(PLAYER + P_FLAGS10, 0xFBFF);
    if (!(P16(P_FLAGS2) & PF2_CROUCHED) && (P16(P_FLAGS10) & 0x8000)) {   /* forced crouch */
        p2_or(PF2_CROUCHED);
        set_act(ACT_CROUCH);
    }
    sub_06042B90();
    if (!(P16(P_FLAGS2) & PF2_POSTURE4)) {
        int a = Player_Action();
        if (!(P16(P_FLAGS2) & PF2_AIRBORNE) || a == ACT_RUN_SLASH || a == ACT_DEAD) {
            if (!(P16(P_FLAGS2) & PF2_CROUCHED)) { p2_and(0xF7FF); Player_SelectAction_Stand(); }
            else Player_SelectAction_Crouch();
        } else {
            p2_and(0xF7FF);
            Player_SelectAction_Air();
        }
    } else {
        p2_and(0xF7FF);
        sub_060424A6();
    }
    AND16(PLAYER + P_FLAGS4, 0xEFFF);
    AND16(PLAYER + P_FLAGS4, 0xF7FF);
}

/* 0x06040B72: player tick. */
void Player_Tick(void)
{
    int frozen;
    PW32(P_VELX, 0);
    p2_and(0xFFDF);
    frozen = (R16(G_SYSFLAGS) & 0x8000) != 0;
    int a = Player_Action();
    if (!frozen || a == 0x15 || a == 0x2C || a == 0x4A || a == 0x3E) {
        Player_SelectPosture();
        Player_OnActionChange();
        Player_UpdateAction();
        Player_OnActionChange();
        if ((HELD & 0x90) == 0x90 && R32(G_CHEAT_KEY) == CHEAT_MAGIC) sub_06040F80();  /* RT+Z debug move */
        else Player_GroundVelocity();
        Player_Physics();
    }
    sub_06043FDA();
    sub_06044280();
    AND16(PLAYER + P_FLAGS4, 0xFFDF);
    if (!(R16(G_SYSFLAGS) & 0x8000)) AND16(PLAYER + P_FLAGS4, 0xFEFF);
    if (!(R16(G_SYSFLAGS) & 0x8000)) AND16(PLAYER + P_FLAGS10, 0xC77F);
}

/* PC-side init (the original sets these up in stage init code not yet traced). */
void Player_Init(void)
{
    for (uint32_t i = 0; i < 0x60; i += 2) PW16(i, 0);
    W32(G_HAZARD_PTR, 0x060FFF00u);   /* scratch area for the hazard/invuln counters */
    PW16(P_HP, 0x10);
    W16(G_HP_MAX, 0x10);
    W16(G_SHURIKENS, 20);
    if (R32(0x0601440Cu) == 0) W32(0x0601440Cu, 0x00024000);   /* walk speed if A.BIN not loaded */
    PW32(P_X, 0x00A00000);                                      /* PC test spawn point */
    PW32(P_Y, 0x00C00000);
}
