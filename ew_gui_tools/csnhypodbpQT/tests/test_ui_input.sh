#!/bin/sh
# test_ui_input.sh - Scroll, teclado y magnitudes del csnhypodbpQT, headless.
#
# Corre el binario real con QT_QPA_PLATFORM=offscreen sobre el fixture `pins`
# (sin anillos ni BD) y comprueba, por el dump:
#
#   - tracesPerScreen=1 deja 3 filas (HHZ/HHN/HHE) y una sola visible;
#   - la rueda sin Ctrl mueve `scroll` y NO la ventana temporal;
#   - P con foco en la tabla arma el picado (no hace type-ahead);
#   - Down con foco en la tabla mueve el canal visible;
#   - P con foco en una caja de texto NO arma el picado;
#   - la pestana magnitudes llena una banda por canal (ntr=3) y tiene ancla.
#
# Uso: sh tests/test_ui_input.sh

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
if [ ! -f "$params/csnhypodbpqt.d" ]; then
    echo "SKIP: falta $params/csnhypodbpqt.d"
    exit 0
fi

state=$(mktemp -d "${TMPDIR:-/tmp}/bpq_input.XXXXXX") || exit 2
trap 'rm -rf "$state"' EXIT INT TERM

pass=0
fail=0
ok()  { pass=$((pass + 1)); echo "ok   $*"; }
bad() { fail=$((fail + 1)); echo "FALLA $*"; }

out=
run_dump() {  # <tab>
    out=$( cd "$params" && env QT_QPA_PLATFORM=offscreen \
        BPQ_MODE=--dump BPQ_TEST_FIXTURE=pins BPQ_TAB="$1" \
        BPQ_WAVE_WAIT_MS=100 BPQ_STATE_DIR="$state" \
        "$bin" csnhypodbpqt.d 2>/dev/null )
}
run_input() {  # <pares VAR=valor...>
    out=$( cd "$params" && env QT_QPA_PLATFORM=offscreen \
        BPQ_MODE=--input BPQ_TEST_FIXTURE=pins BPQ_TAB=picking \
        BPQ_WAVE_WAIT_MS=100 BPQ_STATE_DIR="$state" \
        "$@" "$bin" csnhypodbpqt.d 2>/dev/null )
}
ck() {  # <descripcion> <regex ERE>
    if printf '%s\n' "$out" | grep -qE "$2"; then
        ok "$1"
    else
        bad "$1"
        printf '%s\n' "$out" | grep -E '^(traces|pick|mag|input) ' | sed 's/^/       /'
    fi
}

# --- 1. filas por canal y tracesPerScreen --------------------------------
run_dump picking
ck "3 filas por canal, 1 visible"     '^traces rows=3 with=[0-9]+ first=0 nvis=1 scroll=0 per=1 '
ck "hay un canal por componente"      '^stations rows=[0-9]+ picked=[1-9]'

# --- 2. teclado con foco en la tabla -------------------------------------
run_input BPQ_INPUT_TARGET=sta BPQ_INPUT_KEY=P
ck "P en tabla arma el picado"        '^pick armed=1 phase=P$'

run_input BPQ_INPUT_TARGET=sta BPQ_INPUT_KEY=Down
ck "Down en tabla mueve el canal"     '^traces rows=3 .* first=1 nvis=1 scroll=1 '
ck "Down no arma el picado"           '^pick armed=0 phase=-$'

# --- 3. guardia de texto: P no se convierte en picado --------------------
run_input BPQ_INPUT_TARGET=text BPQ_INPUT_KEY=P
ck "P en caja de texto no pica"       '^pick armed=0 phase=-$'

# --- 4. rueda: filas sin Ctrl, zoom con Ctrl ------------------------------
run_input BPQ_INPUT_TARGET=waves BPQ_INPUT_WHEEL=down
ck "rueda abajo mueve scroll"         '^traces .* first=1 nvis=1 scroll=1 '
ck "rueda abajo no cambia la ventana" '^traces .* win=\[[0-9.]+,?\+120\] '

run_input BPQ_INPUT_TARGET=waves BPQ_INPUT_WHEEL=up BPQ_INPUT_MODS=ctrl
ck "Ctrl+rueda hace zoom"             '^traces .* win=\[[0-9.]+,?\+100\] '

# --- 5. magnitudes: una banda por canal + ancla --------------------------
run_dump magnitudes
ck "magnitudes: 3 bandas"             '^mag .* ntr=3 anchor=[1-9][0-9]*\.'
ck "magnitudes: magcfg cargada"       '^mag .* magcfg=1 '

echo "----"
echo "== ui input checks=$((pass + fail)) fallas=$fail =="
[ "$fail" -eq 0 ] || exit 1
exit 0
