#ifndef SYS_H
#define SYS_H

#include <stddef.h>

/* FUN_06009370 in A.BIN — generic byte-copy loop. */
void Sys_MemCopy(void *dst, const void *src, size_t len);

/* Sys_MemCopyWords (0x0602adc8) — confirmed distinct from Sys_MemCopy:
 * 16-bit-unit copy loop, param is a BYTE count (copies len/2 words). Used
 * by Gfx_FlushDirtyRegions/Vdp2_FlushDirtyRegions to flush small dirty
 * buffers each frame. Behaviorally identical to a byte memcpy on a
 * little/big-endian-agnostic host as long as len is even (true at every
 * traced call site), so this just wraps memcpy while preserving the
 * word-count semantics for documentation/call-site fidelity. */
void Sys_MemCopyWords(void *dst, const void *src, size_t len_bytes);

/* FUN_0602ccc4 in A.BIN — signals a flag then busy-waits on two ack flags.
 * Hypothesis: master/slave SH-2 synchronization handshake. Not applicable
 * on PC (single CPU), so this is a no-op stub for now. */
void Sys_SignalStopAndWaitAck(void);

#endif
