/* Enemies: shared update loop, hitboxes, player <-> enemy combat and the stage 1
 * soldier (type 0x0001), ported from SHINOBI.PPB. */
#include "enemy.h"
#include "objects.h"
#include "objhandlers.h"
#include "ram.h"
#include "anim.h"
#include "sprite.h"
#include "stage.h"
#include "player_ext.h"
#include "collision.h"
#include "fx.h"
#include "fixmath.h"
#include <stdio.h>

#define O         R32(ENEMY_CUR)
#define SLOT      RS16(ENEMY_CUR_SLOT)
#define PL        0x060EA2B4u
#define CAM_X     0x060252A0u
#define CAM_Y     0x060252A4u
#define FROZEN    (R16(0x0602529Eu) & 0x8000)
#define SPR       0x060250ACu
#define MAGIC     0x060252BAu        /* ninjutsu / magic state */
#define STAGE     RS16(0x0602526Au)
#define CHAR_BASE(s)  RS16(0x060A9064u + 2u * (uint32_t)(int16_t)(s))
#define COLOR_BANK(s) RS16(0x060A9084u + 2u * (uint32_t)(int16_t)(s))
#define SCRATCH_ANIM  0x060FFE80u    /* PC: stand-in for the originals' on-stack anim states */

void Obj_Integrate(uint32_t o);      /* physics.c, 0x060452A4 */

static uint32_t slot_addr(int s) { return OBJ_TABLE + (uint32_t)(int16_t)(s * (int)OBJ_SIZE); }
static int32_t iabs(int32_t v) { return v < 0 ? -v : v; }
static void once(const char *what, unsigned id)
{
    static unsigned seen[64]; static int n;
    for (int i = 0; i < n; i++) if (seen[i] == id) return;
    if (n < 64) seen[n++] = id;
    printf("[enemy] %s %04X not ported\n", what, id);
}

/* ---------------------------------------------------------------- tables */

int Enemy_FindSlot(int16_t type)
{
    for (int16_t i = 0; i < 5; i++)
        if (RS16(0x060A91B8u + 2u * (uint32_t)(STAGE * 5 + i)) == type) return i;
    return -1;
}

void Enemy_RegisterChars(int16_t type, uint32_t t)
{
    int16_t s = (int16_t)Enemy_FindSlot(type);
    int base = (uint16_t)CHAR_BASE(s);
    uint32_t off = R32(0x060A9070u + 4u * (uint32_t)s);
    uint32_t n = R32(t);
    for (uint32_t i = 0; i < n; i++) {
        uint32_t r = t + 4 + i * 8;
        Char_Register((int)((base + i) & 0xFFFF), Ram_Ptr(r + 6)[0] * 8, Ram_Ptr(r + 7)[0], RS16(r + 4), off);
        off += (uint32_t)R16(r + 2) * 32;
    }
}

/* 0x06005C30: copy n RGB555 colours into the shadow CRAM. */
static void Pal_Copy(int dst, int n, uint32_t src)
{
    for (int i = 0; i < n; i++) W16(SHADOW_CRAM + 2u * (uint32_t)((dst + i) & 0xFFFF), R16(src + 2u * (uint32_t)i));
    W16(0x060250A8u, 1);
}

/* 0x0608FA74: soldier graphics + 4 palettes (darkened on stage index 0). */
static void Enemy1_Load(void)
{
    static const uint32_t pals[4] = { 0x060E3EDCu, 0x060E3EFCu, 0x060E3F1Cu, 0x060E3F3Cu };
    Enemy_RegisterChars(1, 0x060E3C40u);
    int16_t b = COLOR_BANK(Enemy_FindSlot(1));
    for (int k = 0; k < 4; k++) Pal_Copy((uint16_t)(b + k) * 16, 16, pals[k]);
    if (STAGE != 0) return;
    for (int k = 0; k < 4; k++) Pal_SetFaded((uint16_t)(b + k) * 16, 16, pals[k], -16, -16, 8);
}

void Enemy_LoadTypes(void)
{
    for (int16_t i = 0; i < 4; i++) {
        uint16_t t = R16(0x060A91B8u + 2u * (uint32_t)(STAGE * 5 + i));
        if (t == 0xFFFF || R32(0x060A9124u + 4u * t) == 0) continue;
        if (t == 1) Enemy1_Load();
        else once("loader for type", t);
    }
}

