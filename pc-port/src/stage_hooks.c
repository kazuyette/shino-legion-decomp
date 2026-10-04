/* Per-stage frame hooks (SHINOBI 0x0606BCD0, tables 0x060B10CC / 0x060B1114), the layer
 * animation system 0x0606F6AC / 0x0606EF9C on the block 0x060F4D80, the HBlank line table of
 * A.BIN (0x0601ABD8) and the entry of the ninjutsu effects 0x06063554. See stage_hooks.h.
 *
 * Per-stage handlers (index = 0x0602526A, game stage in brackets):
 *   0 [1-1] 0x0606E434  drop-in end, torch palette flicker, NBG2 line bands (clouds drift),
 *                       auto-run at the exit, left bound follows the checkpoint
 *   1 [1-2] 0x0606E590  NBG1 line list 1 entry 1 scrolls
 *   2 [2-1] 0x0606C7AE  falling debris effects near the top, music change, SAKYOU.PBC, mid-boss
 *   4 [3-1] 0x0606CA70  palette cycling (two banks), flying NBG2 platform ride, bottom bound
 *   5 [3-2] 0x0606D7F2  NBG2 shown after 63 frames (colour calc), look-down lock
 *   6 [4-1] 0x0606D8C4  exit, look-down zones
 *   7 [4-2] 0x0606D99E  NBG1 wave when the boss arrives / leaves, dropped items slide with the
 *                       ledges, boss life bar (Hud_DrawBossBar)
 *   8 [5-1] 0x0606DC68  bounds, camera on object 0x103D, opening jump-in
 *   9 [5-2] 0x0606DDBC  rising sequence: objects 0x1043, cell scroll of NBG1
 *  10 [6-1] 0x0606E2F4  camera ranges, bounds
 *  11 [6-2] 0x0606E3A0  NBG1 line bands
 *  12 [7-1] 0x0606C676  NBG2 line bands (clouds), bounds
 *  13 [7-2] 0x0606C760  colour offset gradient by line
 *  14 [8-1] 0x0606E5C0  NBG1 line bands, raising water colour gradient
 *  15 [8-2] 0x0606E8B4  nothing
 *  16 [9-1] 0x0606E8CC  blinking colour 0xBE, camera range
 *  17 [9-2] 0x0606E9DA  palette pulse / rotation, lightning objects 0x104D + screen shake,
 *                       six orbs 0x0605EF3C, NBG2 wave
 * Per-stage inits (0x060B1114): 0 0x0606C1F8, 1 0x0606C2D0, 4 0x0606BDC0, 7 0x0606BF12 (4-2:
 * sprite colour banks 0-4 = the ledge/beam/lantern palettes 0x060DB808..), 8 0x0606BF98,
 * 9 0x0606BFB4, 10 0x0606C088, 11 0x0606C134, 12 0x0606BD24, 14 0x0606C360, 15 0x0606C448,
 * 16 0x0606C44E, 17 0x0606C4F8. */
#include "stage_hooks.h"
#include "ram.h"
#include "fixmath.h"
#include "stage.h"
#include "stage_inits.h"
#include "camera.h"
#include "sprite.h"
#include "objects.h"
#include "fx.h"
#include "hud.h"
#include "anim.h"
#include "boot.h"
#include "resource.h"
#include "player_ext.h"
#include "objhandlers_d.h"
#include <stdio.h>

#define FROZEN    (R16(0x0602529Eu) & 0x8000)
#define V38       0x060F4D38u
#define V3A       0x060F4D3Au
#define V3C       0x060F4D3Cu
#define HV        0x060F4D40u      /* 10 longs of per-stage state */
#define PL        0x060EA2B4u
#define PF2       (PL + 0x02)
#define PF4       (PL + 0x04)
#define PX        (PL + 0x08)
#define PY        (PL + 0x0C)
#define PVY       (PL + 0x34)
#define CTRL      0x060EA618u      /* +0 left, +4 top, +8 right, +0xC bottom, +0x20 flags */
#define DESC0     0x060EA588u
#define DESC1     0x060EA5B8u
#define DESC2     0x060EA5E8u
#define VBLANKS   0x060143EAu
#define G_CHECKPOINT_ADDR 0x060EC658u
#define MAPFILE   0x060EA650u
#define LINES0    (LANIM + 0x6Cu)
#define LINES1    (LANIM + 0x86Cu)
#define CELLS0    (LANIM + 0x106Cu)
#define CELLS1    (LANIM + 0x110Cu)
#define LLIST     (LANIM + 0x11ACu)  /* 0x060F5F2C */
#define CLIST     (LANIM + 0x156Cu)  /* 0x060F62EC */

#define ONCE(msg) do { static int s_; if (!s_) { s_ = 1; printf("[stub] %s\n", msg); } } while (0)

/* A.BIN 0x060092BC (r1 / r0), 0x0600A604 (r1 % r0, signed), 0x0600CEF4 (unsigned %) */
static int32_t sdiv(int32_t a, int32_t b) { return b ? a / b : 0; }
static int32_t smod(int32_t a, int32_t b) { return b ? a % b : 0; }
static uint32_t umod(uint32_t a, uint32_t b) { return b ? a % b : 0; }
static int32_t iabs(int32_t v) { return v < 0 ? -v : v; }
static void add32(uint32_t a, int32_t v) { W32(a, (uint32_t)(RS32(a) + v)); }
static void add16(uint32_t a, int v) { W16(a, (uint32_t)(RS16(a) + v)); }
static void clear_hv(void) { for (uint32_t i = 0; i < 10; i++) W32(HV + 4 * i, 0); }

/* 0x0606F8D8: write the whole 32x32-tile page of a layer around its position into VRAM */
void Layer_FillPage(uint32_t d)
{
    for (int32_t y = 0; y < 0x2000000; y += 0x100000)
        for (int32_t x = 0; x < 0x2000000; x += 0x200000) {
            Layer_PutTile(RS32(d) + x, RS32(d + 4) + y, d);
            Layer_PutTile(RS32(d) + x + 0x100000, RS32(d + 4) + y, d);
        }
}

/* 0x0606BB70 */
static void Music_Change(int m)
{
    W16(0x060F4D32u, (uint32_t)m);
    sub_06007E00();
    sub_06007CDE(RS16(0x060F4D32u));
}

/* 0x0604E12C: debris effect (kind bit 3 -> type 9, else 8; kind & 7 = variant) */
static void Debris_Spawn(int32_t x, int32_t y, int kind)
{
    Obj_Spawn((kind & 8) ? 9 : 8, x, y, kind & 7, 0);
}

static uint32_t obj(int i) { return OBJ_TABLE + (uint32_t)i * OBJ_SIZE; }

/* ================= layer animation (0x0606F408 .. 0x0606FE38) ================= */

void LayerAnim_Reset(void)
{
    static const uint8_t w[] = { 0x00, 0x02, 0x04, 0x06, 0x08, 0x0A, 0x0C, 0x0E, 0x10, 0x12, 0x14, 0x16,
                                 0x18, 0x1A, 0x1C, 0x1E, 0x20, 0x22, 0x54, 0x56, 0x5A, 0x5C };
    static const uint8_t l[] = { 0x24, 0x28, 0x30, 0x34, 0x3C, 0x40, 0x48, 0x4C, 0x60, 0x64, 0x68 };
    W16(0x0603A60Au, 0);                                  /* SCRCTL shadow */
    for (unsigned i = 0; i < sizeof w; i++) W16(LANIM + w[i], 0);
    for (unsigned i = 0; i < sizeof l; i++) W32(LANIM + l[i], 0);
    for (uint32_t i = 0; i < 0x100; i++) {
        W32(LINES0 + i * 8, 0); W32(LINES0 + i * 8 + 4, 0);
        W32(LINES1 + i * 8, 0); W32(LINES1 + i * 8 + 4, 0);
    }
    for (uint32_t i = 0; i < 0x28; i++)
        for (uint32_t k = 0; k < 5; k++) W32(0x060F656Cu + k * 0xA0u + i * 4, 0);
    for (uint32_t i = 0; i < 0x78; i++) { W16(LLIST + i * 8, 0); W16(LLIST + i * 8 + 2, 0); W32(LLIST + i * 8 + 4, 0); }
    for (uint32_t i = 0; i < 0x28; i++) { W32(CELLS0 + i * 4, 0); W32(CELLS1 + i * 4, 0); }
    OR16(LINE_FLAGS, 0x4000);
}

void LineList_Set(int list, int idx, uint16_t type, int16_t lines, int32_t value)
{
    uint32_t e = LLIST + (uint32_t)((list & 0xFFFF) * 0x28 + (idx & 0xFFFF)) * 8u;
    W16(e, type); W16(e + 2, (uint16_t)lines); W32(e + 4, (uint32_t)value);
}

void CellList_Set(int list, int idx, uint16_t type, int16_t cols, int32_t value)
{
    uint32_t e = CLIST + (uint32_t)((list & 0xFFFF) * 0x28 + (idx & 0xFFFF)) * 8u;
    W16(e, type); W16(e + 2, (uint16_t)cols); W32(e + 4, (uint32_t)value);
}

