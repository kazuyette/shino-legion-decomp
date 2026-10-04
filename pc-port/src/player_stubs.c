/* Player sub-routines of SHINOBI.dec.bin called by player.c (declared in player_ext.h), plus
 * the A.BIN runtime / sound entry points they use. Formerly logging stubs, now ported
 * (the ninjutsu itself is in ninjutsu.c). Names = original address. */
#include "audio.h"
#include "sfx.h"
#include "resource.h"
#include "player_ext.h"
#include "player.h"
#include "pad.h"
#include "sprite.h"
#include "collision.h"
#include "enemy.h"
#include "ninjutsu.h"
#include "ram.h"
#include <stdio.h>

#define ONCE(name) do { static int s_; if (!s_) { s_ = 1; printf("[stub] %s not ported\n", name); } } while (0)

#define G_SYSFLAGS  0x0602529Eu
#define G_FRAME     0x060143EAu
#define G_SHURIKENS 0x060252B6u
#define G_MAGIC     0x060252BAu
#define G_MAGIC_AV  0x060252BEu
#define G_IDLE_CTR  0x060EA430u
#define G_CMD_BUF   0x060EA432u
#define G_JUMP_T0   0x060EA438u
#define G_FIDGET    0x060EA444u
#define G_HAZARD    0x060EA41Cu
#define HELD        R16(ADDR_GAME_HELD)
#define PRESSED     R16(ADDR_GAME_PRESSED)
#define PPREV       R16(ADDR_GAME_PRESSED_PREV)
#define BTN0        R16(ADDR_ACTION_BTN0)
#define BTN1        R16(ADDR_ACTION_BTN1)
#define BTN2        R16(ADDR_ACTION_BTN2)

/* Snd_Play 0x06007CDE -> sfx.c (SCSP .SND sequences) through audio.c's SFX hook */
void     sub_06007CDE(int sfx)  { Res_EnsureSoundBank(); Audio_PlaySfx((int16_t)sfx); }
void     sub_06007E00(void)     { Audio_StopSfx(); }   /* Snd_StopAll */

/* A.BIN 0x0600A604 (__modsi3, SHC runtime): r1 % r0, signed; % 0 returns 0 (error word set) */
int32_t  sub_0600A604(int32_t a, int32_t b) { return b ? a % b : 0; }
/* A.BIN 0x0600CEF4 (__umodsi3): r1 % r0, unsigned; % 0 returns 0 */
uint32_t sub_0600CEF4(uint32_t a, uint32_t b) { return b ? a % b : 0; }

static void set_act(int a) { PW16(P_ACTION, (uint16_t)a); }

/* 0x06040F80: debug free move (cheat key + RT+Z): the pad moves Sho 2 px/frame, no gravity */
void sub_06040F80(void)
{
    PW32(P_VELY, 0);
    if (HELD & 0x4000) PW32(P_VELX, 0xFFFE0000u);
    if (HELD & 0x8000) PW32(P_VELX, 0x00020000u);
    if (HELD & 0x1000) PW32(P_VELY, 0x00020000u);
    if (HELD & 0x2000) PW32(P_VELY, 0xFFFE0000u);
    PW16(P_FLAGS2, P16(P_FLAGS2) | 0x0100);
}

/* 0x060410F8: on hit: end the hit-react / air flags, cry (0x11D hurt, 0x11E killed) */
void sub_060410F8(void)
{
    PW16(P_FLAGS4, P16(P_FLAGS4) & 0xBFFF);
    PW16(P_FLAGS2, P16(P_FLAGS2) & 0xEFFF);
    sub_06007CDE(PS16(P_HP) > 0 ? 0x11D : 0x11E);
}

