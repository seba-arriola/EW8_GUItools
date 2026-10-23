#!/bin/sh
# test_layout.sh - C15: la ventana no infla su minimo (trampa de WSLg: si el
# minimo no cabe en el area maximizada, GDK/GDK-Qt matan el proceso con
# Error 71) y el reparto deja sitio a las dos columnas.
#
# Gate del modulo GTK equivalente: gap Details->Ring <= 40 px y el panel de
# anillos >= 200 px de alto.
set -u

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
params=$root/run_working_v8/params
bin=$here/../ew_controllerQT
[ -x "$bin" ] || bin=$root/ew_8.0/bin/ew_controllerQT

if [ ! -x "$bin" ]; then
    echo "FALLA no existe $bin (make -C ew_gui_tools/ew_controllerQT)"
    exit 1
fi

out=$(cd "$params" && \
      EW_PARAMS="$params" QT_QPA_PLATFORM=offscreen ECQ_MODE=--layout \
      /usr/bin/timeout 30 "$bin" ew_controllerqt.d 2>/dev/null)
rc=$?

fail=0
ok()  { echo "ok   $*"; }
bad() { echo "FALLA $*"; fail=$((fail + 1)); }

metric() {  # metric <nombre>
    printf '%s\n' "$out" | awk -v k="metric $1" '$1" "$2==k {print $3}'
}

if [ "$rc" -ne 0 ]; then
    bad "--layout devolvio rc=$rc"
    printf '%s\n' "$out" | tail -5
    exit 1
fi

gap=$(metric gap_details_ring)
rings=$(metric rings_height)
minw=$(metric window_min_width)
minh=$(metric window_min_height)

[ -n "$gap" ] && [ "$gap" -ge 0 ] && [ "$gap" -le 40 ] \
    && ok "gap Details->Ring = ${gap}px (<= 40)" \
    || bad "gap Details->Ring = '${gap}' (esperado 0..40)"

[ -n "$rings" ] && [ "$rings" -ge 200 ] \
    && ok "alto del panel de anillos = ${rings}px (>= 200)" \
    || bad "alto del panel de anillos = '${rings}' (esperado >= 200)"

[ -n "$minw" ] && [ "$minw" -le 700 ] \
    && ok "ancho minimo de la ventana = ${minw}px (<= 700)" \
    || bad "ancho minimo de la ventana = '${minw}' (esperado <= 700)"

[ -n "$minh" ] && [ "$minh" -le 640 ] \
    && ok "alto minimo de la ventana = ${minh}px (<= 640)" \
    || bad "alto minimo de la ventana = '${minh}' (esperado <= 640)"

exit "$fail"
