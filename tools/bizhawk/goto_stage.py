#!/usr/bin/env python3
"""Start a new game in the real game and enter stage index N (0 = 1-1 ... 17 = 9-2) the way a
stage clear does: from the title menu (bridge/title_menu.State) GAME START is pressed, then during
DIRECTOR's stage-start scene the stage index 0x0602526A = N and 0x060252DC = 1 (entered after a
clear: start at the checkpoint, like the port's --stage) are written; movies are skipped with
Start until SHINOBI (the index is rewritten until SHINOBI's load mode 5 reads it) runs the stage (game mode 0x060DAF72 = 6), then saves bridge/stageN.State.
usage: goto_stage.py N"""
import os, sys, subprocess
HERE = os.path.dirname(os.path.abspath(__file__))
def bh(*c):
    r = subprocess.run([sys.executable, os.path.join(HERE, 'bh.py')] + list(c), capture_output=True, text=True, env=dict(os.environ, BH_TIMEOUT='170'))
    return r.stdout.split('\n')
n = int(sys.argv[1])
bh('load title_menu.State', 'press P1_Start 6', 'frames 30')
for i in range(400):
    out = bh('frames 5', 'read 0x06040000 4', 'read 0x060DAF72 2', 'read 0x0602526A 2')
    if len(out) < 4: print('bridge:', out); continue
    shinobi = out[1].strip() == '4F22D416'
    if shinobi and out[2].strip() == '0006': break
    if not shinobi or out[2].strip() in ('0000', '0003', '0004'):
        bh('write 0x0602526A %04X' % n, 'write 0x060252DC 0001')   # DIRECTOR resets it at GAME START
    if not shinobi and i % 12 == 11: bh('press P1_Start 4')
print('frame', out[0], 'stage', out[3], 'mode', out[2])
bh('save stage%d.State' % n)
