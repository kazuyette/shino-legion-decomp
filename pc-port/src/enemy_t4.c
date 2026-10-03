/* Enemy type 0x0004: a guard ninja that keeps to its post (+0x48 = home X in pixels).
 * It faces Sho, walks back home or keeps its distance, leaps at Sho with a jump solved
 * from the height/distance (0x06097130), slashes when close, back-flips away when Sho
 * attacks it from the front and wall-jumps off walls. 2 HP, 200 points.
 * Subtypes (+0x12 & 0x1F): 0 = 0x060673C6 (stage 6 lists), 1 = 0x060683DC (spawned
 * with flags 1 by the trap-door object 0x1003 in stage 1-1, SHINOBI 0x0604C90C), 2 =
 * 0x06069216 (a simpler hopper, not placed by any stage list).
 * Ported from SHINOBI 0x06067378 (loader) / 0x060673C6 (handler) and helpers.
 * Fields: +0x14 timer, +0x1E collision width, +0x48 home X (px), +0x4C/+0x4E jump
 * speeds (>> 12), +0x52/+0x54/+0x56 death overlay anim state, flags4 1 = flip/jump
 * in progress, 2 = keeping distance from Sho (instead of returning home).
 * Actions +6: 0 guard, 1/2 slash, 3 recover, 4 fall, 5 land, 6 jump, 7/8 air slash,
 * 9 run at Sho, 10 slash (subtype 1/2), 11 hurt, 12 knocked away, 13 cut down,
 * 15 walk (home / away), 16 back-flip, 17 flip landing, 18 guarded, 19 waiting (stage 6). */
#include "enemy_int.h"
#include "anim.h"
#include "sprite.h"
#include "collision.h"
#include "player_ext.h"
#include "fx.h"

void Obj_Integrate(uint32_t o);      /* physics.c, 0x060452A4 */

#define ST         SCRATCH_ANIM2     /* the handlers' on-stack anim state (death overlay) */
#define DEF4       0x060AE6BCu
#define SCRIPT4    0x060D9A48u
#define HOME(o)    ((int32_t)((uint32_t)(int32_t)RS16((o) + 0x48) << 16))
#define SHL12(v)   ((uint32_t)(int32_t)(v) << 12)

static int32_t iabs(int32_t v) { return v < 0 ? -v : v; }
static int32_t sdiv(int32_t a, int32_t b) { return b ? a / b : 0; }   /* A.BIN 0x060092BC */

/* 0x06067378 */
void Enemy4_Load(void)
{
    Enemy_RegisterChars(4, 0x060D9530u);
    int16_t s = (int16_t)Enemy_FindSlot(4);
    uint16_t b = R16(0x060A9084u + 2u * (uint32_t)s);
    Pal_Copy((uint16_t)(b << 4), 16, 0x060D97A4u);
    Pal_Copy((uint16_t)((b + 1) << 4), 16, 0x060D97C4u);
}

/* ------------------------------------------------------------- helpers */

/* 0x06080808 (shared clamp; local copy): keep X 24 px inside the stage bounds. */
static void Guard4_ClampX(uint32_t o)
{
    if (RS32(o + 8) < RS32(0x060EA618u) + 0x00180000) W32(o + 8, (uint32_t)(RS32(0x060EA618u) + 0x00180000));
    else if (RS32(o + 8) > RS32(0x060EA620u) + (int32_t)0xFFE80000) W32(o + 8, (uint32_t)(RS32(0x060EA620u) + (int32_t)0xFFE80000));
}

/* 0x06069172: Sho attacking from the front, close by -> back-flip (action 16). */
static void Guard4_CheckAttack(void)
{
    uint32_t o = O;
    if ((R16(PL + 2) & 0x8000) == (R16(o + 2) & 0x8000)) return;
    if (iabs(RS32(o + 8) - RS32(PL + 8)) >= 0x00500000) return;
    if (iabs(RS32(o + 0xC) - RS32(PL + 0xC)) >= 0x00280000) return;
    if ((int16_t)Player_AttackKind() == 6) return;
    W16(o + 6, 16);
}

