/* Enemy type 0x0015: stage 2-2 boss (index 3, file STAGE2_2), the masked ninja.
 * Loader 0x06071B24 (0x43 chars of 0x060DC6B2, palettes 0x060DC3AC (256) and 0x060DC5AC
 * (128, bank +0x10), opens a .PBC), handler 0x06071E04, init 0x06072CB8, action setter
 * 0x06073614, decision 0x0607333C, threat 0x06073C26, flee 0x06073D3C, wall stop
 * 0x06072C94, invulnerability 0x06073DB6 / 0x06073DD0 / 0x06073DF6, big graphics
 * 0x06071C3C (0x24C chars) / death graphics 0x06071D2C (99 chars) loaded from the PBC
 * files at 0x060B4938 / 0x060B4944. Projectiles 0xE (dust 0x06072D88) and 0xF (sword
 * slash 0x06072FC4, frames from 0x060B48C0/EA/4912/491A by the boss's frame).
 * It is woken by the two heads of the stage (objhandlers_e.c 0x0604FAC0: 0x060F688C = 1,
 * 0x060F688E = 60 frames). HP 0x060B48BA[difficulty], boss bar 0x06061B58.
 * Intro (0, 1) then 2 = stand: Sho close in front and attacking -> flee (10 back-dash
 * or 15 dash away, +0x48 = 30 invulnerable frames); else when +0x14 runs out picks by
 * distance (24/48/80/120/144 px) and rand % 2..4: 3 dash slash, 4 walk slash, 5 jump
 * slash ((dx) / 50 per frame), 7 leap ((dx +- 40 px) / 38), 9 run (afterimages), 10 back
 * steps (+0x4E), 12 slow walk slash. Hurt -> 0xD; dead -> 0xE (knocked away) or 0x10
 * (cut down: overlay anim 0x12 + body anim 0x11 drifting), then 0x15: fade out and
 * stage clear (0x0602529E |= 10). Uses the shared afterimage trail (enemy_more.c). */
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

#define B15_DEFS 0x060B1DCCu
#define B15_SCR  0x060DE294u
#define PROJ(i)  (PROJECTILES + (uint32_t)((int16_t)(i) * 0x40))
#define OBJ(i)   (OBJ_TABLE + (uint32_t)(int16_t)((int16_t)(i) * 0x78))

static void load_chars(uint32_t tbl, int n, const char *what)
{
    (void)what;
    int16_t s = (int16_t)Enemy_FindSlot(0x15);
    int base = (uint16_t)RS16(0x060A9064u + 2u * (uint32_t)s);
    uint32_t off = R32(0x060A9070u + 4u * (uint32_t)s);
    for (int i = 0; i < n; i++) {
        uint32_t r = tbl + (uint32_t)i * 8;                       /* {size, ?, w/8, h} */
        Char_Register((base + i) & 0xFFFF, Ram_Ptr(r + 4)[0] * 8, Ram_Ptr(r + 5)[0], RS16(r + 2), off);
        off += (uint32_t)R16(r) * 32;
    }
}

void Enemy15_Load(void)                                     /* 0x06071B24 */
{
    load_chars(0x060DC6B2u, 0x43, "boss");
    int16_t s = (int16_t)Enemy_FindSlot(0x15);
    uint16_t b = R16(0x060A9084u + 2u * (uint32_t)s);
    Pal_Copy((b & 0xFFF) << 4, 0x100, 0x060DC3ACu);
    Pal_Copy((uint16_t)(b + 0x10) * 16, 0x80, 0x060DC5ACu);
    Res_Request(2, 0, 0, 0xFFFFFFFFu, 0, 0x060B4938u);
}

static void big_graphics(uint32_t file, uint32_t tbl, int n)   /* 0x06071C3C / 0x06071D2C */
{
    int16_t s = (int16_t)Enemy_FindSlot(0x15);
    Res_Request(1, 1, 0, 0xFFFFFFFFu, R32(0x060A9070u + 4u * (uint32_t)s) + 0x25C04740u, file);
    load_chars(tbl, n, "big");
}

