/* SHINOBI.PPB entry (0x06040000) and game-mode dispatcher (0x0606B54C).
 * G_GAME_MODE 0x060DAF72: 0 start (0x0606B5EE, -> 3), 3 init screens + "loading" sprite
 * (0x0606B634), 4 loading sprite one more frame (0x0606B56C), 5 load the stage (0x0606B6EC),
 * 6 stage frames (0x0606B8FA), 7 stage-clear bonuses (0x0606BA08), 9 game over (0x0606B58C),
 * 11 back to DIRECTOR (0x0606BAA4); 1, 2, 8, 10 do nothing. */
#include "shinobi.h"
#include "boot.h"
#include "ram.h"
#include "pad.h"
#include "player.h"
#include "stage.h"
#include "camera.h"
#include "enemy.h"
#include "objects.h"
#include "fx.h"
#include "fade.h"
#include "sprite.h"
#include "resource.h"
#include "objhandlers_c.h"
#include "hud.h"
#include "player_ext.h"
#include <stdio.h>

int g_shinobiVerbose = 0;

/* 0x060400B0 (partial, see shinobi.h). The SCL/VDP2 register writes that follow are not
 * emulated; the renderer takes the layer setup from the stage descriptors. */
void Shinobi_InitVdp2(void)
{
    static const uint32_t vram[4] = { 0x25E60000u, 0x25E64000u, 0x25E68000u, 0x25E6C000u };
    for (uint32_t n = 0; n < 4; n++) {
        uint32_t c = 0x06020D08u + 0x4Cu * n;
        uint8_t *p = Ram_Ptr(c);
        p[0] = 1;                                    /* dispenbl */
        p[1] = n < 3 ? 1 : 0;                        /* charsize */
        p[2] = 0; p[3] = 0; p[4] = 0;
        p[5] = n < 3 ? 1 : 0;                        /* coltype */
        p[6] = 0; p[7] = 0; p[8] = 0;
        W32(c + 12, vram[n]); W32(c + 16, vram[n]); W32(c + 20, vram[n]); W32(c + 24, vram[n]);
    }
    for (uint32_t n = 0; n < 4; n++) {               /* 0x06024058: scroll 0,0, zoom 1.0 */
        uint32_t b = 0x06024058u + 16u * n;
        W32(b, 0); W32(b + 4, 0); W32(b + 8, 0x10000); W32(b + 12, 0x10000);
    }
    W16(0x0603A608u, 0);
    W16(0x0603A598u + 8, 0); W16(0x0603A598u + 10, 0); W16(0x0603A598u + 12, 0); W16(0x0603A598u + 14, 0);
}

/* 0x06040000 */
void Shinobi_Entry(void)
{
    switch (R16(G_DIR_MODE)) {
    case 0:
        W16(G_GAME_MODE, 0);
        W16(G_DIR_MODE, R16(G_DIR_MODE) + 1u);
        break;
    case 1: Game_ModeDispatch(); break;
    case 2: case 3: case 4: case 5: case 6: case 7:
        W16(G_DIR_MODE, R16(G_DIR_MODE) + 1u);
        break;
    default: break;
    }
}

/* 0x0606BC3E: "loading" sprite graphics (LZ 0x060DA884, list 0x060DAF46 as char 1) */
static void Loading_LoadChars(void)
{
    Lz_DecompressRam(0x060DA884u, VDP1_VRAM_BASE + 0x4740u);
    Char_RegisterList(1, 0, 0x060DAF46u);
    Pal_Copy(0, 16, 0x060DAF52u);
}

/* 0x0606BC94 */
static void Loading_Draw(void)
{
    uint32_t s = SPR_WORK;
    W16(s + 0x10, 0); W32(s + 0x18, 0);
    W16(s + 0x0C, 0); W16(s + 0x0E, 0);
    W16(s + 6, 0); W16(s + 8, 0);
    W16(s + 2, 69); W16(s + 4, 102);
    W16(s + 0x0A, 0);
    W16(s, 1);
    Sprite_Draw(s);
}

