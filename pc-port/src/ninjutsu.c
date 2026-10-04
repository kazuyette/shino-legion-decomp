/* Sho's ninjutsu (magic) and its screen effects, ported from SHINOBI.dec.bin.
 * See ninjutsu.h for the RAM layout and docs/ninjutsu.md for the flow.
 *
 * Kinds (MAGIC & 0xF) and what they do:
 *   0  burst of afterimages, then the Ikazuchi-like shield (6 points, absorbs damage)
 *   1  damages the on-screen enemies that are not flagged +0x12 & 0x8000 (8 HP)
 *   2  ZIRYU (earth dragon): damages every enemy on screen (8 HP), uses the scroll (normal X cast)
 *   3  heals Sho to full
 *   4  BISYAMON: arms the kind-4 sword (power 4, run special, sword flashes)
 *   5  nothing
 *   6  NYORAI (9-1 boss, after the six orbs): kills the boss object 0x1F
 * Effect states (0x060F4BF0): kind 0 -> 7, 2 -> 2, 4 -> 6, 6 -> 5. */
#include "ninjutsu.h"
#include "ram.h"
#include "player.h"
#include "player_ext.h"
#include "anim.h"
#include "sprite.h"
#include "fade.h"
#include "fixmath.h"
#include "resource.h"
#include "stage_hooks.h"
#include "stage_inits.h"
#include "fx.h"
#include "audio.h"
#include "sfx.h"
#include <stdio.h>

#define MAGIC        0x060252BAu
#define MAGIC_PTS    0x060252BCu
#define MAGIC_AVAIL  0x060252BEu
#define HP_MAX       0x060252B0u
#define MAGIC_SAVED  0x060EA43Cu
#define G_SYSFLAGS   0x0602529Eu
#define G_FRAME      0x060143EAu
#define CAM_X        0x060252A0u
#define CAM_Y        0x060252A4u
#define CAM_LOCKS    0x060EA638u
#define HAZARD_PTR   0x060EA41Cu
#define STAGE_C      0x060EA618u
#define OBJ_TBL      0x060EC65Cu
#define OBJ_CUR      0x060F205Cu
#define PFX          0x060EA32Cu   /* 2 player sub-objects, 0x78 bytes */
#define SPR          SPR_WORK

#define NJ_STATE     0x060F4BF0u
#define NJ_TIMER     0x060F4BF2u
#define NJ_FRAME     0x060F4BF4u
#define NJ_STEP      0x060F4BF6u
#define NJ_TABLE     0x060F4BF8u
#define NJ_V         0x060F4BFCu   /* per-effect variables */
#define NJ_REQ       0x060F4CA8u
#define NJ_CB        0x060F4CACu   /* step callback, original address (dispatched below) */
#define NJ_FADE      0x060F4CB0u   /* 3 x s32 colour offsets saved at the start */
#define NJ_PHASE     0x060F4CBCu
#define NJ_ANIM      0x060FFD00u   /* PC: the originals' on-stack anim state (0x24 bytes) */

#define FADE_CUR     0x06025100u
#define FADE_QUEUED  0x06025114u   /* u8 */

/* effect graphics: anim definitions / scripts (chars 0x480.., colour bank 0x68) */
#define ZIR_DEFS 0x060AE2ECu
#define ZIR_SCR  0x060D848Cu
#define NYO_DEFS 0x060AE396u
#define NYO_SCR  0x060D84D0u
#define BIS_DEFS 0x060AE42Au
#define BIS_SCR  0x060D850Cu

static int16_t  v16(uint32_t off)              { return RS16(NJ_V + off); }
static void     w16(uint32_t off, int v)       { W16(NJ_V + off, (uint32_t)v); }
static void __attribute__((unused)) add16(uint32_t a, int v)       { W16(a, (uint32_t)(RS16(a) + v)); }
static int32_t  smod(int32_t a, int32_t b)     { return b ? a % b : 0; }         /* A.BIN 0x0600A604 */
static uint32_t umod(uint32_t a, uint32_t b)   { return b ? a % b : 0; }         /* A.BIN 0x0600CEF4 */
static int32_t  sdiv(int32_t a, int32_t b)     { return b ? a / b : 0; }         /* A.BIN 0x060092BC */
static int16_t  scr16(int32_t world, uint32_t cam) { return (int16_t)((uint32_t)(world - RS32(cam)) >> 16); }
static int      fade_idle(void) { return R16(FADE_STATE) == 0 && Ram_Ptr(FADE_QUEUED)[0] == 0; }
static void     fade_cur(uint32_t r, uint32_t g, uint32_t b) { W32(FADE_CUR, r); W32(FADE_CUR + 4, g); W32(FADE_CUR + 8, b); }
static void     snd(int id) { sub_06007CDE(id); }

/* A.BIN 0x06008154 (sequence slot volume fade) / 0x060084BE (stop a slot): the PC sequencer has
 * no slot table at 0x06025138; slots are found by id (Sfx_CountLocked) and stopped by id. */
static void seq_volume(int slot, int vol, int rate)
{
    static int once;
    if (!once++) printf("[snd] 06008154 seq slot %d volume %d rate %d (not ported)\n", slot, vol, rate);
}
static int  seq_playing(int id) { return Sfx_CountLocked(id) > 0; }
static void seq_stop_id(int id) { Audio_Lock(); Sfx_StopId(id); Audio_Unlock(); }

/* ======================================================= player side */

/* 0x060468FC: free the 2 player sub-objects */
void PlayerFx_Reset(void)
{
    for (uint32_t i = 0; i < 2; i++) {
        uint32_t o = PFX + ((i * 0x78u) & 0xFFu);
        W16(o, 0xFFFF);
        W16(o + 2, 0); W16(o + 0x12, 0); W16(o + 4, 0); W16(o + 0x14, 0);
        Anim_Reset(o + 0x58);
    }
}

/* 0x060467DC: a free sub-object (0xFFFF), else the oldest (largest +0x14) */
static int PlayerFx_FindSlot(void)
{
    for (int i = 0; i < 2; i++)
        if (R16(PFX + (((uint32_t)i * 0x78u) & 0xFFu)) == 0xFFFF) return i;
    int best = -1, age = 0;
    for (int i = 0; i < 2; i++) {
        uint16_t a = R16(PFX + (((uint32_t)i * 0x78u) & 0xFFu) + 0x14);
        if ((int)a >= age) { best = i; age = (int16_t)a; }
    }
    return best;
}

/* 0x06046858: a sword flash at Sho's position (+0x12 & 0x10: crouched / 2 px lower anim) */
static int PlayerFx_Spawn(void)
{
    int i = PlayerFx_FindSlot();
    if (i != -1) {
        uint32_t o = PFX + (((uint32_t)i * 0x78u) & 0xFFu);
        W16(o, 0);
        W16(o + 0x12, 0);
        W16(o + 2, (P16(P_FLAGS2) & 0x8000) | 1);
        W16(o + 0x14, 0);
        W32(o + 8, R32(PLAYER + P_X));
        W32(o + 0xC, R32(PLAYER + P_Y));
        if (P16(P_FLAGS2) & 0x4000) {                        /* airborne */
            W32(o + 0xC, R32(o + 0xC) - 0x200000u);
            W16(o + 0x12, 0x10);
        }
        Anim_Reset(o + 0x58);
    }
    return i;
}

/* 0x06046782: kind 4 armed + anim event bit 1 of an attack -> sword flash */
void PlayerFx_OnAttack(void)
{
    uint16_t m = R16(MAGIC);
    if ((m & 0xF0) == 0x40 && (m & 0x4000) && (P16(P_ANIM) & 2)) {
        PlayerFx_Spawn();
        AND16(PLAYER + P_ANIM, 0xFFFD);
    }
}

