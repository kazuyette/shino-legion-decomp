/* Projectiles and visual effects, ported from SHINOBI.PPB / A.BIN. */
#include "fx.h"
#include "enemy.h"
#include "objects.h"
#include "objhandlers.h"
#include "ram.h"
#include "anim.h"
#include "sprite.h"
#include "collision.h"
#include "fixmath.h"
#include "player_ext.h"
#include "render.h"
#include <stdio.h>

#define PL     0x060EA2B4u
#define CAM_X  0x060252A0u
#define CAM_Y  0x060252A4u
#define FROZEN (R16(0x0602529Eu) & 0x8000)
#define SPR    0x060250ACu

static int32_t iabs(int32_t v) { return v < 0 ? -v : v; }
static void once(const char *what, unsigned id)
{
    static unsigned seen[64]; static int n;
    for (int i = 0; i < n; i++) if (seen[i] == id) return;
    if (n < 64) seen[n++] = id;
    printf("[fx] %s %04X not ported\n", what, id);
}

/* ------------------------------------------------------------ common chars */

static void Pal_Copy(int dst, int n, uint32_t src)                     /* 0x06005C30 */
{
    for (int i = 0; i < n; i++) W16(SHADOW_CRAM + 2u * (uint32_t)((dst + i) & 0xFFFF), R16(src + 2u * (uint32_t)i));
    W16(0x060250A8u, 1);
}

/* Shurikens, sparks, explosions...: chars 0x4B0+ at VRAM offset 0x72000 (table
 * 0x060C9794), palettes CRAM 0x700 (256) and 0x380 (128). */
void Common_LoadChars(void)
{
    uint32_t t = 0x060C9794u, n = R16(t + 2), off = 0x72000;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t r = t + 4 + i * 8;
        Char_Register((int)(0x4B0 + i), Ram_Ptr(r + 6)[0] * 8, Ram_Ptr(r + 7)[0], RS16(r + 4), off);
        off += (uint32_t)R16(r + 2) * 32;
    }
    Pal_Copy(0x700, 0x100, 0x060C9494u);
    Pal_Copy(0x380, 0x80, 0x060C9694u);
}

/* ----------------------------------------------------------------- effects */

void Effects_Clear(void)
{
    for (uint32_t i = 0; i < 64; i++) {
        uint32_t e = EFFECTS + i * 0x30;
        W16(e, 0xFFFF); W16(e + 2, 0); W16(e + 4, 0); W16(e + 6, 0);
        Anim_Reset(e + 0x10);
    }
}

int Obj_Spawn(int type, int32_t x, int32_t y, int flags, int param)
{
    int s;
    for (s = 0; s < 64; s++) if (R16(EFFECTS + (uint32_t)s * 0x30) == 0xFFFF) break;
    if (s == 64) return -1;
    uint32_t e = EFFECTS + (uint32_t)(int16_t)(s * 0x30);
    W16(e, (uint32_t)type);
    W32(e + 8, (uint32_t)x);
    W32(e + 0xC, (uint32_t)y);
    W16(e + 2, (uint32_t)(flags | 1));
    W16(e + 4, (uint32_t)param);
    W16(e + 6, 0);
    Anim_Reset(e + 0x10);
    return s;
}

static void fx_screen(uint32_t e)
{
    W16(SPR + 2, (uint32_t)((RS32(e + 8) - RS32(CAM_X)) >> 16));
    W16(SPR + 4, (uint32_t)((RS32(e + 0xC) - RS32(CAM_Y)) >> 16));
}
static void fx_start(uint32_t e, uint32_t defs, int anim)
{
    if (!(R16(e + 2) & 1)) return;
    Anim_Start(e + 0x10, defs, 0x060D2FE8u, 0x4B0, anim);
    AND16(e + 2, 0xFFFE);
}
static void fx_tick(uint32_t e)
{
    Anim_Tick(e + 0x10);
    if (R16(e + 0x10) & 0x8000) W16(e, 0xFFFF);
}

