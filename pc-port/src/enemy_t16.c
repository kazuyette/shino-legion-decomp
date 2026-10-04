/* Enemy type 0x0016 (stage 3-1): bomb-throwing ninja. Loader 0x06073E2C, handler
 * 0x06073EC4, init 0x06074D54, flip test 0x06075D10, range 0x06075DB8, decision
 * 0x06075DEC, back-off limit 0x06075C84; projectiles 0x13 (bomb 0x06074E88, its hand
 * flash 0x06075368) and 0x14 (blast 0x06075524). 4 HP, 300 points.
 * It keeps its distance: by range (< 24 / 56 / 72 / 160 / 480 px) it backs off (1, to
 * +0x4A = x -+ 64 px), crouches (3, spark anim 0xD frames 0x060B5D64) and throws a bomb
 * (2: projectile 0x13 at +-32,-51 px, angle +-10, speed 6.5), or rushes (9) and leaps
 * at Sho when he comes within 56 px (0x12 -> +0x50). Sho attacking it from the front at
 * close range -> back-flip (0x10). It does not walk off ledges (floor probe ahead via
 * 0x06046FE2 / 0x06045B88 -> +0x10 bits 4/8 = blocked). Subtype 0 hides (0xF) until Sho
 * comes within 236 px, drops in from the top of the screen and, when killed, goes back
 * home (+0x1E/+0x20 px, 600 frames, no item); subtype 1 waits (0x11) until within 200 px.
 * Bombs explode on Sho/guard or on the 0x1021 lanterns (state 7) into 5 blasts that grow
 * (0x600/frame) and hurt Sho (1) or break the lanterns (flags2 |= 8). */
#include "enemy_more.h"
#include "anim.h"
#include "sprite.h"
#include "collision.h"
#include "objects.h"
#include "objhandlers.h"
#include "player_ext.h"
#include "fixmath.h"
#include "fx.h"

#define T16_DEFS 0x060B4950u
#define T16_SCR  0x060DEB28u

void Enemy16_Load(void)                                     /* 0x06073E2C */
{
    Enemy_RegisterChars(0x16, 0x060DE2F8u);
    int16_t s = (int16_t)Enemy_FindSlot(0x16);
    uint16_t b = R16(0x060A9084u + 2u * (uint32_t)s);
    Pal_Copy((b & 0xFFF) << 4, 16, 0x060DE7C4u);
    Pal_Copy((uint16_t)(b + 1) * 16, 16, 0x060DE7E4u);
    Pal_Copy((uint16_t)(b + 2) * 16, 16, 0x060DE804u);
    Pal_Copy((uint16_t)(b + 3) * 16, 16, 0x060DE824u);
}

static void Bomber_Init(void)                               /* 0x06074D54 */
{
    uint32_t o = O;
    W16(o + 6, (R16(o + 0x12) & 0x1F) == 0 ? 0xF : 0x11);
    Anim_Start(o + 0x58, T16_DEFS, T16_SCR, CHAR_BASE(SLOT), RS16(o + 6));
    AND16(o + 2, 0xFFFE);
    W16(o + 0x16, 4);
    W16(o + 4, 0);
    for (uint32_t k = 0x48; k <= 0x56; k += 2) W16(o + k, 0);
    W16(o + 0x4E, 1);
    W16(o + 0x1E, R32(o + 8) >> 16);
    W16(o + 0x20, R32(o + 0xC) >> 16);
    W16(o + 0x14, 0);
    if (RS32(o + 8) < RS32(PL + 8)) AND16(o + 2, 0x7FFF); else OR16(o + 2, 0x8000);
}

