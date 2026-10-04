/* DIRECTOR movie wrapper (0x06040944-0x06040DE6), see movie.h; player backend in cpk.h. */
#include "movie.h"
#include "movie_backend.h"
#include "ram.h"
#include "sprite.h"
#include "render.h"
#include "director_int.h"
#include <stdio.h>
#include <stdlib.h>

/* 0x06040944: copy up to 25 ids; the source pointer stops advancing at the first negative
 * id, so the rest of the list is filled with it. */
void Movie_SetPlaylist(const int16_t *list, uint16_t charId, uint32_t vramOff, uint16_t flags)
{
    for (uint32_t a = MOVIE_PLAYLIST; a < MOVIE_PLAYLIST + 50; a += 2) {
        W16(a, (uint16_t)*list);
        if (RS16(a) >= 0) list++;
    }
    W16(MOVIE_CHAR, charId);
    W32(MOVIE_VRAM, vramOff);
    W16(MOVIE_FLAGS, flags);
    W32(MOVIE_STATE, 0);
    W16(MOVIE_INDEX, 0);
}

void Movie_SetPlaylistRam(uint32_t list, uint16_t charId, uint32_t vramOff, uint16_t flags)
{
    int16_t tmp[25];
    for (int i = 0; i < 25; i++) {
        tmp[i] = RS16(list);
        if (tmp[i] >= 0) list += 2;
    }
    Movie_SetPlaylist(tmp, charId, vramOff, flags);
}

/* 0x06040AE6 (and the first part of state 1): Cpk_Begin 0x06008CD6 + Snd_SetAreaMap(0x06087124)
 * (frees sound RAM for the movie PCM buffer). PC: 0x0602524C = library active. */
void Movie_Prepare(void)
{
    W16(0x0602524Cu, 1);
    Snd_SetAreaMap(0x06087124u);
}

/* Empty frame until the backend presents one (the char exists, 0x0 pixels). */
static void empty_frame(void) { Movie_PresentFrame(NULL, 0, 0); }

/* 0x06040B10: open the movie file by name (GFS); 0x06040B9C: from memory. */
static void open_file(const char *name)
{
    W16(0x0602524Eu, 0);
    empty_frame();
    if (!MovieBk_OpenFile(name)) printf("[movie] cannot open %s\n", name);
}
static void open_mem(uint32_t addr, uint32_t size)
{
    W16(0x0602524Eu, 1);
    empty_frame();
    if (!MovieBk_OpenMem(addr, size)) printf("[movie] cannot open movie at %08X\n", (unsigned)addr);
}

/* 0x060409C0: VDP2 for movies: colour mix rates (0x0602B344(0,0), (0x100,0)), shadow dirty,
 * SPCTL bit 5 (mixed palette / RGB sprites). */
static void movie_setup_vdp2(void)
{
    Scl_SetColMix(0, 0); Scl_SetColMix(0x100, 0);
    Ram_Ptr(0x0603B4ACu)[0] = (uint8_t)((Ram_Ptr(0x0603B4ACu)[0] & 0x7F) | 0x80);
    W16(0x0603B46Cu, (R16(0x0603B46Cu) & 0xFFDF) | 0x20);
    W32(MOVIE_STATE, R32(MOVIE_STATE) + 1);
}

/* 0x06040A10 */
static void movie_open_next(void)
{
    Movie_Prepare();                                   /* Cpk_Begin + Snd_SetAreaMap */
    int16_t id = RS16(MOVIE_PLAYLIST + 2u * R16(MOVIE_INDEX));
    if (id < 0x1000) {
        /* Movie_GfsInit 0x06040D0C (Res_CloseIfOpen + GFS directory) */
        Res_CloseIfOpen();
        char name[16];
        uint32_t p = R32(MOVIE_NAMES + 4u * (uint32_t)id);
        int i = 0;
        for (; i < 15 && Ram_Ptr(p + (uint32_t)i)[0]; i++) name[i] = (char)Ram_Ptr(p + (uint32_t)i)[0];
        name[i] = 0;
        open_file(name);
    } else {
        open_mem(0x00200000u, 0x00100000u);
    }
    Dir_DispOn();
    W32(MOVIE_STATE, R32(MOVIE_STATE) + 1);
}

