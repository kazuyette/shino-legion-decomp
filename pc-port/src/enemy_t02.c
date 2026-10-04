/* Enemy types 0x0002 and 0x000F (stage 1-1, 4-1): jumping ninjas, ported from SHINOBI.
 *  type 2  loader 0x06096290, handler 0x0609630A (subtype 1 → 0x0609723A)
 *  type F  loader 0x0606FE7C, handler 0x0606FF64 (subtype 1 → 0x06070E08)
 * The four handlers are variations of one template. The ninja faces Sho, walks towards
 * him (dodging a shuriken in flight with a back-flip), and when close enough or when
 * the think timer +0x14 runs out jumps at him (EM_SolveJump 0x06097130: +0x4C/+0x4E
 * vertical/horizontal speed >> 12) slashing on the way down. A wall in the way (+0x10
 * bits 8/4) makes it hop (+0x4E = +-0x20). Guarding Sho's sword -> recoil (0x10/0x11).
 * 2 HP; killed by a heavy attack (Sho's attack kind 4/5) -> knocked away (anim 9);
 * otherwise cut down: explosion overlay (anim 0xB, kept in +0x48/+0x50/+0x52).
 * 200 points. Subtype 1 of type 2 respawns: it waits (action 0xF, hidden) at its home
 * (+0x54/+0x56 px) until Sho is within 236 px, then drops in from the top of the
 * screen; after a death it waits until Sho has moved 320 px away (+4 bit 1).
 * Actions: 0 stand, 1 walk, 2 hop, 3 fall, 4 land, 5 jump attack, 6/0xC (type F 0xD)
 * descend, 7 land, 8 hurt, 9 knocked away, 10 cut down, 0xD/0xE back-flip.
 * The ninja keeps "jump" decisions that the original overwrites (case 0: action 5 then 1)
 * exactly as the original does. */
#include "enemy_more.h"
#include "anim.h"
#include "sprite.h"
#include "collision.h"
#include "player_ext.h"
#include "fx.h"

#define T2_DEFS  0x060C1B04u
#define T2_SCR   0x060E531Cu
#define TF_DEFS  0x060B11ACu
#define TF_SCR   0x060DC364u

/* ------------------------------------------------------------------ loaders */

void Enemy2_Load(void)                                      /* 0x06096290 */
{
    Enemy_RegisterChars(2, 0x060E4EFCu);
    int16_t s = (int16_t)Enemy_FindSlot(2);
    int b = (uint16_t)COLOR_BANK(s);
    if (STAGE == 0) Pal_SetFaded((b & 0xFFF) << 4, 16, 0x060E50D0u, -0x38, -0x30, 0x10);
    else Pal_Copy((b & 0xFFF) << 4, 16, 0x060E50D0u);
    Pal_Copy((int16_t)(COLOR_BANK(s) + 1) * 16, 16, 0x060E50F0u);
}

void EnemyF_Load(void)                                      /* 0x0606FE7C */
{
    Enemy_RegisterChars(0xF, 0x060DBE88u);
    int16_t s = (int16_t)Enemy_FindSlot(0xF);
    int b = (uint16_t)COLOR_BANK(s);
    if (STAGE == 0) {
        Pal_SetFaded((b & 0xFFF) << 4, 16, 0x060DC0C4u, -0x20, -0x18, 0x18);
        Pal_SetFaded(((b + 2) & 0xFFFF) * 16, 16, 0x060DC0E4u, -0x20, -0x18, 0x18);
    } else {
        Pal_Copy((b & 0xFFF) << 4, 16, 0x060DC0C4u);
        Pal_Copy(((b + 2) & 0xFFFF) * 16, 16, 0x060DC0E4u);
    }
    Pal_Copy((int16_t)(COLOR_BANK(s) + 1) * 16, 16, 0x060E50F0u);
}

/* ------------------------------------------------------------------- common */

static void anim_reset(uint32_t o)            /* replay the current anim (still in the air) */
{
    AND16(o + 0x58, 0x7FFF);
    W16(o + 0x5A, 0);
    W16(o + 0x72, 0);
}

