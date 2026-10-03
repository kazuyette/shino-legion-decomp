#ifndef SHINOBI_H
#define SHINOBI_H
/* SHINOBI.PPB (in-game overlay at 0x06040000): entry and top-level game modes
 * (dispatcher 0x0606B54C on G_GAME_MODE 0x060DAF72), see shinobi.c. */
void Shinobi_Entry(void);          /* 0x06040000 */
void Game_ModeDispatch(void);      /* 0x0606B54C */
/* Part of 0x060400B0 (SHINOBI VDP2 init) the PC renderer depends on: SclConfig blocks of
 * NBG0..NBG3 at 0x06020D08 (+0x4C each; byte 0 = display enable) and the 4 scroll/zoom
 * blocks at 0x06024058. Also useful for tools that load a stage directly (tests/shot*.c). */
void Shinobi_InitVdp2(void);
extern int g_shinobiVerbose;       /* PC: log player action changes */
#endif