/* 0x06097130 (shared; local copy): jump at Sho. Frames n to climb to Sho's height
 * under gravity 0x5000 (clamped to vmin/g..vmax/g, plus the fall below h px), then
 * +0x4C = vertical speed, +0x4E = (Sho.x +-24 px - x) / n, both >> 12. */
static void Guard4_SolveJump(int32_t vmin, int32_t vmax, int16_t h)
{
    uint32_t o = O;
    const int32_t g = 0x5000;
    int32_t n = 0, v = 0, s = 0;
    int32_t dy = RS32(o + 0xC) - RS32(PL + 0xC);
    if (!(0 >= dy)) {
        do { v += g; s += v; n++; } while (!(s >= dy));
    }
    W16(o + 0x4C, (uint16_t)(v >> 12));
    if ((int16_t)n < sdiv(vmin, g)) n = (int16_t)sdiv(vmin, g);
    else if ((int16_t)n > sdiv(vmax, g)) n = (int16_t)sdiv(vmax, g);
    int32_t y = (int32_t)((uint32_t)(int32_t)h << 16);
    if (!(dy >= y) && y > dy) {
        int32_t u = 0;
        do { y += u; u -= g; n++; } while (y > dy);
    }
    int32_t x = (R16(o + 2) & 0x8000) ? 0x00180000 : (int32_t)0xFFE80000;
    x = x + RS32(PL + 8) - RS32(o + 8);
    W16(o + 0x4E, (uint16_t)(int16_t)(sdiv(x, (int16_t)n) >> 12));
}

static void Guard4_FacePlayer(void)        /* inline in the handlers (case 0/9/19...) */
{
    uint32_t o = O;
    AND16(o + 2, 0x7FFF);
    if (RS32(PL + 8) < RS32(o + 8)) OR16(o + 2, 0x8000);
}

/* off-screen test, same code in the three handlers. 1 = asleep. */
/* face Sho, 'o.x < Sho.x -> right' form used by subtype 2 */
static void Guard4_FacePlayerX(void)
{
    uint32_t o = O;
    if (RS32(o + 8) < RS32(PL + 8)) AND16(o + 2, 0x7FFF); else OR16(o + 2, 0x8000);
}

static int Guard4_Asleep(void)
{
    uint32_t o = O;
    int32_t d = RS32(o + 8) - RS32(CAM_X) + (int32_t)0xFF600000;
    if (d < 0) d = RS32(CAM_X) - RS32(o + 8) + 0x00A00000;
    int far = d >= 0x01000000;
    if (!far) {
        int32_t y;
        if (RS32(o + 0xC) - RS32(CAM_Y) + (int32_t)0xFF900000 < 0) y = -RS32(o + 0xC) + RS32(CAM_Y) + 0x00700000;
        else y = RS32(o + 0xC) - RS32(CAM_Y) + (int32_t)0xFF900000;
        far = y >= 0x01000000;
    }
    return far && !(R16(o + 2) & 8) && !(R16(o + 2) & 0x4000);
}

/* hit reaction (0x06067548 / 0x06068514 / 0x06069382). 1 = it ran. */
static int Guard4_HitReact(int16_t r)
{
    uint32_t o = O;
    if (!(R16(o + 2) & 8) || R16(o + 6) == 11 || R16(o + 6) == 12 || R16(o + 6) == 13) return 0;
    int16_t k = (r == 2) ? (int16_t)Player_AttackKind() : 4;
    if (RS16(o + 0x16) > 0) W16(o + 6, 11);                           /* hurt */
    else if (k == 4 || k == 5) W16(o + 6, 12);                        /* knocked away */
    else {                                                            /* cut down */
        W16(o + 6, 13);
        Anim_Start(ST, DEF4, SCRIPT4, CHAR_BASE(SLOT), 14);
        W16(o + 0x52, R16(ST + 2));
        W16(o + 0x54, R16(ST + 26));
        W16(o + 0x56, R16(ST));
    }
    Enemy_AnimSwitch();
    return 1;
}

