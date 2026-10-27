#!/bin/sh
# test_live_invariants.sh - invariantes del locator vivo (sin motor ni anillos).
#
# El locator con live_usage=1 lee la semilla de HYPO_RING y asocia TODOS los
# P/S de PICK_RING por tiempo de viaje (live_assoc). Recalcula cada
# time_recalc_live y se detiene tras N_retry intentos sin picadas nuevas o sin
# solucion valida. La tabla de locators vive SOLO en csnhypodbpqt.d.

set -u
here=$(cd "$(dirname "$0")" && pwd)
src=$here/../hyp2000_ring.c
assoc=$here/../../csnloc/live_assoc.c
check=$here/../../csnloc/live_check.c
d=$here/../../../run_working_v8/params/hyp2000_ring.d
pass=0
fail=0
ok()  { pass=$((pass + 1)); echo "ok   $*"; }
bad() { fail=$((fail + 1)); echo "FALLA $*"; }

if grep -q 'InPickRing' "$d" && grep -q 'LivePickLookbackSec' "$d" &&
   grep -q 'LivePickResidualSecP' "$d" && grep -q 'LivePickResidualSecS' "$d"; then
    ok "hyp2000_ring.d declara InPickRing y la asociacion por tiempo de viaje"
else
    bad "hyp2000_ring.d no declara InPickRing/LivePickLookbackSec/Residual*"
fi
if grep -q 'LivePickWindowSec' "$d" || grep -q 'LivePickWindowSec' "$src"; then
    bad "queda la clave vieja LivePickWindowSec (debe reemplazarse)"
else
    ok "LivePickWindowSec reemplazada por LivePickLookbackSec"
fi
if grep -q 'live_add_pick' "$src" && grep -q 'PickSCNL_Parse' "$src"; then
    ok "el vivo consume TYPE_PICK_SCNL"
else
    bad "el vivo no consume los picks"
fi
if grep -q 'live_build_arc' "$assoc" && grep -q 'csnhypodbp_arc_build' "$assoc"; then
    ok "construye el ARC con todas las P/S asociadas (no con las fases del seed)"
else
    bad "no construye el ARC desde las picadas asociadas"
fi
if grep -q 'live_assoc_pick' "$assoc" && grep -q 'TTModel_Predict' "$assoc"; then
    ok "asocia por tiempo de viaje predicho (ventana creciente)"
else
    bad "no asocia por tiempo de viaje"
fi
if grep -q 'live_check_arc' "$src" && grep -q 'live_check_arc' "$check"; then
    ok "valida rms/nph/gap de la salida antes de publicar"
else
    bad "no valida la salida del refinador"
fi
if grep -q 'live_build_arc_seeded' "$assoc" && grep -q 'live_build_arc_seeded' "$src"; then
    ok "el ARC se siembra con las fases del seed (nucleo coherente)"
else
    bad "el ARC no incluye las fases del seed"
fi
if grep -q 'live_prune_arc' "$assoc" && grep -q 'live_prune_arc' "$src"; then
    ok "poda iterativa por residual del ARC de salida"
else
    bad "no hay poda iterativa: una salida invalida no se puede corregir"
fi
if grep -q 'LiveMaxIter' "$d" && grep -q 'LiveMaxIter' "$src"; then
    ok "LiveMaxIter declarado y leido"
else
    bad "falta LiveMaxIter"
fi
if grep -q 'g_pick_seq' "$src" && grep -q 'reabierto' "$src"; then
    ok "un evento detenido se reabre con picadas nuevas"
else
    bad "done sigue siendo permanente"
fi
if grep -q 'LiveEventTTLSec' "$d" && grep -q 'LiveEventTTLSec' "$src"; then
    ok "LiveEventTTLSec acota la reapertura"
else
    bad "falta LiveEventTTLSec"
fi
if grep -q 'LiveNRetry' "$src" && grep -q 'no_new' "$src"; then
    ok "N_retry corta los intentos sin picadas nuevas o sin solucion valida"
else
    bad "falta N_retry"
fi
if grep -q 'live_tick' "$src" && grep -q 'LiveRecalcSec' "$src"; then
    ok "recalculo periodico por LiveRecalcSec"
else
    bad "falta el temporizador de recalculo"
fi
if grep -q 'HYPO_RING_REF2' "$src" && grep -q 'live_prefer' "$src"; then
    ok "usa la solucion confirmada como preferida"
else
    bad "no lee la solucion confirmada"
fi
if grep -qE '^[[:space:]]*(locator|LocatorTable|ModuleLabel)[[:space:]]' "$d"; then
    bad "hyp2000_ring.d referencia la tabla/etiquetas de locators"
else
    ok "hyp2000_ring.d no declara locators"
fi

echo "== live invariants checks=$((pass + fail)) fallas=$fail =="
[ "$fail" -eq 0 ]
