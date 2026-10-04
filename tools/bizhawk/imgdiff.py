#!/usr/bin/env python3
"""Compare a port render (320x224 PNG from tests/snap_compare.c) with a BizHawk screenshot
(330x240 with Mednafen's overscan border, or any size >= 320x224): finds the alignment, writes a
side-by-side + diff image and prints per-screen statistics from OUT_src.bin (which screens
produced each port pixel: bit 0..3 NBG0..3, bit 4 sprites).
BizHawk's Saturn core outputs a 5-bit channel c as c << 3 (31 -> 248), the port as c * 255 / 31:
the port image is converted to that scale first (--raw: no conversion). Colour offsets (fades) are added in 8 bits, hence the tolerance.
usage: imgdiff.py PORT.png BIZHAWK.png OUT.png [SRC.bin] [--tol 6] [--raw] [--search]
(--search: find the alignment instead of the fixed 5,8 of a 330x240 BizHawk shot)"""
import sys
import numpy as np
from PIL import Image

args = [a for a in sys.argv[1:] if not a.startswith('--')]
raw = '--raw' in sys.argv
tol = 6
if '--tol' in sys.argv: tol = int(sys.argv[sys.argv.index('--tol') + 1]); args.remove(str(tol))
port = np.asarray(Image.open(args[0]).convert('RGB')).astype(int)
if not raw: port = (np.round(port * 31 / 255) * 8).astype(int)
bh = np.asarray(Image.open(args[1]).convert('RGB')).astype(int)
H, W = port.shape[:2]
best = None
if bh.shape[:2] == (240, 330) and '--search' not in sys.argv:
    best = (0, 5, 8)           # Mednafen overscan border: 5 px left, 8 lines top
for dy in (range(0, bh.shape[0] - H + 1) if best is None else []):
    for dx in range(0, bh.shape[1] - W + 1):
        d = np.abs(bh[dy:dy + H, dx:dx + W] - port).max(axis=2)
        score = (d > 32).mean()
        if best is None or score < best[0]: best = (score, dx, dy)
_, dx, dy = best
ref = bh[dy:dy + H, dx:dx + W]
d = np.abs(ref - port).max(axis=2)
bad = d > tol
print("align %d,%d  identical(<=%d) %.2f%%  exact %.2f%%  mean |d| %.2f" % (dx, dy, tol, 100 * (1 - bad.mean()), 100 * (d == 0).mean(), d.mean()))
if len(args) > 3:
    src = np.fromfile(args[3], dtype=np.uint8).reshape(H, W)
    names = ['NBG0', 'NBG1', 'NBG2', 'NBG3', 'SPR']
    top = np.full((H, W), 5)          # topmost contributor: sprites > NBG0 > ... (approx.)
    for b in [3, 2, 1, 0, 4]:
        top[(src >> b) & 1 == 1] = b
    for b in range(6):
        m = top == b
        if m.sum() == 0: continue
        print("  %-5s pixels %6d  divergent %6d (%.1f%%)" % (names[b] if b < 5 else 'BACK', m.sum(), (bad & m).sum(), 100 * (bad & m).sum() / m.sum()))
# divergent bounding boxes by 16-line bands
rows = bad.sum(axis=1)
bands = []
for y0 in range(0, H, 16):
    n = rows[y0:y0 + 16].sum()
    if n: bands.append("%d-%d:%d" % (y0, y0 + 15, n))
print("  divergent px per 16-line band:", ' '.join(bands) if bands else 'none')
vis = np.zeros((H, W * 3, 3), dtype=np.uint8)
vis[:, :W] = port; vis[:, W:2 * W] = ref
dd = np.zeros((H, W, 3), dtype=np.uint8)
dd[..., 0] = np.where(bad, 255, (port.mean(axis=2) // 4).astype(np.uint8))
dd[..., 1] = np.where(bad, 0, (port.mean(axis=2) // 4).astype(np.uint8))
dd[..., 2] = dd[..., 1]
vis[:, 2 * W:] = dd
Image.fromarray(vis).save(args[2])
