#!/usr/bin/env python3
"""
Extract the ISO9660 data track (track 1, MODE1/2352) from the Shinobi Legions
Saturn .bin/.cue image into a plain .iso, and list its contents.

Usage:
    python3 extract_disc.py <path-to-.bin> <path-to-.cue> [out.iso]

There's no bchunk/xorriso available in some environments, so this strips the
16-byte sync+header and trailing ECC/EDC from each 2352-byte raw sector,
keeping only the 2048-byte user data payload. It uses the .cue file's track 2
PREGAP/INDEX to know where track 1 (the data track) ends in the .bin.
"""
import re
import sys
import numpy as np


def msf_to_frames(m, s, f):
    return (m * 60 + s) * 75 + f


def find_track1_end_frames(cue_path):
    text = open(cue_path, encoding="ascii", errors="replace").read()
    m = re.search(r"TRACK 02.*?(?=TRACK 03|$)", text, re.S)
    if not m:
        raise ValueError("Could not find TRACK 02 in cue file")
    block = m.group(0)
    idx01 = re.search(r"INDEX 01 (\d+):(\d+):(\d+)", block)
    if not idx01:
        raise ValueError("Could not find TRACK 02 INDEX 01")
    return msf_to_frames(*map(int, idx01.groups()))


def extract(bin_path, cue_path, out_path):
    sector_count = find_track1_end_frames(cue_path)
    SECTOR = 2352
    print(f"Track 1: {sector_count} sectors ({sector_count * 2048} bytes of user data)")
    with open(bin_path, "rb") as fin, open(out_path, "wb") as fout:
        CHUNK = 20000
        remaining = sector_count
        while remaining > 0:
            n = min(CHUNK, remaining)
            raw = fin.read(n * SECTOR)
            arr = np.frombuffer(raw, dtype=np.uint8).reshape(n, SECTOR)
            fout.write(arr[:, 16:16 + 2048].tobytes())
            remaining -= n
    print(f"Wrote {out_path}")


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    bin_path, cue_path = sys.argv[1], sys.argv[2]
    out_path = sys.argv[3] if len(sys.argv) > 3 else "track1.iso"
    extract(bin_path, cue_path, out_path)