/* wall bump while running/walking (+0x10 set by Obj_CollideMap): jump off it. */
static int Guard4_WallJump(void)
{
    uint32_t o = O;
    if (R16(o + 0x10) & 8) {
        AND16(o + 2, 0x7FFF);
        W16(o + 6, 6); W16(o + 0x4E, 32); W16(o + 0x14, 12); OR16(o + 4, 1);
        return 1;
    }
    if (R16(o + 0x10) & 4) {
        OR16(o + 2, 0x8000);
        W16(o + 6, 6); W16(o + 0x4E, 0xFFE0); W16(o + 0x14, 12); OR16(o + 4, 1);
        return 1;
    }
    return 0;
}

/* action 6 with flags4 & 1 (wall jump), 0x06067A30 / 0x06068A0A */
static void Guard4_WallJumpAir(void)
{
    uint32_t o = O;
    if ((R16(o + 0x58) & 3) == 1) {
        W16(o + 0x14, R16(o + 0x14) - 1);
        if (R16(o + 0x14) != 0 && (int16_t)(R32(O + 8) >> 16) == (int16_t)(R32(O + 0x28) >> 16))
            W32(o + 0x34, 0x00060000u);
        else { AND16(o + 0x58, 0xFFFC); OR16(o + 0x58, 2); }
    }
    if (R16(o + 2) & 0x4000) {
        W32(o + 0x30, SHL12(RS16(o + 0x4E)));
        if (RS32(o + 0x34) < 0) W16(o + 6, 4);
    } else if ((R16(o + 0x58) & 3) == 2) W16(o + 6, 5);
}

/* action 15 (walk home / keep distance), same code at 0x06067F4E and 0x06068D1A */
static void Guard4_Walk(void)
{
    uint32_t o = O;
    if (R16(o + 4) & 2) {
        if (RS32(o + 8) < RS32(PL + 8)) AND16(o + 2, 0x7FFF); else OR16(o + 2, 0x8000);
    } else if (RS32(o + 8) < HOME(o)) {
        OR16(o + 2, 0x8000);
        if (!(RS32(o + 8) >= RS32(PL + 8))) { W16(o + 6, 0); W16(o + 0x14, 2); return; }
    } else {
        AND16(o + 2, 0x7FFF);
        if (!(RS32(PL + 8) >= RS32(o + 8))) { W16(o + 6, 0); W16(o + 0x14, 2); return; }
    }
    if (RS32(o + 0x34) != 0) { W16(o + 6, 4); W16(o + 0x4E, 0); return; }
    Guard4_CheckAttack();
    if (Guard4_WallJump()) return;
    if (R16(o + 4) & 2) {
        if (iabs(RS32(o + 8) - RS32(PL + 8)) >= 0x00500000) { W16(o + 6, 0); W16(o + 0x14, 30); }
        else W32(o + 0x30, RS32(o + 8) < RS32(PL + 8) ? 0xFFFF0000u : 0x00010000u);
    } else {
        int32_t d = RS32(o + 8) - HOME(o);
        if (d < 0) d = HOME(o) - RS32(o + 8);
        if (d < 0x00010000) {
            W32(o + 8, (uint32_t)HOME(o));
            W16(o + 6, 0);
            W16(o + 0x14, 30);
        } else W32(o + 0x30, RS32(o + 8) < HOME(o) ? 0x00010000u : 0xFFFF0000u);
    }
}

/* actions 12/13 airborne: hold the first frame */
static void Guard4_HoldInAir(void)
{
    uint32_t o = O;
    if (R16(o + 2) & 0x4000) {
        AND16(o + 0x58, 0x7FFF);
        W16(o + 0x5A, 0);
        W16(o + 0x72, 0);
    }
}

/* action 16: back-flip */
static void Guard4_Flip(void)
{
    uint32_t o = O;
    if ((R16(o + 0x58) & 3) == 1) {
        AND16(o + 0x58, 0xFFFC);
        W32(o + 0x30, (R16(o + 2) & 0x8000) ? 0x00030000u : 0xFFFD0000u);
        W32(o + 0x34, 0x00040000u);
    } else if (!(R16(o + 2) & 0x4000)) W16(o + 6, 17);
}

/* action 18: guarded, pushed back */
static void Guard4_Guarded(void)
{
    uint32_t o = O;
    if ((R16(o + 0x58) & 3) == 1)
        W32(o + 0x30, (R16(o + 2) & 0x8000) ? 0x00020000u : 0xFFFE0000u);
}

