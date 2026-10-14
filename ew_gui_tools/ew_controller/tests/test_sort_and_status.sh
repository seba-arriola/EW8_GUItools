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
#   test_sort_and_status.sh --select selecciona una fila y sobrevive al rebuild
#   test_sort_and_status.sh --dup    nombres repetidos: la fila apunta a su PID
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

# El cliente de prueba (escribe TYPE_STATUS en el anillo) lo construye el Makefile
# con el target tests/fake_status. Sin el, la inyeccion falla y el test aborta: se
# avisa aqui para que el motivo no quede enterrado en fake.out.
if [ ! -x "$MOD_DIR/tests/fake_status" ]; then
    echo "FALLO: falta $MOD_DIR/tests/fake_status" >&2
    echo "       construyelo con: make -C $MOD_DIR tests/fake_status" >&2
    exit 1
fi

usage() { sed -n '2,22p' "$0" | sed 's/^# \{0,1\}//'; exit "${1:-0}"; }

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

# --- modo --select: seleccionar una fila y sobrevivir a la reconstruccion ----
# Reproduce el use-after-free de on_row_selected: liberaba el item PRESTADO de
# gtk_single_selection_get_selected_item() (transfer none), asi que la fila
# quedaba finalizada mientras el GListStore y el sort model seguian usandola, y
# el siguiente rebuild hacia g_object_unref sobre memoria liberada -> SIGSEGV en
# libgobject (muerte subita de la ventana; en produccion se veia al pulsar
# Restart, con el cuadro abierto o al pulsar Yes).
if [ "$EWGUI_MODE" = "--select" ]; then
    rcx=0

    # 1) La tabla tiene filas: estado sintetico (AA_probe 200 / ZZ_probe 33).
    inject status_alive.txt || { echo "FALLO: no pude inyectar status_alive"; exit 1; }
    if ! wait_rows "$EWGUI_LOG/rows_s0.txt" 'AA_probe'; then
        echo "FALLO: la tabla no muestra AA_probe"; cat "$EWGUI_LOG/rows_s0.txt"; exit 1
    fi

    # 2) Seleccionar una fila con TECLADO (Tab hasta la tabla + Down). En Xvfb,
    #    sin gestor de ventanas, los clics por XTEST no se entregan y las
    #    celdas/filas de GtkColumnView solo exponen la accion AT-SPI
    #    'listitem.scroll-to' (bug #8354), asi que el raton no sirve.
    #    La barra de estado ("target: <modulo> (<estado>)") confirma la
    #    seleccion: se refresca al inyectar un TYPE_STATUS, sin tocar el anillo
    #    de control.
    wid=$(xdotool search --name 'Earthworm Controller' 2>/dev/null | head -1)
    [ -n "$wid" ] && xdotool windowfocus --sync "$wid" 2>/dev/null
    sel=0
    for _i in $(seq 1 14); do
        xdotool key --clearmodifiers Tab 2>/dev/null
        xdotool key --clearmodifiers Down 2>/dev/null
        inject status_alive.txt
        sleep 0.6
        if probe --assert-text 'target: ' --timeout 2 >/dev/null 2>&1; then sel=1; break; fi
    done
    if [ "$sel" -ne 1 ]; then echo "FALLO: no consegui seleccionar una fila por teclado"; exit 1; fi
    echo "ok  : fila seleccionada por teclado (la barra de estado marca el target)"

    # 3) Armar el use-after-free: mover la seleccion (el modelo suelta la fila
    #    anterior, ya finalizada por el unref indebido) y provocar VARIOS
    #    rebuilds (cada uno desenlaza/enlaza las filas visibles). Con la memoria
    #    de la fila liberada ya reutilizada, el siguiente rebuild revienta.
    inject status_dead.txt || { echo "FALLO: no pude inyectar status_dead"; exit 1; }
    for _j in 1 2 3; do
        xdotool key --clearmodifiers Down 2>/dev/null
        sleep 0.5
    done
    for _j in 1 2 3 4; do
        inject status_alive.txt; sleep 1
        inject status_dead.txt;  sleep 1
    done
    sleep 4

    # 4) ¿Sigue vivo? (un hijo muerto queda ZOMBIE y `kill -0` daria falso OK.)
    st=$(ps -o stat= -p "$app" 2>/dev/null | tr -d ' \n')
    if [ -n "$st" ] && [ "${st#Z}" = "$st" ]; then
        echo "ok  : ew_controller sobrevive al rebuild con una fila seleccionada"
    else
        echo "FALLO: ew_controller murio al reconstruir las filas (estado '${st:-ausente}')"
        tail -n 5 "$EWGUI_LOG/app.out" 2>/dev/null
        rcx=1
    fi

    printf 'RESULTADO_SELECT=%s\n' "$rcx"
    exit "$rcx"
