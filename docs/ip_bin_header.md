# IP.BIN header (sector 0 of track 1)

```
Hardware ID   : SEGA SEGASATURN
Maker ID      : SEGA TP
Product #     : T-2301H
Version       : V1.000
Release date  : 1995-08-02
Device info   : CD-1/1   (single CD, disc 1 of 1)
Area          : U (USA)
Peripherals   : J (standard pad?)
Title         : SHINOBI LEGIONS
1st read addr : 0x06004000  (Work RAM-H, where the boot loader stages A.BIN)
```

`A.BIN` (245,760 bytes) on the ISO9660 filesystem is almost certainly the main
SH-2 executable — standard Saturn homebrew/retail naming for the program the
IP.BIN bootstrap loads and jumps to. This is the file to disassemble first.

Next: pull A.BIN out with `tools/extract_disc.py` + pycdlib (or 7z on the
resulting .iso), then feed it to an SH-2 disassembler (objdump -m sh
--target=binary, or Ghidra with the SH-2 processor module) starting at
load address 0x06004000.
