#ifndef SYS_H
#define SYS_H

#include <stddef.h>

/* FUN_06009370 in A.BIN — generic byte-copy loop. */
void Sys_MemCopy(void *dst, const void *src, size_t len);

/* FUN_0602ccc4 in A.BIN — signals a flag then busy-waits on two ack flags.
 * Hypothesis: master/slave SH-2 synchronization handshake. Not applicable
 * on PC (single CPU), so this is a no-op stub for now. */
void Sys_SignalStopAndWaitAck(void);

#endif