void LayerAnim_SetWave(int sel, int mode, int period, int steps, int32_t a5, int32_t a6)
{
    for (int n = 0; n < 2; n++) {
        if (!(sel & (1 << n))) continue;
        uint32_t o = (uint32_t)n * 4u, h = (uint32_t)n * 2u;
        W32(LANIM + 0x48 + o, (uint32_t)sdiv(a5, (int16_t)steps));
        W16(LANIM + 0x5A + h, (uint16_t)steps);
        W32(LANIM + 0x24 + o, (uint32_t)a6);
        W16(LANIM + 0x16 + h, (uint16_t)period);
        W16(LANIM + 0x00 + h, (uint16_t)mode);
        if ((mode & 0xFFFF) == 1) W32(LANIM + 0x30 + o, (uint32_t)a5);
        if ((mode & 0xFFFF) == 4) W32(LANIM + 0x48 + o, (uint32_t)-sdiv(RS32(LANIM + 0x30 + o), (int16_t)steps));
    }
}

/* 0x0606F664 (both selector bits write +6) */
static void LayerAnim_SetParam(int sel, int v)
{
    if (sel & 1) W16(LANIM + 6, (uint32_t)v);
    if (sel & 2) W16(LANIM + 6, (uint32_t)v);
}

/* 0x0606F67E */
static void LayerAnim_Arm(int sel)
{
    if (sel & 1) W16(LANIM + 0, 16);
    if (sel & 2) W16(LANIM + 2, 16);
    if (sel & 4) W16(LANIM + 4, 16);
}

void LineWave_Apply(int layer, int phase, int start, int lines, int32_t len, int32_t amp, int flags)
{
    uint32_t d = DESC0 + (uint32_t)(((layer & 0xFF) * 0x30) & 0xFF);
    int16_t top = (int16_t)((uint32_t)(R32(d + 4) - R32(d + 0x14)) >> 16);
    int16_t end = (int16_t)((int16_t)((int16_t)start + (int16_t)lines) - top);
    int16_t s = (int16_t)((int16_t)start - top);
    int ly = layer & 0xFFFF;
    uint32_t tbl = 0;
    if (s < 0) s = 0;
    if (end <= 0) return;
    int16_t step = (int16_t)sdiv(0x100, (int16_t)((uint32_t)len >> 16));
    if (ly == 0) { tbl = LINES0; W16(LANIM + 8, 1); }
    else if (ly == 1) { tbl = LINES1; W16(LANIM + 8, 1); }
    int32_t ang = (int16_t)((uint32_t)(uint16_t)phase * (uint32_t)(int32_t)step);
    for (uint32_t ln = (uint16_t)s; (int32_t)ln < end && ln < 0xE0; ln++, ang += step) {
        int32_t v = FixMul((flags & 1) ? Cos8(ang) : Sin8(ang), amp);
        if (ly == 0 || ly == 1) add32(tbl + ln * 8 + 4, v);
        if (ly == 2) add32(LINE_HBL + ln * 16, v);
    }
    if (ly == 0 || ly == 1) OR16(LANIM + 8, 1);
}

/* 0x0606FC1E: line list -> line table (NBG0/NBG1: set X, NBG2: add to the HBlank table) */
static void LineList_Apply(int layer, uint32_t list)
{
    uint32_t d = DESC0 + (uint32_t)(((layer & 0xFF) * 0x30) & 0xFF);
    int16_t skip = (int16_t)((uint32_t)(R32(d + 0xC) - R32(d + 0x14)) >> 16);
    int32_t v = skip;
    int ly = layer & 0xFFFF;
    uint32_t tbl = ly == 0 ? LINES0 : LINES1;
    uint32_t ln = 0;
    for (uint32_t e = list;; e += 8) {
        int16_t type = RS16(e);
        if (type == -0x8000) {
            for (; ln < 0xE0; ln++) {
                if (ly == 0 || ly == 1) W32(tbl + ln * 8 + 4, (uint32_t)v);
                else if (ly == 2) W32(LINE_HBL + ln * 16, (uint32_t)v);
            }
            if (ly == 0 || ly == 1) OR16(LANIM + 8, 1);
            return;
        }
        int16_t n = RS16(e + 2);
        if (skip >= 0) {
            if ((int16_t)(skip - n) >= 0) { skip = (int16_t)(skip - n); continue; }
            n = (int16_t)(n - skip);
            skip = -1;
        }
        if (type == 0x4000) v = RS32(e + 4);
        if (type == 0x2000) v = FixMul(RS32(0x060252A8u) + (int32_t)0xFC000000, RS32(e + 4));
        for (int k = 0; k < n; k++, ln = (ln + 1) & 0xFFFF) {
            if (ln >= 0xE0) continue;
            if (ly == 0 || ly == 1) W32(tbl + ln * 8 + 4, (uint32_t)v);
            else if (ly == 2) add32(LINE_HBL + ln * 16, v);
        }
    }
}

/* 0x0606F92E: cell scroll list -> cell table (the value starts as the layer number: quirk) */
static void CellList_Apply(int layer, uint32_t list)
{
    int32_t v = layer;
    uint32_t col = 0;
    for (uint32_t e = list; RS16(e) != -0x8000; e += 8) {
        int16_t type = RS16(e), n = RS16(e + 2);
        if (type == 0x4000) v = RS32(e + 4);
        if (type == 0x2000) v = FixMul(RS32(0x060252ACu) + (int32_t)0xFC000000, RS32(e + 4));
        for (int k = 0; k < n; k++, col = (col + 1) & 0xFFFF) {
            if (col < 0x20 && (int16_t)layer == 0) add32(CELLS0 + col * 4, v);
            if ((int16_t)layer == 1) add32(CELLS1 + col * 4, v);
        }
    }
    W16(LANIM + 0xA, 1);
}

/* waves of NBG0 (n = 0) / NBG1 (n = 1), part of 0x0606F6AC */
static void wave_step(int n)
{
    uint32_t o = (uint32_t)n * 4u, h = (uint32_t)n * 2u;
    if (RS16(LANIM + 0x1C + h) != 0) { add16(LANIM + 0x1C + h, -1); return; }
    LineWave_Apply(n, RS16(LANIM + 0x0E + h), 0, 0x1000, RS32(LANIM + 0x24 + o), RS32(LANIM + 0x30 + o), 0);
    if (!FROZEN) {
        if (RS16(LANIM + 0x5A + h) < 1) W16(LANIM + h, (R16(LANIM + h) & 0xFFF9) | 1);
        else {
            if (R16(LANIM + h) & 2) { add32(LANIM + 0x24 + o, RS32(LANIM + 0x3C + o)); add32(LANIM + 0x30 + o, RS32(LANIM + 0x48 + o)); }
            if (R16(LANIM + h) & 4) { add32(LANIM + 0x24 + o, RS32(LANIM + 0x3C + o)); add32(LANIM + 0x30 + o, RS32(LANIM + 0x48 + o)); }
            add16(LANIM + 0x5A + h, -1);
        }
    }
    W16(LANIM + 0x1C + h, R16(LANIM + 0x16 + h));
    add16(LANIM + 0x0E + h, 1);
}

void LayerAnim_Update(void)
{
    if (R16(LANIM + 0xC) & 0x0008) wave_step(0);
    if (R16(LANIM + 0xC) & 0x0800) wave_step(1);
    if (R16(LANIM + 0xC) & 0x0001) CellList_Apply(1, CLIST);           /* (layer 1 in both calls) */
    if (R16(LANIM + 0xC) & 0x0100) CellList_Apply(1, CLIST + 0x140);
    if (R16(LANIM + 0xC) & 0x0010) LineList_Apply(0, LLIST);
    if (R16(LANIM + 0xC) & 0x0020) LineList_Apply(1, LLIST + 0x140);
    if ((R16(LINE_FLAGS) & 0x8000) && (R16(LANIM + 0xC) & 0x4000)) LineList_Apply(2, LLIST + 0x280);
}

static uint32_t vram(uint32_t a) { return VDP2_VRAM_BASE | (a & (VDP2_VRAM_SIZE - 1)); }

/* 0x0606F050: line table -> VDP2 VRAM (format from the SCRCTL bits), cleared after the copy */
static void LineTable_Copy(uint16_t f, uint32_t va, uint32_t tbl)
{
    if (!(R16(LANIM + 8) & 1)) return;
    uint32_t dst = vram(va);
    if ((f & 6) == 6) {            /* (|| (f & 0x204) == 0x600: never true, original) */
        for (uint32_t i = 0; i < 0x200; i++) { W32(dst + i * 4, R32(tbl + i * 4)); W32(tbl + i * 4, 0); }
    } else if (!(f & 4) && !(f & 0x400)) {
        if ((f & 2) || (f & 0x200))
            for (uint32_t i = 0; i < 0x100; i++) { W32(dst + i * 4, R32(tbl + i * 8 + 4)); W32(tbl + i * 8 + 4, 0); }
    } else {
        for (uint32_t i = 0; i < 0x100; i++) { W32(dst + i * 4, R32(tbl + i * 8)); W32(tbl + i * 8, 0); }
    }
    W16(LANIM + 8, 0);
}