static void Fx_Explosion(uint32_t e)                                   /* 0x06062224: 0, 1, 6, 0xF */
{
    fx_start(e, 0x060ACCFEu, 0);
    fx_screen(e);
    W16(SPR + 0x10, 0);
    uint16_t t = R16(e);
    W16(SPR + 0x0A, t == 1 ? 0x72 : t == 6 ? 0x39 : t == 0xF ? 0x3A : 0x71);
    W16(SPR + 0x0C, 0);
    W16(SPR + 0x0E, R16(e + 0x10) & 3);
    fx_tick(e);
}

static void Fx_Spark(uint32_t e, int anim, int bank)                   /* 0x06062314 (2) / 0x060623E4 (3) */
{
    fx_start(e, 0x060ACB20u, anim);
    fx_screen(e);
    W16(SPR + 0x10, (R16(e + 2) & 0x8000) | 0x0200);
    W32(SPR + 0x18, 0x81);
    W16(SPR + 0x0A, (uint32_t)bank);
    W16(SPR + 0x0C, 0);
    W16(SPR + 0x0E, 0);
    fx_tick(e);
}

static void Fx_Slash(uint32_t e)                                       /* 0x060624B4: 4 */
{
    fx_start(e, 0x060ACB20u, 4);
    fx_screen(e);
    W16(SPR + 0x10, R16(e + 2) & 0x8000);
    W16(SPR + 0x0A, 0x70);
    W16(SPR + 0x0C, 0);
    W16(SPR + 0x0E, R16(e + 0x10) & 3);
    fx_tick(e);
}

void Effects_Update(void)
{
    for (uint32_t i = 0; i < 64; i++) {
        uint32_t e = EFFECTS + (uint32_t)(int16_t)(i * 0x30);
        W32(FX_CUR, e);
        uint16_t t = R16(e);
        if (t == 0xFFFF) continue;
        if (R16(e + 4) != 0) {                                         /* delayed start */
            if (!FROZEN) W16(e + 4, R16(e + 4) - 1);
            continue;
        }
        switch (t) {
        case 0: case 1: case 6: case 0xF: Fx_Explosion(e); break;
        case 2: Fx_Spark(e, 3, 0x73); break;
        case 3: Fx_Spark(e, 2, 0x74); break;
        case 4: Fx_Slash(e); break;
        default: once("effect type", t); W16(e, 0xFFFF); break;
        }
    }
}

/* -------------------------------------------------------------- projectiles */

static uint32_t proj_addr(int s) { return PROJECTILES + (uint32_t)(int16_t)s * 0x40; }

static void Proj_Init(int s, uint16_t type)                            /* 0x06060948 */
{
    uint32_t e = proj_addr(s);
    W16(e, type);
    W16(e + 2, 0); W16(e + 4, 0); W16(e + 0x10, 0); W16(e + 0x12, 0); W16(e + 6, 0);
    W32(e + 8, 0); W32(e + 0xC, 0); W32(e + 0x18, 0); W32(e + 0x1C, 0);
    W16(e + 0x14, 0);
    Anim_Reset(e + 0x20);
}

void Projectiles_Clear(void)
{
    for (int i = 0; i < 64; i++) Proj_Init(i, 0xFFFF);
}

int Projectile_Spawn(int16_t type, int32_t x, int32_t y, int16_t angle, int32_t speed)
{
    int s;
    for (s = 0; s < 64; s++) if (R16(proj_addr(s)) == 0xFFFF) break;    /* 0x06060A50 */
    if (s == 64) return -1;
    Proj_Init(s, (uint16_t)type);
    uint32_t e = proj_addr(s);
    W32(e + 8, (uint32_t)x);
    W32(e + 0xC, (uint32_t)y);
    W32(e + 0x18, (uint32_t)FixMul(Cos8(angle), speed));
    W32(e + 0x1C, (uint32_t)FixMul(Sin8(angle), speed));
    W16(e + 0x14, (uint32_t)angle);
    W16(e + 4, 0x2001);
    W16(e + 6, 0);
    if (type == 0 || type == 4) AND16(e + 4, 0xDFFF);                  /* Sho's: not hostile */
    Anim_Reset(e + 0x20);
    return s;
}

