#ifndef ENEMY_INT_H
#define ENEMY_INT_H
/* Internal helpers shared by the enemy modules (enemy.c, enemy_soldier*.c, enemy_t3.c,
 * enemy_t4.c). Everything works on the "current enemy" O = *(0x060F205C), like the
 * original code. Ported function addresses are given next to each prototype. */
#include <stdint.h>
#include "enemy.h"
#include "ram.h"

#define O         R32(ENEMY_CUR)
#define SLOT      RS16(ENEMY_CUR_SLOT)
#define PL        0x060EA2B4u
#define CAM_X     0x060252A0u
#define CAM_Y     0x060252A4u
#define FROZEN    (R16(0x0602529Eu) & 0x8000)
#define SPR       0x060250ACu
#define MAGIC     0x060252BAu        /* ninjutsu / magic state */
#define STAGE     RS16(0x0602526Au)
#define CHAR_BASE(s)  RS16(0x060A9064u + 2u * (uint32_t)(int16_t)(s))
#define COLOR_BANK(s) RS16(0x060A9084u + 2u * (uint32_t)(int16_t)(s))
#define SCRATCH_ANIM  0x060FFE80u    /* PC: stand-in for the originals' on-stack anim states (0x24 bytes) */
#define SCRATCH_ANIM2 0x060FFEB0u    /* a second one */

void Enemy_Once(const char *what, unsigned id);       /* log "not ported" once per id */
void Pal_Copy(int dst, int n, uint32_t src);          /* A.BIN 0x06005C30 */

int  Player_SwordPower(void);                         /* 0x06045BA6 */
int  Player_AttackKind(void);                         /* 0x0604634E */
int  Enemy_AttackPlayer(int16_t dmgWeapon, int16_t dmgTouch, uint16_t mode);   /* 0x0604931E */
int  Enemy_CheckShurikens(void);                      /* 0x0604967C */
int  Enemy_CheckPlayerHits(uint16_t mode);            /* 0x060494B4 */
void Enemy_DrawOverlay(uint32_t st, int16_t bank, uint32_t a, uint32_t b, uint32_t c);   /* 0x06049A4C */

/* soldier (type 1) common AI */
void Soldier_Init(void);                              /* 0x0608FDE4 */
int  Soldier_CheckHits(void);                         /* 0x06092C74 */
void Soldier_OnHit(int r, uint32_t a, uint32_t b, uint32_t c);   /* 0x06092E14 */
int  Soldier_AnimDone(int16_t next);                  /* 0x06092ECE */
int  Soldier_PlayerZone(void);                        /* 0x0609379C */
int  Soldier_Decide(uint32_t next);                   /* 0x060936DE */
void Soldier_Jump(int16_t landAct, int16_t airAct);   /* 0x06093856 */
int  Soldier_Aim(uint32_t o, int32_t dx, int32_t dy); /* 0x06093518 */
int  Soldier_Think(uint32_t next);                    /* 0x0609307A */
void Soldier_Physics(void);                           /* 0x06093ACC */
int  Soldier_Dying(uint32_t a, uint32_t b, uint32_t c);   /* 0x060935F0 */
void Soldier_Draw(uint16_t p);                        /* 0x06093930 */

/* soldier subtypes (+0x12 & 0x1F = 2..12, jump table 0x0608FC48) — enemy_soldier_sub.c */
void Soldier_Subtype(uint16_t sub);
/* other enemy types — enemy_t3.c / enemy_t4.c */
void Enemy3_Load(void);    void EnemyHandler_0003(void);   /* 0x0609E12C / 0x0609E258 */
void Enemy4_Load(void);    void EnemyHandler_0004(void);   /* 0x06067378 / 0x060673C6 */
/* stage 4-2 boss (type 0x20) and its projectiles 0x20 (orb) / 0x21 (lightning) — enemy_boss42.c */
void Enemy20_Load(void);   void EnemyHandler_0020(void);   /* 0x06084420 / 0x06084560 */
void Proj_Boss42Orb(uint32_t e);                           /* 0x06085CD8 */
void Proj_Boss42Bolt(uint32_t e);                          /* 0x06086A68 */
#endif
