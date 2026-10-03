/* Animation player, ported from A.BIN. */
#include "anim.h"
#include "ram.h"
#include "player_ext.h"
#include "sprite.h"

#define S16(i)      R16(st + 2u * (i))
#define SW16(i, v)  W16(st + 2u * (i), (uint32_t)(v))

/* 0x0600648C */
void Anim_Start(uint32_t st, uint32_t defTbl, uint32_t scriptTbl, int charBase, int anim)
{
    SW16(11, charBase);
    SW16(12, anim);
    W32(st + 4, R32(scriptTbl + (uint32_t)(uint16_t)anim * 4));
    W32(st + 8, defTbl);
    SW16(13, 0);
    SW16(1, 0);
    SW16(0, 0);
    SW16(14, 0);
    SW16(10, 0);
    W32(st + 16, 0);
}

/* 0x060064C0 */
void Anim_Tick(uint32_t st)
{
    if (S16(12) == 0xFFFF) return;
    if ((S16(0) & 0x80) && S16(1) < S16(14)) SW16(1, S16(14));
    uint32_t e = R32(st + 4) + (uint32_t)S16(1) * 4;
    do {
        uint16_t a = R16(e), b = R16(e + 2);
        if (a < 0xF000) {
            if (!(S16(0) & 0x8000) || b == 0) {
                if (!(R16(0x0602529Eu) & 0x8000) && !(S16(0) & 0x2000))
                    SW16(13, S16(13) + 1);            /* frozen world / paused: no tick */
                if (b < S16(13)) { e += 4; SW16(1, S16(1) + 1); SW16(13, 0); }
            }
        } else {
            switch (a) {
            case 0xF900: break;
            case 0xFA00:
                if (!(b & 0xF000)) sub_06007CDE((int16_t)b);
                else sub_06007D60(b & 0x0FFF, b >> 12);
                break;
            case 0xFB00: SW16(14, b); break;
            case 0xFC00: SW16(0, S16(0) | b); break;
            case 0xFD00: SW16(0, (S16(0) & 0xFFF0) | (b & 0xF)); break;
            case 0xFE00:
                SW16(13, 0); SW16(1, b);
                e = R32(st + 4) + (uint32_t)b * 4;
                goto next;
            case 0xFF00:
                SW16(0, S16(0) | 0x8000);             /* finished: hold the previous frame */
                e -= 8; SW16(1, S16(1) - 2); SW16(13, 0);
                break;
            }
            e += 4; SW16(1, S16(1) + 1);
        }
    next:;
    } while (R16(e) >= 0xF000 && !(S16(0) & 0x8000));

    uint16_t a = R16(e);
    if (a == 0xEFFF) {
        SW16(10, 0); W32(st + 16, 0);
        return;
    }
    uint32_t defs = R32(st + 8);
    uint32_t list = defs + R16(defs + (uint32_t)(a & 0x0FFF) * 2);
    SW16(10, R16(list));
    W32(st + 12, list + 2);
    W32(st + 16, list + 2 + (uint32_t)(int16_t)S16(10) * 8);
    uint32_t part = list + 2;
    for (uint16_t n = S16(10); n != 0; n--) {
        W16(ANIM_SPRITE + 0x00, R16(part) + S16(11));     /* char */
        W16(ANIM_SPRITE + 0x06, R16(part + 2));           /* x offset */
        W16(ANIM_SPRITE + 0x08, R16(part + 4));           /* y offset */
        W16(ANIM_SPRITE + 0x10, (R16(ANIM_SPRITE + 0x10) & 0xFFFC) | R16(part + 6));
        part += 8;
        if (S16(0) & 0x4000) break;                       /* caller draws itself */
        Sprite_Draw(ANIM_SPRITE);
    }
    SW16(0, S16(0) & 0xFF9F);
    if (a & 0x8000) SW16(0, S16(0) | 0x40);
    if (a & 0x4000) SW16(0, S16(0) | 0x20);
}
