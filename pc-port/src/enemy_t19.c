/* Enemy type 0x0019 (stage 9-1): flying mine-sled and its launchers. Loader 0x0609F380,
 * handler 0x0609F3D0 (jump table 0x0609FD80 on +0x12 & 0xF), launch 0x0609FE68,
 * explosion = projectile 0x23 (0x0609FE92). Graphics: defs 0x060C533C / 0x060E8460,
 * 256-colour palette 0x060E703C.
 *  sub 0: the sled (anim 0) flies left 3.125 px/frame; Sho can ride it (0x06060268,
 *         carried with PL+0x38; when Sho is dead only above y 950); deleted 512 px left /
 *         1024 px right of the camera; frozen (no draw) more than 640 px from the screen
 *         centre; while it is still 320+ px right of the camera it arms the sky flash
 *         0x060F8328. Its attack box touching Sho's body -> 11 explosions (projectile 0x23,
 *         delays 1/8/16 frames, 10 damage each, all around it), shake C+0x20 |= 0x80, and
 *         sub 0xC (16 frames of blinking, then gone, shake off).
 *  sub 1, 2, 6-0xB: launcher at Sho's height: armed (+4 bit 1) when Sho is 0-32 px left of
 *         it, launching (+4 bit 0) once he is 0-16 px right of it; then a sled every
 *         256 frames, 704 px (sub 2) / 416 px ahead, sound 0x210, until Sho leaves
 *         [x - 64, x + 128] px.
 *  sub 3: same, armed at x-208..x-192 px, launching x-128..x px (only above y 1609), every
 *         128 frames, cancelled outside [x - 208, x + 85] px.
 *  sub 4 / 5: like 1 but every 128 frames, cancelled left of x - 256 px (and sub 4 right
 *         of x + 128 px).
 *  sub 0xD: palette controller: every other frame steps the 256 colours of the bank
 *         towards 0x060E723C / 0x060E703C alternately (A.BIN 0x06006000); the background
 *         colours 0x100.. are the map's own palette (+0xE00), or, while 0x060F8328 > 0,
 *         the lightning palette 0x060E743C + ((frame >> 1) & 0xF) * 0x100 (128 colours). */
#include "enemy_more.h"
#include "anim.h"
#include "sprite.h"
#include "player_ext.h"
#include "objects.h"
#include "objhandlers.h"
#include "objhandlers_d.h"
#include "fx.h"

#define T19_DEFS 0x060C533Cu
#define T19_SCR  0x060E8460u
#define FLASH    0x060F8328u                 /* s32: lightning frames left */
#define SHAKE    0x060EA638u

void Enemy19_Load(void)                                     /* 0x0609F380 */
{
    Enemy_RegisterChars(0x19, 0x060E7008u);
    int16_t s = (int16_t)Enemy_FindSlot(0x19);
    Pal_Copy((R16(0x060A9084u + 2u * (uint32_t)s) & 0xFFF) << 4, 0x100, 0x060E703Cu);
}

static void Sled19_Launch(int32_t dx)                       /* 0x0609FE68 */
{
    uint32_t o = O;
    Obj_Register(0x19, RS32(o + 8) + dx, RS32(o + 0xC), 0, 0xFFFF);
}

static void Sled19_Draw(uint32_t o, int16_t s)
{
    W16(SPR + 0x0E, 0);
    W16(SPR + 0x10, 0x200);
    W16(SPR + 0x0A, (uint16_t)COLOR_BANK(s));
    W16(SPR + 2, (uint32_t)((RS32(o + 8) - RS32(CAM_X)) >> 16));
    W16(SPR + 4, (uint16_t)((int16_t)((uint32_t)(RS32(o + 0xC) - RS32(CAM_Y)) >> 16) + RS16(o + 0x4C)));
    W16(SPR + 0x0C, 0);
    W32(SPR + 0x18, 0x80);
    if ((R16(o + 0x12) & 0xF) == 0xC) {
        W16(SPR + 0x0E, (R16(o + 0x14) >> 2) & 3);
        if (R16(0x060143EAu) & 1) W32(SPR + 0x18, 0x7F);
    }
    if (!(R16(o + 0x58) & 0x8000)) Anim_Tick(o + 0x58);
}