/* 0x0606B5EE */
static void Game_Start(void)
{
    W16(G_GAME_MODE, R16(G_GAME_MODE) + 1u);
    Vdp2_DisplayOff();                               /* 0x06005BB4 */
    W16(0x060F4D30u, 0xFFFF);                        /* loaded sound bank: none */
    W16(G_GAME_MODE, 3);
    W16(G_CHECKPOINT, 0);                            /* 0x0604878C */
    W16(G_RESPAWN, 0);                               /* 0x06048794 */
}

/* 0x0606B634 */
static void Game_InitScreens(void)
{
    Shinobi_InitVdp2();                              /* 0x060400B0 */
    /* 0x0602B344(0,0) / (0x100,0): colour-calc rates, not emulated */
    for (uint32_t n = 0; n < 4; n++) Ram_Ptr(0x06020D08u + 0x4Cu * n)[0] = 0;   /* NBGs off */
    W16(0x0603B49Eu, 0); W16(0x0603B4A0u, 0); W16(0x0603B4A2u, 0);           /* COAR/G/B */
    Fade_Reset();                                    /* 0x06006F56 */
    Loading_LoadChars();                             /* 0x0606BC3E */
    Loading_Draw();                                  /* 0x0606BC94 */
    Vdp2_DisplayOn();                                /* 0x06005BA0 */
    W16(G_GAME_MODE, R16(G_GAME_MODE) + 1u);
}

/* 0x0606B6EC (PC: Stage_Load stands for the file loads 0x0606BB0C / 0x060472C4 /
 * 0x06047178 and the object/enemy setup calls) */
static void Game_LoadStage(void)
{
    W16(0x0601C498u, 0);
    int16_t st = RS16(G_STAGE_INDEX);
    printf("[game] load stage index %d (checkpoint %d, respawn %d, lives %d)\n",
           st, R16(G_CHECKPOINT), R16(G_RESPAWN), RS16(LIVES));
    Res_LoadStageSoundBank(st);                                   /* 0x0606BB0C */
    W16(0x060F4D32u, (uint32_t)(st == 17 ? 3 : (st & 1) + 3));    /* stage tune */
    sub_06007E00(); sub_06007CDE(RS16(0x060F4D32u));              /* StopAll + Snd_Play */
    Shinobi_InitVdp2();                              /* 0x060400B0 */
    W32(0x06024058u, 0); W32(0x0602405Cu, 0); W32(0x06024060u, 0x10000); W32(0x06024064u, 0x10000);
    W32(SPR_ZOOM, 0x10000);
    Player_Init();
    if (!Stage_Load(Res_GetAssetDir(), st)) {
        printf("[game] stage %d not available yet: loading index 12 instead\n", st);
        W16(G_CHECKPOINT, 0);
        Stage_Load(Res_GetAssetDir(), 12);
    }
    W16(0x060F4CCEu, 0); W16(0x060F4CCCu, 0);        /* 0x0606694C */
    Stage_SpawnPlayer();
    Fade_StageIntro();                               /* 0x0606B7C6-0x0606B838 */
    W16(G_STAGE_STATE, R16(G_STAGE_STATE) & 0x7FF0);
    Vdp2_DisplayOff();                               /* 0x06005BB4 */
    W16(G_GAME_MODE, R16(G_GAME_MODE) + 1u);
}

/* 0x0606BB8A: attract demo end (1800 frames, or Start -> 0x06014408 |= 0x4000) */
static void Game_CheckDemoEnd(void)
{
    uint32_t demo = R32(ADDR_DEMO_FLAGS);
    if (!(demo & 0x8000)) return;
    if (demo & 0x2000) {
        if (R16(FADE_STATE) == 0) W16(G_STAGE_STATE, (R16(G_STAGE_STATE) & 0xFFF8) | 11);
        return;
    }
    int end = 0;
    if ((R16(0x0601C4A4u) & 0x0800) && !(R16(G_STAGE_STATE) & 0x8000)) {
        W32(ADDR_DEMO_FLAGS, R32(ADDR_DEMO_FLAGS) | 0x4000);
        end = 1;
    }
    if (R16(0x060F4CCCu) > 0x708) end = 1;
    if (!end) return;
    W16(G_STAGE_STATE, (R16(G_STAGE_STATE) & 0xFFF8) | 11);
    W32(ADDR_DEMO_FLAGS, R32(ADDR_DEMO_FLAGS) | 0x2000);
    W16(G_STAGE_STATE, R16(G_STAGE_STATE) | 0x8000);
}