/* 0x060465EA: one sword flash: grows for 0xC0 frames; hits (+0x12 & 1) while +0xE <= 2 */
static void PlayerFx_UpdateOne(uint32_t o)
{
    if (R16(o) == 0xFFFF) return;
    if (R16(o + 2) & 1) {
        Anim_Start(o + 0x58, 0x060A8A90u, 0x060CA718u, 0x480, 0);
        if (R16(o + 0x12) & 0x10) W16(o + 0x5A, 1);
        AND16(o + 2, 0xFFFE);
        W16(o + 0x14, 0);
        AND16(o + 0x12, 0xFFFE);
        sub_06007D60(0x119, 3);
    }
    W16(SPR + 0xE, 2);
    if (R16(o + 0x14) < 16 || R16(o + 0x14) > 0xB0) W16(SPR + 0xE, 3);
    int32_t s = (int32_t)((uint32_t)R16(o + 0x14) << 11) + 0xA000;
    W32(SPR + 0x20, (uint32_t)s);
    W32(SPR + 0x1C, (uint32_t)s);
    if (s > 0x14000) { W32(SPR + 0x1C, 0x14000); W32(SPR + 0x20, 0x14000); }
    AND16(o + 0x12, 0xFFFE);
    if (R16(SPR + 0xE) <= 2) OR16(o + 0x12, 1);
    if (!(R16(G_SYSFLAGS) & 0x8000)) {
        W16(o + 0x14, R16(o + 0x14) + 1);
        if (R16(o + 0x14) >= 0xC0) { W16(o, 0xFFFF); return; }
    }
    W16(SPR + 2, (uint32_t)scr16(RS32(o + 8), CAM_X));
    W16(SPR + 4, (uint32_t)scr16(RS32(o + 0xC), CAM_Y));
    W16(SPR + 0xA, 0x68);
    W16(SPR + 0x10, (R16(o + 2) & 0x8000) | 0x1A00);
    W16(SPR + 0xC, 0);
    W32(SPR + 0x18, 0x7F);
    if (R16(G_FRAME) & 1) W32(SPR + 0x18, 0xFFFFFFFEu);
    if (R16(o) != 0xFFFF) Anim_Tick(o + 0x58);
}

/* 0x060465B8 */
void PlayerFx_Update(void)
{
    for (uint32_t i = 0; i < 2; i++) PlayerFx_UpdateOne(PFX + ((i * 0x78u) & 0xFFu));
}

/* 0x06044D08: one character from SHOU.CHR (table 0x060CB020: u32 offset, s16 w, s16 h) */
static void Magic_LoadChar(uint16_t idx, int16_t chr, uint32_t off)
{
    uint32_t e = 0x060CB020u + (uint32_t)idx * 8u;
    Lz_DecompressRam(R32(e) + 0x00200000u, off + 0x25C04740u);
    Char_Register(chr, RS16(e + 4), RS16(e + 6), 0x20, off);
}

/* 0x06046968: kind-4 graphics (chars 0x480.., palette 0x680) */
static void Magic_LoadKind4Gfx(void)
{
    Lz_DecompressRam(0x060CC4F8u, 0x25C6A740u);
    Char_RegisterList(0x480, 0x66000, 0x060CE5A2u);
    Pal_Copy(0x680, 0x80, 0x060CC3F8u);
}

/* 0x06045E86 */
void Magic_RestoreGfx(void)
{
    uint16_t m = R16(MAGIC);
    if (!(m & 0x4000) || RS16(MAGIC_PTS) <= 0) return;
    switch ((m & 0xF0) >> 4) {
    case 0: Magic_LoadChar(0xAB, 0x480, 0x66000); break;
    case 4: Magic_LoadKind4Gfx(); break;
    }
}

/* 0x06049768 */
void Enemies_MagicDamage(int16_t dmg, uint16_t mode)
{
    for (int16_t s = 0; s < 0xC0; s++) {
        uint32_t o = OBJ_TBL + (uint32_t)(int32_t)(int16_t)(s * 0x78);
        W32(OBJ_CUR, o);
        if (R16(o) == 0xFFFF || (R16(o + 0x12) & 0x4000) || (R16(o + 2) & 8)) continue;
        int32_t x = RS32(o + 8), y = RS32(o + 0xC);
        int32_t dx = x - RS32(CAM_X) - 0xA00000;
        if (dx < 0) dx = RS32(CAM_X) - x + 0xA00000;
        if (dx > 0xC00000) continue;
        int32_t dy = y - RS32(CAM_Y) - 0x800000;
        if (dy < 0) dy = RS32(CAM_Y) - y + 0x800000;
        if (dy > 0xA00000) continue;
        switch (mode & 0xF) {
        case 0: break;
        case 1: if (R16(o + 0x12) & 0x8000) continue; break;
        case 2: if (!(R16(o + 0x12) & 0x8000)) continue; break;
        default: continue;
        }
        W16(o + 0x16, (uint32_t)(RS16(o + 0x16) - dmg));
        OR16(o + 2, 8);
    }
}

/* 0x06045D1E */
void Magic_Apply(void)
{
    switch (R16(MAGIC) & 0xF) {
    case 0:
        W16(MAGIC, (R16(MAGIC) & 0xFF0F) | 0x4000);
        W16(MAGIC_PTS, 6);
        Magic_LoadChar(0xAB, 0x480, 0x66000);
        break;
    case 1:
        Enemies_MagicDamage(8, 1);
        break;
    case 2:
        Enemies_MagicDamage(8, 0);
        W16(MAGIC_AVAIL, 0);
        if (R16(MAGIC_SAVED) != 0xFFFF) {                    /* re-arm the shield / kind 4 */
            W16(MAGIC, R16(MAGIC_SAVED) & 0xDFFF);
            Magic_RestoreGfx();
        }
        break;
    case 3:
        PW16(P_HP, R16(HP_MAX));
        break;
    case 4:
        W16(MAGIC, (R16(MAGIC) & 0xFF0F) | 0x4040);
        W16(MAGIC_PTS, 6);
        Magic_LoadKind4Gfx();
        break;
    case 6:
        for (int16_t s = 0; s < 0xC0; s++) {
            uint32_t o = OBJ_TBL + (uint32_t)(int32_t)(int16_t)(s * 0x78);
            if (R16(o) == 0x1F) { W16(o + 0x16, 0x8001); OR16(o + 2, 8); break; }
        }
        break;
    default:
        break;
    }
}

/* 0x060437C0: ninjutsu actions 0x15 (standing) / 0x2C (crouched) / 0x3E (air) / 0x4A (posture 4) */
void Player_NinjutsuExec(void)
{
    if (P16(0x5A) == 0 && P16(0x72) == 0) {                 /* first frame of the animation */
        W16(MAGIC_SAVED, 0xFFFF);
        if ((R16(MAGIC) & 0x4000) && RS16(MAGIC_PTS) > 0) W16(MAGIC_SAVED, R16(MAGIC));
        switch (R16(MAGIC) & 0xF) {                          /* open the effect's picture file */
        case 2: Res_Request(2, 0, 0, 0xFFFFFFFFu, 0, 0x060A8A3Cu); break;   /* ZIRYU.PBC */
        case 4: Res_Request(2, 0, 0, 0xFFFFFFFFu, 0, 0x060A8A48u); break;   /* BISYAMON.PBC */
        case 6: Res_Request(2, 0, 0, 0xFFFFFFFFu, 0, 0x060A8A58u); break;   /* NYORAI.PBC */
        }
    }
    if ((P16(P_ANIM) & PANIM_FINISHED) && R16(NJ_STATE) == 0) {
        if (!(R16(MAGIC) & 0x8000)) {                        /* pose done: start the effect */
            Ninjutsu_Start(R16(MAGIC));
            OR16(MAGIC, 0x8000);
            AND16(MAGIC, 0xBFFF);
            PlayerFx_Reset();
        } else {                                             /* effect over: apply, back to play */
            AND16(MAGIC, 0x7FFF);
            AND16(MAGIC, 0xDFFF);
            PW16(P_ACTION, ACT_IDLE);
            PW16(P_FLAGS2, P16(P_FLAGS2) & 0xEFFF);
            W16(R32(HAZARD_PTR), 0x60);                      /* 0x60 frames of invulnerability */
            PW32(P_VELY, 0);
            AND16(G_SYSFLAGS, 0x7FFF);                       /* unfreeze the world */
            Magic_Apply();
            Ram_Ptr(0x0603B4ACu)[0] = (uint8_t)((Ram_Ptr(0x0603B4ACu)[0] & 0xFB) | 4);  /* VDP2 shadow dirty */
            W16(0x0603B49Au, (R16(0x0603B49Au) & 0xFFF7) | 8);   /* CLOFEN: colour offset on NBG3 */
        }
    }
}

/* ======================================================= effect machinery */

static void cb_ziryu(void);
static void cb_nyorai(void);
static void cb_bisyamon(void);

/* the step callback 0x060F4CAC holds an original code address */
static void nj_callback(void)
{
    switch (R32(NJ_CB)) {
    case 0x06066908u: cb_ziryu(); break;
    case 0x060659AAu: cb_nyorai(); break;
    case 0x0606688Au: cb_bisyamon(); break;
    default: break;
    }
}

/* 0x06063C7A: next step now */
static void nj_next_step(void)
{
    W16(NJ_TIMER, 0);
    W16(NJ_STEP, R16(NJ_STEP) + 1);
    nj_callback();
}

/* 0x06063CB8: next step when the step's duration (table 0x060F4BF8) is reached */
static void nj_step_timer(void)
{
    uint16_t d = R16(R32(NJ_TABLE) + (uint32_t)R16(NJ_STEP) * 2u);
    if (d <= R16(NJ_TIMER) && d != 0xFFFF) nj_next_step();
}

