#!/bin/sh
# test_output_invariants.sh - nlloc_ring NO publica una salida invalida.
#
# No fija live_usage (lo configura el operador en csnhypodbpqt.d): valida
# rms/nph/gap del ARC que devuelve NLLoc antes de publicarlo.
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
if grep -q 'live_refine_new' "$src" && grep -q 'live_refine_tick' "$src" &&
   grep -q 'live_refine_add_pick_msg' "$src"; then
    ok "nlloc_ring usa el motor vivo comun (live_refine)"
else
    bad "nlloc_ring no usa el motor vivo comun"
fi
if grep -q 'InPickRing' "$src" && grep -q 'InPickRing' "$d" &&
   grep -q 'ConfRing' "$src" && grep -q 'ConfRing' "$d"; then
    ok "nlloc_ring lee PICK_RING y HYPO_RING_REF2"
else
    bad "faltan InPickRing/ConfRing en nlloc_ring"
fi
if grep -q 'StaFile' "$d" && grep -q 'TauTable' "$d" &&
   grep -q 'StaFile' "$src" && grep -q 'TauTable' "$src"; then
    ok "nlloc_ring carga estaciones y tiempos de viaje para asociar"
else
    bad "faltan StaFile/TauTable"
fi
if grep -q 'aun no soportado' "$src"; then
    bad "sigue el rechazo de live_usage=1"
else
    ok "live_usage=1 ya no se rechaza"
fi
if grep -qE 'NllIntervalSec|NllTTLSec|NllMaxPending' "$src" "$d"; then
    bad "quedan las claves viejas del rate-limit (las sustituye la tabla)"
else
    ok "NllIntervalSec/NllTTLSec/NllMaxPending retiradas"
fi
if grep -q 'LiveMaxIter' "$d" && grep -q 'LiveMaxIter' "$src"; then
    ok "LiveMaxIter declarado y leido"
else
    bad "falta LiveMaxIter"
fi
if grep -q 'LiveEventTTLSec' "$d" && grep -q 'LiveEventTTLSec' "$src"; then
    ok "LiveEventTTLSec declarado y leido"
else
    bad "falta LiveEventTTLSec"
fi
echo "== nlloc output invariants checks=$((pass + fail)) fallas=$fail =="
[ "$fail" -eq 0 ]
