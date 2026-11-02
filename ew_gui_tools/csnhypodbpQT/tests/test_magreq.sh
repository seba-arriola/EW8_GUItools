#!/bin/sh
# test_magreq.sh - La casilla por estacion de la tabla de magnitudes filtra
# SOLO la peticion de magnitud.
#
# Con `BPQ_MAG_OFF=PB02` la estacion desmarcada no debe viajar a `csnmags` (ni
# a la solucion base ni a las trazas): `--magcalc BPQ_MAG_DRY=1` arma la
# peticion y la imprime (`magreq n=... stas=...`) sin lanzar el motor. Sin
# `BPQ_MAG_OFF`, la estacion vuelve a entrar. Va en su propio script porque el
# `locate` previo de cada caso tarda varios segundos (el test de UI tiene un
# presupuesto de tiempo aparte).
set -u

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
params=$root/run_working_v8/params
bin=$here/../csnhypodbpQT
[ -x "$bin" ] || bin=$root/ew_8.0/bin/csnhypodbpQT
if [ ! -x "$bin" ] || [ ! -f "$params/csnhypodbpqt.d" ]; then
    echo "SKIP: falta $bin o $params/csnhypodbpqt.d"
    exit 0
fi

pass=0
fail=0
out=
ok()  { pass=$((pass + 1)); echo "ok   $*"; }
bad() { fail=$((fail + 1)); echo "FALLA $*"; }

run() {  # <off|-> <state>
    if [ "$1" = "-" ]; then
        out=$( cd "$params" && env QT_QPA_PLATFORM=offscreen \
            BPQ_MODE=--magcalc BPQ_TEST_FIXTURE=pins BPQ_TAB=magnitudes \
            BPQ_MAG_DRY=1 BPQ_WAVE_WAIT_MS=100 BPQ_STATE_DIR="$2" \
            "$bin" csnhypodbpqt.d 2>/dev/null )
    else
        out=$( cd "$params" && env QT_QPA_PLATFORM=offscreen \
            BPQ_MODE=--magcalc BPQ_TEST_FIXTURE=pins BPQ_TAB=magnitudes \
            BPQ_MAG_OFF="$1" BPQ_MAG_DRY=1 BPQ_WAVE_WAIT_MS=100 \
            BPQ_STATE_DIR="$2" "$bin" csnhypodbpqt.d 2>/dev/null )
    fi
}

s1=$(mktemp -d "${TMPDIR:-/tmp}/bpq_magreq1.XXXXXX") || exit 2
s2=$(mktemp -d "${TMPDIR:-/tmp}/bpq_magreq2.XXXXXX") || exit 2
trap 'rm -rf "$s1" "$s2"' EXIT INT TERM

run PB02 "$s1"
if printf '%s\n' "$out" | grep -q '^magreq n=[1-9][0-9]* stas='; then
    ok "magreq: la peticion se arma"
else
    bad "magreq: no hay peticion"
fi
if printf '%s\n' "$out" | grep -q '^magreq .*PB02'; then
    bad "magreq: PB02 desmarcada sigue en la peticion"
else
    ok "magreq: PB02 desmarcada no entra"
fi
if printf '%s\n' "$out" | grep -q '^mag .* off=PB02'; then
    ok "magreq: la tabla registra la estacion desmarcada"
else
    bad "magreq: el dump no registra off=PB02"
fi

run - "$s2"
if printf '%s\n' "$out" | grep -q '^magreq .*PB02'; then
    ok "magreq: sin desmarcar PB02 si entra"
else
    bad "magreq: PB02 desaparecio sin estar desmarcada"
fi

echo "== magreq checks=$((pass + fail)) fallas=$fail =="
[ "$fail" = 0 ]