/* 0x06063C40: draw frame 0 of `anim` with the shared sprite struct (colour bank 0x68) */
static void nj_draw(uint32_t defs, uint32_t scripts, int anim)
{
    Anim_Start(NJ_ANIM, defs, scripts, 0x480, anim);
    W16(SPR + 0xA, 0x68);
    W16(SPR + 0xC, 1);
    Anim_Tick(NJ_ANIM);
}

/* common end: fade the colour offsets back to the values saved at the start */
static void nj_fade_back(int32_t speed)
{
    Fade_SetTarget((uint16_t)(R32(NJ_FADE) >> 16), (uint16_t)(R32(NJ_FADE + 4) >> 16),
                   (uint16_t)(R32(NJ_FADE + 8) >> 16));
    Fade_SetEndLayerMask(8);
    Fade_SetNextSpeed(speed, 1);
    Fade_StartOut();
}

/* --- starts (0x060634A4 picks one) --- */

static void nj_begin(int state, uint32_t cb, uint32_t table)
{
    W32(NJ_CB, cb);
    W16(NJ_STATE, (uint32_t)state);
    W16(NJ_TIMER, 0);
    W16(NJ_STEP, 0);
    W32(NJ_TABLE, table);
}

/* 0x06063642: Ziryu (kind 2) */
static void nj_start_ziryu(void)
{
    if (R16(NJ_STATE) != 0) return;
    nj_begin(2, 0x06066908u, 0x060AE488u);
    W16(NJ_REQ, 2);
}

/* 0x0606379C: Nyorai (kind 6) */
static void nj_start_nyorai(void)
{
    if (R16(NJ_STATE) != 0) return;
    nj_begin(5, 0x060659AAu, 0x060AE498u);
    W16(NJ_REQ, 5);
    W16(0x060F4CC8u, 0);
    w16(0x00, 0x40);                     /* intro angle */
    w16(0x08, 0x46);                     /* ring A radius */
    w16(0x10, 1);
    w16(0x12, 0x46);                     /* ring B radius */
    W32(0x060F4C94u, 0x1000);            /* statue scale */
    W32(0x060F4C98u, 0x8000);
    w16(0x84, 0); w16(0x86, 0); w16(0x88, 0);
    w16(0x8A, R16(0x060AE4E4u)); w16(0x8C, R16(0x060AE514u)); w16(0x8E, R16(0x060AE534u));
    w16(0x90, 0); w16(0x92, 0); w16(0x94, 0);
}

/* 0x0606399C: Bishamon (kind 4) */
static void nj_start_bisyamon(void)
{
    if (R16(NJ_STATE) != 0) return;
    nj_begin(6, 0x0606688Au, 0x060AE5D4u);
    W16(NJ_REQ, 6);
    w16(0x00, 0x350);                    /* descent height */
    W32(0x060F4C94u, R32(CAM_X));        /* world anchor = camera at the cast */
    W32(0x060F4C98u, R32(CAM_Y));
    W32(0x060F4CA0u, 0x1000);
    w16(0x02, 0); w16(0x04, 0);
    w16(0x8C, 0); w16(0x8E, 0); w16(0x90, 30);
    for (uint32_t i = 5; i < 25; i++) w16(i * 2u, 0);   /* particle timers */
    snd(0x10E);
}

/* 0x06063B6C: burst of afterimages (kind 0) */
static void nj_start_burst(void)
{
    if (R16(NJ_STATE) != 0) return;
    nj_begin(7, 0, 0x060AE674u);
    W16(NJ_REQ, 0);
    static const uint16_t r[8] = { 0x154, 0x1EA, 0x280, 0x316, 0x3AC, 0x442, 0x4D8, 0x56E };
    for (uint32_t i = 0; i < 8; i++) w16(i * 2u, r[i]);
    snd(0x10D);
    Fade_SetTarget(0x1D0, 0x1D0, 0x1D0);
    Fade_SetNextSpeed(0x60000, 1);
    Fade_SetEndLayerMask(0x48);
    Fade_StartOut();
}

/* 0x060634A4 */
int Ninjutsu_Start(uint16_t flags)
{
    int k = flags & 0xF;
    for (uint32_t i = 0; i < 12; i += 2) W16(NJ_FADE + i, R16(FADE_CUR + i));   /* 0x0600A6CC: 12 bytes */
    W16(FADE_STATE, 0);
    Ram_Ptr(FADE_QUEUED)[0] = 0;
    switch (k) {
    case 0: nj_start_burst();    snd(0x11A); break;
    case 2: nj_start_ziryu();    snd(0x118); break;
    case 4: nj_start_bisyamon(); snd(0x11B); break;
    case 6: nj_start_nyorai();   snd(0x11C); break;
    default: break;
    }
    return k;
}

/* --- graphics loads requested through 0x060F4CA8 (the frame after the start) --- */

static void nj_load(uint32_t name, uint32_t list, uint32_t pal, uint16_t target)
{
    Res_Request(1, 1, 0, 0xFFFFFFFFu, 0x25C6A740u, name);
    Char_RegisterList(0x480, 0x66000, list);
    Pal_Copy(0x680, 0x80, pal);
    if (name == 0x060AE688u) Pal_Copy(0x6F0, 0x10, 0x060D831Cu);
    Fade_SetTarget(target, target, target);
    Fade_SetNextSpeed(0x60000, 1);
    Fade_SetEndLayerMask(0x48);
    Fade_StartOut();
}
static void nj_load_ziryu(void)    { nj_load(0x060AE67Cu, 0x060D806Cu, 0x060D80A8u, 0x1D0); }   /* 0x0606369C */
static void nj_load_nyorai(void)                                                              /* 0x06063870 */
{
    nj_load(0x060AE688u, 0x060D81A8u, 0x060D81FCu, 0x100);
    Res_Request(2, 0, 0, 0xFFFFFFFFu, 0, 0x060AE694u);                                   /* KAZUMA2.PBC */
}
static void nj_load_bisyamon(void) { nj_load(0x060AE6A0u, 0x060D833Cu, 0x060D8370u, 0x1D0); } /* 0x06063A6C */

/* 0x06063538 */
void Ninjutsu_Init(void)
{
    W16(NJ_STATE, 0); W16(NJ_TIMER, 0); W16(NJ_FRAME, 0); W16(NJ_STEP, 0);
    W32(NJ_TABLE, 0);
    W16(NJ_REQ, 0);
}

/* ======================================================= state 2: Ziryu (earth dragon) */

/* 0x06065ECC: the dragon's head, colour-calc level `cc` (3 = faint .. 0 = solid) */
static void zir_head(int cc)
{
    W16(SPR + 0x10, 0x200);
    W16(SPR + 0xE, (uint32_t)cc);
    W16(SPR + 2, 0xA0);
    W16(SPR + 4, 0x58);
    W32(SPR + 0x18, 0x8D);
    nj_draw(ZIR_DEFS, ZIR_SCR, 0);
}

/* 0x06065EEE: body coils; grow = 1: zoom in over step 3 (drawn every other frame) */
static void zir_body(int grow)
{
    if (grow && (R16(NJ_FRAME) & 1)) return;
    if (grow) {
        W16(SPR + 0x10, 0x1A00);
        int32_t d = sdiv((int32_t)((uint32_t)R16(NJ_TIMER) << 16), 80);
        int32_t s = FixMul(d, FixMul(d, 0x10000));
        W32(SPR + 0x1C, (uint32_t)s);
        W32(SPR + 0x20, (uint32_t)s);
    } else W16(SPR + 0x10, 0x200);
    W16(SPR + 0xE, 0);
    W16(SPR + 2, 0xA8); W16(SPR + 4, 0x70); W32(SPR + 0x18, 0x8F);
    nj_draw(ZIR_DEFS, ZIR_SCR, 1);
    W16(SPR + 2, 0x110); W16(SPR + 4, 0x60); W32(SPR + 0x18, 0x8C);
    nj_draw(ZIR_DEFS, ZIR_SCR, 2);
    W16(SPR + 0x10, grow ? 0x1E00 : 0x600);
    W16(SPR + 0x14, 0x40);
    W16(SPR + 2, 0x40); W16(SPR + 4, 0x3C); W32(SPR + 0x18, 0x8C);
    nj_draw(ZIR_DEFS, ZIR_SCR, 3);
}