/* action 4: falling */
static void Guard4_Fall(void)
{
    uint32_t o = O;
    if (R16(o + 2) & 0x4000) W32(o + 0x30, SHL12(RS16(o + 0x4E)));
    else W16(o + 6, 5);
}

/* hurt animation done (action 11) */
static void Guard4_HurtDone(uint16_t next, int setTimer)
{
    uint32_t o = O;
    if (R16(o + 2) & 0x4000) {
        AND16(o + 0x58, 0x7FFF);
        W16(o + 0x5A, 0);
        W16(o + 0x72, 0);
    } else {
        AND16(o + 2, 0xFFF7);
        W16(o + 6, next);
        if (setTimer) W16(o + 0x14, 30);
    }
}

/* the end of action 8/17: back off if Sho is close, else guard */
static void Guard4_AfterSlash(void)
{
    uint32_t o = O;
    if (iabs(RS32(o + 8) - RS32(PL + 8)) < 0x003C0000) { W16(o + 6, 15); OR16(o + 4, 2); }
    else { W16(o + 6, 0); W16(o + 0x14, 30); }
}

/* 0x060681B8 / 0x06068F78 / 0x060696E4: cut-down overlay. 1 = deleted. */
static int Guard4_Dying(void)
{
    uint32_t o = O;
    if ((R16(o + 0x58) & 3) != 1 || R16(o + 6) != 13) return 0;
    Anim_Start(ST, DEF4, SCRIPT4, CHAR_BASE(SLOT), 14);
    Enemy_DrawOverlay(ST, (int16_t)((uint16_t)COLOR_BANK(SLOT) + 1), o + 0x52, o + 0x54, o + 0x56);
    if (R16(ST) & 0x8000) {
        Enemy_DeathBurst(o);
        Score_Add(200);
        W16(o, 0xFFFF);
        return 1;
    }
    return 0;
}

static void Guard4_Draw(int stage6)
{
    uint32_t o = O;
    W16(SPR + 2, (uint32_t)((RS32(o + 8) - RS32(CAM_X)) >> 16));
    W16(SPR + 4, (uint32_t)((RS32(o + 0xC) - RS32(CAM_Y)) >> 16));
    W16(SPR + 0x10, R16(o + 2) & 0x8000);
    W16(SPR + 0x0A, (uint32_t)COLOR_BANK(SLOT));
    W16(SPR + 0x0C, 0);
    W16(SPR + 0x0E, 0);
    if (R16(o + 6) == 12) W16(SPR + 0x0E, R16(o + 0x58) & 3);
    else if (R16(o + 6) == 13) W16(SPR + 0x0E, R16(ST) & 3);
    if (stage6 && STAGE == 6 && R16(o + 6) == 19) {
        OR16(SPR + 0x10, 0x0200);
        W32(SPR + 0x18, 0xFFFFFF7Fu);
    }
    Anim_Tick(o + 0x58);
}

/* ------------------------------------------------------- subtype 1 */

/* 0x0606910C */
static void Guard4_Sub1Init(void)
{
    uint32_t o = O;
    W16(o + 6, 0);
    if (STAGE == 6) W16(o + 6, 19);
    Anim_Start(o + 0x58, DEF4, SCRIPT4, CHAR_BASE(SLOT), RS16(o + 6));
    W16(o + 0x48, R32(o + 8) >> 16);
    AND16(o + 2, 0xFFFE);
    W16(o + 0x16, 2);
}

