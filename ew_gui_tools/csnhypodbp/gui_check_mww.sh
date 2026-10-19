#!/usr/bin/env bash
# gui_check_mww.sh - valida con AT-SPI que la pestana *magnitudes* tiene el panel
# `Mww` (inversion de fase W): el selector de la pila, la tabla de parametros, los
# botones `Calculate`/`Cancel` y el visor de la salida completa.
#
# Corre contra el EarthWorm EN MARCHA (solo LEE anillos: no arranca ni detiene
# nada) y con una copia AISLADA de params en $TMP y un EW_LOG propio. NO toca
# run_working_v8/params. NO pulsa `Calculate`: eso correria el motor de fase W de
# verdad (minutos) y este arnes es de UI.
#
#   gui_check_mww.sh --dump   vuelca el arbol accesible (calibracion)
#   gui_check_mww.sh          corre las aserciones
set -u

MOD_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT=$(cd "$MOD_DIR/../.." && pwd)
TMP=${EWGUI_GUI_TMP:-/tmp/opencode/gui_mww}
PARAMS="$TMP/params"

usage() { sed -n '2,12p' "$0" | sed 's/^# \{0,1\}//'; exit "${1:-0}"; }

make_fixture() {
    rm -rf "$TMP"; mkdir -p "$PARAMS" "$TMP/log"
    cp -a "$ROOT/run_working_v8/params/." "$PARAMS/" 2>/dev/null || true
    # Sin BD (el historial no hace falta para mirar la UI del panel) y con el
    # .d de fase W y su directorio de corridas apuntando al temporal.
    sed -i -e "/^DbEnvFile/d" \
           -e "s|^MwwWorkDir.*|MwwWorkDir     $TMP/runs|" "$PARAMS/csnhypodbp.d"
    if ! grep -q '^MwwConfigFile' "$PARAMS/csnhypodbp.d"; then
        printf 'MwwConfigFile  csnwphase.d\n' >> "$PARAMS/csnhypodbp.d"
    fi
    if [ ! -f "$PARAMS/csnwphase.d" ]; then
        echo "AVISO: no hay csnwphase.d en $PARAMS (el panel avisara de la falta de config)"
    fi
}

