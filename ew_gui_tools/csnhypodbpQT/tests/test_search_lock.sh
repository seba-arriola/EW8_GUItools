#!/bin/sh
# test_search_lock.sh - La vista queda BLOQUEADA durante una busqueda (Qt).
#
# Sin BD ni anillos: BPQ_TEST_FIXTURE=pins carga 2 eventos y BPQ_TEST_SEARCH=1
# arma el modo busqueda (lo que hace applySearch al terminar la consulta).
# Despues se simula el camino vivo/confirmacion (BPQ_TEST_FEED_AFTER=1): ni el
# store ni la tabla pueden cambiar hasta Events > Clear search.
#
# Tambien fija invariantes de fuente: el refresco y el drenaje del anillo tienen
# que consultar el modo busqueda.
#
# Uso: sh tests/test_search_lock.sh

set -u

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
params=$root/run_working_v8/params
bin=$here/../csnhypodbpQT
[ -x "$bin" ] || bin=$root/ew_8.0/bin/csnhypodbpQT

if [ ! -x "$bin" ]; then
    echo "SKIP: no existe csnhypodbpQT (construye con 'make all')"
    exit 0
fi
if [ ! -f "$params/csnhypodbp.d" ]; then
    echo "SKIP: falta $params/csnhypodbp.d"
    exit 0
fi

state=$(mktemp -d "${TMPDIR:-/tmp}/bpq_search.XXXXXX") || exit 2
trap 'rm -rf "$state"' EXIT INT TERM

pass=0
fail=0
ok()  { pass=$((pass + 1)); echo "ok   $*"; }
bad() { fail=$((fail + 1)); echo "FALLA $*"; }

out=
run_dump() {
    out=$( cd "$params" && env QT_QPA_PLATFORM=offscreen \
        BPQ_MODE=--dump BPQ_TEST_FIXTURE=pins BPQ_WAVE_WAIT_MS=300 \
        BPQ_STATE_DIR="$state" "$@" "$bin" csnhypodbp.d 2>/dev/null )
}

ck() {  # <descripcion> <regex ERE>
    if printf '%s\n' "$out" | grep -qE "$2"; then
        ok "$1"
    else
        bad "$1"
        printf '%s\n' "$out" | grep -E '^(data |pin |panels |preview |db )' |
            sed 's/^/       /'
    fi
}

# --- 1. control: sin busqueda, la confirmacion SI entra --------------------
run_dump BPQ_TEST_FEED_AFTER=1
ck "control: la confirmacion agrega un origen"   '^data origins=3$'
ck "control: sin busqueda (search=0)"            '^db avail=0 search=0$'

# --- 2. busqueda: la vista queda bloqueada ---------------------------------
run_dump BPQ_TEST_SEARCH=1
ck "busqueda: 2 eventos"                         '^data events=2$'
ck "busqueda: search=1"                          '^db avail=0 search=1$'

run_dump BPQ_TEST_SEARCH=1 BPQ_TEST_FEED_AFTER=1
ck "busqueda: el vivo NO entra (2 eventos)"      '^data events=2$'
ck "busqueda: el vivo NO agrega origenes"        '^data origins=2$'
ck "busqueda: sigue search=1"                    '^db avail=0 search=1$'

run_dump BPQ_TEST_SEARCH=1 BPQ_TEST_REFRESH=1
ck "refresh forzado: la vista no cambia"         '^data events=2$'
ck "refresh: la seleccion se conserva"           '^data origins=2$'

# --- 3. Clear search vuelve a vivo+historial -------------------------------
# Sin BD (fixture) no hay historial que recargar: el store queda vacio, pero el
# modo busqueda SI se suelta.
run_dump BPQ_TEST_SEARCH=1 BPQ_TEST_CLEAR_SEARCH=1
ck "clear: search=0"                             '^db avail=0 search=0$'
ck "clear: sin busqueda la vista no esta bloqueada" '^data events=0$'

# --- 4. invariantes de fuente ----------------------------------------------
src=$here/..
if grep -q 'm_searchMode' "$src/ui/bpq_ring_worker.cxx"; then
    ok "worker: deliverArc/deliverMag consultan el modo busqueda"
else
    bad "worker: deliverArc/deliverMag consultan el modo busqueda"
fi
if grep -q 'm_searchMode && !force' "$src/ui/bpq_mainwindow.cxx"; then
    ok "refreshFromStore: no repinta durante la busqueda"
else
    bad "refreshFromStore: no repinta durante la busqueda"
fi
if grep -q 'm_events && !m_searchMode' "$src/ui/bpq_mainwindow.cxx"; then
    ok "Confirm: no alimenta el store durante la busqueda"
else
    bad "Confirm: no alimenta el store durante la busqueda"
fi

echo "----"
echo "== search_lock checks=$((pass + fail)) fallas=$fail =="
[ "$fail" -eq 0 ] || exit 1
exit 0
