/* Logging stubs for original functions not ported yet (see player_ext.h).
 * Each logs once on first call so the console shows what the current
 * gameplay path still depends on. */
#include "player_ext.h"
#include <stdio.h>

#define ONCE(name) do { static int s_; if (!s_) { s_ = 1; printf("[stub] %s not ported\n", name); } } while (0)

void     sub_06007CDE(int sfx)  { printf("[sfx] %03X\n", sfx); }
void     sub_06007E00(void)     { ONCE("06007E00"); }
int      sub_0600A604(void)     { ONCE("0600A604 (idle-anim gate, returns 1)"); return 1; }
int      sub_0600CEF4(void)     { ONCE("0600CEF4"); return 0; }
void     sub_06040F80(void)     { ONCE("06040F80 (debug free-move)"); }
void     sub_060410F8(void)     { ONCE("060410F8 (on-hit)"); }
int      sub_060424A6(void)     { ONCE("060424A6 (posture 4)"); return 0; }
void     sub_06042B90(void)     { ONCE("06042B90"); }
void     sub_06042C88(void)     { ONCE("06042C88"); }
void     sub_06042E24(void)     { ONCE("06042E24"); }
void     sub_060437C0(void)     { ONCE("060437C0 (ninjutsu)"); }
void     sub_06043A44(void)     { ONCE("06043A44"); }
void     sub_06042D80(int32_t dx, int32_t dy, int a, int32_t v) { printf("[obj] shuriken dx=%08X dy=%08X angle=%02X speed=%08X\n", (unsigned)dx, (unsigned)dy, a, (unsigned)v); }
#include "ram.h"
void     sub_06044280(void)     { ONCE("06044280"); }
uint16_t sub_0604588E(int32_t x, int32_t y, int r, int16_t p) { (void)x; (void)y; (void)r; (void)p; ONCE("0604588E (terrain probe, returns 0xFF)"); return 0xFF; }
void     sub_0604592E(void)     { ONCE("0604592E"); }
void     sub_06046782(void)     { ONCE("06046782"); }
void     Obj_Spawn(int t, int32_t x, int32_t y, int l, int p) { printf("[obj] spawn type %d at %08X,%08X left=%d p=%d\n", t, (unsigned)x, (unsigned)y, l != 0, p); }
void     sub_0602DDD2(void)     { ONCE("0602DDD2"); }

void     sub_060451C8(void)     { ONCE("060451C8 (camera scroll)"); }

/* TEMPORARY PC PLACEHOLDER for 0x06045340 (terrain collision, needs map data):
 * a flat floor at Y = 0x00C00000. Sets/clears the airborne flag, counts air
 * frames in +0x1A, and on landing switches airborne poses to 0x3D (landing)
 * and raises the "landed" flag (flags4 & 0x20) that many actions wait for. */
#define PC_FLOOR_Y 0x00C00000
void     sub_06045340(uint32_t o, int n)
{
    (void)n; ONCE("06045340 terrain collision -> flat-floor placeholder");
    if (RS32(o + 0x0C) >= PC_FLOOR_Y && RS32(o + 0x34) <= 0) {
        W32(o + 0x0C, PC_FLOOR_Y);
        W32(o + 0x34, 0);
        if (R16(o + 0x02) & 0x4000) {
            int a = RS16(o + 0x06);
            if (a == 0x37 || a == 0x39 || a == 0x35 || a == 0x38 || a == 0x3A)
                W16(o + 0x06, 0x3D);
            W16(o + 0x04, R16(o + 0x04) | 0x20);
        }
        W16(o + 0x02, R16(o + 0x02) & (uint16_t)~(0x4000 | 0x1000 | 0x0100 | 0x0080));
        W16(o + 0x1A, 0);
    } else {
        W16(o + 0x02, R16(o + 0x02) | 0x4000);
        W16(o + 0x1A, R16(o + 0x1A) + 1);
    }
}
void     sub_06007D60(int sfx, int bank) { printf("[sfx] bank %d %03X\n", bank, sfx); }
