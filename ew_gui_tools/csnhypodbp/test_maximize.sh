#!/bin/sh
# Prueba LIVE del cierre al maximizar (AGENTS §7).
#
# Lanza el modulo REAL con el hook `CSNHYPODBP_MAXIMIZE_MS` (5 ciclos de
# maximizar/des-maximizar, termina maximizado) y comprueba que el proceso sigue
# VIVO y que no aparece `Gdk-Message: ... Error 71 (Protocol error)`.
#
# Necesita: el entorno cargado (`source ./ew8_unix.sh`), DISPLAY (es un modulo
# GUI) y EarthWorm arriba (es cliente de anillos). Si falta cualquiera de los
# tres, o el modulo no llega a arrancar, se SALTA (rc 0): no es un FAIL.
#
# El log del modulo va a un EW_LOG temporal: no toca run_working_v8/log.
set -u
cd "$(dirname "$0")" || exit 1

ROOT=$(cd ../.. && pwd)
PARAMS="$ROOT/run_working_v8/params"
BIN="$ROOT/ew_8.0/bin/csnhypodbp"
INTERVAL_MS=${CSNHYPODBP_MAXIMIZE_MS_TEST:-300}   # 10 toggles = 3 s
WAIT_S=6                                          # 3 s de ciclos + margen

skip() { echo "SKIP: $1"; exit 0; }

[ -n "${EW_PARAMS:-}" ] || skip "falta el entorno (source ./ew8_unix.sh)"
[ -x "$BIN" ] || skip "falta el binario instalado $BIN"
[ -f "$PARAMS/csnhypodbp.d" ] || skip "falta $PARAMS/csnhypodbp.d"
[ -n "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ] || skip "sin DISPLAY (modulo GUI)"

OUT=$(mktemp /tmp/opencode/csnhypodbp_maximize.XXXXXX.log)
EWLOG=$(mktemp -d /tmp/opencode/csnhypodbp_maxlog.XXXXXX)

( cd "$PARAMS" && EW_LOG="$EWLOG" CSNHYPODBP_MAXIMIZE_MS="$INTERVAL_MS" \
      "$BIN" csnhypodbp.d ) >"$OUT" 2>&1 &
PID=$!

sleep 2
if ! kill -0 "$PID" 2>/dev/null; then
    kill "$PID" 2>/dev/null
    wait "$PID" 2>/dev/null
    echo "SKIP: el modulo no arranco (¿EarthWorm abajo?); log: $OUT"
    tail -3 "$OUT"
    exit 0
fi

sleep $((WAIT_S - 2))
if kill -0 "$PID" 2>/dev/null; then alive=1; else alive=0; fi
kill "$PID" 2>/dev/null
wait "$PID" 2>/dev/null

if [ "$alive" -eq 1 ] && ! grep -q "Error 71" "$OUT"; then
    echo "ok  : la ventana sobrevivio a 5 ciclos de maximizar/des-maximizar"
    echo "      log: $OUT"
    exit 0
fi

echo "FAIL: la ventana NO sobrevivio (alive=$alive, interval=${INTERVAL_MS}ms)"
grep -nE "Error 71|Protocol error|xdg_surface|does not match" "$OUT" | head -5
echo "      log completo: $OUT"
exit 1
