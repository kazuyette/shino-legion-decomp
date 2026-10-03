/* Water objects of stage 1-1 (0x1005 waves, 0x1006 splash) and the end-of-stage
 * checks (exit zone, falling death, stage state sequence), ported from SHINOBI.PPB.
 * Style and field conventions: see objhandlers.c / objhandlers.h. */
#include "objhandlers.h"
#include "objhandlers_c.h"
#include "fade.h"
#include "objects.h"
#include "anim.h"
#include "fixmath.h"
#include "player_ext.h"
#include "ram.h"
#include <stdio.h>

#define CUR       R32(OBJ_CUR)
#define PL        0x060EA2B4u           /* player object */
#define CAM_X     0x060252A0u
#define CAM_Y     0x060252A4u
#define FROZEN    (R16(0x0602529Eu) & 0x8000)
#define SPR       0x060250ACu
#define STAGE_IDX 0x0602526Au
#define FRAME     0x060F4CCCu           /* stage frame counter (0x06066958) */
#define VBLANKS   0x060143EAu           /* A.BIN frame counter */
#define LIVES     0x060252B8u
#define DEMO      0x06014408u           /* u32: 0x8000 demo playback, 0x4000 ?, 0x2000 demo ended */
#define FX_BUSY   0x0602510Cu           /* A.BIN screen-fade sequencer state (0 = idle) */

/* ---- 0x1005: water surface (stage 1-1) ----
 * Five 128-px sprites (bank 0x1D, anim 2 of 0x060A9400/0x060D1B18) laid along the
 * object's Y, re-anchored every frame on the camera (X snapped to 128 px) so the strip
 * always covers the screen. Breathing scale 1 + 0.75 cos(2t) (SPR +0x20) and a 64-px
 * shift every 128 frames. Only while the camera is low enough to see the water
 * (camY >= 1024+224) and camX - 1024 in [0x1E0, 0x20E0]. */
void ObjHandler_1005(void)                                 /* 0x0604B838 */
{
    uint32_t o = CUR;
    if (R16(o + 2) & 1) {                                   /* spawn */
        W16(o + 6, 2);
        Anim_Start(o + 0x58, 0x060A9400u, 0x060D1B18u, 0x10, RS16(o + 6));
        AND16(o + 2, 0xFFFE);
        W16(o + 0x14, 0);
        W16(o + 0x1C, 0x8000);
    }
    if (!(RS32(CAM_Y) + (int32_t)0xFC000000 >= 0x00E00000)) return;
    int32_t cx = RS32(CAM_X) + (int32_t)0xFC000000;
    if (!(cx >= 0x01E00000) || cx > 0x20E00000) return;
    W32(o + 8, (uint32_t)(((RS32(CAM_X) + (int32_t)0xFB800000) & (int32_t)0xFF800000) + 0x04000000));
    for (int16_t i = 0; i < 5; i++) {
        W16(SPR + 0x0E, 0);
        W16(SPR + 0x10, 0x0A00);
        int32_t c = Cos8((int16_t)((R16(FRAME) << 1) & 0xFF));
        W32(SPR + 0x20, (uint32_t)(FixMul(c, 0xC000) + 0x10000));
        W16(SPR + 0x0A, 0x1D);
        int32_t shift = (int32_t)((((uint32_t)R16(FRAME) << 1) + 0x80) & 0x100) / 4;   /* A.BIN 0x060092BC (sdiv) */
        W16(SPR + 0x02, (uint32_t)(shift * 0x10000 + RS32(o + 8) - RS32(CAM_X)) >> 16);
        W16(SPR + 0x04, (uint32_t)(RS32(o + 0xC) - RS32(CAM_Y)) >> 16);
        W16(SPR + 0x0C, 0);
        W16(SPR + 0x14, 0);
        W32(SPR + 0x18, 0xFF);
        if (R16(VBLANKS) & 1) W32(SPR + 0x18, 0xFFFFFF00u);
        Anim_Tick(o + 0x58);
        W32(o + 8, R32(o + 8) + 0x00800000);
    }
}

/* ---- 0x1006: water splash (stage 1-1) ----
 * When Sho's feet cross the water line (world y 0x5EE) going down, moves to his X on
 * the line, plays splash anim 5 + sound 0x204; drawn (bank 0x1D) until the animation
 * ends. +0x14 = visible. */