int Projectile_InBox(int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    if (x1 < x0) { int32_t t = x0; x0 = x1; x1 = t; }
    if (y1 < y0) { int32_t t = y0; y0 = y1; y1 = t; }
    for (uint32_t i = 0; i < 64; i++) {
        uint32_t e = PROJECTILES + i * 0x40;
        if ((R16(e + 4) & 0x2000) || (R16(e + 4) & 0x4000) || R16(e) == 0xFFFF) continue;
        if (RS32(e + 8) <= x0 || x1 <= RS32(e + 8)) continue;
        if (RS32(e + 0xC) <= y0 || y1 <= RS32(e + 0xC)) continue;
        return 1;
    }
    return 0;
}

/* 0x06060E8E: object hitboxes (kind ko) vs projectile hitboxes (kind kp). */
static uint16_t Proj_Hitbox(uint32_t o, int16_t ko, uint32_t p, int16_t kp)
{
    if (iabs(RS32(o + 8) - RS32(p + 8)) > 0x02000000) return 0xFFFF;
    if (iabs(RS32(o + 0xC) - RS32(p + 0xC)) > 0x02000000) return 0xFFFF;
    /* reuse Hitbox_Test's list walker through a temporary "object" view */
    static const uint32_t TMP = 0x060FFE00u;                            /* scratch: x, y, flags, list */
    W32(TMP + 8, R32(p + 8)); W32(TMP + 0xC, R32(p + 0xC));
    W16(TMP + 2, R16(p + 4) & 0x8000); W32(TMP + 0x68, R32(p + 0x30));
    return Hitbox_Test(o, ko, TMP, kp);
}

/* 0x06061062: what the current projectile touches. Bits: 4 weapon, 8 weak spot,
 * 2 guard (only if mode & 0x1000), 1 body. */
static int Proj_FindTarget(uint32_t e, uint32_t *t, uint16_t mode)
{
    int m = 0;
    if (R16(e + 4) & 0x4000) return 0;
    if (!(R16(e + 4) & 0x2000)) {                                      /* Sho's: hits stage objects */
        int32_t ylim = 0x00700000;
        uint32_t o = OBJ_TABLE;
        for (int n = 0xC0; n > 0; n--, o += 0x78) {
            if (R16(o) == 0xFFFF || (R16(o + 0x1C) & 0x8000) || (R16(o + 2) & 8)) continue;
            if (R16(o) == 0x17 || R16(o) == 0x1046) ylim = 0x00F00000;
            if (iabs(RS32(o + 8) - RS32(e + 8)) >= 0x00A00000) continue;
            if (iabs(RS32(o + 0xC) - RS32(e + 0xC)) >= ylim) continue;
            if (!m && Proj_Hitbox(o, 2, e, 2) != 0xFFFF) m = 4;
            if (!m && Proj_Hitbox(o, 3, e, 2) != 0xFFFF) m = 8;
            if ((mode & 0x1000) && !m && Proj_Hitbox(o, 1, e, 2) != 0xFFFF) m = 2;
            if (!m && Proj_Hitbox(o, 0, e, 2) != 0xFFFF) m = 1;
            if (m) { *t = o; return m; }
        }
        return 0;
    }
    if (!(R16(0x060EA2D0u) & 0x8000)) {                                /* hostile: hits Sho */
        if (Proj_Hitbox(PL, 2, e, 2) != 0xFFFF) m = 4;
        if ((mode & 0x1000) && !m && Proj_Hitbox(PL, 1, e, 2) != 0xFFFF) m = 2;
        if (!(R16(PL + 2) & 8) && R16(R32(0x060EA41Cu)) == 0 && !(R16(PL + 4) & 0x0410)) {
            if (!m && Proj_Hitbox(PL, 3, e, 2) != 0xFFFF) m = 8;
            if (!m && Proj_Hitbox(PL, 0, e, 2) != 0xFFFF) m = 1;
        }
    }
    if (m) *t = PL;
    return m;
}

static void Proj_Bounce(uint32_t e)                                    /* 0x0606137C */
{
    W16(e + 6, 0);
    OR16(e + 4, 0x4000);
    int32_t v = RS32(e + 0x18); W32(e + 0x18, (uint32_t)-((v + (v < 0)) >> 1));
    v = RS32(e + 0x1C);         W32(e + 0x1C, (uint32_t)-((v + (v < 0)) >> 1));
}