static int center(uint32_t o)
{
    return RS32(0x060EA618u) + 0x320000 < RS32(o + 8) && RS32(o + 8) < RS32(0x060EA620u) - 0x320000;
}

static void slash(uint32_t o, uint16_t anim)                /* projectile 0xF on the boss */
{
    int s = Projectile_Spawn(0xF, RS32(o + 8), RS32(o + 0xC), 0, 0);
    if ((int16_t)s == -1) return;
    uint32_t p = PROJ(s);
    AND16(p + 4, 0x7FFF);
    if (R16(o + 2) & 0x8000) OR16(p + 4, 0x8000);
    W16(p + 0x10, anim);
    W16(p + 0x14, R16(ENEMY_CUR_IDX));
}

static void dust(uint32_t o, uint16_t anim, uint16_t delay) /* projectile 0xE */
{
    int s = Projectile_Spawn(0xE, RS32(o + 8), RS32(o + 0xC), 0, 0);
    if ((int16_t)s == -1) return;
    uint32_t p = PROJ(s);
    AND16(p + 4, 0x7FFF);
    if (R16(o + 2) & 0x8000) OR16(p + 4, 0x8000);
    W16(p + 0x10, anim);
    W16(p + 6, delay);
    W16(p + 0x14, R16(ENEMY_CUR_IDX));
}

static void face_sho(uint32_t o) { EM_FaceSho(o); }

static void Ninja15_Set(int16_t a)                          /* 0x06073614 */
{
    uint32_t o = O;
    for (;;) {
        switch (a) {
        case 0: case 1: W16(o + 6, (uint16_t)a); return;
        case 2:
            AND16(o + 4, 0xFFFD);
            W16(o + 6, 2);
            W16(o + 0x14, (R16(0x060143EAu) % 4u) == 0 ? 6 : 0x18);    /* A.BIN 0x0600A604 */
            return;
        case 3: case 7: AND16(o + 4, 0xFFF9); W16(o + 6, (uint16_t)a); return;
        case 4: AND16(o + 4, 0xFFF9); W16(o + 6, 4); slash(o, 0x16); return;
        case 5: AND16(o + 4, 0xFFF9); W16(o + 6, 5); slash(o, 0x17); return;
        case 6: case 8: W16(o + 6, (uint16_t)a); W32(o + 0x30, 0); return;
        case 9: AND16(o + 4, 0xFFF9); W16(o + 6, 9); EM_TrailReset(); slash(o, 0x18); return;
        case 10: OR16(o + 4, 2); face_sho(o); W16(o + 0x4E, 100); W16(o + 6, 10); return;
        case 0xB: OR16(o + 2, 8); W16(o + 0x48, 100); W16(o + 6, 0xB); W32(o + 0x30, 0); return;
        case 0xC: AND16(o + 4, 0xFFFD); W16(o + 6, 0xC); return;
        case 0xD:
            face_sho(o);
            W32(o + 0x30, (R16(o + 2) & 0x8000) ? 0x00020000u : 0xFFFE0000u);
            W32(o + 0x34, 0x00040000u);
            OR16(o + 2, 0x4000);
            W16(o + 6, 0xD);
            return;
        case 0xE: W16(o + 6, 0xE); dust(o, 0x13, 0x58); return;
        case 0xF: face_sho(o); EM_TrailReset(); W16(o + 6, 0xF); return;
        case 0x10: W16(o + 6, 0x10); W16(o + 0x4A, 0); W16(o + 0x4C, 0); dust(o, 0x14, 0x2D); return;
        case 0x15: AND16(o + 2, 0xFFF7); W16(o + 6, 0x15); return;
        case 1000: face_sho(o); W16(o + 0x4E, 1); W16(o + 6, 10); return;
        case 1001:
            face_sho(o);
            W16(o + 0x4E, (uint16_t)((uint32_t)sub_0602DDD2() % 3u + 1));
            W16(o + 6, 10);
            return;
        case 1002:
            if (!(R16(o + 4) & 2) && !(R16(o + 4) & 4)) { Ninja15_Set(2); W16(o + 0x14, 0x3C); return; }
            a = 2;
            continue;
        default: return;
        }
    }
}

