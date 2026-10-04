/* Enemy type 0x0006: stage 7-2 boss (index 13, file STAGE7_2), Koryu.
 * Loader 0x0606A150 (0x3F chars of 0x060D9A9C, 256-colour palette 0x060DA154 + 16-colour
 * 0x060DA354 at bank +0x10, Sho's input locked), handler 0x0606A264, init 0x0606B282
 * (opens KORYU1.PBC), end 0x060698F8 (fade to white, stage state |= 10). Graphics
 * swaps: 0x0606B364 (intro: 0x78 chars of 0x060D9C9A read from KORYU1.PBC at char 0x76,
 * opens KORYU2.PBC), 0x0606B470 (death: 0x1F chars of 0x060DA05E from KORYU2.PBC).
 * HP 0x060B0F14[difficulty] (30/35/40), boss bar 0x06061B58.
 * Actions (+6): 8 intro (spits projectile 3 rocks 0x06069EF8, waits for them), 0 stand,
 * 1/2 walk (bit 2 of flags4 "approach"), 3 dash with afterimages (sound 0x200 every 16
 * frames), 4 leap at Sho (+0x50 = (|dx| / 2 + 40) / 4 - 8 rising frames; < 6 -> 5),
 * 5 short hop, 6 roar (palette darkened by +0x56), 7 / 0x10 fire breath (alternating via
 * 0x060F4D00, projectile 2 0x06069BF8 at the anim event), 9 hurt (knocked back 0.5 px),
 * 10 dying (projectile 2 burst 0x06069B30), 0xE dying when cut down by the sword
 * (attack kind 0..3, 0x060F4D24): overlay anim 0xD at 0x060F4D04 (0x06069AA4), palette
 * +0x10 reddened. Decision (+0x14 every 8 frames, +0x48 next / +0x52 previous / +0x54
 * close-range cycle 0->3 dash, 1->7/0x10, 2->5, 3->7/0x10, 4->4): < 96 px close cycle, < 192
 * px walk (1 after 2), else 4. +0x4A = moves left before standing again.
 * Globals 0x060DA874..0x060DA880 (u32, written only: 1/0, 0x10000, 0x10000, 0) kept.
 * Projectiles 2 (fire 0x06069CAC: anim 0xC, or 0xF on breath 0x10; pulsing scale, hurts 2)
 * and 3 (rock 0x06069FA8: anim 0xB, falls until the floor 0x06046FE2, then steps 8..). */
#include "enemy_more.h"
#include "anim.h"
#include "sprite.h"
#include "objects.h"
#include "collision.h"
#include "player_ext.h"
#include "resource.h"
#include "fade.h"
#include "hud.h"
#include "fx.h"

#define B06_DEFS 0x060AF284u
#define B06_SCR  0x060DA830u
#define G_FLAG   0x060DA874u          /* u32 x4, written only */
#define K_ALT    0x060F4D00u          /* u16 breath 7 / 0x10 alternation */
#define K_OVL    0x060F4D04u          /* death overlay anim state */
#define K_KIND   0x060F4D24u          /* s16 Sho's attack kind of the last hit (-1) */
#define PROJ(i)  (PROJECTILES + (uint32_t)((int16_t)(i) * 0x40))

static int32_t dxabs(uint32_t o) { return EM_Abs(RS32(o + 8) - RS32(PL + 8)); }
static void g_set(uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    W32(G_FLAG, a); W32(G_FLAG + 4, b); W32(G_FLAG + 8, c); W32(G_FLAG + 0xC, d);
}
static int pal_dst(int16_t s) { return (R16(0x060A9084u + 2u * (uint32_t)s) & 0xFFF) << 4; }

/* chars `n` of the table at `tbl` (8-byte entries {?, size, mode, w/8, h}) from char `base` */
static uint32_t reg_chars(int base, uint32_t tbl, int n, uint32_t off)
{
    for (int i = 0; i < n; i++) {
        uint32_t r = tbl + (uint32_t)i * 8;
        Char_Register((base + i) & 0xFFFF, Ram_Ptr(r + 6)[0] * 8, Ram_Ptr(r + 7)[0], RS16(r + 4), off);
        off += (uint32_t)R16(r + 2) * 32;
    }
    return off;
}

