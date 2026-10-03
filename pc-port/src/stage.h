#ifndef STAGE_H
#define STAGE_H
/* Stage loading/initialisation (SHINOBI 0x060470E6/0x06047178/0x06047128,
 * 0x0604861C + per-stage init table 0x060A8B0C). Only stage 1-1 (index 12,
 * init 0x060473B4) is wired so far. */
#define G_STAGE_INDEX 0x0602526Au
int  Stage_Load(const char *assetDir, int stageIndex);   /* 1 on success */
void Stage_SpawnPlayer(void);
#endif