static void Ninja15_Init(void)                              /* 0x06072CB8 */
{
    uint32_t o = O;
    W32(o + 8, 0x05A40000u);
    W32(o + 0xC, 0x04460000u);
    Ninja15_Set(0);
    Anim_Start(o + 0x58, B15_DEFS, B15_SCR, CHAR_BASE(SLOT), RS16(o + 6));
    AND16(o + 2, 0xFFFE);
    W16(o + 4, 0);
    W16(o + 0x16, R16(0x060B48BAu + 2u * (uint32_t)RS16(0x060252C4u)));
    W16(o + 0x14, 0);
    W32(o + 0x30, 0); W32(o + 0x34, 0);
    OR16(o + 2, 0x8000);
    OR16(o + 2, 8);
    W16(o + 0x48, 1);
    W16(0x060F688Cu, 0);
    EM_TrailReset();
}

static int16_t Ninja15_Decide(void)                         /* 0x0607333C */
{
    uint32_t o = O;
    int32_t d = EM_Abs(RS32(PL + 8) - RS32(o + 8));
    uint32_t r = (uint32_t)sub_0602DDD2();
    int c = center(o) && !(R16(o + 4) & 4);
    int16_t res;
    int32_t m;
    if (d < 0x180000) {
        if ((int16_t)Player_AttackKind() != 6) res = 12;
        else res = (c && (r & 1)) ? 10 : 5;
    } else if (d < 0x300000) {
        if ((int16_t)Player_AttackKind() != 6) res = 4;
        else {
            m = (int32_t)r % (c ? 3 : 2);                             /* A.BIN 0x0600A604 */
            res = m == 0 ? 3 : m == 1 ? ((R16(PL + 2) & 0x4000) ? 3 : 5) : 10;
        }
    } else if (d < 0x500000) {
        if (R16(PL + 2) & 0x4000) res = 4;
        else {
            m = (int32_t)r % (c ? 4 : 3);
            static const int16_t t[4] = { 4, 5, 3, 10 };
            res = t[m < 0 ? 0 : m];
        }
    } else if (d < 0x780000) {
        m = (int32_t)r % (c ? 4 : 3);
        static const int16_t t[4] = { 5, 7, 12, 10 };
        res = t[m < 0 ? 0 : m];
    } else if (d < 0x900000) {
        m = (int32_t)r % (c ? 3 : 2);
        static const int16_t t[3] = { 7, 12, 10 };
        res = t[m < 0 ? 0 : m];
    } else {
        res = 9;
        if (c && ((int32_t)r % 2) == 1) res = 10;
    }
    return res == 0 ? 2 : res;
}

static int Ninja15_Threat(void)                             /* 0x06073C26 */
{
    uint32_t o = O;
    int r = 0;
    int16_t k = (int16_t)Player_AttackKind();
    if ((R16(PL + 2) & 0x8000) == (R16(o + 2) & 0x8000)) return 0;
    if (EM_Abs(RS32(o + 8) - RS32(PL + 8)) < 0x580000 && EM_Abs(RS32(o + 0xC) - RS32(PL + 0xC)) < 0x280000 &&
        k != 6 && (sub_0602DDD2() & 3) == 0 && !(R16(o + 4) & 4)) {
        OR16(o + 4, 4);
        r = 1;
    }
    if (k == 3 && EM_Abs(RS32(PL + 8) - RS32(o + 8)) < 0x5A0000 && EM_Abs(RS32(PL + 0xC) - RS32(o + 0xC)) < 0x640000) {
        OR16(o + 4, 0x10);
        r = 1;
    }
    return r;
}

