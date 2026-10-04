#!/usr/bin/env python3
"""Lock-step logic check: plays the same pad schedule in the real game (BizHawk, from a save
state) and in the port (tests/snap_compare.c transplanted from a dump of that same state), then
diffs the game-state RAM at checkpoints (ramdiff.py).
usage: lockstep.py STATE DUMPPREFIX SNAPBIN ASSETS OUTDIR "HEX:count,HEX:count,..." CHECK1,CHECK2,...
  STATE       save state in bridge/ (e.g. s11_start.State) -- DUMPPREFIX = dumps of that state
  SNAPBIN     the built snap_compare; ASSETS the extracted/ directory
Pad bits are pad.h's (8000 Right 4000 Left 2000 Down 1000 Up 0800 Start 0400 A 0200 C 0100 B
0080 R 0040 X 0020 Y 0010 Z 0008 L). Checkpoint dumps: bridge/ls_<N>_hw.bin, port: OUTDIR/ls_<N>_hw.bin."""
import os, sys, subprocess
HERE = os.path.dirname(os.path.abspath(__file__))
BTN = [(0x8000, 'P1_Right'), (0x4000, 'P1_Left'), (0x2000, 'P1_Down'), (0x1000, 'P1_Up'), (0x0800, 'P1_Start'),
       (0x0400, 'P1_A'), (0x0200, 'P1_C'), (0x0100, 'P1_B'), (0x0080, 'P1_R'), (0x0040, 'P1_X'),
       (0x0020, 'P1_Y'), (0x0010, 'P1_Z'), (0x0008, 'P1_L')]
state, pre, snap, assets, out, sched, checks = sys.argv[1:8]
segs = [(int(a, 16), int(b)) for a, b in (x.split(':') for x in sched.split(','))]
checks = sorted(int(c) for c in checks.split(','))
pads = []
for v, n in segs: pads += [v] * n
cmds = ['load ' + state]
f, cur = 0, None
for c in checks:
    while f < c:
        v = pads[f] if f < len(pads) else pads[-1]
        n = 1
        while f + n < c and (pads[f + n] if f + n < len(pads) else pads[-1]) == v: n += 1
        if v != cur:
            cmds.append('release')
            names = [nm for b, nm in BTN if v & b]
            if names: cmds.append('hold ' + ' '.join(names))
            cur = v
        cmds.append('frames %d' % n)
        f += n
    cmds.append('dump 0x06000000 0x100000 ls_%d_hw.bin' % c)
    cmds.append('shot ls_%d_s.png' % c)
cmds += ['release', 'load ' + state]
r = subprocess.run([sys.executable, os.path.join(HERE, 'bh.py')] + cmds, capture_output=True, text=True,
                   env=dict(os.environ, BH_TIMEOUT='170'))
if 'ERR' in r.stdout or 'TIMEOUT' in r.stdout: print(r.stdout); sys.exit(1)
os.makedirs(out, exist_ok=True)
for c in checks:
    o = os.path.join(out, 'ls_%d' % c)
    subprocess.run([snap, assets, pre, o, 'step', str(c), sched], capture_output=True)
    print('==== frame %d' % c, flush=True)
    sys.stdout.flush()
    subprocess.run([sys.executable, os.path.join(HERE, 'ramdiff.py'), os.path.join(HERE, 'bridge', 'ls_%d_hw.bin' % c),
                    o + '_hw.bin', '--base', pre + '_hw.bin', '--max', '6'])
print('(real-game dumps: bridge/ls_<N>_hw.bin + ls_<N>_s.png; ignore 0x060252E8-F0 VDP1 transfer queue, 0x0603A5E0-600'
      ' register upload, 0x06025138/3C/0x06025244 sound slots, 0x060F21B0-E8 stack garbage of 0x06049BF0)')