/* mode: 0x8000 pierce, 0x4000 ignore walls, 0x2000 can be reflected by a sword,
 * 0x1000 can be guarded, 0x0800 bounce instead of vanishing, 0x0400/0x0200 player
 * damage variants, 0x0100 not stopped by weapons. Returns 1 wall, 2 weapon, 3 guard, 4 hit. */
int Projectile_Collide(int16_t dmg, uint16_t mode)
{
    uint32_t e = R32(PROJ_CUR);
    int r = 0;
    if (!(mode & 0x4000) && !(R16(e + 4) & 0x4000) && RS16(0x060EA64Au) >= 0) {
        uint16_t a = Map_GetAttr(RS32(e + 8), RS32(e + 0xC),
                                 0x060EA588u + (((uint32_t)Ram_Ptr(0x060EA64Au)[1] * 0x30) & 0xFF));
        if ((Coll_AttrFlags(a) & 0xF) == 0xF) goto wall;
    }
    for (;;) {
        uint32_t t = 0;
        int m = Proj_FindTarget(e, &t, mode);
        if (!m || !t) return r;
        if ((m & 4) && !(mode & 0x0100)) {                             /* met a weapon */
            r = 2;
            sub_06007CDE(0x105);
            if ((mode & 0x2000) && (R16(t + 2) & 4)) {                 /* reflected */
                W16(e + 4, R16(e + 4) ^ 0xA000);
                W32(e + 0x18, (uint32_t)FixMul(RS32(e + 0x18), (int32_t)0xFFFF0000));
                W32(e + 0x1C, (uint32_t)FixMul(RS32(e + 0x1C), (int32_t)0xFFFF0000));
                W16(e + 0x14, (R16(e + 0x14) + 0x80) & 0xFF);
                OR16(e + 4, 0x1000);
                Obj_Spawn(3, RS32(HIT_POINT), RS32(HIT_POINT + 4), R16(t + 2) & 0x8000, 0);
                return 2;
            }
            W16(e + 0x12, R16(e + 0x12) - 1);
            if (RS16(e + 0x12) < 0) {
                W16(e, 0xFFFF);
                Obj_Spawn(3, RS32(HIT_POINT), RS32(HIT_POINT + 4), R16(t + 2) & 0x8000, 0);
                return 2;
            }
        }
        if (m & 8) goto hit;
        if ((mode & 0x1000) && (m & 2)) goto guard;
        if (!(m & 1)) return r;
        if (!(R16(t + 0x1C) & 0x4000)) {
            if ((R16(t + 0x12) & 0x8000) != 0x8000) goto hit;
            goto guard;
        }
        if (mode & 0x4000) return r;
    wall:
        r = 1;
        if (!(mode & 0x0800)) W16(e, 0xFFFF); else Proj_Bounce(e);
        continue;
    hit:
        Obj_Spawn(2, RS32(HIT_POINT), RS32(HIT_POINT + 4), 0, 0);
        if (t == PL) {
            Player_TakeDamage(dmg);
            if (mode & 0x0200) OR16(0x060EA2B8u, 0x40);                /* 0x06045D0A */
            if (!(mode & 0x0400)) { Player_KnockDir(HIT_POINT); AND16(t + 4, 0xBFFF); }
            else {
                if (!(R16(t + 4) & 0x4000)) Player_KnockDir(HIT_POINT);
                OR16(t + 4, 0x4000);
                AND16(t + 2, 0xFFF7);
            }
        } else {
            OR16(t + 2, 8);
            W16(t + 0x16, (uint32_t)(RS16(t + 0x16) - dmg));
            sub_06007CDE(0x104);
        }
        if (!(mode & 0x8000)) W16(e, 0xFFFF);
        return 4;
    guard:
        Obj_Spawn(3, RS32(HIT_POINT), RS32(HIT_POINT + 4), R16(t + 2) & 0x8000, 0);
        sub_06007CDE(0x105);
        if (t == PL) OR16(0x060EA2B8u, 0x1000);
        if (mode & 0x0800) { Proj_Bounce(e); return 3; }
        W16(e, 0xFFFF);
        return 3;
    }
}