void Enemy6_Load(void)                                      /* 0x0606A150 */
{
    int16_t s = (int16_t)Enemy_FindSlot(6);
    reg_chars(CHAR_BASE(s), 0x060D9A9Cu, 0x3F, R32(0x060A9070u + 4u * (uint32_t)s));
    Pal_Copy(pal_dst(s), 0x100, 0x060DA154u);
    Pal_Copy((R16(0x060A9084u + 2u * (uint32_t)s) + 0x10) * 0x10, 0x10, 0x060DA354u);
    OR16(PL + 4, 0x400);
}

static void Koryu_Init(void)                                /* 0x0606B282 */
{
    uint32_t o = O;
    W16(o + 6, 8);
    Anim_Start(o + 0x58, B06_DEFS, B06_SCR, 0x60, 8);
    AND16(o + 2, 0xFFFE);
    W16(o + 0x16, R16(0x060B0F14u + 2u * (uint32_t)RS16(0x060252C4u)));
    W16(o + 0x48, 8); W16(o + 0x4A, 0); W16(o + 0x4C, 0); W16(o + 0x4E, 0);
    W16(o + 0x50, 0xFFFF); W16(o + 0x52, 8); W16(o + 0x54, 0); W16(o + 0x56, 0);
    W16(K_KIND, 0xFFFF);
    EM_TrailReset();
    Res_Request(2, 0, 0, 0xFFFFFFFFu, 0, 0x060B0F1Cu);      /* "KORYU1.PBC" */
    W16(K_ALT, 0);
}

static void Koryu_End(void)                                 /* 0x060698F8 */
{
    Fade_SetTarget(0x100, 0x100, 0x100);
    Fade_SetEndLayerMask(0);
    Fade_SetNextSpeed(0x20000, 1);
    Fade_StartOut();
    W16(0x0602529Eu, (R16(0x0602529Eu) & 0xFFF8) | 10);
    W16(O, 0xFFFF);
}

static void Koryu_DeathOverlay(void)                        /* 0x06069AA4 */
{
    uint32_t o = O;
    int16_t s = (int16_t)Enemy_FindSlot(6);
    W16(SPR + 2, (uint32_t)((RS32(o + 8) - RS32(CAM_X)) >> 16));
    W16(SPR + 4, (uint32_t)((RS32(o + 0xC) - RS32(CAM_Y)) >> 16));
    W16(SPR + 0x0A, (uint16_t)COLOR_BANK(s));
    W16(SPR + 0x10, (R16(o + 2) & 0x8000) | 0x200);
    W32(SPR + 0x18, 1);
    W16(SPR + 0x0C, 0);
    W16(SPR + 0x0E, 0);
    Anim_Tick(K_OVL);
}

static void proj_face(int s)
{
    uint32_t p = PROJ(s);
    if (!(R16(O + 2) & 0x8000)) AND16(p + 4, 0x7FFF); else OR16(p + 4, 0x8000);
}

static void Koryu_Burst(void)                               /* 0x06069B30 */
{
    uint32_t o = O;
    int32_t d = (R16(o + 2) & 0x8000) ? -0x100000 : 0x100000;
    int s = (int16_t)Projectile_Spawn(2, RS32(o + 8) - d, RS32(o + 0xC) - 0x440000, 0, 0);
    W32(G_FLAG, 0);
    if (s != -1) { OR16(PROJ(s) + 0x10, 1); proj_face(s); }
}

static void Koryu_Breath(void)                              /* 0x06069BF8 */
{
    uint32_t o = O;
    int32_t d = (R16(o + 2) & 0x8000) ? 0x260000 : -0x260000;
    int s = (int16_t)Projectile_Spawn(2, RS32(o + 8) - d, RS32(o + 0xC) - 0x260000, 0, 0);
    W32(G_FLAG, 0);
    if (s != -1) proj_face(s);
}

