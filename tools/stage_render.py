#!/usr/bin/env python3
"""Render a decompressed stage (.MAP.dec + .CHR.dec) layer to PNG.
Layer page layouts come from SHINOBI.dec.bin (stage init), passed as hex addr."""
import sys
from PIL import Image
SHI_BASE = 0x06040000
def load(p): return open(p, 'rb').read()
def W(b, o): return int.from_bytes(b[o:o+2], 'big')
def L(b, o): return int.from_bytes(b[o:o+4], 'big')
def palettes(m):
    pal = []
    for i in range(768):
        v = W(m, 0xC00 + 2*i)
        pal.append(((v & 31)*255//31, ((v >> 5) & 31)*255//31, ((v >> 10) & 31)*255//31))
    return pal
def tile_pixels(c, layer, pn):
    """16x16 tile = 4 8x8 cells (TL,TR,BL,BR), 8bpp, 256 bytes."""
    base = layer*0x20000 + (pn & 0x3FF)*128
    out = [[0]*16 for _ in range(16)]
    for cell in range(4):
        cx, cy = (cell & 1)*8, (cell >> 1)*8
        for y in range(8):
            for x in range(8):
                i = base + cell*64 + y*8 + x
                out[cy+y][cx+x] = c[i] if i < len(c) else 0
    if pn & 0x8000: out = [row[::-1] for row in out]        # H flip
    if pn & 0x0800: out = out[::-1]                          # V flip (guess)
    return out
def render_layer(m, c, shi, layer, layout_addr, scale=1):
    pal = palettes(m)
    pages = 0x1220 + L(m, 0x1200 + 4*layer)
    o = layout_addr - SHI_BASE
    pw, ph = shi[o], shi[o+1]; ids = shi[o+2:o+2+pw*ph]
    img = Image.new('RGBA', (pw*512, ph*512), (0, 0, 0, 0)); px = img.load()
    cache = {}
    for pr in range(ph):
        for pc in range(pw):
            pid = ids[pr*pw+pc]
            if pid == 0xFF: continue
            for ty in range(32):
                for tx in range(32):
                    pn = W(m, pages + pid*0x800 + 2*(ty*32+tx))
                    if (pn & 0x3FF) == 0 and not pn: continue
                    key = pn & 0x8FFF | (pn & 0x7000)
                    t = cache.get(pn)
                    if t is None: t = cache[pn] = tile_pixels(c, layer, pn)
                    bank = (pn >> 12) & 7
                    for y in range(16):
                        row = t[y]
                        for x in range(16):
                            v = row[x]
                            if v: px[pc*512+tx*16+x, pr*512+ty*16+y] = pal[(bank % 3)*256 + v] + (255,)
    return img
if __name__ == '__main__':
    m, c, shi = load(sys.argv[1]), load(sys.argv[2]), load(sys.argv[3])
    layer, layout = int(sys.argv[4]), int(sys.argv[5], 16)
    render_layer(m, c, shi, layer, layout).save(sys.argv[6])