void Enemy_ResetTables(void)
{
    Projectiles_Clear();                                            /* 0x06060904 */
    Effects_Clear();                                                /* 0x0606210A */
    Items_Clear();                                                  /* 0x0606294E */
    W16(0x060EA32Cu, 0xFFFF); W16(0x060EA32Cu + 0x78, 0xFFFF);     /* Sho's shurikens */
}

/* --------------------------------------------------------------- hitboxes */

typedef struct { uint32_t list; int32_t x, y; uint16_t fl; } HitSide;

/* 0x06048EBC: first overlap between a box of kind ka in A and kind kb in B.
 * Returns (indexB << 8 | indexA) or 0xFFFF; HIT_POINT = centre of the overlap. */
static uint16_t hitbox_lists(int16_t ka, const HitSide *A, int16_t kb, const HitSide *B)
{
    if (!A->list || !B->list || R16(A->list) == 0xFFFF || R16(B->list) == 0xFFFF) return 0xFFFF;
    uint32_t pa = A->list;
    for (int16_t na = 11; na > 0 && R16(pa) != 0xFFFF; na--, pa += 10) {
        if (RS16(pa) != ka) continue;
        ObjRect a, b;
        a.t = RS16(pa + 4) * 0x10000 + A->y; a.b = RS16(pa + 8) * 0x10000 + A->y;
        if (!(A->fl & 0x8000)) { a.l = RS16(pa + 2) * 0x10000 + A->x; a.r = RS16(pa + 6) * 0x10000 + A->x; }
        else                   { a.r = A->x - RS16(pa + 2) * 0x10000; a.l = A->x - RS16(pa + 6) * 0x10000; }
        uint32_t pb = B->list;
        for (int16_t nb = 11; nb > 0 && R16(pb) != 0xFFFF; nb--, pb += 10) {
            if (RS16(pb) != kb) continue;
            b.t = RS16(pb + 4) * 0x10000 + B->y; b.b = RS16(pb + 8) * 0x10000 + B->y;
            if (!(B->fl & 0x8000)) { b.l = RS16(pb + 2) * 0x10000 + B->x; b.r = RS16(pb + 6) * 0x10000 + B->x; }
            else                   { b.r = B->x - RS16(pb + 2) * 0x10000; b.l = B->x - RS16(pb + 6) * 0x10000; }
            /* (debug cheat: box outlines, not ported) */
            if (!Rect_Overlap(&a, &b)) continue;
            int32_t lo = b.l < a.l ? a.l : b.l, hi = a.r < b.r ? a.r : b.r, d = hi - lo;
            W32(HIT_POINT, (uint32_t)(((d + (d < 0)) >> 1) + lo));
            lo = b.t < a.t ? a.t : b.t; hi = a.b < b.b ? a.b : b.b; d = hi - lo;
            W32(HIT_POINT + 4, (uint32_t)(((d + (d < 0)) >> 1) + lo));
            return (uint16_t)((((11 - nb) * 0x100) & 0xFF00) | (11 - na));
        }
    }
    return 0xFFFF;
}

uint16_t Hitbox_Test(uint32_t a, int16_t ka, uint32_t b, int16_t kb)
{
    if (iabs(RS32(a + 8) - RS32(b + 8)) > 0x02000000) return 0xFFFF;
    if (iabs(RS32(a + 0xC) - RS32(b + 0xC)) > 0x02000000) return 0xFFFF;
    HitSide A = { R32(a + 0x68), RS32(a + 8), RS32(a + 0xC), (uint16_t)(R16(a + 2) & 0x8000) };
    HitSide B = { R32(b + 0x68), RS32(b + 8), RS32(b + 0xC), (uint16_t)(R16(b + 2) & 0x8000) };
    return hitbox_lists(ka, &A, kb, &B);
}

/* ------------------------------------------------------ player <-> enemy */

static int Player_SwordPower(void)                                  /* 0x06045BA6 */
{
    return ((R16(MAGIC) & 0x4000) && (R16(MAGIC) & 0xF) == 4) ? 4 : 2;
}

void Player_KnockDir(uint32_t pt)                            /* 0x06060E40 */
{
    if (((R16(MAGIC) & 0x00F0) || !(R16(MAGIC) & 0x4000)) && !(R16(0x0602529Cu) & 1)) {
        AND16(PL + 2, 0x7FFF);
        if (RS32(pt) < RS32(PL + 8)) OR16(PL + 2, 0x8000);
    }
}