static void Koryu_Rock(void)                                /* 0x06069EF8 */
{
    uint32_t o = O;
    int32_t d = (R16(o + 2) & 0x8000) ? 0x1A0000 : -0x1A0000;
    int s = (int16_t)Projectile_Spawn(3, RS32(o + 8) - d, RS32(o + 0xC) - 0x4C0000, 0, 0);
    if (s != -1) proj_face(s);
}

static void Koryu_IntroGfx(void)                            /* 0x0606B364 */
{
    int16_t s = (int16_t)Enemy_FindSlot(6);
    uint32_t off = R32(0x060A9070u + 4u * (uint32_t)s);
    for (int i = 0; i < 0x16; i++) off += (uint32_t)R16(0x060D9A9Eu + (uint32_t)i * 8) * 32;
    Res_Request(1, 1, 0, 0xFFFFFFFFu, off + 0x25C04740u, 0x060B0F1Cu);   /* KORYU1.PBC */
    Res_Request(2, 0, 0, 0xFFFFFFFFu, 0, 0x060B0F28u);                   /* KORYU2.PBC */
    reg_chars(0x76, 0x060D9C98u, 0x78, off);
}

static void Koryu_DeathGfx(void)                            /* 0x0606B470 */
{
    uint32_t off = 0xC000;
    for (int i = 0; i < 0x16; i++) off += (uint32_t)R16(0x060D9A9Eu + (uint32_t)i * 8) * 32;
    Res_Request(1, 1, 0, 0xFFFFFFFFu, off + 0x25C04740u, 0x060B0F28u);   /* KORYU2.PBC */
    reg_chars(0x76, 0x060DA05Cu, 0x1F, off);
}

static void set_next(uint32_t o, uint16_t n) { W16(o + 0x48, n); W16(o + 0x52, n); }

static void pick_breath(uint32_t o)
{
    W16(K_ALT, ~R16(K_ALT) & 1);
    W16(o + 0x48, R16(K_ALT) == 0 ? 7 : 0x10);
}

static void Koryu_Decide(uint32_t o)                        /* inline in 0x0606A264 */
{
    if (R16(o + 0x14) == 0) {
        W16(o + 0x14, 8);
        if (RS16(o + 0x48) == 8) W16(o + 0x48, 8);
        else if (dxabs(o) < 0x600000) {
            switch (RS16(o + 0x54)) {
            case 0: W16(o + 0x48, 3); W16(o + 0x52, 3); W16(o + 0x54, 1); break;
            case 1: pick_breath(o); W16(o + 0x52, R16(o + 0x48)); W16(o + 0x54, 2);
                    g_set(0, 0x10000, 0x10000, 0); break;
            case 2: W16(o + 0x48, 5); W16(o + 0x52, 5); W16(o + 0x54, 3); break;
            case 3: pick_breath(o); W16(o + 0x52, R16(o + 0x48)); W16(o + 0x54, 4);
                    g_set(0, 0x10000, 0x10000, 0); break;
            case 4: W16(o + 0x48, 4); W16(o + 0x52, 4); W16(o + 0x54, 0); break;
            }
        } else if (dxabs(o) < 0xC00000) {
            W16(o + 0x14, 1);
            set_next(o, RS16(o + 0x52) == 2 ? 1 : 2);
        } else { W16(o + 0x14, 1); set_next(o, 4); }
    }
    if (R16(o + 0x14) != 0) W16(o + 0x14, R16(o + 0x14) - 1);
    if (R16(o + 0x14) != 0) return;
    switch (RS16(o + 0x48)) {
    case 1: W16(o + 6, 1); W16(o + 0x4A, 2); break;
    case 2: W16(o + 6, 2); W16(o + 0x4A, 0); break;
    case 3: W16(o + 6, 3); W16(o + 0x4A, 5); break;
    case 4: {
        W16(o + 6, 4);
        int16_t d = (int16_t)(uint16_t)((uint32_t)dxabs(o) >> 16);
        int32_t h = d + 80;
        h = (h + (h < 0)) >> 1;
        W16(o + 0x50, (uint16_t)(int16_t)(EM_Sdiv(h, 4) - 8));
        if (RS16(o + 0x50) < 6) { W16(o + 6, 5); W16(o + 0x4A, 2); }
        else W16(o + 0x4A, 1);
        break;
    }
    case 5: W16(o + 6, 5); W16(o + 0x4A, 2); break;
    case 6: W16(o + 6, 6); W16(o + 0x4A, 1); break;
    case 7: case 0x10:
        W16(o + 6, R16(o + 0x48)); W16(o + 0x4A, 0); W16(o + 0x4C, 0); W16(o + 0x56, 0); break;
    case 8: W16(o + 6, 8); W16(o + 0x4A, 0); break;
    }
}