/* combat preamble shared by the four handlers; returns Enemy_CheckPlayerHits' result */
static int16_t combat(int16_t dmg, uint16_t recoil)
{
    uint32_t o = O;
    if ((int16_t)Enemy_AttackPlayer(dmg, 0, 0xC000) != 0) EM_PlayerFaceBack(o);
    int16_t r = (int16_t)Enemy_CheckPlayerHits(0xC000);
    if (r == 1 && !(R16(o + 2) & 0x4000)) W16(o + 6, recoil);
    return r;
}

/* hit taken: hurt (8), knocked away (9) or cut down (10, overlay anim 0xB) */
static void take_hit(int16_t r, uint32_t defs, uint32_t scr)
{
    uint32_t o = O;
    if (r == 2) W16(o + 0x4A, (uint16_t)Player_AttackKind()); else W16(o + 0x4A, 4);
    if (RS16(o + 0x16) < 1) {
        if (R16(o + 0x4A) == 4 || R16(o + 0x4A) == 5) W16(o + 6, 9);
        else {
            W16(o + 6, 10);
            EM_OverlayStart(defs, scr, 0xB, o + 0x48, o + 0x50, o + 0x52);
        }
    } else W16(o + 6, 8);
    Enemy_AnimSwitch();
    sub_06007CDE(0x205);
}

/* walking: a shuriken coming (checked every other 2 frames) -> back-flip */
static int shuriken_ahead(uint32_t o)
{
    if (!((R16(0x060143EAu) & 2) >> 1)) return 0;
    int32_t d = (R16(o + 2) & 0x8000) ? (int32_t)0xFFC00000 : 0x00400000;
    return Projectile_InBox(RS32(o + 8) + d, RS32(o + 0xC) + (int32_t)0xFFC00000, (d >> 8) + RS32(o + 8), RS32(o + 0xC)) != 0;
}

/* blocked by a wall (+0x10 bits 8 right / 4 left): hop over it (action `act`). 1 = done */
static int wall_hop(uint32_t o, uint16_t act)
{
    if (R16(o + 0x10) & 8) { AND16(o + 2, 0x7FFF); W16(o + 6, act); W16(o + 0x4E, 0x20); }
    else if (R16(o + 0x10) & 4) { OR16(o + 2, 0x8000); W16(o + 6, act); W16(o + 0x4E, 0xFFE0); }
    else return 0;
    return 1;
}

static int stuck(uint32_t o)
{
    return R16(o + 0x5A) > 2 && (int16_t)(R32(o + 8) >> 16) == (int16_t)(R32(o + 0x28) >> 16);
}

/* action 2: hop (rising while the anim is in phase 1 and X still blocked) */
static void act_hop(uint32_t o)
{
    if ((R16(o + 0x58) & 3) == 1) {
        int16_t t = (int16_t)(RS16(o + 0x14) - 1);
        W16(o + 0x14, (uint16_t)t);
        if (t == 0 || (int16_t)(R32(o + 8) >> 16) != (int16_t)(R32(o + 0x28) >> 16)) {
            AND16(o + 0x58, 0xFFFC); OR16(o + 0x58, 2);
        } else W32(o + 0x34, 0x00060000u);
    }
    if (!(R16(o + 2) & 0x4000)) { if ((R16(o + 0x58) & 3) == 2) W16(o + 6, 4); }
    else W32(o + 0x30, (uint32_t)((int32_t)RS16(o + 0x4E) << 12));
}

/* action 5: jump attack; descend (`desc`) when level with Sho or forced (+4 bit 0) */
static void act_jump(uint32_t o, int32_t lo, int32_t hi, uint16_t desc)
{
    if (!(R16(o + 2) & 0x4000)) {
        if ((R16(o + 0x58) & 3) == 2) { W16(o + 6, 7); AND16(o + 4, 0xFFFE); }
    } else {
        if (R16(o + 0x4E) == 0) EM_FaceSho(o);
        else if (((int32_t)RS16(o + 0x4E) << 12) < 1) OR16(o + 2, 0x8000);
        else AND16(o + 2, 0x7FFF);
        if (RS32(o + 0x34) < 0 && (EM_Abs(RS32(o + 0xC) - RS32(PL + 0xC)) < 0x00320000 || (R16(o + 4) & 1))) {
            if (((int32_t)RS16(o + 0x4C) << 12) < lo + 1) W16(o + 6, desc);
            else if (RS32(o + 0x34) < -0xF000) W16(o + 6, desc);
            else W16(o + 6, 6);
            AND16(o + 4, 0xFFFE);
        }
    }
    if ((R16(o + 0x58) & 3) == 1) {                             /* take off */
        AND16(o + 0x58, 0xFFFC); OR16(o + 0x58, 2);
        W32(o + 0x34, (uint32_t)((int32_t)RS16(o + 0x4C) << 12));
        W32(o + 0x30, (uint32_t)((int32_t)RS16(o + 0x4E) << 12));
        if (!(R16(o + 4) & 1)) {
            if (RS32(o + 0x34) > hi) W32(o + 0x34, (uint32_t)hi);
            else if (RS32(o + 0x34) < lo) W32(o + 0x34, (uint32_t)lo);
        }
    }
}

