#!/usr/bin/env bash
# gui_run.sh - arnes de validacion GUI para los modulos GTK4 (Xvfb + D-Bus + AT-SPI).
#
#   gui_run.sh --check               verifica dependencias (sale !=0 si falta alguna)
#   gui_run.sh --selftest            prueba el arnes completo contra una app GTK4 minima
#   gui_run.sh -- <comando> [args]   corre <comando> dentro del display virtual
#
# El comando corre con GDK_BACKEND=x11 GSK_RENDERER=cairo GTK_A11Y=atspi dentro de un
# bus de sesion propio (AT-SPI lo necesita) y un Xvfb efimero. NO toca la configuracion
# real de EarthWorm ni los params de run_working_v8: el llamante decide que params usar.
#
# Dentro de la sesion quedan exportados, para los scripts que se pasen:
#   EWGUI_PROBE      ruta de gui_probe.py
#   EWGUI_TOOLS_DIR  ruta de este directorio
set -u

SIZE=${EWGUI_GUI_SIZE:-1600x1000}
TOOLS_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
PROBE="$TOOLS_DIR/gui_probe.py"
SELFTEST_APP="$TOOLS_DIR/gui_selftest_app.py"

usage() { sed -n '2,14p' "$0" | sed 's/^# \{0,1\}//'; exit "${1:-0}"; }

check_deps() {
    local miss=0 b
    for b in Xvfb xvfb-run dbus-run-session; do
        if command -v "$b" >/dev/null 2>&1; then
            printf 'ok       %s (%s)\n' "$b" "$(command -v "$b")"
        else
            printf 'FALTA    %s\n' "$b"
            miss=$((miss + 1))
        fi
    done
    for b in xdotool xdpyinfo xwd import; do
        if command -v "$b" >/dev/null 2>&1; then
            printf 'ok       %s (%s)\n' "$b" "$(command -v "$b")"
        else
            printf 'opcional %s (sin el: no hay clic por coordenadas ni capturas)\n' "$b"
        fi
    done
    if python3 -c 'import pyatspi' >/dev/null 2>&1; then
        printf 'ok       python3+pyatspi\n'
    else
        printf 'FALTA    python3+pyatspi (apt install python3-pyatspi)\n'
        miss=$((miss + 1))
    fi
    if [ "$miss" -gt 0 ]; then
        printf '\n%d dependencia(s) obligatoria(s) ausente(s): el arnes NO puede correr.\n' "$miss"
        return 1
    fi
    printf '\nTodo lo obligatorio esta presente.\n'
    return 0
}

# Corre un script interno (el que arma el modo pedido) en la sesion aislada.
run_inner() {
    local inner="$1"; shift
    EWGUI_PROBE="$PROBE" EWGUI_TOOLS_DIR="$TOOLS_DIR" \
        dbus-run-session -- xvfb-run -a --server-args="-screen 0 ${SIZE}x24" \
        bash "$inner" "$@"
    return $?
}

run_selftest() {
    local tmp rc
    tmp=$(mktemp -d "${TMPDIR:-/tmp}/ewgui-selftest.XXXXXX") || return 1
    cat > "$tmp/inner.sh" <<'INNER'
set -u
export GDK_BACKEND=x11 GSK_RENDERER=cairo GTK_A11Y=atspi
rc=0
python3 "$EWGUI_TOOLS_DIR/gui_selftest_app.py" & app=$!
trap 'kill "$app" 2>/dev/null' EXIT
sleep 1

printf '\n[1/3] AT-SPI: leer texto y pulsar por accion\n'
python3 "$EWGUI_PROBE" --click-by-name selftest-target \
    --assert-text selftest-clicked-1 --timeout 10 || rc=1

printf '\n[2/3] AT-SPI: geometria del widget\n'
rect=$(python3 "$EWGUI_PROBE" --rect selftest-target) || rc=1
printf 'rect=%s\n' "$rect"
set -- $rect
if [ "$#" -eq 4 ]; then
    x=$(( $1 + $3 / 2 )); y=$(( $2 + $4 / 2 ))
    printf '\n[3/3] xdotool: clic real en %s,%s\n' "$x" "$y"
    xdotool mousemove --sync "$x" "$y" click 1
    python3 "$EWGUI_PROBE" --assert-text selftest-clicked-2 --timeout 10 || rc=1
else
    printf 'SIN GEOMETRIA: se omite el clic por coordenadas\n'
    rc=1
fi

printf '\nRESULTADO_SELFTEST=%s\n' "$rc"
exit "$rc"
INNER
    run_inner "$tmp/inner.sh"
    rc=$?
    rm -rf "$tmp"
    return $rc
}

run_cmd() {
    local tmp rc
    tmp=$(mktemp -d "${TMPDIR:-/tmp}/ewgui-run.XXXXXX") || return 1
    cat > "$tmp/inner.sh" <<'INNER'
set -u
export GDK_BACKEND=x11 GSK_RENDERER=cairo GTK_A11Y=atspi
exec "$@"
INNER
    run_inner "$tmp/inner.sh" "$@"
    rc=$?
    rm -rf "$tmp"
    return $rc
}

case "${1:-}" in
    --check)    check_deps; exit $? ;;
    --selftest) run_selftest; exit $? ;;
    --help|-h)  usage 0 ;;
    --)         shift; [ "$#" -gt 0 ] || usage 2; run_cmd "$@"; exit $? ;;
    "")         usage 2 ;;
    *)          printf 'gui_run.sh: opcion desconocida: %s\n' "$1" >&2; usage 2 ;;
esac
