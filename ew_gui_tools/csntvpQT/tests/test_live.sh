#!/bin/sh
# Arranque real de csntvpQT contra el EarthWorm en marcha (necesita DISPLAY).
# Solo comprueba que abre, late y sale solo: el resto lo cubre `make check`.
set -u

BIN="${BIN:-./csntvpQT}"
PARAMS="${PARAMS:-$(cd "$(dirname "$0")/../../.." && pwd)/run_working_v8/params}"

# El binario se resuelve a ruta ABSOLUTA antes de entrar en params/ (si no,
# `./csntvpQT` deja de existir tras el cd y el test sale 127).
case "$BIN" in
    /*) ;;
    *) BIN="$(cd "$(dirname "$BIN")" && pwd)/$(basename "$BIN")" ;;
esac

if [ -z "${DISPLAY:-}" ] && [ -z "${WAYLAND_DISPLAY:-}" ]; then
    echo "SKIP: sin DISPLAY/WAYLAND_DISPLAY"
    exit 0
fi

if [ ! -f "$PARAMS/csntvpqt.d" ]; then
    echo "SKIP: falta $PARAMS/csntvpqt.d"
    exit 0
fi

cd "$PARAMS" || exit 1

CSNTVPQT_EXIT_AFTER_MS=3000 /usr/bin/timeout 15 "$BIN" csntvpqt.d
rc=$?

case "$rc" in
    0|124)
        echo "OK: csntvpQT arranco y salio solo (rc=$rc)"
        exit 0
        ;;
    *)
        echo "FAIL: csntvpQT salio con rc=$rc"
        exit 1
        ;;
esac
