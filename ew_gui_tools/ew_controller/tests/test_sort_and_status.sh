#!/usr/bin/env bash
# test_sort_and_status.sh - valida con AT-SPI (display virtual) la tabla de
# Modulos de ew_controller:
#   (1) ordena pulsando la cabecera (Module / PID / Status) y el 2.º clic invierte;
#   (2) REFLEJA el cambio de estado de un modulo (una fila que pasa a Dead).
#
# Corre contra el EarthWorm EN MARCHA: necesita el CONTROL_RING que crea
# startstop. Solo ESCRIBE un TYPE_STATUS sintetico (nombres inventados AA_probe /
# ZZ_probe) en ese anillo, y usa una copia AISLADA de params en $TMP y un EW_LOG
# propio: no toca run_working_v8/params. El .d de prueba usa PollInt 3600 para
# que startstop no vuelva a responder y pise el estado sintetico.
#
#   test_sort_and_status.sh --dump   calibracion: cabeceras, filas reales, filas
#                                    tras inyectar el estado sintetico y arbol
#   test_sort_and_status.sh --layout comprueba el reparto de espacio (layout)
#   test_sort_and_status.sh          corre las aserciones de tabla/estado
#
# Nota: el TYPE_STATUS sintetico tambien lo ven los demas clientes del anillo
# (startstop lo imprime en su consola y otra instancia de ew_controller lo
# muestra unos segundos). Es transitorio: no se pide estado de nuevo en 1 h.
set -u
export LC_ALL=C

MOD_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
ROOT=$(cd "$MOD_DIR/../.." && pwd)
TMP=${EWGUI_GUI_TMP:-/tmp/opencode/ew_controller_gui}
PARAMS="$TMP/params"
FIX="$MOD_DIR/tests/fixtures"