/* back-flip away from Sho (type 2: 0xD, type F: 0xE), then hop a wall or `next` */
static void act_flip(uint32_t o, int32_t vx, uint16_t hopAct, uint16_t next)
{
    if ((R16(o + 0x58) & 3) == 1) {
        W32(o + 0x34, 0x00040000u);
        W32(o + 0x30, (uint32_t)((R16(o + 2) & 0x8000) ? vx : -vx));
        AND16(o + 0x58, 0xFFFC); OR16(o + 0x58, 2);
    } else if ((R16(o + 0x58) & 3) == 2 && !(R16(o + 2) & 0x4000)) {
        if (wall_hop(o, hopAct)) W16(o + 0x14, 0xC);
        else W16(o + 6, next);
    }
}

/* the four handlers' tail: death overlay (1 = finished), then the sprite */
static int overlay_done(uint32_t o, uint32_t defs, uint32_t scr)
{
    return R16(o + 6) == 10 && (R16(o + 0x58) & 0x8000) &&
           EM_OverlayDraw(defs, scr, 0xB, o + 0x48, o + 0x50, o + 0x52);
}

static void draw(uint32_t o, int16_t bank)
{
    EM_SprBasic(o, bank);
    if (R16(o + 6) == 9) W16(SPR + 0x0E, R16(o + 0x58) & 3);
    else if (R16(o + 6) == 10 && (R16(o + 0x58) & 0x8000)) W16(SPR + 0x0E, R16(SCRATCH_ANIM) & 3);
}

static void air_flag(uint32_t o, uint16_t descAct)
{
    int16_t a = RS16(o + 6);
    if (a == 5 || a == 6 || a == 7 || a == (int16_t)descAct) OR16(o + 2, 4);
    else AND16(o + 2, 0xFFFB);
}

/* ------------------------------------------------------------ type 2, sub 0 */

static void Ninja2_Init(void)                               /* 0x060970B4 */
{
    uint32_t o = O;
    W16(o + 6, 1);
    W16(o + 0x16, 2);
    if (STAGE == 6) W16(o + 6, 0x11);
    Anim_Start(o + 0x58, T2_DEFS, T2_SCR, CHAR_BASE(SLOT), RS16(o + 6));
    AND16(o + 2, 0xFFFE);
    W16(o + 4, 0);
}

