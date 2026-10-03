// Creates functions for every object handler referenced by the object
// handler table at 0x060AC820 (type 0x1000 + i), names them ObjHandler_10xx,
// and decompiles cleanly afterwards. Run on SHINOBI.dec.bin (base 0x06040000).
//@category ShinobiLegions
import ghidra.app.script.GhidraScript;
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;

public class CreateObjectHandlers extends GhidraScript {
    @Override
    public void run() throws Exception {
        Address table = toAddr(0x060AC820L);
        int created = 0, existing = 0;
        for (int i = 0; i < 0x80; i++) {
            long p = getInt(table.add(i * 4)) & 0xFFFFFFFFL;
            if (p == 0) continue;
            if (p < 0x06040000L || p >= 0x060AC000L || (p & 1) != 0) break;
            Address a = toAddr(p);
            String name = String.format("ObjHandler_%04X", 0x1000 + i);
            Function f = getFunctionAt(a);
            if (f == null) {
                clearListing(a, a.add(1));
                new DisassembleCommand(a, null, true).applyTo(currentProgram, monitor);
                f = createFunction(a, name);
                if (f != null) created++;
            } else {
                existing++;
                if (f.getName().startsWith("FUN_")) f.setName(name, SourceType.USER_DEFINED);
            }
        }
        println("Object handlers: created " + created + ", already existing " + existing);
    }
}
