// Creates functions Ghidra's auto-analysis missed in SHINOBI.PPB / A.BIN:
//  1. every entry of the known handler tables (enemies, projectiles, enemy loaders, objects);
//  2. every SH-2 prologue ("mov.l r14,@-r15" or "sts.l pr,@-r15" not preceded by another
//     push) that is not inside an existing function.
// @category ShinobiLegions
import ghidra.app.script.GhidraScript;
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.program.model.address.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.listing.*;

public class CreateMissingFunctions extends GhidraScript {
    int created = 0;

    boolean make(Address a) throws Exception {
        if (a == null || !currentProgram.getMemory().contains(a)) return false;
        if (getFunctionContaining(a) != null) return false;
        if (getInstructionAt(a) == null) {
            DisassembleCommand d = new DisassembleCommand(a, null, true);
            d.applyTo(currentProgram, monitor);
        }
        if (getInstructionAt(a) == null) return false;
        CreateFunctionCmd c = new CreateFunctionCmd(a);
        if (c.applyTo(currentProgram, monitor)) { created++; return true; }
        return false;
    }

    void table(long base, int max, String prefix) throws Exception {
        Memory m = currentProgram.getMemory();
        for (int i = 0; i < max; i++) {
            Address e = toAddr(base + 4L * i);
            if (!m.contains(e)) break;
            long t = m.getInt(e) & 0xFFFFFFFFL;
            if (t == 0) continue;
            if (t < 0x06004000L || t >= 0x060B0000L || (t & 1) != 0) continue;
            Address a = toAddr(t);
            make(a);
            Function f = getFunctionAt(a);
            if (f != null && f.getName().startsWith("FUN_"))
                f.setName(String.format("%s_%02X", prefix, i), ghidra.program.model.symbol.SourceType.USER_DEFINED);
        }
    }

    public void run() throws Exception {
        table(0x060A9090L, 64, "EnemyHandler");
        table(0x060A9124L, 64, "EnemyLoader");
        table(0x060AC994L, 45, "ProjHandler");
        table(0x060AC820L, 74, "ObjHandler10");
        Memory m = currentProgram.getMemory();
        for (MemoryBlock b : m.getBlocks()) {
            if (!b.isInitialized()) continue;
            Address s = b.getStart(), e = b.getEnd();
            if (s.getOffset() < 0x06004000L || s.getOffset() > 0x060FFFFFL) continue;
            for (long o = s.getOffset() & ~1L; o + 4 <= e.getOffset(); o += 2) {
                if (monitor.isCancelled()) return;
                Address a = toAddr(o);
                int w = m.getShort(a) & 0xFFFF;
                if (w != 0x2FE6 && w != 0x4F22) continue;
                int prev = m.getShort(toAddr(o - 2)) & 0xFFFF;
                if ((prev & 0xF00F) == 0x2006 && ((prev >> 4) & 0xF) == 0xF) continue;   // previous push
                if (prev == 0x4F22 || prev == 0x4F12) continue;
                if (getFunctionContaining(a) != null) continue;
                Instruction i = getInstructionContaining(a);
                if (i != null && !i.getAddress().equals(a)) continue;
                make(a);
            }
        }
        println("CreateMissingFunctions: created " + created + " functions");
    }
}