static void Ninja15_Flee(void)                              /* 0x06073D3C */
{
    uint32_t o = O;
    OR16(o + 2, 8);
    W16(o + 0x48, 0x1E);
    OR16(o + 4, 2);
    Ninja15_Set((!(R16(o + 4) & 0x10) && center(o)) ? 1000 : 0xF);
}

static void dodge_upward(uint32_t o)        /* Sho's upward slash (kind 3) close by -> flee */
{
    if ((int16_t)Player_AttackKind() == 3 && EM_Abs(RS32(PL + 8) - RS32(o + 8)) < 0x5A0000 &&
        EM_Abs(RS32(PL + 0xC) - RS32(o + 0xC)) < 0x640000) {
        W32(o + 0x30, 0);
        OR16(o + 4, 0x10);
        Ninja15_Flee();
    }
}

static void anim_reset(uint32_t o) { AND16(o + 0x58, 0x7FFF); W16(o + 0x5A, 0); W16(o + 0x72, 0); }

static void Ninja15_Dead(uint32_t o)                        /* action 0x15: fade out, clear */
{
    Fade_SetTarget(0x100, 0x100, 0x100);
    Fade_SetEndLayerMask(0);
    Fade_SetNextSpeed(0x20000, 1);
    Fade_StartOut();
    W16(0x0602529Eu, (R16(0x0602529Eu) & 0xFFF8) | 10);
    W16(o, 0xFFFF);
}