/* the anim of the current action ended */
static int Koryu_AnimEnd(uint32_t o, int16_t s)
{
    W32(o + 0x30, 0);
    W16(o + 0x56, 0);
    if (RS16(o + 6) == 8) AND16(PL + 4, 0xFBFF);
    if (RS16(o + 6) == 3 && dxabs(o) < 0x200000) W16(o + 6, 0);
    if (RS16(o + 6) == 10 || RS16(o + 6) == 0xE) { Koryu_End(); return 1; }
    int16_t a = RS16(o + 6);
    if (a != 0 && a != 10 && a != 0xE && RS16(o + 0x4A) > 0) W16(o + 0x4A, R16(o + 0x4A) - 1);
    W16(o + 0x4C, 0);
    if (RS16(o + 6) == 9) {
        if (RS16(o + 0x16) < 1) {                                   /* dead */
            Koryu_DeathGfx();
            sub_06007CDE(0x222);
            int16_t k = RS16(K_KIND);
            if (k == 0 || k == 1 || k == 2 || k == 3) {
                W16(o + 0x56, 0);
                W16(o + 6, 0xE);
                Pal_SetFaded((R16(0x060A9084u + 2u * (uint32_t)s) + 0x10) * 0x10, 0x10, 0x060DA354u, 0x80, -0x30, -0x30);
                Anim_Start(K_OVL, B06_DEFS, B06_SCR, 0x60, 0xD);
            } else W16(o + 6, 10);
            W16(o + 0x48, R16(o + 6));
            W16(o + 0x4A, 1);
            W16(o + 0x52, R16(o + 6));
            OR16(PL + 4, 0x400);
        } else {
            AND16(o + 2, 0xFFF7);
            int16_t n = RS16(o + 0x48);
            if (n == 0) { W16(o + 6, 6); W16(o + 0x4A, 1); set_next(o, 6); }
            else if (n == 1 || n == 4) {
                pick_breath(o);
                W16(o + 6, R16(o + 0x48)); W16(o + 0x4A, 1); W16(o + 0x52, R16(o + 6));
                W16(o + 0x4C, 0); W16(o + 0x56, 0);
                g_set(0, 0x10000, 0x10000, 0);
            } else if (n == 6) W16(o + 0x4A, 0);
            else if (n == 7 || n == 0x10) { W16(o + 6, 3); W16(o + 0x4A, 2); set_next(o, 3); }
            else if (RS16(o + 0x16) < 1) { Koryu_DeathGfx(); W16(o + 6, 10); W16(o + 0x4A, 1); set_next(o, 10); }
            else { W16(o + 6, 6); W16(o + 0x4A, 1); set_next(o, 6); }
        }
    } else {
        AND16(o + 2, 0xFFF7);
        if (RS16(o + 0x48) == 2) {
            if (dxabs(o) < 0x600000) W16(o + 0x4A, 0);
            else if (dxabs(o) < 0xC80000) {
                if (RS16(o + 0x52) == 2) { W16(o + 6, 1); set_next(o, 1); W16(o + 0x4A, 2); }
                else { W16(o + 6, 2); set_next(o, 2); W16(o + 0x4A, 1); }
            } else { W16(o + 6, 2); W16(o + 0x14, 1); set_next(o, 2); W16(o + 0x4A, 1); }
        }
    }
    if (RS16(o + 6) == 5 && dxabs(o) > 0xC00000) {
        W16(o + 6, 2); W16(o + 0x14, 1); set_next(o, 2); W16(o + 0x4A, 1);
    }
    if (R16(o + 0x4A) == 0) {
        g_set(1, 0x10000, 0x10000, 0);
        W16(o + 6, 0); W16(o + 0x48, 0); W16(o + 0x52, 0);
        AND16(o + 0x58, 0x7FFF); W16(o + 0x5A, 0);
    } else {
        AND16(o + 0x58, 0x7FFF); W16(o + 0x5A, 0);
        EM_FaceSho(o);
    }
    return 0;
}