static int Bomber_FlipTest(void)                            /* 0x06075D10 */
{
    uint32_t o = O;
    if ((R16(PL + 2) & 0x8000) == (R16(o + 2) & 0x8000)) return 0;
    if (EM_Abs(RS32(o + 8) - RS32(PL + 8)) >= 0x00500000) return 0;
    if (EM_Abs(RS32(o + 0xC) - RS32(PL + 0xC)) < 0x001E0000 && !(R16(o + 0x10) & 0xC) &&
        (int16_t)Player_AttackKind() != 6) {
        W16(o + 6, 0x10);
        return 1;
    }
    return 0;
}

/* 0x06075DB8: distance class; beyond 480 px the original returns whatever r0 held */
static int16_t Bomber_Range(int32_t d, int16_t r0)
{
    if (d < 0x00180000) return 0;
    if (d < 0x00380000) return 1;
    if (d < 0x00480000) return 2;
    if (d < 0x00A00000) return 3;
    if (d < 0x01E00000) return 4;
    return r0;
}

static int16_t Bomber_Limit(void)                           /* 0x06075C84 */
{
    uint32_t o = O;
    if (R16(o + 0x10) & 0xC) return 0;
    W16(o + 6, 1);
    AND16(o + 4, 0xFFF7);
    int32_t v;
    if (R16(o + 2) & 0x8000) {
        v = RS32(o + 8) + 0x00400000;
        if (RS32(0x060EA620u) - 0x00190000 <= v) v = RS32(0x060EA620u) - 0x00190000;
    } else {
        v = RS32(o + 8) - 0x00400000;
        if (v <= RS32(0x060EA618u) + 0x00190000) v = RS32(0x060EA618u) + 0x00190000;
    }
    return (int16_t)((uint32_t)v >> 16);
}

static void Bomber_Decide(int16_t r)                        /* 0x06075DEC */
{
    uint32_t o = O;
    switch (r) {
    case 1:
        if ((R16(PL + 2) & 0x4000) && !(R16(PL + 2) & 0x0400)) { W16(o + 6, 3); return; }
        /* fall through */
    case 0:
        if (((uint32_t)sub_0602DDD2() >> 7) % 101u < 30u) W16(o + 6, 3);   /* A.BIN 0x0600CEF4 */
        else W16(o + 0x4A, (uint16_t)Bomber_Limit());
        return;
    case 2:
        if (!(R16(o + 4) & 2)) {
            if (R16(0x060143EAu) & 1) { OR16(o + 4, 1); W16(o + 6, 9); W16(o + 0x48, 2); }
            else W16(o + 0x4A, (uint16_t)Bomber_Limit());
            return;
        }
        if (R16(PL + 2) & 0x0400) { W16(o + 6, 0); return; }
        OR16(o + 4, 1); AND16(o + 4, 0xFFFD);
        W16(o + 6, 9); W16(o + 0x48, 2);
        return;
    case 3:
        if (!(R16(o + 4) & 4)) {
            if (RS16(0x060B5D7Eu + 2u * (uint32_t)RS16(o + 0x4C)) == 0) {
                if (!(R16(o + 4) & 8) && !(R16(PL + 2) & 0x0400)) W16(o + 6, 2);
            } else { W16(o + 6, 9); W16(o + 0x48, 3); OR16(o + 4, 2); }
            W16(o + 0x4C, R16(o + 0x4C) < 0xC ? R16(o + 0x4C) + 1 : 0);
            return;
        }
        if (!(R16(PL + 2) & 0x0400)) { W16(o + 6, 2); AND16(o + 4, 0xFFFB); }
        else W16(o + 6, 0);
        return;
    case 4: W16(o + 6, 9); W16(o + 0x48, 4); return;
    }
}

/* killed: subtype 0 goes home for 600 frames, subtype 1 is removed. 1 = removed */
static int Bomber_Killed(uint32_t o)
{
    Enemy_DeathBurst(o);
    Score_Add(300);
    if (R16(o + 0x12) & 0x1F) { W16(o, 0xFFFF); return 1; }
    W16(o + 6, 0xF);
    W16(o + 0x16, 4);
    W32(o + 8, (uint32_t)((int32_t)RS16(o + 0x1E) << 16));
    W32(o + 0xC, (uint32_t)((int32_t)RS16(o + 0x20) << 16));
    W16(o + 0x14, 600);
    W16(o + 4, 0);
    W16(o + 0x18, 0xFFFF);
    return 0;
}

