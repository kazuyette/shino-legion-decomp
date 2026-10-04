#ifndef NINJUTSU_H
#define NINJUTSU_H
#include <stdint.h>
/* Sho's ninjutsu (magic), ported from SHINOBI.dec.bin (docs/ninjutsu.md).
 *
 * RAM (original addresses):
 *   0x060252BA u16 MAGIC flags: low nibble = ninjutsu kind (0..6), 0x00F0 = armed sub-kind
 *              (0 shield, 0x40 = kind 4 "Bishamon" sword), 0x2000 = cast requested / running,
 *              0x4000 = armed (shield / kind 4 points in 0x060252BC), 0x8000 = effect started
 *   0x060252BC s16 armed magic points (6 when armed; shield absorbs, run special costs 2)
 *   0x060252BE s16 ninjutsu available (HUD icon; 1 after the scroll pickup, 0 after kind 2)
 *   0x060EA43C u16 MAGIC saved at the cast (0xFFFF none) to re-arm a running shield after
 *   0x060F4BF0 effect block: +0 u16 state (0 idle, 2 Ziryu, 5 Nyorai, 6 Bishamon, 7 burst),
 *              +2 u16 step timer, +4 u16 frame counter, +6 u16 step, +8 u32 step-duration
 *              table (0xFFFF = until the handler advances), +0xC.. per-effect variables,
 *              +0xB8 u16 graphics request (2/5/6), +0xBC u32 step callback (original address),
 *              +0xC0 3 x s32 colour offsets saved at the start (restored by the end fade),
 *              +0xCC u16 NBG2 wave phase, +0xD0/+0xD4 s32 wave length / amplitude
 *   0x060EA32C 2 x 0x78 player sub-objects (kind 4 sword flashes, 0x060465B8). */

void Player_NinjutsuExec(void);      /* 0x060437C0: actions 0x15 / 0x2C / 0x3E / 0x4A */
int  Ninjutsu_Start(uint16_t flags); /* 0x060634A4: start the effect of MAGIC & 0xF */
void Magic_Apply(void);              /* 0x06045D1E: the ninjutsu's effect once the show ended */
void Magic_RestoreGfx(void);         /* 0x06045E86: re-load an armed shield / kind-4 graphics */
/* 0x06049768: damage every enemy on screen. mode & 0xF: 0 all, 1 only those without
 * +0x12 & 0x8000, 2 only those with it. */
void Enemies_MagicDamage(int16_t dmg, uint16_t mode);
void Ninjutsu_Init(void);            /* 0x06063538 */
int  Ninjutsu_Update(void);          /* 0x06063554: 1 = no effect running / just ended */

void PlayerFx_Reset(void);           /* 0x060468FC */
void PlayerFx_Update(void);          /* 0x060465B8 (stage tick after Player_Tick) */
void PlayerFx_OnAttack(void);        /* 0x06046782 (anim event 2 of attack actions, kind 4) */
void Ninjutsu_PlayerStageInit(void); /* part of 0x06044E18 (ring, sub-objects, magic graphics) */
#endif