void Player_TakeDamage(int16_t dmg)                          /* 0x06045BEC */
{
    if (R16(PL + 4) & 0x0410) return;
    if ((R16(0x0602529Cu) & 1) && R32(0x06014414u) == 0x4465268Fu) return;   /* cheat: invincible */
    uint16_t mf = R16(MAGIC);
    if (!(mf & 0x00F0) && (mf & 0x4000)) {                          /* Ikazuchi shield absorbs */
        W16(0x060252BCu, (uint32_t)(RS16(0x060252BCu) - dmg));
        Obj_Spawn(7, RS32(PL + 8), RS32(PL + 0xC), R16(PL + 2) & 0x8000, 0);
        W16(R32(0x060EA41Cu), 0x60);
        sub_06007CDE(0x111);
        if (RS16(0x060252BCu) < 1) { AND16(MAGIC, 0xBFFF); W16(0x060252BCu, 0); }
        return;
    }
    if ((mf & 0x00F0) == 0x40 && (mf & 0x4000)) {
        W16(0x060252BCu, (uint32_t)(RS16(0x060252BCu) - dmg));
        if (RS16(0x060252BCu) < 1) { AND16(MAGIC, 0xBFFF); W16(0x060252BCu, 0); }
    }
    if (dmg != 0) W16(PL + 0x16, (uint32_t)(RS16(PL + 0x16) - dmg));
    OR16(PL + 2, 8);
}

static int Player_AttackKind(void)                                  /* 0x0604634E */
{
    switch (RS16(0x060EA2BAu)) {
    case 0x04: case 0x09: case 0x0C: return 1;
    case 0x05: case 0x0A: case 0x0D: case 0x1B: return 0;
    case 0x16: case 0x4D: return 5;
    case 0x17: case 0x50: return 3;
    case 0x18: case 0x1C: case 0x23: case 0x31: case 0x48: case 0x4E: return 4;
    case 0x24: case 0x32: case 0x3C: case 0x49: return 2;
    default: return 6;
    }
}

/* 0x0604931E: the current enemy's attack boxes against Sho. Returns 2 blocked by Sho's
 * guard, 3 Sho hit by the weapon (dmgWeapon), 1 Sho touched the body (dmgTouch), 0. */
static int Enemy_AttackPlayer(int16_t dmgWeapon, int16_t dmgTouch, uint16_t mode)
{
    uint32_t o = O;
    if ((mode & 0x8000) && Hitbox_Test(o, 2, PL, 1) != 0xFFFF && (mode & 0x4000) &&
        (R16(PL + 2) & 0x8000) != (R16(o + 2) & 0x8000)) {
        Obj_Spawn(3, RS32(HIT_POINT), RS32(HIT_POINT + 4), R16(PL + 2) & 0x8000, 0);
        OR16(PL + 4, 0x1000);
        sub_06007CDE(0x105);
        return 2;
    }
    if (R16(R32(0x060EA41Cu)) == 0 && !(R16(PL + 2) & 8) && !(R16(PL + 4) & 0x0410)) {
        if (Hitbox_Test(o, 2, PL, 0) != 0xFFFF) {
            Obj_Spawn(2, RS32(HIT_POINT), RS32(HIT_POINT + 4), 0, 0);
            Player_KnockDir(HIT_POINT);
            Player_TakeDamage(dmgWeapon);
            return 3;
        }
        if ((R16(o + 0x12) & 0x8000) && R16(o) != 0x1A && Hitbox_Test(o, 0, PL, 0) != 0xFFFF) {
            Player_KnockDir(HIT_POINT);
            Player_TakeDamage(dmgTouch);
            return 1;
        }
    }
    return 0;
}

/* 0x0604967C: Sho's shurikens (2 objects after the player) against the current enemy. */
static int Enemy_CheckShurikens(void)
{
    uint32_t o = O;
    if ((R16(o + 2) & 8) || (R16(o + 0x1C) & 0x8000)) return 0;
    uint32_t s = 0x060EA32Cu;
    for (int i = 0; i < 2; i++, s += 0x78) {
        if (R16(s) == 0xFFFF || !(R16(s + 0x12) & 1)) continue;
        if (Hitbox_Test(o, 0, s, 2) == 0xFFFF && Hitbox_Test(o, 3, s, 2) == 0xFFFF) return 0;
        Obj_Spawn(2, RS32(HIT_POINT), RS32(HIT_POINT + 4), 0, 0);
        OR16(o + 2, 8);
        W16(o + 0x16, (uint32_t)(RS16(o + 0x16) - 1));
        OR16(0x060EA2B8u, 0x0800);
        sub_06007CDE(0x104);
        return 2;
    }
    return 0;
}

