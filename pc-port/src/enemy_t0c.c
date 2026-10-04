/* Enemy type 0x000C (stage 6-1): ceiling golem with an arm cannon. Loader 0x060A3FF4,
 * handler 0x060A47A4, init 0x060A408C, decision 0x060A41E4, face test 0x060A4486 (pivot
 * 72 px ahead of it), death overlays 0x060A44FC (anim 9, falling) / 0x060A45D0 (anim 10),
 * idle part 0x060A4698 (anim 0xB, frame ((+0x4C & 0xFF) >> 2) % 12 at +-78,+109 px), death
 * bursts 0x060A508C (7, 24 px apart), aim 0x060A50F4 (Atan8 from its muzzle (+-72,+112 px):
 * 0 / 1 / 2 = act 4 / 5 / 6 shot low / mid / high). 4 HP, 800 points.
 * Hidden at first (+2 bit 8 = invulnerable, gravity off; its graphics hang ~100 px below
 * its origin). When Sho comes within 56 px (or at once with +0x12 bit 0) it drops 110 px
 * (mode 1, act 1, 2 px/frame, debris 0x0605A16C at home + 100 px) and, unless +0x12 bit 1,
 * drops a second one 80 px to its left (type C, flags 1); the first one jumps 160 px right.
 * Then (+4 bit 0) every 32 frames: Sho behind -> turn (mode 2, act 3); within 200 px ->
 * rand % 16 < 5 ? pause (mode 7, act 0xD) : shoot (mode 3, act 4/5/6, its arm = attack box,
 * 2 damage; after it 50% climbs back); 200-792 px -> climb back (mode 6, act 2, comes down
 * again when Sho is within 200 px). A hit makes it flash (bank +3) and invulnerable 32
 * frames; at 0 HP: act 0xC (cut, kinds 0-3) or 8, 32 frames, then 7 bursts + 800.
 * Act 0xD's script pointer is the overlay's very last word (0x060EA2A8 -> 0x060EA254, see
 * lz.c). Fields: +0x14 wait, +0x1E/+0x20 home, +0x48 counter, +0x4A drop wait / fall
 * speed, +0x4C idle frame, +0x4E mode, +0x50 drop / fall offset, +0x52 kind that hurt it,
 * +0x54 invulnerable timer, +0x56 aim. */
#include "enemy_more.h"
#include "anim.h"
#include "sprite.h"
#include "player_ext.h"
#include "objects.h"
#include "fx.h"

#define TC_DEFS 0x060C7AF8u
#define TC_SCR  0x060EA274u

static uint32_t rnd_div(uint32_t d) { return ((uint32_t)sub_0602DDD2() >> 8) / d; }   /* A.BIN 0x0600A55C */

void EnemyC_Load(void)                                      /* 0x060A3FF4 */
{
    Enemy_RegisterChars(0xC, 0x060E9C98u);
    int16_t s = (int16_t)Enemy_FindSlot(0xC);
    uint16_t b = R16(0x060A9084u + 2u * (uint32_t)s);
    Pal_Copy((b & 0xFFF) << 4, 16, 0x060E9FF4u);
    for (uint32_t k = 1; k < 4; k++) Pal_Copy((uint16_t)(b + k) * 16, 16, 0x060E9FF4u + k * 0x20);
}

/* 0x0605A16C (local copy of objhandlers_e2.c's): 8 debris objects 0x101E (flags 1)
 * around (x, y) from table 0x060ABF22 {angle, dx, dy, frame} */
static void debris_spawn(int32_t x, int32_t y)
{
    for (int16_t i = 0; i < 8; i++) {
        uint32_t e = 0x060ABF22u + (uint32_t)i * 8;
        int s = Obj_Register(0x101E, RS16(e + 2) * 0x10000 + x, RS16(e + 4) * 0x10000 + y, 1, 0xFFFF);
        if ((int16_t)s < 0) continue;
        uint32_t p = OBJ_TABLE + (uint32_t)(int16_t)s * OBJ_SIZE;
        W16(p + 0x48, R16(e));
        W16(p + 0x4A, ((uint16_t)s & 0xF) << 4);
        W16(p + 0x4C, R16(e + 6));
    }
}

