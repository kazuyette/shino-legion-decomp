/* Screen fade (VDP2 colour offset) of A.BIN 0x06006EE8-0x06007BA8.
 * See fade.h for the hardware model and the RAM map. */
#include "fade.h"
#include "fixmath.h"
#include "ram.h"

#define WEIGHT      0x060250ECu   /* s32 x3 */
#define TARGET      0x060250F8u   /* u16 x3 */
#define CUR         0x06025100u   /* s32 x3 */
#define DEF_SPEED   0x06025110u
#define QUEUED      0x06025114u   /* u8 */
#define END_MASK    0x06025116u
#define FLASH_A     0x06025118u   /* u16 x3 */
#define FLASH_B     0x0602511Eu   /* u16 x3 */
#define FLASH_MASK  0x06025124u
#define FLASH_FLAGS 0x06025126u
#define FLASH_SPEED 0x06025128u
#define RUN_SPEED   0x0602512Cu
#define RUN_COUNT   0x06025130u
#define NEXT_COUNT  0x06025132u
#define NEXT_SPEED  0x06025134u

#define SH_CLOFEN   0x0603B49Au   /* VDP2 shadow (0x0603B46C block) */
#define SH_CLOFSL   0x0603B49Cu
#define SH_COA      0x0603B49Eu   /* COAR, COAG, COAB */
#define SH_COB      0x0603B4A4u   /* COBR, COBG, COBB */
#define SH_DIRTY    0x0603B4ACu

#define NEG    0x01000000          /* 0x100 << 16: first negative 9-bit value */
#define WRAP   0x02000000          /* 0x200 << 16 */

static inline uint8_t R8(uint32_t a) { return *Ram_Ptr(a); }
static inline void W8(uint32_t a, uint32_t v) { *Ram_Ptr(a) = (uint8_t)v; }

/* 0x06009254(1, 0x0501, 0x0603B4AC): set the 1-bit field at bit 26 of the shadow
 * "changed" word (5 bits below the MSB). */
static void shadow_mark(void) { W32(SH_DIRTY, R32(SH_DIRTY) | (1u << 26)); }

/* 0x060092BC: signed division r1 / r0. */
static int32_t sdiv(int32_t a, int32_t b) { return b ? a / b : 0; }
/* 0x060093A8 */
static int32_t iabs(int32_t a) { return a > 0 ? a : -a; }

static int32_t cur(int i)            { return RS32(CUR + 4u * (uint32_t)i); }
static void    set_cur(int i, int32_t v) { W32(CUR + 4u * (uint32_t)i, (uint32_t)v); }
static int32_t weight(int i)         { return RS32(WEIGHT + 4u * (uint32_t)i); }
static int16_t cur_int(int i)        { return (int16_t)((uint32_t)cur(i) >> 16); }

/* ---- setters ---- */

/* 0x06006F20 */
void Fade_SetWeights(int32_t r, int32_t g, int32_t b)
{
    W32(WEIGHT, (uint32_t)r); W32(WEIGHT + 4, (uint32_t)g); W32(WEIGHT + 8, (uint32_t)b);
}

/* 0x06006F48 */
void Fade_SetTarget(uint16_t r, uint16_t g, uint16_t b)
{
    W16(TARGET, r); W16(TARGET + 2, g); W16(TARGET + 4, b);
}

/* 0x06007234 */
void Fade_SetDefaultSpeed(int32_t speed) { W32(DEF_SPEED, (uint32_t)speed); }

/* 0x060077A4: CLOFEN bit n = 1 unless bit n of `mask` is set (mask = layers WITHOUT
 * colour offset). Each bit is written separately, each time flagging the shadow. */
void Fade_SetLayerMask(uint16_t mask)
{
    for (int n = 0; n < 7; n++) {
        shadow_mark();
        uint16_t on = (mask & (1u << n)) ? 0 : 1;
        W16(SH_CLOFEN, (R16(SH_CLOFEN) & (uint16_t)~(1u << n)) | (uint16_t)(on << n));
    }
}

/* 0x0600725C: layer mask applied at the end of the next fade; applied now when idle.
 * (Was logged as "sound request 0" in objhandlers_c.c: it is not sound.) */
void Fade_SetEndLayerMask(uint16_t mask)
{
    W16(END_MASK, mask);
    if (R16(FADE_STATE) == 0) Fade_SetLayerMask(R16(END_MASK));
}

/* 0x0600727A: speed + number of fades for the next timed fades (refused while a timed
 * fade is running). */