/* 0x06066008: coiled body */
static void zir_coil(void)
{
    W16(SPR + 0x10, 0x200); W16(SPR + 0xE, 0);
    W16(SPR + 2, 0xBC); W16(SPR + 4, 0x40); W32(SPR + 0x18, 0x8F);
    nj_draw(ZIR_DEFS, ZIR_SCR, 3);
    W16(SPR + 0x10, 0x200);
    W16(SPR + 2, 0x8C); W16(SPR + 4, 0x38); W32(SPR + 0x18, 0x90);
    nj_draw(ZIR_DEFS, ZIR_SCR, 4);
    W16(SPR + 2, 0x110); W16(SPR + 4, 0x60); W32(SPR + 0x18, 0x8C);
    nj_draw(ZIR_DEFS, ZIR_SCR, 2);
    W16(SPR + 0x10, 0x600); W16(SPR + 0x14, 0x40);
    W16(SPR + 2, 0x40); W16(SPR + 4, 0x3C); W32(SPR + 0x18, 0x8C);
    nj_draw(ZIR_DEFS, ZIR_SCR, 3);
}

/* 0x060660B0: lightning strikes + white flashes (step 6) */
static void zir_strike(void)
{
    OR16(CAM_LOCKS, 0x80);
    if (!(R16(NJ_FRAME) & 0x1F)) fade_cur(0xFF0000, 0xFF0000, 0xFF0000);
    Fade_SetTarget(0x1D0, 0x1D0, 0x1D0);
    Fade_SetNextSpeed(0xA0000, 1);
    Fade_SetEndLayerMask(8);
    Fade_StartOut();
    W16(SPR + 0x10, 0x200); W16(SPR + 0xE, 0);
    W16(SPR + 4, 0xD0); W32(SPR + 0x18, 0x8C);
    if (smod((int32_t)R16(G_FRAME), 15) == 0) snd(0x117);
    static const int16_t bx[4] = { 0x70, 0x122, 0x24, 0xD0 };
    W16(SPR + 2, (uint32_t)bx[R16(NJ_FRAME) & 3]);
    nj_draw(ZIR_DEFS, ZIR_SCR, 5);
    W32(SPR + 0x18, 0x100);
    switch (smod((int32_t)R16(NJ_FRAME), 3)) {
    case 0: W16(SPR + 2, 0x110); break;
    case 1: W16(SPR + 2, 0x90); break;
    case 2: W16(SPR + 2, 0x30); break;
    default: return;
    }
    nj_draw(ZIR_DEFS, ZIR_SCR, 6);
}

/* 0x06066908 */
static void cb_ziryu(void)
{
    if (R16(NJ_STEP) == 3) snd(0x109);
    else if (R16(NJ_STEP) == 5) snd(0x10A);
}

/* 0x06065DAE. Steps (table 0x060AE488): 8, 8, 8 head fades in; 0x50 body zooms in; 0x28 body;
 * 0x14 coil; 0x5A strikes; 2 -> end (white, fade back, Sho resumes). */
static int nj_state_ziryu(void)
{
    Fade_HoldFlash();
    switch (R16(NJ_STEP)) {
    case 0: zir_head(3); break;
    case 1: zir_head(2); break;
    case 2: zir_head(1); break;
    case 3: zir_head(0); zir_body(1); break;
    case 4: zir_head(0); zir_body(0); break;
    case 5: zir_head(0); zir_coil(); break;
    case 6: zir_head(0); zir_coil(); zir_strike(); break;
    case 7:
        fade_cur(0xFF0000, 0xFF0000, 0xFF0000);
        AND16(CAM_LOCKS, 0xFF7F);
        nj_fade_back(0x40000);
        W16(NJ_STATE, 0);
        return 1;
    }
    return 0;
}

/* ======================================================= state 6: Bishamon */

/* 0x06066330: the god descends (anim 1) at the world anchor; step 0 lowers it to 0x1A0 */
static void bis_descend(int phase)
{
    W16(SPR + 0x10, 0x200); W16(SPR + 0xE, 0);
    W16(SPR + 2, (uint32_t)(int16_t)(scr16(RS32(0x060F4C94u), CAM_X) + 0xA0));
    W16(SPR + 4, (uint32_t)(int16_t)(scr16(RS32(0x060F4C98u), CAM_Y) + (v16(0) >> 1)));
    W32(SPR + 0x18, 0x8E);
    nj_draw(BIS_DEFS, BIS_SCR, 1);
    if (phase == 0) {
        OR16(CAM_LOCKS, 0x80);
        w16(0, v16(0) - 2);
        if (v16(0) <= 0x1A0) {
            w16(0, 0x1A0);
            nj_next_step();
            AND16(CAM_LOCKS, 0xFF7F);
        }
    }
}

/* 0x06066410: 20 flame particles (timers +0x0A..+0x30, x +0x32.., y +0x5A..), up to 4 new a frame */
static void bis_flames(int phase)
{
    int spawn = (phase == 2) ? 0 : 4;
    for (int i = 5; i < 25; i++) {
        uint32_t e = NJ_V + (uint32_t)i * 2u;
        if (R16(e) == 0) {
            if (spawn == 1) {
                W16(e, 7);
                W16(e + 40, R16(0x060AE616u + (uint32_t)R16(NJ_V + 0x8C) * 2u));
                W16(e + 80, (uint32_t)(int16_t)(R16(0x060AE65Cu + (uint32_t)R16(NJ_V + 0x8E) * 2u) + R16(NJ_V + 0x90)));
                spawn--;
                w16(0x8C, R16(NJ_V + 0x8C) >= 34 ? 0 : R16(NJ_V + 0x8C) + 1);
                w16(0x8E, R16(NJ_V + 0x8E) >= 11 ? 0 : R16(NJ_V + 0x8E) + 1);
            } else if (spawn != 0) {
                W16(e, 7);
                W16(e + 40, R16(0x060AE5E0u + (uint32_t)R16(NJ_V + 2) * 2u));
                W16(e + 80, (uint32_t)(int16_t)(R16(0x060AE600u + (uint32_t)R16(NJ_V + 4) * 2u) + R16(NJ_V + 0x90)));
                spawn--;
                w16(0x02, R16(NJ_V + 2) >= 15 ? 0 : R16(NJ_V + 2) + 1);
                w16(0x04, R16(NJ_V + 4) >= 10 ? 0 : R16(NJ_V + 4) + 1);
            } else continue;
        }
        W16(SPR + 0x10, 0x1200);
        int32_t t = RS16(e) - 7;
        W32(SPR + 0x1C, (uint32_t)(t * 0xC00 + 0x19000));
        W16(SPR + 0xE, 1);
        W16(SPR + 2, (uint32_t)(int16_t)(scr16(RS32(0x060F4C94u), CAM_X) + RS16(e + 40)));
        W16(SPR + 4, (uint32_t)(int16_t)(scr16(RS32(0x060F4C98u), CAM_Y) + RS16(e + 80)));
        W32(SPR + 0x18, 0x8F);
        nj_draw(BIS_DEFS, BIS_SCR, 0);
        W16(e, R16(e) - 1);
    }
    if (phase != 0) w16(0x90, v16(0x90) + 1);
    else if (v16(0x90) != 0) w16(0x90, v16(0x90) - 1);
}

/* 0x06066640: the god (anim 2); phase 3 adds up to 3 mirrored afterimage pairs moving out */
static void bis_god(int phase)
{
    W16(SPR + 0x10, 0x200); W16(SPR + 0xE, 0);
    W16(SPR + 2, 0x9F); W16(SPR + 4, 0xD0); W32(SPR + 0x18, 0x8F);
    nj_draw(BIS_DEFS, BIS_SCR, 2);
    if (phase != 3) return;
    if (v16(10) == 0) {
        w16(10, v16(10) + 1);
        w16(20, 0);
        w16(60, 0);
    } else if (smod((int32_t)R16(NJ_FRAME), 10) == 0 && v16(10) < 3) {
        int k = v16(10);
        W16(NJ_V + 20 + (uint32_t)k * 2u, R16(NJ_V + 18 + (uint32_t)k * 2u));
        W16(NJ_V + 60 + (uint32_t)k * 2u, (uint32_t)k);
        w16(10, k + 1);
    }
    for (int i = 0; i < v16(10); i++) {
        uint32_t off = NJ_V + 20 + (uint32_t)i * 2u, cc = NJ_V + 60 + (uint32_t)i * 2u;
        if (R16(G_FRAME) & 1) {
            W16(SPR + 0x10, 0x200); W16(SPR + 0xE, R16(cc));
            W16(SPR + 2, (uint32_t)(int16_t)(0x9F - RS16(off))); W16(SPR + 4, 0xD0);
            W32(SPR + 0x18, (uint32_t)(0x8E - i));
            nj_draw(BIS_DEFS, BIS_SCR, 2);
            W16(SPR + 0x10, 0x200); W16(SPR + 0xE, R16(cc));
            W16(SPR + 2, (uint32_t)(int16_t)(RS16(off) + 0x9F)); W16(SPR + 4, 0xD0);
            W32(SPR + 0x18, (uint32_t)(0x8E - i));
            nj_draw(BIS_DEFS, BIS_SCR, 2);
        }
        int lim = (i + 1) * 30;
        if (RS16(off) < lim) W16(off, (uint32_t)(RS16(off) + 2));
        else W16(off, (uint32_t)(int16_t)lim);
    }
}