static void GolemC_Debris(uint32_t o)
{
    sub_06007CDE(0x20A);
    debris_spawn((int32_t)RS16(o + 0x1E) << 16, (RS16(o + 0x20) + 100) * 0x10000);
}

static int GolemC_Face(void)                                /* 0x060A4486 */
{
    uint32_t o = O;
    uint16_t old = R16(o + 2);
    AND16(o + 2, 0x7FFF);
    int32_t d = (old & 0x8000) ? (int32_t)0xFFB80000 : 0x00480000;
    if (RS32(PL + 8) < d + RS32(o + 8)) OR16(o + 2, 0x8000);
    return (R16(o + 2) & 0x8000) != (old & 0x8000);
}

static void GolemC_Init(void)                               /* 0x060A408C */
{
    uint32_t o = O;
    W16(o + 6, 0);
    Anim_Start(o + 0x58, TC_DEFS, TC_SCR, CHAR_BASE(SLOT), RS16(o + 6));
    AND16(o + 2, 0xFFFE);
    W16(o + 0x16, 4);
    for (uint32_t k = 0x48; k <= 0x56; k += 2) W16(o + k, 0);
    W16(o + 0x52, 0xFFFF);
    W16(o + 0x1E, (uint16_t)(R32(o + 8) >> 16));
    W16(o + 0x20, (uint16_t)(R32(o + 0xC) >> 16));
    OR16(o + 2, 0x100);
    OR16(o + 2, 8);
    GolemC_Face();
    if (R16(o + 0x12) & 1) {                                        /* drops at once */
        W16(o + 0x4E, 1);
        W16(o + 6, 1);
        GolemC_Debris(o);
        W16(o + 0x50, 0);
        AND16(o + 0x12, 0xFFFE);
        OR16(o + 4, 4);
        OR16(o + 4, 0x10);
    }
}

static int16_t GolemC_Aim(void)                             /* 0x060A50F4 */
{
    uint32_t o = O;
    int32_t d = (R16(o + 2) & 0x8000) ? (int32_t)0xFFB80000 : 0x00480000;
    int16_t a = (int16_t)Atan8((int16_t)((uint32_t)(RS32(o + 8) + d) >> 16), (int16_t)((uint32_t)(RS32(o + 0xC) + 0x700000) >> 16),
                               (int16_t)(R32(PL + 8) >> 16), (int16_t)(R32(PL + 0xC) >> 16));
    int16_t b = (int16_t)((a + 0x80) & 0xFF);
    if (R16(o + 2) & 0x8000) {
        int16_t c = (int16_t)((b + 0x80) & 0xFF);
        if (c > 0x80) return 2;
        if (c > 0x60) return 1;
        if (c > 0x40) return 0;
    } else {
        if (b < 0x80) return 2;
        if (b < 0xA0) return 1;
        if (b < 0xC0) return 0;
    }
    return (int16_t)R16(o + 2);                                     /* sic: no match -> flags2 */
}

