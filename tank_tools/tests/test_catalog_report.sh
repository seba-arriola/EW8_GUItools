#!/bin/bash
# Pruebas del reporte contra catalogo y del barrido de calibracion de csnloc:
#   - catalog_report.py --selftest      (catalogo, agrupacion, clases, --only)
#   - calibrate_csnloc.py --selftest    (materialize de overrides, load_variants)
#   - calibrate_csnloc.py --dry-run     (no escribe nada)
#   - smoke: 2 variantes x 2 tanks -> calibration.json + ranking
#   - invariante: run_working_v8/params/csnloc.d NO cambia (CA4)
set -u

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$DIR/../.." && pwd)"
fail=0

echo "=== selftest catalog_report ==="
python3 "$ROOT/tank_tools/catalog_report.py" --selftest || fail=1

echo
echo "=== selftest loc_report ==="
python3 "$ROOT/tank_tools/loc_report.py" --selftest || fail=1

echo
echo "=== selftest calibrate_csnloc ==="
python3 "$ROOT/tank_tools/calibrate_csnloc.py" --selftest || fail=1

CAT="$ROOT/tests_soluciones_publicadas.dat"
if [ ! -f "$CAT" ]; then
    echo
    echo "SKIP (no existe tests_soluciones_publicadas.dat)"
    exit "$fail"
fi

CSNLOC="$ROOT/earthworm_8.0/bin/csnloc"
if [ ! -x "$CSNLOC" ]; then
    echo
    echo "SKIP (falta el binario $CSNLOC)"
    exit "$fail"
fi

CAP=""
for d in "$ROOT"/picks_ps/*/ "$ROOT"/picks/*/; do
    if [ -d "$d" ]; then CAP="${d%/}"; break; fi
done
if [ -z "$CAP" ]; then
    echo
    echo "SKIP (no hay capturas en picks_ps/ ni picks/)"
    exit "$fail"
fi
echo
echo "captura: $CAP"

TMP="$(mktemp -d "${TMPDIR:-/tmp}/test_catalog_report.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT

CFG="$ROOT/run_working_v8/params/csnloc.d"
before="$(sha256sum "$CFG" | cut -d' ' -f1)"

echo
echo "=== --dry-run (no debe escribir nada) ==="
python3 "$ROOT/tank_tools/calibrate_csnloc.py" --capture "$CAP" --catalog "$CAT" \
    --work "$TMP/dry" --dry-run > "$TMP/dry.txt" 2>&1
if [ -e "$TMP/dry" ]; then
    echo "FAIL: --dry-run escribio $TMP/dry"; fail=1
else
    echo "ok  : --dry-run no escribio nada"
fi

echo
echo "=== smoke: 2 variantes x test[1-2] ==="
if python3 "$ROOT/tank_tools/calibrate_csnloc.py" --capture "$CAP" --catalog "$CAT" \
        --work "$TMP/sweep" --only 'test[1-2]' --tags 'base,refdepth20' \
        > "$TMP/sweep.txt" 2>&1; then
    if [ -f "$TMP/sweep/calibration.json" ]; then
        echo "ok  : calibration.json generado"
    else
        echo "FAIL: falta calibration.json"; fail=1
    fi
    if grep -q '^variante' "$TMP/sweep.txt"; then
        echo "ok  : comparacion impresa"
    else
        echo "FAIL: sin comparacion"; fail=1
    fi
    for f in val/base/loc_report.txt val/base/catalog_report.txt; do
        if [ -f "$TMP/sweep/$f" ]; then
            echo "ok  : $f generado"
        else
            echo "FAIL: falta $f"; fail=1
        fi
    done
else
    echo "FAIL: smoke sweep (rc!=0)"; tail -5 "$TMP/sweep.txt"; fail=1
fi

after="$(sha256sum "$CFG" | cut -d' ' -f1)"
if [ "$before" = "$after" ]; then
    echo "ok  : csnloc.d de produccion intacto (CA4)"
else
    echo "FAIL: csnloc.d de produccion cambio"; fail=1
fi

exit "$fail"