/* launcher: Sho must come from the left within [x+lo, x+hi) to arm it */
static void Sled19_Launcher(uint32_t o, uint16_t sub)
{
    int32_t sx = RS32(PL + 8), x = RS32(o + 8);
    W16(o + 0x24, R16(o + 0x10));
    AND16(o + 0x10, 0xEFFF);
    if (sub == 3) {
        if (!(R16(o + 4) & 2)) {
            if (sx <= x - 0xD00000 || x - 0xC00000 <= sx) return;
            OR16(o + 4, 2);
        }
        if (sx < x - 0xD00000 || x + 0x550000 < sx) { AND16(o + 4, 0xFFFE); AND16(o + 4, 0xFFFD); return; }
        if (!(R16(o + 4) & 1) && x - 0x800000 < sx && sx < x && RS32(PL + 0xC) < 0x6490000) {
            W16(o + 0x14, 0);
            OR16(o + 4, 1);
        }
        if (R16(o + 4) & 1) {
            if ((R16(o + 0x14) & 0x7F) == 0) { OR16(o + 4, 1); sub_06007CDE(0x210); Sled19_Launch(0x1A00000); }
            if (sx < x - 0xD00000) { AND16(o + 4, 0xFFFE); AND16(o + 4, 0xFFFD); }
            if (x + 0x550000 < sx) { AND16(o + 4, 0xFFFE); AND16(o + 4, 0xFFFD); }
            W16(o + 0x14, R16(o + 0x14) + 1);
        }
        return;
    }
    if (!(R16(o + 4) & 2)) {
        if (sx <= x - 0x200000 || x <= sx) return;
        OR16(o + 4, 2);
    }
    if (!(R16(o + 4) & 1) && x < sx && sx < x + 0x100000) {
        W16(o + 0x14, 0);
        OR16(o + 4, 1);
    }
    if (!(R16(o + 4) & 1)) return;
    if (sub == 4 || sub == 5) {
        if ((R16(o + 0x14) & 0x7F) == 0) { OR16(o + 4, 1); sub_06007CDE(0x210); Sled19_Launch(0x1A00000); }
        if (sx < x - 0x1000000) { AND16(o + 4, 0xFFFE); AND16(o + 4, 0xFFFD); }
        if (sub == 4 && x + 0x800000 < sx) { AND16(o + 4, 0xFFFE); AND16(o + 4, 0xFFFD); }
    } else {
        if ((R16(o + 0x14) & 0xFF) == 0) {
            OR16(o + 4, 1);
            sub_06007CDE(0x210);
            Sled19_Launch(sub == 2 ? 0x2C00000 : 0x1A00000);
        }
        if (sx < x - 0x400000) { AND16(o + 4, 0xFFFE); AND16(o + 4, 0xFFFD); }
        if (x + 0x800000 < sx) { AND16(o + 4, 0xFFFE); AND16(o + 4, 0xFFFD); }
    }
    W16(o + 0x14, R16(o + 0x14) + 1);
}

