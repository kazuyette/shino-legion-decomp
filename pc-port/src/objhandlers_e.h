#ifndef OBJHANDLERS_E_H
#define OBJHANDLERS_E_H
#include <stdint.h>
/* Stage objects 0x1007-0x101F (SHINOBI table 0x060AC820), ported in objhandlers_e.c
 * (stage idx 13, 2, 3) and objhandlers_e2.c (stage idx 5, 0, 10). Field conventions:
 * objhandlers.h. +0x5A = script position of the object's animation (anim state [1]).
 * Stage index (0x0602526A) -> objects:
 *  13 (STAGE1_2): 0x1007 glow on the layer-1 background, 0x1008 lantern (sword: top flies off)
 *   2 (STAGE2_1): 0x1009 bush (rustles when walked through, slashed -> leaf effects 8),
 *                 0x100A/0x100B decor, 0x100C sliding gate (opens at checkpoint flags & 3 and
 *                 moves the world bottom C+0xC), 0x100D crumbling block (shakes, falls)
 *   3 (STAGE2_2): boss room: 0x100F falling-debris spawner/pieces, 0x1010/0x1011 statue
 *                 heads (12 HP, spit 0x1012 fireballs; when both die -> 0x100E)
 *   5 (STAGE3_2): 0x1013 hanging plants (sway; cut -> 0x104B leaves), 0x1014 lamp, 0x1015 sparks
 *   0 (STAGE7_1): 0x1016/0x1018 parallax decor, 0x1017 lantern (lid falls off), 0x1019 solid
 *                 block, 0x101A breakable rock pillar (falls in pieces, projectile 0x25 rocks)
 *  10 (STAGE6_1): 0x101B cannon turret (projectile 0x24), 0x101C breakable crate,
 *                 0x101D parallax decor, 0x101E breakable map tiles (layer 2) debris,
 *                 0x101F swinging rope (Sho grabs it: PL+4 |= 0x100, 0x060EA440 = rope)
 * Global boss-room state (stage 3): 0x060F21F6/F8 shot timers, 0x060F21FA heads alive,
 * 0x060F21FE/0x060F2200 shot-table indices, 0x060F2204/0x060F2224 mouth anim states,
 * 0x060F2244/46 mouth state, 0x060F2248/4C mouth glow scale. */
void ObjHandler_1007(void);   /* 0x0604CCCC */
void ObjHandler_1008(void);   /* 0x0604CEB4 */
void ObjHandler_1009(void);   /* 0x0604D30C */
void ObjHandler_100A(void);   /* 0x0604D83C */
void ObjHandler_100B(void);   /* 0x0604D9A4 */
void ObjHandler_100C(void);   /* 0x0604DB08 */
void ObjHandler_100D(void);   /* 0x0604DD8C */
void ObjHandler_100F(void);   /* 0x0604EB88 */
void ObjHandler_1010(void);   /* 0x0604EF58 */
void ObjHandler_1011(void);   /* 0x0604F450 */
void ObjHandler_1013(void);   /* 0x06051F34 */
void ObjHandler_1014(void);   /* 0x06052454 */
void ObjHandler_1015(void);   /* 0x06052614 */
void ObjHandler_1016(void);   /* 0x0605B4EC */
void ObjHandler_1017(void);   /* 0x0605B7C8 */
void ObjHandler_1018(void);   /* 0x0605BC62 */
void ObjHandler_1019(void);   /* 0x0605BD7C */
void ObjHandler_101A(void);   /* 0x0605BEE4 */
void ObjHandler_101B(void);   /* 0x06058F7C */
void ObjHandler_101C(void);   /* 0x06059842 */
void ObjHandler_101D(void);   /* 0x06059CAC */
void ObjHandler_101E(void);   /* 0x06059F4A */
void ObjHandler_101F(void);   /* 0x060599D4 */
/* Effects 8/9 (0x0604E180): leaves / shards thrown by 0x1009 (and 0x1012), effect +2 & 7
 * = trajectory, sprite list 0x060A9E26 (A.BIN 0x060067C2) + char 0x1C. */
void Fx_Shard(uint32_t e);
/* Projectiles 0x24 (0x06059700, 0x101B cannon ball) and 0x25 (0x0605B226, 0x101A rocks). */
void Proj_CannonBall(uint32_t e);
void Proj_Rock(uint32_t e);
/* A.BIN 0x060067C2: step a simple sprite list ({char, ox, oy, ticks, cc} x n, 0x8000 =
 * goto, 0xFFFF = end) into the sprite struct; idx/timer are u16 cells. Returns the char. */
uint16_t SprList_Step(uint32_t tbl, uint32_t idx, uint32_t timer);
#endif
