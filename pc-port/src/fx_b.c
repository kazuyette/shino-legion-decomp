/* Effect types 0x0A-0x0D (SHINOBI 0x06062670), ported from SHINOBI.PPB: small sparkles
 * (anims 5..8 of the common effect sprites 0x060ACB20 / scripts 0x060D2FE8, chars 0x4B0+,
 * colour bank 0x75), drawn while within 256 px of the screen centre, gone when the animation
 * ends (it does not advance off screen). The original keeps the current effect in 0x060F3FD4;
 * here it is passed in. The draw priority (+0x18) is left as the previous sprite set it, as
 * in the original. */
#include "fx.h"
#include "ram.h"
#include "anim.h"
#include "sprite.h"

#define CAM_X  0x060252A0u
#define CAM_Y  0x060252A4u
#define SPR    0x060250ACu

void Fx_Sparkle(uint32_t e)                                            /* 0x06062670: 0xA-0xD */
{
    if (R16(e + 2) & 1) {
        uint16_t t = R16(e);
        int anim = 5;
        if (t == 0xB) anim = 6;
        if (t == 0xC) anim = 7;
        if (t == 0xD) anim = 8;
        Anim_Start(e + 0x10, 0x060ACB20u, 0x060D2FE8u, 0x4B0, anim);
        AND16(e + 2, 0xFFFE);
    }
    int32_t cx = (int32_t)R32(CAM_X), cy = (int32_t)R32(CAM_Y);
    int32_t d = (int32_t)R32(e + 8) - cx + (int32_t)0xFF600000;
    if (d < 0) d = cx - (int32_t)R32(e + 8) + 0xA00000;
    if (d >= 0x1000001) return;
    d = (int32_t)R32(e + 0xC) - cy + (int32_t)0xFF600000;
    if (d < 0) d = cy - (int32_t)R32(e + 0xC) + 0xA00000;
    if (d >= 0x1000001) return;
    W16(SPR + 2, (R32(e + 8) - (uint32_t)cx) >> 16);
    W16(SPR + 4, (R32(e + 0xC) - (uint32_t)cy) >> 16);
    W16(SPR + 0x10, 0);
    W16(SPR + 0x0A, 0x75);
    W16(SPR + 0x0C, 0);
    W16(SPR + 0x0E, 0);
    Anim_Tick(e + 0x10);
    if (R16(e + 0x10) & 0x8000) W16(e, 0xFFFF);
}
