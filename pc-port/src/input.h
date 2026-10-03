#ifndef INPUT_H
#define INPUT_H
#include <stdint.h>

/* Host input -> emulated SMPC peripheral report.
 *
 * Replaces the original SMPC/PER library (A.BIN 0x0602CF00-0x0602DC00,
 * PER_GetPer @ 0x0602D076). We only provide what Pad_Update consumes:
 * per port, "is a standard digital pad connected" and the raw 16-bit data
 * word, ACTIVE-LOW exactly like the hardware (bit = 0 when pressed). */

#define INPUT_MAX_PORTS 2

typedef struct {
    int      connected;   /* standard digital pad present (record size 2) */
    uint16_t raw;         /* active-low Saturn pad word */
} InputPort;

/* Polls SDL keyboard into the port reports (call once per frame). */
void Input_Update(void);

/* Equivalent of PER_GetPer(): current report for a port. */
const InputPort *Input_GetPort(int port);

#endif