void LayerAnim_Commit(void)
{
    uint16_t f = R16(LANIM + 0xC);
    W16(0x0603A60Au, f & 0xF7C7u);                             /* SCRCTL */
    W32(0x0603A610u, (R32(LANIM + 0x60) >> 1) & 0x7FFFFu);     /* LSTA0 */
    W32(0x0603A614u, (R32(LANIM + 0x64) >> 1) & 0x7FFFFu);     /* LSTA1 */
    W32(0x0603A60Cu, (R32(LANIM + 0x68) >> 1) & 0x7FFFFu);     /* VCSTA */
    if (f & 0x0018) LineTable_Copy(f, R32(LANIM + 0x60), LINES0);
    if (f & 0x0820) LineTable_Copy(f, R32(LANIM + 0x64), LINES1);
    if (!(f & 0x101) || !(R16(LANIM + 0xA) & 1)) return;
    uint32_t dst = vram(R32(LANIM + 0x68));
    if ((f & 0x101) == 0x101) {
        for (uint32_t i = 0; i < 40; i++) {
            W32(dst + i * 8, R32(CELLS0 + i * 4)); W32(dst + i * 8 + 4, R32(CELLS1 + i * 4));
            W32(CELLS0 + i * 4, 0); W32(CELLS1 + i * 4, 0);
        }
    } else {
        if (f & 1) for (uint32_t i = 0; i < 40; i++) { W32(dst + i * 4, R32(CELLS0 + i * 4)); W32(CELLS0 + i * 4, 0); }
        if (f & 0x100) for (uint32_t i = 0; i < 40; i++) { W32(dst + i * 4, R32(CELLS1 + i * 4)); W32(CELLS1 + i * 4, 0); }
    }
    W16(LANIM + 0xA, 0);
}

/* ================= A.BIN HBlank line table ================= */

static int s_lineDma;

static int16_t fade_word(uint32_t a)        /* 0x0600527C: 9-bit fade value (quirk kept) */
{
    int16_t v = (int16_t)(R32(a) >> 16);
    if (v & 0x100) v = (int16_t)((v & 0xFF) - 255);
    return v;
}

void LineTable_VBlank(void)
{
    uint16_t f = R16(LINE_FLAGS);
    if (!(f & 0x8000)) { s_lineDma = 0; return; }           /* no DMA started this frame */
    int16_t fr = fade_word(0x06025100u), fg = fade_word(0x06025104u), fb = fade_word(0x06025108u);
    for (uint32_t ln = 0; ln < 0x108; ln++) {
        uint32_t e = LINE_HBL + ln * 16, w = LINE_HBL_W + ln * 8;
        int16_t c[3] = { fr, fg, fb };
        W16(w, R32(e) >> 16);
        W32(e, 0);
        for (uint32_t k = 0; k < 3; k++) {
            int16_t v = (int16_t)(c[k] + RS32(e + 4 + 4 * k));
            if (v > 0xFF) v = 0xFF;
            else if (v < -0x100) v = -0x100;
            W16(w + 2 + 2 * k, (uint16_t)v);
            W32(e + 4 + 4 * k, 0);
        }
    }
    s_lineDma = f & 0x6000;
    W16(LINE_FLAGS, f & 0x7FFF);                             /* VBlank-OUT 0x060051C8 */
}

int LineTable_DmaActive(void) { return s_lineDma; }
void LineTable_SetDmaActive(int flags) { s_lineDma = flags & 0x6000; }

/* ================= per-stage inits (0x060B1114) ================= */

static void init_00(void)                                    /* 0x0606C1F8 */
{
    OR16(LINE_FLAGS, 0x2000);
    W16(LANIM + 0xC, 0x4000);
    LineList_Set(2, 0, 0x2000, 0x38, 0x800);
    LineList_Set(2, 1, 0x4000, 0x30, 0);
    LineList_Set(2, 2, 0x2000, 0xC0, 0x4000);
    LineList_Set(2, 3, 0x8000, 0, 0);
    uint32_t x = R32(DESC2), y = R32(DESC2 + 4);
    W32(DESC2 + 0x10, 0x04000000u); W32(DESC2 + 0x14, 0x04000000u);
    W32(DESC2, 0x04000000u); W32(DESC2 + 4, 0x04000000u); W32(DESC2 + 0x24, 0x25E68000u);
    Layer_FillPage(DESC2);
    W32(DESC2, 0x06000000u); W32(DESC2 + 4, 0x04000000u); W32(DESC2 + 0x24, 0x25E69000u);
    Layer_FillPage(DESC2);
    W32(DESC2 + 0x24, 0x25E68000u);
    W32(DESC2 + 0x14, 0x05180000u);
    W32(DESC2, x); W32(DESC2 + 4, y);
}

static void fill_at(uint32_t d, uint32_t x, uint32_t y, uint32_t plate)
{
    W32(d, x); W32(d + 4, y); W32(d + 0x24, plate);
    Layer_FillPage(d);
}

static void init_01(void)                                    /* 0x0606C2D0 */
{
    W16(LANIM + 0xC, 0x220);
    W32(LANIM + 0x64, 0x05E70000u);
    LineList_Set(1, 0, 0x4000, 0x50, 0);
    LineList_Set(1, 1, 0x4000, 0x20, 0);
    LineList_Set(1, 2, 0x4000, 0x90, 0);
    LineList_Set(1, 3, 0x8000, 0, 0);
    LayerAnim_Arm(2);
    W32(DESC1, 0x04000000u); W32(DESC1 + 4, 0x04000000u);
    Layer_FillPage(DESC1);
}

static void init_04(void)                                    /* 0x0606BDC0 */
{
    clear_hv();
    W16(0x060F227Eu, 0);
    for (uint32_t i = 0; i < 3; i++) { W32(0x060F4D68u + i * 8, R32(DESC2 + 0x10)); W32(0x060F4D6Cu + i * 8, R32(DESC2 + 0x14)); }
    uint32_t x = R32(DESC2), y = R32(DESC2 + 4);
    Vdp2_DisplayOff();
    fill_at(DESC2, 0x04000000u, 0x04000000u, 0x25E68000u);
    fill_at(DESC2, 0x06000000u, 0x04000000u, 0x25E69000u);
    fill_at(DESC2, 0x04000000u, 0x06000000u, 0x25E6A000u);
    fill_at(DESC2, 0x06000000u, 0x06000000u, 0x25E6B000u);
    fill_at(DESC2, 0x08000000u, 0x04000000u, 0x25E70000u);
    fill_at(DESC2, 0x0A000000u, 0x04000000u, 0x25E71000u);
    fill_at(DESC2, 0x08000000u, 0x06000000u, 0x25E72000u);
    fill_at(DESC2, 0x0A000000u, 0x06000000u, 0x25E73000u);
    Vdp2_DisplayOn();
    W32(DESC2 + 0x24, 0x25E68000u);
    W32(DESC2, x); W32(DESC2 + 4, y);
}

/* 0x0606BF12 (4-2): the sprite colour banks 0-4 used by the ledges / beams / halo / lanterns
 * (objects 0x1025-0x1028) come from here, not from the stage files. */
static void init_07(void)
{
    clear_hv();
    W32(DESC1, 0x04000000u); W32(DESC1 + 4, 0x04000000u);
    Layer_FillPage(DESC1);
    Pal_Copy(0x00, 0x10, 0x060DB808u);
    Pal_Copy(0x10, 0x10, 0x060DB808u);
    Pal_Copy(0x20, 0x10, 0x060DB828u);
    Pal_Copy(0x30, 0x10, 0x060DB848u);
    Pal_Copy(0x40, 0x10, 0x060DB868u);
}

static void init_08(void) { W16(V38, 0); W16(V3A, 0); W16(V3C, 0); }   /* 0x0606BF98 */

static void init_09(void)                                    /* 0x0606BFB4 */
{
    clear_hv();
    Vdp2_DisplayOff();
    uint32_t x = R32(DESC1), y = R32(DESC1 + 4);
    fill_at(DESC1, 0x04000000u, 0x04000000u, 0x25E64000u);
    fill_at(DESC1, 0x06000000u, 0x04000000u, 0x25E65000u);
    fill_at(DESC1, 0x04000000u, 0x06000000u, 0x25E66000u);
    fill_at(DESC1, 0x06000000u, 0x06000000u, 0x25E67000u);
    W32(DESC1 + 0x24, 0x25E64000u);
    W32(DESC1, x); W32(DESC1 + 4, y);
    Vdp2_DisplayOn();
    OR16(PF4, 0x400);
}

static void init_10(void)                                    /* 0x0606C088 */
{
    W16(LANIM + 0xC, 0x12);
    W32(LANIM + 0x60, 0x05E70000u);
    LineList_Set(0, 0, 0x2000, 0xCE, 0x2000);
    LineList_Set(0, 1, 0x2000, 0xD2, 0x5000);
    LineList_Set(0, 2, 0x8000, 0, 0);
    LayerAnim_Arm(1);
    W32(DESC0, 0x04000000u); W32(DESC0 + 4, 0x04000000u);
    Layer_FillPage(DESC0);
    fill_at(DESC0, 0x06000000u, 0x04000000u, 0x25E61000u);
    W32(DESC0 + 0x24, 0x25E60000u);
}