/* 0x060667F8: rotating flash zooming in; white-out once tall enough */
static void bis_flash(void)
{
    W16(SPR + 0x10, 0x1E00);
    W16(SPR + 0x14, 0xA0);
    int32_t d = sdiv((int32_t)((uint32_t)R16(NJ_FRAME) << 16), 60);
    W32(SPR + 0x20, (uint32_t)FixMul(d, FixMul(d, 0x160000)));
    W32(SPR + 0x1C, (uint32_t)FixMul(d, FixMul(d, 0x60000)));
    W16(SPR + 0xE, 0);
    W16(SPR + 2, 0x98); W16(SPR + 4, 0x60); W32(SPR + 0x18, 0x100);
    nj_draw(BIS_DEFS, BIS_SCR, 4);
    if (RS32(SPR + 0x20) > 0x90000) {
        Fade_SetTarget(0xFF, 0xFF, 0xFF);
        Fade_SetNextSpeed(0xA0000, 1);
        Fade_SetEndLayerMask(8);
        Fade_StartOut();
    }
}

/* 0x0606688A */
static void cb_bisyamon(void)
{
    if (R16(NJ_STEP) == 3) {
        W16(NJ_FRAME, 1);
        w16(10, 0);
        snd(0x10F);
    } else if (R16(NJ_STEP) == 4) fade_cur(0xFF0000, 0xFF0000, 0xFF0000);
}

/* 0x06066252. Steps (table 0x060AE5D4): descent (until the god reached 0x1A0), 0x3C flames,
 * 1, 0x3C afterimages, 2 flash, 2 -> end. */
static int nj_state_bisyamon(void)
{
    Fade_HoldFlash();
    switch (R16(NJ_STEP)) {
    case 0: bis_descend(0); bis_flames(0); break;
    case 1: bis_descend(1); bis_flames(1); break;
    case 2: bis_god(2); bis_flames(2); break;
    case 3: bis_god(3); bis_flash(); break;
    case 4: bis_flash(); break;
    case 5:
        nj_fade_back(0x40000);
        W16(NJ_STATE, 0);
        return 1;
    }
    return 0;
}

/* ======================================================= state 7: burst (kind 0) */

/* 0x06063DE4: copies of Sho's current frame fly out in 8 directions (12 rings) */
static void burst_draw(void)
{
    static const uint8_t ang[8] = { 0xE0, 0x60, 0xA0, 0x20, 0xC0, 0x40, 0x80, 0x00 };
    int32_t ring = 0;
    int none = 1;
    for (int i = 0; i < 4; i++) {
        W16(SPR + 0xE, (uint32_t)i);
        W16(SPR + 0x10, (P16(P_FLAGS2) & 0x8000) | 0x200);
        W32(SPR + 0x18, (uint32_t)(0x7F - i));
        W16(SPR + 0xC, 1);
        W16(SPR + 0, R16(0x060EA420u) + 1u);                 /* Sho's streamed char slot */
        W16(SPR + 6, R16(R32(PLAYER + 0x64) + 2));
        W16(SPR + 8, R16(R32(PLAYER + 0x64) + 4));
        W16(SPR + 0xA, 0x40);
        for (int j = 0; j < 3; j++) {
            for (int d = 0; d < 8; d++) {
                int32_t r = (int32_t)((uint32_t)(int32_t)v16((uint32_t)d * 2u) << 16) + ring;
                if (r <= 0) continue;
                none = 0;
                int32_t x = FixMul(r, Cos8(ang[d])) + P32(P_X);
                int32_t y = FixMul(r, Sin8(ang[d])) + P32(P_Y);
                W16(SPR + 2, (uint32_t)scr16(x, CAM_X));
                W16(SPR + 4, (uint32_t)scr16(y, CAM_Y));
                Sprite_Draw(SPR);
            }
            ring += 0x140000;
        }
    }
    for (uint32_t d = 0; d < 8; d++) w16(d * 2u, v16(d * 2u) - 10);
    if (none) {                                               /* all copies back: white, fade in */
        fade_cur(0xFF0000, 0xFF0000, 0xFF0000);
        Fade_SetNextSpeed(0x80000, 1);
        Fade_SetEndLayerMask(8);
        Fade_StartIn();
        snd(0x109);
        nj_next_step();
    }
}

/* 0x06063CFC. Steps (table 0x060AE674): burst until all copies are back, 2, wait for the fade */
static int nj_state_burst(void)
{
    Fade_HoldFlash();
    switch (R16(NJ_STEP)) {
    case 0: burst_draw(); break;
    case 2:
        if (fade_idle()) nj_next_step();
        break;
    case 3:
        W16(NJ_STATE, 0);
        nj_fade_back(0x40000);
        return 1;
    }
    return 0;
}

/* ======================================================= state 5: Nyorai (9-1 boss) */

#define ORB_MASK  0x060F22C4u   /* orbs collected, not yet absorbed (bit per orb) */
#define ORB_TIMER 0x060F23C4u
#define ORB_LAST  0x060F23C6u
#define ORB_DONE  0x060F23C8u   /* orbs absorbed (not drawn by Orbs_Update) */
#define ORB_STATE 0x060F23CAu
#define ORB_X     0x060F2394u
#define ORB_Y     0x060F23ACu

static void tint(uint32_t r, uint32_t g, uint32_t b)
{
    W16(STAGE_C + 0x18, r); W16(STAGE_C + 0x1A, g); W16(STAGE_C + 0x1C, b);
}

/* 0x0605F0E6 (step 0): the six orbs pulse, then fly into Sho one by one (each tints him);
 * when all are absorbed the arena background (0x060AC810) is loaded. 1 = done. */
static int nyo_orbs(void)
{
    if (P32(P_X) < RS32(0x060F7AD8u)) PW16(P_FLAGS2, P16(P_FLAGS2) & 0x7FFF);   /* face the boss */
    else PW16(P_FLAGS2, P16(P_FLAGS2) | 0x8000);
    switch (R16(ORB_STATE)) {
    case 0:
        W16(ORB_TIMER, 0);
        W16(ORB_STATE, R16(ORB_STATE) + 1);
        W16(0x060EA426u, 0xFFFF);
        tint(0, 0, 0);
        break;
    case 1: {
        int32_t r = smod(RS16(ORB_TIMER), 24) - 12;
        int16_t d = (int16_t)(r < 0 ? -r : r);
        W16(ORB_TIMER, R16(ORB_TIMER) + 1);
        if (RS16(ORB_TIMER) > 0x81) {
            W16(ORB_TIMER, 0);
            W16(ORB_STATE, R16(ORB_STATE) + 1);
            break;
        }
        for (int k = 0; k < 6; k++)
            Pal_SetFaded((k + 82) * 16, 16, R32(0x060D2E70u + (uint32_t)k * 4u), d * 20, d * 20, d * 20);
        break;
    }
    case 2:
        if (R16(ORB_TIMER) == 0) {
            uint16_t bit = 1;
            for (int i = 0; i < 6; i++, bit = (uint16_t)(bit << 1)) {
                if (!(bit & R16(ORB_MASK))) continue;
                uint32_t xa = ORB_X + (uint32_t)i * 4u, ya = ORB_Y + (uint32_t)i * 4u;
                int a = Atan8((int16_t)(R32(xa) >> 16), (int16_t)(R32(ya) >> 16),
                              scr16(P32(P_X), CAM_X), scr16(P32(P_Y) - 0x320000, CAM_Y));
                W32(xa, (uint32_t)(RS32(xa) + FixMul(0x40000, Cos8(a))));
                W32(ya, (uint32_t)(RS32(ya) - FixMul(0x40000, Sin8(a))));
                int32_t dx = RS32(xa) - P32(P_X) + RS32(CAM_X);
                if (dx < 0) dx = P32(P_X) - RS32(xa) - RS32(CAM_X);
                if (dx >= 0x100000) return 0;
                int32_t dy = RS32(ya) - P32(P_Y) + RS32(CAM_Y) + 0x280000;
                if (dy < 0) dy = -RS32(ya) + P32(P_Y) - RS32(CAM_Y) - 0x280000;
                if (dy >= 0x200000) return 0;
                snd(0x106);
                W16(ORB_TIMER, 20);
                W16(ORB_LAST, (uint32_t)i);
                W16(ORB_MASK, R16(ORB_MASK) & (uint16_t)~bit);
                W16(ORB_DONE, R16(ORB_DONE) | bit);
                return 0;
            }
            return 0;
        }
        W16(ORB_TIMER, R16(ORB_TIMER) - 1);
        if (R16(ORB_TIMER) == 0) {
            W16(0x060EA426u, 0xFFFF);
            tint(0, 0, 0);
            if (R16(ORB_MASK) != 0) return 0;
            Res_Request(1, 1, 0, 0xFFFFFFFFu, 0x25E40000u, 0x060AC810u);
            Pal_Copy(0x200, 0x100, 0x060D2B2Cu);
            W32(0x060EA5E8u, 0x04000000u);
            W32(0x060EA5ECu, 0x08000000u);
            W32(0x060EA60Cu, 0x25E68000u);
            Layer_FillPage(0x060EA5E8u);
            return 1;
        }
        W16(0x060EA426u, 0xFFFF);
        if (R16(ORB_TIMER) & 1) tint(0, 0, 0);
        else {
            uint32_t k = (uint32_t)R16(ORB_LAST) * 2u;
            tint(R16(0x060D2E4Cu + k), R16(0x060D2E58u + k), R16(0x060D2E64u + k));
        }
        break;
    }
    return 0;
}

