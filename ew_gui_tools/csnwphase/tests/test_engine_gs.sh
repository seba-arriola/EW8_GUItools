#!/bin/bash
# test_engine_gs.sh — e2e del grid search del centroide (pasos 8 y 9 del run.sh)
# contra el MOTOR REAL de fase W, sobre un run dir ya preparado.
#
# Por qué existe: los pasos 8/9 solo estaban cubiertos por `test_run_dry.sh`, que
# LEE el run.sh generado. Con eso el paso 10 pasaba "verde" mientras fallaba en
# TODAS las corridas reales: el cmtfile de la pasada xy fina quedaba con 20
# campos (depth y los 6 momentos dos veces) y `get_cmtf` del motor (flag==2)
# exige EXACTAMENTE 13 (read_i_files.c:238-247) -> `field(s) redundancy` +
# exit(1). Evidencia: run_working_v8/csnhypodbp/mww/0801000068-*/run.log:1102.
#
# Ejecuta el bloque del grid search del run.sh GENERADO AHORA MISMO (con el
# codigo del working tree) en un run dir montado con enlaces a las ENTRADAS de
# una corrida real: no se escribe nada en el run dir original.
#
# Uso:  tests/test_engine_gs.sh [rundir] [csnwphase.d]
#       CSNWPHASE_TEST_RUNDIR=<dir>   (equivale al 1.er argumento)
#       CSNWPHASE_MWW_DIR=<dir>       (dir con los run dirs; default el del .d)
# SKIP (exit 0) si no hay motor, .d o run dir utilizable: el test no puede
# inventarse las ondas ni la base de funciones de Green.
set -u

MOD="$(cd "$(dirname "$0")/.." && pwd)"
ROOT="$(cd "$MOD/../.." && pwd)"
PARAMS="${EW_PARAMS:-$ROOT/run_working_v8/params}"
CFG="${2:-$PARAMS/csnwphase.d}"
MWW="${CSNWPHASE_MWW_DIR:-$ROOT/run_working_v8/csnhypodbp/mww}"
TMP="${CSNWPHASE_TEST_TMP:-/tmp/csnwphase_engine_gs}"

skip() { echo "SKIP: $*"; exit 0; }

# Todos los ficheros que lista o_wpinversion (columna 1) estan en el run dir?
have_inputs() {
    local d="$1" f rest
    [ -f "$d/o_wpinversion" ] || return 1
    while read -r f rest; do
        [ -z "$f" ] && continue
        [ -e "$d/$f" ] || return 1
    done < "$d/o_wpinversion"
    return 0
}

[ -x "$MOD/csnwphase" ] || skip "no esta compilado $MOD/csnwphase (make)"
[ -f "$CFG" ] || skip "no encuentro $CFG"

# --- el motor, segun el .d ---------------------------------------------------
PCFG=$("$MOD/csnwphase" "$CFG" --print-config) || skip "--print-config fallo"
WBIN=$(echo "$PCFG" | awk '/^WphaseBin/{print $2}')
WPHOME=$(echo "$PCFG" | awk '/^WphaseHome/{print $2}')
[ -x "$WBIN/wpinversion_gs" ] || skip "no existe $WBIN/wpinversion_gs"
[ -d "$WPHOME" ] || skip "no existe WphaseHome=$WPHOME"