#define WATER_Y 0x05EE0000
void ObjHandler_1006(void)                                 /* 0x0604CB56 */
{
    uint32_t o = CUR;
    if (R16(o + 2) & 1) {                                   /* spawn */
        W16(o + 6, 5);
        AND16(o + 2, 0xFFFE);
        W16(o + 0x14, 0);
        W16(o + 0x1C, 0x8000);
    }
    if (!FROZEN) {
        if (RS32(PL + 0xC) > WATER_Y && !(RS32(PL + 0x2C) >= WATER_Y)) {   /* +0x2C = previous Y */
            W32(o + 8, R32(PL + 8));
            W32(o + 0xC, WATER_Y);
            Anim_Start(o + 0x58, 0x060A9400u, 0x060D1B18u, 0x10, RS16(o + 6));
            W16(o + 0x14, 1);
            sub_06007CDE(0x204);
        }
        if (R16(o + 0x58) & 0x8000) W16(o + 0x14, 0);      /* animation finished */
    }
    if (R16(o + 0x14) == 0) return;
    W16(SPR + 0x0E, 0);
    W16(SPR + 0x10, 0x0200);
    W16(SPR + 0x0A, 0x1D);
    W16(SPR + 0x02, (uint32_t)(RS32(o + 8) - RS32(CAM_X)) >> 16);
    W16(SPR + 0x04, (uint32_t)(RS32(o + 0xC) - RS32(CAM_Y)) >> 16);
    W16(SPR + 0x0C, 0);
    W32(SPR + 0x18, 0xFF);
    if (R16(VBLANKS) & 1) W32(SPR + 0x18, 0xFFFFFF00u);
    Anim_Tick(o + 0x58);
}

/* ---- end-of-stage checks ---- */

/* 0x06046146: Sho's position inside [x0,x1] x [y0,y1] (16.16 world, inclusive). */
static int player_in_box(int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    int32_t x = RS32(PL + 8), y;
    if (!(x >= x0) || x > x1) return 0;
    y = RS32(PL + 0xC);
    if (!(y >= y0) || y > y1) return 0;
    return -1;
}
#define PX(v) ((int32_t)(v) << 16)

/* Exit zone per stage index (world px). Indexes 1/5/7/11/13/15/17 have none (boss
 * stages: they end elsewhere). Cases 3 and 9 also need player flags4 & 0x10. The
 * original returns whatever is in r0 (unused by its only caller 0x06046190); the port
 * returns 1 when the exit fired. */
int Stage_CheckExit(void)                                  /* 0x06045F64 */
{
    int r = 0;
    if (R16(G_STAGE_STATE) & 7) return 0;                  /* a sequence is already running */
    switch (R16(STAGE_IDX)) {                               /* unsigned: < 0 or > 16 -> none */
    case 0:  r = player_in_box(PX(0x2B95), PX(0x400), PX(0x2BE5), PX(0x600)); break;
    case 2:  r = player_in_box(PX(0x792),  PX(0x5CF), PX(0x7C2),  PX(0x5E7)); break;
    case 3:  if (R16(PL + 4) & 0x10) r = player_in_box(PX(0x57C), PX(0x4E6), PX(0x59A), PX(0x4FA)); break;
    case 4:  r = player_in_box(PX(0x17E0), PX(0x400), PX(0x17F0), PX(0x500)); break;
    case 6:  r = player_in_box(PX(0x17B0), PX(0x780), PX(0x17EF), PX(0x800)); break;
    case 8:  r = player_in_box(PX(0x7BA0), PX(0x400), PX(0x7C00), PX(0x800)); break;
    case 9:  if (R16(PL + 4) & 0x10) r = player_in_box(PX(0x662), PX(0x4B0), PX(0xA50), PX(0x4C4)); break;
    case 10: r = player_in_box(PX(0x27B8), PX(0x840), PX(0x27FF), PX(0x970)); break;
    case 12: r = player_in_box(PX(0x37D8), PX(0x5A0), PX(0x3800), PX(0x5E0)); break;   /* 1-1 */
    case 14: r = player_in_box(PX(0x2BD0), PX(0x400), PX(0x2D00), PX(0x600)); break;
    case 16: r = player_in_box(PX(0x33C0), PX(0x400), PX(0x3400), PX(0x4C0)); break;
    default: break;                                         /* 1, 5, 7, 11, 13, 15, >= 17 */
    }
    if ((int16_t)r == 0) return 0;
    W16(G_STAGE_STATE, (R16(G_STAGE_STATE) & 0xFFF8) | 10); /* stage clear, just requested */
    return 1;
}

