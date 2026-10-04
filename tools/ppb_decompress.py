#!/usr/bin/env python3
"""Decompress Shinobi Legions .PPB overlays (custom word-based LZ).

Format (reverse engineered 2026-10-03):
  u32 BE header: top byte 0x80 = compressed flag, low 24 bits = output size in 16-bit WORDS - 1
  (A.BIN 0x06004EE8 decodes header + 1 words; a final back-reference is written in full).
  Then repeated groups: u16 BE flag word, read MSB-first, followed by 16 u16 items.
    bit=1 -> literal word, copied as-is
    bit=0 -> back-reference word: distance = w >> 4 (in words), length = (w & 0xF) + 3 words
Usage: ppb_decompress.py IN.PPB [OUT.BIN]
"""
import sys
def decompress(d):
    hdr = int.from_bytes(d[0:4], 'big')
    if hdr >> 24 != 0x80:
        raise ValueError('not compressed (header %08x)' % hdr)
    total = (hdr & 0xFFFFFF) + 1
    out = []; p = 4
    while len(out) < total:
        f = int.from_bytes(d[p:p+2], 'big'); p += 2
        for b in range(15, -1, -1):
            if len(out) >= total: break
            w = int.from_bytes(d[p:p+2], 'big'); p += 2
            if f >> b & 1:
                out.append(w)
            else:
                dist, n = w >> 4, (w & 0xF) + 3
                if not 0 < dist <= len(out):
                    raise ValueError('bad ref %04x at word %d' % (w, len(out)))
                for _ in range(n): out.append(out[-dist])
    return b''.join(x.to_bytes(2, 'big') for x in out)
if __name__ == '__main__':
    src = sys.argv[1]; dst = sys.argv[2] if len(sys.argv) > 2 else src.rsplit('.', 1)[0] + '.dec.bin'
    data = decompress(open(src, 'rb').read())
    open(dst, 'wb').write(data); print(src, '->', dst, len(data), 'bytes')