/* 0x06040AA0 */
static void movie_play(void)
{
    if ((int16_t)Movie_Tick() >= 0) return;
    if (RS16(MOVIE_PLAYLIST + 2u * R16(MOVIE_INDEX) + 2u) < 0) {
        W32(MOVIE_STATE, 0xFFFFFFFFu);
    } else {
        W16(MOVIE_INDEX, R16(MOVIE_INDEX) + 1u);
        W32(MOVIE_STATE, 1);
    }
}

/* 0x0604097A */
void Movie_Update(void)
{
    if (RS32(MOVIE_STATE) < 0) return;
    switch (R32(MOVIE_STATE)) {
    case 0: movie_setup_vdp2(); break;
    case 1: movie_open_next(); break;
    case 2: movie_play(); break;
    default: break;
    }
}

/* 0x06040BDC */
int Movie_Tick(void)
{
    if ((int16_t)MovieBk_TaskFrame() == 0) {
        if (!(Dir_Pressed() & 0x0800) || (R16(MOVIE_FLAGS) & 2)) {
            Movie_DrawFrame();
            return 0;
        }
    }
    Movie_Close();
    return (Dir_Pressed() & 0x0800) ? -2 : -1;
}

/* 0x06040C5E */
void Movie_Close(void)
{
    if (!R16(0x0602524Cu) || RS32(MOVIE_STATE) < 0) return;
    /* 0x0602524E == 0: Cpk_DestroyGfsMovie + Movie_GfsClose 0x06040DCC, else
     * Cpk_DestroyMemMovie; then Cpk_End 0x06008CE2 */
    MovieBk_Close();
    W16(0x0602524Cu, 0);
}

/* 0x06040AD6 */
void Movie_Stop(void)
{
    Movie_Close();
    W32(MOVIE_STATE, 0xFFFFFFFFu);
}

/* 0x06040CC4 */
void Movie_DrawFrame(void)
{
    uint32_t s = SPR_WORK;
    W16(s + 6, 0); W16(s + 8, 0);
    W16(s + 2, (uint16_t)((0x140u - R32(MOVIE_FRAME_W)) >> 1));
    W16(s + 4, (uint16_t)((0xE0u - R32(MOVIE_FRAME_H)) >> 1));
    W16(s + 0x10, 0); W32(s + 0x18, 0);
    W16(s + 0x0A, 0); W16(s + 0x0C, 0); W16(s + 0x0E, 0);
    W16(s, R16(MOVIE_FRAME_CH));
    Sprite_Draw(s);
}

/* ---- backend hand-off (what Cpk_OpenMovie / the Cinepak decoder do on the Saturn: the
 * frame is an RGB VDP1 character at 0x25C04740 + *0x060DCF80, char *0x060DCF7E) ---- */
void Movie_PresentFrame(const uint16_t *rgb555, int w, int h)
{
    uint16_t chr = R16(MOVIE_CHAR);
    uint32_t off = R32(MOVIE_VRAM);
    uint32_t dst = VDP1_VRAM_BASE + 0x4740u + off;
    if (w < 0 || h < 0 || off + 0x4740u + (uint32_t)(w * h * 2) > VDP1_VRAM_SIZE) return;
    if (rgb555) {
        uint8_t *p = Ram_Ptr(dst);
        for (int i = 0; i < w * h; i++) {
            uint16_t c = (uint16_t)(rgb555[i] | 0x8000);   /* RGB dot: MSB set = opaque */
            p[2 * i] = (uint8_t)(c >> 8); p[2 * i + 1] = (uint8_t)c;
        }
        Render_InvalidateVram(dst, (size_t)(w * h * 2));
    }
    Char_Register(chr, w, h, 0x28, off);
    W32(MOVIE_FRAME_W, (uint32_t)w);
    W32(MOVIE_FRAME_H, (uint32_t)h);
    W16(MOVIE_FRAME_CH, chr);
}

void Movie_PresentFrameRGB888(const uint8_t *rgb, int w, int h)
{
    if (w <= 0 || h <= 0) { Movie_PresentFrame(NULL, 0, 0); return; }
    uint16_t *tmp = (uint16_t *)malloc((size_t)(w * h) * 2);
    if (!tmp) return;
    for (int i = 0; i < w * h; i++)
        tmp[i] = (uint16_t)((rgb[3 * i] >> 3) | (rgb[3 * i + 1] >> 3) << 5 | (rgb[3 * i + 2] >> 3) << 10);
    Movie_PresentFrame(tmp, w, h);
    free(tmp);
}
