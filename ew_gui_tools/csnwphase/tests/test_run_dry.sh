#!/bin/bash
# F3.1: el run.sh generado contiene la secuencia del motor EN ORDEN y con las
# variables de entorno que el motor necesita. Dry run: no toca ondas.
set -e

MOD="$(cd "$(dirname "$0")/.." && pwd)"
D="/tmp/csnwphase_run_dry"
CFG="${1:-$EW_PARAMS/csnwphase.d}"

mkdir -p "$D"
"$MOD/csnwphase" "$CFG" --run "$D" 2015-09-16T22:54:32 -31.573 -71.674 25.0 8.3 Chile --dry-run
echo "--- run.sh ---"
cat "$D/run.sh"

fail=0
prev=0
chk_order() { # <patron> <descripcion>
    ln=$(grep -n "$1" "$D/run.sh" | head -1 | cut -d: -f1)
    if [ -z "$ln" ]; then echo "FALLO: falta $2"; fail=1; return; fi
    if [ "$ln" -le "$prev" ]; then echo "FALLO: $2 fuera de orden"; fail=1; fi
    prev=$ln
}

grep -q "export WPHASE_HOME=" "$D/run.sh" || { echo "FALLO: falta WPHASE_HOME"; fail=1; }
grep -q "export GF_PATH=" "$D/run.sh" || { echo "FALLO: falta GF_PATH"; fail=1; }
grep -q "export LC_ALL=C" "$D/run.sh" || { echo "FALLO: falta LC_ALL=C"; fail=1; }

chk_order 'cp -f DATA_org/SAC_PZs_'      "copia de respuestas"
chk_order '\$WBIN/decim_one_sac_file_to_1sps' "diezmado a 1 sps"
chk_order '\$WBIN/trim_sac_files'        "ventanas W"
chk_order '\$WBIN/make_resp_lookup_table' "LUT de respuesta"
chk_order '\$WBIN/rec_dec_filt'          "deconvolucion+filtrado"
chk_order '\$WBIN/prep_kernels'          "kernels de las GF"
chk_order 'wpinversion" -log'            "inversion con -med"
chk_order 'wpinversion" -th'             "screening por RMS"
chk_order 'wpinversion" -nr'             "screening por razon"
chk_order 'wpinversion_gs" -imas'        "grid search del centroide"
grep -q 'xygsf grid_search_xy_out' "$D/run.sh" || { echo "FALLO: falta la salida xy del grid search"; fail=1; }

# El GFDIR del run dir no puede ser la base de Green (prep_kernels la borraria).
grep -q 'GFDIR:   ./GF' "$D/i_master" || { echo "FALLO: GFDIR inesperado"; fail=1; }
if grep -q 'GfDir/' "$D/i_master"; then echo "FALLO: GFDIR apunta a la base"; fail=1; fi

[ "$fail" -eq 0 ] || exit 1
echo "ALL CSNWPHASE RUN-DRY TESTS PASSED"