/* 0x060494B4: Sho's sword against the current enemy. 2 = damaged, 1 = guarded, 0. */
static int Enemy_CheckPlayerHits(uint16_t mode)
{
    uint32_t o = O;
    if (R16(o + 2) & 8) return 0;
    int16_t dmg = (int16_t)Player_SwordPower();
    if (Hitbox_Test(o, 3, PL, 2) != 0xFFFF) goto damaged;
    if ((mode & 0x8000) && Hitbox_Test(o, 1, PL, 2) != 0xFFFF &&
        !((mode & 0x4000) && (R16(PL + 2) & 0x8000) == (R16(o + 2) & 0x8000))) {
        Obj_Spawn(3, RS32(HIT_POINT), RS32(HIT_POINT + 4), R16(o + 2) & 0x8000, 0);
        OR16(PL + 4, 0x0800);
        sub_06007CDE(0x105);
        return 1;
    }
    if (Hitbox_Test(o, 0, PL, 2) == 0xFFFF)
        return (!(mode & 0x2000) && Enemy_CheckShurikens()) ? 2 : 0;
damaged:
    Obj_Spawn(2, RS32(HIT_POINT), RS32(HIT_POINT + 4), 0, 0);
    OR16(o + 2, 8);
    W16(o + 0x16, (uint32_t)(RS16(o + 0x16) - dmg));
    OR16(PL + 4, 0x0800);
    sub_06007CDE(0x104);
    return 2;
}

/* ----------------------------------------------------------- common bits */

void Enemy_AnimSwitch(void)
{
    uint32_t o = O;
    uint16_t t = R16(o);
    if (t == 0xFFFF || RS16(o + 6) == RS16(o + 0x22)) return;
    W16(o + 0x22, R16(o + 6));
    Anim_Start(o + 0x58, R32(0x060A92B8u + (uint32_t)t * 8), R32(0x060A92B8u + (uint32_t)t * 8 + 4),
               CHAR_BASE(SLOT), RS16(o + 6));
}

/* 0x06049A4C: draw a secondary animation (explosion overlay) on the current enemy. */
static void Enemy_DrawOverlay(uint32_t st, int16_t bank, uint32_t a, uint32_t b, uint32_t c)
{
    uint32_t o = O;
    W16(st + 2, R16(a)); W16(st + 26, R16(b)); W16(st, R16(c));
    W16(SPR + 2, (uint32_t)((RS32(o + 8) - RS32(CAM_X)) >> 16));
    W16(SPR + 4, (uint32_t)((RS32(o + 0xC) - RS32(CAM_Y)) >> 16));
    W16(SPR + 0x10, (R16(o + 2) & 0x8000) | 0x0200);
    W16(SPR + 0x0A, (uint32_t)bank);
    W16(SPR + 0x0C, 0);
    W32(SPR + 0x18, 1);
    W16(SPR + 0x0E, R16(st) & 3);
    Anim_Tick(st);
    W16(a, R16(st + 2)); W16(b, R16(st + 26)); W16(c, R16(st));
}

int Lives_Add(int16_t n)
{
    int16_t old = RS16(LIVES);
    W16(LIVES, (uint32_t)(old + n));
    if (RS16(LIVES) > 99) W16(LIVES, 99);
    if (RS16(LIVES) < 0) W16(LIVES, 0);
    return (int16_t)(RS16(LIVES) - old);
}

void Score_Add(int32_t pts)
{
    int32_t old = RS32(SCORE);
    W32(SCORE, (uint32_t)(old + pts));
    if (RS32(SCORE) > 0x05F5E0F6) W32(SCORE, 0x05F5E0F6u);
    if (R32(0x06000214u) < R32(SCORE)) W32(0x06000214u, R32(SCORE));          /* high score */
    int32_t n = RS32(0x06014418u + 4u * (uint32_t)RS16(0x060252C4u));        /* extend every n points */
    if (n && old / n < RS32(SCORE) / n) {
        Lives_Add(1);
        if (R16(0x060DAF72u) != 7) sub_06007CDE(0x107);
    }
}

