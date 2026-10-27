#!/bin/sh
# test_gate_invariants.sh - las puertas del gatillo existen y son configurables.
set -u
here=$(cd "$(dirname "$0")" && pwd)
src=$here/../csnloc.c
d=$here/../../../run_working_v8/params/csnloc.d
pass=0; fail=0
ok()  { pass=$((pass + 1)); echo "ok   $*"; }
bad() { fail=$((fail + 1)); echo "FALLA $*"; }

for key in MinPPhases MaxAssocDistKm MaxGapDeg EventMatchSharedPicks \
           MinEmitPhases EmitImproveMinPicks; do
    if grep -q "$key" "$src" && grep -q "$key" "$d"; then
        ok "$key declarado y aplicado"
    else
        bad "$key ausente en csnloc.c o csnloc.d"
    fi
done
if grep -q 'candidato descartado nP=' "$src" &&
   grep -q 'candidato descartado gap=' "$src"; then
    ok "los descartes del gatillo dejan rastro en el log"
else
    bad "los descartes del gatillo no se loguean"
fi
if grep -q 'EventDedupSec\|EventDedupKm' "$d" || \
   grep -q 'EventDedupSec\|EventDedupKm' "$src"; then
    bad "quedan claves viejas EventDedup* (deben reemplazarse)"
else
    ok "EventDedup* reemplazadas por EventMatch*"
fi

echo "== gate invariants checks=$((pass + fail)) fallas=$fail =="
[ "$fail" -eq 0 ]