/* 0x06068EFC jump table: action of a subtype-1 guard. */
static void Guard4_Sub1Act(void)
{
    uint32_t o = O;
    switch (R16(o + 6)) {
    case 0:                                                           /* 0x06068768 */
        Guard4_FacePlayer();
        Guard4_CheckAttack();
        if ((R16(o + 0x58) & 3) != 1) return;
        if (iabs(RS32(PL + 8) - HOME(o)) >= 0x01400000 && (int16_t)(R32(o + 8) >> 16) != RS16(o + 0x48)) {
            if (HOME(o) < RS32(o + 8) && RS32(o + 8) < RS32(PL + 8)) { W16(o + 6, 15); AND16(o + 4, 0xFFFD); return; }
            if (HOME(o) > RS32(o + 8) && RS32(o + 8) > RS32(PL + 8)) { W16(o + 6, 15); AND16(o + 4, 0xFFFD); return; }
        } else if (iabs(RS32(o + 8) - RS32(PL + 8)) < 0x003C0000) {
            W16(o + 6, 15); OR16(o + 4, 2);
            return;
        }
        /* 0x06068862 */
        if (iabs(RS32(o + 8) - RS32(PL + 8)) < 0x00E60000 &&
            RS32(o + 0xC) - RS32(PL + 0xC) >= (int32_t)0xFFFD0000 && RS32(o + 0xC) - RS32(PL + 0xC) < 0x00640000)
            W16(o + 6, 9);
        return;
    case 2:                                                           /* 0x060688A6: slash ready */
        if ((R16(o + 0x58) & 3) == 1) {
            uint16_t want = RS32(o + 8) >= RS32(PL + 8) ? 0x8000 : 0;
            if ((R16(o + 2) & 0x8000) != want) { W16(o + 6, 3); return; }
            if (iabs(RS32(o + 8) - RS32(PL + 8)) >= 0x00650000) { W16(o + 6, 3); return; }
            if (iabs(RS32(o + 8) - RS32(PL + 8)) < 0x003C0000) { W16(o + 6, 3); return; }
        }
        /* 0x0606892E */
        if (iabs(RS32(o + 8) - RS32(PL + 8)) < 0x00650000 && RS32(o + 0xC) - RS32(PL + 0xC) < 0x00280000) {
            W16(o + 0x14, R16(o + 0x14) - 1);
            if (R16(o + 0x14) == 0) W16(o + 6, 10);
        } else W16(o + 0x14, 1);
        return;
    case 4: Guard4_Fall(); return;
    case 6:                                                           /* 0x060689FC */
        if (R16(o + 4) & 1) Guard4_WallJumpAir();
        return;
    case 9:                                                           /* 0x06068B18: run at Sho */
        Guard4_FacePlayer();
        W32(o + 0x30, (R16(o + 2) & 0x8000) ? 0xFFFE8000u : 0x00018000u);
        if (RS32(o + 0x34) != 0) { W16(o + 6, 4); W16(o + 0x4E, 0); return; }
        Guard4_CheckAttack();
        if (Guard4_WallJump()) return;
        if (iabs(RS32(o + 8) - RS32(PL + 8)) < 0x00650000) { W16(o + 6, 1); return; }
        if (iabs(RS32(PL + 8) - HOME(o)) >= 0x01400000) {
            if (HOME(o) < RS32(o + 8) && RS32(o + 8) < RS32(PL + 8)) { W16(o + 6, 15); AND16(o + 4, 0xFFFD); return; }
            if (HOME(o) > RS32(o + 8) && RS32(o + 8) > RS32(PL + 8)) { W16(o + 6, 15); AND16(o + 4, 0xFFFD); }
        }
        return;
    case 12: Guard4_HoldInAir(); return;
    case 13: W16(ST, 0); Guard4_HoldInAir(); return;
    case 15: Guard4_Walk(); return;
    case 16: Guard4_Flip(); return;
    case 18: Guard4_Guarded(); return;
    case 19: Guard4_FacePlayer(); return;
    default: return;
    }
}

/* 0x060683DC */
static void Guard4_Sub1(void)
{
    uint32_t o = O;
    if (R16(o + 2) & 1) Guard4_Sub1Init();
    if (Guard4_Asleep()) return;
    if (!FROZEN) {
        if ((int16_t)Enemy_AttackPlayer(1, 0, 0xC000) == 3) sub_06007CDE(0x10C);
        int16_t r = (int16_t)Enemy_CheckPlayerHits(0xC000);
        if (r == 1 && !(R16(O + 2) & 0x4000)) W16(o + 6, 18);
        Guard4_HitReact(r);                                           /* (no sound here) */
        if (!(R16(o + 2) & 0x4000)) W32(o + 0x30, 0);
        if (R16(o + 0x58) & 0x8000) {
            switch (R16(o + 6)) {
            case 1: W16(o + 6, 2); W16(o + 0x14, 20); break;
            case 3: case 17: case 19: W16(o + 6, 0); W16(o + 0x14, 30); break;
            case 5: W16(o + 6, 3); break;
            case 10: W16(o + 6, 2); W16(o + 0x14, 60); break;
            case 11: Guard4_HurtDone(0, 1); break;
            case 12:
                Enemy_DeathBurst(o);
                Score_Add(200);
                W16(o, 0xFFFF);
                return;
            case 18: W16(o + 6, 0); W16(o + 0x14, 10); break;
            default: break;
            }
        } else Guard4_Sub1Act();
        /* 0x06068F38 */
        W16(o + 0x1E, R16(o + 6) != 0 ? 3 : 1);
        Obj_Integrate(o);
        Guard4_ClampX(o);
        Obj_CollideMap(o, RS16(o + 0x1E), 8);
        Enemy_AnimSwitch();
    }
    if (Guard4_Dying()) return;
    Guard4_Draw(1);
}