void Enemy_DeathBurst(uint32_t o)
{
    sub_06007CDE(0x115);
    int kind = ((R16(o + 0x12) & 0x8000) == 0x8000 || R16(o) >= 0x1000) ? 6 : 1;
    for (uint32_t i = 0; i < 4; i++) {
        uint32_t d = 0x060A93E0u + i * 8;
        Obj_Spawn(kind, RS16(d) * 0x10000 + RS32(o + 8), RS16(d + 2) * 0x10000 + RS32(o + 0xC), RS16(d + 6), RS16(d + 4));
    }
    if (R16(o + 0x18) != 0xFFFF) Item_Drop(RS16(o + 0x18), RS32(o + 8), RS32(o + 0xC));   /* 0x06062A14 */
}

/* ------------------------------------------------ type 0x0001: soldier */

static void Soldier_Init(void)                                      /* 0x0608FDE4 */
{
    uint32_t o = O;
    W16(o + 6, 0);
    Anim_Start(o + 0x58, 0x060BFFB0u, 0x060E43D4u, CHAR_BASE(SLOT), RS16(o + 6));
    AND16(o + 2, 0xFFFE);
    W16(o + 0x16, 1);
    W16(o + 0x14, 30);
    W16(o + 4, 0);
}

static int Soldier_CheckHits(void)                                  /* 0x06092C74 */
{
    uint32_t o = O;
    int16_t a = RS16(o + 6);
    uint16_t sub = R16(o + 0x12) & 0x1F;
    if (a == 0x11 || a == 0x12 || a == 0x13 || a == 0x19 || a == 0x1B ||
        ((a == 7 || a == 8) && (sub == 3 || sub == 0xB))) {
        OR16(o + 2, 8);                                             /* invulnerable poses */
        OR16(o + 0x12, 0x4000);
        return 0;
    }
    if (Enemy_AttackPlayer(1, 0, 0xC000)) {                          /* face each other */
        if (!(R16(o + 2) & 0x8000)) OR16(PL + 2, 0x8000); else AND16(PL + 2, 0x7FFF);
    }
    AND16(o + 0x12, 0xBFFF);
    return Enemy_CheckPlayerHits(0xC000);
}

static void Soldier_OnHit(int r, uint32_t a, uint32_t b, uint32_t c)    /* 0x06092E14 */
{
    uint32_t o = O;
    int k = (r == 2) ? Player_AttackKind() : 4;
    if (RS16(o + 0x16) > 0) W16(o + 6, 0x0D);                       /* hurt */
    else if (k == 4 || k == 5) W16(o + 6, 0x0E);                    /* knocked away */
    else {                                                          /* cut down: explosion overlay */
        W16(o + 6, 0x0F);
        Anim_Start(SCRATCH_ANIM, 0x060BFFB0u, 0x060E43D4u, CHAR_BASE(SLOT), 0x10);
        W16(a, R16(SCRATCH_ANIM + 2));
        W16(b, R16(SCRATCH_ANIM + 26));
        W16(c, R16(SCRATCH_ANIM));
    }
    Enemy_AnimSwitch();
    sub_06007CDE(0x205);
}

static int Soldier_AnimDone(int16_t next)                           /* 0x06092ECE */
{
    uint32_t o = O;
    switch (RS16(o + 6)) {
    case 1:                                                         /* crouched down */
        W16(o + 6, (uint32_t)next);
        OR16(o + 2, 0x0800);
        if (RS16(o + 6) == 2 && R16(o + 0x14) > 60) W16(o + 0x14, 60);
        return 0;
    case 3:                                                         /* stood up */
        W16(o + 6, (uint32_t)next);
        AND16(o + 2, 0xF7FF);
        if (RS16(o + 6) == 0 && R16(o + 0x14) > 60) W16(o + 0x14, 60);
        return 0;
    case 0x0D:
        if (!(R16(o + 2) & 0x4000)) {
            if (R16(o + 0x14) > 60) W16(o + 0x14, 60);
            W16(o + 6, (R16(o + 2) & 0x0800) ? 2 : 0);
            AND16(o + 2, 0xFFF7);
        } else {                                                    /* still in the air */
            W16(o + 6, 0x0D);
            AND16(o + 0x58, 0x7FFF);
            W16(o + 0x5A, 0);
            W16(o + 0x72, 0);
        }
        return 0;
    case 0x0E:
        Enemy_DeathBurst(o);
        Score_Add(100);
        W16(o, 0xFFFF);
        return 1;
    default:
        W16(o + 6, (R16(o + 2) & 0x0800) ? 2 : 0);
        if (R16(o + 0x14) > 60) W16(o + 0x14, 60);
        AND16(o + 2, 0xFFF7);
        return 0;
    }
}