static int proj_offscreen(uint32_t e)
{
    int32_t sx = RS32(e + 8) - RS32(CAM_X), sy = RS32(e + 0xC) - RS32(CAM_Y);
    return sx < (int32_t)0xFF800000 || sx > 0x01C00000 || sy < (int32_t)0xFF800000 || sy > 0x01C00000;
}

static void proj_move_and_age(uint32_t e, int16_t dmg)
{
    W32(e + 8, (uint32_t)(RS32(e + 8) + RS32(e + 0x18)));
    W32(e + 0xC, (uint32_t)(RS32(e + 0xC) - RS32(e + 0x1C)));
    Projectile_Collide(dmg, 0x3A00);
    if (proj_offscreen(e)) W16(e, 0xFFFF);
    if (R16(e + 4) & 0x1000) W16(e + 0x14, (R16(e + 0x14) + 32) & 0xFF);   /* deflected: spin */
    if (R16(e + 4) & 0x4000) {                                          /* bouncing away */
        W16(e + 6, R16(e + 6) + 1);
        if (R16(e + 6) >= 16) W16(e, 0xFFFF);
    }
}

static void Proj_ShoShuriken(uint32_t e)                               /* 0x060613BA: type 0 */
{
    if (R16(e + 4) & 1) {
        Anim_Start(e + 0x20, 0x060ACB20u, 0x060D2EE8u, 0x4B0, 0);
        AND16(e + 4, 0xFFFE);
    }
    if (!FROZEN) proj_move_and_age(e, R16(e) == 4 ? 2 : 1);
    OR16(e + 0x20, 0x4000);
    Anim_Tick(e + 0x20);
    W16(SPR + 0x0A, 0x70);
    W16(SPR + 0x10, 0x0400);
    W16(SPR + 0x0C, 0);
    W16(SPR + 0x14, R16(e + 0x14));
    if (R16(e + 4) & 0x4000) W16(SPR + 0x14, (R16(e + 0x14) + R16(e + 6) * 16) & 0xFF);
    if (R16(e) == 4 && !(R16(e + 4) & 0x4000) && (R16(0x060143EAu) & 1)) {      /* trail */
        int32_t sx = RS32(e + 8) - RS32(CAM_X), sy = RS32(e + 0xC) - RS32(CAM_Y);
        for (int k = 4; k > 0; k--) {
            W16(SPR + 2, (uint32_t)((sx - k * RS32(e + 0x18)) >> 16));
            W16(SPR + 4, (uint32_t)((k * RS32(e + 0x1C) + sy) >> 16));
            W16(SPR + 0x0E, (uint32_t)(k - 1));
            Sprite_Draw(SPR);
        }
    }
    W16(SPR + 0x0E, R16(e + 6) >> 2);
    W16(SPR + 2, (uint32_t)((RS32(e + 8) - RS32(CAM_X)) >> 16));
    W16(SPR + 4, (uint32_t)((RS32(e + 0xC) - RS32(CAM_Y)) >> 16));
    if (R16(e) != 0xFFFF) Sprite_Draw(SPR);
}

static void Proj_EnemyShuriken(uint32_t e)                             /* 0x06061854: type 1 */
{
    if (R16(e + 4) & 1) {
        Anim_Start(e + 0x20, 0x060ACB20u, 0x060D2EE8u, 0x4B0, 1);
        AND16(e + 4, 0xFFFE);
    }
    if (!FROZEN) {
        W32(e + 8, (uint32_t)(RS32(e + 8) + RS32(e + 0x18)));
        W32(e + 0xC, (uint32_t)(RS32(e + 0xC) - RS32(e + 0x1C)));
        Projectile_Collide(1, 0x3A00);
        if (proj_offscreen(e)) W16(e, 0xFFFF);
        if (R16(e + 4) & 0x1000) W16(e + 0x14, (R16(e + 0x14) + 32) & 0xFF);
        if (R16(e + 4) & 0x4000) {
            W16(e + 6, R16(e + 6) + 1);
            if (R16(e + 6) >= 16) W16(e, 0xFFFF);
        }
    }
    W16(SPR + 2, (uint32_t)((RS32(e + 8) - RS32(CAM_X)) >> 16));
    W16(SPR + 4, (uint32_t)((RS32(e + 0xC) - RS32(CAM_Y)) >> 16));
    W16(SPR + 0x0A, 0x70);
    W16(SPR + 0x10, 0x0400);
    W16(SPR + 0x0C, 0);
    W16(SPR + 0x0E, R16(e + 6) >> 2);
    W16(SPR + 0x14, R16(e + 0x14));
    if (R16(e + 4) & 0x4000) W16(SPR + 0x14, (R16(e + 0x14) + R16(e + 6) * 8) & 0xFF);
    if (R16(e) != 0xFFFF) Anim_Tick(e + 0x20);
}

