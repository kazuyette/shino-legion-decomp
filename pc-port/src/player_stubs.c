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
#include "ram.h"
void     sub_06044280(void)     { ONCE("06044280"); }
void     sub_0604592E(void)     { ONCE("0604592E"); }
void     sub_06046782(void)     { ONCE("06046782"); }
void     sub_0602DDD2(void)     { ONCE("0602DDD2"); }

void     sub_06007D60(int sfx, int bank) { printf("[sfx] bank %d %03X\n", bank, sfx); }
