#!/bin/sh
# test_maximize.sh - fase 6.5: la ventana del port Qt sobrevive a 5 ciclos de
# maximizar/des-maximizar (equivalente Qt de test_maximize.sh del modulo GTK).
#
# Es un test VIVO: necesita DISPLAY (X11/XWayland), xdotool, el binario
# instalado y EarthWorm en marcha (el modulo es cliente de anillos).
# Sin cualquiera de ellos sale 0 (SKIP); solo falla si el proceso muere.
#
# Uso: sh tests/test_maximize.sh

set -u

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
params=$root/run_working_v8/params
bin=$root/ew_8.0/bin/csnhypodbpQT

if [ ! -x "$bin" ]; then
    echo "SKIP test_maximize: no existe $bin (make all)"
    exit 0
fi
if [ -z "${DISPLAY:-}" ]; then
    echo "SKIP test_maximize: sin DISPLAY"
    exit 0
fi
if ! command -v xdotool >/dev/null 2>&1; then
    echo "SKIP test_maximize: sin xdotool"
    exit 0
fi
if [ ! -f "$params/csnhypodbpqt.d" ]; then
    echo "SKIP test_maximize: falta $params/csnhypodbpqt.d"
    exit 0
fi

log=$(mktemp)
trap 'rm -f "$log"' EXIT INT TERM

# xcb: xdotool solo ve ventanas X11 (en WSLg el default puede ser Wayland).
( cd "$params" && QT_QPA_PLATFORM=xcb BPQ_EXIT_AFTER_MS=30000 \
    "$bin" csnhypodbpqt.d ) >"$log" 2>&1 &
pid=$!

sleep 4
if ! kill -0 "$pid" 2>/dev/null; then
    echo "SKIP test_maximize: el binario no arranco (¿EarthWorm parado o sin xcb?)"
    tail -5 "$log" 2>/dev/null | sed 's/^/       /'
    exit 0
fi

wid=$(xdotool search --pid "$pid" 2>/dev/null | head -1)
if [ -z "$wid" ]; then
    echo "SKIP test_maximize: no encontre la ventana (¿Wayland sin XWayland?)"
    kill "$pid" 2>/dev/null
    wait "$pid" 2>/dev/null || true
    exit 0
fi

rc=0
i=1
while [ "$i" -le 5 ]; do
    xdotool windowmaximize "$wid" 2>/dev/null
    sleep 1
    if ! kill -0 "$pid" 2>/dev/null; then
        echo "FALLA test_maximize: el proceso murio al maximizar (ciclo $i)"
        rc=1
        break
    fi
    xdotool windowunmaximize "$wid" 2>/dev/null
    sleep 1
    if ! kill -0 "$pid" 2>/dev/null; then
        echo "FALLA test_maximize: el proceso murio al des-maximizar (ciclo $i)"
        rc=1
        break
    fi
    i=$((i + 1))
done

kill "$pid" 2>/dev/null
wait "$pid" 2>/dev/null || true

if [ "$rc" -eq 0 ]; then
    echo "== maximize ciclos=5 vivo=1 =="
else
    tail -10 "$log" 2>/dev/null | sed 's/^/       /'
fi
exit "$rc"