/* ------------------------------------------------------- subtype 2 */

/* 0x06069850 */
static void Guard4_Sub2Init(void)
{
    uint32_t o = O;
    W16(o + 6, 2);
    W16(o + 0x14, 2);
    Anim_Start(o + 0x58, DEF4, SCRIPT4, CHAR_BASE(SLOT), RS16(o + 6));
    W16(o + 0x16, 2);
    AND16(o + 2, 0xFFFE);
    if (RS32(o + 8) < RS32(PL + 8)) AND16(o + 2, 0x7FFF); else OR16(o + 2, 0x8000);
}

/* 0x06069216 */
static void Guard4_Sub2(void)
{
    uint32_t o = O;
    if (R16(o + 2) & 1) Guard4_Sub2Init();
    if (STAGE == 8) {                                                 /* far behind: gone */
        int32_t d = RS32(o + 8) - RS32(CAM_X) + (int32_t)0xFF600000;
        if (d < 0) d = RS32(CAM_X) - RS32(o + 8) + 0x00A00000;
        if (d > 0x02000000) { W16(o, 0xFFFF); return; }
    }
    if (Guard4_Asleep()) return;
    if (!FROZEN) {
        if ((int16_t)Enemy_AttackPlayer(1, 0, 0xC000) == 3) {
            sub_06007CDE(0x10C);
            OR16(PL + 4, 0x40);
        }
        int16_t r = (int16_t)Enemy_CheckPlayerHits(0xC000);
        if (Guard4_HitReact(r)) sub_06007CDE(0x206);
        W32(o + 0x30, 0);
        if (R16(o + 0x58) & 0x8000) {
            switch (R16(o + 6)) {
            case 1: case 5: W16(o + 6, 2); W16(o + 0x14, 2); break;
            case 10: W16(o + 6, 2); W16(o + 0x14, 60); break;
            case 11: Guard4_HurtDone(1, 0); break;
            case 12:
                Enemy_DeathBurst(o);
                Score_Add(200);
                W16(o, 0xFFFF);
                return;
            default: break;
            }
        } else {
            switch (R16(o + 6)) {
            case 2:                                                   /* 0x0606954C */
                if (RS32(o + 0x34) != 0) { W16(o + 6, 4); break; }
                if (iabs(RS32(o + 8) - RS32(PL + 8)) < 0x00690000) {
                    W16(o + 0x14, R16(o + 0x14) - 1);
                    if (R16(o + 0x14) == 0) W16(o + 6, 10);
                    else Guard4_FacePlayerX();
                } else W16(o + 0x14, 1);
                break;
            case 4:                                                   /* 0x060695CC */
                Guard4_FacePlayerX();
                if (!(R16(o + 2) & 0x4000)) W16(o + 6, 5);
                break;
            case 12: Guard4_HoldInAir(); break;
            case 13: W16(ST, 0); Guard4_HoldInAir(); break;
            default: break;
            }
        }
        /* 0x06069680 */
        W16(o + 0x1E, R16(o + 6) != 0 ? 3 : 1);
        Obj_Integrate(o);
        if (STAGE == 8) {                                             /* no background collision layer */
            uint16_t keep = R16(G_COLL_LAYER);
            W16(G_COLL_LAYER, 0xFFFF);
            Obj_CollideMap(o, RS16(o + 0x1E), 8);
            W16(G_COLL_LAYER, keep);
        } else Obj_CollideMap(o, RS16(o + 0x1E), 8);
        Enemy_AnimSwitch();
    }
    if (Guard4_Dying()) return;
    Guard4_Draw(0);
}

