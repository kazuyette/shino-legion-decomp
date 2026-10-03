#!/usr/bin/env python3
"""Minimal ISO9660 root-directory reader (no pycdlib needed).
Usage: iso_extract.py ISO NAME [OUT]   -> extracts one file (e.g. TITLE.PBC)
       iso_extract.py ISO              -> lists files (lba, size, name)
Sizes > 2 GB are wrapped in the Saturn image's directory (ENDING.PCM/TITLE.PCM
show negative sizes in file_listing.txt); they are read as unsigned u32."""
import sys
def entries(iso):
    f = open(iso, 'rb')
    f.seek(16 * 2048); pvd = f.read(2048)
    root = pvd[156:156 + 34]
    lba = int.from_bytes(root[2:6], 'little'); size = int.from_bytes(root[10:14], 'little')
    f.seek(lba * 2048); d = f.read(size)
    p = 0
    while p < len(d):
        n = d[p]
        if n == 0:
            p = (p // 2048 + 1) * 2048; continue
        e = d[p:p + n]
        l = int.from_bytes(e[2:6], 'little'); s = int.from_bytes(e[10:14], 'little')
        name = e[33:33 + e[32]].decode('ascii', 'replace').split(';')[0]
        if name not in ('\x00', '\x01'):
            yield name, l, s
        p += n
def read(iso, name):
    for n, l, s in entries(iso):
        if n == name:
            f = open(iso, 'rb'); f.seek(l * 2048); return f.read(s)
    raise KeyError(name)
if __name__ == '__main__':
    if len(sys.argv) == 2:
        for n, l, s in entries(sys.argv[1]): print('%8d %10d %s' % (l, s, n))
    else:
        data = read(sys.argv[1], sys.argv[2])
        out = sys.argv[3] if len(sys.argv) > 3 else sys.argv[2]
        open(out, 'wb').write(data); print(sys.argv[2], len(data), 'bytes ->', out)