/* 0x060424A6: action selector of posture 4 (hanging from a ceiling, actions 0x40-0x4C) */
int sub_060424A6(void)
{
    int a = Player_Action();
    int idle = (a == 0x40 || a == 0x41 || a == 0x42 || a == 0x4C);
    if (idle) { if (R16(G_IDLE_CTR) < 0x7FFF) W16(G_IDLE_CTR, R16(G_IDLE_CTR) + 1); }
    else W16(G_IDLE_CTR, 0);

    if (P16(P_FLAGS2) & PF2_HIT) {                               /* hit while hanging */
        if (Player_Action() != 0x4B) { sub_060410F8(); set_act(0x4B); }
        if (PS16(P_HP) > 0) return Player_Action();
        sub_060410F8();
        set_act(ACT_AIR_DEAD);
        PW16(P_FLAGS4, P16(P_FLAGS4) & 0xFEFF);
        PW16(P_FLAGS2, P16(P_FLAGS2) & 0xFBFF);
        W16(0x060EA62Au, 0x7FFF);
        return Player_Action();
    }
    if (P16(P_FLAGS4) & 0x4000) {                                /* knocked off */
        set_act(0x3A);
        PW16(P_FLAGS2, P16(P_FLAGS2) & 0xFBFF);
        return Player_Action();
    }
    if (Player_Action() == 0x4A) return 0x4A;
    if ((R16(G_MAGIC) & 0x2000) || (R16(G_MAGIC_AV) != 0 && (PRESSED & PAD_X))) {   /* ninjutsu */
        set_act(0x4A);
        sub_06007CDE(0x116);
        OR16(G_SYSFLAGS, 0x8000);
        if (!(R16(G_MAGIC) & 0x2000)) W16(G_MAGIC, (R16(G_MAGIC) & 0xFFF0) | 0x2002);
        return 0x4A;
    }
    if (Player_Action() == 0x44 && (P16(P_FLAGS4) & 0x1000)) { set_act(0x46); return 0x46; }
    if (Player_Action() == 0x46) return 0x46;
    if (!(P16(P_FLAGS4) & 0x0100)) {
        if ((PRESSED & BTN0) && R16(G_SHURIKENS) != 0) { set_act(0x19); return 0x19; }
        a = Player_Action();
        if (((HELD & 0x30) && (idle || a == 0x47 || a == 0x11)) || (PRESSED & 0x30)) { set_act(0x43); return 0x43; }
        uint16_t pp = PRESSED | PPREV;
        if ((pp & 0xC000) && (BTN1 & pp)) { set_act(0x48); return 0x48; }
        if (PRESSED & BTN1) {
            set_act(0x18);
            if ((HELD & 0x2000) || ((HELD & 0xC000) && (HELD & 0x1000))) set_act(0x49);
            return Player_Action();
        }
        a = Player_Action();
        if ((HELD & BTN1) && (idle || a == 0x47 || a == 0x11)) { set_act(0x43); return 0x43; }
    }
    if ((HELD & 0x2000) && (PRESSED & BTN2)) {                   /* down + jump: let go */
        PW16(P_FLAGS2, P16(P_FLAGS2) & 0xFBFF);
        set_act(0x3A);
        PW16(P_FLAGS4, P16(P_FLAGS4) & 0xFEFF);
        sub_06007CDE(0x102);
        return 0x3A;
    }
    if (((HELD & 0x1000) || (P16(P_FLAGS4) & 0x0100)) && (PRESSED & BTN2) &&
        !(P16(P_FLAGS10) & 1) && P32(0x44) >= 0) {               /* up + jump: climb out */
        PW16(P_FLAGS2, P16(P_FLAGS2) & 0xFBFF);
        set_act(ACT_AIRBORNE);
        PW16(P_FLAGS2, P16(P_FLAGS2) | 0x1000);
        PW16(P_1A, 0);
        W16(G_JUMP_T0, 0);
        PW16(P_FLAGS10, P16(P_FLAGS10) | 0x0100);
        PW32(P_VELY, 0x00060000);
        sub_06007CDE(0x102);
        return ACT_AIRBORNE;
    }
    if ((HELD & 0xC000) && (idle || Player_Action() == 0x47) && !(P16(P_FLAGS4) & 0x0100)) {
        set_act(0x11);                                           /* hand-over-hand */
        return 0x11;
    }
    if (!idle) return Player_Action();
    if (Player_Action() == 0x40 && R16(G_IDLE_CTR) > 30) set_act(0x41);
    int32_t v = sub_0600A604((int32_t)R16(G_IDLE_CTR), RS16(G_FIDGET));
    if (v != 0) return v;
    set_act(0x42);
    int16_t t = 0xB4;
    if (RS16(G_FIDGET) > 0x40)
        t = (int16_t)(sub_0600CEF4((uint32_t)sub_0602DDD2() >> 8, 5) * 0x60 + 0x20);
    W16(G_FIDGET, (uint16_t)t);
    return 0x42;
}

/* 0x06042B90: command input buffer 0x060EA432 (only in idle/walk actions): up (0x100),
 * up+side (0x200), side (0x300 = the command move ACT_COMMAND_Q); low byte = frames */
