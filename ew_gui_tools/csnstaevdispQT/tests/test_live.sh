#!/bin/sh
# Prueba live: abre la ventana real unos segundos. Requiere DISPLAY y que el
# anillo HYPO_RING exista (EarthWorm arriba). Se salta si no hay sesion.
set -u

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
PARAMS="$ROOT/run_working_v8/params"
BIN="$ROOT/ew_gui_tools/csnstaevdispQT/csnstaevdispQT"

if [ -z "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]; then
    echo "SKIP: sin DISPLAY/WAYLAND_DISPLAY"
    exit 0
fi
if [ ! -x "$BIN" ]; then
    echo "SKIP: falta $BIN"
    exit 0
fi

cd "$PARAMS" || exit 1
CSNSTAEVDISPQT_EXIT_AFTER_MS=3000 /usr/bin/timeout 15 "$BIN" csnstaevdisp.d
RC=$?
if [ "$RC" -eq 0 ] || [ "$RC" -eq 124 ]; then
    echo "OK: la ventana sobrevivio el arranque"
    exit 0
fi
echo "FAIL: rc=$RC (¿EarthWorm/anillo arriba?)"
exit 1
