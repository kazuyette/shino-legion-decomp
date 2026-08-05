#include "sys.h"
#include <string.h>

void Sys_MemCopy(void *dst, const void *src, size_t len)
{
    /* Matches the traced behavior of FUN_06009370: plain byte copy. */
    memcpy(dst, src, len);
}

void Sys_MemCopyWords(void *dst, const void *src, size_t len_bytes)
{
    /* Matches the traced behavior of FUN_0602adc8: copies len_bytes/2
     * 16-bit words. memcpy(dst, src, len_bytes) is behaviorally equivalent
     * here since every traced call site passes an even length. */
    memcpy(dst, src, len_bytes);
}

void Sys_SignalStopAndWaitAck(void)
{
    /* TODO: on real hardware this coordinates master/slave SH-2 execution.
     * No-op on PC until we confirm what game-visible effect (if any) needs
     * to be reproduced. */
}
