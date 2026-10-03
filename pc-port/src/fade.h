#ifndef FADE_H
#define FADE_H
#include <stdint.h>

/* Screen fade system of A.BIN (0x06006EE8-0x06007BA8), ported in fade.c.
 *
 * The Saturn fade is done ONLY with the VDP2 colour offset function: no CRAM
 * rewrite. Every frame the sequencer moves three 16.16 "current offset" values
 * (R, G, B, 9-bit signed encoding: 0x000-0x0FF = 0..+255, 0x100-0x1FF = -256..-1)
 * towards a target and copies their integer part into the VDP2 register shadow
 * (A.BIN 0x0603B46C block, which skips the reserved register 0xFE):
 *   0x0603B49A CLOFEN (0x25F80110)  bit n = colour offset on for N0,N1,N2,N3,R0,BACK,SPR
 *   0x0603B49C CLOFSL (0x25F80112)  bit n = layer uses offset B (fade clears it: all A)
 *   0x0603B49E/A0/A2 COAR/COAG/COAB (0x25F80114-118), 9-bit signed
 *   0x0603B4A4/A6/A8 COBR/COBG/COBB (0x25F8011A-11E)
 *   0x0603B4AC bit 26: "shadow changed" flag for the upload (0x06009254(1, 0x0501)).
 * Final pixel = clamp(colour8 + offset, 0, 255) per channel (colour8 = 5-bit << 3).
 *
 * RAM (original addresses kept, see ram.h):
 *   0x060250EC/F0/F4 s32  per-channel speed weight (16.16, default 1.0)
 *   0x060250F8/FA/FC u16  fade-out target (9-bit, default 0x100 = -256 = black)
 *   0x06025100/04/08 s32  current offset (16.16, 9-bit encoding)
 *   0x0602510C u16  FADE_STATE: 0 idle, 1 fading in (to 0), 2 fading out (to target)
 *   0x06025110 s32  default speed (units/frame 16.16, reset 10.0)
 *   0x06025114 u8   queued next state (taken when the current fade ends)
 *   0x06025116 u16  CLOFEN "disable" mask applied when a fade ends
 *   0x06025118/1E   flash colours A/B (3 x u16 each), 0x06025124 flash layer mask,
 *   0x06025126 u16  flash flags (1 armed this frame, 2 phase, 4 skip one frame),
 *   0x06025128 s32  flash speed
 *   0x0602512C s32  speed of the running timed fade, 0x06025130 u16 timed fades left,
 *   0x06025132 u16  pending timed-fade count, 0x06025134 s32 pending timed speed. */

#define FADE_STATE 0x0602510Cu

void Fade_Reset(void);                                   /* 0x06006F56 */
void Fade_Init(void);                                    /* 0x0600766C */
void Fade_Update(void);                                  /* 0x06007084: per-frame sequencer */
int  Fade_StartIn(void);                                 /* 0x06006FB8 */
int  Fade_StartOut(void);                                /* 0x06006FDE */
int  Fade_RequestIn(void);                               /* 0x06007018 */
int  Fade_RequestOut(void);                              /* 0x0600704A */
void Fade_Complete(void);                                /* 0x060075CA */
void Fade_SetWeights(int32_t r, int32_t g, int32_t b);   /* 0x06006F20 */
void Fade_SetTarget(uint16_t r, uint16_t g, uint16_t b); /* 0x06006F48 */
void Fade_SetDefaultSpeed(int32_t speed);                /* 0x06007234 */
void Fade_SetEndLayerMask(uint16_t mask);                /* 0x0600725C */
int  Fade_SetNextSpeed(int32_t speed, uint16_t count);   /* 0x0600727A */
int  Fade_SetupDuration(uint16_t frames, uint16_t mode, uint16_t count);   /* 0x060072AC */
void Fade_SetLayerMask(uint16_t mask);                   /* 0x060077A4 */
void Fade_WriteOffsets(void);                            /* 0x060078EC */
int  Fade_StepIn(int32_t speed);                         /* 0x06007960 */
int  Fade_StepToTarget(int32_t speed, const uint16_t target[3]);   /* 0x06007A5E */
void Fade_SetFlash(int32_t speed, uint16_t mask, const uint16_t a[3], const uint16_t b[3]);   /* 0x06006EE8 */
void Fade_HoldFlash(void);                               /* 0x06006F16 */

/* Inline sequence of SHINOBI Game_LoadStage 0x0606B7C6-0x0606B838: reset, start
 * fully black, 16-frame fade-in. */
void Fade_StageIntro(void);
/* SHINOBI Stage_UpdateState 0x060461DA-0x060461FA: 16-frame fade-out to black. */
void Fade_StageOutro(void);

/* PC renderer accessors (pure: read the VDP2 shadow only).
 * Offsets are signed, -256..255 per channel (VDP2 colour offset A).
 * enabledMask = CLOFEN bits: 1 NBG0, 2 NBG1, 4 NBG2, 8 NBG3, 0x10 RBG0, 0x20 BACK, 0x40 SPRITE. */
void Fade_GetColorOffset(int *r, int *g, int *b, int *enabledMask);
/* Offset actually applied to one layer (0..6 as above), honouring CLOFEN and CLOFSL
 * (A or B). Returns 0 and zeros when the layer has no offset. */
int  Fade_GetLayerOffset(int layer, int *r, int *g, int *b);
#endif