static void init_11(void)                                    /* 0x0606C134 */
{
    W16(LANIM + 0xC, 0x220);
    W32(LANIM + 0x64, 0x05E70000u);
    LineList_Set(1, 0, 0x4000, 0x20, 0);
    LineList_Set(1, 1, 0x4000, 0x20, 0);
    LineList_Set(1, 2, 0x4000, 0x20, 0);
    LineList_Set(1, 3, 0x4000, 0x1C, 0);
    LineList_Set(1, 4, 0x4000, 0x90, 0);
    LineList_Set(1, 5, 0x8000, 0, 0);
    LayerAnim_Arm(2);
    W32(DESC0, 0x04000000u); W32(DESC0 + 4, 0x04000000u);
    Layer_FillPage(DESC0);
    W32(DESC1, 0x04000000u); W32(DESC1 + 4, 0x04000000u);
    Layer_FillPage(DESC1);
    W16(V38, 0);
}

static void init_12(void)                                    /* 0x0606BD24 */
{
    OR16(LINE_FLAGS, 0x2000);
    W16(LANIM + 0xC, 0x4000);
    LineList_Set(2, 0, 0x4000, 0x30, 0);
    LineList_Set(2, 1, 0x4000, 0x30, 0);
    LineList_Set(2, 2, 0x4000, 0x10, 0);
    LineList_Set(2, 3, 0x2000, 0xA0, 0x1800);
    LineList_Set(2, 4, 0x8000, 0, 0);
    W32(DESC2, 0x04000000u); W32(DESC2 + 4, 0x04000000u);
    Layer_FillPage(DESC2);
}

static void init_14(void)                                    /* 0x0606C360 */
{
    W16(LANIM + 0xC, 0x220);
    W32(LANIM + 0x64, 0x05E70000u);
    LineList_Set(1, 0, 0x4000, 0x20, 0);
    LineList_Set(1, 1, 0x4000, 0x6F, 0);
    LineList_Set(1, 2, 0x4000, 0x1E, 0);
    LineList_Set(1, 3, 0x2000, 0xA0, 0x800);
    LineList_Set(1, 4, 0x8000, 0, 0);
    LayerAnim_Arm(2);
    W32(DESC1, 0x04000000u); W32(DESC1 + 4, 0x04000000u);
    Layer_FillPage(DESC1);
    W16(V38, 0);
    fill_at(DESC1, 0x06000000u, 0x04000000u, 0x25E63000u);   /* (plate left at 0x25E63000) */
    W16(V38, 0); W16(V3C, 0); W16(V3A, 0x200);
}

static void init_15(void) { W16(0x060F22BEu, 0); W16(0x060F22C0u, 0); W16(0x060F22C2u, 0); }  /* 0x0606C448 */

static void init_16(void)                                    /* 0x0606C44E */
{
    W16(LANIM + 0xC, 0x220);
    W32(LANIM + 0x64, 0x05E70000u);
    LineList_Set(1, 0, 0x2000, 0x260, 0x6000);
    LineList_Set(1, 1, 0x2000, 0x22, 0x6000);
    LineList_Set(1, 2, 0x2000, 0x5C, 0x3000);
    LineList_Set(1, 3, 0x2000, 0x30, 0x6000);
    LineList_Set(1, 4, 0x8000, 0, 0);
    LayerAnim_Arm(2);
    W32(DESC1, 0x04000000u); W32(DESC1 + 4, 0x04000000u);
    Layer_FillPage(DESC1);
}

/* 0x0605EE20: the six orbs of 9-2 (state 0x060F22C4.., anim states 0x060F22D4 + 0x20*i) */
static void Orbs_Init(void)
{
    W16(0x060F22C4u, 0); W16(0x060F23C4u, 0); W16(0x060F23C8u, 0); W16(0x060F23CAu, 0);
    for (uint32_t i = 0; i < 6; i++) {
        W16(0x060F22C6u + 2 * i, 0);
        Anim_Start(0x060F22D4u + 0x20 * i, 0x060AC72Au, 0x060D2E1Cu, 0x10, (int)((i & 0x7FFF) << 1));
        W32(0x060F2394u + 4 * i, (i * 0x10 + 0x38) << 16);
        W32(0x060F23ACu + 4 * i, 0x200000u);
    }
    Pal_Copy(0x520, 0x10, 0x060D2D2Cu);
    Pal_Copy(0x530, 0x10, 0x060D2D8Cu);
    Pal_Copy(0x540, 0x10, 0x060D2D4Cu);
    Pal_Copy(0x550, 0x10, 0x060D2DACu);
    Pal_Copy(0x560, 0x10, 0x060D2D6Cu);
    Pal_Copy(0x570, 0x10, 0x060D2DCCu);
}

/* 0x0605EF3C */
static void Orbs_Update(void)
{
    uint16_t bit = 1;
    for (uint32_t i = 0; i < 6; i++, bit = (uint16_t)(bit << 1)) {
        uint32_t t = 0x060F22C6u + 2 * i, st = 0x060F22D4u + 0x20 * i;
        if (!FROZEN) add16(t, 1);
        if (RS16(t) > 12) W16(t, 0);
        if (!(bit & R16(0x060F22C4u))) Anim_Start(st, 0x060AC72Au, 0x060D2E1Cu, 0x10, (int)(i * 2 + 1));
        else {
            if (!FROZEN) {
                int v = RS16(t) - 6;
                if (v < 0) v = 6 - RS16(t);
                v = (int16_t)(v * 14);
                Pal_SetFaded((int)(i + 0x52) * 16, 16, R32(0x060D2E70u + 4 * i), v, v, v);
            }
            Anim_Start(st, 0x060AC72Au, 0x060D2E1Cu, 0x10, (int)((i & 0x7FFF) << 1));
        }
        if (!(bit & R16(0x060F23C8u))) {
            W16(SPR_WORK + 2, (uint32_t)(int16_t)(R32(0x060F2394u + 4 * i) >> 16));
            W16(SPR_WORK + 4, (uint32_t)(int16_t)(R32(0x060F23ACu + 4 * i) >> 16));
            W16(SPR_WORK + 0x10, 0x200);
            W32(SPR_WORK + 0x18, 0x84);
            W16(SPR_WORK + 0x0A, i + 0x52);
            W16(SPR_WORK + 0x0C, 1);
            W16(SPR_WORK + 0x0E, 0);
            Anim_Tick(st);
        }
    }
}

static void init_17(void)                                    /* 0x0606C4F8 */
{
    OR16(LINE_FLAGS, 0x2000);
    W16(LANIM + 0xC, 0x4000);
    LineList_Set(2, 0, 0x4000, 0xE0, RS32(DESC2 + 0x10));
    LineList_Set(2, 1, 0x8000, 0, 0);
    OR16(LINE_FLAGS, 0x8000);
    Orbs_Init();
    Vdp2_DisplayOff();
    uint32_t x = R32(DESC2), y = R32(DESC2 + 4);
    fill_at(DESC2, 0x04000000u, 0x04000000u, 0x25E68000u);
    fill_at(DESC2, 0x06000000u, 0x04000000u, 0x25E69000u);
    fill_at(DESC2, 0x04000000u, 0x06000000u, 0x25E6A000u);
    fill_at(DESC2, 0x06000000u, 0x06000000u, 0x25E6B000u);
    W32(DESC2 + 0x24, 0x25E68000u);
    W32(DESC2, x); W32(DESC2 + 4, y);
    Vdp2_DisplayOn();
    clear_hv();
}

typedef void (*HookFn)(void);
static const HookFn kInits[18] = {                           /* 0x060B1114 */
    init_00, init_01, 0, 0, init_04, 0, 0, init_07, init_08, init_09,
    init_10, init_11, init_12, 0, init_14, init_15, init_16, init_17 };

void StageHooks_Init(void)
{
    W16(V3C, 0);
    W16(V3A, 0);
    W16(V38, R16(V3A));
    LayerAnim_Reset();                                       /* 0x0606F408 */
    int st = RS16(G_STAGE_INDEX);
    if (st >= 0 && st < 18 && kInits[st]) kInits[st]();
    OR16(LINE_FLAGS, 0x4000);
    /* 0x060053AC(0x0601ABD8): HBlank DMA tables (SCU indirect) -> the renderers read the
     * converted line table directly (soft_render.c Vdp2_GetLayerScroll / line colour offsets) */
}

/* ================= per-stage frame handlers (0x060B10CC) ================= */

