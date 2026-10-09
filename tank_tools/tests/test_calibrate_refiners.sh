#!/bin/bash
# Pruebas del arnes de calibracion de refinadores (hyp2000_ring / nlloc_ring):
#   - refine_arcs.py --selftest        (parseo de ARC, suelo, filtros, preflight)
#   - refine_report.py --selftest      (reporte A: parametros + delta vs crudo)
#   - calibrate_refiners.py --selftest (materializacion aislada de .d/.hyp/ctrl)
#   - --dry-run no escribe nada
#   - smoke: 1 variante por refinador sobre el suelo real -> reportes A y B
#   - invariante: los .d/.hyp/plantillas de produccion NO cambian
set -u

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$DIR/../.." && pwd)"
fail=0

echo "=== selftest refine_arcs ==="
python3 "$ROOT/tank_tools/refine_arcs.py" --selftest || fail=1

echo
echo "=== selftest refine_report ==="
python3 "$ROOT/tank_tools/refine_report.py" --selftest || fail=1

echo
echo "=== selftest calibrate_refiners ==="
python3 "$ROOT/tank_tools/calibrate_refiners.py" --selftest || fail=1

PARAMS="$ROOT/run_working_v8/params"
PROT=("$PARAMS/csnloc.d" "$PARAMS/hyp2000_ring.d" "$PARAMS/hyp2000_ring.hyp" \
      "$PARAMS/nlloc_ring.d" "$ROOT/resources/hyp2000/hyp2000_ring.hyp" \
      "$ROOT/resources/nlloc/ctrl/N18-26_1.5k.in")
declare -A BEFORE
for f in "${PROT[@]}"; do
    [ -f "$f" ] && BEFORE["$f"]="$(sha256sum "$f" | cut -d' ' -f1)"
done

TMP="$(mktemp -d "${TMPDIR:-/tmp}/test_refiners.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT

echo
echo "=== --dry-run (no debe escribir nada) ==="
for R in hyp2000 nlloc; do
    python3 "$ROOT/tank_tools/calibrate_refiners.py" --refiner "$R" \
        --work "$TMP/dry_$R" --dry-run > "$TMP/dry_$R.txt" 2>&1
    if [ -e "$TMP/dry_$R" ]; then
        echo "FAIL: --dry-run de $R escribio $TMP/dry_$R"; fail=1
    else
        echo "ok  : --dry-run de $R no escribio nada"
    fi
done

BASE="$ROOT/tmp/refine"
NARC=0
[ -d "$BASE/arcs" ] && NARC=$(ls "$BASE"/arcs/*.arc 2>/dev/null | wc -l)
if [ "$NARC" -lt 3 ]; then
    echo
    echo "SKIP smoke (el suelo $BASE/arcs tiene $NARC ARC; construyelo con refine_arcs.py --baseline)"
else
    echo
    echo "suelo: $NARC ARC"
    S1="$(ls "$BASE"/arcs/*.arc 2>/dev/null | sed 's#.*/##; s#_[0-9]*\.arc$##' | sort -u | head -1)"
    echo "slug del smoke: $S1"
    for R in hyp2000 nlloc; do
        echo
        echo "=== smoke $R (1 variante x $S1) ==="
        if [ "$R" = hyp2000 ]; then TAG=h_bandas; else TAG=n_bandas; fi
        if python3 "$ROOT/tank_tools/calibrate_refiners.py" --refiner "$R" \
                --baseline "$BASE" --catalog "$ROOT/tests_soluciones_publicadas.dat" \
                --work "$TMP/$R" --only "$S1" --tags "$TAG" \
                > "$TMP/$R.txt" 2>&1; then
            for f in "val/$TAG/refine_report.txt" "val/$TAG/catalog_report.txt" \
                     "cfg/$TAG/$TAG.d" "calibration.json"; do
                if [ -f "$TMP/$R/$f" ]; then
                    echo "ok  : $f"
                else
                    echo "FAIL: falta $f"; fail=1
                fi
            done
            if grep -q '^variante' "$TMP/$R.txt"; then
                echo "ok  : comparacion impresa"
            else
                echo "FAIL: sin comparacion"; fail=1
            fi
            if grep -q 'base_d_km' "$TMP/$R/val/$TAG/catalog_report.txt"; then
                echo "ok  : el crudo aparece en el cruce"
            else
                echo "FAIL: sin columna del crudo"; fail=1
            fi
            if grep -q '2. EVENTOS' "$TMP/$R/val/$TAG/refine_report.txt"; then
                echo "ok  : reporte A con eventos"
            else
                echo "FAIL: reporte A sin eventos"; fail=1
            fi
            # el smoke no debe ser vacuo: tiene que haber refinado algo
            if python3 -c "
import json,sys
m=json.load(open('$TMP/$R/val/$TAG/manifest.json'))
sys.exit(0 if m['n_in']>0 and m['n_ok']>0 else 1)"; then
                echo "ok  : el refinador trabajo (n_in>0, n_ok>0)"
            else
                echo "FAIL: el smoke no refino nada ($R)"; fail=1
            fi
        else
            echo "FAIL: smoke $R (rc!=0)"; tail -5 "$TMP/$R.txt"; fail=1
        fi
    done
fi

echo
echo "=== invariante: produccion intacta ==="
for f in "${PROT[@]}"; do
    if [ -n "${BEFORE[$f]:-}" ]; then
        now="$(sha256sum "$f" | cut -d' ' -f1)"
        if [ "$now" = "${BEFORE[$f]}" ]; then
            echo "ok  : $(basename "$f") intacto"
        else
            echo "FAIL: $(basename "$f") cambio"; fail=1
        fi
    fi
done

exit "$fail"