/* ------------------------------------------------------- subtype 0 */

/* 0x0606834C */
static void Guard4_Init(void)
{
    uint32_t o = O;
    W16(o + 6, 0);
    W16(o + 0x14, 2);
    if (STAGE == 6) { W16(o + 6, 19); W16(o + 0x14, 32); }
    Anim_Start(o + 0x58, DEF4, SCRIPT4, CHAR_BASE(SLOT), RS16(o + 6));
    W16(o + 0x16, 2);
    W16(o + 0x48, R32(o + 8) >> 16);
    AND16(o + 2, 0xFFFE);
}

/* 0x06067802 jump table: action of a subtype-0 guard. */
static void Guard4_Act(void)
{
    uint32_t o = O;
    switch (R16(o + 6)) {
    case 0:                                                           /* 0x06067834 */
        Guard4_FacePlayer();
        Guard4_CheckAttack();
        if (iabs(RS32(PL + 8) - HOME(o)) >= 0x00E60000 && (int16_t)(R32(o + 8) >> 16) != RS16(o + 0x48)) {
            if (HOME(o) < RS32(o + 8) && RS32(o + 8) < RS32(PL + 8)) { W16(o + 6, 15); AND16(o + 4, 0xFFFD); return; }
            if (HOME(o) > RS32(o + 8) && RS32(o + 8) > RS32(PL + 8)) { W16(o + 6, 15); AND16(o + 4, 0xFFFD); return; }
        } else if (iabs(RS32(o + 8) - RS32(PL + 8)) < 0x003C0000) {
            W16(o + 6, 15); OR16(o + 4, 2);
            return;
        }
        /* 0x0606791E */
        if (iabs(RS32(o + 8) - RS32(PL + 8)) >= 0x00E60000) return;
        if (RS32(o + 0xC) - RS32(PL + 0xC) < (int32_t)0xFFFD0000) return;
        if (RS32(o + 0xC) - RS32(PL + 0xC) >= 0x00640000) return;
        if ((R16(o + 2) & 0x0800) && (R16(o + 0x58) & 3) == 1) { W16(o + 6, 1); return; }
        if (iabs(RS32(o + 8) - RS32(PL + 8)) >= 0x00960000) { W16(o + 6, 9); return; }
        W16(o + 0x14, R16(o + 0x14) - 1);
        if (R16(o + 0x14) == 0) {
            Guard4_SolveJump(0x00070000, 0x00090000, 81);
            W16(o + 6, 6);
            AND16(o + 4, 0xFFFE);
        }
        return;
    case 4: Guard4_Fall(); return;
    case 6:                                                           /* 0x06067A26: jump */
        if (R16(o + 4) & 1) { Guard4_WallJumpAir(); return; }
        if (R16(o + 2) & 0x4000) {
            if (!(RS32(o + 0x34) > 0)) {                               /* coming down: air slash? */
                if (R16(o + 2) & 0x8000) {
                    if (iabs(RS32(o + 0xC) - RS32(PL + 0xC)) < 0x00640000 &&
                        RS32(o + 8) - RS32(PL + 8) < 0x003C0000 && RS32(o + 8) - RS32(PL + 8) >= 0) { W16(o + 6, 7); return; }
                } else {
                    if (iabs(RS32(o + 0xC) - RS32(PL + 0xC)) < 0x00640000 &&
                        RS32(PL + 8) - RS32(o + 8) < 0x003C0000 && RS32(PL + 8) - RS32(o + 8) >= 0) { W16(o + 6, 7); return; }
                }
            }
        } else if ((R16(o + 0x58) & 3) == 2) W16(o + 6, 5);
        /* 0x06067BBC: take-off frame */
        if ((R16(o + 0x58) & 3) == 1) {
            W32(o + 0x34, SHL12(RS16(o + 0x4C)));
            W32(o + 0x30, SHL12(RS16(o + 0x4E)));
            if (RS32(o + 0x34) < 0x00070000) W32(o + 0x34, 0x00070000u);
            else if (RS32(o + 0x34) > 0x00090000) W32(o + 0x34, 0x00090000u);
            AND16(o + 0x58, 0xFFFC);
            OR16(o + 0x58, 2);
        }
        return;
    case 7:                                                           /* 0x06067CA0 */
        if (!(R16(o + 2) & 0x4000) && (R16(o + 0x58) & 3) == 1) W16(o + 6, 8);
        return;
    case 9:                                                           /* 0x06067CCE: run at Sho */
        Guard4_FacePlayer();
        W32(o + 0x30, (R16(o + 2) & 0x8000) ? 0xFFFE8000u : 0x00018000u);
        if (RS32(o + 0x34) != 0) { W16(o + 6, 4); W16(o + 0x4E, 0); return; }
        Guard4_CheckAttack();
        if (Guard4_WallJump()) return;
        if (iabs(RS32(o + 8) - RS32(PL + 8)) < 0x00960000) {
            Guard4_SolveJump(0x00070000, 0x00090000, 81);
            W16(o + 6, 6);
            AND16(o + 4, 0xFFFE);
            return;
        }
        if (iabs(RS32(PL + 8) - HOME(o)) >= 0x01400000) {
            if (HOME(o) < RS32(o + 8) && RS32(o + 8) < RS32(PL + 8)) { W16(o + 6, 15); AND16(o + 4, 0xFFFD); return; }
            if (HOME(o) > RS32(o + 8) && RS32(o + 8) > RS32(PL + 8)) { W16(o + 6, 15); AND16(o + 4, 0xFFFD); }
            return;
        }
        if (iabs(RS32(o + 8) - RS32(PL + 8)) < 0x003C0000) { W16(o + 6, 15); OR16(o + 4, 2); }
        return;
    case 12: Guard4_HoldInAir(); return;
    case 13: W16(ST, 0); Guard4_HoldInAir(); return;
    case 15: Guard4_Walk(); return;
    case 16: Guard4_Flip(); return;
    case 18: Guard4_Guarded(); return;
    case 19: Guard4_FacePlayer(); return;
    default: return;
    }
}