void Projectiles_Update(void)
{
    for (int i = 0; i < 64; i++) {
        uint32_t e = proj_addr(i);
        W32(PROJ_CUR, e);
        uint16_t t = R16(e);
        if (t == 0xFFFF) continue;
        if (t == 0) Proj_ShoShuriken(e);
        else if (t == 1) Proj_EnemyShuriken(e);
        else { once("projectile type", t); W16(e, 0xFFFF); }
    }
}

/* -------------------------------------------------------------------- atan */

static int atan_lookup(int32_t v)                                      /* 0x0600628A */
{
    const uint32_t T = 0x0600EEFCu;
    int16_t i = 4;
    if (v < RS32(T + 16)) {
        for (;;) {
            if (!(v < RS32(T + 4u * (uint32_t)(i - 1)))) {
                int32_t a = iabs(RS32(T + 4u * (uint32_t)(i - 1)) - v), b = iabs(RS32(T + 4u * (uint32_t)i) - v);
                return (a >= b ? i : i - 1) * 4;
            }
            if (--i == 0) return 0;
        }
    }
    for (;;) {
        if (RS32(T + 4u * (uint32_t)(i + 1)) >= v) {
            int32_t a = iabs(RS32(T + 4u * (uint32_t)(i + 1)) - v), b = iabs(RS32(T + 4u * (uint32_t)i) - v);
            return (a >= b ? i : i + 1) * 4;
        }
        if (++i > 8) return 32;
    }
}

int Atan8(int16_t x1, int16_t y1, int16_t x2, int16_t y2)
{
    int left = x1 > x2, below = !(y1 > y2);
    uint16_t dx = (uint16_t)(left ? x1 - x2 : x2 - x1);
    uint16_t dy = (uint16_t)(y1 > y2 ? y1 - y2 : y2 - y1);
    int steep = dx < dy;
    uint16_t lo = steep ? dx : dy, hi = steep ? dy : dx;
    int32_t ratio = hi ? (int32_t)((double)((uint32_t)lo << 16) / (double)hi) : 65535;   /* soft-float in the original */
    int16_t a = (int16_t)atan_lookup(ratio);
    if (steep) a = (int16_t)(0x40 - a);
    if (left) a = below ? (int16_t)(a + 0x80) : (int16_t)(0x80 - a);
    else if (below) a = (int16_t)(0x100 - a);
    return a;
}

/* 0x06042D80: Sho throws a shuriken (offset dx/dy from his feet). */
void sub_06042D80(int32_t dx, int32_t dy, int angle, int32_t speed)
{
    uint16_t mf = R16(0x060252BAu);
    int16_t type = ((mf & 0x4000) && (mf & 0x00F0) == 0x40) ? 4 : 0;   /* Ikazuchi: powered */
    int s = Projectile_Spawn(type, RS32(PL + 8) + dx, RS32(PL + 0xC) + dy, (int16_t)angle, speed);
    if (RS16(0x0602526Au) == 8 && (R16(PL + 0x10) & 0x2000))
        W32(proj_addr(s) + 0x18, (uint32_t)(RS32(proj_addr(s) + 0x18) + RS32(PL + 0x38)));
    W32(0x060252D4u, 0);
}

/* ------------------------------------------------------------------- items */