int Fade_SetNextSpeed(int32_t speed, uint16_t count)
{
    if (R16(RUN_COUNT) != 0) return 0;
    W16(NEXT_COUNT, count);
    W32(NEXT_SPEED, (uint32_t)speed);
    return 1;
}

/* 0x060078EC: current offsets -> COAR/COAG/COAB shadow (integer part, 9 bits). */
void Fade_WriteOffsets(void)
{
    for (int i = 0; i < 3; i++) {
        shadow_mark();
        W16(SH_COA + 2u * (uint32_t)i, (uint16_t)cur_int(i) & 0x1FF);
    }
}

/* 0x0600766C: offsets on for every layer, all layers on offset A, current offset read
 * back from the shadow, target black, weights 1.0. */
void Fade_Init(void)
{
    Fade_SetLayerMask(0);
    for (int n = 0; n < 7; n++) {
        shadow_mark();
        W16(SH_CLOFSL, R16(SH_CLOFSL) & (uint16_t)~(1u << n));
    }
    for (int i = 0; i < 3; i++)
        set_cur(i, (int32_t)((uint32_t)(R16(SH_COA + 2u * (uint32_t)i) & 0x1FF) << 16));
    Fade_SetTarget(0x100, 0x100, 0x100);
    Fade_SetWeights(0x10000, 0x10000, 0x10000);
}

/* 0x06006F56 (named Obj_InitLinkedLists in object.c/docs: wrong, it is this). */
void Fade_Reset(void)
{
    Fade_Init();
    W16(FADE_STATE, 0);
    W32(DEF_SPEED, 0x000A0000);
    W8(QUEUED, 0);
    W32(RUN_SPEED, 0x000A0000);
    W16(END_MASK, 0);
    W16(RUN_COUNT, 0);
    W16(NEXT_COUNT, 0);
    W32(NEXT_SPEED, 0x000A0000);
    W16(FLASH_FLAGS, 0);
}

/* ---- start / request ---- */

/* shared tail of 0x06006FB8 / 0x06006FDE: take the pending timed speed */
static int start(uint16_t state)
{
    W16(FADE_STATE, state);
    uint16_t c = R16(RUN_COUNT);
    if (c == 0) {
        W16(RUN_COUNT, R16(NEXT_COUNT));
        W32(RUN_SPEED, R32(NEXT_SPEED));
        W16(NEXT_COUNT, 0);
    }
    return c;
}
/* 0x06006FB8: fade in (offsets back to 0). */
int Fade_StartIn(void) { return start(1); }
/* 0x06006FDE: fade out (offsets to the target). */
int Fade_StartOut(void) { return start(2); }

/* 0x06007018: start now if idle, else queue (one slot). 0 if the queue is full. */
int Fade_RequestIn(void)
{
    if (R16(FADE_STATE) == 0) { Fade_StartIn(); return 1; }
    if (R8(QUEUED) == 0) { W8(QUEUED, 1); return 1; }
    return 0;
}
/* 0x0600704A */
int Fade_RequestOut(void)
{
    if (R16(FADE_STATE) == 0) { Fade_StartOut(); return 1; }
    if (R8(QUEUED) == 0) { W8(QUEUED, 2); return 1; }
    return 0;
}

/* 0x060072AC: speed so that the next fade lasts `frames` frames. mode 1 = fade in
 * (distance = current value as stored, i.e. 0x100 for -256), mode 2 = fade out
 * (signed 9-bit distance current -> target). Distance per channel is divided by
 * its weight; the longest one sets the speed. `count` = number of fades using it.
 * Refused (0) while a timed fade runs or with frames/mode 0. */
int Fade_SetupDuration(uint16_t frames, uint16_t mode, uint16_t count)
{
    if (R16(RUN_COUNT) != 0 || frames == 0 || mode == 0) return 0;
    W16(NEXT_COUNT, count);
    int32_t d[3];
    if (mode == 1) {
        for (int i = 0; i < 3; i++) d[i] = cur(i);
    } else if (mode == 2) {
        for (int i = 0; i < 3; i++) {
            int32_t t = (int32_t)((uint32_t)R16(TARGET + 2u * (uint32_t)i) << 16);
            int32_t c = cur(i);
            if (R16(TARGET + 2u * (uint32_t)i) >= 0x100)
                d[i] = c >= NEG ? iabs(t - c) : WRAP - t + c;
            else
                d[i] = c >= NEG ? (int32_t)((uint32_t)(0x200 - cur_int(i)) << 16) + t : iabs(t - c);
        }
    } else {
        return 1;
    }
    int32_t a = FixDiv(d[0], weight(0)), b = FixDiv(d[1], weight(1));
    int32_t m = a > b ? a : b;
    int32_t c = FixDiv(d[2], weight(2));
    if (!(m > c)) m = c;
    W32(NEXT_SPEED, (uint32_t)sdiv(m, frames));
    return 1;
}