static void GolemC_Decide(void)                             /* 0x060A41E4 */
{
    uint32_t o = O;
    uint16_t dx = (uint16_t)(EM_Abs(RS32(o + 8) - RS32(PL + 8)) >> 19);
    if (RS16(o + 0x4E) == 6) return;
    if (!(R16(o + 4) & 1)) {                                        /* still hidden */
        if (R16(o + 6) != 0) return;
        if (!(R16(o + 4) & 4)) {
            if (dx < 7) {
                W16(o + 0x4E, 1);
                W16(o + 6, 1);
                W16(o + 0x50, 0);
                if (!(R16(o + 0x12) & 2)) Obj_Register(0xC, RS32(o + 8) - 0x500000, RS32(o + 0xC), 1, 0xFFFF);
                if (!(R16(o + 4) & 0x10)) W32(o + 8, (uint32_t)(RS32(o + 8) + 0xA00000));
                OR16(o + 4, 0x10);
                W16(o + 0x1E, (uint16_t)(R32(o + 8) >> 16));
                OR16(o + 4, 4);
                GolemC_Debris(o);
            }
        } else if (R16(o + 0x4A) == 0) {
            if (dx < 0x19) {
                W16(o + 0x4E, 1);
                W16(o + 6, 1);
                OR16(o + 4, 0x10);
                GolemC_Debris(o);
                W16(o + 0x50, 0);
            }
        } else W16(o + 0x4A, R16(o + 0x4A) - 1);
        return;
    }
    if (!GolemC_Face()) {
        if (dx < 0x19) {
            if ((rnd_div(10) & 0xF) < 5) W16(o + 0x4E, 7);
            else {
                W16(o + 0x4E, 3);
                W16(o + 0x56, (uint16_t)GolemC_Aim());
                if (RS16(o + 0x56) == -1) W16(o + 0x4E, 7);
            }
        } else W16(o + 0x4E, (dx < 0x19 || dx > 99) ? 0 : 6);
    } else {
        if (!(R16(o + 2) & 0x8000)) OR16(o + 2, 0x8000); else AND16(o + 2, 0x7FFF);
        W16(o + 0x4E, 2);
    }
}

/* the overlays of 0x060A44FC / 0x060A45D0 / 0x060A4698 */
static void GolemC_Part(int16_t anim, int32_t dx, int32_t dy, int16_t bank, uint16_t cc, int16_t script)
{
    uint32_t o = O;
    int16_t s = (int16_t)Enemy_FindSlot(0xC);
    Anim_Start(SCRATCH_ANIM, TC_DEFS, TC_SCR, CHAR_BASE(s), anim);
    if (script >= 0) W16(SCRATCH_ANIM + 2, (uint16_t)script);
    W16(SPR + 2, (uint32_t)((RS32(o + 8) + dx - RS32(CAM_X)) >> 16));
    W16(SPR + 4, (uint32_t)((RS32(o + 0xC) + dy - RS32(CAM_Y)) >> 16));
    W16(SPR + 0x10, (R16(o + 2) & 0x8000) | 0x200);
    W32(SPR + 0x18, 0xFFFFFFFFu);
    W16(SPR + 0x0A, (uint16_t)(COLOR_BANK(s) + bank));
    W16(SPR + 0x0C, 0);
    W16(SPR + 0x0E, cc);
    Anim_Tick(SCRATCH_ANIM);
}

static int GolemC_Die(uint32_t o)                           /* 0x060A508C + 800 points */
{
    int32_t y = RS32(o + 0xC);
    for (uint16_t i = 0; i < 7; i++) {
        W32(o + 0xC, (uint32_t)(RS32(o + 0xC) + 0x180000));
        Enemy_DeathBurst(o);
        W16(o + 0x18, 0xFFFF);
    }
    W32(o + 0xC, (uint32_t)y);
    Score_Add(800);                                                 /* 0x06062B38 */
    W16(o, 0xFFFF);
    return 1;
}

static void GolemC_Dying(uint32_t o)                        /* 0 HP: mode 5 */
{
    W16(o + 0x48, 0);
    W16(o + 0x50, 0);
    W16(o + 0x4E, 5);
    W16(o + 6, 8);
    W16(o + 0x4A, 0);
    int16_t k = RS16(o + 0x52);
    W16(o + 6, (k == 0 || k == 1 || k == 2 || k == 3) ? 0xC : 8);
}

static void GolemC_Sink(uint32_t o)                         /* mode 6, act 2 */
{
    W16(o + 0x4E, 6);
    W16(o + 6, 2);
    W16(o + 0x48, 0);
    OR16(o + 2, 8);
    W16(o + 0x50, 0);
    GolemC_Debris(o);
    AND16(o + 4, 0xFFF7);
}