void EnemyHandler_0006(void)                                /* 0x0606A264 */
{
    uint32_t o = O;
    if (R16(o + 2) & 1) Koryu_Init();
    Hud_DrawBossBar(RS16(0x060B0F14u + 2u * (uint32_t)RS16(0x060252C4u)), RS16(o + 0x16));
    if (FROZEN) goto draw;
    int16_t s = (int16_t)Enemy_FindSlot(6);
    int16_t dmg = (RS16(o + 6) == 4) ? 2 : 1;
    if (!(R16(PL + 2) & 8) && Enemy_AttackPlayer(dmg, 0, 0xC000) == 3) sub_06007CDE(0x10C);
    if (!(R16(o + 2) & 8)) {
        W16(K_KIND, 0xFFFF);
        if (Enemy_CheckPlayerHits(0xC000) == 2) W16(K_KIND, (uint16_t)Player_AttackKind());
    }
    {
        int16_t a = RS16(o + 6);
        if (a == 0 || a == 2 || a == 8) EM_FaceSho(o);
        a = RS16(o + 6);
        if (a != 10 && a != 0xE && a != 9 && (R16(o + 2) & 8)) {          /* hurt */
            W32(G_FLAG, 1); W32(G_FLAG + 0xC, 0);
            Pal_Copy(pal_dst(s), 0x100, 0x060DA154u);
            W16(o + 6, 9);
            if (RS16(o + 0x16) > 0) sub_06007CDE(0x206);
        }
        a = RS16(o + 6);
        if (a != 9 && a != 10 && !(R16(o + 2) & 0x4000) && a != 0xE && a == 0 && R16(o + 0x4A) == 0)
            Koryu_Decide(o);
    }
    switch (RS16(o + 6)) {
    case 1: case 2: case 6: OR16(o + 4, 4); break;
    default: AND16(o + 4, 0xFFFB); break;
    }
    if (RS16(o + 6) == 10 && (R16(o + 0x58) & 3) == 1) { Koryu_Burst(); AND16(o + 0x58, 0xFFFE); }
    if (RS16(o + 6) == 0xE && (R16(o + 0x58) & 3) == 1) AND16(o + 0x58, 0xFFFE);
    if (RS16(o + 6) == 6) {                                         /* roar: darken / restore */
        if ((R16(o + 0x58) & 1) == 1) { Pal_SetFaded(pal_dst(s), 0x100, 0x060DA154u, (int16_t)(R16(o + 0x56) << 4), 0, 0); W16(o + 0x56, R16(o + 0x56) + 1); }
        if ((R16(o + 0x58) & 2) == 2) { Pal_SetFaded(pal_dst(s), 0x100, 0x060DA154u, (int16_t)(R16(o + 0x56) << 4), 0, 0); W16(o + 0x56, R16(o + 0x56) - 1); }
    }
    if (RS16(o + 6) == 8) {                                         /* intro */
        if ((R16(o + 0x58) & 2) == 2) { Koryu_Rock(); AND16(o + 0x58, 0xFFFD); }
        if ((R16(o + 0x58) & 1) == 1) { Koryu_IntroGfx(); AND16(o + 0x58, 0xFFFE); }
        if (RS16(o + 0x5A) == 0x23) {
            W16(o + 0x5A, 0x24);
            for (int k = 0; k < 0x40; k++) if (RS16(PROJ(k)) == 3) W16(o + 0x5A, 0x23);
        }
    }
    if (RS16(o + 6) == 2 && dxabs(o) < 0x600000) { W16(o + 6, 0); W16(o + 0x5A, 0); }
    if (RS16(o + 6) == 9) {
        W32(o + 0x30, 0);
        if (!(R16(o + 2) & 0x4000) && (R16(o + 0x58) & 1)) W32(o + 0x30, 0x8000);
        if (!(R16(o + 2) & 0x8000)) W32(o + 0x30, (uint32_t)-RS32(o + 0x30));
    }
    if (RS16(o + 6) == 2) {
        W32(o + 0x30, 0x10000);
        if (RS16(o + 0x16) < 10) W32(o + 0x30, 0x18000);
        if (R16(o + 2) & 0x8000) W32(o + 0x30, (uint32_t)-RS32(o + 0x30));
    }
    if (RS16(o + 6) == 4) {                                         /* leap */
        W32(o + 0x30, 0x64000);
        if (R16(o + 2) & 0x8000) W32(o + 0x30, (uint32_t)-RS32(o + 0x30));
        if ((RS16(o + 0x5A) == 2 || RS16(o + 0x5A) == 3) && (int32_t)R16(o + 0x1A) < RS16(o + 0x50)) W32(o + 0x34, 0x28000);
        if (!(R16(o + 2) & 0x4000) && RS16(o + 0x5A) == 6) W16(o + 0x5A, 7);
        if (!(R16(o + 2) & 0x4000)) W32(o + 0x30, 0);
    }
    if (RS16(o + 6) == 3) {                                         /* dash */
        OR16(o + 4, 2);
        W32(o + 0x30, 0x28000);
        if ((R16(o + 0x56) & 0xF) == 0) sub_06007CDE(0x200);
        if (R16(o + 2) & 0x8000) W32(o + 0x30, (uint32_t)-RS32(o + 0x30));
        W16(o + 0x56, R16(o + 0x56) + 1);
    }
    if (RS16(o + 6) == 7 || RS16(o + 6) == 0x10) {                  /* fire breath */
        if ((R16(o + 0x58) & 3) == 2) { Pal_SetFaded(pal_dst(s), 0x100, 0x060DA154u, (int16_t)(R16(o + 0x56) << 4), 0, 0); W16(o + 0x56, R16(o + 0x56) + 1); }
        if ((R16(o + 0x58) & 3) == 1) { Koryu_Breath(); AND16(o + 0x58, 0xFFFE); }
        if ((R16(o + 0x58) & 3) == 3) { Pal_SetFaded(pal_dst(s), 0x100, 0x060DA154u, (int16_t)(R16(o + 0x56) << 4), 0, 0); W16(o + 0x56, R16(o + 0x56) - 1); }
    }
    if (RS16(o + 6) == 0) {
        W32(o + 0x30, 0);
        W16(o + 0x5A, 0); W16(o + 0x4A, 0);
        if (EM_Abs(RS32(o + 0xC) - RS32(PL + 0xC)) > 0x500000 && dxabs(o) < 0x640000) W16(o + 6, 6);
    }
    if (R16(o + 0x58) & 0x8000) {
        if (Koryu_AnimEnd(o, s)) return;
    }
    Obj_Integrate(o);
    if (RS32(0x060EA620u) - RS32(o + 8) < 0) W32(o + 8, (uint32_t)(RS32(0x060EA620u) - 0x10000));
    if (RS32(0x060EA618u) - RS32(o + 8) > 0) W32(o + 8, (uint32_t)(RS32(0x060EA618u) + 0x10000));
    Obj_CollideMap(o, 3, 10);
    Enemy_AnimSwitch();
draw:
    EM_Trail(R16(o + 4) & 2, o);
    AND16(o + 4, 0xFFFD);
    if (RS16(o + 6) == 0xE && R16(o + 0x5A) < 4) Koryu_DeathOverlay();
    W16(SPR + 2, (uint32_t)((RS32(o + 8) - RS32(CAM_X)) >> 16));
    W16(SPR + 4, (uint32_t)((RS32(o + 0xC) - RS32(CAM_Y)) >> 16));
    W16(SPR + 0x10, (R16(o + 2) & 0x8000) | 0x200);
    W16(SPR + 0x0A, (uint16_t)COLOR_BANK(SLOT));
    W16(SPR + 0x0C, 0);
    W16(SPR + 0x0E, 0);
    W32(SPR + 0x18, 0);
    if (R16(o) != 0xFFFF) Anim_Tick(o + 0x58);
}

