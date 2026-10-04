/* Helpers shared by the enemy types of enemy_t02.c, enemy_t22.c, ... (see enemy_more.h). */
#include "enemy_more.h"
#include "anim.h"
#include "player_ext.h"
#include "objects.h"
#include "fx.h"

int EM_Far(uint32_t o, int32_t yc)
{
    int32_t d = RS32(o + 8) - RS32(CAM_X) + (int32_t)0xFF600000;
    if (d < 0) d = RS32(CAM_X) - RS32(o + 8) + 0x00A00000;
    if (d >= 0x01000000) return 1;
    d = RS32(o + 0xC) - RS32(CAM_Y) - yc;
    if (d < 0) d = RS32(CAM_Y) - RS32(o + 0xC) + yc;
    return d >= 0x01000000;
}

void EM_FaceSho(uint32_t o)
{
    AND16(o + 2, 0x7FFF);
    if (RS32(PL + 8) < RS32(o + 8)) OR16(o + 2, 0x8000);
}

void EM_PlayerFaceBack(uint32_t o)
{
    if (!(R16(o + 2) & 0x8000)) OR16(PL + 2, 0x8000); else AND16(PL + 2, 0x7FFF);
}

/* 0x06097130: frames n to climb to Sho's height under gravity 0x5000 (clamped to
 * vmin/g..vmax/g, plus the fall below h px), then the horizontal speed to land 24 px
 * in front of him. Same code as enemy_t4.c's local copy. */
void EM_SolveJump(int32_t vmin, int32_t vmax, int16_t h)
{
    uint32_t o = O;
    const int32_t g = 0x5000;
    int32_t n = 0, v = 0, s = 0;
    int32_t dy = RS32(o + 0xC) - RS32(PL + 0xC);
    if (!(0 >= dy)) {
        do { v += g; s += v; n++; } while (!(s >= dy));
    }
    W16(o + 0x4C, (uint16_t)(v >> 12));
    if ((int16_t)n < EM_Sdiv(vmin, g)) n = (int16_t)EM_Sdiv(vmin, g);
    else if ((int16_t)n > EM_Sdiv(vmax, g)) n = (int16_t)EM_Sdiv(vmax, g);
    int32_t y = (int32_t)((uint32_t)(int32_t)h << 16);
    if (!(dy >= y) && y > dy) {
        int32_t u = 0;
        do { y += u; u -= g; n++; } while (y > dy);
    }
    int32_t x = (R16(o + 2) & 0x8000) ? 0x00180000 : (int32_t)0xFFE80000;
    x = x + RS32(PL + 8) - RS32(o + 8);
    W16(o + 0x4E, (uint16_t)(int16_t)(EM_Sdiv(x, (int16_t)n) >> 12));
}

void EM_ClampX(uint32_t o)
{
    if (RS32(o + 8) < RS32(0x060EA618u) + 0x00180000) W32(o + 8, (uint32_t)(RS32(0x060EA618u) + 0x00180000));
    else if (RS32(o + 8) > RS32(0x060EA620u) + (int32_t)0xFFE80000) W32(o + 8, (uint32_t)(RS32(0x060EA620u) + (int32_t)0xFFE80000));
}

void EM_OverlayStart(uint32_t defs, uint32_t scripts, int16_t anim, uint32_t a, uint32_t b, uint32_t c)
{
    Anim_Start(SCRATCH_ANIM, defs, scripts, CHAR_BASE(SLOT), anim);
    W16(a, R16(SCRATCH_ANIM + 2));
    W16(b, R16(SCRATCH_ANIM + 26));
    W16(c, R16(SCRATCH_ANIM));
}

int EM_OverlayDraw(uint32_t defs, uint32_t scripts, int16_t anim, uint32_t a, uint32_t b, uint32_t c)
{
    Anim_Start(SCRATCH_ANIM, defs, scripts, CHAR_BASE(SLOT), anim);
    Enemy_DrawOverlay(SCRATCH_ANIM, (int16_t)(COLOR_BANK(SLOT) + 1), a, b, c);
    return (R16(SCRATCH_ANIM) & 0x8000) != 0;
}