void EnemyHandler_0015(void)                                /* 0x06071E04 */
{
    uint32_t o = O;
    if (R16(o + 2) & 1) Ninja15_Init();
    if (RS16(0x060F688Cu) == 0) return;                             /* the heads are still there */
    if (RS16(0x060F688Eu) != 0) { if (!FROZEN) W16(0x060F688Eu, R16(0x060F688Eu) - 1); return; }
    uint16_t ovl = 0;
    if (!FROZEN) {
        Hud_DrawBossBar(RS16(0x060B48BAu + 2u * (uint32_t)RS16(0x060252C4u)), RS16(o + 0x16));
        if (R16(o + 6) != 0xF && RS16(o + 0x16) > 0) {
            int16_t r = (int16_t)Enemy_AttackPlayer(1, 0, 0xC000);
            if (r != 0) { OR16(o + 4, 2); if (r == 3) sub_06007CDE(R16(o + 6) == 7 ? 0x10C : 0x104); }
        }
        int16_t r = (int16_t)Enemy_CheckPlayerHits(0xC000);
        if (R16(o + 6) != 9 && r != 0) OR16(o + 4, 2);
        if (r == 1 && R16(o + 6) == 2) { OR16(o + 4, 8); W16(o + 0x14, R16(o + 0x14) + 1); }
        if (r == 0 && (R16(o + 4) & 8)) { AND16(o + 4, 0xFFF7); if (R16(o + 6) == 2) Ninja15_Set(Ninja15_Decide()); }
        int inv = RS16(o + 0x48) != 0 && (R16(o + 2) & 8);
        uint16_t a = R16(o + 6);
        if (!(R16(o + 2) & 8) || inv || a == 0xD || a == 0xE || a == 0x10) {
            if (RS16(o + 0x48) != 0 && (R16(o + 2) & 8)) {         /* invulnerable: count down */
                int done = RS16(o + 0x48) == 0;
                if (!done) { W16(o + 0x48, R16(o + 0x48) - 1); done = RS16(o + 0x48) == 0; }
                if (done) { AND16(o + 2, 0xFFF7); W16(o + 0x48, 0); }
            }
        } else {                                                    /* hurt */
            W32(o + 0x30, 0);
            AND16(o + 4, 0xFFFE);
            int16_t k = (r == 2) ? (int16_t)Player_AttackKind() : 4;
            if (RS16(o + 0x16) < 1) {
                OR16(PL + 4, 0x400);
                if (k == 4 || k == 5) Ninja15_Set(0xE);
                else { Ninja15_Set(0x10); EM_OverlayStart(B15_DEFS, B15_SCR, 0x12, o + 0x52, o + 0x54, o + 0x56); }
                sub_06007CDE(0x223);
            } else Ninja15_Set(0xD);
            Enemy_AnimSwitch();
        }
        if (!(R16(o + 0x58) & 0x8000)) {
            uint16_t f = R16(o + 0x58) & 3;
            int right = !(R16(o + 2) & 0x8000);
            switch (R16(o + 6)) {
            case 0:
                OR16(o + 2, 8); W16(o + 0x48, 1);
                if (f == 1) { sub_06007CDE(0x101); W32(o + 0x30, 0xFFFB8000u); W32(o + 0x34, 0x20000u); AND16(o + 0x58, 0xFFFC); }
                else if (f == 2 && !(R16(o + 2) & 0x4000)) { Ninja15_Set(1); W32(o + 0x30, 0); }
                break;
            case 1:
                OR16(o + 2, 8); W16(o + 0x48, 1);
                if (f == 1) { AND16(o + 0x58, 0xFFFC); big_graphics(0x060B4938u, 0x060DC8CEu, 0x24C); Res_Request(2, 0, 0, 0xFFFFFFFFu, 0, 0x060B4944u); }
                break;
            case 2:
                face_sho(o);
                if (!Ninja15_Threat()) {
                    int16_t t = (int16_t)(RS16(o + 0x14) - 1);
                    W16(o + 0x14, (uint16_t)t);
                    if (t < 1) Ninja15_Set(Ninja15_Decide());
                } else Ninja15_Flee();
                break;
            case 3:
                if (f == 1) {
                    AND16(o + 0x58, 0xFFFC);
                    W32(o + 0x30, right ? 0x60000u : 0xFFFA0000u);
                    OR16(o + 4, 1);
                    EM_TrailReset();
                } else if (f == 2) { AND16(o + 0x58, 0xFFFC); W32(o + 0x30, 0); AND16(o + 4, 0xFFFE); }
                else dodge_upward(o);
                break;
            case 4:
                W32(o + 0x30, right ? 0x1B000u : 0xFFFE5000u);
                dodge_upward(o);
                break;
            case 5:
                if (f == 1) {
                    AND16(o + 0x58, 0xFFF0);
                    W32(o + 0x30, (uint32_t)EM_Sdiv(RS32(PL + 8) - RS32(o + 8), 50));
                    W32(o + 0x34, 0x80000u);
                    sub_06007CDE(0x126);
                } else if (f == 2 && !(R16(o + 2) & 0x4000)) Ninja15_Set(6);
                break;
            case 7:
                if (f == 1) {
                    AND16(o + 0x58, 0xFFF0);
                    int32_t dx = RS32(PL + 8) - RS32(o + 8);
                    W32(o + 0x30, (uint32_t)EM_Sdiv(dx + (dx < 0 ? -0x280000 : 0x280000), 38));
                    W32(o + 0x34, 0x60000u);
                    sub_06007CDE(0x126);
                } else if (f == 2 && !(R16(o + 2) & 0x4000)) Ninja15_Set(8);
                break;
            case 9:                                                 /* run */
                W32(o + 0x30, right ? 0x48000u : 0xFFFB8000u);
                OR16(o + 4, 1);
                if (R16(0x060143EAu) % 7u == 0) sub_06007CDE(0x112);       /* A.BIN 0x0600A604 */
                if (right ? RS32(o + 8) >= RS32(0x060EA620u) - 0x280000 : RS32(o + 8) <= RS32(0x060EA618u) + 0x280000) {
                    W32(o + 0x30, 0); Ninja15_Set(1002); AND16(o + 4, 0xFFFE);   /* 0x06072C94 */
                }
                break;
            case 10: {                                              /* back steps */
                static const int32_t v[4] = { 0, 0x20000, 0x2D000, 0x21000 };
                if (f == 0) W32(o + 0x30, 0);
                else W32(o + 0x30, (uint32_t)(right ? -v[f] : v[f]));
                int keep = 1;
                if ((R16(o + 0x58) & 0xF) == 4) {
                    if (RS32(o + 8) < RS32(0x060EA618u) + 0x320000 || RS32(0x060EA620u) - 0x320000 < RS32(o + 8)) { Ninja15_Set(0xB); keep = 0; }
                    else {
                        W16(o + 0x4E, R16(o + 0x4E) - 1);
                        if (RS16(o + 0x4E) == 0) { Ninja15_Set(0xB); keep = 0; }
                    }
                }
                if (keep) { if (R16(o + 2) & 8) OR16(o + 4, 1); else AND16(o + 4, 0xFFFE); }
                break;
            }
            case 0xC:
                W32(o + 0x30, right ? 0xE000u : 0xFFFF2000u);
                dodge_upward(o);
                break;
            case 0xD: if (!(R16(o + 2) & 0x4000)) W32(o + 0x30, 0); break;
            case 0xE:
                if (R16(o + 2) & 0x4000) anim_reset(o);
                if ((R16(o + 0x58) & 0xF) == 1) { AND16(o + 0x58, 0xFFF0); big_graphics(0x060B4944u, 0x060DDB32u, 99); }
                break;
            case 0xF:
                W32(o + 0x30, right ? 0x60000u : 0xFFFA0000u);
                OR16(o + 4, 1);
                break;
            case 0x10:
                if (R16(o + 2) & 0x4000) anim_reset(o);
                if ((R16(o + 0x58) & 3) == 1) { AND16(o + 0x58, 0xFFFC); big_graphics(0x060B4944u, 0x060DDB32u, 99); }
                break;
            }
        } else {
            switch (RS16(o + 6)) {
            case 1: Ninja15_Set(2); AND16(o + 2, 0xFFF7); W16(o + 0x48, 0); break;
            case 8: W32(o + 0x30, 0); Ninja15_Set(1002); break;
            case 0xB: W32(o + 0x30, 0); Ninja15_Set(1002); AND16(o + 2, 0xFFF7); W16(o + 0x48, 0); break;
            case 0xD:
                if (!(R16(o + 2) & 0x4000)) {
                    OR16(o + 2, 8);
                    W16(o + 0x48, 0x1E);
                    if (!(R16(o + 4) & 4)) {
                        if ((R16(0x060143EAu) & 3) == 3 && RS32(o + 8) < RS32(0x060EA620u) - 0x320000 &&
                            RS32(0x060EA618u) + 0x320000 < RS32(o + 8)) Ninja15_Set(1001);
                        else if ((sub_0602DDD2() & 3) == 0) Ninja15_Set(1002);
                        else Ninja15_Set(0xF);
                    } else Ninja15_Set(2);
                } else anim_reset(o);
                break;
            case 0xE: Ninja15_Set(0x15); EM_TrailReset(); return;
            case 0xF: AND16(o + 4, 0xFFFE); W32(o + 0x30, 0); Ninja15_Set(1002); break;
            case 0x15: Ninja15_Dead(o); break;
            default: W32(o + 0x30, 0); Ninja15_Set(2); break;
            }
        }
        Obj_Integrate(o);
        EM_ClampX(o);
        Obj_CollideMap(o, 1, 1);
        Enemy_AnimSwitch();
    }
    if (R16(o + 6) != 0xE && R16(o + 6) != 0x10) EM_Trail(R16(o + 4) & 1, o);
    if (R16(o + 6) == 0x10 && (R16(o + 0x58) & 2)) {                /* cut down */
        if ((R16(o + 0x58) & 3) == 3) {
            Anim_Start(SCRATCH_ANIM, B15_DEFS, B15_SCR, CHAR_BASE(SLOT), 0x12);
            Enemy_DrawOverlay(SCRATCH_ANIM, (int16_t)COLOR_BANK(SLOT), o + 0x52, o + 0x54, o + 0x56);
            ovl = R16(SCRATCH_ANIM);
            if (ovl & 0x8000) { Ninja15_Set(0x15); EM_TrailReset(); return; }
        }
        Anim_Start(SCRATCH_ANIM2, B15_DEFS, B15_SCR, CHAR_BASE(SLOT), 0x11);
        W16(SPR + 2, (uint32_t)((int16_t)((RS32(o + 8) - RS32(CAM_X)) >> 16) + (RS16(o + 0x4A) >> 2)));
        W16(SPR + 4, (uint32_t)((int16_t)((RS32(o + 0xC) - RS32(CAM_Y)) >> 16) + (RS16(o + 0x4C) >> 2)));
        W16(SPR + 0x10, R16(o + 2) & 0x8000);
        W16(SPR + 0x0A, (uint16_t)COLOR_BANK(SLOT));
        W16(SPR + 0x0C, 0);
        W16(SPR + 0x0E, ovl & 3);
        Anim_Tick(SCRATCH_ANIM2);
        if (!FROZEN && (R16(o + 0x58) & 3) == 3) {
            W16(o + 0x4A, (uint16_t)(RS16(o + 0x4A) + ((R16(o + 2) & 0x8000) ? 1 : -1)));
            W16(o + 0x4C, R16(o + 0x4C) + 1);
        }
    }
    if (R16(o + 6) == 0x15) OR16(o + 0x58, 0x4000);
    W16(SPR + 2, (uint32_t)((RS32(o + 8) - RS32(CAM_X)) >> 16));
    W16(SPR + 4, (uint32_t)((RS32(o + 0xC) - RS32(CAM_Y)) >> 16));
    W16(SPR + 0x10, (R16(o + 2) & 0x8000) | 0x1800);
    W32(SPR + 0x1C, 0x10000); W32(SPR + 0x20, 0x10000);
    W16(SPR + 0x0A, (uint16_t)COLOR_BANK(SLOT));
    W16(SPR + 0x0C, 0);
    if (R16(o + 6) == 0xE) W16(SPR + 0x0E, (R16(o + 0x58) & 0xF) < 2 ? 0 : (uint16_t)((R16(o + 0x58) & 0xF) - 1));
    else if (R16(o + 6) == 0x10 && (R16(o + 0x58) & 3) == 3) W16(SPR + 0x0E, ovl & 3);
    else W16(SPR + 0x0E, 0);
    Anim_Tick(o + 0x58);
}

