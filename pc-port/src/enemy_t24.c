/* Enemy type 0x0024 (stages 5-1, 5-2, 8-1): grenadier. Loader 0x06093B34, handler
 * 0x06093B96, init 0x06094E40; grenade = projectile 0x29 (0x06094EE4). 1 HP, 200 pts.
 * A soldier (same anims as the riflemen) that throws grenades: standing (9), crouched
 * (10) or in a jump (0xB, aimed at Sho), by the height of Sho (zone test 44 / 19 px
 * above its feet). The grenade is spawned at its hand and follows the throw animation
 * (hand offsets 0x060C1218/24, 0x060C1230/3C, 0x060C1248/56 by frame) until the release
 * frame (5, aimed throw 6): 5 px/frame plus its own speed, gravity 0x2000, spinning. It
 * bursts on Sho (1 damage), his sword or guard, a wall, the 0x103D (stage 8) / 0x1046
 * (stage 0xE) objects, or low in the pit of stage 9; the burst (anim 0x12) also hurts.
 * Dodges a shuriken by a random jump (4, Soldier_Jump). Off screen it resets (timer 3,
 * grenade dropped). +0x4C grenade slot, +0x48 next action, +0x52..+0x56 overlay. */
#include "enemy_more.h"
#include "anim.h"
#include "sprite.h"
#include "collision.h"
#include "objects.h"
#include "player_ext.h"
#include "fixmath.h"
#include "fx.h"

#define T24_DEFS 0x060C0950u
#define T24_SCR  0x060E4988u

void Enemy24_Load(void)                                     /* 0x06093B34 */
{
    Enemy_RegisterChars(0x24, 0x060E4444u);
    int16_t s = (int16_t)Enemy_FindSlot(0x24);
    uint16_t b = R16(0x060A9084u + 2u * (uint32_t)s);
    Pal_Copy((b & 0xFFF) << 4, 16, 0x060E46E8u);
    Pal_Copy((uint16_t)(b + 1) * 16, 16, 0x060E4708u);
    Pal_Copy((uint16_t)(b + 2) * 16, 16, 0x060E4728u);
}

static void Grenadier_Init(void)                            /* 0x06094E40 */
{
    uint32_t o = O;
    W16(o + 6, 0);
    Anim_Start(o + 0x58, T24_DEFS, T24_SCR, CHAR_BASE(SLOT), RS16(o + 6));
    AND16(o + 2, 0xFFFE);
    W16(o + 0x16, 1);
    W16(o + 4, 0);
    W16(o + 0x4C, 0xFFFF);
    EM_FaceSho(o);
}

#define PROJ(i) (PROJECTILES + (uint32_t)((int16_t)(i) * 0x40))

static void drop_grenade(uint32_t o)
{
    if (RS16(o + 0x4C) != -1) { W16(PROJ(RS16(o + 0x4C)), 0xFFFF); W16(o + 0x4C, 0xFFFF); }
}

static int16_t tbl(uint32_t t, uint32_t o)                  /* t[+0x5A - 1] */
{
    return RS16((uint32_t)((int32_t)t + ((int32_t)R16(o + 0x5A) - 1) * 2));
}

/* throw actions 9 / 10 / 0xB. dxT/dyT: hand offset tables; rel: release frame;
 * idle: action when there is no grenade; aimed: 0xB */