void EM_SprBasic(uint32_t o, int16_t bank)
{
    W16(SPR + 2, (uint32_t)((RS32(o + 8) - RS32(CAM_X)) >> 16));
    W16(SPR + 4, (uint32_t)((RS32(o + 0xC) - RS32(CAM_Y)) >> 16));
    W16(SPR + 0x10, R16(o + 2) & 0x8000);
    W16(SPR + 0x0A, (uint16_t)bank);
    W16(SPR + 0x0C, 0);
    W16(SPR + 0x0E, 0);
}

/* ------------------------------------------------- dispatch (enemy.c calls these) */
void Enemy2_Load(void);    void EnemyHandler_0002(void);   /* enemy_t02.c 0x06096290 / 0x0609630A */
void EnemyF_Load(void);    void EnemyHandler_000F(void);   /* enemy_t02.c 0x0606FE7C / 0x0606FF64 */
void Enemy23_Load(void);   void EnemyHandler_0023(void);   /* enemy_t23.c 0x06095310 / 0x0609535E */
void Enemy5_Load(void);    void EnemyHandler_0005(void);   /* enemy_t05.c 0x06098130 / 0x06098164 */
void Enemy7_Load(void);    void EnemyHandler_0007(void);   /* enemy_t07.c 0x060991D0 / 0x06099254 */
void Enemy16_Load(void);   void EnemyHandler_0016(void);   /* enemy_t16.c 0x06073E2C / 0x06073EC4 */
void Enemy22_Load(void);   void EnemyHandler_0022(void);   /* enemy_t22.c 0x0608C050 / 0x0608C084 */
void Enemy24_Load(void);   void EnemyHandler_0024(void);   /* enemy_t24.c 0x06093B34 / 0x06093B96 */
uint16_t Enemy18_Load(void); void EnemyHandler_0018(void); /* enemy_boss18.c 0x0609FFF4 / 0x060A00EA */
void Enemy15_Load(void);   void EnemyHandler_0015(void);   /* enemy_boss15.c 0x06071B24 / 0x06071E04 */

int EnemyMore_Load(uint16_t t)
{
    switch (t) {
    case 0x02: Enemy2_Load(); return 1;
    case 0x0F: EnemyF_Load(); return 1;
    case 0x23: Enemy23_Load(); return 1;
    case 0x05: Enemy5_Load(); return 1;
    case 0x07: Enemy7_Load(); return 1;
    case 0x16: Enemy16_Load(); return 1;
    case 0x22: Enemy22_Load(); return 1;
    case 0x24: Enemy24_Load(); return 1;
    case 0x18: Enemy18_Load(); return 1;
    case 0x15: Enemy15_Load(); return 1;
    default: return 0;
    }
}

int EnemyMore_Handler(uint16_t t)
{
    switch (t) {
    case 0x02: EnemyHandler_0002(); return 1;
    case 0x0F: EnemyHandler_000F(); return 1;
    case 0x23: EnemyHandler_0023(); return 1;
    case 0x05: EnemyHandler_0005(); return 1;
    case 0x07: EnemyHandler_0007(); return 1;
    case 0x16: EnemyHandler_0016(); return 1;
    case 0x22: EnemyHandler_0022(); return 1;
    case 0x24: EnemyHandler_0024(); return 1;
    case 0x18: EnemyHandler_0018(); return 1;
    case 0x15: EnemyHandler_0015(); return 1;
    default: return 0;
    }
}

/* ------------------------------------------------------------- effects */

/* Effect 0xE (0x0605B08A, spawned by 0x0605B04C): water splash thrown up by soldier
 * subtype 5 (anim 0xE of 0x060AC050 / 0x060D29AC, chars 0x10, bank 0x20, colour
 * calculation 0x81); rises at 10 px/frame under gravity, animated every other frame. */
void Fx_Splash0E(uint32_t e)
{
    if (R16(e + 2) & 1) {
        Anim_Start(e + 0x10, 0x060AC050u, 0x060D29ACu, 0x10, 0xE);
        AND16(e + 2, 0xFFFE);
        sub_06007CDE(0x204);
    }
    W16(e + 6, R16(e + 6) + 1);
    if (!FROZEN) W32(e + 0xC, R32(e + 0xC) - ((uint32_t)R16(e + 6) * (uint32_t)-0x5000 + 0xA0000u));
    W16(SPR + 2, (uint32_t)((RS32(e + 8) - RS32(CAM_X)) >> 16));
    W16(SPR + 4, (uint32_t)((RS32(e + 0xC) - RS32(CAM_Y)) >> 16));
    W16(SPR + 0x0A, 0x20);
    W16(SPR + 0x10, 0x200);
    W16(SPR + 0x0C, 1);
    W16(SPR + 0x0E, 0);
    W32(SPR + 0x18, 0x81);
    if (R16(e) != 0xFFFF && (R16(e + 6) & 1)) Anim_Tick(e + 0x10);
    if (R16(e + 0x10) & 0x8000) W16(e, 0xFFFF);
}

