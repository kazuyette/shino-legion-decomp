#!/bin/sh
# Dump the real game's memory + screenshot from BizHawk (sl_bridge.lua running) for
# tests/snap_compare.c (see docs/ground_truth.md):
#   tools/bizhawk/snap.sh NAME  ->  bridge/NAME_v1.bin (VDP1 RAM, frame N-1), then one frame later
#                                   NAME_hw (HWRAM) _lw (LWRAM) _v2 (VDP2 RAM) _cr (CRAM) .bin and
#                                   NAME.State (frame N), and NAME_s.png = the picture shown at N+1.
# Timing (measured): the dumps are taken between VBlank-IN and VBlank-OUT; the VDP2 state of frame
# N is displayed at N+1, but VDP1 draws the command list of frame N during N+1 (shown at N+2), so
# the sprites shown at N+1 are those of the VDP1 RAM of frame N-1.
cd "$(dirname "$0")" && BH_TIMEOUT=${BH_TIMEOUT:-120} python3 bh.py \
  "dump 0x25C00000 0x80000 $1_v1.bin" "frames 1" \
  "dump 0x06000000 0x100000 $1_hw.bin" "dump 0x00200000 0x100000 $1_lw.bin" \
  "dump 0x25E00000 0x80000 $1_v2.bin" "dump 0x25F00000 0x1000 $1_cr.bin" "save $1.State" \
  "frames 1" "shot $1_s.png" "load $1.State" ping
