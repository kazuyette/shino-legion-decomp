#ifndef CRT_H
#define CRT_H
/* PC port only (not in the Saturn game): optional CRT look, shinobi.ini [video] crt,
 * crt_strength, crt_mask, crt_curvature; F10 cycles off -> scanlines -> crt.
 *
 * No shaders in the SDL2 render API, so the effect is built from plain renderer passes (GPU
 * on the hardware renderers) and overlay textures computed once on the CPU, at the exact
 * output resolution (rebuilt when the window size changes):
 *   1. the 320x224 frame is scaled to the output rectangle (crt: bilinear = soft beam);
 *   2. it is added again with a colour modulation (brightness compensation, saturates the
 *      brightest pixels like an overdriven beam);
 *   3. multiplied (SDL_BLENDMODE_MOD) by the overlay: one factor per output row for the
 *      scanlines (Gaussian beam profile across each source line, box-filtered over the row,
 *      so any height works, integer zoom or not) x a phosphor mask (crt: aperture grille or
 *      slot mask, one factor per output column, 1:1 with the screen pixels = no moire).
 *      The factors are designed in linear light and gamma-encoded (x^(1/2.2));
 *   4. crt: glow = the frame averaged down to 160x112 and 80x56 (bilinear halvings), added
 *      on top, so bright areas bleed into the dark gaps (the beam widens);
 *   5. crt_curvature > 0: steps 1-4 go to an intermediate target drawn as a bent mesh
 *      (SDL_RenderGeometry, barrel shape, darker corners), the mask is applied after it.
 * Render_Present (render.c) calls Crt_Draw / Crt_Finish; the F12 screenshot stays the clean
 * 320x224 frame, Shift+F12 saves the filtered window picture (Crt_RequestOutputShot).
 * The overlay maths below is plain C (tests/test_crt.c builds it with -DCRT_NO_SDL). */
#include <stdint.h>

enum { CRT_OFF, CRT_SCANLINES, CRT_FULL, CRT_NMODES };
enum { CRT_MASK_NONE, CRT_MASK_APERTURE, CRT_MASK_SLOT, CRT_NMASKS };

const char *Crt_ModeName(int mode);     /* "off", "scanlines", "crt" */
const char *Crt_MaskName(int mask);     /* "none", "aperture", "slot" */

/* Scanline factors for the h output rows showing srcLines source lines: out[y] 0..255
 * (255 = unchanged). strength 0..100. Below 2 rows per line the gaps fade out. */
void Crt_ScanlineRows(uint8_t *out, int h, int srcLines, int strength);
/* Mask triad width in output pixels: 2 (magenta / green) below 1200 rows, else 3 (R G B). */
int  Crt_MaskPeriod(int outH);
/* Phosphor mask factors of output row y: w pixels x 3 bytes (R, G, B), 255 = unchanged. */
void Crt_MaskRow(uint8_t *rgb, int w, int y, int outH, int mask, int strength);

#ifndef CRT_NO_SDL
struct SDL_Renderer;
struct SDL_Texture;

void Crt_Init(int mode, int strength, int mask, int curvature);   /* from shinobi.ini */
int  Crt_GetMode(void);
void Crt_SetMode(int mode);
void Crt_CycleMode(void);               /* F10: off -> scanlines -> crt, with a toast */
void Crt_Toast(const char *text);       /* short message drawn over the picture (~2 s) */
/* Draws scene (320x224 target) to the window with the CRT look; the window must be the
 * current target. Returns 0 when the mode is off (the caller draws the plain copy). */
int  Crt_Draw(struct SDL_Renderer *r, struct SDL_Texture *scene);
/* After the picture, before SDL_RenderPresent: filtered screenshot if requested, toast. */
void Crt_Finish(struct SDL_Renderer *r);
void Crt_RequestOutputShot(void);       /* Shift+F12 */
void Crt_Shutdown(void);                /* frees the textures (before the renderer goes) */
#endif

#endif
