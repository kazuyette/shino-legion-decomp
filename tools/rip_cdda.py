#!/usr/bin/env python3
"""
Rip the CD-DA music tracks of the Shinobi Legions disc image to WAV.

    python3 tools/rip_cdda.py [disc.cue] [out_dir] [--source auto|iso|cue] [--tracks 2,3]

Defaults: cue = "Shinobi Legions (USA) (DW0536)/Shinobi Legions (USA) (DW0536).cue",
out_dir = extracted/. Writes TRACK02.WAV, TRACK03.WAV (44100 Hz, stereo, s16le).

The game plays its two music files as CD-DA (docs/director.md 3.2):
    TITLE.PCM  = track 2 (title screen), ENDING.PCM = track 3 (staff roll).
Their ISO9660 directory entries (in the data track) give the exact disc LBA and
length of the audio the game asks the CD block to play. `--source auto` (default)
uses those extents when they are found and the cue only to map disc LBA -> .bin
offset; `--source cue` rips each track from its cue INDEX 01 to the next track.

Why: the DW0536 cue is inconsistent. TRACK 02 has "PREGAP 00:02:00" (pregap not
stored in the .bin, so every later .bin frame = disc LBA - 150) but TRACK 03's
INDEX 01 42:28:41 (= frame 191141) is the *disc LBA* of track 3, not its .bin
position: the ending music really starts at .bin frame 190991 (= LBA 191141 - 150),
preceded by 150 frames of digital silence (track 3's real pregap), exactly where
ENDING.PCM's extent says. Ripping from the cue alone would cut the first 2 s of
the ending music and append them to track 2. Data track 1 sector headers confirm
.bin frame == LBA there.

Raw CD-DA sectors are 2352 bytes = 588 stereo frames of 16-bit little-endian PCM.
The script checks the byte order on the output (high-frequency energy ratio of
both interpretations; a byte-swapped stream looks like white noise, ratio ~1.4).
Python 3 standard library only.
"""
import argparse
import array
import math
import os
import re
import struct
import sys
import wave

RAW = 2352
DEFAULT_CUE = "Shinobi Legions (USA) (DW0536)/Shinobi Legions (USA) (DW0536).cue"


def msf(s):
    m, sec, f = (int(x) for x in s.split(":"))
    return (m * 60 + sec) * 75 + f


