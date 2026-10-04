#!/usr/bin/env python3
"""Find the first frame where the port and the real game diverge (game-state regions of
ramdiff.py, upload bookkeeping ignored), between frames A and B of a lockstep.py schedule.
usage: ls_bisect.py STATE DUMPPREFIX SNAPBIN ASSETS OUTDIR SCHEDULE A B
The real game is dumped every frame from A to B (bridge/bs_<f>_hw.bin, deleted at the end).
Frames where the real game lags (a CD load: 0x0603A4A4 = 0, 0x0601ABD4 counting) are not
re-aligned: start a new transplant after such a load."""
import os, sys, subprocess, struct
HERE = os.path.dirname(os.path.abspath(__file__))
sys.argv, argv = sys.argv[:1], sys.argv
state, pre, snap, assets, out, sched, A, B = argv[1:9]
A, B = int(A), int(B)
BTN = [(0x8000, 'P1_Right'), (0x4000, 'P1_Left'), (0x2000, 'P1_Down'), (0x1000, 'P1_Up'), (0x0800, 'P1_Start'),
       (0x0400, 'P1_A'), (0x0200, 'P1_C'), (0x0100, 'P1_B'), (0x0080, 'P1_R'), (0x0040, 'P1_X'),
       (0x0020, 'P1_Y'), (0x0010, 'P1_Z'), (0x0008, 'P1_L')]
pads = []
for x in sched.split(','):
    v, n = x.split(':'); pads += [int(v, 16)] * int(n)
pad = lambda f: pads[f] if f < len(pads) else pads[-1]
cmds, cur = ['load ' + state], None
for f in range(B):
    if pad(f) != cur:
        cmds.append('release'); names = [nm for b, nm in BTN if pad(f) & b]
        if names: cmds.append('hold ' + ' '.join(names))
        cur = pad(f)
    cmds.append('frames 1')
    if f + 1 >= A:
        cmds.append('dump 0x06000000 0x100000 bs_%d_hw.bin' % (f + 1))
cmds += ['release', 'load ' + state]
r = subprocess.run([sys.executable, os.path.join(HERE, 'bh.py')] + cmds, capture_output=True, text=True, env=dict(os.environ, BH_TIMEOUT='170'))
os.makedirs(out, exist_ok=True)
subprocess.run([snap, assets, pre, os.path.join(out, 'bs'), 'step', str(B), sched, 'from=%d' % A], capture_output=True)
sys.path.insert(0, HERE)
IGN = {0x060252E8, 0x060252EC, 0x060252F0, 0x06025138, 0x0602513C, 0x06025244}   # VDP1 transfer queue; sound slot table + sound flag (A.BIN 0x06007C78..), not modelled
REG = [(0x06025200, 0x06025300), (0x060EC650, 0x060EC65C), (0x0603A4B0, 0x0603A4B4), (0x060EA2B4, 0x060EA660), (0x060EC65C, 0x060F6400), (0x060250E0, 0x06025140), (0x06024098, 0x06025098), (0x060DAF60, 0x060DAF80)]
for f in range(A, B + 1):
    real = open(os.path.join(HERE, 'bridge', 'bs_%d_hw.bin' % f), 'rb').read()
    port = open(os.path.join(out, 'bs_%d_hw.bin' % f), 'rb').read()
    diffs = [a for lo, hi in REG for a in range(lo, hi, 4) if a not in IGN and real[a - 0x06000000:a - 0x05FFFFFC] != port[a - 0x06000000:a - 0x05FFFFFC]]
    if diffs:
        print('first divergent frame', f)
        for a in diffs[:40]:
            print('  %08X real %s port %s' % (a, real[a - 0x06000000:a - 0x05FFFFFC].hex(), port[a - 0x06000000:a - 0x05FFFFFC].hex()))
        break
else: print('no divergence in', A, B)
for f in range(A, B + 1):                     # the per-frame dumps are temporary
    for p in (os.path.join(HERE, 'bridge', 'bs_%d_hw.bin' % f), os.path.join(out, 'bs_%d_hw.bin' % f)):
        try: os.remove(p)
        except OSError: pass
