#!/usr/bin/env python3
"""Build the player package dist/ShinobiLegionsPC-<version>-win64.zip (no game data).

    python3 tools/make_dist.py --exe build/shinobi_legions_pc.exe --sdl SDL2.dll [--out DIR]
                               [--version X.Y.Z] [--dry-run]
    python3 tools/make_dist.py --self-test

Contents (in a ShinobiLegionsPC-<version>-win64/ folder inside the zip):
    shinobi_legions_pc.exe, SDL2.dll,
    README.txt   (pc-port/packaging/README.txt: getting the game files from your own disc,
                  controls, F1 debug menu, shinobi.ini - French + English),
    NOTICE.txt   (no game data included, SEGA / SDL2 notices),
    CHANGELOG.txt (pc-port/CHANGELOG.md).
The version defaults to project(... VERSION x.y.z) in pc-port/CMakeLists.txt. Text files get
CRLF line endings. The script refuses anything that looks like game data, so the zip can be
published: the player builds assets/ with `shinobi_legions_pc.exe --extract DISC.cue` (or by
dropping the .cue on the exe). Default output: pc-port/dist/ (ignored by git).
--self-test builds a package from dummy files in a temporary folder and checks it.
Standard library only (works with the Windows Python, no zip tool needed).
"""
import argparse
import os
import re
import shutil
import sys
import tempfile
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PORT = os.path.join(ROOT, "pc-port")
GAME_DATA = re.compile(r"\.(bin|cue|iso|ppb|pbc|cpk|chr|map|spr|snd|pcm|wav)$", re.I)


def cmake_version():
    text = open(os.path.join(PORT, "CMakeLists.txt"), encoding="utf-8").read()
    m = re.search(r"project\([^)]*VERSION\s+(\d+\.\d+\.\d+)", text)
    if not m:
        raise SystemExit("version not found in pc-port/CMakeLists.txt")
    return m.group(1)


def text_file(path, version):
    s = open(path, encoding="utf-8").read().replace("@VERSION@", version)
    s = s.replace("\r\n", "\n").replace("\n", "\r\n")
    return s.encode("utf-8")


def check_pe(path, what):
    if not os.path.isfile(path):
        raise SystemExit("%s not found: %s" % (what, path))
    with open(path, "rb") as f:
        if f.read(2) != b"MZ":
            raise SystemExit("%s is not a Windows executable/DLL: %s" % (what, path))


def build(exe, sdl, out_dir, version, dry_run=False):
    check_pe(exe, "--exe")
    check_pe(sdl, "--sdl")
    name = "ShinobiLegionsPC-%s-win64" % version
    entries = [
        ("shinobi_legions_pc.exe", open(exe, "rb").read()),
        ("SDL2.dll", open(sdl, "rb").read()),
        ("README.txt", text_file(os.path.join(PORT, "packaging", "README.txt"), version)),
        ("NOTICE.txt", text_file(os.path.join(PORT, "packaging", "NOTICE.txt"), version)),
        ("CHANGELOG.txt", text_file(os.path.join(PORT, "CHANGELOG.md"), version)),
    ]
    for n, _ in entries:                      # never ship game data
        if GAME_DATA.search(n):
            raise SystemExit("refusing to package game data: " + n)
    zip_path = os.path.join(out_dir, name + ".zip")
    for n, data in entries:
        print("  %-24s %9d bytes" % (name + "/" + n, len(data)))
    if dry_run:
        print("dry run: would write", zip_path)
        return zip_path
    os.makedirs(out_dir, exist_ok=True)
    tmp = zip_path + ".part"
    with zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for n, data in entries:
            info = zipfile.ZipInfo(name + "/" + n, date_time=(2026, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            z.writestr(info, data)
    os.replace(tmp, zip_path)
    print("wrote %s (%d bytes)" % (zip_path, os.path.getsize(zip_path)))
    return zip_path


def self_test():
    version = cmake_version()
    tmp = tempfile.mkdtemp(prefix="sl_dist_")
    try:
        exe, dll = os.path.join(tmp, "dummy.exe"), os.path.join(tmp, "SDL2.dll")
        open(exe, "wb").write(b"MZ" + b"\0" * 1000)
        open(dll, "wb").write(b"MZ" + b"\1" * 500)
        build(exe, dll, tmp, version, dry_run=True)
        z = build(exe, dll, os.path.join(tmp, "dist"), version)
        assert os.path.basename(z) == "ShinobiLegionsPC-%s-win64.zip" % version
        with zipfile.ZipFile(z) as f:
            names = sorted(f.namelist())
            assert f.testzip() is None
            readme = f.read("ShinobiLegionsPC-%s-win64/README.txt" % version).decode("utf-8")
            notice = f.read("ShinobiLegionsPC-%s-win64/NOTICE.txt" % version).decode("utf-8")
        base = "ShinobiLegionsPC-%s-win64/" % version
        assert names == sorted(base + n for n in ("shinobi_legions_pc.exe", "SDL2.dll", "README.txt",
                                                  "NOTICE.txt", "CHANGELOG.txt")), names
        assert not any(GAME_DATA.search(n) for n in names)
        assert version in readme and "@VERSION@" not in readme and "\r\n" in readme
        assert "--extract" in readme and "FRANCAIS" in readme and "ENGLISH" in readme
        assert "NO game data" in notice
        bad = os.path.join(tmp, "x.txt")
        open(bad, "wb").write(b"not a PE file")
        try:
            build(bad, dll, tmp, version, dry_run=True)
            raise AssertionError("non-PE exe accepted")
        except SystemExit:
            pass
        print("self-test: OK (%d files in the zip)" % len(names))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser(description="Build the Windows player package (no game data).")
    ap.add_argument("--exe", help="shinobi_legions_pc.exe (MinGW / MSVC build)")
    ap.add_argument("--sdl", help="SDL2.dll matching the build (x64)")
    ap.add_argument("--out", default=os.path.join(PORT, "dist"))
    ap.add_argument("--version", default=None)
    ap.add_argument("--dry-run", action="store_true", help="list the contents, write nothing")
    ap.add_argument("--self-test", action="store_true", help="build from dummy files in a temp dir")
    a = ap.parse_args()
    if a.self_test:
        self_test()
        return 0
    if not a.exe or not a.sdl:
        ap.error("--exe and --sdl are required (or --self-test)")
    build(a.exe, a.sdl, a.out, a.version or cmake_version(), a.dry_run)
    return 0


if __name__ == "__main__":
    sys.exit(main())