static void hook_00(void)                                    /* 0x0606E434 (1-1) */
{
    if (R16(0x060252DCu) == 0 && !(R16(PF2) & 0x4000)) {     /* drop-in over */
        W16(PF4, R16(PF4) & 0xFFEF);
        W16(0x060252DCu, 1);
    }
    if (!FROZEN) {
        if ((R16(V38) & 3) == 3) Pal_Copy(0x200, 0x100, 0x060DB888u);
        if ((R16(V38) & 3) == 0) Pal_Copy(0x200, 0x100, 0x060DBA88u);
        int32_t t = RS16(V38);
        LineList_Set(2, 1, 0x4000, 0x30, t * 0x1000 + FixMul(RS32(0x060252A0u) + (int32_t)0xFC000000, 0x1000));
        add16(V38, 1);
        if (RS32(PX) - 0x2B420000 > 0) OR16(PF4, 0x10);
        if (RS16(G_CHECKPOINT_ADDR) == 1 && RS32(PX) > 0x0D000000) W32(CTRL, 0x0BC00000u);
        if (RS16(G_CHECKPOINT_ADDR) == 2 && RS32(PX) > 0x1E000000) W32(CTRL, 0x1CD00000u);
    }
    OR16(LINE_FLAGS, 0x8000);
}

static void hook_01(void)                                    /* 0x0606E590 (1-2) */
{
    if (!FROZEN) add32(LLIST + 0x140 + 8 + 4, 0x2000);       /* 0x060F6078 */
}

static void hook_02(void)                                    /* 0x0606C7AE (2-1) */
{
    static const struct { int off; int32_t dx; int kind; } deb[11] = {
        { 0, 0x1E0000, 8 }, { 100, 0x780000, 9 }, { 200, 0x500000, 10 }, { 300, 0x6E0000, 12 },
        { 400, 0x320000, 13 }, { 500, 0x280000, 14 }, { 600, 0x460000, 9 }, { 700, 0x5A0000, 8 },
        { 800, 0x1E0000, 10 }, { 900, 0xA00000, 12 }, { 1000, 0, 11 } };
    if (FROZEN) return;
    int32_t cx = RS32(0x060252A0u), cy = RS32(0x060252A4u);
    if (cy > 0x06FF0000)
        for (int i = 0; i < 11; i++)
            if ((((uint32_t)(int32_t)RS16(V38) + (uint32_t)deb[i].off) & 0x3FF) == 0x3FF) Debris_Spawn(cx + deb[i].dx, cy, deb[i].kind);
    if (!(R16(V3C) & 4) && RS32(PY) < 0x06200000 && RS32(PX) > 0x052B0000) {
        OR16(V3C, 4);
        Music_Change(5);
        W16(CTRL + 0x10, 0x7FFF);
    }
    if (!(R16(V3C) & 2) && RS32(PY) < 0x05E00000 && RS32(PX) > 0x04B00000) {
        OR16(V3C, 2);
        Res_Request(2, 0, 0, 0xFFFFFFFFu, 0, 0x060B115Cu);   /* "SAKYOU.PBC" */
    }
    if (!(R16(V3C) & 1) && cy < 0x052C0000 && cx > 0x06090000) {
        OR16(V3C, 1);
        W16(V38, 0);
        W16(CTRL + 0x1E, (uint32_t)Obj_Register(0x21, 0x077A0000, 0x05DF0000, 0, 0xFFFF));
        OR16(CTRL + 0x20, 0xB);
    }
    add16(V38, 1);
}

/* 0x0606CA70 (3-1) */
static void pal_step_or_copy(int dst, int n, uint32_t src, int step)
{
    if (step) Pal_StepTowards(dst, n, src, 1);
    else Pal_Copy(dst, n, src);
}