/* 0x06064F4E: layer priorities for the statue scene (p = 0) or back (p = 1), fade, next step */
static void nyo_priorities(int p)
{
    if (!fade_idle()) return;
    if (p) { W16(STAGE_C + 0x2A, 4); W16(STAGE_C + 0x2C, 2); W16(STAGE_C + 0x2E, 2); }
    else   { W16(STAGE_C + 0x2A, 0); W16(STAGE_C + 0x2C, 0); W16(STAGE_C + 0x2E, 4); }
    Scl_SetPriority(4, R16(STAGE_C + 0x2A) & 0xFF);
    Scl_SetPriority(8, R16(STAGE_C + 0x2C) & 0xFF);
    Scl_SetPriority(0x10, R16(STAGE_C + 0x2E) & 0xFF);
    Fade_SetTarget(0x1D0, 0x1D0, 0x1D0);
    Fade_SetNextSpeed(0x40000, 1);
    Fade_StartOut();
    nj_next_step();
}

/* 0x06064C78: rotate the 4 x 16 colours 0x2C0-0x2FF of the shadow CRAM by one */
static void nyo_cycle_palette(void)
{
    for (uint32_t c = 0xC0; c < 0xFF; c += 0x10) {
        uint32_t b = SHADOW_CRAM + (0x200u + c) * 2u;
        uint16_t last = R16(b + 30);
        for (uint32_t k = 15; k > 0; k--) W16(b + k * 2u, R16(b + (k - 1) * 2u));
        W16(b, last);
    }
    W16(0x060250A8u, 1);
}

/* 0x06064D40 */
static void nyo_white_out(void)
{
    Fade_SetEndLayerMask(8);
    Fade_SetTarget(0xFF, 0xFF, 0xFF);
    Fade_SetNextSpeed(0x20000, 1);
    Fade_StartOut();
}

/* 0x06064D66: halo palette 0x6F0 (pulsing while p = 0) */
static void nyo_halo(int p)
{
    if (p) { Pal_Copy(0x6F0, 16, 0x060D82FCu); return; }
    int16_t v = (int16_t)R32(0x060F4CA4u);
    if (v > 15) v = (int16_t)(32 - v);
    Pal_SetFaded(0x6F0, 16, 0x060D831Cu, v * 15, v * 15, v * 15);
    W32(0x060F4CA4u, R32(0x060F4CA4u) + 1);
}

/* 0x06064E00: blink the statue palettes (tint `mode`) for `on` frames every `off` frames */
static void nyo_blink(int mode, int16_t off, int16_t on)
{
    if (v16(0x44) != 0) {
        if ((int32_t)R16(NJ_FRAME) > on) {
            W16(NJ_FRAME, 0);
            w16(0x44, 0);
            Pal_Copy(0x680, 0x80, 0x060D81FCu);
            Pal_Copy(0x6F0, 0x10, 0x060D82FCu);
        }
        return;
    }
    if ((int32_t)R16(NJ_FRAME) <= off) return;
    W16(NJ_FRAME, 0);
    w16(0x44, 1);
    switch (mode) {
    case 0:
        Pal_SetFaded(0x680, 0x80, 0x060D81FCu, 0xFF, 0xFF, 0xFF);
        Pal_SetFaded(0x6F0, 0x10, 0x060D82FCu, 0xFF, 0xFF, 0xFF);
        break;
    case 1: Pal_SetFaded(0x6F0, 0x10, 0x060D82FCu, 0xFF, 0xFF, 0x1FF); break;
    case 2: Pal_SetFaded(0x6F0, 0x10, 0x060D82FCu, 0xFF, 0x1FF, 0x1FF); break;
    case 3: Pal_SetFaded(0x6F0, 0x10, 0x060D82FCu, 0xFF, 0x1FF, 0xFF); break;
    case 4: Pal_SetFaded(0x6F0, 0x10, 0x060D82FCu, 0x1FF, 0x1FF, 0xFF); break;
    case 5:
        Pal_SetFaded(0x680, 0x80, 0x060D81FCu, 0xFF, 0xFF, 0xFF);
        Pal_SetFaded(0x6F0, 0x10, 0x060D82FCu, 0xFF, 0xFF, 0xFF);
        break;
    }
}

/* 0x0606501E: the statue (anim 0) zoomed by 0x060F4C94; p = 0 grows it to 1.0 then next step */
static void nyo_statue(int p)
{
    W16(SPR + 0x10, 0x1A00);
    W32(SPR + 0x1C, R32(0x060F4C94u));
    W32(SPR + 0x20, R32(0x060F4C94u));
    if (p > 5) {
        W16(SPR + 0xE, (uint32_t)(uint16_t)(p - 5));
        if (R16(SPR + 0xE) == 0) W16(SPR + 0xE, 1);
    } else W16(SPR + 0xE, 1);
    W16(SPR + 2, 0xA0); W16(SPR + 4, 0xD8); W32(SPR + 0x18, 0x8C);
    nj_draw(NYO_DEFS, NYO_SCR, 0);
    if (p != 0) return;
    if (smod((int32_t)R16(G_FRAME), 15) == 0) snd(0x22C);
    int32_t s = RS32(0x060F4C94u) + 0x200;
    W32(0x060F4C94u, (uint32_t)s);
    if (s >= 0x10000) {
        W32(0x060F4C94u, 0x10000);
        nj_next_step();
    }
}

/* 0x06065100: the statue's face (anim 1), colour-calc level `cc` */
static void nyo_face(int cc)
{
    W16(SPR + 0x10, 0x200); W16(SPR + 0xE, (uint32_t)cc);
    W16(SPR + 2, 0xA0); W16(SPR + 4, 0xD8); W32(SPR + 0x18, 0x89);
    nj_draw(NYO_DEFS, NYO_SCR, 1);
}

/* 0x06065158: the 14 orbiting spirits (anims 2..8) around the statue.
 * p = 0: they gather (two mirrored arcs); 1..6: two 3D rings (radius +8 / +0x12, angles +0x14..,
 * tilt angles +0x3E/+0x40/+0x42 and +0x90/+0x92/+0x94 driven by the tables 0x060AE4E2..0x060AE53E),
 * 3 widens, 5 shrinks, 6 widens to 0x8C. */