/* think, shared by subtypes 0 and 1 of type 2 (cases 0..0x10) */
static void Ninja2_Think(uint32_t o)
{
    switch (R16(o + 6)) {
    case 0:
        EM_FaceSho(o);
        if (RS32(o + 0x34) != 0) { W16(o + 6, 3); W16(o + 0x4E, 0); break; }
        if (EM_Abs(RS32(o + 8) - RS32(PL + 8)) < 0x00400000 &&
            (EM_Abs(RS32(o + 0xC) - RS32(PL + 0xC)) < 0x00100000 || (R16(PL + 2) & 0x4000))) {
            W16(o + 6, 0xD);
            break;
        }
        if (EM_Abs(RS32(o + 8) - RS32(PL + 8)) < 0x00780000) {
            int16_t t = RS16(o + 0x14);
            W16(o + 0x14, (uint16_t)(t - 1));
            if (t != 0) break;
            W32(o + 0x30, 0);
            W16(o + 6, 5);
            EM_SolveJump(0x70000, 0xA0000, 0x51);
        }
        W16(o + 6, 1);                                              /* (overwrites the 5) */
        break;
    case 1:
        EM_FaceSho(o);
        if (stuck(o)) OR16(o + 0x10, (R16(o + 2) & 0x8000) ? 4 : 8);
        W32(o + 0x30, (R16(o + 2) & 0x8000) ? 0xFFFEC000u : 0x00014000u);
        if (RS32(o + 0x34) != 0) { W16(o + 6, 3); W16(o + 0x4E, 0); break; }
        if (shuriken_ahead(o)) { W16(o + 6, 0xC); W16(o + 0x4E, 0); break; }
        if (wall_hop(o, 2)) { W16(o + 0x14, 0xC); break; }
        if (EM_Abs(RS32(o + 8) - RS32(PL + 8)) < 0x00780000) {
            W16(o + 6, 5);
            W32(o + 0x30, 0);
            EM_SolveJump(0x70000, 0xA0000, 0x51);
        }
        break;
    case 2: act_hop(o); break;
    case 3: if (RS32(o + 0x34) == 0) W16(o + 6, 4); break;
    case 5: act_jump(o, 0x70000, 0xA0000, 0xC); break;
    case 6: case 0xC:
        if ((R16(o + 0x58) & 3) == 1 && !(R16(o + 2) & 0x4000)) W16(o + 6, 7);
        break;
    case 9: case 10:
        if (R16(o + 2) & 0x4000) anim_reset(o);
        break;
    case 0xD: act_flip(o, 0x30000, 2, 0xE); break;
    case 0x10:
        W32(o + 8, (uint32_t)(RS32(o + 8) - ((R16(o + 2) & 0x8000) ? -0x20000 : 0x20000)));
        break;
    }
}

/* anim over, shared part (1, 4, 7, 8, 0xE, 0x10) */
static void Ninja2_AnimDone(uint32_t o)
{
    switch (RS16(o + 6)) {
    case 1:
        if (EM_Abs(RS32(o + 8) - RS32(PL + 8)) < 0x00780000) {
            W16(o + 6, 5); W32(o + 0x30, 0); EM_SolveJump(0x70000, 0xA0000, 0x51);
        } else { W16(o + 6, 1); anim_reset(o); }
        break;
    case 4:
        if (RS32(o + 0xC) - RS32(PL + 0xC) < 0x00960001) W16(o + 6, 1);
        else { W16(o + 0x14, 0x3C); W16(o + 6, 0); }
        break;
    case 7:
        if (EM_Abs(RS32(o + 8) - RS32(PL + 8)) < 0x00780001) { W16(o + 0x14, 0x3C); W16(o + 6, 0); }
        else W16(o + 6, 1);
        break;
    case 8:
        if (!(R16(o + 2) & 0x4000)) { W16(o + 0x14, 3); W16(o + 6, 0); AND16(o + 2, 0xFFF7); }
        else { W16(o + 6, 8); anim_reset(o); }
        break;
    case 0xE: W16(o + 0x14, 0x3C); W16(o + 6, 0); break;
    case 0x10: W16(o + 0x14, 0xF); W16(o + 6, 0); break;
    }
}

static void Ninja2_Sub1(void);

void EnemyHandler_0002(void)                                /* 0x0609630A */
{
    uint32_t o = O;
    if ((R16(o + 0x12) & 0x1F) == 1) { Ninja2_Sub1(); return; }
    if (R16(o + 2) & 1) Ninja2_Init();
    if (EM_Far(o, 0x00A00000)) {
        if (!(R16(o + 2) & 0x4008)) return;
        if (R16(o + 6) == 0xF) return;
    }
    if (!FROZEN) {
        int16_t r = combat(1, 0x10);
        if (!(R16(o + 2) & 0x4000)) W32(o + 0x30, 0);
        uint16_t a = R16(o + 6);
        if ((R16(o + 2) & 8) && a != 9 && a != 10 && a != 8) take_hit(r, T2_DEFS, T2_SCR);
        if (!(R16(o + 0x58) & 0x8000)) {
            Ninja2_Think(o);
            if (R16(o + 6) == 0x11) EM_FaceSho(o);
        } else if (RS16(o + 6) == 9) {
            Enemy_DeathBurst(o); Score_Add(200); W16(o, 0xFFFF); return;
        } else if (RS16(o + 6) == 0x11) {
            if (EM_Abs(RS32(o + 8) - RS32(PL + 8)) < 0x008C0000) {
                W16(o + 6, 5); W32(o + 0x30, 0); EM_SolveJump(0x70000, 0xA0000, 0x51);
            } else W16(o + 6, 1);
        } else Ninja2_AnimDone(o);
        Obj_Integrate(o);
        EM_ClampX(o);
        Obj_CollideMap(o, 3, 8);
        Enemy_AnimSwitch();
        air_flag(o, 0xC);
    }
    if (overlay_done(o, T2_DEFS, T2_SCR)) { Enemy_DeathBurst(o); Score_Add(200); W16(o, 0xFFFF); return; }
    draw(o, COLOR_BANK(SLOT));
    if (STAGE == 6 && R16(o + 6) == 0x11) { OR16(SPR + 0x10, 0x200); W32(SPR + 0x18, 0xFFFFFF7Fu); }
    Anim_Tick(o + 0x58);
}