void EnemyHandler_000C(void)                                /* 0x060A47A4 */
{
    uint32_t o = O;
    int16_t s = (int16_t)Enemy_FindSlot(0xC);
    if (R16(o + 2) & 1) GolemC_Init();
    int32_t d = RS32(o + 8) - RS32(CAM_X) + (int32_t)0xFF600000;
    if (d < 0) d = RS32(CAM_X) - RS32(o + 8) + 0x00A00000;
    if (d > 0x01000000 && !(R16(o + 2) & 8)) return;
    if (FROZEN) goto draw;
    if (!(R16(o + 4) & 1)) {
        GolemC_Decide();
        if (R16(o + 6) == 0) return;
    }
    if (!(R16(PL + 2) & 8)) {
        int16_t a = RS16(o + 6);
        int16_t dmg = (a == 4 || a == 5 || a == 6) ? 2 : 1;
        if ((int16_t)Enemy_AttackPlayer(dmg, 0, 0) == 3) sub_06007CDE(0x10C);
    }
    if (!(R16(o + 2) & 8)) {
        W16(o + 0x52, 0xFFFF);
        if ((int16_t)Enemy_CheckPlayerHits(0xC000) == 2) W16(o + 0x52, (uint16_t)Player_AttackKind());
    }
    {
        int16_t m = RS16(o + 0x4E);
        if (!(m == 1 || m == 6 || m == 4 || m == 5 || (R16(o + 4) & 8) || !(R16(o + 2) & 8))) {   /* just hit */
            OR16(o + 4, 8);
            W16(o + 0x54, 0x20);
            if (RS16(o + 0x16) <= 0) { GolemC_Dying(o); goto integrate; }
        }
    }
    {
        int16_t m = RS16(o + 0x4E);
        if (m != 1 && m != 6 && m != 4 && m != 5) {
            if (R16(o + 0x54) == 0) { AND16(o + 4, 0xFFF7); AND16(o + 2, 0xFFF7); }
            else W16(o + 0x54, R16(o + 0x54) - 1);
        }
    }
    if (R16(o + 6) == 0) {
        if (R16(o + 0x14) == 0) {
            GolemC_Decide();
            int16_t m = RS16(o + 0x4E);
            if (m == 2) W16(o + 0x14, 1);
            else if (m == 3 || m == 6 || m == 7) W16(o + 0x14, 0x20);
        } else {
            W16(o + 0x14, R16(o + 0x14) - 1);
            if (R16(o + 0x14) == 0) {
                int16_t m = RS16(o + 0x4E);
                if (m == 2) { W16(o + 0x4E, 2); W16(o + 6, 3); W16(o + 0x48, 0); }
                else if (m == 3) {
                    W16(o + 0x4E, 3);
                    W16(o + 6, 5);
                    int16_t a = RS16(o + 0x56);
                    if (a == 0) W16(o + 6, 4); else if (a == 1) W16(o + 6, 5); else if (a == 2) W16(o + 6, 6);
                    W16(o + 0x48, 0);
                } else if (m == 6) GolemC_Sink(o);
                else if (m == 7) { W16(o + 0x4E, 7); W16(o + 6, 0xD); W16(o + 0x48, 0); }
            }
        }
    }
    if (RS16(o + 0x4E) == 1 && R16(o + 6) == 1) {                   /* dropping */
        if (RS16(o + 0x50) < 0x37) { W32(o + 0xC, R32(o + 0xC) + 0x20000); W16(o + 0x50, R16(o + 0x50) + 1); }
        else OR16(o + 4, 2);
    }
    if (RS16(o + 0x4E) == 6 && R16(o + 6) == 2) {                   /* climbing back */
        if (RS16(o + 0x50) < 0x37) { W32(o + 0xC, R32(o + 0xC) - 0x20000); W16(o + 0x50, R16(o + 0x50) + 1); }
        else OR16(o + 4, 2);
    }
    if (R16(o + 6) == 0) W16(o + 0x4C, R16(o + 0x4C) + 1);
    if (RS16(o + 0x4E) == 5) {
        if (RS16(o + 0x48) > 0x1F) { GolemC_Die(o); return; }
        W16(o + 0x48, R16(o + 0x48) + 1);
        W16(o + 0x50, (uint16_t)(RS16(o + 0x50) + RS16(o + 0x4A)));
        W16(o + 0x4A, (uint16_t)(RS16(o + 0x4A) + 0x50));
    }
    if (R16(o + 0x58) & 0x8000) {                                   /* anim over */
        W16(o + 0x14, 0);
        AND16(o + 0x58, 0x7FFF);
        int16_t m = RS16(o + 0x4E);
        if (m == 3) {
            if ((rnd_div(10) & 0xF) < 8) { W16(o + 0x4E, 0); W16(o + 6, 0); }
            else {
                GolemC_Sink(o);
                W16(o + 0x4A, (uint16_t)(((rnd_div(10) & 1) + 1) * 0x40));
            }
        } else if (m == 7) { W16(o + 0x4E, 0); W16(o + 6, 0); }
        else if (m == 1) {
            if (!(R16(o + 4) & 2)) W16(o + 0x22, 0);
            else { OR16(o + 4, 1); W16(o + 0x4E, 0); W16(o + 6, 0); AND16(o + 4, 0xFFFD); AND16(o + 2, 0xFFF7); }
        } else if (m == 6) {
            if (!(R16(o + 4) & 2)) W16(o + 0x22, 0);
            else { AND16(o + 4, 0xFFFE); W16(o + 0x4E, 0); W16(o + 6, 0); AND16(o + 4, 0xFFFD); }
        } else if (m == 5) { GolemC_Die(o); return; }
        else if (m == 2) {
            if (R16(o + 6) == 3) { if (!(R16(o + 2) & 0x8000)) OR16(o + 2, 0x8000); else AND16(o + 2, 0x7FFF); }
            W16(o + 0x4E, 0);
            W16(o + 6, 0);
        } else if (m == 4 && R16(o + 6) == 7) {                     /* (never set) */
            if (RS16(o + 0x16) > 0) { AND16(o + 2, 0xFFF7); W16(o + 0x4E, 0); W16(o + 6, 0); }
            else GolemC_Dying(o);
        }
    }
integrate:
    Obj_Integrate(o);
    Enemy_AnimSwitch();
draw:
    if (R16(o + 6) == 0xC) {
        GolemC_Part(9, 0, RS16(o + 0x50) * 0x100, 0, (uint16_t)((RS16(o + 0x48) >> 3) & 3), -1);
        GolemC_Part(10, 0, 0, 0, (uint16_t)((RS16(o + 0x48) >> 3) & 3), -1);
    }
    if (R16(o + 6) == 0)
        GolemC_Part(0xB, (R16(o + 2) & 0x8000) ? (int32_t)0xFFB20000 : 0x004E0000, 0x006D0000, 2, 0,
                    (int16_t)(((R16(o + 0x4C) & 0xFF) >> 2) % 12));   /* A.BIN 0x0600A604 */
    W16(SPR + 0x0E, 0);
    if (R16(o + 6) == 8) W16(SPR + 0x0E, R16(o + 0x58) & 3);
    W16(SPR + 2, (uint32_t)((RS32(o + 8) - RS32(CAM_X)) >> 16));
    W16(SPR + 4, (uint32_t)((RS32(o + 0xC) - RS32(CAM_Y)) >> 16));
    W16(SPR + 0x10, (R16(o + 2) & 0x8000) | 0x200);
    W16(SPR + 0x0A, (uint16_t)COLOR_BANK(s));
    if ((R16(o + 4) & 8) && (R16(0x060143EAu) & 1)) W16(SPR + 0x0A, (uint16_t)(COLOR_BANK(s) + 3));
    if (R16(o + 6) == 0xC) {
        W16(SPR + 0x0A, (uint16_t)(COLOR_BANK(s) + 1));
        W16(SPR + 2, (uint16_t)(RS16(SPR + 2) + ((R16(o + 2) & 0x8000) ? -0xF : 0xF)));
        W16(SPR + 4, (uint16_t)(RS16(SPR + 4) + 0x41));
        W16(SPR + 0x0E, (uint16_t)((RS16(o + 0x48) >> 3) & 3));
    }
    W16(SPR + 0x0C, 0);
    W32(SPR + 0x18, 0);
    if (R16(o) != 0xFFFF) Anim_Tick(o + 0x58);
}