/* Projectile 0xE (0x06072D88): dust / ghost of the falling boss (anim from +0x10), starts
 * after +6 frames, follows the boss while it is down, then rises and shrinks. */
void Proj_Dust0E(uint32_t e)
{
    int16_t s = (int16_t)Enemy_FindSlot(0x15);
    if (R16(e + 4) & 1) {
        Anim_Start(e + 0x20, B15_DEFS, B15_SCR, CHAR_BASE(s), RS16(e + 0x10));
        AND16(e + 4, 0xFFFE);
        W16(e + 0x12, 0x7FFF);
        W16(e + 0x10, 0);
        W32(e + 0x1C, 0x10000);
    }
    if (R16(e + 6) != 0) { if (!FROZEN) W16(e + 6, R16(e + 6) - 1); return; }
    if (!FROZEN) {
        uint32_t ob = OBJ(RS16(e + 0x14));
        if (R16(ob) == 0x15 && (R16(ob + 6) == 0xE || R16(ob + 6) == 0x10) && (R16(e + 0x20) & 3) == 0) {
            W32(e + 8, R32(ob + 8)); W32(e + 0xC, R32(ob + 0xC));
        } else {
            W32(e + 0xC, R32(e + 0xC) - 0x2A00);
            W32(e + 0x1C, R32(e + 0x1C) - 0xD0);
        }
        if ((R16(e + 0x20) & 3) == 1) {
            AND16(e + 0x20, 0xFFFC);
            if (R16(e + 0x10) < 3) W16(e + 0x10, R16(e + 0x10) + 1);
            else W16(e, 0xFFFF);
        }
        if (R16(0x060143EAu) & 1) OR16(e + 0x20, 0x4000); else AND16(e + 0x20, 0xBFFF);
    }
    W16(SPR + 2, (uint32_t)((RS32(e + 8) - RS32(CAM_X)) >> 16));
    W16(SPR + 4, (uint32_t)((RS32(e + 0xC) - RS32(CAM_Y)) >> 16));
    W16(SPR + 0x0A, (uint16_t)(COLOR_BANK(s) + 0x10));
    W16(SPR + 0x10, (R16(e + 4) & 0x8000) | 0xA00);
    W32(SPR + 0x20, R32(e + 0x1C));
    W16(SPR + 0x0C, 0);
    W16(SPR + 0x0E, R16(e + 0x10));
    W32(SPR + 0x18, 3);
    Anim_Tick(e + 0x20);
}