/* 0x06045EE4: fell off the bottom: 736 px below the camera top (544 px on stage
 * indexes 7 and 9) -> airborne cleared, HP 0, hit flag (player code then starts the
 * death sequence, state 1|8). */
void Player_CheckFall(void)
{
    int fell = 0;
    if (R16(G_STAGE_STATE) & 7) return;
    if (RS32(PL + 0xC) > RS32(CAM_Y) + 0x02E00000) fell = 1;
    if (RS32(PL + 0xC) > RS32(CAM_Y) + 0x01E00000 && (RS16(STAGE_IDX) == 9 || RS16(STAGE_IDX) == 7)) fell = 1;
    if (!fell) return;
    AND16(PL + 2, 0xBFFF);
    W16(PL + 0x16, 0);
    OR16(PL + 2, 8);
}

/* A.BIN 0x06005BB4 */
static void abin_06005BB4(void)
{
    W16(0x0603A570u, R16(0x0603A570u) & 0x7FFF);
    W16(0x0603A474u, 1);
}

/* 0x06046190: last call of the stage tick 0x06040B44. */
void Stage_UpdateState(void)
{
    Player_CheckFall();                                     /* 0x06045EE4 */
    Stage_CheckExit();                                      /* 0x06045F64 */
    if (!(R16(G_STAGE_STATE) & 7)) return;
    if (R16(G_STAGE_STATE) & 8) {                           /* first frame of the request */
        if (R16(FX_BUSY) == 0) {
            Fade_StageOutro();                              /* 0x0600725C(0), 0x06006F48(0x100 x3), 0x060072AC(16,2,1), 0x06006FDE */
        }
        if ((R16(G_STAGE_STATE) & 7) == 2 && RS16(PL + 0x16) <= 0)   /* cleared while dying */
            W16(G_STAGE_STATE, (R16(G_STAGE_STATE) & 0xFFF8) | 1);
        if (R32(DEMO) & 0x8000) {                           /* demo: any end -> demo over */
            W16(G_STAGE_STATE, (R16(G_STAGE_STATE) & 0xFFF8) | 3);
            W32(DEMO, R32(DEMO) | 0x2000);
        }
        AND16(G_STAGE_STATE, 0xFFF7);
        return;
    }
    if (R16(FX_BUSY) != 0) return;                          /* wait for the fade */
    abin_06005BB4();
    switch (R16(G_STAGE_STATE) & 7) {
    case 1:                                                 /* death: restart at the checkpoint */
        W16(LIVES, R16(LIVES) - 1);
        W32(0x060252D4u, 0);
        W32(0x060252D8u, 0);
        W16(G_GAME_MODE, RS16(LIVES) >= 0 ? R16(G_GAME_MODE) - 1 : 9);   /* 6 -> 5 reload, 9 game over */
        W16(G_RESPAWN, 1);
        break;
    case 2:                                                 /* stage clear */
        if (!(R16(STAGE_IDX) & 1)) {                        /* x-1 -> load x-2 directly (mode 5) */
            W16(G_GAME_MODE, R16(G_GAME_MODE) - 1);
            if (RS16(STAGE_IDX) == 16) { W32(G_DIRECTOR_REQ, 4); W16(G_GAME_MODE, 11); }
        } else {
            W16(G_GAME_MODE, 7);                            /* x-2: bonus tally, then STGCLEAR */
        }
        W16(G_CHECKPOINT, 0);                               /* 0x0604878C */
        W16(G_RESPAWN, 0);                                  /* 0x06048794 */
        W16(STAGE_IDX, R16(STAGE_IDX) + 1);
        if (RS16(STAGE_IDX) > 17) W16(STAGE_IDX, 0xFFFF);
        abin_06005BB4();
        break;
    case 3:                                                 /* demo over */
        W16(G_GAME_MODE, 11);
        W16(STAGE_IDX, 0xFFFF);
        W32(G_DIRECTOR_REQ, (R32(DEMO) & 0x4000) ? 1 : 0);
        break;
    default: break;
    }
}