/* ------------------------------------------------------------- projectiles */

/* 0x06060E8E (local copy, as fx.c / enemy_boss42.c): object o's kind-ko hitboxes vs
 * projectile p's kind-kp ones (index pair or 0xFFFF, HIT_POINT = overlap centre) */
uint16_t EM_ProjHitbox(uint32_t o, int16_t ko, uint32_t p, int16_t kp)
{
    if (EM_Abs(RS32(o + 8) - RS32(p + 8)) > 0x02000000) return 0xFFFF;
    if (EM_Abs(RS32(o + 0xC) - RS32(p + 0xC)) > 0x02000000) return 0xFFFF;
    const uint32_t TMP = 0x060FFE00u;                           /* PC scratch object view */
    W32(TMP + 8, R32(p + 8)); W32(TMP + 0xC, R32(p + 0xC));
    W16(TMP + 2, R16(p + 4) & 0x8000); W32(TMP + 0x68, R32(p + 0x30));
    return Hitbox_Test(o, ko, TMP, kp);
}

/* 0x06060F64 (local copy of enemy_boss42.c's proj_touch): the first object (Sho for a
 * hostile projectile) whose kind-ko boxes touch the projectile's kind-kp boxes */
uint16_t EM_ProjTouch(uint32_t p, int16_t kp, uint32_t *who, int16_t ko)
{
    uint16_t r = 0xFFFF;
    if (R16(p + 4) & 0x4000) return r;
    if (!(R16(p + 4) & 0x2000)) {
        uint32_t o = OBJ_TABLE;
        for (int n = 0xC0; n > 0; n--, o += 0x78) {
            if (R16(o) == 0xFFFF || (R16(o + 2) & 8)) continue;
            r = EM_ProjHitbox(o, ko, p, kp);
            if (r != 0xFFFF) { *who = o; return r; }
        }
        return r;
    }
    if (!(R16(PL + 2) & 8) && R16(R32(0x060EA41Cu)) == 0 && !(R16(PL + 4) & 0x0410)) {
        r = EM_ProjHitbox(PL, ko, p, kp);
        if (r != 0xFFFF) *who = PL;
    }
    return r;
}

/* Projectile 5 (0x060631CE): explosion (common anim 10 of 0x060ACCFE / 0x060D8040, chars
 * 0x4B0, bank 0x71) left by bursting fireballs / barrels; hurts Sho for 2 while it lasts. */
void Proj_Explosion05(uint32_t e)
{
    if (R16(e + 4) & 1) {
        Anim_Start(e + 0x20, 0x060ACCFEu, 0x060D8040u, 0x4B0, 10);
        AND16(e + 4, 0xFFFE);
        sub_06007CDE(0x121);
    }
    if (!FROZEN) {
        W16(e + 0x12, 0x7F);
        Projectile_Collide(2, 0xC100);
        if (R16(e + 0x20) & 0x8000) W16(e, 0xFFFF);
    }
    W16(SPR + 2, (uint32_t)((RS32(e + 8) - RS32(CAM_X)) >> 16));
    W16(SPR + 4, (uint32_t)((RS32(e + 0xC) - RS32(CAM_Y)) >> 16));
    W16(SPR + 0x0A, 0x71);
    W16(SPR + 0x10, 0x200);
    W32(SPR + 0x18, 0x81);
    W16(SPR + 0x0C, 0);
    W16(SPR + 0x0E, 0);
    if (R16(e) == 0xFFFF) return;
    Anim_Tick(e + 0x20);
}

/* ------------------------------------------------------- afterimage trail */

