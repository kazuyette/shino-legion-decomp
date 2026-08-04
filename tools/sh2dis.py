#!/usr/bin/env python3
"""
Minimal SH-2 disassembler (16-bit fixed-width instructions, big-endian on Saturn).
Covers the common instruction classes; unknown opcodes print as .word.

Usage: python3 sh2dis.py <file> [start_offset] [length] [--base 0x06004000]
"""
import sys

def s8(v):  return v - 0x100 if v & 0x80 else v
def s12(v): return v - 0x1000 if v & 0x800 else v

def disasm_one(op, addr):
    n = (op >> 8) & 0xF
    m = (op >> 4) & 0xF
    imm8 = op & 0xFF
    d8 = op & 0xFF
    d4 = op & 0xF

    top = op >> 12
    if op == 0x0009: return "NOP"
    if op == 0x000B: return "RTS"
    if op == 0x002B: return "RTE"
    if op == 0x0008: return "CLRT"
    if op == 0x0018: return "SETT"
    if op == 0x0019: return "DIV0U"
    if op == 0x001B: return "SLEEP"
    if top == 0xE:   return f"MOV #{s8(imm8)},R{n}"
    if top == 0x9:   return f"MOV.W @(0x{imm8*2:X},PC),R{n}  ; -> 0x{addr+4+imm8*2:X}"
    if top == 0xD:   return f"MOV.L @(0x{imm8*4:X},PC),R{n}  ; -> 0x{(addr & ~3)+4+imm8*4:X}"
    if top == 0xA:   return f"BRA 0x{(addr+4+s12(op&0xFFF)*2)&0xFFFFFFFF:X}"
    if top == 0xB:   return f"BSR 0x{(addr+4+s12(op&0xFFF)*2)&0xFFFFFFFF:X}"
    if top == 0x8 and n == 0x9: return f"BT 0x{(addr+4+s8(d8)*2)&0xFFFFFFFF:X}"
    if top == 0x8 and n == 0xB: return f"BF 0x{(addr+4+s8(d8)*2)&0xFFFFFFFF:X}"
    if top == 0x8 and n == 0xD: return f"BT/S 0x{(addr+4+s8(d8)*2)&0xFFFFFFFF:X}"
    if top == 0x8 and n == 0xF: return f"BF/S 0x{(addr+4+s8(d8)*2)&0xFFFFFFFF:X}"
    if top == 0x4 and (op & 0xFF) == 0x0B: return f"JSR @R{n}"
    if top == 0x4 and (op & 0xFF) == 0x2B: return f"JMP @R{n}"
    if top == 0x6 and m == 3: return f"MOV R{m},R{n}"
    if top == 0x3 and (op & 0xF) == 0x0: return f"CMP/EQ R{m},R{n}"
    if top == 0x7: return f"ADD #{s8(imm8)},R{n}"
    if top == 0x3 and (op & 0xF) == 0xC: return f"ADD R{m},R{n}"
    if top == 0x2 and (op & 0xF) == 0x8: return f"TST R{m},R{n}"
    if top == 0x2 and (op & 0xF) == 0x9: return f"AND R{m},R{n}"
    if top == 0x2 and (op & 0xF) == 0xA: return f"XOR R{m},R{n}"
    if top == 0x2 and (op & 0xF) == 0xB: return f"OR R{m},R{n}"
    if top == 0x6 and (op & 0xF) == 0x2: return f"MOV.L @R{m},R{n}"
    if top == 0x2 and (op & 0xF) == 0x2: return f"MOV.L R{m},@R{n}"
    if top == 0x6 and (op & 0xF) == 0x0: return f"MOV.B @R{m},R{n}"
    if top == 0x6 and (op & 0xF) == 0x1: return f"MOV.W @R{m},R{n}"
    if op == 0x000A: return "STS MACH,R0"  # placeholder, refine as needed
    # shift / rotate class: 4nXX
    if top == 0x4:
        low = op & 0xFF
        shifts = {
            0x00: "SHLL", 0x01: "SHLR", 0x08: "SHLL2", 0x09: "SHLR2",
            0x10: "DT", 0x11: "CMP/PZ", 0x15: "CMP/PL",
            0x18: "SHLL8", 0x19: "SHLR8", 0x20: "SHAL", 0x21: "SHAR",
            0x24: "ROTCL", 0x25: "ROTCR", 0x28: "SHLL16", 0x29: "SHLR16",
            0x04: "ROTL", 0x05: "ROTR",
        }
        if low in shifts:
            return f"{shifts[low]} R{n}"
        ldsts = {
            0x0A: "LDS", 0x1A: "LDS", 0x2A: "LDS", 0x02: "STS.L", 0x06: "LDS.L",
        }
        # LDS Rn,MACH/MACL/PR (4n0A/4n1A/4n2A) and STS.L/LDS.L @-Rn,X / X,@Rn forms
        stsreg = {0x0A: "MACH", 0x1A: "MACL", 0x2A: "PR"}
        if low in stsreg:
            return f"LDS R{n},{stsreg[low]}"
        stsreg2 = {0x02: "MACH", 0x12: "MACL", 0x22: "PR"}
        if low in stsreg2:
            return f"STS.L {stsreg2[low]},@-R{n}"
        ldsl2 = {0x06: "MACH", 0x16: "MACL", 0x26: "PR"}
        if low in ldsl2:
            return f"LDS.L @R{n}+,{ldsl2[low]}"
    return f".word 0x{op:04X}"


def main():
    path = sys.argv[1]
    start = int(sys.argv[2], 0) if len(sys.argv) > 2 else 0
    length = int(sys.argv[3], 0) if len(sys.argv) > 3 else 256
    base = 0x06004000
    if "--base" in sys.argv:
        base = int(sys.argv[sys.argv.index("--base")+1], 0)

    data = open(path, "rb").read()
    end = min(start + length, len(data))
    for off in range(start, end, 2):
        if off + 1 >= len(data):
            break
        op = (data[off] << 8) | data[off+1]
        addr = base + off
        print(f"{addr:08X}:  {op:04X}    {disasm_one(op, addr)}")

if __name__ == "__main__":
    main()