static void nyo_spirits(int p)
{
    if (p == 0) {
        int16_t a = (int16_t)((v16(0) >> 2) + 9);
        for (int i = 0; i < 7 && a <= 64; i++) {
            int32_t cx = FixMul(0x460000, Cos8(a & 0xFF));
            int32_t sy = FixMul(0x460000, Sin8(a & 0xFF));
            W16(SPR + 0x10, 0x200); W16(SPR + 0xE, 1);
            W32(SPR + 0x18, a >= 0 ? 0x8Au : 0x8Fu);
            int16_t dx = (int16_t)((uint32_t)cx >> 16);
            W16(SPR + 2, (uint32_t)(int16_t)(dx + 0xA0));
            W16(SPR + 4, (uint32_t)(int16_t)((int16_t)((uint32_t)sy >> 16) + 120));
            nj_draw(NYO_DEFS, NYO_SCR, i + 2);
            W16(SPR + 2, (uint32_t)(int16_t)(0xA0 - dx));
            nj_draw(NYO_DEFS, NYO_SCR, i + 2);
            W16(NJ_V + 20 + (uint32_t)i * 4u, (uint32_t)(int16_t)(a << 8));
            W16(NJ_V + 22 + (uint32_t)i * 4u, (uint32_t)(int16_t)(((0x80 - a) & 0xFF) << 8));
            a = (int16_t)(a + 18);
        }
        if ((v16(0) >> 2) > -64) { w16(0, v16(0) - 3); return; }
        w16(0x32, 0); w16(0x34, 0); w16(0x36, 0);
        w16(0x38, R16(0x060AE4FCu)); w16(0x3A, R16(0x060AE524u)); w16(0x3C, R16(0x060AE540u));
        w16(0x3E, 0); w16(0x40, 0); w16(0x42, 0);
        w16(0x46, 0x300);
        nj_next_step();
        return;
    }
    /* the statue's aura (anim 9, colour bank 0x6F) */
    W16(SPR + 0x10, 0x200); W16(SPR + 0xE, 1);
    W16(SPR + 2, 0xA0); W16(SPR + 4, 0x78); W32(SPR + 0x18, 0x8F);
    Anim_Start(NJ_ANIM, NYO_DEFS, NYO_SCR, 0x480, 9);
    W16(SPR + 0xA, 0x6F);
    W16(SPR + 0xC, 1);
    Anim_Tick(NJ_ANIM);
    /* tilt angles: accumulators += table deltas (halved for p 2..3) */
    static const struct { uint32_t idx, acc, tbl; } T[6] = {
        { 0x84, 0x90, 0x060AE4E2u }, { 0x86, 0x92, 0x060AE512u }, { 0x88, 0x94, 0x060AE532u },
        { 0x32, 0x3E, 0x060AE4FAu }, { 0x34, 0x40, 0x060AE522u }, { 0x36, 0x42, 0x060AE53Eu } };
    if (p >= 2) {
        for (int k = 0; k < 6; k++) {
            int16_t dlt = RS16(T[k].tbl + (uint32_t)R16(NJ_V + T[k].idx) * 4u);
            if (p < 4) dlt = (int16_t)(dlt >> 1);
            w16(T[k].acc, v16(T[k].acc) + dlt);
        }
        /* timers: next table entry when they run out */
        static const struct { uint32_t tmr, idx, tbl; uint16_t max; } C[5] = {
            { 0x8A, 0x84, 0x060AE4E2u, 5 }, { 0x38, 0x32, 0x060AE4FAu, 5 }, { 0x8C, 0x86, 0x060AE512u, 3 },
            { 0x3A, 0x34, 0x060AE522u, 3 }, { 0x8E, 0x88, 0x060AE532u, 2 } };
        for (int k = 0; k < 5; k++) {
            w16(C[k].tmr, v16(C[k].tmr) - 1);
            if (v16(C[k].tmr) != 0) continue;
            uint16_t ix = R16(NJ_V + C[k].idx);
            w16(C[k].idx, ix >= C[k].max ? 0 : ix + 1);
            w16(C[k].tmr, R16(C[k].tbl + (uint32_t)R16(NJ_V + C[k].idx) * 4u + 2u));
        }
        w16(0x3C, v16(0x3C) - 1);
        if (v16(0x3C) == 0) {
            w16(0x36, 0);
            w16(0x3C, R16(0x060AE53Eu + (uint32_t)R16(NJ_V + 0x36) * 4u + 2u));
        }
    }
    for (int i = 0; i < 14; i++) {
        uint32_t ap = NJ_V + 20 + (uint32_t)i * 2u;
        int ang = (int16_t)(RS16(ap) >> 8);
        int32_t rad, cx, sy, cR, sR, A, B, z;
        W16(SPR + 0x10, 0x1A00);
        if (i & 2) {                                          /* ring B */
            rad = (int32_t)((uint32_t)(int32_t)v16(0x12) << 16);
            cx = FixMul(rad, Cos8(ang)); sy = FixMul(rad, Sin8(ang));
            cR = Cos8(v16(0x94)); sR = Sin8(v16(0x94));
            A = FixMul(cx, Cos8(v16(0x92))); B = FixMul(sy, Cos8(v16(0x90)));
            z = FixMul(cx, Sin8(v16(0x92))) + FixMul(sy, Sin8(v16(0x90)));
        } else {                                              /* ring A */
            rad = (int32_t)((uint32_t)(int32_t)v16(0x08) << 16);
            cx = FixMul(rad, Cos8(ang)); sy = FixMul(rad, Sin8(ang));
            cR = Cos8(v16(0x42)); sR = Sin8(v16(0x42));
            A = FixMul(cx, Cos8(v16(0x40))); B = FixMul(sy, Cos8(v16(0x3E)));
            z = FixMul(cx, Sin8(v16(0x40))) + FixMul(sy, Sin8(v16(0x3E)));
        }
        int32_t s = (z >> 6) + 0x10000;                       /* depth -> zoom */
        if (p != 5 && s < 0) s = 1;
        W32(SPR + 0x1C, (uint32_t)s);
        W32(SPR + 0x20, (uint32_t)s);
        int32_t k = (z >> 7) + 0x10000;                       /* perspective */
        W16(SPR + 2, (uint32_t)(int16_t)((int16_t)((uint32_t)FixMul(FixMul(A, cR) + FixMul(B, sR), k) >> 16) + 0xA0));
        W16(SPR + 4, (uint32_t)(int16_t)((int16_t)((uint32_t)FixMul(FixMul(A, sR) + FixMul(B, cR), k) >> 16) + 120));
        if (p == 1) W32(SPR + 0x18, (((RS16(ap) >> 8) & 0xFF) > 0x80) ? 0x90u : 0x8Bu);
        else W32(SPR + 0x18, z >= 0 ? 0x90u : 0xFFFFFFFDu);
        W16(SPR + 0xE, 1);
        nj_draw(NYO_DEFS, NYO_SCR, (i >> 1) + 2);
        if (p >= 2) W16(ap, (uint32_t)(RS16(ap) + RS16(NJ_V + 0x46)));
    }
    if (p == 3) { w16(0x12, v16(0x12) + 2); w16(0x08, v16(0x08) + 2); }
    else if (p == 5) {
        w16(0x08, v16(0x08) - 2); if (v16(0x08) < 0) w16(0x08, 0);
        w16(0x12, v16(0x12) - 2); if (v16(0x12) < 0) w16(0x12, 0);
    } else if (p == 6) {
        w16(0x12, v16(0x12) + 1); if (v16(0x12) > 0x8C) w16(0x12, 0x8C);
        w16(0x08, v16(0x08) + 1); if (v16(0x08) > 0x8C) w16(0x08, 0x8C);
    }
}

/* the NBG2 line-scroll wave of steps 0xE..0x16 */
static void nyo_wave(int32_t len, int32_t amp)
{
    OR16(LINE_FLAGS, 0xA000);
    LineList_Set(2, 0, 0x4000, 0xE0, 0x04400000);
    LineList_Set(2, 1, 0x8000, 0, 0);
    int ph = (int16_t)R16(NJ_PHASE);
    W16(NJ_PHASE, R16(NJ_PHASE) + 1);
    LineWave_Apply(2, (uint16_t)ph, 0, 0xE0, len, amp, 0);
    LayerAnim_Update();
}

static void nyo_rumble(int32_t m)
{
    if (smod((int32_t)R16(NJ_TIMER), m) == 0) snd(0x22E);
}

/* 0x060659AA */
static void cb_nyorai(void)
{
    switch (R16(NJ_STEP)) {
    case 8:
        Fade_SetNextSpeed(0x50000, 1); Fade_SetEndLayerMask(0x48); Fade_StartIn();
        W32(0x060F4CA4u, 0);
        break;
    case 11:
        Fade_SetNextSpeed(0x40000, 1); Fade_SetEndLayerMask(8); Fade_StartIn();
        break;
    case 12: snd(0x22E); snd(0x22F); snd(0x231); break;
    case 14:
        snd(0x22E); snd(0x22F);
        fade_cur(0x1FF0000, 0xFF0000, 0x1FF0000);
        Fade_SetNextSpeed(0x27000, 1); Fade_SetEndLayerMask(8); Fade_StartIn();
        OR16(LINE_FLAGS, 0x2000);
        W16(LANIM + 0xC, 0x4000);
        W16(NJ_FRAME, 0); w16(0x44, 0);
        break;
    case 15:
        snd(0x22E); snd(0x22F);
        fade_cur(0xFF0000, 0xFF0000, 0x1FF0000);
        Fade_SetNextSpeed(0x27000, 1); Fade_SetEndLayerMask(8); Fade_StartIn();
        W16(NJ_FRAME, 0);
        break;
    case 16:
        snd(0x22E); snd(0x22F);
        fade_cur(0xFF0000, 0x1FF0000, 0x1FF0000);
        Fade_SetNextSpeed(0x27000, 1); Fade_SetEndLayerMask(8); Fade_StartIn();
        W16(NJ_FRAME, 0);
        break;
    case 17:
        snd(0x22E); snd(0x22F);
        fade_cur(0xFF0000, 0x1FF0000, 0xFF0000);
        Fade_SetNextSpeed(0x27000, 1); Fade_SetEndLayerMask(8); Fade_StartIn();
        W16(NJ_FRAME, 0);
        break;
    case 18:
        if (seq_playing(0x231)) seq_volume(-1, 0, 0x60);
        snd(0x22E); snd(0x22F);
        fade_cur(0x1FF0000, 0x1FF0000, 0xFF0000);
        Fade_SetNextSpeed(0x29000, 1); Fade_SetEndLayerMask(8); Fade_StartIn();
        W16(NJ_FRAME, 0);
        break;
    case 19:
        if (seq_playing(0x231)) seq_stop_id(0x231);
        for (int s = 1; s < 8; s++) seq_volume(s, 0x7F, 0);
        fade_cur(0xFF0000, 0xFF0000, 0xFF0000);
        Fade_SetNextSpeed(0x40000, 1); Fade_SetEndLayerMask(8); Fade_StartIn();
        W16(NJ_FRAME, 0);
        break;
    case 20:
        snd(0x22E); snd(0x22F); snd(0x232);
        W32(0x060F4CC0u, 0x400000); W32(0x060F4CC4u, 0x200000);
        W16(NJ_FRAME, 0);
        break;
    case 21:
        snd(0x22E); snd(0x22F);
        Fade_SetEndLayerMask(8);
        W32(0x060F4C9Cu, 0); W32(0x060F4CA0u, 0);
        W16(NJ_FRAME, 0);
        break;
    default: break;
    }
}