/* 0x0609379C: 1 = too close / unreachable (jump), 0 = shoot standing, 2 = shoot crouching. */
static int Soldier_PlayerZone(void)
{
    uint32_t o = O;
    if (iabs(RS32(PL + 8) - RS32(o + 8)) < 0x500000 && iabs(RS32(PL + 0xC) - RS32(o + 0xC)) < 0x500000) return 1;
    int32_t y = RS32(o + 0xC) + (int32_t)0xFFD40000;
    if (y < RS32(PL + 0xC) && RS32(PL + 0xC) + RS16(PL + 0x20) * -0x80000 + 0x100000 < y) return 0;
    y = RS32(o + 0xC) + (int32_t)0xFFF10000;
    if (y < RS32(PL + 0xC) && RS32(PL + 0xC) + RS16(PL + 0x20) * -0x80000 < y) return 2;
    return 1;
}

static int Soldier_Decide(uint32_t next)                            /* 0x060936DE */
{
    uint32_t o = O;
    int z = Soldier_PlayerZone();
    if (STAGE == 0x10 && z == 1) z = 0;
    if (z == 0) {
        if (!(R16(o + 2) & 0x0800)) { W16(o + 6, 9); return 9; }
        W16(o + 6, 3); W16(next, 9); return 3;                      /* stand up, then shoot */
    }
    if (z == 1) {
        AND16(o + 2, 0xF7FF);
        W16(o + 6, 4);                                              /* jump */
        sub_06007CDE(0x126);
        return 0;
    }
    if (z == 2) {
        if (!(R16(o + 2) & 0x0800)) { W16(o + 6, 1); W16(next, 10); return 1; }
        W16(o + 6, 10); return 10;
    }
    return z;
}

static void Soldier_Jump(int16_t landAct, int16_t airAct)            /* 0x06093856 */
{
    uint32_t o = O;
    int16_t throwAt = (STAGE == 9) ? 8 : 7;
    if (!(R16(o + 2) & 0x4000) && RS16(o + 0x58) == 2) W16(o + 6, (uint32_t)landAct);
    if (!(R16(o + 0x58) & 3)) return;
    if (R16(o + 0x1A) < 4) {
        if ((R16(o + 0x58) & 3) == 1) { AND16(o + 0x58, 0xFFFC); OR16(o + 0x58, 2); }
        W32(o + 0x34, STAGE != 9 ? 0x00080000u : 0x00078000u);     /* take off */
    } else if (RS16(o + 0x1A) == throwAt) {
        W16(o + 6, (uint32_t)airAct);
        AND16(o + 0x58, 0xFFFC);
    }
}

/* 0x06093518: angle from the enemy (+dx,+dy) to Sho's chest, leading a running Sho. */
static int Soldier_Aim(uint32_t o, int32_t dx, int32_t dy)
{
    if (R16(o + 2) & 0x8000) dx = -dx;
    int32_t lead = 0;
    if (iabs(RS32(PL + 0x30)) > 0x8000 && !(R16(PL + 2) & 0x4000)) {
        if (R16(PL + 2) & 0x0200) lead = RS32(0x060C0944u);              /* running */
        else lead = RS32((R16(PL + 2) & 0x0800) ? 0x060C094Cu : 0x060C0948u);
    }
    if (R16(PL + 2) & 0x8000) lead = -lead;
    return Atan8((int16_t)((uint32_t)(RS32(o + 8) + dx) >> 16), (int16_t)((uint32_t)(RS32(o + 0xC) + dy) >> 16),
                 (int16_t)((uint32_t)(RS32(PL + 8) + lead) >> 16), (int16_t)((uint32_t)(RS32(PL + 0xC) + (int32_t)0xFFD80000) >> 16));
}