/* 0x060673C6 */
void EnemyHandler_0004(void)
{
    uint32_t o = O;
    uint16_t sub = R16(o + 0x12) & 0x1F;
    if (sub == 1) { Guard4_Sub1(); return; }
    if (sub == 2) { Guard4_Sub2(); return; }
    if (R16(o + 2) & 1) Guard4_Init();
    if (Guard4_Asleep()) return;
    if (!FROZEN) {
        if ((int16_t)Enemy_AttackPlayer(1, 0, 0xC000) == 3) sub_06007CDE(0x10C);
        int16_t r = (int16_t)Enemy_CheckPlayerHits(0xC000);
        if (r == 1 && !(R16(O + 2) & 0x4000)) W16(o + 6, 18);          /* guarded */
        if (Guard4_HitReact(r)) sub_06007CDE(0x206);
        if (!(R16(o + 2) & 0x4000)) W32(o + 0x30, 0);
        if (R16(o + 0x58) & 0x8000) {                                  /* animation done */
            switch (R16(o + 6)) {
            case 1: case 10: W16(o + 6, 2); W16(o + 0x14, 60); break;
            case 3: case 19: W16(o + 6, 0); W16(o + 0x14, 30); break;
            case 5: W16(o + 6, 3); break;
            case 8: case 17: Guard4_AfterSlash(); break;
            case 11: Guard4_HurtDone(0, 1); break;
            case 12:
                Enemy_DeathBurst(o);
                Score_Add(200);
                W16(o, 0xFFFF);
                return;
            case 18: W16(o + 6, 0); W16(o + 0x14, 10); break;
            default: break;
            }
        } else Guard4_Act();
        /* 0x06068178 */
        W16(o + 0x1E, R16(o + 6) != 0 ? 3 : 1);
        Obj_Integrate(o);
        Obj_CollideMap(o, RS16(o + 0x1E), 8);
        Guard4_ClampX(o);
        Enemy_AnimSwitch();
    }
    if (Guard4_Dying()) return;
    Guard4_Draw(1);
}
