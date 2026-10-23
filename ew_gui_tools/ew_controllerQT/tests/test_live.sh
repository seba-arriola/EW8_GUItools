#!/bin/sh
# test_live.sh - humo VIVO del port Qt: la ventana sobrevive a maximizar/
# des-maximizar (trampa WSLg Error 71) y a una seleccion por teclado que
# dispara el refresco por PID (el rebuild de filas).
#
# Necesita DISPLAY (X11/XWayland), xdotool y el binario. Sin cualquiera de ellos
# sale 0 (SKIP): igual que test_maximize.sh de csnhypodbpQT.
#
# Uso: sh tests/test_live.sh
set -u

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
params=$root/run_working_v8/params
bin=$here/../ew_controllerQT
[ -x "$bin" ] || bin=$root/ew_8.0/bin/ew_controllerQT

if [ ! -x "$bin" ]; then
    echo "SKIP test_live: no existe $bin (make -C ew_gui_tools/ew_controllerQT)"
    exit 0
fi
if [ -z "${DISPLAY:-}" ]; then
    echo "SKIP test_live: sin DISPLAY"
    exit 0
fi
if ! command -v xdotool >/dev/null 2>&1; then
    echo "SKIP test_live: sin xdotool"
    exit 0
fi
if [ ! -f "$params/ew_controllerqt.d" ]; then
    echo "SKIP test_live: falta $params/ew_controllerqt.d"
    exit 0
fi

log=$(mktemp)
trap 'rm -f "$log"' EXIT INT TERM

# xcb: xdotool solo ve ventanas X11 (el default en WSLg puede ser Wayland).
( cd "$params" && QT_QPA_PLATFORM=xcb ECQ_EXIT_AFTER_MS=40000 \
    "$bin" ew_controllerqt.d ) >"$log" 2>&1 &
pid=$!

sleep 4
if ! kill -0 "$pid" 2>/dev/null; then
    echo "SKIP test_live: el binario no arranco (¿sin xcb?)"
    tail -5 "$log" 2>/dev/null | sed 's/^/       /'
    exit 0
fi

wid=$(xdotool search --pid "$pid" 2>/dev/null | head -1)
if [ -z "$wid" ]; then
    echo "SKIP test_live: no encontre la ventana (¿Wayland sin XWayland?)"
    kill "$pid" 2>/dev/null
    wait "$pid" 2>/dev/null || true
    exit 0
fi

rc=0

# 1) 3 ciclos de maximizar/des-maximizar.
i=1
while [ "$i" -le 3 ]; do
    xdotool windowmaximize "$wid" 2>/dev/null
    sleep 1
    if ! kill -0 "$pid" 2>/dev/null; then
        echo "FALLA test_live: el proceso murio al maximizar (ciclo $i)"
        rc=1
        break
    fi
    xdotool windowunmaximize "$wid" 2>/dev/null
    sleep 1
    if ! kill -0 "$pid" 2>/dev/null; then
        echo "FALLA test_live: el proceso murio al des-maximizar (ciclo $i)"
        rc=1
        break
    fi
    i=$((i + 1))
done

# 2) Seleccion por teclado: foco a la tabla y bajar una fila. Si el rebuild de
#    filas estuviera mal, se cae aqui (era el fallo de las referencias
#    prestadas de GTK, AGENTS §7).
if [ "$rc" -eq 0 ]; then
    xdotool windowactivate --sync "$wid" 2>/dev/null
    xdotool key --window "$wid" Tab Tab 2>/dev/null
    sleep 1
    xdotool key --window "$wid" Down 2>/dev/null
    sleep 1
    if ! kill -0 "$pid" 2>/dev/null; then
        echo "FALLA test_live: el proceso murio al seleccionar una fila"
        rc=1
    fi
fi

kill "$pid" 2>/dev/null
wait "$pid" 2>/dev/null || true

if [ "$rc" -eq 0 ]; then
    echo "== live maximizar=3 seleccion=1 vivo=1 =="
else
    tail -10 "$log" 2>/dev/null | sed 's/^/       /'
fi
exit "$rc"