/* 0x0606B8FA */
static void Game_StageFrame(void)
{
    static int last_act = -1;
    Vdp2_DisplayOn();                                /* 0x06005BA0 */
    int16_t stage_before = RS16(G_STAGE_INDEX);
    W16(0x060F4CCCu, R16(0x060F4CCCu) + 1u);         /* PC: frame counter of 0x06066958 */
    Hud_Update();                                    /* 0x06061A10 (status bar) */
    /* 0x06040B44 stage tick: Scroll_Update, 0x06046E54, Game_LatchPad1, palette,
     * Player_Tick, ..., Stage_UpdateState */
    Scroll_Update();                                 /* 0x060443D8 */
    Game_LatchPad1(P16(P_FLAGS4));                   /* 0x06040CAC */
    Player_Tick();                                   /* 0x06040B72 */
    Stage_UpdateState();                             /* 0x06046190 */
    /* PC approximation of 0x0606E440: the drop-in ends when Sho lands */
    if (R16(0x060252DCu) == 0 && !(P16(P_FLAGS2) & 0x4000)) {
        W16(0x060252DCu, 1);
        PW16(P_FLAGS4, P16(P_FLAGS4) & 0xFFEF);
    }
    if (g_shinobiVerbose && Player_Action() != last_act) {
        printf("[player] action %02X -> %02X (pad %04X)\n", last_act & 0xFF, Player_Action(), R16(ADDR_GAME_HELD));
        last_act = Player_Action();
    }
    if (RS16(G_STAGE_INDEX) != stage_before) return;
    /* invincibility cheat + debug ninjutsu select (0x0606B94C-0x0606B998) not ported */
    Obj_UpdateEnemies();                             /* 0x06048954 */
    Obj_UpdateAll();                                 /* 0x06060014 */
    Projectiles_Update();                            /* 0x0606088C */
    Effects_Update();                                /* 0x06061FA0 */
    Items_Update();                                  /* 0x06062894 */
    /* 0x0606BCD0 (per-stage frame hook 0x060B10CC + tile/palette animation 0x0606F6AC /
     * 0x0606EF9C, not the HUD) and 0x06063554 not ported */
    Game_CheckDemoEnd();                             /* 0x0606BB8A */
}

/* 0x0606BA08: bonuses for DIRECTOR's tally, then request 8 */
static void Game_StageClearBonus(void)
{
    W32(SPR_ZOOM, 0x10000);
    W32(0x060252D0u, (uint32_t)(RS16(PLAYER + P_HP) * 1000));          /* LIFE */
    if (RS16(PLAYER + P_HP) < RS16(0x060252B0u)) W32(0x060252D8u, 0);   /* NO DAMAGE */
    W32(0x060252D8u, R32(0x060252D8u) * 10000u);
    Score_Add(5000);                                 /* 0x06062B38 */
    W32(G_DIRECTOR_REQ, 8);
    W16(G_GAME_MODE, 11);
}

/* 0x0606BAA4 */
static void Game_BackToDirector(void)
{
    Load_DirectorPPB();                              /* 0x06004EAC (next frame) */
    W16(FADE_STATE, 0);
    W32(0x06025100u, 0); W32(0x06025104u, 0); W32(0x06025108u, 0);
    W16(G_DIR_MODE, 0);
}

/* 0x0606B54C */
void Game_ModeDispatch(void)
{
    uint16_t mode = R16(G_GAME_MODE);
    if (mode >= 12) return;
    switch (mode) {
    case 0: Game_Start(); break;
    case 3: Game_InitScreens(); break;
    case 4:                                          /* 0x0606B56C */
        Loading_Draw();
        W16(G_GAME_MODE, R16(G_GAME_MODE) + 1u);
        break;
    case 5: Game_LoadStage(); break;
    case 6: Game_StageFrame(); break;
    case 7: Game_StageClearBonus(); break;
    case 9:                                          /* 0x0606B58C: game over */
        W32(SPR_ZOOM, 0x10000);
        W32(G_DIRECTOR_REQ, 5);
        W16(G_GAME_MODE, 11);
        break;
    case 11: Game_BackToDirector(); break;
    default: break;
    }
}
