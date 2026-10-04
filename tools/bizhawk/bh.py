#!/usr/bin/env python3
"""Send commands to the BizHawk bridge (sl_bridge.lua) and print the reply.
usage: bh.py 'cmd1' 'cmd2' ...   (timeout via BH_TIMEOUT, default 60 s)"""
import os, sys, time
D = os.path.join(os.path.dirname(os.path.abspath(__file__)), "bridge")
cid = "%d" % (time.time() * 1000)
tmp = os.path.join(D, "cmd.tmp")
with open(tmp, "w") as f: f.write(cid + "\n" + "\n".join(sys.argv[1:]) + "\n")
os.replace(tmp, os.path.join(D, "cmd.txt"))
t0 = time.time(); to = float(os.environ.get("BH_TIMEOUT", "60"))
while time.time() - t0 < to:
    try:
        s = open(os.path.join(D, "out.txt")).read()
        if s.split("\n", 1)[0].strip() == cid:
            print(s.split("\n", 1)[1], end=""); sys.exit(0)
    except FileNotFoundError: pass
    time.sleep(0.1)
print("TIMEOUT (bridge not running?)"); sys.exit(1)
