// Read-only check of ApplyPortNames: for a deterministic sample of docs/ghidra_names.csv rows
// of the current program, print the symbol now at the address, its function and the first
// line of the [port-sync] comment. Args: csv=<path> n=<sample size per kind>
// @category ShinobiLegions
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.nio.file.*;
import java.util.*;

public class VerifyPortNames extends GhidraScript {
    static List<String> parseCsvLine(String l) {
        List<String> out = new ArrayList<>();
        StringBuilder sb = new StringBuilder();
        boolean q = false;
        for (int i = 0; i < l.length(); i++) {
            char c = l.charAt(i);
            if (q) {
                if (c == '"') {
                    if (i + 1 < l.length() && l.charAt(i + 1) == '"') { sb.append('"'); i++; }
                    else q = false;
                } else sb.append(c);
            } else if (c == '"') q = true;
            else if (c == ',') { out.add(sb.toString()); sb.setLength(0); }
            else sb.append(c);
        }
        out.add(sb.toString());
        return out;
    }

    public void run() throws Exception {
        Map<String, String> args = new HashMap<>();
        for (String s : getScriptArgs()) { int i = s.indexOf('='); if (i > 0) args.put(s.substring(0, i), s.substring(i + 1)); }
        String pn = currentProgram.getName();
        String role = pn.startsWith("SHINOBI.dec") ? "SHINOBI" : pn.startsWith("DIRECTOR.dec") ? "DIRECTOR" : pn.startsWith("A.BIN") ? "A.BIN" : null;
        if (role == null) return;
        int n = Integer.parseInt(args.getOrDefault("n", "25"));
        List<String> lines = Files.readAllLines(Paths.get(args.get("csv")));
        List<String[]> rows = new ArrayList<>();
        for (String l : lines.subList(1, lines.size())) {
            List<String> r = parseCsvLine(l);
            if (r.size() < 4) continue;
            if (r.get(0).equals(role) || (r.get(0).equals("A.BIN") && !role.equals("A.BIN")))
                rows.add(new String[] { r.get(0), r.get(1), r.get(2), r.get(3) });
        }
        int ok = 0, bad = 0, nomem = 0;
        int step = Math.max(1, rows.size() / (2 * n));
        for (int i = 0; i < rows.size(); i++) {
            String[] r = rows.get(i);
            Address a = toAddr(Long.parseLong(r[1].replace("0x", ""), 16));
            if (!currentProgram.getMemory().contains(a)) { nomem++; continue; }
            Symbol p = currentProgram.getSymbolTable().getPrimarySymbol(a);
            boolean has = r[3].isEmpty();
            for (Symbol s : currentProgram.getSymbolTable().getSymbols(a)) if (s.getName().equals(r[3]) || s.getName().startsWith(r[3] + "_06")) has = true;
            Function f = getFunctionAt(a);
            String pc = f != null ? getPlateComment(a) : getEOLComment(a);
            if (pc == null || !pc.contains("port")) pc = getPreComment(a);
            boolean tagged = pc != null && pc.contains("port");
            if (has && tagged) ok++; else bad++;
            if (i % step == 0 || !(has && tagged))
                println(String.format("VERIFY %s %s %s %-8s csv=%-28s primary=%-28s func=%s %s", (has && tagged) ? "OK      " : "MISMATCH",
                    pn, r[1], r[2], r[3], p == null ? "-" : p.getName(), f == null ? "-" : f.getName(),
                    pc == null ? "" : pc.replace("\n", " | ").substring(0, Math.min(110, pc.length()))));
        }
        println("VERIFY TOTAL " + pn + " ok=" + ok + " mismatch=" + bad + " nomem=" + nomem);
    }
}
