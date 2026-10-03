#ifndef RAM_H
#define RAM_H
#include <stdint.h>
#include <stddef.h>

/* Emulated Saturn Work RAM High (0x06000000-0x060FFFFF), BIG-ENDIAN like the
 * real machine. Ported code keeps the original absolute addresses, which makes
 * line-by-line checking against the disassembly trivial and lets us load the
 * original binaries (A.BIN @0x06004000, SHINOBI.dec.bin @0x06040000) so their
 * data tables are available as-is. Refactor into named structs later. */

#define HWRAM_BASE 0x06000000u
#define HWRAM_SIZE 0x00100000u

extern uint8_t g_hwram[HWRAM_SIZE];

static inline uint8_t *Ram_Ptr(uint32_t a) { return &g_hwram[(a - HWRAM_BASE) & (HWRAM_SIZE - 1)]; }
static inline uint16_t R16(uint32_t a) { const uint8_t *p = Ram_Ptr(a); return (uint16_t)(p[0] << 8 | p[1]); }
static inline int16_t  RS16(uint32_t a) { return (int16_t)R16(a); }
static inline uint32_t R32(uint32_t a) { const uint8_t *p = Ram_Ptr(a); return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static inline int32_t  RS32(uint32_t a) { return (int32_t)R32(a); }
static inline void W16(uint32_t a, uint32_t v) { uint8_t *p = Ram_Ptr(a); p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static inline void W32(uint32_t a, uint32_t v) { uint8_t *p = Ram_Ptr(a); p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static inline void OR16(uint32_t a, uint32_t m)  { W16(a, R16(a) | m); }
static inline void AND16(uint32_t a, uint32_t m) { W16(a, R16(a) & m); }

/* Loads a raw binary into HWRAM at `addr`. Returns bytes loaded, 0 on failure. */
size_t Ram_LoadImage(const char *path, uint32_t addr);

#endif