/* ---- steps ---- */

/* 0x06007960: every channel one step closer to 0 (negative ones go up to 0x200 and
 * wrap to 0). Returns 1 when the integer parts are all 0. */
int Fade_StepIn(int32_t speed)
{
    for (int i = 0; i < 3; i++) {
        int32_t c = cur(i);
        if (c >= NEG) {
            c += FixMul(speed, weight(i));
            set_cur(i, c > 0x01FF0000 ? 0 : c);
        } else if (c > 0) {
            c -= FixMul(speed, weight(i));
            set_cur(i, c < 0 ? 0 : c);
        }
    }
    Fade_WriteOffsets();
    return cur_int(0) + cur_int(1) + cur_int(2) == 0;
}

/* 0x06007B04: one channel towards `t` (16.16, 9-bit encoding) by `step`, crossing
 * 0 through the 0x200 wrap when the sign changes. */
static void move_channel(uint32_t p, int32_t t, int32_t step)
{
    int32_t c = RS32(p);
    if (c >= NEG) {
        if (t >= NEG) {
            if (c > t) { c -= step; W32(p, (uint32_t)(c >= t ? c : t)); }
            else if (!(c >= t)) { c += step; W32(p, (uint32_t)(c > t ? t : c)); }
        } else {
            c += step; W32(p, (uint32_t)c);
            if (c > WRAP) { c -= WRAP; W32(p, (uint32_t)(c > t ? t : c)); }
        }
    } else if (t >= NEG) {
        c -= step; W32(p, (uint32_t)c);
        if (c < 0) { c += WRAP; W32(p, (uint32_t)(c >= t ? c : t)); }
    } else if (t > c) {
        c += step; W32(p, (uint32_t)(c > t ? t : c));
    } else if (!(t >= c)) {
        c -= step; W32(p, (uint32_t)(c >= t ? c : t));
    }
}

/* 0x06007A5E: every channel one step closer to `target`; 1 when all integer parts
 * equal the target. */
int Fade_StepToTarget(int32_t speed, const uint16_t target[3])
{
    for (int i = 0; i < 3; i++)
        move_channel(CUR + 4u * (uint32_t)i, (int32_t)((uint32_t)target[i] << 16), FixMul(speed, weight(i)));
    Fade_WriteOffsets();
    for (int i = 0; i < 3; i++)
        if ((int32_t)cur_int(i) != (int32_t)target[i]) return 0;
    return 1;
}

/* ---- flash (colour pulse between two offsets, only while idle) ---- */

/* 0x06006EE8: arms the flash for ONE frame (flag 1 is cleared by Fade_UpdateFlash):
 * the caller re-arms it every frame it wants the pulse. */
void Fade_SetFlash(int32_t speed, uint16_t mask, const uint16_t a[3], const uint16_t b[3])
{
    W32(FLASH_SPEED, (uint32_t)speed);
    for (int i = 0; i < 3; i++) {
        W16(FLASH_A + 2u * (uint32_t)i, a[i]);
        W16(FLASH_B + 2u * (uint32_t)i, b[i]);
    }
    W16(FLASH_FLAGS, R16(FLASH_FLAGS) | 1);
    W16(FLASH_MASK, mask);
}
/* 0x06006F16: skip the next flash frame. */
void Fade_HoldFlash(void) { W16(FLASH_FLAGS, R16(FLASH_FLAGS) | 4); }

static void load3(uint32_t a, uint16_t v[3])
{
    for (int i = 0; i < 3; i++) v[i] = R16(a + 2u * (uint32_t)i);
}

/* 0x0600718A */
static void Fade_UpdateFlash(void)
{
    if (!(R16(FLASH_FLAGS) & 1)) return;
    if (R16(FLASH_FLAGS) & 4) { W16(FLASH_FLAGS, R16(FLASH_FLAGS) & 0xFFFB); return; }
    W16(FLASH_FLAGS, R16(FLASH_FLAGS) & 0xFFFE);
    if (!(R16(FADE_STATE) == 0 && R8(QUEUED) == 0)) return;
    Fade_SetLayerMask(R16(FLASH_MASK));
    uint16_t col[3];
    if (R16(FLASH_FLAGS) & 2) {
        load3(FLASH_A, col);
        if (Fade_StepToTarget(RS32(FLASH_SPEED), col)) W16(FLASH_FLAGS, R16(FLASH_FLAGS) & 0xFFFD);
    } else {
        load3(FLASH_B, col);
        if (Fade_StepToTarget(RS32(FLASH_SPEED), col)) W16(FLASH_FLAGS, R16(FLASH_FLAGS) | 2);
    }
}