# --- run dir de partida (el mas nuevo que sirva) -----------------------------
SRC="${1:-${CSNWPHASE_TEST_RUNDIR:-}}"
if [ -z "$SRC" ]; then
    # Candidatos: run dirs con todo lo que el grid search necesita.
    cands=""
    for d in $(ls -1dt "$MWW"/*/ 2>/dev/null); do
        [ -f "$d/i_master" ] && [ -f "$d/CMTSOLUTION" ] || continue
        ls "$d"/DATA/*.SAC >/dev/null 2>&1 || continue
        have_inputs "$d" || continue
        cands="$cands $d"
    done
    # Se prefiere un run dir donde el grid search YA corrio (grilla 3D presente)
    # y con mas trazas: los run dirs degenerados (1-4 trazas) hacen que la
    # inversion del MOTOR de NaN o reviente, y eso no es un fallo del modulo.
    for d in $cands; do
        n=$(grep -c . "$d/o_wpinversion" 2>/dev/null)
        sc=${n:-0}
        [ -e "$d/grid_search_xyz_out" ] && sc=$((sc + 1000))
        if [ -z "${SRC:-}" ] || [ "$sc" -gt "${best_sc:-0}" ]; then
            SRC="$d"
            best_sc="$sc"
        fi
    done
fi
[ -n "${SRC:-}" ] && [ -d "$SRC" ] || skip "sin run dir con i_master+CMTSOLUTION+o_wpinversion+DATA/*.SAC bajo $MWW"
[ -f "$SRC/i_master" ] || skip "$SRC sin i_master"
[ -f "$SRC/CMTSOLUTION" ] || skip "$SRC sin CMTSOLUTION"
[ -f "$SRC/o_wpinversion" ] || skip "$SRC sin o_wpinversion"
# El run dir de trabajo enlaza DATA/GF al de partida: con una ruta RELATIVA el
# enlace apunta a si mismo y el motor no abre las trazas (`ERROR opening
# DATA/...`). Se resuelve a absoluta.
SRC="$(cd "$SRC" && pwd)"
have_inputs "$SRC" || skip "$SRC no tiene todos los ficheros que lista o_wpinversion (run incompleto)"

# Los parametros del evento salen de la PDE del CMT de la corrida.
set -- $(awk '/^[[:space:]]*PDE/{
    printf "%04d-%02d-%02dT%02d:%02d:%s %s %s %s %s %s\n",
           $2,$3,$4,$5,$6,$7,$8,$9,$10,$11,$13; exit}' "$SRC/CMTSOLUTION")
[ $# -ge 5 ] || skip "no pude leer la PDE de $SRC/CMTSOLUTION"
T0="$1"; LAT="$2"; LON="$3"; PROF="$4"; MAG="$5"; LOC="${6:-Chile}"

echo "run dir de partida: $SRC"
echo "evento: t0=$T0 lat=$LAT lon=$LON prof=$PROF mag=$MAG loc=$LOC"

# --- run dir de trabajo: entradas enlazadas, salidas reales ------------------
rm -rf "$TMP"
mkdir -p "$TMP/run/LOG" "$TMP/run/SYNTH" "$TMP/run/gs_SYNTH"
cp -f "$SRC/o_wpinversion" "$TMP/run/o_wpinversion"
for f in coeffs_rec_lut dec_bp_dat_fil_list scr_dat_fil_list pz_fil_list; do
    [ -e "$SRC/$f" ] && cp -f "$SRC/$f" "$TMP/run/$f"
done
ln -s "$SRC/DATA" "$TMP/run/DATA"          # solo lectura
[ -e "$SRC/GF" ] && ln -s "$SRC/GF" "$TMP/run/GF"   # base de Green, solo lectura

# El `--run --dry-run` regenera i_master/CMTSOLUTION y escribe el run.sh; la
# unica concesion es MinMagnitude 0 (los eventos de prueba de esta semana son
# M~5 y el umbral real es 6.0): aqui NO se invierte, solo se genera el script.
sed 's/^MinMagnitude.*/MinMagnitude 0.0/' "$CFG" > "$TMP/csnwphase.d"
"$MOD/csnwphase" "$TMP/csnwphase.d" --run "$TMP/run" "$T0" "$LAT" "$LON" "$PROF" \
    "$MAG" "$LOC" --dry-run > "$TMP/prep.log" 2>&1
if [ $? -ne 0 ] || [ ! -f "$TMP/run/run.sh" ]; then
    echo "FALLO: no se pudo generar run.sh"
    tail -n 10 "$TMP/prep.log"
    exit 1
fi

# --- solo el bloque del grid search (pasos 8 y 9), con su preambulo ----------
{
    head -n 1 "$TMP/run/run.sh"
    sed -n '/^n=0/,/^WBIN=/p' "$TMP/run/run.sh"
    sed -n '/^mkdir -p gs_SYNTH/,$p' "$TMP/run/run.sh"
} > "$TMP/run/gs_only.sh"

grep -q 'oxygsf grid_search_xyz_out' "$TMP/run/gs_only.sh" \
    || skip "el run.sh generado no trae la pasada 3D (GridSearch 0?)"

echo "--- ejecutando el grid search (wpinversion_gs, dos pasadas) ---"
( cd "$TMP/run" && sh gs_only.sh ) > "$TMP/gs.log" 2>&1
gs_rc=$?
echo "rc del bloque: $gs_rc"

fail=0
chk_file() {   # <fichero> <descripcion>
    if [ -e "$TMP/run/$1" ]; then
        echo "ok: $2 ($1, $(wc -c < "$TMP/run/$1") bytes)"
    else
        echo "FALLO: falta $2 ($1)"; fail=1
    fi
}
chk_file grid_search_xyz_out        "grilla de la pasada 3D"
chk_file grid_search_xy_out         "grilla de la pasada xy fina"
chk_file xy_WCMTSOLUTION            "solucion del centroide"

if [ -e "$TMP/run/_tmp_CMTSOLUTION.xyz" ]; then
    echo "FALLO: quedo _tmp_CMTSOLUTION.xyz (el paso 10 no llego al rm)"; fail=1
fi
if grep -q 'ERROR: reading cmtfile' "$TMP/gs.log"; then
    echo "FALLO: el motor aborto por el cmtfile (field(s) redundancy)"; fail=1
fi
if grep -qE '##### paso [0-9]+: rc=[1-9]' "$TMP/gs.log"; then
    echo "FALLO: un paso del grid search salio con rc!=0"; fail=1
fi
grep -q '^Wmag:' "$TMP/run/LOG/gs_o_wpinversion.log" \
    || { echo "FALLO: el log del grid search no trae Wmag:"; fail=1; }

echo "--- Wmag del log del motor ---"
grep -h '^Wmag:' "$TMP/run/LOG/gs_o_wpinversion.log"
echo "--- primeras filas de grid_search_xy_out ---"
head -n 3 "$TMP/run/grid_search_xy_out"
echo "--- ultimas lineas del bloque ---"
tail -n 6 "$TMP/gs.log"

[ "$fail" -eq 0 ] || exit 1
echo "ALL CSNWPHASE ENGINE GS TESTS PASSED"