static void throw_act(uint32_t o, uint32_t dxT, uint32_t dyT, uint16_t rel, uint16_t idle, int kind)
{
    if ((R16(o + 0x58) & 3) == 1) {                            /* take a grenade */
        int32_t hy = RS32(o + 0xC) - ((int32_t)tbl(dyT, o) << 16);
        if (hy < RS32(CAM_Y) || RS32(CAM_Y) + 0x00E00000 < hy) W16(o + 0x4C, 0xFFFF);
        else W16(o + 0x4C, (uint16_t)Projectile_Spawn(0x29, 0, 0, 0, 0x50000));
        AND16(o + 0x58, 0xFFFC); OR16(o + 0x58, 2);
        if (RS16(o + 0x4C) == -1) { W16(o + 6, idle); W16(o + 0x14, 0x3C); }
        else W16(PROJ(RS16(o + 0x4C)) + 0x10, 0);
        return;
    }
    if ((R16(o + 0x58) & 3) != 2 || RS16(o + 0x4C) == -1) return;
    uint32_t p = PROJ(RS16(o + 0x4C));
    if (R16(p) != 0x29) { W16(o + 0x4C, 0xFFFF); W16(o + 6, idle); W16(o + 0x14, 0x3C); return; }
    if (R16(p + 0x10) & 1) { W16(o + 0x4C, 0xFFFF); AND16(o + 0x58, 0xFFFC); return; }   /* burst in the hand */
    int32_t dx = tbl(dxT, o), dy = tbl(dyT, o);
    W16(p + 4, (R16(p + 4) & 0x7FFF) | (R16(o + 2) & 0x8000));
    int right = !(R16(o + 2) & 0x8000);
    W32(p + 8, (uint32_t)(RS32(o + 8) + (right ? -dx : dx) * 0x10000));
    W32(p + 0xC, (uint32_t)(RS32(o + 0xC) - dy * 0x10000));
    if (R16(o + 0x5A) != rel) { W32(p + 0x18, 0); W32(p + 0x1C, 0); return; }
    int32_t own = RS32(o + 8) - RS32(o + 0x28);
    if (kind == 0xB) {                                          /* aimed */
        int16_t ang = (int16_t)Atan8((int16_t)(R32(p + 8) >> 16), (int16_t)(R32(p + 0xC) >> 16),
                                     (int16_t)(R32(PL + 8) >> 16),
                                     (int16_t)((uint32_t)(RS32(PL + 0xC) - (right ? 0x00200000 : 0x00400000)) >> 16));
        W32(p + 0x18, (uint32_t)FixMul(Cos8(ang), 0x50000));
        W32(p + 0x1C, (uint32_t)FixMul(Sin8(ang), 0x50000));
    } else {
        int32_t add = (kind == 9 && STAGE == 8) ? own : 0;
        W32(p + 0x18, (uint32_t)(add + (right ? 0x50000 : -0x50000)));
        W32(p + 0x1C, 0);
    }
    W32(p + 0x18, (uint32_t)(RS32(p + 0x18) + own));
    W16(o + 0x4C, 0xFFFF);
    AND16(o + 0x58, 0xFFFC);
    sub_06007CDE(0x125);
}