void sub_06042B90(void)
{
    int a = Player_Action();
    if (!(a == 0 || a == 0x1D || a == 0x1E || a == 1 || a == 0x4F || a == 0x1F)) { W16(G_CMD_BUF, 0); return; }
    uint16_t b = R16(G_CMD_BUF);
    if ((int)(b & 0xFF) >= 16) { W16(G_CMD_BUF, 0); return; }
    switch (b & 0x0F00) {
    case 0x000:
        if ((HELD & 0xF000) == 0x1000) W16(G_CMD_BUF, 0x100);
        break;
    case 0x100:
        W16(G_CMD_BUF, b + 1);
        if ((HELD & 0xC000) && (HELD & 0x1000)) W16(G_CMD_BUF, 0x200);
        break;
    case 0x200:
        W16(G_CMD_BUF, b + 1);
        if ((HELD & 0xF000) == 0x4000 || (HELD & 0xF000) == 0x8000) W16(G_CMD_BUF, 0x300);
        break;
    case 0x300:
        W16(G_CMD_BUF, b + 1);
        break;
    }
}

/* 0x06042C88: 8 shurikens at once (fan from table 0x060A8A2C), costs 8 */
void sub_06042C88(void)
{
    int base = 0;
    if (P16(P_FLAGS2) & 0x0080) {                                /* somersault: aim down */
        base = 0xC0;
        if (P16(P_FLAGS2) & 0x8000) base = 0x80;
        if (HELD & 0x2000) base = 0xA0;
    }
    if (Player_Action() == 0x25) {
        base = (P16(P_FLAGS2) & 0x8000) ? 0x40 : 0;
        if (HELD & 0x1000) base = 0x20;
    }
    for (int i = 0; i < 8; i++)
        sub_06042D80(0, (int32_t)0xFFE80000, (int16_t)((R16(0x060A8A2Cu + 2u * (uint32_t)i) + base) & 0xFF), 0x00064000);
    sub_06007CDE(0x101);
    W16(G_SHURIKENS, R16(G_SHURIKENS) - 8);
    if (RS16(G_SHURIKENS) < 0) W16(G_SHURIKENS, 0);
}

/* 0x06042E24: standing in damaging terrain (+0x10 & 0x20) costs 1 HP when not invulnerable */
void sub_06042E24(void)
{
    if (Player_Action() == ACT_AIR_16_Q && P16(P_5A) > 6) return;
    if ((P16(P_FLAGS10) & 0x20) && !(P16(P_FLAGS2) & PF2_HIT) && R16(R32(G_HAZARD)) == 0)
        Player_TakeDamage(1);                                    /* 0x06045BEC */
}

void sub_060437C0(void) { Player_NinjutsuExec(); }

/* 0x06043A44: action 0x1C (third combo slash): step forward, then recover */
void sub_06043A44(void)
{
    if (Player_Action() == 0x1C && !(P16(P_ANIM) & 1)) {
        PW32(P_VELX, 0x00030000);
        if (P16(P_FLAGS2) & 0x8000) PW32(P_VELX, (uint32_t)-P32(P_VELX));
    }
    if ((P16(P_ANIM) & PANIM_FINISHED) || (P16(P_FLAGS4) & 0x20)) set_act(ACT_RECOVER);
}

/* 0x06044280: 6 afterimages from the position history ring 0x060EA446 (64 x {s16 x, s16 y},
 * index 0x060EA546) while the shield is armed or during the run special; every other frame */
void sub_06044280(void)
{
    if (!((R16(G_MAGIC) & 0x40F0) == 0x4000 || Player_Action() == ACT_RUN_SPECIAL)) return;
    if (R16(G_FRAME) & 1) return;
    int16_t yoff = 0;
    if ((P16(P_FLAGS2) & 0x80) || Player_Action() == ACT_RUN_SPECIAL) yoff = -32;
    int idx = (R16(0x060EA546u) - 36) & 63;
    for (int k = 0; k < 6; k++) {
        uint32_t e = 0x060EA446u + (uint32_t)idx * 4u;
        W16(SPR_WORK + 2, (uint32_t)(int16_t)((uint32_t)(((uint32_t)R16(e) << 16) - R32(0x060252A0u)) >> 16));
        W16(SPR_WORK + 4, (uint32_t)(int16_t)((int16_t)((uint32_t)(((uint32_t)R16(e + 2) << 16) - R32(0x060252A4u)) >> 16) + yoff));
        if ((P16(P_FLAGS4) & 0x100) && R32(0x060EA440u) != 0)
            W16(SPR_WORK + 4, (uint32_t)(RS16(SPR_WORK + 4) - PS16(0x20) * 8));
        int32_t f = (k + 1) * 10;
        W16(SPR_WORK + 0xE, (uint32_t)(3 - ((f / 6) * 3) / 10));
        W32(SPR_WORK + 0x18, 0);
        Sprite_Draw(SPR_WORK);
        idx = (idx + 6) & 63;
    }
}