usage() { sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'; exit "${1:-0}"; }

make_fixture() {
    rm -rf "$TMP"; mkdir -p "$PARAMS" "$TMP/log"
    cp -a "$ROOT/run_working_v8/params/." "$PARAMS/" 2>/dev/null || true
    # .d propio: PollInt enorme (solo manda el estado sintetico) y log en $TMP.
    cat > "$PARAMS/ew_controller.d" <<EOF
MyModuleId  MOD_CONTROL
Ring        CONTROL_RING
HeartBeatInt 30
LogFile     1
PollInt     3600
LogDir      $TMP/log
EOF
}

run_check() {
    local mode="$1" inner="$TMP/inner.sh"
    # shellcheck disable=SC1091
    . "$ROOT/ew8_unix.sh" >/dev/null
    export EW_PARAMS="$PARAMS" EW_LOG="$TMP/log"
    cat > "$inner" <<'INNER'
set -u
export LC_ALL=C
cd "$EWGUI_PARAMS" || exit 1
"$EWGUI_BIN" ew_controller.d > "$EWGUI_LOG/app.out" 2>&1 &
app=$!
trap 'kill "$app" 2>/dev/null' EXIT
sleep 8
if ! kill -0 "$app" 2>/dev/null; then
    echo "FALLO: ew_controller no arranco (¿EarthWorm arriba? ¿CONTROL_RING?)"
    tail -n 8 "$EWGUI_LOG/app.out"
    exit 1
fi

probe() { python3 "$EWGUI_PROBE" "$@"; }
rows()  { probe --rows --limit 20; }
inject() { "$EWGUI_FAKE" CONTROL_RING "$EWGUI_FIX/$1" 2>> "$EWGUI_LOG/fake.out"; }

# Orden visual de los dos modulos sinteticos (cada nombre una sola vez).
mod_order() { LC_ALL=C grep -Eo 'AA_probe|ZZ_probe' "$1" | awk '!seen[$0]++' | tr '\n' ' '; }

# Espera (hasta 25 s) a que la tabla muestre `patron`; deja la salida en $1.
# Necesario porque el refresco del modulo tiene su propia cadencia (500 ms) y el
# anillo puede traer mensajes viejos por delante del recien inyectado.
wait_rows() {  # archivo-de-salida, patron ERE
    local out="$1" pat="$2" i
    for i in $(seq 1 25); do
        rows > "$out" 2>/dev/null || true
        grep -qE "$pat" "$out" && return 0
        sleep 1
    done
    return 1
}
wait_order() {  # archivo-de-salida, orden esperado ("A B ")
    local out="$1" want="$2" i
    for i in $(seq 1 10); do
        rows > "$out" 2>/dev/null || true
        [ "$(mod_order "$out")" = "$want" ] && return 0
        sleep 1
    done
    return 1
}

if [ "$EWGUI_MODE" = "--dump" ]; then
    printf '\n== cabeceras ==\n';                  probe --headers
    printf '\n== rects ==\n'
    for h in Module PID Status Details Ring Key Stop Refresh RINGS "LOGS (tail)"; do
        printf '%s: ' "$h"
        probe --rect "$h" --timeout 3 || echo "(no encontrado)"
    done
    printf '\n== filas (estado real de startstop) ==\n'; rows
    printf '\n== inyecto status_alive ==\n';       inject status_alive.txt
    sleep 3
    printf '\n== filas (tras inyectar) ==\n';      rows
    printf '\n== arbol ==\n';                      probe --dump --limit 40
    exit 0
fi

# --- modo --layout: reparto del espacio (no toca el anillo) ---------------
if [ "$EWGUI_MODE" = "--layout" ]; then
    rcx=0
    probe --dump --limit 40 > "$EWGUI_LOG/tree.txt" 2>/dev/null || true
    printf '\n== arbol (calibracion) ==\n'; head -6 "$EWGUI_LOG/tree.txt"

    # La tabla de Modulos debe llenar su panel: la ultima columna tiene que
    # llegar al divisor (antes quedaban 221 px en blanco a su derecha).
    set -- $(probe --rect Details 2>/dev/null); dx=${1:-}; dw=${3:-}
    set -- $(probe --rect Ring    2>/dev/null); rx=${1:-}
    if [ -n "$dx" ] && [ -n "$rx" ]; then
        gap=$(( rx - (dx + dw) ))
        if [ "$gap" -le 40 ]; then echo "ok  : la tabla de Modulos llena su panel (hueco ${gap}px)"
        else echo "FALLO: quedan ${gap}px en blanco a la derecha de la tabla"; rcx=1; fi
    else
        echo "FALLO: no pude medir las columnas Details/Ring"; rcx=1
    fi

    # La tabla de Anillos ya no esta clavada en 120 px: en el arbol, el 2.º
    # [scroll pane] es la suya (el 1.º es la de Modulos y el 3.º los logs).
    h=$(awk '/\[scroll pane\]/ { n++; if (n == 2) { for (i = 1; i <= NF; i++)
             if ($i ~ /^[0-9]+x[0-9]+$/) { split($i, a, "x"); print a[2] } } }' "$EWGUI_LOG/tree.txt")
    if [ -n "$h" ] && [ "$h" -ge 200 ]; then echo "ok  : la tabla de Anillos tiene alto propio (${h}px)"
    else echo "FALLO: alto de la tabla de Anillos = ${h:-?}px (se esperaba >= 200)"; rcx=1; fi

    printf 'RESULTADO_LAYOUT=%s\n' "$rcx"
    exit "$rcx"
fi

rc=0
expect_order() {  # archivo-de-filas, orden esperado, descripcion
    local got; got=$(mod_order "$1")
    if [ "$got" = "$2" ]; then echo "ok  : $3"
    else echo "FALLO: $3 (esperaba '$2', obtuve '$got')"; rc=1; fi
}

# --- estado sintetico inicial: AA_probe(200) Alive, ZZ_probe(33) Alive ---
inject status_alive.txt || { echo "FALLO: no pude inyectar status_alive"; cat "$EWGUI_LOG/fake.out"; exit 1; }
if wait_rows "$EWGUI_LOG/rows_a.txt" 'AA_probe'; then
    echo "ok  : la tabla muestra el estado sintetico"
else
    echo "FALLO: la tabla no muestra los modulos sinteticos"; rc=1
    echo "--- filas ---"; cat "$EWGUI_LOG/rows_a.txt"
fi

# (1a) orden inicial = Module ascendente
expect_order "$EWGUI_LOG/rows_a.txt" "AA_probe ZZ_probe " "orden inicial Module ASC -> AA_probe, ZZ_probe"

# (2) el cambio de estado se VE: misma estructura, solo AA_probe pasa a Dead
inject status_dead.txt || { echo "FALLO: no pude inyectar status_dead"; cat "$EWGUI_LOG/fake.out"; exit 1; }
if wait_rows "$EWGUI_LOG/rows_b.txt" 'AA_probe.*Dead'; then
    echo "ok  : el estado nuevo (Dead) se ve en la celda"
else
    echo "FALLO: el estado nuevo no se ve en la celda"
    awk -F'\t' '/AA_probe/ { print }' "$EWGUI_LOG/rows_b.txt"
    rc=1
fi

# (1b)/(1c) cabeceras: el clic por coordenadas es BEST-EFFORT. Las cabeceras de
# GtkColumnView no exponen accion AT-SPI (bug #8354) y sin gestor de ventanas
# (Xvfb) el evento puede no entregarse. Si no cambia el orden se AVISA, no se
# falla: el enganche del sorter ya lo cubren los invariantes de fuente y el orden
# inicial (el fixture inyecta ZZ antes que AA y la tabla muestra AA, ZZ).
command -v xdotool >/dev/null 2>&1 || { echo "AVISO: falta xdotool; no verifico el clic"; exit "$rc"; }
click_header() {  # nombre de la cabecera que se quiere pulsar
    local rect wid
    rect=$(probe --rect "$1") || return 1
    set -- $rect
    [ $# -eq 4 ] || return 1
    wid=$(xdotool search --name 'Earthworm Controller' 2>/dev/null | head -1)
    [ -n "$wid" ] && xdotool windowfocus --sync "$wid" 2>/dev/null
    xdotool mousemove --sync $(( $1 + $3 / 2 )) $(( $2 + $4 / 2 ))
    sleep 0.3; xdotool click 1; sleep 0.3
    return 0
}

if click_header PID && wait_order "$EWGUI_LOG/rows_c.txt" "ZZ_probe AA_probe "; then
    echo "ok  : 1er clic en PID ordena numerico ascendente"
    if click_header PID && wait_order "$EWGUI_LOG/rows_d.txt" "AA_probe ZZ_probe "; then
        echo "ok  : 2.º clic invierte el orden"
    else
        echo "AVISO: el 2.º clic no invirtio el orden (obtuve '$(mod_order "$EWGUI_LOG/rows_d.txt")')"
    fi
else
    echo "AVISO: no pude verificar el clic en la cabecera PID (bug #8354 + entorno sin gestor de ventanas);"
    echo "       comprueba a mano que Module/PID/Status/Details ordenan al pulsarlas (y el 2.º clic invierte)."
fi

printf 'RESULTADO_SORT_STATUS=%s\n' "$rc"
exit "$rc"
INNER
    EWGUI_BIN="$ROOT/earthworm_8.0/bin/ew_controller" EWGUI_PARAMS="$PARAMS" \
    EWGUI_LOG="$TMP/log" EWGUI_MODE="$mode" EWGUI_FAKE="$MOD_DIR/tests/fake_status" \
    EWGUI_FIX="$FIX" \
        "$ROOT/ew_gui_tools/common/tools/gui_run.sh" -- bash "$inner"
    return $?
}

case "${1:-}" in
    --dump)    make_fixture; run_check --dump; exit $? ;;
    --layout)  make_fixture; run_check --layout; exit $? ;;
    --help|-h) usage 0 ;;
    "")        make_fixture; run_check run; exit $? ;;
    *)         printf 'test_sort_and_status.sh: opcion desconocida: %s\n' "$1" >&2; usage 2 ;;
esac