/* ---- sequencer ---- */

static void finish(void)
{
    if (R16(RUN_COUNT) != 0) W16(RUN_COUNT, R16(RUN_COUNT) - 1);
    W16(FADE_STATE, R8(QUEUED));
    W8(QUEUED, 0);
    Fade_SetLayerMask(R16(END_MASK));
}

/* 0x06007084: called once per frame by the A.BIN main loop 0x06004038 (after the game
 * task and the SGL slot work, before the sound scheduler 0x0600863E). */
void Fade_Update(void)
{
    Fade_UpdateFlash();
    switch (R16(FADE_STATE)) {
    case 0:                                                 /* idle: take a pending timed speed */
        if (R16(NEXT_COUNT) != 0 && R16(RUN_COUNT) == 0) {
            W16(RUN_COUNT, R16(NEXT_COUNT));
            W32(RUN_SPEED, R32(NEXT_SPEED));
            W16(NEXT_COUNT, 0);
        }
        break;
    case 1:
        if (Fade_StepIn(RS32(R16(RUN_COUNT) ? RUN_SPEED : DEF_SPEED))) finish();
        break;
    case 2: {
        uint16_t t[3];
        load3(TARGET, t);
        if (Fade_StepToTarget(RS32(R16(RUN_COUNT) ? RUN_SPEED : DEF_SPEED), t)) finish();
        break;
    }
    default: break;
    }
    Fade_WriteOffsets();
}

/* 0x060075CA: jump to the end of the running fade at once (no end layer mask). */
void Fade_Complete(void)
{
    switch (R16(FADE_STATE)) {
    case 1:
        for (int i = 0; i < 3; i++) set_cur(i, 0);
        break;
    case 2:
        for (int i = 0; i < 3; i++) set_cur(i, (int32_t)((uint32_t)R16(TARGET + 2u * (uint32_t)i) << 16));
        break;
    default: return;
    }
    Fade_WriteOffsets();
    if (R16(RUN_COUNT) != 0) W16(RUN_COUNT, R16(RUN_COUNT) - 1);
    W16(FADE_STATE, R8(QUEUED));
    W8(QUEUED, 0);
}

/* ---- inline sequences of SHINOBI ---- */

/* Game_LoadStage 0x0606B7C6-0x0606B838 */
void Fade_StageIntro(void)
{
    Fade_Reset();                                           /* 0x06006F56 */
    for (int i = 0; i < 3; i++) set_cur(i, NEG);            /* -256: black */
    Fade_WriteOffsets();                                    /* inlined copy of 0x060078EC */
    Fade_SetupDuration(16, 1, 1);                           /* 0x060072AC(16, 1, 1) */
    Fade_StartIn();                                         /* 0x06006FB8 */
}

/* Stage_UpdateState 0x060461DA-0x060461FA (only when FADE_STATE == 0) */
void Fade_StageOutro(void)
{
    Fade_SetEndLayerMask(0);                                /* 0x0600725C(0) */
    Fade_SetTarget(0x100, 0x100, 0x100);                    /* 0x06006F48: black */
    Fade_SetupDuration(16, 2, 1);                           /* 0x060072AC(16, 2, 1) */
    Fade_StartOut();                                        /* 0x06006FDE */
}

/* ---- PC renderer accessors ---- */

static int s9(uint16_t v) { v &= 0x1FF; return v >= 0x100 ? (int)v - 0x200 : (int)v; }

void Fade_GetColorOffset(int *r, int *g, int *b, int *enabledMask)
{
    if (r) *r = s9(R16(SH_COA));
    if (g) *g = s9(R16(SH_COA + 2));
    if (b) *b = s9(R16(SH_COA + 4));
    if (enabledMask) *enabledMask = R16(SH_CLOFEN) & 0x7F;
}

int Fade_GetLayerOffset(int layer, int *r, int *g, int *b)
{
    int on = layer >= 0 && layer < 7 && (R16(SH_CLOFEN) >> layer & 1);
    uint32_t base = (on && (R16(SH_CLOFSL) >> layer & 1)) ? SH_COB : SH_COA;
    if (r) *r = on ? s9(R16(base)) : 0;
    if (g) *g = on ? s9(R16(base + 2)) : 0;
    if (b) *b = on ? s9(R16(base + 4)) : 0;
    return on;
}
