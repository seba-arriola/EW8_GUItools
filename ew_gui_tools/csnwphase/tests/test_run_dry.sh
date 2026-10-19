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
chk_order 'wpinversion_gs" -imas'        "grid search del centroide"

# Un solo screening: se elimino el bucle `for th in 5 3 1` del CSN y el RMS y la
# razon van en la MISMA invocacion (el motor los aplica en secuencia).
if grep -q 'for th in' "$D/run.sh"; then
    echo "FALLO: sigue el bucle de screening -th"; fail=1
fi
ninv=$(grep -c '\$WBIN/wpinversion"' "$D/run.sh")
[ "$ninv" -eq 2 ] || { echo "FALLO: se esperaban 2 corridas de wpinversion (-med y screening), hay $ninv"; fail=1; }
grep -q '\-th .* -nr ' "$D/run.sh" || { echo "FALLO: -th y -nr no van en la misma corrida"; fail=1; }

# Grid search 3D (xyz, replica de --xyz de wp_grid_search.py) y pasada xy fina
# encadenada sobre la solucion 3D + la referencia.
grep -q 'oxygsf grid_search_xyz_out' "$D/run.sh" || { echo "FALLO: falta la salida xyz del grid search"; fail=1; }
grep -q 'xygsf grid_search_xy_out' "$D/run.sh" || { echo "FALLO: falta la salida xy del grid search"; fail=1; }
grep -q '\-dz .* -minz ' "$D/run.sh" || { echo "FALLO: falta -dz/-minz (pasada 3D)"; fail=1; }
nx=$(grep -n 'oxygsf grid_search_xyz_out' "$D/run.sh" | head -1 | cut -d: -f1)
ny=$(grep -n 'oxygsf grid_search_xy_out'  "$D/run.sh" | head -1 | cut -d: -f1)
if [ -z "$nx" ] || [ -z "$ny" ] || [ "$nx" -ge "$ny" ]; then
    echo "FALLO: la pasada xyz debe ir antes de la xy fina"; fail=1
fi
# --- Paso 10 (pasada xy fina). El cmtfile temporal debe tener EXACTAMENTE 13
# campos que el motor reconozca (read_i_files.c:238-247, flag==2): 1 (PDE) + 6
# (event name/time shift/half duration/latitude/longitude/depth) + 6
# (Mrr..Mtp), sin repetir ninguno. La version anterior copiaba el xyz COMPLETO
# (13 lineas, ya con momentos) y le apendaba `tail -n +7` (depth + los 6
# momentos): 20 campos -> `ERROR ... (field(s) redundancy)` y el paso 10 fallaba
# en TODAS las corridas (medido 2026-10-16 en dos run dirs).
grep -q 'head -n 7 xyz_WCMTSOLUTION > _tmp_CMTSOLUTION.xyz' "$D/run.sh" \
    || { echo "FALLO: la base del _tmp no son las 7 lineas del xyz"; fail=1; }
grep -q 'tail -n +8 CMTSOLUTION >> _tmp_CMTSOLUTION.xyz' "$D/run.sh" \
    || { echo "FALLO: el apendice no es L[7:] de la referencia (tail -n +8)"; fail=1; }
if grep -q 'tail -n +7' "$D/run.sh"; then
    echo "FALLO: sigue un apendice desde la linea 7 (duplica depth y los momentos)"; fail=1
fi
grep -q 'icmtf _tmp_CMTSOLUTION.xyz' "$D/run.sh" || { echo "FALLO: la pasada xy no encadena la solucion 3D"; fail=1; }
grep -q 'xyz_WCMTSOLUTION' "$D/run.sh" || { echo "FALLO: no se conserva la solucion 3D"; fail=1; }

# El contrato, simulado con el fixture (CMT completo de 13 lineas).
TMP10="$D/_tmp_CMTSOLUTION.xyz"
head -n 7 "$MOD/tests/data/cmt_illapel.txt" > "$TMP10"
tail -n +8 "$MOD/tests/data/cmt_illapel.txt" >> "$TMP10"
nl10=$(wc -l < "$TMP10")
[ "$nl10" -eq 13 ] || { echo "FALLO: base 7 + apendice 6 = $nl10 lineas (esperadas 13)"; fail=1; }
for pat in '^[[:space:]]*PDE' '^event name' '^time shift' '^half duration' \
           '^latitude' '^longitude' '^depth' '^Mrr' '^Mtt' '^Mpp' '^Mrt' \
           '^Mrp' '^Mtp'; do
    c=$(grep -c "$pat" "$TMP10")
    [ "$c" -eq 1 ] || { echo "FALLO: '$pat' aparece $c veces en el _tmp (debe ser 1)"; fail=1; }
done
# Referencia con <13 lineas: la base queda en 7 campos y el motor degrada solo
# a flag==1 (`NO REF. SOL. USED`, read_i_files.c:99) en vez de abortar.
head -n 7 "$MOD/tests/data/cmt_illapel.txt" > "$TMP10"
nl10=$(wc -l < "$TMP10")
[ "$nl10" -eq 7 ] || { echo "FALLO: sin referencia el _tmp debe quedar en 7 campos (tiene $nl10)"; fail=1; }

# --- Paridad con el wrapper de produccion del CSN y ts2 por magnitud.
# wp_grid_search.py:187-188 (`-s` -> `-hdsafe`) y :298 (`-old`); el `-old` del
# screening ya existia. ts2 = f(mag) (:173-185): el dry run es M8.3 -> 168 s.
nhds=$(grep -c '\-hdsafe' "$D/run.sh")
[ "$nhds" -eq 2 ] || { echo "FALLO: -hdsafe debe ir en las 2 pasadas del gs (hay $nhds)"; fail=1; }
grep -q '\-ocmtf WCMTSOLUTION.screen -old' "$D/run.sh" \
    || { echo "FALLO: el screening perdio su -old"; fail=1; }
grep -q '\-dz .* -minz .* -hdsafe -old' "$D/run.sh" \
    || { echo "FALLO: la pasada 3D no lleva -hdsafe/-old"; fail=1; }
grep -q 'oxygsf grid_search_xy_out -hdsafe -old' "$D/run.sh" \
    || { echo "FALLO: la pasada xy fina no lleva -hdsafe/-old"; fail=1; }
grep -q '\-ts 1 4 168 -ts_Nit 3' "$D/run.sh" \
    || { echo "FALLO: ts2 no derivado de la magnitud (M8.3 -> 168 s)"; fail=1; }
grep -q '13 columnas en o_wpinversion' "$D/run.sh" \
    || { echo "FALLO: falta la guarda de 13 columnas antes de -old"; fail=1; }

# El GFDIR del run dir no puede ser la base de Green (prep_kernels la borraria).
grep -q 'GFDIR:   ./GF' "$D/i_master" || { echo "FALLO: GFDIR inesperado"; fail=1; }
if grep -q 'GfDir/' "$D/i_master"; then echo "FALLO: GFDIR apunta a la base"; fail=1; fi

[ "$fail" -eq 0 ] || exit 1
echo "ALL CSNWPHASE RUN-DRY TESTS PASSED"
