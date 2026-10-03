#include "camera.h"
#include "ram.h"
#include "pad.h"

#define PX        RS32(0x060EA2BCu)
#define PY        RS32(0x060EA2C0u)
#define SX        0x060EA550u
#define SY        0x060EA554u
#define LEAD      0x060EA558u
#define LOCKS     R16(0x060EA638u)
#define FROZEN    (R16(0x0602529Eu) & 0x8000)
#define HELD      R16(ADDR_GAME_HELD)

static int32_t fixmul(int32_t a, int32_t b) { return (int32_t)(((int64_t)a * b) >> 16); }  /* A.BIN 0x0602DD54 */

void Camera_Update(void)
{
    /* ASSUMPTION: 0x060252A8/AC hold the camera committed last frame (the
     * VDP2 scroll commit that copies them is not traced yet). */
    int32_t dx = 0, dy;
    if (PX - RS32(0x060252A8u) < RS32(SX) + (int32_t)0xFFFA0000 && !(LOCKS & 2)) {
        W32(CAM_X, (uint32_t)(PX - RS32(SX) + 0x60000));
        dx = 0x40000;
    } else if (0x60000 + RS32(SX) < PX - RS32(0x060252A8u) && !(LOCKS & 1)) {
        W32(CAM_X, (uint32_t)(PX - RS32(SX) + (int32_t)0xFFFA0000));
        dx = (int32_t)0xFFFC0000;
    }
    if (RS32(SX) + dx < RS32(0x060EA564u)) dx = RS32(0x060EA564u) - RS32(SX);
    else if (RS32(0x060EA568u) < RS32(SX) + dx) dx = RS32(0x060EA568u) - RS32(SX);

    if (!FROZEN && !(R16(0x060EA2B6u) & 8)) {             /* look ahead in the held direction */
        if (HELD & 0x4000) W32(LEAD, 0x40000);
        if (HELD & 0x8000) W32(LEAD, 0xFFFC0000u);
    }
    if ((LOCKS & 1) && RS32(LEAD) < 0) W32(LEAD, 0);
    if ((LOCKS & 2) && RS32(LEAD) > 0) W32(LEAD, 0);
    if (!FROZEN) {
        if (RS32(LEAD) > 0 && RS32(0x060EA568u) + (int32_t)0xFFE00000 < RS32(SX)) W32(LEAD, (uint32_t)fixmul(RS32(LEAD), 0xE000));
        if (RS32(LEAD) < 0 && RS32(SX) < RS32(0x060EA564u) + 0x200000)            W32(LEAD, (uint32_t)fixmul(RS32(LEAD), 0xE000));
        int32_t a = RS32(LEAD); if (a < 0) a = -a;
        if (a < 0x800) W32(LEAD, 0);
        dx += RS32(LEAD);
    }
    if (dx > 0x40000) dx = 0x40000;
    if (dx < (int32_t)0xFFFC0000) dx = (int32_t)0xFFFC0000;
    W32(SX, (uint32_t)(RS32(SX) + dx));
    W32(CAM_X, (uint32_t)(RS32(CAM_X) - dx));

    dy = 0;
    if (PY - RS32(0x060252ACu) < RS32(SY) + (int32_t)0xFFF90000 && !(LOCKS & 8)) {
        W32(CAM_Y, (uint32_t)(PY - RS32(SY) + 0x70000));
        dy = 0x30000;
    } else if (RS32(SY) + 0x20000 < PY - RS32(0x060252ACu) && !(LOCKS & 4)) {
        W32(CAM_Y, (uint32_t)(PY - RS32(SY) + (int32_t)0xFFFE0000));
        dy = (int32_t)0xFFFD0000;
    }
    if (RS32(SY) + dy < RS32(0x060EA55Cu)) dy = RS32(0x060EA55Cu) - RS32(SY);
    else if (RS32(0x060EA560u) < RS32(SY) + dy) dy = RS32(0x060EA560u) - RS32(SY);
    if (dy > 0x30000) dy = 0x30000;
    if (dy < (int32_t)0xFFFD0000) dy = (int32_t)0xFFFD0000;
    if (R16(0x060EA430u) > 0x30 && !FROZEN) {               /* idle: look up / down */
        if ((HELD & 0x1000) && !(LOCKS & 0x208)) { dy = 0x20000; if (RS32(0x060EA56Cu) <= RS32(SY)) dy = 0; }
        if ((HELD & 0x2000) && !(LOCKS & 0x104)) { dy = (int32_t)0xFFFE0000; if (RS32(SY) <= RS32(0x060EA570u)) dy = 0; }
    }
    W32(SY, (uint32_t)(RS32(SY) + dy));
    W32(CAM_Y, (uint32_t)(RS32(CAM_Y) - dy));

    W32(0x060252A8u, R32(CAM_X));                           /* commit (assumption, see above) */
    W32(0x060252ACu, R32(CAM_Y));
}
