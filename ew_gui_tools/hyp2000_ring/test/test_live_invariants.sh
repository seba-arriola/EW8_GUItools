#!/bin/sh
# test_live_invariants.sh - invariantes del locator vivo (sin motor ni anillos).
#
# El locator con live_usage=1 lee la semilla de HYPO_RING y TODOS los picks de
# PICK_RING; recalcula cada time_recalc_live y se detiene tras N_retry intentos
# sin picadas nuevas. La tabla de locators vive SOLO en csnhypodbpqt.d.

set -u
here=$(cd "$(dirname "$0")" && pwd)
src=$here/../hyp2000_ring.c
d=$here/../../../run_working_v8/params/hyp2000_ring.d
pass=0
fail=0
ok()  { pass=$((pass + 1)); echo "ok   $*"; }
bad() { fail=$((fail + 1)); echo "FALLA $*"; }

if grep -q 'InPickRing' "$d" && grep -q 'LivePickWindowSec' "$d"; then
    ok "hyp2000_ring.d declara InPickRing y LivePickWindowSec"
else
    bad "hyp2000_ring.d no declara InPickRing/LivePickWindowSec"
fi
if grep -q 'live_add_pick' "$src" && grep -q 'PickSCNL_Parse' "$src"; then
    ok "el vivo consume TYPE_PICK_SCNL"
else
    bad "el vivo no consume los picks"
fi
if grep -q 'live_build' "$src" && grep -q 'csnhypodbp_arc_build' "$src"; then
    ok "construye el ARC con la solucion preferida + todos los picks"
else
    bad "no construye el ARC con todos los picks"
fi
if grep -q 'LiveNRetry' "$src" && grep -q 'no_new' "$src"; then
    ok "N_retry corta los intentos sin picadas nuevas"
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