def frames_to_msf(n):
    return "%02d:%02d:%02d" % (n // 4500, (n // 75) % 60, n % 75)


# --------------------------------------------------------------------------- cue

class Track:
    def __init__(self, num, mode, fname):
        self.num, self.mode, self.file = num, mode, fname
        self.indexes = {}      # index number -> frame offset inside self.file
        self.pregap = 0        # PREGAP frames (not stored in the file)
        self.postgap = 0
        self.sector = 2048 if mode == "MODE1/2048" else (2336 if mode == "MODE2/2336" else RAW)
        self.file_base = 0     # frame offset of self.file within the concatenated bins
        self.start = 0         # INDEX 01 frame inside self.file
        self.end = 0           # exclusive end frame inside self.file
        self.lba = 0           # disc LBA of INDEX 01

    @property
    def audio(self):
        return self.mode == "AUDIO"


def parse_cue(path):
    base = os.path.dirname(os.path.abspath(path))
    tracks, cur_file = [], None
    for line in open(path, encoding="latin-1"):
        line = line.strip().lstrip("﻿")
        if not line:
            continue
        m = re.match(r'FILE\s+"?(.*?)"?\s+(\S+)$', line, re.I)
        if m:
            cur_file = os.path.join(base, m.group(1))
            continue
        m = re.match(r"TRACK\s+(\d+)\s+(\S+)", line, re.I)
        if m:
            tracks.append(Track(int(m.group(1)), m.group(2).upper(), cur_file))
            continue
        m = re.match(r"INDEX\s+(\d+)\s+(\d+:\d+:\d+)", line, re.I)
        if m and tracks:
            tracks[-1].indexes[int(m.group(1))] = msf(m.group(2))
            continue
        m = re.match(r"(PREGAP|POSTGAP)\s+(\d+:\d+:\d+)", line, re.I)
        if m and tracks:
            setattr(tracks[-1], m.group(1).lower(), msf(m.group(2)))
    if not tracks:
        raise SystemExit("no TRACK in " + path)

    # Track extents inside their file, and disc LBAs (cue semantics: PREGAP /
    # POSTGAP are silence that is NOT in the file; the disc starts at LBA 0 =
    # first file frame 0 for a standard 2-second lead-in).
    file_frames, files = {}, []
    for t in tracks:
        if t.file not in file_frames:
            if not os.path.exists(t.file):
                raise SystemExit("missing image file: " + t.file)
            file_frames[t.file] = os.path.getsize(t.file) // t.sector
            files.append(t.file)
    base_frame, acc = {}, 0
    for f in files:
        base_frame[f] = acc
        acc += file_frames[f]
    shift = 0
    for i, t in enumerate(tracks):
        t.file_base = base_frame[t.file]
        t.start = t.indexes.get(1, t.indexes.get(0, 0))
        nxt = tracks[i + 1] if i + 1 < len(tracks) else None
        if nxt is not None and nxt.file == t.file:
            t.end = min(nxt.indexes.values())
        else:
            t.end = file_frames[t.file]
        shift += t.pregap
        t.lba = t.file_base + t.start + shift
        t.shift = shift          # disc LBA - concatenated-file frame, inside this track
        shift += t.postgap
    return tracks


def lba_to_file(tracks, lba):
    """Map a disc LBA to (track, file, frame-in-file) using cue semantics."""
    best = None
    for t in tracks:
        if lba >= t.lba - t.pregap:
            best = t
    if best is None:
        return None
    return best, best.file, lba - best.shift - best.file_base


# --------------------------------------------------------------------------- iso

def read_user_sector(track, lba):
    """2048-byte user data of a data-track sector (by disc LBA)."""
    frame = lba - track.shift - track.file_base
    with open(track.file, "rb") as f:
        f.seek(frame * track.sector)
        raw = f.read(track.sector)
    if track.sector == RAW:
        off = 16 if track.mode.startswith("MODE1") else 24
        return raw[off:off + 2048]
    return raw[:2048]


def iso_files(tracks):
    """Root-directory entries of the ISO9660 file system on track 1."""
    data = next((t for t in tracks if not t.audio), None)
    if data is None:
        return []
    pvd = read_user_sector(data, 16)
    if pvd[1:6] != b"CD001":
        return []
    root = pvd[156:190]
    lba, size = struct.unpack("<I", root[2:6])[0], struct.unpack("<I", root[10:14])[0]
    raw = b"".join(read_user_sector(data, lba + i) for i in range((size + 2047) // 2048))
    out, i = [], 0
    while i < len(raw):
        n = raw[i]
        if n == 0:
            i = (i // 2048 + 1) * 2048
            continue
        e = raw[i:i + n]
        name = e[33:33 + e[32]].decode("ascii", "replace").split(";")[0]
        if name not in ("\x00", "\x01"):
            out.append((name, struct.unpack("<I", e[2:6])[0], struct.unpack("<I", e[10:14])[0]))
        i += n
    return out


# --------------------------------------------------------------------------- audio

def load_pcm(path, frame, count):
    with open(path, "rb") as f:
        f.seek(frame * RAW)
        return f.read(count * RAW)


def analyse(pcm):
    """RMS, peak and high-frequency ratio rms(x[n]-x[n-1])/rms(x) for both byte orders."""
    res = {}
    for order in ("le", "be"):
        a = array.array("h")
        a.frombytes(pcm[: len(pcm) // 4 * 4])
        if (order == "be") == (sys.byteorder == "little"):
            a.byteswap()
        # left channel, decimated in blocks to keep pure-Python fast
        left = a[0::2]
        s2 = d2 = 0.0
        peak = 0
        blk = 4096
        step = max(1, len(left) // (blk * 200))       # ~200 blocks
        for b in range(0, len(left) - blk, blk * step):
            prev = left[b]
            for x in left[b + 1:b + blk]:
                s2 += x * x
                d = x - prev
                d2 += d * d
                prev = x
                if abs(x) > peak:
                    peak = abs(x)
        n = max(1, (len(range(0, len(left) - blk, blk * step))) * (blk - 1))
        rms = math.sqrt(s2 / n)
        res[order] = (rms, peak, math.sqrt(d2 / n) / rms if rms else 0.0)
    return res


def write_wav(path, pcm):
    a = array.array("h")
    a.frombytes(pcm)
    if sys.byteorder != "little":     # wave wants little-endian samples
        a.byteswap()
    with wave.open(path, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(44100)
        w.writeframes(a.tobytes())


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[1])
    ap.add_argument("cue", nargs="?", default=DEFAULT_CUE)
    ap.add_argument("out_dir", nargs="?", default="extracted")
    ap.add_argument("--source", choices=("auto", "iso", "cue"), default="auto")
    ap.add_argument("--tracks", default="2,3", help="audio track numbers (default 2,3)")
    args = ap.parse_args()

    tracks = parse_cue(args.cue)
    print("cue: %s" % args.cue)
    for t in tracks:
        print("  track %02d %-10s file frames %6d..%6d (%5.1f s)  cue LBA %6d%s" % (
            t.num, t.mode, t.start, t.end, (t.end - t.start) / 75.0, t.lba,
            "  pregap %d" % t.pregap if t.pregap else ""))

    wanted = [int(x) for x in args.tracks.split(",") if x]
    audio_tracks = [t for t in tracks if t.audio]
    # Ranges to rip: {track number: (file, start frame, frame count, label)}
    ranges = {}
    for t in audio_tracks:
        ranges[t.num] = (t.file, t.start, t.end - t.start, "cue")

    if args.source != "cue":
        first_audio_lba = min((t.lba - t.pregap for t in audio_tracks), default=None)
        entries = [e for e in iso_files(tracks) if first_audio_lba is not None and e[1] >= first_audio_lba]
        entries.sort(key=lambda e: e[1])
        if entries:
            print("ISO9660 files located in the audio area:")
        for k, (name, lba, size) in enumerate(entries):
            if k >= len(audio_tracks):
                break
            nsect = (size + 2047) // 2048          # GFS reads ceil(size / 2048) sectors
            hit = lba_to_file(tracks, lba)
            num = audio_tracks[k].num
            _, fname, frame = hit
            cue_t = audio_tracks[k]
            print("  %-12s LBA %6d, %5d sectors (%6.2f s) -> track %02d, .bin frame %6d"
                  "  (cue INDEX 01 frame %6d, diff %+d)" % (
                      name, lba, nsect, nsect / 75.0, num, frame, cue_t.start, cue_t.start - frame))
            ranges[num] = (fname, frame, nsect, name)
        if not entries and args.source == "iso":
            raise SystemExit("no ISO9660 file found in the audio area")

    os.makedirs(args.out_dir, exist_ok=True)
    ok = True
    for num in wanted:
        if num not in ranges:
            print("track %02d: not an audio track in this cue" % num)
            ok = False
            continue
        fname, start, count, label = ranges[num]
        pcm = load_pcm(fname, start, count)
        if len(pcm) != count * RAW:
            print("track %02d: short read (%d of %d bytes)" % (num, len(pcm), count * RAW))
            ok = False
        out = os.path.join(args.out_dir, "TRACK%02d.WAV" % num)
        write_wav(out, pcm)
        st = analyse(pcm)
        le, be = st["le"], st["be"]
        verdict = "OK" if le[0] > 100 and le[2] < 0.8 * be[2] else "SUSPICIOUS"
        if verdict != "OK":
            ok = False
        print("track %02d [%s]: %s  %d frames = %.2f s (%s)" % (
            num, label, out, count, count / 75.0, frames_to_msf(count)))
        print("    LE: rms %7.1f peak %5d hf-ratio %.3f | BE (swapped): rms %7.1f hf-ratio %.3f -> %s" % (
            le[0], le[1], le[2], be[0], be[2], verdict))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