/* ------------------------------------------------------------ type 2, sub 1 */

static void Ninja2b_Init(void)                              /* 0x06098078 */
{
    uint32_t o = O;
    W16(o + 6, 0xF);
    W16(o + 0x16, 2);
    Anim_Start(o + 0x58, T2_DEFS, T2_SCR, CHAR_BASE(SLOT), RS16(o + 6));
    AND16(o + 2, 0xFFFE);
    W16(o + 4, 0);
    if (RS32(o + 8) < RS32(PL + 8)) AND16(o + 2, 0x7FFF); else OR16(o + 2, 0x8000);
    W16(o + 0x54, R32(o + 8) >> 16);
    W16(o + 0x56, R32(o + 0xC) >> 16);
}

/* killed: back into hiding at home, no item drop the next times */
static void Ninja2b_Respawn(uint32_t o)
{
    Enemy_DeathBurst(o);
    Score_Add(200);
    W16(o + 6, 0xF);
    W16(o + 0x16, 2);
    W16(o + 4, 2);
    W32(o + 8, (uint32_t)((int32_t)RS16(o + 0x54) << 16));
    W32(o + 0xC, (uint32_t)((int32_t)RS16(o + 0x56) << 16));
    W16(o + 0x18, 0xFFFF);
}

static void Ninja2_Sub1(void)                               /* 0x0609723A */
{
    uint32_t o = O;
    if (R16(o + 2) & 1) Ninja2b_Init();
    if (EM_Abs(RS32(PL + 8) - RS32(o + 8)) > 0x01400000) AND16(o + 4, 0xFFFD);
    int16_t r = 0;
    if (EM_Far(o, 0x00A00000)) {
        if (!(R16(o + 2) & 0x4008)) return;
        if (R16(o + 6) == 0xF) return;
    }
    if (!FROZEN) {
        if (R16(o + 6) == 0xF) OR16(o + 2, 8);
        else r = combat(1, 0x10);
        if (!(R16(o + 2) & 0x4000)) W32(o + 0x30, 0);
        uint16_t a = R16(o + 6);
        if ((R16(o + 2) & 8) && a != 0xF && a != 9 && a != 10 && a != 8) take_hit(r, T2_DEFS, T2_SCR);
        if (!(R16(o + 0x58) & 0x8000)) {
            if (R16(o + 6) == 0xF) {                                /* waiting at home */
                if (EM_Abs(RS32(o + 8) - RS32(PL + 8)) >= 0x00EC0000) return;
                if (R16(o + 4) & 2) return;
                W16(o + 6, 6);                                      /* drop in from the top */
                W16(o + 0x14, 2);
                W32(o + 0xC, R32(CAM_Y));
                W16(o + 0x4E, 0);
                AND16(o + 2, 0xFFF7);
                if (RS32(o + 8) < RS32(PL + 8)) AND16(o + 2, 0x7FFF); else OR16(o + 2, 0x8000);
            } else if (R16(o + 6) != 0x11) Ninja2_Think(o);
        } else if (RS16(o + 6) == 9) {
            Ninja2b_Respawn(o);
            return;
        } else Ninja2_AnimDone(o);
        if (R16(o + 6) != 0xF) { Obj_Integrate(o); Obj_CollideMap(o, 3, 8); }
        Enemy_AnimSwitch();
        air_flag(o, 0xC);
    }
    if (overlay_done(o, T2_DEFS, T2_SCR)) { Ninja2b_Respawn(o); Enemy_AnimSwitch(); return; }
    if (R16(o + 6) == 0xF) OR16(o + 0x58, 0x4000); else AND16(o + 0x58, 0xBFFF);
    draw(o, COLOR_BANK(SLOT));
    Anim_Tick(o + 0x58);
}