static uint32_t layer_desc(uint16_t l) { return (((uint32_t)(l & 0xFF) * 0x30u) & 0xFFu) + LAYER_DESC; }

void EnemyHandler_0016(void)                                /* 0x06073EC4 */
{
    uint32_t o = O;
    if (R16(o + 2) & 1) Bomber_Init();
    if (!FROZEN && R16(o + 0x14) != 0) W16(o + 0x14, R16(o + 0x14) - 1);
    {
        int32_t d = RS32(o + 8) - RS32(CAM_X) + (int32_t)0xFF600000;
        if (d < 0) d = RS32(CAM_X) - RS32(o + 8) + 0x00A00000;
        int far = d >= 0x01000000;
        if (!far) {
            d = RS32(o + 0xC) - RS32(CAM_Y) - 0x00700000;
            if (d < 0) d = RS32(CAM_Y) - RS32(o + 0xC) + 0x00700000;
            far = d > 0x00AFFFFF;
        }
        if (far) {
            if (!(R16(o + 2) & 8)) return;
            if (R16(o + 6) == 0xF) return;
        }
    }
    uint16_t ovl = 0;
    if (!FROZEN) {
        int16_t k = 4;
        if (EM_Abs(RS32(o + 0xC) - RS32(PL + 0xC)) > 0x00A00000 && R16(o + 6) != 0xF) W16(o + 6, 0x11);
        if (R16(o + 6) != 0xF && (int16_t)Enemy_AttackPlayer(1, 0, 0xC000) == 3) sub_06007CDE(0x104);
        int16_t r;
        if (R16(o + 6) == 0xF) { OR16(o + 2, 8); OR16(o + 0x12, 0x4000); r = 0; }
        else { AND16(o + 0x12, 0xBFFF); r = (int16_t)Enemy_CheckPlayerHits(0xC000); }
        if (!(R16(o + 2) & 0x4000)) W32(o + 0x30, 0);
        uint16_t a = R16(o + 6);
        if ((R16(o + 2) & 8) && a != 0xF && a != 7 && a != 6 && a != 8) {
            if (r == 2) k = (int16_t)Player_AttackKind();
            if (RS16(o + 0x16) < 1) {
                if (k == 4 || k == 5) W16(o + 6, 6);
                else {
                    W16(o + 6, 8);
                    EM_OverlayStart(T16_DEFS, T16_SCR, 0xE, o + 0x52, o + 0x54, o + 0x56);
                }
            } else W16(o + 6, 7);
            Enemy_AnimSwitch();
        }
        if (!(R16(o + 0x58) & 0x8000)) {
            switch (R16(o + 6)) {
            case 0:
                if (RS32(o + 8) < RS32(PL + 8)) AND16(o + 2, 0x7FFF); else OR16(o + 2, 0x8000);
                if (R16(o + 2) & 0x4000) { W16(o + 6, 4); break; }
                if (!Bomber_FlipTest() && (R16(o + 0x58) & 3) == 1)
                    Bomber_Decide(Bomber_Range(EM_Abs(RS32(PL + 8) - RS32(o + 8)), 1));
                break;
            case 1: {                                               /* back off */
                int stop;
                if (!(R16(o + 2) & 0x8000)) stop = (R16(o + 0x10) & 4) || RS32(o + 8) > RS32(PL + 8);
                else stop = (R16(o + 0x10) & 8) || RS32(PL + 8) > RS32(o + 8);
                if (stop) { W16(o + 6, 0); break; }
                if ((R16(o + 0x58) & 3) == 1) {
                    AND16(o + 0x58, 0xFFFC);
                    int32_t lim = (int32_t)RS16(o + 0x4A) << 16;
                    int reached = !(R16(o + 2) & 0x8000) ? !(lim <= RS32(o + 8)) : (lim < RS32(o + 8));
                    if (reached || EM_Abs(RS32(o + 8) - RS32(PL + 8)) >= 0x00A00000) {
                        W16(o + 6, 0); OR16(o + 4, 4); break;
                    }
                }
                if (R16(o + 2) & 0x4000) W16(o + 6, 4);
                else if (!Bomber_FlipTest()) W32(o + 0x30, (R16(o + 2) & 0x8000) ? 0x00014000u : 0xFFFEC000u);
                break;
            }
            case 2:                                                 /* throw */
                if ((R16(o + 0x58) & 3) == 1) {
                    AND16(o + 0x58, 0xFFFC);
                    int left = (R16(o + 2) & 0x8000) != 0;
                    int s = Projectile_Spawn(0x13, RS32(o + 8) + (left ? -0x00200000 : 0x00200000),
                                             RS32(o + 0xC) + (int32_t)0xFFCD0000, (int16_t)(left ? -10 : 10),
                                             left ? (int32_t)0xFFF98000 : 0x00068000);
                    sub_06007CDE(0x124);
                    if ((int16_t)s != -1) {
                        uint32_t p = PROJECTILES + (uint32_t)((int16_t)s * 0x40);
                        W16(p + 0x10, 0);
                        W16(p + 4, (R16(p + 4) & 0x7FFF) | (R16(o + 2) & 0x8000));
                    }
                }
                if (R16(o + 0x5A) == 3 && R16(o + 0x72) == 1) {
                    int s = Projectile_Spawn(0x13, RS32(o + 8), RS32(o + 0xC), 0, 0);   /* hand flash */
                    if ((int16_t)s != -1) {
                        uint32_t p = PROJECTILES + (uint32_t)((int16_t)s * 0x40);
                        W16(p + 0x10, 1);
                        W16(p + 4, (R16(p + 4) & 0x7FFF) | (R16(o + 2) & 0x8000));
                    }
                }
                break;
            case 4: if (!(R16(o + 2) & 0x4000)) W16(o + 6, 5); break;
            case 6: case 8:
                if (R16(o + 2) & 0x4000) { W16(o + 0x5A, 0); W16(o + 0x72, 0); }
                break;
            case 9: {                                               /* rush */
                int blocked = !(R16(o + 2) & 0x8000) ? (R16(o + 0x10) & 8) : (R16(o + 0x10) & 4);
                if (blocked) { W16(o + 6, 0); OR16(o + 4, 8); break; }
                if (EM_Abs(RS32(o + 8) - RS32(PL + 8)) < 0x00380000) {
                    if (!(R16(o + 4) & 1)) W16(o + 6, 0);
                    else { AND16(o + 4, 0xFFFE); W16(o + 6, 0x12); W16(o + 0x50, 3); }
                    break;
                }
                int16_t rg = Bomber_Range(EM_Abs(RS32(PL + 8) - RS32(o + 8)), (int16_t)R16(o + 0x10));
                if (rg == RS16(o + 0x48) || (Bomber_Decide(rg), R16(o + 6) == 9)) {
                    if (R16(o + 2) & 0x4000) W16(o + 6, 4);
                    else if (!Bomber_FlipTest()) W32(o + 0x30, (R16(o + 2) & 0x8000) ? 0xFFFF4000u : 0x0000C000u);
                } else { W16(o + 0x50, R16(o + 6)); W16(o + 6, 0x12); }
                break;
            }
            case 0xF:                                               /* hidden at home */
                if (EM_Abs(RS32(o + 8) - RS32(PL + 8)) >= 0x00EC0000 || R16(o + 0x4E) == 0) {
                    if (R16(o + 0x14) != 0) return;
                    W16(o + 0x4E, 1);
                    return;
                }
                W16(o + 0x4E, 0);
                W32(o + 0xC, R32(CAM_Y));                           /* drop in from the top */
                W32(o + 0x34, 0xFFF80000u);
                W16(o + 6, 4);
                AND16(o + 2, 0xFFF7);
                break;
            case 0x10: {                                            /* back-flip */
                int blocked = !(R16(o + 2) & 0x8000) ? (R16(o + 0x10) & 4) : (R16(o + 0x10) & 8);
                if (blocked) { W16(o + 6, 0); break; }
                if (R16(o + 2) & 0x4000) W16(o + 6, 4);
                else W32(o + 0x30, (R16(o + 2) & 0x8000) ? 0x00018000u : 0xFFFE8000u);
                break;
            }
            case 0x11:
                if (EM_Abs(RS32(o + 8) - RS32(PL + 8)) < 0x00C80000) W16(o + 6, 0);
                break;
            case 0x12:
                if ((R16(o + 0x58) & 3) == 1) W16(o + 6, R16(o + 0x50));
                break;
            }
        } else {
            switch (RS16(o + 6)) {
            case 2: case 3: case 5: case 0x10: W16(o + 6, 0); break;
            case 6: if (Bomber_Killed(o)) return; break;
            case 7:
                if (!(R16(o + 2) & 0x4000)) { W16(o + 6, 0); AND16(o + 2, 0xFFF7); }
                else { W16(o + 6, 7); AND16(o + 0x58, 0x7FFF); W16(o + 0x5A, 0); W16(o + 0x72, 0); }
                break;
            }
        }
        if (R16(o + 6) != 0xF) {
            Obj_Integrate(o);
            EM_ClampX(o);
            Obj_CollideMap(o, 4, 1);
            uint16_t a2 = R16(o + 6);
            if (a2 == 0x10 || a2 == 1 || a2 == 9) {                 /* no floor ahead -> blocked */
                int32_t dx = 0x00100000;
                if (!(R16(o + 2) & 0x8000)) { if (a2 != 9) dx = -0x00100000; }
                else if (a2 == 9) dx = -0x00100000;
                uint16_t f = Coll_AttrFlags(Map_GetAttr(RS32(o + 8) + dx, RS32(o + 0xC) + 0x00080000, layer_desc(R16(G_COLL_LAYER)))) & 0xF;
                if (RS16(0x060EA64Cu) >= 0)
                    f |= Coll_AttrFlags(Map_GetAttr(RS32(o + 8), RS32(o + 0xC), layer_desc(R16(0x060EA64Cu)))) & 0xF;
                if (!(f & 2)) {
                    int right = !(R16(o + 2) & 0x8000);
                    if (a2 == 9) OR16(o + 0x10, right ? 8 : 4);
                    else OR16(o + 0x10, right ? 4 : 8);
                }
            }
        }
        Enemy_AnimSwitch();
    }
    if ((R16(o + 0x58) & 3) == 1 && R16(o + 6) == 8) {
        Anim_Start(SCRATCH_ANIM, T16_DEFS, T16_SCR, CHAR_BASE(SLOT), 0xE);
        Enemy_DrawOverlay(SCRATCH_ANIM, (int16_t)(COLOR_BANK(SLOT) + 2), o + 0x52, o + 0x54, o + 0x56);
        ovl = R16(SCRATCH_ANIM);
        if (ovl & 0x8000) {
            if (Bomber_Killed(o)) return;
            Enemy_AnimSwitch();
        }
    }
    if (R16(o + 6) == 3 && RS16(0x060B5D64u + 2u * (uint32_t)R16(o + 0x5A)) != -1) {   /* fuse spark */
        int16_t f = RS16(0x060B5D64u + 2u * (uint32_t)R16(o + 0x5A));
        if ((f == 0 || f == 3) && R16(o + 0x72) == 0) sub_06007CDE(0x127);
        Anim_Start(SCRATCH_ANIM2, T16_DEFS, T16_SCR, CHAR_BASE(SLOT), 0xD);
        W16(SCRATCH_ANIM2 + 2, (uint16_t)f);
        W16(SPR + 2, (uint32_t)((RS32(o + 8) - RS32(CAM_X)) >> 16));
        W16(SPR + 4, (uint32_t)((RS32(o + 0xC) - RS32(CAM_Y)) >> 16));
        W16(SPR + 0x10, (R16(o + 2) & 0x8000) | 0x200);
        W16(SPR + 0x0A, (uint16_t)(COLOR_BANK(SLOT) + 1));
        W16(SPR + 0x0C, 0);
        W32(SPR + 0x18, 1);
        W16(SPR + 0x0E, 1);
        Anim_Tick(SCRATCH_ANIM2);
    }
    if (R16(o + 6) == 0xF) OR16(o + 0x58, 0x4000); else AND16(o + 0x58, 0xBFFF);
    EM_SprBasic(o, COLOR_BANK(SLOT));
    if (R16(o + 6) == 6) W16(SPR + 0x0E, R16(o + 0x58) & 3);
    else if (R16(o + 6) == 8 && (R16(o + 0x58) & 3) == 1) W16(SPR + 0x0E, ovl & 3);
    W32(SPR + 0x18, 0);
    Anim_Tick(o + 0x58);
}

