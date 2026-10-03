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
int      Player_SelectAction_Crouch(void) { ONCE("06041B3C SelectAction_Crouch"); return 0; }
int      Player_SelectAction_Air(void)    { ONCE("06041E48 SelectAction_Air"); return 0; }
int      sub_0604241E(void)     { ONCE("0604241E"); return 0; }
int      sub_060424A6(void)     { ONCE("060424A6 (posture 4)"); return 0; }
void     sub_06042B90(void)     { ONCE("06042B90"); }
void     sub_06042C88(void)     { ONCE("06042C88"); }
void     sub_06042E24(void)     { ONCE("06042E24"); }
void     sub_060437C0(void)     { ONCE("060437C0 (ninjutsu)"); }
void     sub_06043A44(void)     { ONCE("06043A44"); }
void     sub_06043BC8(void)     { ONCE("06043BC8 (crouch slash)"); }
void     sub_06043C58(void)     { ONCE("06043C58"); }
void     sub_06042D80(int32_t dx, int32_t dy, int a, int32_t v) { printf("[obj] shuriken dx=%08X dy=%08X angle=%02X speed=%08X\n", (unsigned)dx, (unsigned)dy, a, (unsigned)v); }
#include "ram.h"
/* PC placeholder until the A.BIN animation player is ported: reset anim
 * flags (+0x58) and frame counters (+0x5A, +0x72). */
void     Anim_Start(uint32_t st, uint32_t at, uint32_t ft, int z, int anim)
{
    (void)at; (void)ft; (void)z; (void)anim; ONCE("0600648C Anim_Start (placeholder)");
    W16(st, 0); W16(st + 2, 0); W16(st + 0x1A, 0);
}
void     sub_06043FDA(void)     { ONCE("06043FDA"); }
void     sub_06044280(void)     { ONCE("06044280"); }
uint16_t sub_0604588E(int32_t x, int32_t y, int r, int16_t p) { (void)x; (void)y; (void)r; (void)p; ONCE("0604588E (terrain probe, returns 0xFF)"); return 0xFF; }
void     sub_0604592E(void)     { ONCE("0604592E"); }
void     sub_06046782(void)     { ONCE("06046782"); }
void     Obj_Spawn(int t, int32_t x, int32_t y, int l, int p) { printf("[obj] spawn type %d at %08X,%08X left=%d p=%d\n", t, (unsigned)x, (unsigned)y, l != 0, p); }
void     sub_0602DDD2(void)     { ONCE("0602DDD2"); }

void     sub_060451C8(void)     { ONCE("060451C8 (camera scroll)"); }

/* TEMPORARY PC PLACEHOLDER for 0x06045340 (terrain collision, needs map data):
 * a flat floor at Y = 0x00C00000. Sets/clears the airborne flag, counts air
 * frames in +0x1A, and on landing returns airborne actions to idle (the real
 * landing logic lives in the unported air selector 0x06041E48). */
#define PC_FLOOR_Y 0x00C00000
void     sub_06045340(uint32_t o, int n)
{
    (void)n; ONCE("06045340 terrain collision -> flat-floor placeholder");
    if (RS32(o + 0x0C) >= PC_FLOOR_Y && RS32(o + 0x34) <= 0) {
        W32(o + 0x0C, PC_FLOOR_Y);
        W32(o + 0x34, 0);
        if (R16(o + 0x02) & 0x4000) {
            int a = RS16(o + 0x06);
            if (a == 0x37 || a == 0x39 || a == 0x35 || a == 0x38 || a == 0x3A || a == 0x3D)
                W16(o + 0x06, 0);
        }
        W16(o + 0x02, R16(o + 0x02) & (uint16_t)~(0x4000 | 0x1000 | 0x0100 | 0x0080));
        W16(o + 0x1A, 0);
    } else {
        W16(o + 0x02, R16(o + 0x02) | 0x4000);
        W16(o + 0x1A, R16(o + 0x1A) + 1);
    }
}