void EnemyHandler_0024(void)                                /* 0x06093B96 */
{
    uint32_t o = O;
    if (R16(o + 2) & 1) Grenadier_Init();
    if (EM_Far(o, 0x00A00000) && !(R16(o + 2) & 0x4000) && !(R16(o + 2) & 8)) {
        W16(o + 6, 0);
        drop_grenade(o);
        W16(o + 0x14, 3);
        Enemy_AnimSwitch();
        return;
    }
    uint16_t ovl = 0;
    if (!FROZEN) {
        if ((int16_t)Enemy_AttackPlayer(1, 0, 0xC000) != 0) EM_PlayerFaceBack(o);
        int16_t r = (int16_t)Enemy_CheckPlayerHits(0xC000);
        uint16_t a = R16(o + 6);
        if ((R16(o + 2) & 8) && a != 0xD && a != 0xE && a != 0xF) {
            drop_grenade(o);
            int16_t k = (r == 2) ? (int16_t)Player_AttackKind() : 4;
            if (RS16(o + 0x16) < 1) {
                if (k == 4 || k == 5) W16(o + 6, 0xE);
                else { W16(o + 6, 0xF); EM_OverlayStart(T24_DEFS, T24_SCR, 0x10, o + 0x52, o + 0x54, o + 0x56); }
            } else W16(o + 6, 0xD);
            sub_06007CDE(0x205);
            Enemy_AnimSwitch();
        }
        if (!(R16(o + 0x58) & 0x8000)) {
            a = R16(o + 6);
            if (a == 0 || a == 2) {
                EM_FaceSho(o);
                if (R16(o + 2) & 0x4000) { W16(o + 6, 5); Enemy_AnimSwitch(); goto physics; }
                int32_t d = (R16(o + 2) & 0x8000) ? (int32_t)0xFF800000 : 0x00800000;
                if (!Projectile_InBox(RS32(o + 8) + d, RS32(o + 0xC) + (int32_t)0xFFC00000, RS32(o + 8), RS32(o + 0xC)))
                    AND16(o + 4, 0x7FFF);
                else if (!(R16(o + 4) & 0x8000)) {
                    OR16(o + 4, 0x8000);
                    if (!(sub_0602DDD2() & 1)) { W16(o + 6, 4); goto physics; }
                }
                if (EM_Abs(RS32(PL + 8) - RS32(o + 8)) < 0x01000000) {
                    W16(o + 0x14, R16(o + 0x14) - 1);
                    if (RS16(o + 0x14) < 1) {
                        int32_t y = RS32(o + 0xC), sy = RS32(PL + 0xC), h = RS16(PL + 0x20) * -0x80000;
                        if (y - 0x2C0000 < sy && sy + h < y - 0x2C0000) {
                            if (!(R16(o + 2) & 0x0800)) W16(o + 6, 9);
                            else { W16(o + 6, 3); W16(o + 0x48, 9); }
                        } else if (y - 0x130000 < sy && sy + h < y - 0x130000) {
                            if (!(R16(o + 2) & 0x0800)) { W16(o + 6, 1); W16(o + 0x48, 10); }
                            else W16(o + 6, 10);
                        } else W16(o + 6, 4);
                    }
                }
            } else if (a == 4) Soldier_Jump(0, 0xB);
            else if (a == 5) {
                if (!(R16(o + 2) & 0x4000)) { AND16(o + 2, 0xF7FF); W16(o + 6, 0); W16(o + 0x14, 0x3C); }
            } else if (a == 9) throw_act(o, 0x060C1218u, 0x060C1224u, 5, 0, 9);
            else if (a == 10) throw_act(o, 0x060C1230u, 0x060C123Cu, 5, 2, 10);
            else if (a == 0xB) {
                if (!(R16(o + 2) & 0x4000)) { drop_grenade(o); W16(o + 6, 0xC); }
                else throw_act(o, 0x060C1248u, 0x060C1256u, 6, 0, 0xB);
            } else if (a == 0xE || a == 0xF) {
                if (R16(o + 2) & 0x4000) { W16(o + 0x5A, 0); W16(o + 0x72, 0); }
            }
        } else {
            switch (RS16(o + 6)) {
            case 1: OR16(o + 2, 0x0800); W16(o + 6, R16(o + 0x48)); break;
            case 3: AND16(o + 2, 0xF7FF); W16(o + 6, R16(o + 0x48)); break;
            case 0xD:
                if (!(R16(o + 2) & 0x4000)) {
                    W16(o + 0x14, 3);
                    W16(o + 6, (R16(o + 2) & 0x0800) ? 2 : 0);
                    AND16(o + 2, 0xFFF7);
                } else { AND16(o + 0x58, 0x7FFF); W16(o + 0x5A, 0); W16(o + 0x72, 0); }
                break;
            case 0xE:
                Enemy_DeathBurst(o); AND16(o + 2, 0xFFF7); Score_Add(200); drop_grenade(o); W16(o, 0xFFFF);
                return;
            default:
                W16(o + 6, (R16(o + 2) & 0x0800) ? 2 : 0);
                W16(o + 0x14, 0x3C);
                break;
            }
        }
physics:
        Obj_Integrate(o);
        Obj_CollideMap(o, 3, 8);
        Enemy_AnimSwitch();
    }
    if ((R16(o + 0x58) & 3) == 1 && R16(o + 6) == 0xF) {
        int done = EM_OverlayDraw(T24_DEFS, T24_SCR, 0x10, o + 0x52, o + 0x54, o + 0x56);
        ovl = R16(SCRATCH_ANIM);
        if (done) { Enemy_DeathBurst(o); Score_Add(200); drop_grenade(o); W16(o, 0xFFFF); return; }
    }
    EM_SprBasic(o, COLOR_BANK(SLOT));
    if (R16(o + 6) == 0xE) W16(SPR + 0x0E, R16(o + 0x58) & 3);
    else if (R16(o + 6) == 0xF && (R16(o + 0x58) & 3) == 1) W16(SPR + 0x0E, ovl & 3);
    Anim_Tick(o + 0x58);
}

static uint32_t layer_desc(uint16_t l) { return (((uint32_t)(l & 0xFF) * 0x30u) & 0xFFu) + LAYER_DESC; }

static int touch_type(uint32_t e, uint16_t type)            /* 0x060585F8 / 0x0605E050 */
{
    for (int16_t s = 0; s < 0xC0; s++) {
        uint32_t o = OBJ_TABLE + (uint32_t)(int16_t)(s * 0x78);
        if (R16(o) == type && EM_ProjHitbox(o, 0, e, 2) != 0xFFFF) return 1;
    }
    return 0;
}