/* Projectile 2 (0x06069CAC): Koryu's fire (anim 0xC; 0xF for the breath when 0x060F4D00
 * bit 0 is set, +0x10 bit 0 = death burst: rotated 32, harmless). Scale cycles 1/1.25/
 * 1.5/2 with a 3 px jitter; hurts Sho (2) unless he is already hurt. Freed at the anim
 * end (and clears 0x060DA874 / 0x060DA880). */
void Proj_KoryuFire02(uint32_t e)
{
    if (R16(e + 4) & 1) {
        int anim = 0xC;
        if (!(R16(e + 0x10) & 1) && (R16(K_ALT) & 1)) anim = 0xF;
        Anim_Start(e + 0x20, B06_DEFS, B06_SCR, 0x60, anim);
        AND16(e + 4, 0xFFFE);
    }
    int16_t s = (int16_t)Enemy_FindSlot(6);
    if (!FROZEN) {
        if (!(R16(PL + 2) & 8) && !(R16(e + 0x10) & 1)) Projectile_Collide(2, 0xC100);
        W16(e + 6, R16(e + 6) + 1);
    }
    W16(e + 0x12, 0x7FFF);
    AND16(e + 4, 0xFFF7);
    int16_t sx = (int16_t)((uint32_t)(RS32(e + 8) - RS32(CAM_X)) >> 16);
    int16_t sy = (int16_t)((uint32_t)(RS32(e + 0xC) - RS32(CAM_Y)) >> 16);
    W16(SPR + 0x0A, (uint16_t)(COLOR_BANK(s) + 0x10));
    W16(SPR + 0x10, (R16(e + 4) & 0x8000) | 0x1800);
    W16(SPR + 0x0C, 0);
    W16(SPR + 0x0E, R16(e + 6) & 3);
    if (R16(e + 0x10) & 1) {
        W16(SPR + 0x10, (R16(e + 4) & 0x8000) | 0x1C00);
        W16(SPR + 0x14, (R16(e + 4) & 0x8000) ? 0xFFE0 : 0x20);
    }
    switch (R16(e + 6) & 3) {
    case 0: W32(SPR + 0x1C, 0x10000); W32(SPR + 0x20, 0x10000); sx += 3; break;
    case 1: W32(SPR + 0x1C, 0x14000); W32(SPR + 0x20, 0x14000); sy -= 3; break;
    case 2: W32(SPR + 0x1C, 0x18000); W32(SPR + 0x20, 0x18000); sx -= 3; break;
    case 3: W32(SPR + 0x1C, 0x20000); W32(SPR + 0x20, 0x20000); sy += 3; break;
    }
    W16(SPR + 2, (uint16_t)sx);
    W16(SPR + 4, (uint16_t)sy);
    if (R16(e) != 0xFFFF) Anim_Tick(e + 0x20);
    if (R16(e + 0x20) & 0x8000) { W16(e, 0xFFFF); W32(G_FLAG, 1); W32(G_FLAG + 0xC, 0); }
}