run_check() {
    local mode="$1" inner="$TMP/inner.sh"
    # shellcheck disable=SC1091
    . "$ROOT/ew8_unix.sh" >/dev/null
    export EW_PARAMS="$PARAMS" EW_LOG="$TMP/log"
    cat > "$inner" <<'INNER'
set -u
cd "$EWGUI_PARAMS" || exit 1
"$EWGUI_BIN" csnhypodbp.d > "$EWGUI_LOG/app.out" 2>&1 &
app=$!
trap 'kill "$app" 2>/dev/null' EXIT
sleep "${EWGUI_SLEEP:-8}"
if ! kill -0 "$app" 2>/dev/null; then
    echo "FALLO: csnhypodbp no arranco (¿EarthWorm arriba? ¿anillos?)"
    tail -n 8 "$EWGUI_LOG/app.out"
    exit 1
fi

if [ "$EWGUI_MODE" = "--shot" ]; then
    # Clics por coordenada (EWGUI_CLICK="x,y;x,y") antes de la captura: con el
    # puente AT-SPI caido es la unica forma de llegar a una pestana concreta.
    if [ -n "${EWGUI_CLICK:-}" ]; then
        # Activar la ventana antes de clicar: sin foco los eventos XTest no llegan
        # a la pestana (medido en este entorno).
        wid=$(xdotool search --name CSnhypodbp 2>/dev/null | head -1)
        if [ -n "$wid" ]; then
            xdotool windowactivate --sync "$wid" 2>/dev/null || true
        fi
        oldifs=$IFS; IFS=';'
        for xy in $EWGUI_CLICK; do
            IFS=$oldifs
            x=${xy%,*}; y=${xy#*,}
            echo "click $x,$y"
            xdotool mousemove "$x" "$y" click 1 || echo "aviso: xdotool fallo en $x,$y"
            sleep 2
            IFS=';'
        done
        IFS=$oldifs
        sleep 2
    fi
    # Captura del display virtual: sirve para revisar la UI cuando el puente
    # AT-SPI no expone la app (pasa en este entorno).
    if command -v import >/dev/null 2>&1; then
        import -window root "$EWGUI_LOG/${EWGUI_SHOT_NAME:-shot}.png" 2>/dev/null || true
    fi
    if [ ! -s "$EWGUI_LOG/${EWGUI_SHOT_NAME:-shot}.png" ] && command -v xwd >/dev/null 2>&1; then
        xwd -root -silent > "$EWGUI_LOG/${EWGUI_SHOT_NAME:-shot}.xwd" 2>/dev/null || true
    fi
    echo "captura: $(ls -la "$EWGUI_LOG"/${EWGUI_SHOT_NAME:-shot}.* 2>/dev/null | head -2)"
    exit 0
fi

if [ "$EWGUI_MODE" = "--dump" ]; then
    python3 "$EWGUI_PROBE" --click-by-name Procesar --timeout 25 >/dev/null 2>&1 || true
    python3 "$EWGUI_PROBE" --click-by-name magnitudes --timeout 25 >/dev/null 2>&1 || true
    printf '\n== arbol (pagina magnitudes) ==\n'
    python3 "$EWGUI_PROBE" --dump --limit 8
    exit 0
fi

rc=0
printf '== Procesar -> magnitudes ==\n'
python3 "$EWGUI_PROBE" --click-by-name Procesar --timeout 25 || rc=1
python3 "$EWGUI_PROBE" --click-by-name magnitudes --timeout 25 || rc=1
printf '== el area central se cambia al panel Mww ==\n'
python3 "$EWGUI_PROBE" --click-by-name Mww --timeout 25 || rc=1
printf '== el panel Mww esta armado ==\n'
python3 "$EWGUI_PROBE" --assert-text "Parámetros de la inversión de fase W" --timeout 25 || rc=1
python3 "$EWGUI_PROBE" --assert-text "Calculate" --timeout 25 || rc=1
python3 "$EWGUI_PROBE" --assert-text "Cancel" --timeout 25 || rc=1
python3 "$EWGUI_PROBE" --assert-text "Resultados de la inversión" --timeout 25 || rc=1
python3 "$EWGUI_PROBE" --assert-text "Mecanismo focal" --timeout 25 || rc=1
python3 "$EWGUI_PROBE" --assert-text "Salida completa del motor" --timeout 25 || rc=1
python3 "$EWGUI_PROBE" --assert-text "sin resultado" --timeout 25 || rc=1
# La tabla de parametros es editable y ya NO hay boton de guardado: el mecanismo
# focal se persiste al confirmar la solución (radio `Mww` del preview).
python3 "$EWGUI_PROBE" --assert-text "magnitud semilla" --timeout 25 || rc=1
python3 "$EWGUI_PROBE" --assert-text "ventana de ondas pre" --timeout 25 || rc=1
# DMIN/DMAX son dos filas editables mas, en cajas de texto.
python3 "$EWGUI_PROBE" --assert-text "DMIN [deg]" --timeout 25 || rc=1
python3 "$EWGUI_PROBE" --assert-text "DMAX [deg]" --timeout 25 || rc=1
if python3 "$EWGUI_PROBE" --assert-text "Save to DB" --timeout 10 >/dev/null 2>&1; then
    printf 'FALLO: sigue el boton Save to DB\n'; rc=1
fi
printf '== se vuelve a Estaciones ==\n'
python3 "$EWGUI_PROBE" --click-by-name Estaciones --timeout 25 || rc=1
printf 'RESULTADO_MWW=%s\n' "$rc"
exit "$rc"
INNER
    EWGUI_BIN="$ROOT/ew_8.0/bin/csnhypodbp" EWGUI_PARAMS="$PARAMS" \
    EWGUI_LOG="$TMP/log" EWGUI_MODE="$mode" EWGUI_SLEEP="${EWGUI_SLEEP:-8}" \
    EWGUI_CLICK="${EWGUI_CLICK:-}" EWGUI_SHOT_NAME="${EWGUI_SHOT_NAME:-shot}" \
        "$ROOT/ew_gui_tools/common/tools/gui_run.sh" -- bash "$inner"
    return $?
}

case "${1:-}" in
    --dump)   make_fixture; run_check --dump; exit $? ;;
    --shot)   make_fixture; run_check --shot; exit $? ;;
    --help|-h) usage 0 ;;
    "")       make_fixture; run_check run; exit $? ;;
    *)        printf 'gui_check_mww.sh: opcion desconocida: %s\n' "$1" >&2; usage 2 ;;
esac