static int Soldier_Think(uint32_t next)                             /* 0x0609307A */
{
    uint32_t o = O;
    uint16_t a = R16(o + 6);
    if (a == 0 || a == 2) {
        AND16(o + 2, 0x7FFF);
        if (RS32(0x060EA2BCu) < RS32(o + 8)) OR16(o + 2, 0x8000);  /* face Sho */
        if (RS32(o + 0x34) != 0) { W16(o + 6, 5); return 5; }        /* falling */
        if (STAGE == 0x10) AND16(o + 4, 0x7FFF);
        else {
            int32_t dx = (R16(o + 2) & 0x8000) ? (int32_t)0xFF800000 : 0x00800000;
            int32_t x = RS32(o + 8), y = RS32(o + 0xC);
            if (!Projectile_InBox(x + dx, y + (int32_t)0xFFC00000, x, y)) AND16(o + 4, 0x7FFF);
            else if (!(R16(o + 4) & 0x8000)) {                        /* dodge a shuriken */
                OR16(o + 4, 0x8000);
                W16(o + 6, 4);
                return 4;
            }
        }
        if (iabs(RS32(0x060EA2BCu) - RS32(o + 8)) < 0x01000000) {
            W16(o + 0x14, R16(o + 0x14) - 1);
            if (RS16(o + 0x14) < 1) {
                W16(o + 0x14, STAGE == 2 ? 0x28 : 0x3C);
                return Soldier_Decide(next);
            }
        }
        return a;
    }
    if (a == 4) { Soldier_Jump(0, 0x0B); return 0; }
    if (a == 9 || a == 10) {                                         /* shoot standing / crouched */
        if (!(R16(o + 0x58) & 3)) return 0x58;
        int32_t dx = 0x00220000, dy = (a == 9) ? (int32_t)0xFFD10000 : (int32_t)0xFFED0000;
        int dir = 0;
        if (R16(o + 2) & 0x8000) { dx = (int32_t)0xFFDE0000; dir = 0x80; }
        int32_t x = RS32(o + 8) + dx, y = RS32(o + 0xC) + dy;
        int32_t xl = (a == 9) ? 0x000A0000 : 0, xr = (a == 9) ? 0x01360000 : 0x01400000;
        if (RS32(CAM_X) + xl <= x && x <= RS32(CAM_X) + xr && RS32(CAM_Y) <= y && y <= RS32(CAM_Y) + 0x00E00000)
            Projectile_Spawn(1, x, y, (int16_t)dir, RS32(0x060C085Cu + 4u * (uint32_t)STAGE));
        AND16(o + 0x58, 0xFFFC);
        sub_06007CDE(0x125);
        return 0;
    }
    if (a == 0x0B) {                                                 /* throw while jumping */
        if (!(R16(o + 2) & 0x4000)) { W16(o + 6, 0x0C); return 0x0C; }
        if (!(R16(o + 0x58) & 3)) return 0x58;
        int dir = Soldier_Aim(o, 0x00190000, (int32_t)0xFFD50000);
        int32_t dx = (R16(o + 2) & 0x8000) ? (int32_t)0xFFE70000 : 0x00190000;
        int32_t x = RS32(o + 8) + dx, y = RS32(o + 0xC) + (int32_t)0xFFD50000;
        if (RS32(CAM_X) <= x && x <= RS32(CAM_X) + 0x01400000 &&
            RS32(CAM_Y) + (int32_t)0xFFD80000 <= y && y <= RS32(CAM_Y) + 0x00E00000)
            Projectile_Spawn(1, x, y, (int16_t)dir, RS32(0x060C085Cu + 4u * (uint32_t)STAGE));
        AND16(o + 0x58, 0xFFFC);
        sub_06007CDE(0x125);
        return 0;
    }
    if (a == 0x0E && (R16(o + 2) & 0x4000)) { W16(o + 0x5A, 0); W16(o + 0x72, 0); }
    return a;
}

static void Soldier_Physics(void)                                   /* 0x06093ACC */
{
    uint32_t o = O;
    Obj_Integrate(o);
    Obj_CollideMap(o, 2, 8);
    if ((RS16(o + 6) == 4 || RS16(o + 6) == 5) && (R16(o + 2) & 0x10)) {
        W16(o + 6, 6);                                              /* landed */
        Enemy_AnimSwitch();
    }
}

static int Soldier_Dying(uint32_t a, uint32_t b, uint32_t c)        /* 0x060935F0 */
{
    uint32_t o = O;
    if (RS16(o + 6) != 0x0F) return 0;
    if (R16(o + 2) & 0x4000) { W16(o + 0x5A, 0); W16(o + 0x72, 0); }
    if ((R16(o + 0x58) & 3) != 1) return 0;
    Anim_Start(SCRATCH_ANIM, 0x060BFFB0u, 0x060E43D4u, CHAR_BASE(SLOT), 0x10);
    Enemy_DrawOverlay(SCRATCH_ANIM, (int16_t)(COLOR_BANK(SLOT) + 1), a, b, c);
    if (R16(SCRATCH_ANIM) & 0x8000) {
        Enemy_DeathBurst(o);
        Score_Add(100);
        W16(o, 0xFFFF);
        return 1;
    }
    return 0;
}

