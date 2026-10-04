#ifndef ENEMY_MORE_H
#define ENEMY_MORE_H
/* Helpers shared by the enemy types ported in enemy_t02.c, enemy_t22.c, ... (agent
 * "enemies", 2026-10-04). Everything works on the current enemy O, like enemy_int.h. */
#include <stdint.h>
#include "enemy_int.h"

void Obj_Integrate(uint32_t o);                          /* physics.c, 0x060452A4 */

static inline int32_t EM_Abs(int32_t v) { return v < 0 ? -v : v; }
static inline int32_t EM_Sdiv(int32_t a, int32_t b) { return b ? a / b : 0; }   /* A.BIN 0x060092BC */

/* Off-screen test inlined in the handlers: |x - camX - 160| >= 256 or
 * |y - camY - yc| >= 256 (yc = 160 or 112 px, 16.16). 1 = far. */
int  EM_Far(uint32_t o, int32_t yc);
/* face Sho: flags2 &= 0x7FFF, |= 0x8000 when Sho.x < x */
void EM_FaceSho(uint32_t o);
/* 0x06097130 (shared): jump at Sho, +0x4C = vertical speed >> 12, +0x4E = horizontal >> 12 */
void EM_SolveJump(int32_t vmin, int32_t vmax, int16_t h);
/* 0x06080808 (shared): keep X 24 px inside the stage bounds 0x060EA618 / 0x060EA620 */
void EM_ClampX(uint32_t o);
/* the "face each other" reaction after Enemy_AttackPlayer != 0 */
void EM_PlayerFaceBack(uint32_t o);
/* death overlay (anim `anim` of defs/scripts, drawn with colour bank + 1) kept in
 * +a/+b/+c; start = copy a fresh state into the fields, draw = tick it; 1 when over */
void EM_OverlayStart(uint32_t defs, uint32_t scripts, int16_t anim, uint32_t a, uint32_t b, uint32_t c);
int  EM_OverlayDraw(uint32_t defs, uint32_t scripts, int16_t anim, uint32_t a, uint32_t b, uint32_t c);
/* sprite position/flip/bank for the current enemy (0x060250AE.. writes) */
void EM_SprBasic(uint32_t o, int16_t bank);
/* 0x06060E8E / 0x06060F64 (local copies): projectile p's kind-kp boxes vs an object's
 * kind-ko boxes; EM_ProjTouch scans Sho (hostile p) or the object table */
uint16_t EM_ProjHitbox(uint32_t o, int16_t ko, uint32_t p, int16_t kp);
uint16_t EM_ProjTouch(uint32_t p, int16_t kp, uint32_t *who, int16_t ko);
/* 0x06049BA4 / 0x06049BF0: the bosses' afterimage trail (8 copies, one every 4 frames) */
void EM_TrailReset(void);
void EM_Trail(int active, uint32_t o);
#endif
