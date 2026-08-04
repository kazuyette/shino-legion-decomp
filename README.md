# Shinobi Legions (Saturn) — Reverse Engineering / PC Port

Source: Shinobi Legions (USA), Sega Saturn, serial T-2301H, 1995.

Goal: reverse engineer game logic and asset formats to reimplement/port the game for PC.

## Layout
- `tools/` — extraction/conversion scripts (Python) for disc image and proprietary formats
- `disasm/a_bin/` — SH-2 disassembly notes/output for the main executable (A.BIN)
- `docs/` — findings: file format specs, memory maps, function notes
- `notes/` — working notes / progress log

## Disc contents (ISO9660, track 1)
- `A.BIN` — main SH-2 executable, loaded at 0x06004000 (Work RAM High)
- `*.CPK` — full-motion video (opening/ending/demos/stage-start clips)
- `*.PBC` — likely compressed still-image/portrait graphics
- `*.CHR` / `*.MAP` / `*.SPR` — per-stage tile graphics / tilemaps / sprite data
- `*.SND` / `*.PCM` — audio

Raw disc image is NOT checked into git (copyrighted game data). Keep it locally, outside version control.