int32_t sub_0602DDD2(void)
{
    int32_t v = RS32(0x0603A4B0u) * 5 + 0x3573;
    W32(0x0603A4B0u, (uint32_t)v);
    return v;
}

void Items_Clear(void)
{
    for (uint32_t i = 0; i < 64; i++) W16(ITEMS + i * 0x30, 0xFFFF);
}

int Item_Drop(int16_t kind, int32_t x, int32_t y)
{
    if (R16(0x060252BEu) != 0 && kind == 5)                             /* already have magic */
        kind = RS16(0x060AD2F4u + 2u * ((uint32_t)sub_0602DDD2() & 3));
    int s;
    for (s = 0; s < 64; s++) if (R16(ITEMS + (uint32_t)s * 0x30) == 0xFFFF) break;   /* 0x060629D4 */
    if (s == 64) return -1;
    uint32_t it = ITEMS + (uint32_t)(int16_t)(s * 0x30);
    W16(it, (uint32_t)kind);
    W32(it + 4, (uint32_t)x);
    W32(it + 8, (uint32_t)y);
    W16(it + 2, 0);
    W16(it + 0x2C, 1);
    Anim_Reset(it + 0xC);
    return s;
}

static int16_t add_clamped(uint32_t a, int16_t n, int16_t max)       /* 0x06062AA4 style */
{
    int16_t old = RS16(a);
    W16(a, (uint32_t)(old + n));
    if (RS16(a) > max) W16(a, (uint32_t)max);
    if (RS16(a) < 0) W16(a, 0);
    return (int16_t)(RS16(a) - old);
}

static void Player_Heal(int16_t n)                                     /* 0x06062AD0 */
{
    W16(PL + 0x16, (uint32_t)(RS16(PL + 0x16) + n));
    if (RS16(PL + 0x16) > RS16(0x060252B0u)) W16(PL + 0x16, R16(0x060252B0u));
}

static void OneUp_Token(int16_t n)                                     /* 0x06062BC6 */
{
    W16(0x060252B2u, (uint32_t)(RS16(0x060252B2u) + n));
    if (RS16(0x060252B2u) >= 10) {
        Lives_Add(1);
        W16(0x060252B2u, (uint32_t)(RS16(0x060252B2u) - 10));
        sub_06007CDE(0x107);
    }
}

static void Item_Apply(uint16_t kind)                                  /* 0x06062BFC */
{
    int sfx = 0x106;
    switch (kind & 0xFF) {
    case 0: add_clamped(0x060252B6u, 1, 999); Score_Add(100); break;
    case 1: add_clamped(0x060252B6u, 5, 999); Score_Add(200); break;
    case 2: Player_Heal(RS16(0x060AD2FCu + 2u * (uint32_t)RS16(0x060252C4u))); Score_Add(100); sfx = 0x108; break;
    case 3: Player_Heal(RS16(0x060252B0u)); Score_Add(200); sfx = 0x108; break;
    case 4: OneUp_Token(1); Score_Add(200); break;
    case 5: W16(0x060252BEu, 1); Score_Add(500); break;
    case 6: W16(0x060252BAu, (R16(0x060252BAu) & 0xFFF0) | 0x2004); Score_Add(500); break;
    case 7: W16(0x060252BAu, (R16(0x060252BAu) & 0xFFF0) | 0x2000); Score_Add(500); break;
    default: return;
    }
    sub_06007CDE(sfx);
}