/* Projectile 0xF (0x06072FC4): the boss's sword slash (anim from +0x10), frame chosen from
 * the boss's own frame (+0x5A) by 0x060B48C0 / 0x060B48EA / 0x060B4912 / 0x060B491A
 * (actions 4/5/6/9; -1 hidden, -2 over); hurts Sho for 1. */
void Proj_Slash0F(uint32_t e)
{
    int16_t s = (int16_t)Enemy_FindSlot(0x15);
    if (R16(e + 4) & 1) {
        Anim_Start(e + 0x20, B15_DEFS, B15_SCR, CHAR_BASE(s), RS16(e + 0x10));
        AND16(e + 4, 0xFFFE);
        W16(e + 0x12, 0x7FFF);
    }
    if (!FROZEN) {
        uint32_t ob = OBJ(RS16(e + 0x14));
        uint16_t a = R16(ob + 6);
        if (R16(ob) != 0x15 || a == 0xE || a == 0x10 || a == 0xD || (R16(ob + 0x58) & 0x8000)) { W16(e, 0xFFFF); return; }
        uint32_t t;
        if (a == 4) t = 0x060B48C0u; else if (a == 5) t = 0x060B48EAu;
        else if (a == 6) t = 0x060B4912u; else if (a == 9) t = 0x060B491Au;
        else { W16(e, 0xFFFF); return; }
        W16(e + 0x22, R16(t + 2u * (uint32_t)R16(ob + 0x5A)));
        if (RS16(e + 0x22) == -1) { W16(e + 0x22, 0); OR16(e + 0x20, 0x4000); }
        else {
            if (RS16(e + 0x22) == -2) { W16(e, 0xFFFF); return; }
            if (RS16(e + 0x22) == 0) { if (R16(ob + 0x72) == 0) sub_06007CDE(0x127); }
            else if (a == 4 && RS16(e + 0x22) == 3 && R16(ob + 0x72) == 0) sub_06007CDE(0x127);
            W32(e + 8, R32(ob + 8)); W32(e + 0xC, R32(ob + 0xC));
            AND16(e + 0x20, 0xBFFF);
            Projectile_Collide(1, 0xC000);
        }
    }
    W16(SPR + 2, (uint32_t)((RS32(e + 8) - RS32(CAM_X)) >> 16));
    W16(SPR + 4, (uint32_t)((RS32(e + 0xC) - RS32(CAM_Y)) >> 16));
    W16(SPR + 0x0A, (uint16_t)(COLOR_BANK(s) + 0xF));
    W16(SPR + 0x10, (R16(e + 4) & 0x8000) | 0x200);
    W16(SPR + 0x0C, 0);
    W16(SPR + 0x0E, 0);
    W32(SPR + 0x18, 1);
    Anim_Tick(e + 0x20);
}