/* 0x060459A0: does Sho's sword box (hitbox kind 2) touch a wall? Probes every 8 px along the
 * box's width at its bottom; *pt = the probe point. */
static int sword_hits_wall(int16_t layer, int32_t pt[2])
{
    if (layer < 0) return 0;
    uint32_t hb = R32(PLAYER + 0x68);
    if (hb == 0 || R16(hb) == 0xFFFF) return 0;
    int16_t x1 = 0x7FFF, y1 = 0x7FFF, x2 = -0x8000, y2 = -0x8000;
    int n = 0;
    for (int i = 0; i < 11 && R16(hb) != 0xFFFF; i++, hb += 10) {
        if (R16(hb) != 2) continue;
        n++;
        if (x1 >= RS16(hb + 2)) x1 = RS16(hb + 2);
        if (y1 >= RS16(hb + 4)) y1 = RS16(hb + 4);
        if (!(x2 > RS16(hb + 6))) x2 = RS16(hb + 6);
        if (!(y2 > RS16(hb + 8))) y2 = RS16(hb + 8);
    }
    (void)y1;
    if (n == 0) return 0;
    pt[1] = (int32_t)((uint32_t)(int32_t)y2 << 16) + P32(P_Y);
    int32_t w = x2 - x1;
    if (w < 0) w = x1 - x2;
    int16_t steps = (int16_t)((w + 4) / 8);
    if (steps == 0) return 0;
    if (P16(P_FLAGS2) & 0x8000) pt[0] = P32(P_X) - (int32_t)((uint32_t)(int32_t)x2 << 16);
    else pt[0] = (int32_t)((uint32_t)(int32_t)x1 << 16) + P32(P_X);
    uint32_t desc = LAYER_DESC + (((uint32_t)(uint8_t)layer * 48u) & 0xFFu);
    for (; steps > 0; steps--) {
        if (Coll_AttrFlags(Map_GetAttr(pt[0], pt[1], desc)) & 0xC) return 1;
        pt[0] += 0x80000;
    }
    return 0;
}

/* 0x0604592E: sword against a wall: spark (effect 5) + clink 0x105, once per swing (+4 & 0x20) */
void sub_0604592E(void)
{
    if (P16(P_FLAGS4) & 0x20) return;
    int32_t pt[2] = { 0, 0 };
    if (sword_hits_wall(RS16(G_COLL_LAYER), pt)) {
        PW16(P_FLAGS4, P16(P_FLAGS4) | 0x20);
        Obj_Spawn(5, pt[0] + 0x40000, pt[1] + 0x40000, P16(P_FLAGS2) & 0x8000, 0);
        sub_06007CDE(0x105);
    }
}

void sub_06046782(void) { PlayerFx_OnAttack(); }

/* 0x06007D60: Snd_Play(sfx) only while fewer than `bank` (max count) of the 8 sequence
 * slots (0x06025138) already play this id; else returns -1 */
void     sub_06007D60(int sfx, int bank) { if (Sfx_CountLocked(sfx) < bank) sub_06007CDE(sfx); }

/* Sound calls of DIRECTOR (SCSP / CD-DA, work packages D8/D9), declared in director_int.h. */
void Snd_SetAreaMap(uint32_t map)  { (void)map; ONCE("06007E96 Snd_SetAreaMap"); }
void Snd_SetCdDaLevel(int l, int r) { Audio_SetMusicLevels(l, r); }   /* 0x0603125C */
void Snd_SetCdDaPan(int l, int r)   { (void)l; (void)r; ONCE("060312F4 Snd_SetCdDaPan"); }
void Snd_SetMasterVolume(int v)     { (void)v; ONCE("06030710 Snd_SetMasterVolume"); }
