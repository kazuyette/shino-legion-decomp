#ifndef MOVIE_H
#define MOVIE_H
#include <stdint.h>
/* DIRECTOR's movie wrapper (0x06040944-0x06040DE6), ported in movie.c. The player itself
 * is behind cpk.h (D7). State in emulated RAM at the original addresses:
 *   0x060DCF48 s16[25] playlist (movie ids, < 0 = end; id >= 0x1000 = play from memory)
 *   0x060DCF7C u16 playlist index     0x060DCF7E u16 char id of the frame (0x100)
 *   0x060DCF80 u32 VDP1 VRAM offset of the frame (from 0x25C04740; 0x58800 or 0x8000)
 *   0x060DCF84 s32 state: -1 idle/finished, 0 setup, 1 open next, 2 playing
 *   0x060DCF88 u16 flags: bit 1 = Start cannot skip
 *   0x06025254 / 0x06025258 u32 frame width / height, 0x0602525C u16 frame char
 *   (written by the CPK library on real hardware, by Movie_PresentFrame here).
 * Movie ids (name table 0x060870BC): 0 OPENING, 1-10 DEMO_1..10, 11 ENDING, 12 TOUJYOU,
 * 13 CNTCOUNT, 14 CONTINUE, 15 TITLE, 16 GAMEOVER, 17-25 STGSTRT1..9. */
#define MOVIE_PLAYLIST 0x060DCF48u
#define MOVIE_INDEX    0x060DCF7Cu
#define MOVIE_CHAR     0x060DCF7Eu
#define MOVIE_VRAM     0x060DCF80u
#define MOVIE_STATE    0x060DCF84u
#define MOVIE_FLAGS    0x060DCF88u
#define MOVIE_NAMES    0x060870BCu
#define MOVIE_FRAME_W  0x06025254u
#define MOVIE_FRAME_H  0x06025258u
#define MOVIE_FRAME_CH 0x0602525Cu

/* 0x06040944: list = host array of movie ids (-1 terminated, max 25). */
void Movie_SetPlaylist(const int16_t *list, uint16_t charId, uint32_t vramOff, uint16_t flags);
/* Same with a list in emulated RAM (DIRECTOR tables such as 0x0604361E). */
void Movie_SetPlaylistRam(uint32_t list, uint16_t charId, uint32_t vramOff, uint16_t flags);
void Movie_Update(void);       /* 0x0604097A: run the playlist, call once per frame */
int  Movie_Tick(void);         /* 0x06040BDC: 0 playing, -1 ended, -2 skipped with Start */
void Movie_DrawFrame(void);    /* 0x06040CC4: current frame, centred */
void Movie_Stop(void);         /* 0x06040AD6: close, state -1 */
void Movie_Close(void);        /* 0x06040C5E */
void Movie_Prepare(void);      /* 0x06040AE6: Cpk_Begin + movie sound area map */
static inline int Movie_IsDone(void);

/* Backend -> wrapper: the decoded frame (RGB555 host words, bit 15 ignored). Copies it as
 * an RGB VDP1 character into the emulated VRAM at the playlist's offset, registers char
 * *0x060DCF7E and sets the frame size words. */
void Movie_PresentFrame(const uint16_t *rgb555, int w, int h);
void Movie_PresentFrameRGB888(const uint8_t *rgb, int w, int h);

#include "ram.h"
static inline int Movie_IsDone(void) { return RS32(MOVIE_STATE) < 0; }
#endif