static void Soldier_Draw(uint16_t p)                                /* 0x06093930 */
{
    uint32_t o = O;
    int16_t a = RS16(o + 6);
    uint16_t sub = R16(o + 0x12) & 0x1F;
    W16(SPR + 2, (uint32_t)((RS32(o + 8) - RS32(CAM_X)) >> 16));
    W16(SPR + 4, (uint32_t)((RS32(o + 0xC) - RS32(CAM_Y)) >> 16));
    W16(SPR + 0x0A, (uint32_t)COLOR_BANK(SLOT));
    if (a == 0x19)      { W16(SPR + 0xC, 1); W16(SPR + 0x10, (R16(o + 2) & 0x8000) | 0x200); W32(SPR + 0x18, 0x81); }
    else if (a == 0x1B) { W16(SPR + 0xC, 0); W16(SPR + 0x10, (R16(o + 2) & 0x8000) | 0x200); W32(SPR + 0x18, 0xFFFFFF00u); }
    else if (sub == 3 || sub == 0xB) {
        W16(SPR + 0xC, 0xFFFF); W16(SPR + 0x10, (R16(o + 2) & 0x8000) | 0x200); W32(SPR + 0x18, 0xFFFFFFFFu);
    } else { W16(SPR + 0xC, 0); W16(SPR + 0x10, R16(o + 2) & 0x8000); }
    if ((sub == 3 || sub == 0xB) && (a == 7 || a == 8)) W16(SPR + 0xE, (uint32_t)sub_0600A604());
    else if (a == 0x0E || a == 0x11 || a == 0x12)        W16(SPR + 0xE, R16(o + 0x58) & 3);
    else W16(SPR + 0xE, (a == 0x0F && (R16(o + 0x58) & 3) == 1) ? (p & 3) : 0);
    Anim_Tick(o + 0x58);
}

static void EnemyHandler_0001(void)                                 /* 0x0608FB9C */
{
    uint32_t o = O;
    uint16_t sub = R16(o + 0x12) & 0x1F;
    if ((uint16_t)(sub - 2) < 11) { once("soldier subtype", sub); return; }
    int32_t d = RS32(o + 8) - RS32(CAM_X) + (int32_t)0xFF600000;
    if (d < 0) d = RS32(CAM_X) - RS32(o + 8) + 0x00A00000;
    int far = d >= 0x01000000;
    if (!far) {
        d = RS32(o + 0xC) - RS32(CAM_Y) + (int32_t)0xFF600000;
        if (d < 0) d = RS32(CAM_Y) - RS32(o + 0xC) + 0x00A00000;
        far = d >= 0x01000000;
    }
    if (far && !(R16(o + 2) & 0x4008)) return;                     /* off screen: asleep */
    if (R16(o + 2) & 1) Soldier_Init();
    if (!FROZEN) {
        int r = Soldier_CheckHits();
        int16_t a = RS16(o + 6);
        if ((R16(o + 2) & 8) && a != 0x0D && a != 0x0E && a != 0x0F) {
            Soldier_OnHit(r, o + 0x52, o + 0x54, o + 0x56);
        } else if (R16(o + 0x58) & 0x8000) {
            if (Soldier_AnimDone(RS16(o + 0x4A))) return;
        } else {
            Soldier_Think(o + 0x4A);
        }
        Soldier_Physics();
        Enemy_AnimSwitch();
    }
    if (Soldier_Dying(o + 0x52, o + 0x54, o + 0x56)) return;
    Soldier_Draw(R16(o + 0x56));
}

/* ------------------------------------------------------------ main loop */

void Obj_UpdateEnemies(void)
{
    for (W16(ENEMY_CUR_IDX, 0); R16(ENEMY_CUR_IDX) < OBJ_SLOTS; W16(ENEMY_CUR_IDX, R16(ENEMY_CUR_IDX) + 1)) {
        uint32_t o = slot_addr(R16(ENEMY_CUR_IDX));
        W32(ENEMY_CUR, o);
        uint16_t t = R16(o);
        if (t == 0xFFFF || t >= 0x1000 || R32(0x060A9090u + 4u * t) == 0) continue;
        int16_t s = (int16_t)Enemy_FindSlot((int16_t)t);
        W16(ENEMY_CUR_SLOT, (uint16_t)s);
        if (s == -1) continue;
        if (t == 1) EnemyHandler_0001();
        else once("enemy type", t);
        if (!FROZEN) AND16(o + 0x10, 0xDFFF);
    }
}
