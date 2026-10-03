#ifndef FIXMATH_H
#define FIXMATH_H
#include <stdint.h>
#include "ram.h"

/* 16.16 fixed-point helpers, ported from A.BIN. */

/* 0x0602DD54: dmuls.l + xtrct -> bits 47..16 of the 64-bit product. */
static inline int32_t FixMul(int32_t a, int32_t b) { return (int32_t)(((int64_t)a * b) >> 16); }

/* 0x0602DD68: (a << 16) / b on the SH-2 DIVU (64/32 signed). On overflow or
 * division by zero the hardware saturates to 0x7FFFFFFF / 0x80000000. */
static inline int32_t FixDiv(int32_t a, int32_t b)
{
    int64_t n = (int64_t)a * 65536;
    if (b == 0) return n < 0 ? (int32_t)0x80000000 : 0x7FFFFFFF;
    int64_t q = n / b;
    if (q > 0x7FFFFFFF) return 0x7FFFFFFF;
    if (q < -(int64_t)0x80000000) return (int32_t)0x80000000;
    return (int32_t)q;
}

/* 0x0600621C: sine of an 8-bit angle (256 per turn) as 16.16, quarter-wave
 * table at A.BIN 0x0600EDF8 (65 entries). */
#define SIN_TABLE 0x0600EDF8u
static inline int32_t Sin8(int a)
{
    int neg = 0;
    a &= 0xFF;
    if (a >= 0xC0)      { neg = 1; a = 0x100 - a; }
    else if (a == 0x80) { a = 0; }
    else if (a > 0x80)  { neg = 1; a -= 0x80; }
    else if (a > 0x40)  { a = 0x80 - a; }
    int32_t v = RS32(SIN_TABLE + (uint32_t)a * 4);
    return neg ? -v : v;
}
/* 0x06006286 */
static inline int32_t Cos8(int a) { return Sin8(a + 0x40); }
#endif