static void hook_04(void)
{
    const uint32_t T1 = 0x060B1168u, T2 = 0x060B117Au, MAP = R32(MAPFILE);
    if (FROZEN) return;
    if (RS32(PX) > 0x17500000 && RS32(PY) < 0x04C00000) OR16(PF4, 0x10);
    /* palette cycle 1: colours 0x100-0x1BF, state 0x060F4D42 / timer 0x060F4D40 */
    uint16_t s1 = R16(HV + 2);
    if (RS16(HV) == 0) {
        switch (s1) {
        case 1: case 3: case 5: pal_step_or_copy(0x100, 0xC0, 0x060DB008u, 0); break;
        case 7: pal_step_or_copy(0x100, 0xC0, 0x060DB208u, 1); break;
        case 8: pal_step_or_copy(0x100, 0xC0, MAP + 0xE00, 1); break;
        case 0: case 2: case 4: case 6: pal_step_or_copy(0x100, 0xC0, MAP + 0xE00, 0); break;
        default: break;
        }
    }
    add16(HV, 1);
    if (s1 == 7) { if (smod(RS16(HV), sdiv(RS16(T1 + 14), 15)) == 0) Pal_StepTowards(0x100, 0xC0, 0x060DB208u, 1); }
    else if (s1 == 8) { if (smod(RS16(HV), sdiv(RS16(T1 + 16), 15)) == 0) Pal_StepTowards(0x100, 0xC0, MAP + 0xE00, 1); }
    if (RS16(HV) >= RS16(T1 + 2u * s1)) { W16(HV, 0); W16(HV + 2, s1 < 8 ? s1 + 1u : 0); }
    W16(0x060F227Eu, 0);
    /* palette cycle 2: colours 0x200-0x2FF, state 0x060F4D46 / timer 0x060F4D44 */
    uint16_t s2 = R16(HV + 6);
    if (RS16(HV + 4) == 0) {
        switch (s2) {
        case 0: case 4: pal_step_or_copy(0x200, 0x100, MAP + 0x1000, 0); break;
        case 1: pal_step_or_copy(0x200, 0x100, 0x060DB608u, 1); break;
        case 2: pal_step_or_copy(0x200, 0x100, 0x060DB608u, 0); break;
        case 3: case 7: pal_step_or_copy(0x200, 0x100, MAP + 0x1000, 1); break;
        case 5: pal_step_or_copy(0x200, 0x100, 0x060DB408u, 1); break;
        case 6: pal_step_or_copy(0x200, 0x100, 0x060DB408u, 0); break;
        default: break;
        }
    }
    add16(HV + 4, 1);
    {
        uint32_t src = 0; int idx = s2;
        if (s2 == 1) { src = 0x060DB608u; idx = 1; }
        else if (s2 == 3 || s2 == 7) src = MAP + 0x1000;
        else if (s2 == 5) src = 0x060DB408u;
        if (src && smod(RS16(HV + 4), sdiv(RS16(T2 + 2u * (uint32_t)idx), 15)) == 0 && Pal_StepTowards(0x200, 0x100, src, 1)) {
            W16(HV + 4, 0);
            W16(HV + 6, R16(HV + 6) < 7 ? R16(HV + 6) + 1u : 0);
        }
    }
    s2 = R16(HV + 6);
    if (RS16(HV + 4) >= RS16(T2 + 2u * s2)) { W16(HV + 4, 0); W16(HV + 6, s2 < 7 ? s2 + 1u : 0); }
    /* flying platform (NBG2 = the platform, its offset follows 0x060F4D68 history) */
    const uint32_t D50 = HV + 0x10, D54 = HV + 0x14, OX = DESC2 + 0x10, OY = DESC2 + 0x14;
    if (RS16(HV + 0x18) == 0) {
        int16_t st = RS16(HV + 0xA);
        if (st != 0) {
            W32(0x060F4D70u, R32(0x060F4D68u)); W32(0x060F4D74u, R32(0x060F4D6Cu));
            add32(0x060F4D68u, -RS32(D50));
            add32(0x060F4D6Cu, -RS32(D54));
            W32(OX, R32(0x060F4D70u)); W32(OY, R32(0x060F4D74u));
        }
        if (st == 0) {
            uint16_t bit = 1;
            for (int k = 0; k < 2; k++, bit <<= 1) {
                if (R16(HV + 8) & bit) continue;
                int32_t tx = RS32(0x060B118Cu + 8u * k), ty = RS32(0x060B1190u + 8u * k);
                int32_t dx = RS32(PX) - tx - 0x04000000;
                if (dx < 0) dx = tx - RS32(PX) + 0x04000000;
                if (dx < 0x500000 && ty - RS32(PY) + 0x04000000 < 0xA00000) {
                    OR16(HV + 8, bit);
                    W16(HV + 0xA, (uint32_t)(k + 1));
                    W16(HV + 0xC, 0); W32(D50, 0); W32(D54, 0); W16(HV + 0xE, 0); W16(HV + 0x1A, 0); W16(HV + 0x1C, 0);
                    OR16(CTRL + 0x20, 0xB);
                    uint32_t p = 0x060B119Cu + 8u * (uint32_t)k;
                    for (uint32_t j = 0; j < 2; j++) { W32(0x060F4D68u + j * 8, R32(p)); W32(0x060F4D6Cu + j * 8, R32(p + 4)); }
                    W32(OX, R32(p)); W32(OY, R32(p + 4));
                    /* 0x06051B60: wake the first object 0x1022 */
                    for (int i = 0; i < OBJ_SLOTS; i++) if (R16(obj(i)) == 0x1022) { OR16(obj(i) + 2, 1); break; }
                    break;
                }
            }
        } else if (st == 1 || st == 2 || st == 3) {
            if (RS16(HV + 0xC) == 0) {
                int32_t a = iabs(RS32(OX) - 0x08280000);
                if (a < iabs(RS32(D50))) {
                    OR16(HV + 0x1A, 1);
                    add32(D50, RS32(D50) < 1 ? 0x2A00 : -0x2A00);
                    W32(OX, 0x08280000u);
                } else if (RS32(OX) < 0x08280000) { add32(D50, -0x1600); if (RS32(D50) < -0x3B000) W32(D50, (uint32_t)-0x3B000); }
                else { add32(D50, 0x1600); if (RS32(D50) > 0x3B000) W32(D50, 0x3B000); }
                a = iabs(RS32(OY) - 0x04E00000);
                if (a < iabs(RS32(D54))) { OR16(HV + 0x1A, 2); W32(OY, 0x04E00000u); }
                else if (RS32(OY) < 0x04E00000) { add32(D54, -0x3000); if (RS32(D54) < -0x50000) W32(D54, (uint32_t)-0x50000); }
                else { add32(D54, 0x3000); if (RS32(D54) > 0x50000) W32(D54, 0x50000); }
                if ((R16(HV + 0x1A) & 3) == 3) { W16(HV + 0xC, 1); W16(HV + 0x1A, 0); W16(HV + 0x1C, RS32(D50) > 0); }
            } else if (RS16(HV + 0xC) == 1) {
                if (iabs(RS32(OX) - 0x08280000) > 0x500000) W16(HV + 0x1C, RS32(OX) > 0x0827FFFF);
                if (R16(HV + 0x1C) == 0) { if (RS32(D50) < -0x48000) W16(HV + 0x1C, 1); add32(D50, -0x2800); }
                else { if (RS32(D50) > 0x48000) W16(HV + 0x1C, 0); add32(D50, 0x2800); }
                if (iabs(RS32(D50)) > 0x30000) W32(D50, RS32(D50) < 1 ? (uint32_t)-0x30000 : 0x30000);
                if (R16(HV + 0x1A) == 0) {
                    if (RS32(OY) < 0x05080000) { add32(D54, -0x1000); if (RS32(D54) < -0x28000) W32(D54, (uint32_t)-0x28000); }
                    else { W16(HV + 0x1A, 1); add32(D54, 0x1000); }
                } else if (RS32(OY) < 0x04B80001) { W16(HV + 0x1A, 0); add32(D54, -0x1000); }
                else { add32(D54, 0x1000); if (RS32(D54) > 0x28000) W32(D54, 0x28000); }
            }
        } else if (st == 4) {
            if (RS16(HV + 0xC) == 0) {
                W32(D50, 0);
                add32(D54, -0x4000);
                if (RS32(D54) < -0x70000) W32(D54, (uint32_t)-0x70000);
                if (RS32(OY) > 0x05400000) {
                    W32(OX, 0x08000000u); W32(OY, 0x05400000u);
                    W32(0x060F4D68u, 0x08000000u); W32(0x060F4D6Cu, 0x05400000u);
                    W16(HV + 0xA, 0);
                    W16(CTRL + 0x20, R16(CTRL + 0x20) & 0xFFF4);
                    W32(0x060EA550u, (uint32_t)(RS32(PX) - RS32(0x060252A0u)));
                    W32(0x060EA554u, (uint32_t)(RS32(PY) - RS32(0x060252A4u)));
                }
            }
        } else if (st == 5 || st == 7) {
            if (RS16(HV + 0xC) == 0) {
                W32(D54, 0);
                add32(D50, 0x2000);
                if (RS32(D50) > 0x40000) W32(D50, 0x40000);
                if (RS32(OX) < 0x07600000) { W16(HV + 0xA, (uint32_t)(st + 1)); W16(HV + 0xC, 0); W32(D50, 0); W32(D54, 0); }
            }
        } else if (st == 6 || st == 8) {
            int16_t sub = RS16(HV + 0xC);
            if (sub == 0) {
                W32(D50, 0);
                W32(0x060F4D6Cu, (uint32_t)(RS32(0x060252A4u) - RS32(PY) + 0x053F0000));
                W16(HV + 0xC, 1); W16(HV + 0x1C, 1);
                W32(D54, st == 6 ? (uint32_t)-0x30000 : 0x2C000u);
            } else if (sub == 1) {
                if (R16(HV + 0x1C) == 0) {
                    if (RS32(D54) < -0x28000) { W16(HV + 0x1C, 1); if (st == 8) add32(D54, -0x1000); }
                    else add32(D54, -0x1000);
                } else if (RS32(D54) < 0x28001) add32(D54, 0x1000);
                else { W16(HV + 0x1C, 0); if (st == 8) add32(D54, 0x1000); }
                add32(D50, -0x1800);
                if (RS32(D50) < -0x40000) W32(D50, (uint32_t)-0x40000);
                if (RS32(OX) > 0x08800000) W16(HV + 0xC, 2);
            } else if (sub == 2) {
                if (R16(HV + 0x1C) == 0) {
                    if (RS32(D54) < 0x28001) add32(D54, 0x1000);
                    else { W16(HV + 0x1C, 1); if (st == 8) add32(D54, 0x1000); }
                } else if (RS32(D54) < -0x28000) { W16(HV + 0x1C, 0); if (st == 8) add32(D54, -0x1000); }
                else add32(D54, -0x1000);
                add32(D50, 0x1800);
                if (RS32(D50) > 0x40000) W32(D50, 0x40000);
                if (RS32(OX) < 0x07600000) { W16(HV + 0xE, 0); W32(D50, 0); W32(D54, 0); W16(HV + 0xC, 3); }
            } else if (sub == 3) {
                add16(HV + 0xE, 1);
                if (RS16(HV + 0xE) > 0x28) W16(HV + 0xC, 0);
            }
        }
    }
    if (RS32(PY) < 0x05700000 && RS32(0x060252A0u) > 0x0CC00000) W32(CTRL, 0x0CC00000u);
    int32_t want = (RS32(PX) < 0x07680000 || RS32(PX) > 0x089F0000) ? 0x07100000 : 0x06900000;
    int32_t cur = RS32(CTRL + 0xC);
    if (iabs(cur - want) > 0x3FFFF) want = cur < want ? cur + 0x40000 : cur - 0x40000;
    W32(CTRL + 0xC, (uint32_t)want);
}

static void hook_05(void)                                    /* 0x0606D7F2 (3-2) */
{
    W16(CTRL + 0x20, R16(CTRL + 0x20) & 0xFDFF);
    if (R16(PF2) & 0x400) OR16(CTRL + 0x20, 0x200);
    if (FROZEN) return;
    if (!(R16(V3C) & 1)) {
        Scl_SetColMix(0x10, 0xF);                             /* 0x0602B344: NBG2 colour calc ratio 15 */
        if (RS16(V38) == 0x3F) {
            OR16(V3C, 1);
            W16(CTRL + 0x2E, 2);                              /* NBG2 priority (read by 0x060472D2) */
            Ram_Ptr(0x0603B4ACu)[0] = (uint8_t)((Ram_Ptr(0x0603B4ACu)[0] & 0xDF) | 0x20);
            /* PRINB: only when the PC keeps the VDP2 priority registers (else C+0x2E is used) */
            if (R16(VDP2_SHADOW_ADDR + 0x18) | R16(VDP2_SHADOW_ADDR + 0x1A))
                W16(0x0603B486u, (R16(0x0603B486u) & 0xFF00) | 2);
            sub_06007CDE(0x22D);
        }
    }
    add16(V38, 1);
}

static int in_box(int32_t x0, int32_t x1, int32_t y0, int32_t y1)
{
    return RS32(PX) > x0 && RS32(PX) < x1 && RS32(PY) > y0 && RS32(PY) < y1;
}

static void hook_06(void)                                    /* 0x0606D8C4 (4-1) */
{
    if (RS32(PX) > 0x17A00000 && RS32(PY) > 0x06FF0000) OR16(PF4, 0x10);
    W16(CTRL + 0x20, R16(CTRL + 0x20) & 0xFEFF);
    if (in_box(0x07F50000, 0x0AC30000, 0x05C80000, 0x066A0000) || in_box(0x0D1E0000, 0x0EF30000, 0x04F90000, 0x05600000) ||
        in_box(0x0FB00000, 0x11080000, 0x05080000, 0x05600000))
        OR16(CTRL + 0x20, 0x100);
}