/* Afterimage trail of the bosses (SHINOBI 0x06049BA4 / 0x06049BF0 / 0x06049C10): 8 copies
 * of an object's animation state (0x060F206C, 0x20 bytes each; flags bit 0 = mirrored),
 * positions 0x060F216C / 0x060F218C, extra {u32 flags, u16 bank, u16 rotation} 0x060F21AC,
 * step counter 0x060F21EC (a new image every 4 frames). Copies use A.BIN 0x0600A6CC. */
#define TR_ANIM  0x060F206Cu
#define TR_X     0x060F216Cu
#define TR_Y     0x060F218Cu
#define TR_EXT   0x060F21ACu
#define TR_STEP  0x060F21ECu

static void copy_mem(uint32_t dst, uint32_t src, uint32_t n)   /* A.BIN 0x0600A6CC */
{
    for (uint32_t k = 0; k < n; k += 2) W16(dst + k, R16(src + k));
}

void EM_TrailReset(void)                                    /* 0x06049BA4 */
{
    W16(TR_STEP, 0);
    for (uint32_t i = 0; i < 8; i++) {
        uint32_t a = TR_ANIM + i * 0x20;
        Anim_Reset(a);
        AND16(a, 0xFFF0);
    }
}

void EM_Trail(int active, uint32_t o)                       /* 0x06049BF0 -> 0x06049C10 */
{
    const uint32_t EXTRA = 0x060FFE40u;                     /* PC scratch: the zeroed 8 bytes */
    W32(EXTRA, 0); W32(EXTRA + 4, 0);
    if (!FROZEN) {
        if (RS16(TR_STEP) != 0) W16(TR_STEP, R16(TR_STEP) - 1);
        else {
            W16(TR_STEP, 3);
            for (int16_t i = 7; i != 0; i--) {
                uint32_t a = TR_ANIM + (uint32_t)i * 0x20, b = a - 0x20;
                copy_mem(a, b, 0x20);
                if (active) {
                    copy_mem(TR_ANIM, o + 0x58, 0x20);
                    if (R16(o + 2) & 0x8000) OR16(TR_ANIM, 1); else AND16(TR_ANIM, 0xFFFE);
                } else if (R16(b + 0x18) != 0xFFFF) { Anim_Reset(b); AND16(b, 0xFFF0); }
                W32(TR_X + 4u * (uint32_t)i, R32(TR_X + 4u * (uint32_t)(i - 1)));
                if (active) W32(TR_X, R32(o + 8));
                W32(TR_Y + 4u * (uint32_t)i, R32(TR_Y + 4u * (uint32_t)(i - 1)));
                if (active) W32(TR_Y, R32(o + 0xC));
                copy_mem(TR_EXT + 8u * (uint32_t)i, TR_EXT + 8u * (uint32_t)(i - 1), 8);
                if (active) copy_mem(TR_EXT, EXTRA, 8);
            }
        }
    }
    for (int16_t i = 0; i < 8; i++) {
        uint32_t a = TR_ANIM + (uint32_t)i * 0x20, ex = TR_EXT + 8u * (uint32_t)i;
        if (R16(a + 0x18) == 0xFFFF) continue;
        copy_mem(SCRATCH_ANIM2, a, 0x20);
        W16(SPR + 2, (uint32_t)((RS32(TR_X + 4u * (uint32_t)i) - RS32(CAM_X)) >> 16));
        W16(SPR + 4, (uint32_t)((RS32(TR_Y + 4u * (uint32_t)i) - RS32(CAM_Y)) >> 16));
        uint16_t fl = (R16(SCRATCH_ANIM2) & 1) ? 0x8000 : 0;
        W16(SPR + 0x10, fl | 0x200);
        if (R16(ex + 6) != 0 && (R32(ex) & 2)) { W16(SPR + 0x10, fl | 0x600); W16(SPR + 0x14, R16(ex + 6)); }
        if (R32(ex) & 1) W16(SPR + 0x0A, R16(ex + 4));
        else W16(SPR + 0x0A, R16(0x060A9084u + 2u * (uint32_t)(int16_t)Enemy_FindSlot((int16_t)R16(o))));
        W32(SPR + 0x18, (uint32_t)(-1 - i));
        W16(SPR + 0x0C, 0);
        W16(SPR + 0x0E, (uint16_t)(i >> 1));
        Anim_Tick(SCRATCH_ANIM2);
    }
}