/* ------------------------------------------------------------ projectiles */

/* world rect of a hitbox {kind,l,t,r,b} at (x,y), mirrored when flip, scaled by s */
static void box_rect(uint32_t hb, int32_t x, int32_t y, int flip, int32_t s, ObjRect *r)
{
    int32_t a = (int32_t)RS16(hb + 2) << 16, b = (int32_t)RS16(hb + 6) << 16;
    int32_t c = (int32_t)RS16(hb + 4) << 16, d = (int32_t)RS16(hb + 8) << 16;
    if (s) { a = FixMul(a, s); b = FixMul(b, s); c = FixMul(c, s); d = FixMul(d, s); }
    if (flip) { a = -a; b = -b; }
    r->l = (a + x < b + x) ? a + x : b + x;
    r->r = (b + x < a + x) ? a + x : b + x;
    r->t = (c + y < d + y) ? c + y : d + y;
    r->b = (d + y < c + y) ? c + y : d + y;
}

/* does rect p touch a body box (kind 0) of a 0x1021 lantern in state 7 within `range`?
 * returns the lantern or 0 */
static uint32_t lantern_hit(const ObjRect *p, int32_t x, int32_t range)
{
    for (int16_t s = 0; s < 0xC0; s++) {
        uint32_t l = OBJ_TABLE + (uint32_t)(int16_t)(s * 0x78);
        if (R16(l) != 0x1021 || R16(l + 6) != 7) continue;
        if (EM_Abs(RS32(l + 8) - x) >= range + 1) continue;
        for (uint32_t hb = R32(l + 0x68); hb && RS16(hb) != -1; hb += 10) {
            if (RS16(hb) != 0) continue;
            ObjRect r;
            box_rect(hb, RS32(l + 8), RS32(l + 0xC), 0, 0, &r);
            if (Rect_Overlap(&r, p) == -1) return l;
        }
    }
    return 0;
}

