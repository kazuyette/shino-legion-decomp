#!/usr/bin/env python3
"""Diff two HWRAM dumps (real game from BizHawk vs the port stepped by tests/snap_compare.c)
over the regions that hold game state; prints every differing 32-bit word (up to --max per region).
usage: ramdiff.py REAL_hw.bin PORT_hw.bin [--max 12] [--all] [--base BEFORE_hw.bin]
--base: only report words that changed in the real game or the port since BEFORE (filters
uninitialised / unrelated memory)."""
import sys, struct
REGIONS = [
    ("globals 0x06025200", 0x06025200, 0x06025300),
    ("frame/pad 0x06014400", 0x060143E0, 0x06014460),
    ("RNG seed 0x0603A4B0 (A.BIN 0x0602DDD2)", 0x0603A4B0, 0x0603A4B4),
    ("player 0x060EA2B4", 0x060EA2B4, 0x060EA588),
    ("layer desc 0x060EA588", 0x060EA588, 0x060EA618),
    ("stage ctrl 0x060EA618", 0x060EA618, 0x060EA660),
    ("checkpoint/respawn 0x060EC658", 0x060EC650, 0x060EC65C),
    ("objects 0x060EC65C", 0x060EC65C, 0x060F23D0),
    ("projectiles 0x060F23D0", 0x060F23D0, 0x060F33D4),
    ("effects 0x060F33D4", 0x060F33D4, 0x060F3FDC),
    ("items 0x060F3FDC", 0x060F3FDC, 0x060F4CC0),
    ("stage vars 0x060F4CC0", 0x060F4CC0, 0x060F4D80),
    ("layer anim 0x060F4D80", 0x060F4D80, 0x060F6400),
    ("game mode 0x060DAF60", 0x060DAF60, 0x060DAF80),
    ("fade 0x060250E0", 0x060250E0, 0x06025140),
    ("VDP2 shadow 0x0603B46C", 0x0603B46C, 0x0603B4B0),
    ("VDP2 regs 0x0603A570", 0x0603A570, 0x0603A6F0),
    ("CRAM shadow 0x06024098", 0x06024098, 0x06025098),
    ("scroll 0x06024058", 0x06024058, 0x06024098),
    ("line table 0x0601BC58", 0x0601BC58, 0x0601C498),
]
args = [a for a in sys.argv[1:] if not a.startswith('--')]
mx = int(sys.argv[sys.argv.index('--max') + 1]) if '--max' in sys.argv else 12
if '--max' in sys.argv: args.remove(str(mx))
base = None
if '--base' in sys.argv:
    bp = sys.argv[sys.argv.index('--base') + 1]; args.remove(bp); base = open(bp, 'rb').read()
real, port = open(args[0], 'rb').read(), open(args[1], 'rb').read()
w = lambda d, a: struct.unpack('>I', d[a - 0x06000000:a - 0x06000000 + 4])[0]
tot = 0
for name, lo, hi in REGIONS:
    diffs = []
    for a in range(lo, hi, 4):
        r, p = w(real, a), w(port, a)
        if r == p: continue
        if base is not None and w(base, a) == r == p: continue
        diffs.append((a, r, p, w(base, a) if base else None))
    tot += len(diffs)
    print("%-26s %5d words differ" % (name, len(diffs)))
    for a, r, p, b in diffs[:mx]:
        print("    %08X real %08X port %08X" % (a, r, p) + (" (before %08X)" % b if b is not None else ""))
if '--all' in sys.argv:
    blocks = {}
    for a in range(0, len(real), 4):
        if real[a:a+4] != port[a:a+4]: blocks[a >> 8] = blocks.get(a >> 8, 0) + 1
    print("all HWRAM: %d differing 256-byte blocks:" % len(blocks), ' '.join("%08X:%d" % (0x06000000 + (k << 8), v) for k, v in sorted(blocks.items())[:80]))
