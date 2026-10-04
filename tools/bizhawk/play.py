#!/usr/bin/env python3
"""Advance the real game with a simple scripted player (walk right, hop, slash, shuriken), like
tests/audit_stages.c. usage: play.py FRAMES [--hp] [--dir L]   (--hp: refill Sho's HP every
48 frames by writing 0x060EA2CA = HP max 0x060252B0 -- traversal only, never during a check)"""
import os, sys, subprocess
HERE = os.path.dirname(os.path.abspath(__file__))
n = int(sys.argv[1]); hp = '--hp' in sys.argv
d = 'P1_Left' if '--dir' in sys.argv and sys.argv[sys.argv.index('--dir') + 1] == 'L' else 'P1_Right'
cmds = []
for t in range(0, n, 48):
    cmds += ['release', 'hold %s P1_C' % d, 'frames 14', 'release', 'hold %s' % d, 'frames 6',
             'hold P1_B', 'frames 2', 'release P1_B', 'frames 8', 'hold P1_B', 'frames 2', 'release P1_B', 'frames 16']
    if hp: cmds += ['read 0x060252B0 2']
cmds += ['release', 'read 0x060EA2BC 4', 'read 0x060EA2C0 4', 'read 0x0602526A 2', 'read 0x060DAF72 2', 'read 0x060EA2CA 2']
if not hp:
    r = subprocess.run([sys.executable, os.path.join(HERE, 'bh.py')] + cmds, capture_output=True, text=True, env=dict(os.environ, BH_TIMEOUT='170'))
    print(' '.join(r.stdout.split()[-6:])); sys.exit(0)
# with HP refill: run chunk by chunk
out = []
for t in range(0, n, 48):
    c = ['release', 'hold %s P1_C' % d, 'frames 14', 'release', 'hold %s' % d, 'frames 6', 'hold P1_B', 'frames 2',
         'release P1_B', 'frames 8', 'hold P1_B', 'frames 2', 'release P1_B', 'frames 16', 'read 0x060252B0 2']
    r = subprocess.run([sys.executable, os.path.join(HERE, 'bh.py')] + c, capture_output=True, text=True, env=dict(os.environ, BH_TIMEOUT='170'))
    hpmax = r.stdout.split()[-1]
    subprocess.run([sys.executable, os.path.join(HERE, 'bh.py'), 'write 0x060EA2CA ' + hpmax], capture_output=True)
r = subprocess.run([sys.executable, os.path.join(HERE, 'bh.py'), 'release', 'read 0x060EA2BC 4', 'read 0x060EA2C0 4',
                    'read 0x0602526A 2', 'read 0x060DAF72 2', 'read 0x060EA2CA 2'], capture_output=True, text=True)
print(' '.join(r.stdout.split()))
