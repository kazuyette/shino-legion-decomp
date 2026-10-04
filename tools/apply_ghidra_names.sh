#!/bin/bash
# Re-apply the PC port's knowledge (names + comments) to the Ghidra project.
#
#   tools/apply_ghidra_names.sh            # mine -> lock check -> backup -> apply -> verify
#
# Environment overrides:
#   PROJECT_DIR   folder holding the .gpr          (default: shinolegions)
#   PROJECT_NAME  project name (.gpr without ext)   (default: "Shinobi Legions decomp")
#                 the empty shared project is PROJECT_DIR=projet PROJECT_NAME=shinolegions
#   PROGRAMS      programs to process               (default: "SHINOBI.dec.bin DIRECTOR.dec.bin")
#   GHIDRA_HOME   Ghidra install                    (default: ./ghidra_12.1.2_PUBLIC)
#   JAVA_HOME     JDK 21                            (default: ~/w/jdk-21* if present)
#   BSS=0         do not create the uninitialized blocks BSS (image end .. 0x060FFFFF) and
#                 WRAM_H_LOW (0x06000000-0x06003FFF) that let work-RAM globals get labels (default 1; with 0 those rows are skipped = nomem)
#   FORCE=1       apply even if a <name>.lock file exists (ONLY if Ghidra is really closed:
#                 a stale lock after a crash). Never run while the GUI has the project open.
#   NO_BACKUP=1   skip the backup copy
#   APPLY=0 / VERIFY=0  skip the apply / the verify step (e.g. APPLY=0 NO_BACKUP=1 to only verify)
# Close the project in the Ghidra GUI first. Output: notes/ghidra_sync/report.<ROLE>.tsv,
# notes/ghidra_sync/headless_<prog>.log, notes/ghidra_sync/verify.txt
set -euo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO"
PROJECT_DIR="${PROJECT_DIR:-shinolegions}"
PROJECT_NAME="${PROJECT_NAME:-Shinobi Legions decomp}"
PROGRAMS="${PROGRAMS:-SHINOBI.dec.bin DIRECTOR.dec.bin}"
GHIDRA_HOME="${GHIDRA_HOME:-$REPO/ghidra_12.1.2_PUBLIC}"
if [ -z "${JAVA_HOME:-}" ]; then
  for j in "$HOME"/w/jdk-21*; do [ -d "$j" ] && export JAVA_HOME="$j" PATH="$j/bin:$PATH"; done
fi
OUT="$REPO/notes/ghidra_sync"; mkdir -p "$OUT"
case "$PROJECT_DIR" in /*) PD="$PROJECT_DIR";; *) PD="$REPO/$PROJECT_DIR";; esac

[ -f "$PD/$PROJECT_NAME.gpr" ] || { echo "no project $PD/$PROJECT_NAME.gpr"; exit 1; }
if [ -f "$PD/$PROJECT_NAME.lock" ] && [ "${FORCE:-0}" != 1 ]; then
  echo "LOCKED: $PD/$PROJECT_NAME.lock exists -> the project is open in Ghidra (GUI):"
  grep -E "Hostname|Timestamp|Username" "$PD/$PROJECT_NAME.lock" || true
  echo "Close it in Ghidra (File > Close Project) and re-run (FORCE=1 only for a stale lock)."
  exit 2
fi

if [ "${APPLY:-1}" = 1 ]; then
  echo "== 1. mine names"
  python3 tools/mine_names.py
fi

if [ "${NO_BACKUP:-0}" != 1 ] && [ "${APPLY:-1}" = 1 ]; then
  B="${PD%/}_backup_$(date +%Y%m%d_%H%M%S)"
  echo "== 2. backup -> $B"
  cp -a "$PD" "$B"
fi

# headless refuses a project owned by another user name (project.prp OWNER)
OWNER=$(grep -o 'NAME="OWNER" TYPE="string" VALUE="[^"]*"' "$PD/$PROJECT_NAME.rep/project.prp" | sed 's/.*VALUE="//;s/"//' || true)
if [ -n "$OWNER" ] && [ "$OWNER" != "$(id -un 2>/dev/null || echo "$USERNAME")" ]; then
  export JAVA_TOOL_OPTIONS="${JAVA_TOOL_OPTIONS:-} -Duser.name=$OWNER"
fi

H="$GHIDRA_HOME/support/analyzeHeadless"
[ -x "$H" ] || H="$GHIDRA_HOME/support/analyzeHeadless.bat"
[ "${APPLY:-1}" = 1 ] && echo "== 3. apply"
for P in $( [ "${APPLY:-1}" = 1 ] && echo $PROGRAMS ); do
  "$H" "$PD" "$PROJECT_NAME" -process "$P" -noanalysis -scriptPath "$REPO/ghidra_scripts" \
    -postScript ApplyPortNames.java "csv=$REPO/docs/ghidra_names.csv" "report=$OUT/report" "bss=${BSS:-1}" \
    > "$OUT/headless_$P.log" 2>&1 || { echo "headless failed for $P (see $OUT/headless_$P.log)"; exit 3; }
  grep -h "SUMMARY\|created BSS" "$OUT/headless_$P.log" | sed 's/^.*> //; s/ (GhidraScript).*//'
done

[ "${VERIFY:-1}" = 1 ] || exit 0
echo "== 4. verify (read-only listing of a sample)"
: > "$OUT/verify.txt"
for P in $PROGRAMS; do
  "$H" "$PD" "$PROJECT_NAME" -process "$P" -noanalysis -readOnly -scriptPath "$REPO/ghidra_scripts" \
    -postScript VerifyPortNames.java "csv=$REPO/docs/ghidra_names.csv" "n=${VERIFY_N:-25}" > "$OUT/verify_$P.log" 2>&1 || true
  grep -h "VERIFY" "$OUT/verify_$P.log" | sed 's/^.*VerifyPortNames.java> //; s/ (GhidraScript).*//' >> "$OUT/verify.txt"
done
grep "VERIFY TOTAL" "$OUT/verify.txt" || true
echo "mismatches listed in $OUT/verify.txt: $(grep -c MISMATCH "$OUT/verify.txt" || true)"
