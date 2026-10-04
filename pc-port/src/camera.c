#include "camera.h"
#include "ram.h"
#include "pad.h"
#include "fixmath.h"

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
    /* 0x060252A8/AC = camera at the start of this frame (set by Scroll_Update). */
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

}

/* 0x060470A4: layer position from the camera: FixMul(cam - 1024, rate) + offset. */
void Layer_FromCamera(int32_t cx, int32_t cy, uint32_t d)
{
    W32(d + 0, (uint32_t)(fixmul(cx + (int32_t)0xFC000000, RS16(d + 0x18) << 8) + RS32(d + 0x10)));
    W32(d + 4, (uint32_t)(fixmul(cy + (int32_t)0xFC000000, RS16(d + 0x1A) << 8) + RS32(d + 0x14)));
}

/* 0x060443D8: per-frame scroll update (camera follow + world bounds + layers). */
void Scroll_Update(void)
{
    const uint32_t D = 0x060EA588u, C = 0x060EA618u;
    W32(0x060252A8u, R32(CAM_X));
    W32(0x060252ACu, R32(CAM_Y));
    for (uint32_t n = 0; n < 3; n++) {                      /* previous layer positions */
        W32(D + n * 0x30 + 8, R32(D + n * 0x30));
        W32(D + n * 0x30 + 12, R32(D + n * 0x30 + 4));
    }
    Camera_Update();
    if (RS32(CAM_X) < RS32(C + 0)) W32(CAM_X, R32(C + 0));
    if (RS32(CAM_Y) < RS32(C + 4)) W32(CAM_Y, R32(C + 4));
    if (RS32(C + 8) + (int32_t)0xFEC00000 <= RS32(CAM_X))  W32(CAM_X, (uint32_t)(RS32(C + 8) + (int32_t)0xFEBFFFFF));
    if (RS32(C + 0xC) + (int32_t)0xFF200000 <= RS32(CAM_Y)) W32(CAM_Y, (uint32_t)(RS32(C + 0xC) + (int32_t)0xFF1FFFFF));
    if (R16(C + 0x20) & 0x80) {                             /* screen shake */
        int16_t shake = RS16(0x060CA6F0u + 2u * (R16(0x060143EAu) & 3));
        W32(CAM_Y, (uint32_t)(RS32(CAM_Y) + shake * 0x10000));
    }
    for (uint32_t n = 0; n < 3; n++)                        /* layers follow the frame-start camera */
        Layer_FromCamera(RS32(0x060252A8u), RS32(0x060252ACu), D + n * 0x30);
    /* NBG0..NBG2 scroll shadows (uploaded to SCXIN0.. by the main loop): (pos - 1024) & 0x07FFFFFF.
     * The renderers take the layer positions from the descriptors directly. */
    for (uint32_t n = 0; n < 3; n++) {
        W32(0x06024058u + 16u * n, (R32(D + n * 0x30) + 0xFC000000u) & 0x07FFFFFFu);
        W32(0x0602405Cu + 16u * n, (R32(D + n * 0x30 + 4) + 0xFC000000u) & 0x07FFFFFFu);
    }
    Zoom_Update();                                          /* 0x0604455A */
}

/* 0x0604455A: zoom of sprites (0x060250E0, around Sho's screen position 0x060250E4/E8) and of
 * NBG0/NBG1 (scroll shadow +8/+12 coordinate increments, scroll moved to keep Sho in place),
 * driven by the distance between Sho and the object in slot control+0x1E. The target zoom is
 * 1 + FixMul(closeness, *0x06014410); that factor is only changed by SHINOBI's debug menu
 * (0x06087406 / 0x060875CE), so in normal play the zoom stays 1.0 (checked against the real
 * game). Quirk kept: the "no object" test compares the sign-extended word with 0x0000FFFF, so
 * 0xFFFF never matches and slot -1 (0x060EC5E4) is read. */
void Zoom_Update(void)
{
    const uint32_t C = 0x060EA618u, P = 0x060EA2B4u;
    int32_t target, zoom;
    if (R16(C + 0x1E) == 0xFFFF) target = 0x10000;
    else {
        uint32_t o = 0x060EC65Cu + (uint32_t)(int32_t)(int16_t)(RS16(C + 0x1E) * 120);
        int32_t dx = RS32(o + 8) - RS32(P + 8), dy = RS32(o + 0xC) - RS32(P + 0xC);
        if (dx < 0) dx = -dx;
        if (dy < 0) dy = -dy;
        int32_t d = dx + dy - 0x300000;
        if (d < 0) d = 0;
        if (d > 0xC00000) d = 0xC00000;
        target = 0x10000 + FixMul(FixDiv(0xC00000 - d, 0xC00000), RS32(0x06014410u));
        if (R16(P + 4) & 0x0400) target = 0x10000;
    }
    int32_t step = target - RS32(0x060250E0u);
    if (step > 0x400) step = 0x400;
    else if (step < -0x400) step = -0x400;
    W32(0x060250E0u, (uint32_t)(RS32(0x060250E0u) + step));
    zoom = RS32(0x060250E0u);
    W32(0x060250E4u, (uint32_t)(RS32(P + 8) - RS32(CAM_X)));
    W32(0x060250E8u, (uint32_t)(RS32(P + 0xC) - RS32(CAM_Y)));
    for (uint32_t b = 0x06024058u; b <= 0x06024068u; b += 0x10) {
        int32_t zx = RS32(0x060250E4u), zy = RS32(0x060250E8u);
        W32(b, (uint32_t)(RS32(b) + (zx - FixDiv(zx, zoom))));
        W32(b + 4, (uint32_t)(RS32(b + 4) + (zy - FixDiv(zy, zoom))));
        W32(b + 8, (uint32_t)zoom);
        W32(b + 12, (uint32_t)zoom);
    }
}