/* Projectile 3 (0x06069FA8): rock spat in the intro (anim 0xB): while its script step is
 * < 8 it falls (0.03 px/frame^2, -1.5 px start, drifting 0.5 px left) and jumps to step 8
 * on the floor (0x06046FE2 on the layer 0x060EA588); blinks. Harmless. */
void Proj_KoryuRock03(uint32_t e)
{
    if (R16(e + 4) & 1) {
        Anim_Start(e + 0x20, B06_DEFS, B06_SCR, 0x60, 0xB);
        AND16(e + 4, 0xFFFE);
    }
    int16_t s = (int16_t)Enemy_FindSlot(6);
    if (!FROZEN) {
        if (R16(e + 0x22) < 8) {
            if ((int16_t)Map_GetAttr(RS32(e + 8), RS32(e + 0xC), 0x060EA588u) != -1) W16(e + 0x22, 8);
            W32(e + 0xC, (uint32_t)R16(e + 6) * 0x2000u + R32(e + 0xC) - 0x18000u);
            W32(e + 8, R32(e + 8) - 0x8000u);
        }
        W16(e + 6, R16(e + 6) + 1);
    }
    W16(SPR + 2, (uint32_t)((RS32(e + 8) - RS32(CAM_X)) >> 16));
    W16(SPR + 4, (uint32_t)((RS32(e + 0xC) - RS32(CAM_Y)) >> 16));
    W16(SPR + 0x0A, (uint16_t)COLOR_BANK(s));
    W16(SPR + 0x10, R16(e + 4) & 0x8000);
    W16(SPR + 0x0C, 0);
    W16(SPR + 0x0E, R16(e + 0x20) & 3);
    W16(SPR + 0x14, 0);
    if (R16(e) != 0xFFFF) {
        if (!(R16(e + 6) & 1)) AND16(e + 0x20, 0xBFFF); else OR16(e + 0x20, 0x4000);
        Anim_Tick(e + 0x20);
    }
    if (R16(e + 0x20) & 0x8000) W16(e, 0xFFFF);
}