/* 0x0606429C. Steps (table 0x060AE498): orbs absorbed -> statue scene (layer priorities, palette
 * cycling 0x2C0-0x2FF), the statue grows, its face fades in, spirits gather and orbit, the
 * NBG2 waves grow with the rumble (sounds 0x22C-0x234), colour flashes, then everything
 * winds down and fades back. */
static int nj_state_nyorai(void)
{
    Fade_HoldFlash();
    AND16(CAM_LOCKS, 0xFF7F);
    switch (R16(NJ_STEP)) {
    case 0:
        if (nyo_orbs()) {
            seq_stop_id(RS16(0x060F4D32u));                  /* 0x060084BE(0): slot 0 = stage tune */
            nyo_priorities(0);
        }
        break;
    case 2: nyo_cycle_palette(); nyo_statue(0); break;
    case 3: nyo_cycle_palette(); nyo_statue(1); break;
    case 4: nyo_cycle_palette(); nyo_statue(2); nyo_face(3); break;
    case 5: nyo_cycle_palette(); nyo_statue(3); nyo_face(2); break;
    case 6: nyo_cycle_palette(); nyo_statue(4); nyo_face(1); break;
    case 7: nyo_cycle_palette(); nyo_statue(5); nyo_face(0); nyo_spirits(0); break;
    case 8: nyo_cycle_palette(); nyo_statue(5); nyo_face(0); nyo_halo(0); nyo_spirits(1); break;
    case 9:
        nyo_cycle_palette(); nyo_statue(5); nyo_face(0); nyo_halo(0); nyo_spirits(1);
        nyo_white_out();
        break;
    case 10:
        nyo_cycle_palette(); nyo_statue(5); nyo_face(0); nyo_halo(1); nyo_spirits(1);
        if (fade_idle()) nj_next_step();
        break;
    case 11:
        nyo_cycle_palette();
        if (fade_idle()) nj_next_step();
        nyo_spirits(1);
        break;
    case 12: nyo_cycle_palette(); nyo_spirits(2); break;
    case 13: nyo_rumble(40); nyo_cycle_palette(); nyo_spirits(3); break;
    case 14: case 15: case 16: case 17: case 18: case 19: {
        static const int32_t len[6] = { 0x80000, 0x120000, 0x180000, 0x200000, 0x240000, 0x280000 };
        static const int32_t amp[6] = { 0x100000, 0x1E0000, 0x200000, 0x200000, 0x240000, 0x280000 };
        static const int16_t mod[4] = { 40, 35, 30, 25 };
        int k = R16(NJ_STEP) - 14;
        if (k < 4) nyo_rumble(mod[k]);
        else if (k == 5 && !seq_playing(0x230)) snd(0x230);
        nyo_cycle_palette();
        nyo_wave(len[k], amp[k]);
        switch (k) {
        case 0: w16(0x46, v16(0x46) + 4); break;
        case 1: w16(0x46, v16(0x46) + 3); break;
        case 2: case 3: w16(0x46, v16(0x46) + 2); break;
        case 4: w16(0x46, v16(0x46) - 10); break;
        case 5: w16(0x46, 0x300); break;
        }
        nyo_spirits(k < 4 ? 4 : 5);
        if (fade_idle()) nj_next_step();
        break;
    }
    case 20:
        nyo_rumble(20);
        nyo_cycle_palette();
        nyo_wave(RS32(0x060F4CC0u), RS32(0x060F4CC4u));
        W32(0x060F4CC0u, R32(0x060F4CC0u) - 0xA00);
        W32(0x060F4CC4u, R32(0x060F4CC4u) - 0xF00);
        w16(0x46, v16(0x46) + 9);
        nyo_spirits(6);
        nyo_blink(5, 4, 1);
        break;
    case 21:
        nyo_rumble(20);
        if (smod((int32_t)R16(G_FRAME), 40) == 0) snd(0x230);
        nyo_cycle_palette();
        nyo_wave(RS32(0x060F4CC0u), RS32(0x060F4CC4u));
        W32(0x060F4CC0u, R32(0x060F4CC0u) - 0xA00);
        W32(0x060F4CC4u, R32(0x060F4CC4u) - 0x1000);
        w16(0x46, v16(0x46) + 4);
        if (v16(0x46) > 0xA00) w16(0x46, 0xA00);
        W32(0x060F4CA0u, R32(0x060F4CA0u) + 1);
        nyo_spirits(6);
        if (R32(0x060F4C9Cu) == 0xEF0000) {
            fade_cur(0xFF0000, 0xFF0000, 0xFF0000);
            nyo_priorities(1);
            if (seq_playing(0x232)) seq_stop_id(0x232);
        } else {                                             /* flicker through the colour table */
            uint32_t k = umod(R32(0x060F4CA0u), 12);
            uint32_t base = R32(0x060F4C9Cu);
            W32(FADE_CUR, R32(0x060AE544u + k * 12u) | base);
            W32(FADE_CUR + 4, R32(0x060AE544u + (k * 3u + 1u) * 4u) | base);
            W32(FADE_CUR + 8, R32(0x060AE544u + (k * 3u + 2u) * 4u) | base);
            int32_t v = RS32(0x060F4C9Cu) + 0x8001;
            W32(0x060F4C9Cu, (uint32_t)v);
            if (v > 0xEF0000) W32(0x060F4C9Cu, 0xEF0000);
        }
        nyo_blink(5, 4, 2);
        break;
    case 22:
        snd(0x234);
        seq_volume(0, 0x7F, 0);
        nyo_wave(0, 0);
        W16(NJ_STATE, 0);
        nj_fade_back(0x30000);
        return 1;
    default: break;
    }
    return 0;
}

/* ======================================================= frame update */

/* 0x06063554 (game frame, after the stage hooks) */
int Ninjutsu_Update(void)
{
    switch (R16(NJ_REQ)) {
    case 1: case 3: case 4: W16(NJ_REQ, 0); break;
    case 2: W16(NJ_REQ, 0); nj_load_ziryu(); break;
    case 5: W16(NJ_REQ, 0); nj_load_nyorai(); break;
    case 6: W16(NJ_REQ, 0); nj_load_bisyamon(); break;
    default: break;
    }
    if (R16(NJ_STATE) != 0) {
        nj_step_timer();
        W16(NJ_FRAME, R16(NJ_FRAME) + 1);
        W16(NJ_TIMER, R16(NJ_TIMER) + 1);
    }
    switch (R16(NJ_STATE)) {
    case 0: case 1: case 3: case 4: return 1;
    case 2: return nj_state_ziryu();
    case 5: return nj_state_nyorai();
    case 6: return nj_state_bisyamon();
    case 7: return nj_state_burst();
    default: return R16(NJ_STATE);
    }
}

/* Part of 0x06044E18 (Player_InitForStage, 0x06045004-0x06045032): fill the position ring with
 * the spawn point, free the sub-objects, re-load an armed magic's graphics. */
void Ninjutsu_PlayerStageInit(void)
{
    for (uint32_t i = 0; i < 64; i++) {
        W16(0x060EA446u + i * 4u, (uint32_t)P32(P_X) >> 16);
        W16(0x060EA448u + i * 4u, (uint32_t)P32(P_Y) >> 16);
    }
    W16(0x060EA546u, 0);
    PlayerFx_Reset();
    Magic_RestoreGfx();
}