static void hurt_sho(void)
{
    Obj_Spawn(2, RS32(HIT_POINT), RS32(HIT_POINT + 4), 0, 0);
    Player_KnockDir(HIT_POINT);
    Player_TakeDamage(1);
}

/* Projectile 0x29 (0x06094EE4): grenade (anim 0x11), +0x10 bit 0 = burst (anim 0x12). */
void Proj_Grenade29(uint32_t e)
{
    int16_t s = (int16_t)Enemy_FindSlot(0x24);
    uint32_t who;
    if (R16(e + 4) & 1) {
        Anim_Start(e + 0x20, T24_DEFS, T24_SCR, CHAR_BASE(s), 0x11);
        AND16(e + 4, 0xFFFE);
        W32(e + 0x18, 0); W32(e + 0x1C, 0);
        W16(e + 0x14, 0);
    }
    if (!FROZEN) {
        if (!(R16(e + 0x10) & 1)) {
            W32(e + 8, (uint32_t)(RS32(e + 8) + RS32(e + 0x18)));
            W32(e + 0xC, (uint32_t)(RS32(e + 0xC) - RS32(e + 0x1C)));
            if (RS32(e + 0x18) != 0 || RS32(e + 0x1C) != 0) {
                W32(e + 0x1C, (uint32_t)(RS32(e + 0x1C) - 0x2000));
                W16(e + 0x14, (uint16_t)(RS16(e + 0x14) + ((R16(e + 4) & 0x8000) ? -2 : 2)));
            }
            int hit = 0;
            if (EM_ProjTouch(e, 2, &who, 2) != 0xFFFF) hit = 1;     /* sword */
            if (EM_ProjTouch(e, 2, &who, 1) != 0xFFFF) hit = 1;     /* guard */
            if (EM_ProjTouch(e, 2, &who, 0) != 0xFFFF) { hurt_sho(); hit = 1; }
            if (STAGE == 9 && RS32(e + 8) < 0x06600000 && RS32(e + 8) > 0x05600000 && RS32(e + 0xC) > RS32(CAM_Y) + 0x00B80000) hit = 1;
            if (STAGE == 8 && touch_type(e, 0x103D)) hit = 1;
            if (STAGE == 0xE && touch_type(e, 0x1046)) hit = 1;
            if ((RS32(e + 0x18) != 0 || RS32(e + 0x1C) != 0) &&
                (Coll_AttrFlags(Map_GetAttr(RS32(e + 8), RS32(e + 0xC), layer_desc(R16(G_COLL_LAYER)))) & 0xF)) hit = 1;
            if (hit) {
                sub_06007CDE(0x121);
                OR16(e + 0x10, 1);
                Obj_Spawn(0, RS32(e + 8), RS32(e + 0xC), 0, 0);
                Obj_Spawn(0, RS32(e + 8) + 0x50000, RS32(e + 0xC) - 0x50000, 0, 5);
                Obj_Spawn(0, RS32(e + 8) - 0x70000, RS32(e + 0xC) - 0x30000, 0, 8);
                Anim_Start(e + 0x20, T24_DEFS, T24_SCR, CHAR_BASE(s), 0x12);
                OR16(e + 0x20, 0x4000);
            }
            if (RS32(e + 8) != 0 || RS32(e + 0xC) != 0) {
                int32_t dx = RS32(e + 8) - RS32(CAM_X), dy = RS32(e + 0xC) - RS32(CAM_Y);
                if (dx < -0x00800000 || dx > 0x01C00000 || dy < -0x00800000 || dy > 0x01C00000) { W16(e, 0xFFFF); return; }
            }
        } else {
            if (EM_ProjTouch(e, 0, &who, 0) != 0xFFFF) hurt_sho();
            if (R16(e + 0x20) & 0x8000) { W16(e, 0xFFFF); return; }
        }
    }
    W16(SPR + 2, (uint32_t)((RS32(e + 8) - RS32(CAM_X)) >> 16));
    W16(SPR + 4, (uint32_t)((RS32(e + 0xC) - RS32(CAM_Y)) >> 16));
    W16(SPR + 0x0A, (uint16_t)(COLOR_BANK(s) + 2));
    W16(SPR + 0x10, (R16(e + 4) & 0x8000) | 0x600);
    W16(SPR + 0x14, R16(e + 0x14));
    W32(SPR + 0x18, 2);
    W16(SPR + 0x0C, 0);
    W16(SPR + 0x0E, 0);
    Anim_Tick(e + 0x20);
}