static void blast(int32_t x, int32_t y, uint16_t delay, int16_t hurts)   /* 0x060754B2 */
{
    int s = Projectile_Spawn(0x14, x, y, 0, 0);
    if ((int16_t)s == -1) return;
    uint32_t p = PROJECTILES + (uint32_t)((int16_t)s * 0x40);
    W16(p + 6, delay);
    W16(p + 0x14, (uint16_t)hurts);
}

static int offscreen(uint32_t e)
{
    int32_t dx = RS32(e + 8) - RS32(CAM_X), dy = RS32(e + 0xC) - RS32(CAM_Y);
    return dx < -0x00800000 || dx > 0x01C00000 || dy < -0x00800000 || dy > 0x01C00000;
}

static void Proj_BombHand(uint32_t e)                       /* 0x06075368 */
{
    int16_t s = (int16_t)Enemy_FindSlot(0x16);
    if (R16(e + 4) & 1) {
        Anim_Start(e + 0x20, T16_DEFS, T16_SCR, CHAR_BASE(s), 0xC);
        AND16(e + 4, 0xFFFE);
        W16(e + 0x12, 0x7FFF);
    }
    if (!FROZEN && (offscreen(e) || (R16(e + 0x20) & 0x8000))) { W16(e, 0xFFFF); return; }
    W16(SPR + 2, (uint32_t)((RS32(e + 8) - RS32(CAM_X)) >> 16));
    W16(SPR + 4, (uint32_t)((RS32(e + 0xC) - RS32(CAM_Y)) >> 16));
    W16(SPR + 0x0A, (uint16_t)(COLOR_BANK(s) + 1));
    W16(SPR + 0x10, (R16(e + 4) & 0x8000) | 0x200);
    W16(SPR + 0x0C, 0);
    W16(SPR + 0x0E, R16(e + 0x20) & 3);
    W32(SPR + 0x18, 0xFFFFFFFFu);
    Anim_Tick(e + 0x20);
}

