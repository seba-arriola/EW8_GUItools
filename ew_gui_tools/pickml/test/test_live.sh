#!/bin/sh
# test_live.sh — prueba del camino vivo de publicacion de pickml.
#
# NO arranca EarthWorm (eso es del usuario): si el stack no esta arriba, SKIP.
# Con el stack arriba:
#   1. Exige PICK_RING/LEGACY_PICK_RING en earthworm.d (config coherente).
#   2. Corre `pickml --ring --publish-dry-run` 30 s con el .d real: valida el
#      fail-fast, el attach del anillo de entrada y la politica de publicacion
#      SIN escribir en PICK_RING (no puede contaminar csnloc).
#   3. Comprueba que el log tiene la linea de arranque con "publica" y que el
#      resumen trae los contadores pub_*.
#
# El envío real (tport_putmsg) se valida con `sniffring -n PICK_RING` en una
# corrida normal y con `PublishMaxWeight`; ver docs/DL_to-do.md §2.5.

set -e

cd "$(dirname "$0")/.."
. ../../ew8_unix.sh >/dev/null 2>&1 || true

D=${1:-pickml_phasenetwc.d}
PARAMS=${EW_PARAMS:-../../run_working_v8/params}

skip() { echo "test_live: SKIP ($*)"; exit 0; }

[ -n "${EW_HOME:-}" ] || skip "sin entorno EarthWorm (source ./ew8_unix.sh)"
command -v pickml >/dev/null 2>&1 || skip "pickml no instalado"
pgrep -x startstop >/dev/null 2>&1 || skip "EarthWorm no esta corriendo"
[ -f "$PARAMS/earthworm.d" ] || skip "falta $PARAMS/earthworm.d"

for r in PICK_RING LEGACY_PICK_RING; do
    grep -qE "^[[:space:]]*Ring[[:space:]]+$r[[:space:]]" "$PARAMS/earthworm.d" || \
        skip "$r no declarado en earthworm.d"
done

# Log a un directorio propio: el test no ensucia EW_LOG de la corrida real.
TLOG=$(mktemp -d)
out="$TLOG/salida.txt"
trap 'rm -rf "$TLOG"' EXIT
export EW_LOG="$TLOG"

# Corrida corta en dry-run: no escribe en el anillo.
(
    cd "$PARAMS" || exit 4
    /usr/bin/timeout 30 pickml "$D" --ring --publish-dry-run --stats
) >"$out" 2>&1 || rc=$?
rc=${rc:-0}
case "$rc" in
    0|124) ;;   # 124 = lo corto el timeout, esperado
    *) echo "FAIL: pickml salio con rc=$rc"; tail -20 "$out"; exit 1 ;;
esac

if ! grep -q 'publica' "$out"; then
    echo "FAIL: no aparece la linea de arranque de publicacion"
    tail -20 "$out"; exit 1
fi
if ! grep -q 'pub_dry=' "$out"; then
    echo "FAIL: el resumen no trae los contadores pub_*"
    tail -20 "$out"; exit 1
fi
# Log con el nombre LITERAL del modulo (no el tallo del .d).
if ! ls "$TLOG"/pickml_*.log >/dev/null 2>&1; then
    echo "FAIL: no se creo EW_LOG/pickml_*.log"; tail -20 "$out"; exit 1
fi
if ls "$TLOG"/pickml_phasenetwc_*.log >/dev/null 2>&1; then
    echo "FAIL: el log usa el nombre del .d en vez de 'pickml'"; exit 1
fi

echo "test_live: OK (dry-run sobre $D; log en $TLOG/pickml_*.log)"