/* ------------------------------------------------------------ type F, sub 0 */

static void NinjaF_Init(void)                               /* 0x06070D60 */
{
    uint32_t o = O;
    W16(o + 6, 1);
    Anim_Start(o + 0x58, TF_DEFS, TF_SCR, CHAR_BASE(SLOT), RS16(o + 6));
    AND16(o + 2, 0xFFFE);
    W16(o + 0x16, 2);
    W16(o + 0x56, 0);
    W16(o + 4, 0);
    EM_FaceSho(o);
}

static void NinjaF_Sub1(void);

void EnemyHandler_000F(void)                                /* 0x0606FF64 */
{
    uint32_t o = O;
    if ((R16(o + 0x12) & 0x1F) == 1) { NinjaF_Sub1(); return; }
    if (R16(o + 2) & 1) NinjaF_Init();
    if (EM_Far(o, 0x00700000) && !(R16(o + 2) & 8) && !(R16(o + 2) & 0x4000)) return;
    if (!FROZEN) {
        int16_t r = combat(2, 0x11);
        if (!(R16(o + 2) & 0x4000)) W32(o + 0x30, 0);
        uint16_t a = R16(o + 6);
        if ((R16(o + 2) & 8) && a != 9 && a != 10 && a != 8) take_hit(r, TF_DEFS, TF_SCR);
        if (!(R16(o + 0x58) & 0x8000)) {
            switch (R16(o + 6)) {
            case 0:
                EM_FaceSho(o);
                if (RS32(o + 0x34) != 0) { W16(o + 6, 3); W16(o + 0x4E, 0); break; }
                if (EM_Abs(RS32(o + 8) - RS32(PL + 8)) < 0x00400000 &&
                    (EM_Abs(RS32(o + 0xC) - RS32(PL + 0xC)) < 0x00100000 || (R16(PL + 2) & 0x4000))) {
                    W16(o + 6, 0xE);
                    break;
                }
                if (EM_Abs(RS32(o + 8) - RS32(PL + 8)) < 0x00C80000) {
                    int16_t t = RS16(o + 0x14);
                    W16(o + 0x14, (uint16_t)(t - 1));
                    if (t != 0) break;
                    W32(o + 0x30, 0);
                    W16(o + 6, 5);
                    EM_SolveJump(0x90000, 0xB0000, 0x86);
                }
                W16(o + 6, 1);                                      /* (overwrites the 5) */
                break;
            case 1: {
                uint16_t old = R16(o + 2);
                EM_FaceSho(o);
                if ((old & 0x8000) != (R16(o + 2) & 0x8000) && R16(o + 0x56) != 0) {
                    W16(o + 2, (R16(o + 2) & 0x7FFF) | (old & 0x8000));     /* skid, then turn */
                    W16(o + 6, 0xC);
                    W16(o + 0x54, 1);
                    break;
                }
                if ((R16(o + 0x58) & 3) == 1) W32(o + 0x30, (R16(o + 2) & 0x8000) ? 0xFFFD0000u : 0x00030000u);
                if (RS32(o + 0x34) != 0) { W16(o + 6, 3); W16(o + 0x4E, 0); break; }
                if (shuriken_ahead(o)) { W16(o + 6, 0xD); W16(o + 0x4E, 0); break; }
                if (wall_hop(o, 0xC)) { W16(o + 0x54, 2); W16(o + 0x14, 0xC); break; }
                if (R16(0x060143EAu) % 7u == 0) sub_06007CDE(0x112);          /* footstep: A.BIN 0x0600A604 = frame % 7 */
                if (EM_Abs(RS32(o + 8) - RS32(PL + 8)) < 0x00C80000) {
                    W16(o + 6, 5);
                    W32(o + 0x30, 0);
                    EM_SolveJump(0x90000, 0xB0000, 0x86);
                }
                break;
            }
            case 2: act_hop(o); break;
            case 3: if (RS32(o + 0x34) == 0) W16(o + 6, 4); break;
            case 5: act_jump(o, 0x90000, 0xB0000, 0xD); break;
            case 6: case 0xD:
                if ((R16(o + 0x58) & 3) == 1 && !(R16(o + 2) & 0x4000)) W16(o + 6, 7);
                break;
            case 9: case 10:
                if (R16(o + 2) & 0x4000) anim_reset(o);
                break;
            case 0xC: {                                            /* skid: slowing down */
                static const int32_t v[4] = { 0x30000, 0x20000, 0x10000, 0x5000 };
                int32_t s = v[R16(o + 0x58) & 3];
                W32(o + 0x30, (uint32_t)((R16(o + 2) & 0x8000) ? -s : s));
                break;
            }
            case 0xE: act_flip(o, 0x40000, 2, 0xF); break;
            case 0x11:
                if ((R16(o + 0x58) & 3) == 1) W32(o + 0x30, (R16(o + 2) & 0x8000) ? 0x00020000u : 0xFFFE0000u);
                break;
            }
        } else {
            switch (RS16(o + 6)) {
            case 4:
                if (RS32(o + 0xC) - RS32(PL + 0xC) < 0x00960001) W16(o + 6, 1);
                else { W16(o + 0x14, 0x3C); W16(o + 6, 0); }
                break;
            case 7:
                if (EM_Abs(RS32(o + 8) - RS32(PL + 8)) < 0x00C80001) { W16(o + 0x14, 0x3C); W16(o + 6, 0); }
                else W16(o + 6, 1);
                break;
            case 8:
                if (!(R16(o + 2) & 0x4000)) { W16(o + 6, 0); AND16(o + 2, 0xFFF7); }
                else { W16(o + 6, 8); anim_reset(o); }
                break;
            case 9:
                Enemy_DeathBurst(o); Score_Add(200); W16(o, 0xFFFF); return;
            case 0xC:
                W16(o + 6, R16(o + 0x54));
                if (R16(o + 6) == 5) { W32(o + 0x30, 0); EM_SolveJump(0x90000, 0xB0000, 0x86); }
                break;
            case 0xF: W16(o + 0x14, 0x3C); W16(o + 6, 0); break;
            case 0x11: W16(o + 0x14, 0x14); W16(o + 6, 0); break;
            }
        }
        W16(o + 0x56, (uint16_t)(RS32(o + 0x30) >> 12));
        Obj_Integrate(o);
        Obj_CollideMap(o, 3, 8);
        Enemy_AnimSwitch();
        air_flag(o, 0xD);
    }
    if (overlay_done(o, TF_DEFS, TF_SCR)) { Enemy_DeathBurst(o); Score_Add(200); W16(o, 0xFFFF); return; }
    draw(o, COLOR_BANK(SLOT));
    Anim_Tick(o + 0x58);
}

