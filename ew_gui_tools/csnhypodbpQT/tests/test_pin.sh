#!/bin/sh
# test_pin.sh - Origen fijado (fase 7), end-to-end y sin DISPLAY.
#
# Corre el binario real con QT_QPA_PLATFORM=offscreen y BPQ_MODE=--dump sobre un
# fixture determinista (BPQ_TEST_FIXTURE=pins: 2 eventos, 3 soluciones) y
# comprueba el estado que imprime dumpTree():
#
#   - doble clic fija y lleva a Procesar/picking;
#   - Selected solution / magnitudes / trazas siguen SOLO al pin;
#   - el clic simple no mueve el pin;
#   - el pin sobrevive a refresh y a una confirmacion que cambia la preferida;
#   - al cambiar de evento se suelta;
#   - la candidata manda mientras existe y Discard vuelve al pin.
#
# No toca anillos, BD ni estado real: el modo fixture los desactiva y
# BPQ_STATE_DIR apunta a un temporal.
#
# Uso: sh tests/test_pin.sh

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

state=$(mktemp -d "${TMPDIR:-/tmp}/bpq_pin.XXXXXX") || exit 2
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
        printf '%s\n' "$out" | grep -E '^(data |pin |panels |preview |traces |mag )' |
            sed 's/^/       /'
    fi
}

# --- 1. fixture: sin pin, todo en la preferida ------------------------------
run_dump
ck "fixture: 2 eventos"                          '^data events=2$'
ck "fixture: 2 origenes en el evento A"          '^data origins=2$'
ck "sin pin: key=- y pestana Eventos"            '^pin key=- event=100:1767323045\.000000 tab=0/0 '
ck "sin pin: sel=pref=0"                         '^panels pts=2 pref=0 sel=0 '
ck "sin pin: Selected solution de la preferida"  '^preview .*title="Selected solution" .*lat=-20\.100 .*lon=-70\.100 .*dep=10 .*nph=12 .*rms=0\.2 .*mags=4\.1-8,'
ck "sin pin: trazas de la preferida"             '^traces .* sol=100:162:1$'
ck "Mod: la solucion 162 se etiqueta csnloc"     '^row 0: .*[|]csnloc[|]'

# --- 2. doble clic fija y lleva a Procesar ----------------------------------
run_dump BPQ_TEST_PIN=100:163:2
ck "pin B impreso"                               '^pin key=100:163:2 event=100:1767323045\.000000 tab=1/0 '
ck "pin B: Procesar/picking activos"             '^pin .* tab=1/0 '
ck "pin B: el marcador verde es B"               '^panels pts=2 pref=0 sel=1 '
ck "pin B: Selected solution es B"               '^preview .*title="Selected solution" .*lat=-20\.900 .*lon=-70\.900 .*dep=33 .*nph=8 .*rms=0\.5 '
ck "pin B: magnitudes por origen de B"           '^preview .*mags=3\.2-5,3\.5-4,-,-,-$'
ck "pin B: trazas ancladas a B"                  '^traces .* sol=100:163:2$'
ck "pin B: un solo fetch (el del pin)"           '^pin .* fetches=2$'

# --- 3. clic simple en A NO mueve el pin -----------------------------------
run_dump BPQ_TEST_PIN=100:163:2 BPQ_TEST_CLICK=100:162:1
ck "clic en A: sigue fijado B"                   '^pin key=100:163:2 '
ck "clic en A: la seleccion vuelve a B"          '^panels pts=2 pref=0 sel=1 '
ck "clic en A: preview sigue en B"               '^preview .*lat=-20\.900 .*dep=33 '
ck "clic en A: no re-descarga ondas"             '^pin .* fetches=2$'

# --- 4. refresh del store conserva pin y no re-descarga ---------------------
run_dump BPQ_TEST_PIN=100:163:2 BPQ_TEST_REFRESH=1
ck "refresh: pin B intacto"                      '^pin key=100:163:2 '
ck "refresh: sel sigue en B"                     '^panels pts=2 pref=0 sel=1 '
ck "refresh: preview sigue en B"                 '^preview .*lat=-20\.900 .*dep=33 '
ck "refresh: no re-descarga ondas"               '^pin .* fetches=2$'

# --- 5. doble clic en el MISMO pin fuerza recarga ---------------------------
run_dump BPQ_TEST_PIN=100:163:2 BPQ_TEST_PIN_AGAIN=1
ck "pin otra vez: sigue B"                       '^pin key=100:163:2 '
ck "pin otra vez: +1 fetch"                      '^pin .* fetches=3$'

# --- 6. solo fijando otro origen se cambia ---------------------------------
run_dump BPQ_TEST_PIN=100:163:2,100:162:1
ck "fijar A reemplaza a B"                       '^pin key=100:162:1 '
ck "fijar A: sel=0"                              '^panels pts=2 pref=0 sel=0 '
ck "fijar A: preview A"                          '^preview .*lat=-20\.100 .*dep=10 '
ck "fijar A: segundo fetch"                      '^pin .* fetches=3$'

# --- 7. confirmacion nueva: cambia la preferida, NO el pin ------------------
run_dump BPQ_TEST_PIN=100:163:2 BPQ_TEST_FEED_AFTER=1
ck "confirmacion: 3 origenes"                    '^data origins=3$'
ck "confirmacion: la preferida pasa a C"         '^panels pts=3 pref=2 sel=1 '
ck "confirmacion: el pin sigue en B"             '^pin key=100:163:2 '
ck "confirmacion: la identidad del evento no cambia" \
                                                 '^pin .* event=100:1767323045\.000000 '
ck "confirmacion: preview sigue en B"            '^preview .*lat=-20\.900 .*dep=33 '
ck "confirmacion: sin re-descarga"               '^pin .* fetches=2$'

# --- 8. cambiar de evento suelta el pin ------------------------------------
run_dump BPQ_TEST_PIN=100:163:2 BPQ_TEST_EVENT=200:162:1
ck "cambio de evento: pin suelto"                '^pin key=- event=200:1767236645\.000000 '
ck "cambio de evento: 1 origen"                  '^data origins=1$'
ck "cambio de evento: preferida del evento B"    '^panels pts=1 pref=0 sel=0 '
ck "cambio de evento: preview del evento B"      '^preview .*lat=-30\.000 .*lon=-71\.000 .*dep=20 .*mags=2\.5-3,'
ck "cambio de evento: trazas del evento B"       '^traces .* sol=200:162:1$'

# --- 9. candidata por clave y Discard --------------------------------------
run_dump BPQ_TEST_PIN=100:163:2 BPQ_TEST_CAND=1
ck "candidata: manda sobre el pin"               '^preview .*cand=1 '
ck "candidata: titulo New solution"              '^preview .*title="New solution'
ck "candidata: valores de la candidata"          '^preview .*lat=-20\.400 .*dep=38 '
ck "candidata: trazas siguen en el pin"          '^traces .* sol=100:163:2$'

run_dump BPQ_TEST_PIN=100:163:2 BPQ_TEST_CAND=1 BPQ_TEST_CAND_DISCARD=1
ck "discard: vuelve al pin"                      '^pin key=100:163:2 '
ck "discard: preview de B"                       '^preview .*cand=0 .*title="Selected solution" .*lat=-20\.900 '

# --- 10. clave inexistente no fija nada ------------------------------------
run_dump BPQ_TEST_PIN=999:1:1
ck "clave inexistente: no hay pin"               '^pin key=- '

echo "----"
echo "== pin checks=$((pass + fail)) fallas=$fail =="
[ "$fail" -eq 0 ] || exit 1
exit 0
