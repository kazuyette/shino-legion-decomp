/* Logging stubs for original functions not ported yet (see player_ext.h).
 * Each logs once on first call so the console shows what the current
 * gameplay path still depends on. */
#include "audio.h"
#include "sfx.h"
#include "resource.h"
#include "player_ext.h"
#include <stdio.h>

#define ONCE(name) do { static int s_; if (!s_) { s_ = 1; printf("[stub] %s not ported\n", name); } } while (0)

/* Snd_Play 0x06007CDE -> sfx.c (SCSP .SND sequences) through audio.c's SFX hook */
void     sub_06007CDE(int sfx)  { Res_EnsureSoundBank(); Audio_PlaySfx((int16_t)sfx); }
void     sub_06007E00(void)     { Audio_StopSfx(); }   /* Snd_StopAll */
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

/* 0x06007D60: Snd_Play(sfx) only while fewer than `bank` (max count) of the 8 sequence
 * slots (0x06025138) already play this id; else returns -1 */
void     sub_06007D60(int sfx, int bank) { if (Sfx_CountLocked(sfx) < bank) sub_06007CDE(sfx); }

/* Sound calls of DIRECTOR (SCSP / CD-DA, work packages D8/D9), declared in director_int.h. */
void Snd_SetAreaMap(uint32_t map)  { (void)map; ONCE("06007E96 Snd_SetAreaMap"); }
void Snd_SetCdDaLevel(int l, int r) { Audio_SetMusicLevels(l, r); }   /* 0x0603125C */
void Snd_SetCdDaPan(int l, int r)   { (void)l; (void)r; ONCE("060312F4 Snd_SetCdDaPan"); }
void Snd_SetMasterVolume(int v)     { (void)v; ONCE("06030710 Snd_SetMasterVolume"); }