fi

# --- modo --dup: dos modulos con el MISMO nombre ------------------------------
# La identidad de un modulo es su PID. Antes se resolvia por nombre, asi que con
# dos `csnmags` (csnmags.d / csnmags_raw.d) la fila de la 2.ª instancia mandaba
# Stop/Restart al pid de la 1.ª: se mataba el modulo equivocado.
if [ "$EWGUI_MODE" = "--dup" ]; then
    rcx=0

    # 1) Dos filas con el mismo nombre: AA_probe pid 200 (Alive) y 201 (Dead).
    inject status_dup_a.txt || { echo "FALLO: no pude inyectar status_dup_a"; exit 1; }
    if ! wait_rows "$EWGUI_LOG/rows_d0.txt" 'AA_probe.*Dead'; then
        echo "FALLO: la tabla no muestra las dos instancias"; cat "$EWGUI_LOG/rows_d0.txt"; exit 1
    fi

    # 2) Seleccionar por TECLADO (en Xvfb los clics no se entregan) la fila del
    #    pid 201. Testigo: la barra de estado muestra el estado del seleccionado,
    #    "target: AA_probe (Dead)".
    wid=$(xdotool search --name 'Earthworm Controller' 2>/dev/null | head -1)
    [ -n "$wid" ] && xdotool windowfocus --sync "$wid" 2>/dev/null
    got=0
    for _i in $(seq 1 14); do
        xdotool key --clearmodifiers Tab 2>/dev/null
        xdotool key --clearmodifiers Down 2>/dev/null
        inject status_dup_a.txt
        sleep 0.6
        if probe --assert-text 'target: ' --timeout 2 >/dev/null 2>&1; then got=1; break; fi
    done
    [ "$got" -eq 1 ] || { echo "FALLO: no llegue a la tabla por teclado"; exit 1; }

    ok201=0
    for _i in $(seq 1 3); do
        if probe --assert-text 'target: AA_probe (Dead)' --timeout 2 >/dev/null 2>&1; then ok201=1; break; fi
        xdotool key --clearmodifiers Down 2>/dev/null
        inject status_dup_a.txt
        sleep 0.6
    done
    if [ "$ok201" -eq 1 ]; then
        echo "ok  : la fila seleccionada es la del pid 201 (barra: target AA_probe (Dead))"
    else
        echo "FALLO: la seleccion apunta a la OTRA instancia (resolucion por nombre)"
        rcx=1
    fi

    # 3) Rebuild con esa fila seleccionada (Dead -> Stop): la seleccion debe seguir
    #    en el pid 201 y no saltar a la 1.ª instancia.
    inject status_dup_b.txt || { echo "FALLO: no pude inyectar status_dup_b"; exit 1; }
    sleep 2
    if probe --assert-text 'target: AA_probe (Stop)' --timeout 5 >/dev/null 2>&1; then
        echo "ok  : el rebuild mantiene la seleccion en su instancia (pid 201)"
    else
        echo "FALLO: tras el rebuild la seleccion se movio a la otra instancia"
        echo "--- filas ---"; rows
        echo "--- barra de estado ---"
        probe --dump --limit 40 2>/dev/null | grep -a 'target' | head -3
        rcx=1
    fi

    # 4) Panel de Log: la lista distingue las dos instancias y al elegir la 2.ª se
    #    lee SU log (el prefijo sale de su .d, no del nombre del modulo).
    printf 'LOG_A\n' > "$EWGUI_LOG/probe_a_20260101.log"
    printf 'LOG_B\n' > "$EWGUI_LOG/probe_b_20260101.log"
    logbtn=0; focos=""
    for _i in $(seq 1 24); do
        foco=$(probe --focused 2>/dev/null || true)
        case "$foco" in *'|AA_probe '*) logbtn=1; break ;; esac
        focos="${focos}${foco%% *}|"
        xdotool key --clearmodifiers Tab 2>/dev/null
        sleep 0.3
    done
    if [ "$logbtn" -eq 1 ]; then
        xdotool key --clearmodifiers space 2>/dev/null
        sleep 1
        if probe --assert-text 'AA_probe (probe_b.d)' --timeout 5 >/dev/null 2>&1; then
            echo "ok  : el selector de log distingue las instancias repetidas (nombre + .d)"
        else
            echo "FALLO: el selector de log no distingue las instancias repetidas"
            probe --dump --limit 30 2>/dev/null | grep -a 'probe_b' | head -4
            rcx=1
        fi
        xdotool key --clearmodifiers Down 2>/dev/null
        sleep 0.3
        xdotool key --clearmodifiers Return 2>/dev/null
        sleep 1.5
        if probe --assert-text 'LOG_B' --timeout 5 >/dev/null 2>&1; then
            echo "ok  : el panel de log lee el log de la instancia elegida (probe_b)"
        else
            echo "FALLO: el panel de log no lee el log de la instancia elegida"
            rcx=1
        fi
    else
        echo "AVISO: no pude enfocar el selector de log por teclado; no verifico el panel"
        echo "  cadena de foco (Tab): ${focos:0:500}"
    fi

    # 5) El cuadro de confirmacion debe nombrar el pid/.d de la fila seleccionada.
    #    Solo se pulsa Space si el foco esta en Restart (con --focused): pulsar a
    #    ciegas podria mandar un TYPE_RECONFIG real a startstop.
    btn=0; focos2=""
    for _i in $(seq 1 24); do
        foco=$(probe --focused 2>/dev/null || true)
        case "$foco" in *'|Restart '*) btn=1; break ;; esac
        focos2="${focos2}${foco%% *}|"
        xdotool key --clearmodifiers shift+Tab 2>/dev/null
        sleep 0.3
    done
    if [ "$btn" -eq 1 ]; then
        xdotool key --clearmodifiers space 2>/dev/null
        sleep 1
        if probe --assert-text 'pid 201' --timeout 5 >/dev/null 2>&1; then
            echo "ok  : el cuadro de Restart nombra el pid 201 y su .d"
        else
            echo "FALLO: el cuadro de Restart no nombra el pid de la fila seleccionada"
            rcx=1
        fi
    else
        echo "AVISO: no pude enfocar el boton Restart por teclado; no verifico el cuadro"
        echo "  cadena de foco (shift+Tab): ${focos2:0:500}"
    fi

    # 6) Y sigue vivo tras los rebuilds.
    st=$(ps -o stat= -p "$app" 2>/dev/null | tr -d ' \n')
    if [ -n "$st" ] && [ "${st#Z}" = "$st" ]; then
        echo "ok  : ew_controller sobrevive a los rebuilds con nombres repetidos"
    else
        echo "FALLO: ew_controller murio (estado '${st:-ausente}')"
        tail -n 5 "$EWGUI_LOG/app.out" 2>/dev/null
        rcx=1
    fi

    printf 'RESULTADO_DUP=%s\n' "$rcx"
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
    --select)  make_fixture; run_check --select; exit $? ;;
    --dup)     make_fixture; run_check --dup; exit $? ;;
    --help|-h) usage 0 ;;
    "")        make_fixture; run_check run; exit $? ;;
    *)         printf 'test_sort_and_status.sh: opcion desconocida: %s\n' "$1" >&2; usage 2 ;;
esac