/* ------------------------------------------------ dispatch for fx.c (bosses 6 / 0x17 / 0x1F) */
void Proj_FallingRock08(uint32_t e); void Proj_B17Hand15(uint32_t e);   /* enemy_boss17.c */
void Proj_B17Laser16(uint32_t e); void Proj_B17Beam17(uint32_t e);
void Fx_B17Blast12(uint32_t e); void Fx_B17Smoke13(uint32_t e);
void Proj_KazumaBlade18(uint32_t e); void Proj_KazumaFlame1B(uint32_t e);   /* enemy_boss1f.c */
void Proj_KazumaDragon1C(uint32_t e); void Fx_KazumaBlast14(uint32_t e);
int BossB_Projectile(uint16_t t, uint32_t e)
{
    switch (t) {
    case 2: Proj_KoryuFire02(e); return 1;
    case 3: Proj_KoryuRock03(e); return 1;
    case 8: Proj_FallingRock08(e); return 1;                 /* enemy_boss17.c (0x0605E0E0) */
    case 0x15: Proj_B17Hand15(e); return 1;
    case 0x16: Proj_B17Laser16(e); return 1;
    case 0x17: Proj_B17Beam17(e); return 1;
    case 0x18: Proj_KazumaBlade18(e); return 1;               /* enemy_boss1f.c */
    case 0x1B: Proj_KazumaFlame1B(e); return 1;
    case 0x1C: Proj_KazumaDragon1C(e); return 1;
    default: return 0;
    }
}

int BossB_Effect(uint16_t t, uint32_t e)
{
    switch (t) {
    case 0x12: Fx_B17Blast12(e); return 1;                   /* enemy_boss17.c */
    case 0x13: Fx_B17Smoke13(e); return 1;
    case 0x14: Fx_KazumaBlast14(e); return 1;                 /* enemy_boss1f.c */
    default: return 0;
    }
}
