#!/usr/bin/env bash
# gui_check_residual.sh - valida con AT-SPI que la tabla de Ondas muestra los
# residuales de la solucion elegida SIN pulsar locate.
#
# Corre contra el EarthWorm EN MARCHA (solo LEE anillos: no arranca ni detiene
# nada) y con una copia AISLADA de params en $TMP, un EW_LOG propio y un archivo
# de estado sintetico (StateFile) con un evento y sus picadas, cada una con su
# residual. No toca run_working_v8/params.
#
#   gui_check_residual.sh --dump   vuelca el arbol accesible (calibracion)
#   gui_check_residual.sh          corre las aserciones
set -u

MOD_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT=$(cd "$MOD_DIR/../.." && pwd)
TMP=${EWGUI_GUI_TMP:-/tmp/opencode/gui}
PARAMS="$TMP/params"
STATE="$TMP/csnloc.events"

usage() { sed -n '2,12p' "$0" | sed 's/^# \{0,1\}//'; exit "${1:-0}"; }

# Estaciones reales de estaciones_107.txt (la tabla se arma desde StaFile, asi
# que las picadas sinteticas tienen que ser de estaciones que existan).
make_fixture() {
    rm -rf "$TMP"; mkdir -p "$PARAMS" "$TMP/log"
    cp -a "$ROOT/run_working_v8/params/." "$PARAMS/" 2>/dev/null || true
    sed -i -e "s|^StateFile.*|StateFile       $STATE|" \
           -e "/^DbEnvFile/d" "$PARAMS/csnhypodbp.d"
    cat > "$STATE" <<'EOF'
E 4242 3 1600000000.000 -33.1000 -70.2000 25.0 3 0.42 120.0 77.0 0.0 1 1600000000.0 1 1 0 1600000000.0
P TA02 C1 HHZ -- 0 1600000010.000 0 0.250 0
P TA02 C1 HHE -- 1 1600000030.000 0 -0.300 0
P PB01 CX HHZ -- 0 1600000012.000 0 0.110 0
EOF
}

run_check() {
    local mode="$1" inner="$TMP/inner.sh"
    # Entorno EarthWorm (EW_HOME/EW_VERSION/EW_INSTALLATION/PATH) y luego params y
    # log AISLADOS: el modulo nunca ve run_working_v8/params ni sus logs.
    # shellcheck disable=SC1091
    . "$ROOT/ew8_unix.sh" >/dev/null
    export EW_PARAMS="$PARAMS" EW_LOG="$TMP/log"
    cat > "$inner" <<'INNER'
set -u
cd "$EWGUI_PARAMS" || exit 1
"$EWGUI_BIN" csnhypodbp.d > "$EWGUI_LOG/app.out" 2>&1 &
app=$!
trap 'kill "$app" 2>/dev/null' EXIT
sleep 8
if ! kill -0 "$app" 2>/dev/null; then
    echo "FALLO: csnhypodbp no arranco (¿EarthWorm arriba? ¿anillos?)"
    tail -n 8 "$EWGUI_LOG/app.out"
    exit 1
fi

if [ "$EWGUI_MODE" = "--dump" ]; then
    python3 "$EWGUI_PROBE" --click-by-name Ondas --timeout 25 >/dev/null 2>&1 || true
    printf '\n== filas de la tabla ==\n'
    python3 "$EWGUI_PROBE" --rows --limit 12
    printf '\n== arbol ==\n'
    python3 "$EWGUI_PROBE" --dump --limit 7
    exit 0
fi

rc=0
printf '== pestaña Ondas ==\n'
python3 "$EWGUI_PROBE" --click-by-name Ondas --timeout 25 || rc=1
printf '== residuales de la solucion elegida (sin pulsar locate) ==\n'
python3 "$EWGUI_PROBE" --assert-text "+0.250" --timeout 25 || rc=1
python3 "$EWGUI_PROBE" --assert-text "-0.300" --timeout 25 || rc=1
python3 "$EWGUI_PROBE" --assert-text "+0.110" --timeout 25 || rc=1
printf 'RESULTADO_RESIDUAL=%s\n' "$rc"
exit "$rc"
INNER
    EWGUI_BIN="$ROOT/earthworm_8.0/bin/csnhypodbp" EWGUI_PARAMS="$PARAMS" \
    EWGUI_LOG="$TMP/log" EWGUI_MODE="$mode" \
        "$ROOT/ew_gui_tools/common/tools/gui_run.sh" -- bash "$inner"
    return $?
}

case "${1:-}" in
    --dump)   make_fixture; run_check --dump; exit $? ;;
    --help|-h) usage 0 ;;
    "")       make_fixture; run_check run; exit $? ;;
    *)        printf 'gui_check_residual.sh: opcion desconocida: %s\n' "$1" >&2; usage 2 ;;
esac