/* ------------------------------------------------------------ type F, sub 1 */

static void NinjaFb_Init(void)                              /* 0x06071ABC */
{
    uint32_t o = O;
    W16(o + 6, 0x10);
    W16(o + 0x16, 2);
    Anim_Start(o + 0x58, TF_DEFS, TF_SCR, CHAR_BASE(SLOT), RS16(o + 6));
    AND16(o + 2, 0xFFFE);
    W16(o + 4, 0);
}

static void NinjaF_Sub1(void)                               /* 0x06070E08 */
{
    uint32_t o = O;
    if (R16(o + 2) & 1) NinjaFb_Init();
    if (EM_Far(o, 0x00700000) && !(R16(o + 2) & 8) && !(R16(o + 2) & 0x4000)) return;
    if (!FROZEN) {
        int16_t r = combat(1, 0x11);
        if (!(R16(o + 2) & 0x4000)) W32(o + 0x30, 0);
        uint16_t a = R16(o + 6);
        if ((R16(o + 2) & 8) && a != 9 && a != 10 && a != 8) take_hit(r, TF_DEFS, TF_SCR);
        if (!(R16(o + 0x58) & 0x8000)) {
            switch (R16(o + 6)) {
            case 0:
                EM_FaceSho(o);
                if (RS32(o + 0x34) != 0) { W16(o + 6, 3); W16(o + 0x4E, 0); break; }
                if (EM_Abs(RS32(o + 8) - RS32(PL + 8)) < 0x003C0000 &&
                    (EM_Abs(RS32(o + 0xC) - RS32(PL + 0xC)) < 0x00100000 || (R16(PL + 2) & 0x4000))) {
                    W16(o + 6, 0xE);
                    break;
                }
                if (EM_Abs(RS32(o + 8) - RS32(PL + 8)) < 0x00F00000) {
                    int16_t t = RS16(o + 0x14);
                    W16(o + 0x14, (uint16_t)(t - 1));
                    if (t != 0) break;
                    W32(o + 0x30, 0);
                    W16(o + 6, 5);
                    EM_SolveJump(0xA4000, 0xC0000, 0xAD);
                }
                W16(o + 6, 0x10);                                   /* (overwrites the 5) */
                break;
            case 2: act_hop(o); break;
            case 3: if (RS32(o + 0x34) == 0) W16(o + 6, 4); break;
            case 5: act_jump(o, 0x90000, 0xB0000, 0xD); break;
            case 6: case 0xD:
                if ((R16(o + 0x58) & 3) == 1 && RS32(o + 0x34) == 0) W16(o + 6, 7);
                break;
            case 9: case 10:
                if (R16(o + 2) & 0x4000) anim_reset(o);
                break;
            case 0xE: act_flip(o, 0x30000, 2, 0xF); break;
            case 0x10:                                              /* run */
                EM_FaceSho(o);
                W32(o + 0x30, (R16(o + 2) & 0x8000) ? 0xFFFEC000u : 0x00014000u);
                if (RS32(o + 0x34) != 0) { W16(o + 6, 3); W16(o + 0x4E, 0); break; }
                if (shuriken_ahead(o)) { W16(o + 6, 0xD); W16(o + 0x4E, 0); break; }
                if (wall_hop(o, 2)) { W16(o + 0x14, 0xC); break; }
                if (EM_Abs(RS32(o + 8) - RS32(PL + 8)) < 0x00F00000) {
                    W16(o + 6, 5);
                    W32(o + 0x30, 0);
                    EM_SolveJump(0xA4000, 0xC0000, 0xAD);
                }
                break;
            case 0x11:
                if ((R16(o + 0x58) & 3) == 1) W32(o + 0x30, (R16(o + 2) & 0x8000) ? 0x00020000u : 0xFFFE0000u);
                break;
            }
        } else {
            switch (RS16(o + 6)) {
            case 4:
                if (RS32(o + 0xC) - RS32(PL + 0xC) < 0x00960001) W16(o + 6, 0x10);
                else { W16(o + 0x14, 0x3C); W16(o + 6, 0); }
                break;
            case 7:
                if (EM_Abs(RS32(o + 8) - RS32(PL + 8)) < 0x00F00001) { W16(o + 0x14, 0x3C); W16(o + 6, 0); }
                else W16(o + 6, 0x10);
                break;
            case 8:
                if (!(R16(o + 2) & 0x4000)) { W16(o + 6, 0); AND16(o + 2, 0xFFF7); }
                else { W16(o + 6, 8); anim_reset(o); }
                break;
            case 9:
                Enemy_DeathBurst(o); Score_Add(200); W16(o, 0xFFFF); return;
            case 0xF: W16(o + 0x14, 0x3C); W16(o + 6, 0); break;
            case 0x10:
                if (EM_Abs(RS32(o + 8) - RS32(PL + 8)) < 0x00F00000) {
                    W16(o + 6, 5); W32(o + 0x30, 0); EM_SolveJump(0xA4000, 0xC0000, 0xAD);
                } else { W16(o + 6, 0x10); anim_reset(o); }
                break;
            case 0x11: W16(o + 0x14, 0x14); W16(o + 6, 0); break;
            }
        }
        Obj_Integrate(o);
        Obj_CollideMap(o, 3, 8);
        Enemy_AnimSwitch();
        air_flag(o, 0xD);
    }
    if (overlay_done(o, TF_DEFS, TF_SCR)) { Enemy_DeathBurst(o); Score_Add(200); W16(o, 0xFFFF); return; }
    draw(o, (int16_t)(COLOR_BANK(SLOT) + 2));
    Anim_Tick(o + 0x58);
}
