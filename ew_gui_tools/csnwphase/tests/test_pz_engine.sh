#!/bin/bash
# AC4: los SAC_PZs_* que genera csnwphase los consume el binario REAL del motor
# (make_resp_lookup_table) y el id que resulta es el que consulta rec_dec_filt
# (NET_STA_LOC_CHAN, con "--" si no hay location).
#
# Uso: tests/test_pz_engine.sh [WphaseBin]
set -e

MOD="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${1:-/home/seba/Dev/wphase/CSNop/wphase-prod/bin}"
D="/tmp/csnwphase_pz_engine"

if [ ! -x "$BIN/make_resp_lookup_table" ]; then
    echo "SKIP: no existe $BIN/make_resp_lookup_table"
    exit 0
fi

mkdir -p "$D"
if ! "$MOD/test_pz" "$D"; then
    echo "SKIP: no se pudieron generar los PZ (¿faltan resources/csnmags/responses?)"
    exit 0
fi

cd "$D"
"$BIN/make_resp_lookup_table" pz_fil_list i_master coeffs_rec_lut \
    > engine.log 2>&1 || true

echo "--- coeffs_rec_lut ---"
cat coeffs_rec_lut

fail=0
grep -q '^C1_AC06_--_BHZ ' coeffs_rec_lut || { echo "FALLO: falta el id C1_AC06_--_BHZ"; fail=1; }
grep -q '^C1_AF02_--_HHN ' coeffs_rec_lut || { echo "FALLO: falta el id C1_AF02_--_HHN"; fail=1; }
[ "$(wc -l < coeffs_rec_lut)" -eq 2 ] || { echo "FALLO: se esperaban 2 lineas"; fail=1; }

if grep -q 'rejected\|skipped\|WARNING' engine.log; then
    echo "--- avisos del motor ---"; grep 'rejected\|skipped\|WARNING' engine.log | head -5
    fail=1
fi

[ "$fail" -eq 0 ] || exit 1
echo "ALL CSNWPHASE PZ ENGINE TESTS PASSED"