/* Projectile 0x13 (0x06074E88): bomb. Flies straight (angle/speed from the spawn),
 * spinning; explodes on Sho / his guard (Projectile_Collide(1, 0x1000)) or a lantern. */
void Proj_Bomb13(uint32_t e)
{
    if (R16(e + 0x10) != 0) { Proj_BombHand(e); return; }
    int16_t s = (int16_t)Enemy_FindSlot(0x16);
    if (R16(e + 4) & 1) {
        Anim_Start(e + 0x20, T16_DEFS, T16_SCR, CHAR_BASE(s), 0xB);
        AND16(e + 4, 0xFFFE);
        W16(e + 0x12, 1);
    }
    if (!FROZEN) {
        W32(e + 8, (uint32_t)(RS32(e + 8) + RS32(e + 0x18)));
        W32(e + 0xC, (uint32_t)(RS32(e + 0xC) + RS32(e + 0x1C)));
        int16_t hit = (int16_t)Projectile_Collide(1, 0x1000);
        uint32_t hb = R32(e + 0x30);
        if (hit == 0 && hb && RS16(hb) == 2) {
            ObjRect p;
            box_rect(hb, RS32(e + 8), RS32(e + 0xC), (R16(e + 4) & 0x8000) != 0, 0, &p);
            if (lantern_hit(&p, RS32(e + 8), 0x00300000)) hit = 1;
        }
        if (hit) {
            /* the original re-reads x/y from the (now free) slot before each blast; the first
             * blast takes that slot, so the other four are placed around y - 16 (found with the
             * 3-1 attract demo lock-step, docs/ground_truth.md) */
            W16(e, 0xFFFF);
            blast(RS32(e + 8), RS32(e + 0xC) - 0x00100000, 0, 1);
            blast(RS32(e + 8) - 0x00120000, RS32(e + 0xC) - 0x00120000, 8, 1);
            blast(RS32(e + 8) + 0x00120000, RS32(e + 0xC) - 0x00120000, 8, 1);
            blast(RS32(e + 8) - 0x00120000, RS32(e + 0xC) + 0x00100000, 8, 0);
            blast(RS32(e + 8) + 0x00120000, RS32(e + 0xC) + 0x00100000, 8, 0);
            sub_06007CDE(0x209);
            return;
        }
        if (offscreen(e)) { W16(e, 0xFFFF); return; }
    }
    W16(SPR + 2, (uint32_t)((RS32(e + 8) - RS32(CAM_X)) >> 16));
    W16(SPR + 4, (uint32_t)((RS32(e + 0xC) - RS32(CAM_Y)) >> 16));
    W16(SPR + 0x0A, (uint16_t)(COLOR_BANK(s) + 1));
    W16(SPR + 0x10, (R16(e + 4) & 0x8000) | 0x600);
    W16(SPR + 0x0C, 0);
    W16(SPR + 0x0E, R16(e + 0x20) & 3);
    if (RS16(e + 0x14) < 0) W16(e + 0x14, R16(e + 0x14) + 0x100);
    int16_t d = ((R16(e + 0x14) & 0xFF) < 0x41) ? -4 : 4;
    W16(SPR + 0x14, (uint16_t)(0x100 - ((R16(e + 0x14) & 0xFF) + d)));
    W32(SPR + 0x18, 0xFFFFFFFFu);
    Anim_Tick(e + 0x20);
}

