#ifndef STAGE_H
#define STAGE_H
/* Stage loading/initialisation (SHINOBI 0x060470E6/0x06047178/0x06047128,
 * 0x060472D2 -> 0x0604861C + per-stage init table 0x060A8B0C in stage_inits.c). */
#define G_STAGE_INDEX 0x0602526Au
int  Stage_Load(const char *assetDir, int stageIndex);   /* 1 on success */
void Stage_SpawnPlayer(void);

#define VDP2_SHADOW_ADDR 0x0603B46Cu   /* VDP2 register shadow block (A.BIN) */
void Vdp2_SetColorRamOffsets(int n0, int n1, int n2, int n3);
void Stage_RegisterSpriteChars(void);   /* 0x06060108 */
#endif