static void hook_07(void)                                    /* 0x0606D99E (4-2) */
{
    if (FROZEN) return;
    if (RS32(HV) == 0) {
        for (int i = 0; i < OBJ_SLOTS; i++)                  /* the boss is in: NBG1 starts waving */
            if (R16(obj(i)) == 0x20 && R16(obj(i) + 0x52) != 0) {
                W16(LANIM + 0xC, 0xA00);
                W32(LANIM + 0x64, 0x05E70000u);
                LayerAnim_SetWave(2, 2, 0, 0x40, 0x50000, 0x200000);
                W32(HV, 1);
            }
    } else if (RS32(HV + 4) != 0 && RS32(HV + 8) == 0) {     /* boss dead (0x0606DC5C): wave out */
        add32(HV + 0xC, -1);
        if (RS32(HV + 0xC) == 0) {
            LayerAnim_SetWave(2, 4, 1, 0x80, 0xA0000, 0x200000);
            W32(HV + 8, 1);
        }
    }
    if (RS32(HV + 4) == 0)                                    /* dropped items slide with the ledges */
        for (uint32_t i = 0; i < 0x40; i++) {
            uint32_t it = ITEMS + i * 0x30;
            if (R16(it) == 0xFFFF) continue;
            int32_t x = RS32(it + 4) - 0x04000000, a = x - 0x400000;
            if (a < 0) a = 0x400000 - x;
            if (a <= 0x200000) { add32(it + 8, 0x2000); continue; }
            a = x - 0xA00000;
            if (a < 0) a = 0xA00000 - x;
            add32(it + 8, a <= 0x200000 ? 0x6000 : 0x4000);
        }
    int sum = 0, found = 0;                                  /* boss (and clone) life */
    for (int i = 0; i < OBJ_SLOTS; i++) {
        uint32_t o = obj(i);
        if (R16(o) != 0x20 || (R16(o + 0x12) & 0x1F) >= 2) continue;
        sum += RS16(o + 0x16);
        if (found) break;
        found = 1;
    }
    Hud_DrawBossBar(RS16(0x060BD0B8u + 2u * R16(0x060252C4u)), (int16_t)sum);   /* 0x06061B58 */
}

static void hook_08(void)                                    /* 0x0606DC68 (5-1) */
{
    if (FROZEN) return;
    int32_t cx = RS32(0x060252A0u), cy = RS32(0x060252A4u);
    if (RS32(PX) > 0x7B300000) OR16(PF4, 0x10);
    if (cx > 0x30000000 && cx < 0x32000000 && cy > 0x06000000) W32(CTRL + 4, 0x04000000u);
    if (cx > 0x50000000 && cy > 0x05FFFFFF) W32(CTRL + 4, 0x06000000u);
    uint32_t p = 0;                                          /* 0x06058658 */
    for (int i = 0; i < OBJ_SLOTS && !p; i++)
        if (R16(obj(i)) == 0x103D && !(R16(obj(i) + 0x12) & 3) && (R16(obj(i) + 4) & 1)) p = obj(i);
    if (p && cx - 0x300000 < RS32(PX)) { W32(CTRL, (uint32_t)(RS32(p + 8) - 0x01400000)); OR16(CTRL + 0x20, 2); }
    int16_t t = RS16(V38);
    if (t == 0x7FFE) return;
    OR16(PF4, 0x10);
    if (t < 0x40) { add16(V38, 1); W32(PY, 0x08500000u); W32(PVY, 0x10000); }
    else if (t <= 0x4B) { W32(PVY, 0x80000); add16(V38, 1); }
    else {
        add16(V38, 1);
        if (RS16(V38) > 0xD0) { W16(V38, 0x7FFE); W32(CTRL, R32(0x060252A0u)); W16(PF4, R16(PF4) & 0xFFEF); }
    }
}

static void objs_1043(int v)                                 /* 0x06058C58 / 0x06058CB8 */
{
    for (int i = 0; i < OBJ_SLOTS; i++) if (R16(obj(i)) == 0x1043) W16(obj(i) + 0x48, (uint32_t)v);
}

static void hook_09(void)                                    /* 0x0606DDBC (5-2) */
{
    if (FROZEN) return;
    int16_t s = RS16(HV + 4);
    if (s == 0 || s == 1) { add16(HV + 6, 1); if (RS16(HV + 6) > 5) { W16(HV + 6, 0); W16(HV + 4, (uint32_t)(s + 1)); } }
    else if (s == 2) {
        add16(HV + 6, 1);
        if (RS32(PX) > 0x05DFFFFF) { OR16(CTRL + 0x20, 0x80); W16(HV + 4, 3); W16(HV + 6, 0x6E); sub_06007CDE(0x10E); }
    } else if (s == 3) {
        add16(HV + 6, -1);
        if (RS16(HV + 6) < 1) { W16(CTRL + 0x20, R16(CTRL + 0x20) & 0xFF7F); W32(HV, R32(HV) | 1); W16(HV + 4, 4); }
    } else if (s == 5) {
        add16(HV + 6, -1);
        if (RS16(HV + 6) < 1) {
            W16(CTRL + 0x20, R16(CTRL + 0x20) & 0xFF7F);
            W16(HV + 4, 6);
            objs_1043(2); W16(PF4, R16(PF4) & 0xFBEF); sub_06007CDE(0x211);
        }
    } else if (s == 6) {
        for (uint32_t i = 0; i < 0x40; i++) {
            uint32_t it = ITEMS + i * 0x30;
            int32_t x = RS32(it + 4) - 0x04000000;
            if (R16(it) != 0xFFFF && x > 0x14E0000 && x < 0x2700000) add32(it + 8, -0xC000);
        }
        for (uint32_t i = 0; i < 0x40; i++) {
            uint32_t pr = 0x060F23D0u + i * 0x40;
            if (R16(pr) != 0xFFFF && R16(pr) != 0x29) add32(pr + 0xC, -0xC000);
        }
    }
    if (R32(HV) & 1) {
        W32(HV, R32(HV) & 0xFFFFFFFEu);
        W32(CTRL + 0xC, 0x2FA00000u);
        W32(CTRL, 0x05400000u);
        W32(0x060EA550u, (uint32_t)(RS32(PX) - RS32(0x060252A0u)));
        W32(0x060EA554u, (uint32_t)(RS32(PY) - RS32(0x060252A4u)));
        objs_1043(1);
    }
    if (RS16(HV + 4) > 2) {
        static const int16_t n[9] = { 4, 6, 2, 3, 10, 3, 2, 6, 4 };
        static const int32_t r[9] = { 0, 0xC000, 0x8000, 0x5000, 0x2800, 0x5000, 0x8000, 0xC000, 0 };
        W16(LANIM + 0xC, 0x100);
        W32(LANIM + 0x68, 0x05E70000u);
        LayerAnim_SetParam(2, 8);
        for (int i = 0; i < 9; i++) CellList_Set(1, i, 0x2000, n[i], r[i]);
        CellList_Set(1, 9, 0x8000, 0, 0);
    }
}

static void hook_10(void)                                    /* 0x0606E2F4 (6-1) */
{
    W32(0x060EA55Cu, 0x900000); W32(0x060EA560u, 0xA00000);
    if (!(R16(PF2) & 0x4000)) { W32(0x060EA55Cu, 0xC00000); W32(0x060EA560u, 0xD00000); }
    if (FROZEN) return;
    if (RS32(PX) > 0x27880000) OR16(PF4, 0x10);
    if (RS32(0x060252A4u) > 0x08000000) W32(CTRL + 4, 0x08000000u);
    W32(CTRL + 0xC, RS32(PX) < 0x0F400000 ? 0x06000000u : 0x0A000000u);
}

static void hook_11(void)                                    /* 0x0606E3A0 (6-2) */
{
    int32_t t = RS16(V38);
    LineList_Set(1, 0, 0x4000, 0x10, t << 15);
    LineList_Set(1, 1, 0x4000, 0x20, t << 16);
    LineList_Set(1, 2, 0x4000, 0x20, t << 15);
    LineList_Set(1, 3, 0x4000, 0x1C, t << 14);
    LineList_Set(1, 4, 0x4000, 0x90, 0);
    add16(V38, 1);
}

static void hook_12(void)                                    /* 0x0606C676 (7-1) */
{
    if (!FROZEN) {
        uint32_t t = R16(V38);
        int32_t base = RS32(DESC2) - RS32(DESC2 + 0x10);
        W32(LLIST + 0x280 + 4, t * 0x4000u + (uint32_t)base);     /* 0x060F61B0 */
        W32(LLIST + 0x280 + 12, t * 0x2000u + (uint32_t)base);
        W32(LLIST + 0x280 + 20, t * 0x1000u + (uint32_t)base);
        add16(V38, 1);
        if (RS32(0x060252A0u) > 0x24C00000) W32(CTRL, 0x24C00000u);
        if (RS32(PX) > 0x37600000) OR16(PF4, 0x10);
    }
    OR16(LINE_FLAGS, 0x8000);
}

static void hook_13(void)                                    /* 0x0606C760 (7-2) */
{
    int32_t dy = RS32(0x060252A4u) - RS32(CTRL + 4);
    for (uint32_t ln = 0; ln < 0x108; ln++) {
        int32_t v = (int32_t)(ln >> 3) + (int16_t)(dy >> 17) - 0x3C;
        W32(LINE_HBL + ln * 16 + 4, (uint32_t)v); W32(LINE_HBL + ln * 16 + 8, (uint32_t)v); W32(LINE_HBL + ln * 16 + 12, (uint32_t)v);
    }
    OR16(LINE_FLAGS, 0x8000);
}