/* Projectile 0x14 (0x06075524): blast (anim 10), starts after +6 frames, grows by
 * 0x600 per frame (hitbox by 0x300); hurts Sho for 1 (+0x14 != 0, once per 20 frames)
 * and breaks the lanterns it touches. */
void Proj_Blast14(uint32_t e)
{
    if (R16(e + 6) != 0) { if (!FROZEN) W16(e + 6, R16(e + 6) - 1); return; }
    int16_t s = (int16_t)Enemy_FindSlot(0x16);
    if (R16(e + 4) & 1) {
        Anim_Start(e + 0x20, T16_DEFS, T16_SCR, CHAR_BASE(s), 10);
        AND16(e + 4, 0xFFFE);
        W16(e + 0x12, 0x7FFF);
        W16(e + 0x10, 0);
        W32(e + 0x18, 0);
    }
    if (!FROZEN) {
        int32_t sc = (int32_t)((uint32_t)R16(e + 0x10) * 0x300u + 0xA000u);
        uint32_t hb = R32(e + 0x30);
        if (!(R16(PL + 2) & 8) && R32(0x060EA31Cu) != 0 && RS32(e + 0x18) == 0 && R16(R32(0x060EA41Cu)) == 0 &&
            R16(e + 0x14) != 0 && hb && RS16(hb) == 2) {
            ObjRect p;
            box_rect(hb, RS32(e + 8), RS32(e + 0xC), (R16(e + 4) & 0x8000) != 0, sc, &p);
            for (uint32_t sb = R32(0x060EA31Cu); RS16(sb) != -1; sb += 10) {
                if (RS16(sb) != 0 && RS16(sb) != 3) continue;
                ObjRect r;
                box_rect(sb, RS32(PL + 8), RS32(PL + 0xC), (R16(PL + 2) & 0x8000) != 0, 0, &r);
                if (Rect_Overlap(&r, &p) == -1) { Player_TakeDamage(1); W32(e + 0x18, 0x14); break; }
            }
            uint32_t l = lantern_hit(&p, RS32(e + 8), 0x00600000);
            if (l) OR16(l + 2, 8);
        }
        if (R16(e + 0x20) & 0x8000) W16(e, 0xFFFF);
        W16(e + 0x10, R16(e + 0x10) + 1);
        if (RS32(e + 0x18) != 0) W32(e + 0x18, R32(e + 0x18) - 1);
    }
    W16(SPR + 2, (uint32_t)((RS32(e + 8) - RS32(CAM_X)) >> 16));
    W16(SPR + 4, (uint32_t)((RS32(e + 0xC) - RS32(CAM_Y)) >> 16));
    W16(SPR + 0x10, 0x1A00);
    W16(SPR + 0x0C, 0);
    W16(SPR + 0x0E, R16(e + 0x20) & 3);
    W16(SPR + 0x0A, (uint16_t)(COLOR_BANK(s) + 3));
    W32(SPR + 0x18, 0x81);
    W32(SPR + 0x1C, (uint32_t)R16(e + 0x10) * 0x600u + 0x10000u);
    W32(SPR + 0x20, (uint32_t)R16(e + 0x10) * 0x600u + 0x10000u);
    Anim_Tick(e + 0x20);
}
