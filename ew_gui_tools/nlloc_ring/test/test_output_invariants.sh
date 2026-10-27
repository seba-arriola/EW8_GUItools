#!/bin/sh
# test_output_invariants.sh - nlloc_ring NO publica una salida invalida.
#
# No cambia live_usage (nlloc sigue manual/offline): solo valida rms/nph/gap
# del ARC que devuelve NLLoc antes de publicarlo.
set -u
here=$(cd "$(dirname "$0")" && pwd)
src=$here/../nlloc_ring.c
d=$here/../../../run_working_v8/params/nlloc_ring.d
pass=0; fail=0
ok()  { pass=$((pass + 1)); echo "ok   $*"; }
bad() { fail=$((fail + 1)); echo "FALLA $*"; }

if grep -q 'live_check_arc' "$src"; then
    ok "nlloc_ring valida la salida con live_check_arc"
else
    bad "nlloc_ring no valida rms/nph/gap de la salida"
fi
if grep -q 'MaxGapDeg' "$d" && grep -q 'MaxGapDeg' "$src"; then
    ok "MaxGapDeg declarado y leido"
else
    bad "falta MaxGapDeg"
fi
if grep -q 'rms=99.99' "$src" || grep -q 'live_check_arc' "$src"; then
    ok "el centinela rms=99.99 se rechaza por live_check_arc"
else
    bad "no hay defensa contra rms=99.99"
fi
if grep -qE '^[[:space:]]*locator[[:space:]]+nlloc_ring[[:space:]]+nlloc_ring\.d[[:space:]]+0[[:space:]]+0[[:space:]]+5' \
        "$here/../../../run_working_v8/params/csnhypodbpqt.d"; then
    ok "nlloc_ring sigue offline (live_usage=0)"
else
    bad "cambio live_usage de nlloc_ring"
fi

echo "== nlloc output invariants checks=$((pass + fail)) fallas=$fail =="
[ "$fail" -eq 0 ]