static void Item_Update(uint32_t it)                                   /* 0x06062CFC */
{
    if (R16(it + 0x2C) & 1) {
        Anim_Start(it + 0xC, 0x060AD302u, 0x060D8040u, 0x4B0, RS16(0x060AE2DAu + 2u * (R16(it) & 0xFF)));
        AND16(it + 0x2C, 0xFFFE);
    }
    int show = 1;
    if (!(R16(it + 2) & 0x8000)) {
        if (RS16(0x0602526Au) == 8) {
            int32_t d = RS32(CAM_X) - RS32(it + 4) + 0x00A00000;
            if (d < 0) d = RS32(it + 4) - RS32(CAM_X) + (int32_t)0xFF600000;
            if (d > 0x01000000) return;
        }
        if (!FROZEN) W16(it + 2, R16(it + 2) + 1);
        if (RS16(0x0602526Au) == 8) AND16(it + 2, 0x3F);
        switch ((int32_t)R16(it + 2) / 60) {
        case 7:  if (!(R16(it + 2) & 2)) show = 0; break;
        case 8:  if (!(R16(it + 2) & 4)) show = 0; break;
        case 9:  if (!(R16(it + 2) & 8)) show = 0; break;
        case 10: W16(it, 0xFFFF); return;
        }
        int32_t x = RS32(it + 4), y = RS32(it + 8);
        if (!(R16(0x060252BAu) & 0xA000) && !(R16(PL + 2) & 8)) {
            int32_t dx = iabs(x - RS32(PL + 8)), dy = y - RS32(PL + 0xC);
            if (dx <= 0x00180000 && dy <= 0x00180000 && dy >= (int32_t)0xFFC00000) {   /* picked up */
                W16(it + 2, 0x8000);
                Item_Apply(R16(it));
                W32(it + 4, (uint32_t)x); W32(it + 8, (uint32_t)y);
            }
        }
    } else {                                                            /* rising after pickup */
        W32(it + 8, (uint32_t)(RS32(it + 8) - 0x10000));
        W16(it + 2, R16(it + 2) + 1);
        if ((R16(it + 2) & 0x7FFF) > 0x20) { W16(it, 0xFFFF); return; }
        if ((R16(it + 2) & 0x7FFF) > 0x18 && (R16(it + 2) & 2)) show = 0;
    }
    W16(SPR + 0x10, 0x0200);
    W32(SPR + 0x18, 0x81);
    W16(SPR + 0x0A, (uint32_t)(RS16(0x060AE2CAu + 2u * (R16(it) & 0xFF)) + 0x70));
    W16(SPR + 0x0C, 0);
    W16(SPR + 0x0E, 0);
    AND16(it + 0xC, 0xBFFF);
    if (!show) OR16(it + 0xC, 0x4000);
    W16(SPR + 2, (uint32_t)((RS32(it + 4) - RS32(CAM_X)) >> 16));
    W16(SPR + 4, (uint32_t)((RS32(it + 8) - RS32(CAM_Y)) >> 16));
    Anim_Tick(it + 0xC);
}

/* 0x06063350 / 0x06063428: animated common characters (one of three updated per frame). */
static void char_frame(uint16_t chr, uint32_t src)
{
    uint32_t dst = VDP1_VRAM_BASE + (uint32_t)R16(CHAR_TABLE + (uint32_t)(int16_t)(chr * 10)) * 8;
    for (uint32_t i = 0; i < 9 * 32; i += 2) W16(dst + i, R16(src + i));
    Render_InvalidateVram(dst, 9 * 32);
}
static void Common_AnimateChars(void)
{
    switch (R16(0x060143EAu) & 3) {
    case 0: char_frame(0x4EE, 0x060D3B4Cu + R32(0x060F4BE8u) * 0x120);
            W32(0x060F4BE8u, R32(0x060F4BE8u) + 1 > 0x1D ? 0 : R32(0x060F4BE8u) + 1); break;
    case 1: char_frame(0x4DF, 0x060D5D0Cu + R32(0x060F4BE4u) * 0x120);
            W32(0x060F4BE4u, R32(0x060F4BE4u) + 1 > 0x1D ? 0 : R32(0x060F4BE4u) + 1); break;
    case 2: char_frame(0x4DE, 0x060D300Cu + R32(0x060F4BECu) * 0x120);
            W32(0x060F4BECu, R32(0x060F4BECu) + 1 > 9 ? 0 : R32(0x060F4BECu) + 1); break;
    }
}

void Items_Update(void)
{
    Common_AnimateChars();
    for (uint32_t i = 0; i < 64; i++) {
        uint32_t it = ITEMS + (uint32_t)(int16_t)(i * 0x30);
        W32(ITEM_CUR, it);
        if (R16(it) == 0xFFFF) continue;
        uint16_t k = R16(it) & 0xFF;
        if (k <= 7) Item_Update(it);
        else if (k == 8) once("item kind", k);
    }
}
