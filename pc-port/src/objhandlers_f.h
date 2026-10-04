#ifndef OBJHANDLERS_F_H
#define OBJHANDLERS_F_H
#include <stdint.h>
/* Stage objects 0x1020-0x1050 (except 0x1025-0x1028, objhandlers_d.c), ported in
 * objhandlers_f.c (6-1, 3-1, 1-2 + shared helpers), objhandlers_f2.c (4-1, 6-2) and
 * objhandlers_f3.c (5-1, 5-2, 8-1, 9-1). Field use is described next to each handler.
 * Game stage = stage index / 2 + 1 (index 0 = file STAGE7_1 = game 1-1). */
void ObjHandler_1020(void);   /* 0x06059DE0  6-1 background decoration */
void ObjHandler_1021(void);   /* 0x06050884  3-1 stone / hanging lanterns (glass / lid / base) */
void ObjHandler_1022(void);   /* 0x060516BC  3-1 wall eye (end of the stage) */
void ObjHandler_1023(void);   /* 0x06051B9C  3-1 sliding wall blocks */
void ObjHandler_1024(void);   /* 0x06051458  3-1 glass shards */
void ObjHandler_1031(void);   /* 0x0605C85C  1-2 swinging bell */
void ObjHandler_1032(void);   /* 0x0605CA4C  1-2 hanging sign (cut by the sword) */
void ObjHandler_1033(void);   /* 0x0605CE4A  1-2 clouds */
void ObjHandler_1034(void);   /* 0x0605CF64  1-2 petals (generator + petal) */

/* objhandlers_f2.c: 4-1 */
void ObjHandler_1029(void);   /* 0x06052ECC  hanging lantern (cut: top falls, base) */
void ObjHandler_102A(void);   /* 0x06055A1C  platform hanging from a 0x102B rope */
void ObjHandler_102B(void);   /* 0x06055090  rope / chain (lowers, sways, cut by the sword) */
void ObjHandler_102C(void);   /* 0x06054CA4  floor switch of the flags-8 lift */
void ObjHandler_102D(void);   /* 0x060546FC  lift */
void ObjHandler_102E(void);   /* 0x06053FF4  row of 4 spears / pistons */
void ObjHandler_102F(void);   /* 0x06053A28  sliding door releasing an enemy */
void ObjHandler_1030(void);   /* 0x06053470  paper screen (cut / enemy bursting out) */
void ObjHandler_1037(void);   /* 0x06055E00  falling rope segment */
/* objhandlers_f3.c: 5-1, 5-2 */
void ObjHandler_103D(void);   /* 0x06057464  mine cart */
void ObjHandler_103E(void);   /* 0x06058238  explosive barrels */
void ObjHandler_103F(void);   /* 0x06058444  5-1 controller (items, cart carry, warning sign) */
void ObjHandler_1040(void);   /* 0x060570FC  water surface */
void ObjHandler_1041(void);   /* 0x06057298  water splash */
void ObjHandler_1042(void);   /* 0x06056F34  far background element (+ palette cycle) */
void ObjHandler_1043(void);   /* 0x06058774  5-2 elevator */
void ObjHandler_1044(void);   /* 0x06058D38  breakable crate */
/* objhandlers_f4.c: 6-2, 8-1, 9-1, strays */
void ObjHandler_1038(void);   /* 0x0605ACF0  crystal (fragments) */
void ObjHandler_1039(void);   /* 0x0605A224  wall collapsing into rubble */
void ObjHandler_103A(void);   /* 0x0605A714  debris (enemy code) */
void ObjHandler_103B(void);   /* 0x0605AB42  rubble piece */
void ObjHandler_103C(void);   /* 0x0605E40A  shard (projectile / enemy code) */
void ObjHandler_1045(void);   /* 0x0605D234  scenery block */
void ObjHandler_1046(void);   /* 0x0605D3B0  crumbling ledge */
void ObjHandler_1047(void);   /* 0x0605D668  breakable post */
void ObjHandler_1048(void);   /* 0x0605D928  falling rock */
void ObjHandler_1049(void);   /* 0x0605DCF8  small rubble */
void ObjHandler_104B(void);   /* 0x06052C4C  leaf of the 2-1 plants (0x1013) */
void ObjHandler_104C(void);   /* 0x0605F520  9-1 boss orb */
void ObjHandler_104D(void);   /* 0x0605FAA8  flash (stage hook) */
void ObjHandler_104E(void);   /* 0x0605FC38  rising ember (enemy code) */
void ObjHandler_104F(void);   /* 0x0605FEC4  spark (enemy code) */
void ObjHandler_1035(void);   /* 0x0605E81C  9-1 sinking platform */
void ObjHandler_1036(void);   /* 0x0605EB8C  9-1 breakable urn */
void ObjHandler_1050(void);   /* 0x0605ED0C  9-1 scenery */
/* Spawn helpers called by enemy / projectile code (return the slot or -1). */
int  ObjF_RubbleSpawn(int32_t x, int32_t y, uint16_t part, uint16_t kind, uint16_t mirror); /* 0x0605AA34 */
int  ObjF_DebrisSpawn(int32_t x, int32_t y, uint16_t flags, uint16_t set, uint16_t mirror); /* 0x0605A688 */
void ObjF_RockfallSpawn(int32_t x, int32_t y);                                           /* 0x0605D838 */
void ObjF_Rubble2Spawn(int32_t x, int32_t y, int16_t kind);                              /* 0x0605DBEC */
int  ObjF_ShardSpawn(int32_t dx, int32_t dy);                                            /* 0x0605E76C */
/* Dispatch for the types above (ObjHandler_Call): 1 if handled. */
int  ObjHandlerF_Call(uint16_t type);

/* 0x06051432: add an enemy slot to the list carried by the 0x1021 lanterns (enemy code). */
void ObjF_CarryListAdd(int16_t slot);
/* Stage-hook state setters (block 0x060F4D40) called by the objects. */
void Hook_0606D774(void);
void Hook_0606D77C(void);
void Hook_0606D7AE(void);
int  Hook_0606D7C8(void);
void Hook_0606E154(void);
void Hook_0606E184(void);
void Hook_0606E1B8(void);
#endif