void EnemyHandler_0019(void)                                /* 0x0609F3D0 */
{
    uint32_t o = O;
    int16_t s = SLOT;
    if (R16(o + 2) & 1) {
        Anim_Start(o + 0x58, T19_DEFS, T19_SCR, CHAR_BASE(s), 0);
        AND16(o + 2, 0xFFFE);
        W16(o + 0x14, 0);
        W16(o + 0x1C, 0x4000);
        OR16(o + 0x12, 0x4000);
        W16(o + 0x48, 0); W16(o + 0x4A, 0); W16(o + 0x4C, 0);
        if (R16(o + 0x12) & 0xF) W32(FLASH, 0);
    }
    uint16_t sub = R16(o + 0x12) & 0xF;
    if (FROZEN) {
        if (sub == 0 || sub == 0xC) Sled19_Draw(o, s);
        return;
    }
    switch (sub) {
    case 0: {                                                       /* the sled */
        if (RS32(o + 8) < RS32(CAM_X) - 0x2000000 || RS32(CAM_X) + 0x4000000 < RS32(o + 8)) { W16(o, 0xFFFF); return; }
        int32_t d = RS32(o + 8) - RS32(CAM_X) - 0xA00000;
        if (d < 0) d = RS32(CAM_X) - RS32(o + 8) + 0xA00000;
        if (d > 0x2800000) { W32(o + 8, R32(o + 8) - 0x32000); return; }
        if (RS32(o + 8) - RS32(CAM_X) - 0xA00000 > 0xA00000) W32(FLASH, 1);
        W32(OBJ_CUR, o);
        int16_t on;
        if (RS16(PL + 0x16) < 1) {
            on = -1;
            if (RS32(PL + 0xC) < 0x3B60000) on = (int16_t)Obj_PlayerContact(R16(o + 2) & 0x8000);
        } else on = (int16_t)Obj_PlayerContact(R16(o + 2) & 0x8000);
        W32(o + 8, R32(o + 8) - 0x32000);
        if (on == 2) W32(PL + 0x38, R32(PL + 0x38) - 0x32000);      /* carry Sho */
        if ((int16_t)Hitbox_Test(o, 2, PL, 0) != -1) {              /* hits Sho: blows up */
            static const int8_t pts[11][3] = {
                { 0, 3, 1 }, { 0, -3, 8 }, { 4, 3, 8 }, { -4, -3, 8 }, { 4, 0, 0x10 }, { -4, 0, 0x10 },
                { 4, 3, 1 }, { -4, -3, 1 }, { -5, -1, 0x10 }, { -5, 4, 1 }, { -6, 5, 1 } };
            for (int i = 0; i < 11; i++)
                Projectile_Spawn(0x23, RS32(o + 8) + pts[i][0] * 0x100000, RS32(o + 0xC) + pts[i][1] * 0x100000, pts[i][2], 0);
            W16(o + 0x12, (R16(o + 0x12) & 0xFFF0) | 0xC);
            W16(o + 0x14, 0);
            OR16(SHAKE, 0x80);
        }
        Sled19_Draw(o, s);
        return;
    }
    case 0xC:                                                       /* exploding */
        W16(o + 0x14, R16(o + 0x14) + 1);
        if (R16(o + 0x14) > 0xF) { W16(o, 0xFFFF); AND16(SHAKE, 0xFF7F); return; }
        Sled19_Draw(o, s);
        return;
    case 0xD: {                                                     /* palette controller */
        int dst = (R16(0x060A9084u + 2u * (uint32_t)s) & 0xFFF) << 4;
        if (R16(0x060143EAu) & 1) {
            if (R16(o + 0x48) == 0) { if ((int16_t)Pal_StepTowards(dst, 0x100, 0x060E723Cu, 1) != 0) W16(o + 0x48, 1); }
            else if ((int16_t)Pal_StepTowards(dst, 0x100, 0x060E703Cu, 1) != 0) W16(o + 0x48, 0);
        }
        if (RS32(FLASH) < 1) Pal_Copy(0x100, 0x100, R32(0x060EA650u) + 0xE00);
        else {
            Pal_Copy(0x100, 0x80, 0x060E743Cu + ((R16(0x060143EAu) >> 1) & 0xF) * 0x100u);
            W32(FLASH, (uint32_t)(RS32(FLASH) - 1));
        }
        return;
    }
    case 0xE: case 0xF:                                             /* beyond the table: draw */
        Sled19_Draw(o, s);
        return;
    default:                                                        /* 1-0xB: launchers */
        Sled19_Launcher(o, sub);
        return;
    }
}

/* Projectile 0x23 (0x0609FE92): explosion piece (anim 1) after a delay of +0x14 frames
 * (sound 0x121 when it goes off); 10 damage to Sho (0x06060A7C mode 0xC100). */
void Proj_Blast23(uint32_t e)
{
    int16_t s = (int16_t)Enemy_FindSlot(0x19);
    if (R16(e + 4) & 1) {
        Anim_Start(e + 0x20, T19_DEFS, T19_SCR, CHAR_BASE(s), 1);
        AND16(e + 4, 0xFFFE);
    }
    if (R16(e + 0x14) != 0) {
        W16(e + 0x14, R16(e + 0x14) - 1);
        if (R16(e + 0x14) == 0) sub_06007CDE(0x121);
        return;
    }
    if (!FROZEN) {
        W16(e + 0x12, 0x7F);
        Projectile_Collide(10, 0xC100);
    }
    W16(SPR + 2, (uint32_t)((RS32(e + 8) - RS32(CAM_X)) >> 16));
    W16(SPR + 4, (uint32_t)((RS32(e + 0xC) - RS32(CAM_Y)) >> 16));
    W16(SPR + 0x0A, (uint16_t)COLOR_BANK(s));
    W16(SPR + 0x10, 0x200);
    W32(SPR + 0x18, 0x81);
    W16(SPR + 0x0C, 0);
    W16(SPR + 0x0E, 0);
    if (R16(e) != 0xFFFF) Anim_Tick(e + 0x20);
    if (R16(e + 0x20) & 0x8000) W16(e, 0xFFFF);
}