static void hook_14(void)                                    /* 0x0606E5C0 (8-1) */
{
    int32_t t = RS16(V38);
    LineList_Set(1, 0, 0x4000, 0x20, t << 15);
    LineList_Set(1, 1, 0x4000, 0x70, t << 14);
    LineList_Set(1, 2, 0x4000, 0x20, t << 13);
    LineList_Set(1, 3, 0x2000, 0xA0, 0x800);
    if (RS32(PY) < 0x05E10000) {
        if (RS16(G_CHECKPOINT_ADDR) == 0) {
            if (RS32(PX) > 0x04D00000 && RS32(PX) < 0x05300000 && !(R16(V3C) & 1)) {
                OR16(PF4, 0x10); OR16(V3C, 1); W32(CTRL + 0xC, 0x06500000u);
            }
        } else W32(CTRL + 0xC, 0x06000000u);
    }
    if (R16(V3C) & 1) {
        if (RS32(CTRL + 0xC) > 0x06000000) add32(CTRL + 0xC, -0x40000);
        if (RS32(PX) > 0x05800000) W16(PF4, R16(PF4) & 0xFFEF);
    }
    if (RS32(PX) > 0x05700000 && !(R16(V3C) & 4)) {
        W32(CTRL, 0x04F50000u); W32(CTRL + 0xC, 0x06000000u); OR16(V3C, 10);
    }
    if (!(R16(V3C) & 4) && RS32(PX) > 0x20C00000) {
        W32(CTRL, 0x20240000u); W32(CTRL + 0xC, 0x06000000u); OR16(V3C, 4);
        W32(0x060EA55Cu, 0xC00000); W32(0x060EA560u, 0xD00000);
    }
    if (RS32(PX) - 0x2B700000 > 0) OR16(PF4, 0x10);
    if (!(R16(V3C) & 8)) {                                   /* colour gradient from the top */
        int16_t s = (int16_t)(0x10 - (int16_t)((RS32(0x060252A4u) - RS32(CTRL + 4)) >> 22));
        for (uint32_t ln = 0; ln < 0x108; ln++)
            if (s < 0) {
                int32_t v = (int32_t)s - (int32_t)(ln >> 5);
                W32(LINE_HBL + ln * 16 + 4, (uint32_t)v); W32(LINE_HBL + ln * 16 + 8, (uint32_t)v); W32(LINE_HBL + ln * 16 + 12, (uint32_t)v);
            }
    } else {                                                 /* water level (0x060F4D3A) */
        if (!(R16(V3C) & 0x10)) {
            if (RS16(V3A) < 0x1B1) OR16(V3C, 1);
            else add16(V3A, -1);
        }
        if (!(R16(VBLANKS) & 0x40)) { if ((R16(VBLANKS) & 7) == 7) add16(V3A, -1); }
        else if ((R16(VBLANKS) & 7) == 7) add16(V3A, 1);
        int16_t s = (int16_t)((uint32_t)(RS32(0x060252A4u) - RS32(CTRL + 4)) >> 16);
        for (uint32_t ln = 0; ln < 0x108; ln++) {
            int32_t d = (int32_t)s + (int32_t)ln, v = d < RS16(V3A) ? 0 : (d - RS16(V3A)) * 2;
            W32(LINE_HBL + ln * 16 + 4, (uint32_t)v); W32(LINE_HBL + ln * 16 + 8, (uint32_t)v); W32(LINE_HBL + ln * 16 + 12, (uint32_t)v);
        }
    }
    OR16(LINE_FLAGS, 0x8000);
    add16(V38, 1);
}

static void hook_16(void)                                    /* 0x0606E8CC (9-1) */
{
    static const uint16_t blink[4] = { 0x1F, 0x18, 0x10, 0x08 };
    W16(SHADOW_CRAM + 0xBE * 2, blink[R16(V38) & 3]);         /* Pal_Copy(0xBE, 1, &colour) */
    W16(0x060250A8u, 1);
    if (RS32(PX) > 0x33900000 && RS32(PY) < 0x04A40000) OR16(PF4, 0x10);
    W16(CTRL + 0x20, R16(CTRL + 0x20) & 0xFEFF);
    if (RS32(PY) - 0x04000000 > 0x03080000) OR16(CTRL + 0x20, 0x100);
    int32_t want = RS32(PY) - 0x04000000 < 0x02C00000 ? 0x640000 : 0xC80000;
    int32_t d = want - RS32(0x060EA55Cu);
    if (iabs(d) > 0x30000) d = d < 0 ? -0x30000 : 0x30000;
    add32(0x060EA55Cu, d);
    W32(0x060EA560u, (uint32_t)(RS32(0x060EA55Cu) + 0x80000));
    add16(V38, 1);
}

static void rot(const uint16_t *off, int n)                  /* CRAM words: [0] <- [1] <- ... <- [n-1] <- [0] */
{
    uint16_t first = R16(SHADOW_CRAM + off[0]);
    for (int i = 0; i < n - 1; i++) W16(SHADOW_CRAM + off[i], R16(SHADOW_CRAM + off[i + 1]));
    W16(SHADOW_CRAM + off[n - 1], first);
}

static void hook_17(void)                                    /* 0x0606E9DA (9-2) */
{
    if (!FROZEN) {
        if (R16(VBLANKS) & 1) {                              /* palette pulse */
            if (R16(HV) != 0) { if (Pal_StepTowards(0, 0x100, R32(MAPFILE) + 0xC00, 1)) W16(HV, 0); }
            else if (Pal_StepTowards(0, 0x100, 0x060DBC88u, 1)) W16(HV, 1);
        }
        if (RS16(HV + 2) < 1) {                              /* palette rotation every 16 frames */
            static const uint16_t a[3] = { 0x3D6, 0x3DA, 0x3D8 }, b[2] = { 0x3DC, 0x3DE },
                c[5] = { 0x3E0, 0x3E8, 0x3E6, 0x3E4, 0x3E2 }, d[5] = { 0x3EA, 0x3F2, 0x3F0, 0x3EE, 0x3EC },
                e[2] = { 0x3F4, 0x3F6 }, f[4] = { 0x3F8, 0x3FE, 0x3FC, 0x3FA };
            W16(HV + 2, 15);
            rot(a, 3); rot(b, 2); rot(c, 5); rot(d, 5); rot(e, 2); rot(f, 4);
            W16(0x060250A8u, 1);
        } else add16(HV + 2, -1);
        if (smod(R16(VBLANKS), 200 - 30 * RS16(0x060F7AD4u)) >= 20) W32(HV + 0x10, (uint32_t)sub_0602DDD2());
        else {                                               /* lightning strike 0x104D */
            int32_t q = smod(RS32(HV + 0x10) >> 8, 18);
            int found = 0;
            for (uint32_t i = 0; i < 0x40 && !found; i++) found = R16(0x060F23D0u + i * 0x40) == 0x1C;
            if (!found) { int32_t lim = 17 - (RS16(0x060F7AD4u) >> 1); if ((int16_t)q < lim) q = (int16_t)lim; }
            else q = 30;
            if (smod(R16(VBLANKS), (int16_t)q) == 0) {
                int32_t y = (int32_t)(umod((uint32_t)sub_0602DDD2() >> 4, 200) << 16) + 0x04000000;
                int32_t x = (int32_t)(umod((uint32_t)sub_0602DDD2() >> 4, 320) << 16) + 0x04000000;
                Obj_Register(0x104D, x, y, 0, 0xFFFF);
                sub_06007CDE(0x209);
                add16(HV + 0xC, 10);
                if (RS16(HV + 0xC) > 30) W16(HV + 0xC, 30);
            }
        }
        if (RS16(HV + 0xC) > 0) { OR16(CTRL + 0x20, 0x80); add16(HV + 0xC, -1); }
        else { W16(CTRL + 0x20, R16(CTRL + 0x20) & 0xFF7F); W16(HV + 0xC, 0); }
    }
    Orbs_Update();                                           /* 0x0605EF3C */
    if (FROZEN && (R16(0x060252BAu) & 0x8000) && (R16(0x060252BAu) & 0xF) == 6) {
        W16(LINE_FLAGS, R16(LINE_FLAGS) & 0xDFFF);
        return;
    }
    W32(DESC2 + 0xC, R32(DESC2 + 0x14));
    if (R16(HV + 6)) {
        int ph = RS16(HV + 4);
        add16(HV + 4, 1);
        LineWave_Apply(2, ph, 0, 0xE0, 0x200000, 0x20000, 0);
    }
    LineList_Set(2, 0, 0x4000, 0xE0, RS32(HV + 8) + RS32(0x060252A0u) - 0x04000000);
    LineList_Set(2, 1, 0x8000, 0, 0);
    W32(HV + 8, R32(DESC2 + 0x10));
    OR16(LINE_FLAGS, 0xA000);
}

static const HookFn kHooks[18] = {                           /* 0x060B10CC */
    hook_00, hook_01, hook_02, 0, hook_04, hook_05, hook_06, hook_07, hook_08, hook_09,
    hook_10, hook_11, hook_12, hook_13, hook_14, 0 /* 0x0606E8B4: rts */, hook_16, hook_17 };

void Stage_FrameHooks(void)
{
    int st = RS16(G_STAGE_INDEX);
    if (st >= 0 && st < 18 && kHooks[st]) kHooks[st]();
    LayerAnim_Update();                                      /* 0x0606F6AC */
    LayerAnim_Commit();                                      /* 0x0606EF9C */
}

/* Ninjutsu effects 0x06063538 / 0x06063554: ninjutsu.c */
