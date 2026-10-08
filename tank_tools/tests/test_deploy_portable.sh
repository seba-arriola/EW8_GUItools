#!/bin/bash
# =============================================================================
#  test_deploy_portable.sh - despliegue portable (deploy_portable.sh)
#
#  OFFLINE: no arranca servicios. Verifica:
#    D1  la copia crea la estructura y el whitelist (bins/params/grids)
#    D2  cero rutas absolutas; los tanks quedan relativos (../tanks/)
#    D3  ew8_unix.sh es valido y exporta rutas relativas al destino
#    D4  idempotencia (2a corrida: 0 cambios, 0 eliminados)
#    D5  --delete purga obsoletos; --no-delete los conserva
#    D6  --verify-only detecta fallos (exit 2) y valida un arbol sano (exit 0)
#    D7  el estado en vivo (*.state/*.ndx) NO se borra con --delete-excluded
# =============================================================================
set -u
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/lib.sh"

DEPLOY="$ROOT_DIR/deploy_portable.sh"
# el destino debe estar FUERA del repo (deploy_portable.sh lo exige)
DEPLOY_TMP="$(mktemp -d "${TMPDIR:-/tmp}/ew8portable.XXXXXX")"
DST="$DEPLOY_TMP/ew8portable"
trap 'cleanup_tmp; rm -rf "$DEPLOY_TMP"' EXIT

if [ ! -f "$DEPLOY" ]; then
    skip "deploy_portable.sh no existe"
    summary
    exit 0
fi

# -----------------------------------------------------------------------------
echo "=== D1: copia inicial y estructura ==="
bash "$DEPLOY" --dst "$DST" >"$TESTTMP/d1.out" 2>&1
check_exit "D1 copia inicial -> exit 0" 0 "$?"
check_eq "D1 binarios en whitelist" "22" \
    "$(ls "$DST/earthworm_8.0/bin" 2>/dev/null | wc -l | tr -d ' ')"
check_eq "D1 grids copiados" "13" \
    "$(ls "$DST/run_working_v8/params/grids"/*.grid 2>/dev/null | wc -l | tr -d ' ')"
for f in earthworm.d startstop_unix.d csnloc.d csnmags_toy.d pick_FP.sta \
         pickS.d pickS.sta estaciones_107.txt wave_serverV.d iasp91.tbl \
         hyp2000_ring.d nlloc_ring.d hyp2000_ring.hyp estaciones_hyp.sta \
         chile_1d.crh; do
    if [ -f "$DST/run_working_v8/params/$f" ]; then
        pass "D1 params/$f presente"
    else
        failed "D1 FALTA params/$f"
    fi
done
if [ -d "$DST/run_working_v8/log" ] && [ -d "$DST/run_working_v8/tanks" ]; then
    pass "D1 log/ y tanks/ creados"
else
    failed "D1 faltan log/ o tanks/"
fi
check_eq "D1 sin directorios legacy" "0" \
    "$(ls -d "$DST/run_working_v8/params/_legacy_atwc" \
             "$DST/run_working_v8/params/response" \
             "$DST/run_working_v8/params/hyp2000_output" 2>/dev/null | wc -l | tr -d ' ')"

# -----------------------------------------------------------------------------
echo "=== D2: rutas relativas ==="
if grep -RIn -- '/home/' "$DST/run_working_v8/params" >/dev/null 2>&1; then
    failed "D2 hay rutas absolutas en params"
else
    pass "D2 sin rutas absolutas en params"
fi
check_contains "D2 TankStructFile relativo" \
    "$DST/run_working_v8/params/wave_serverV.d" '^TankStructFile[[:space:]]+\.\./tanks/'
check_eq "D2 Tank sin rutas absolutas" "0" \
    "$(awk '$1=="Tank" && $11 ~ /^\//' "$DST/run_working_v8/params/wave_serverV.d" | wc -l | tr -d ' ')"

# -----------------------------------------------------------------------------
echo "=== D3: entorno portable ==="
bash -n "$DST/ew8_unix.sh" 2>/dev/null
check_exit "D3 ew8_unix.sh valido (bash -n)" 0 "$?"
ep="$(cd "$DST" && bash -c 'source ./ew8_unix.sh >/dev/null 2>&1; printf %s "$EW_PARAMS"' 2>/dev/null)"
check_eq "D3 EW_PARAMS apunta al destino" "$DST/run_working_v8/params" "$ep"

# -----------------------------------------------------------------------------
echo "=== D4: idempotencia ==="
bash "$DEPLOY" --dst "$DST" >"$TESTTMP/d4.out" 2>&1
check_contains "D4 2a corrida -> 0 cambios / 0 eliminados" \
    "$TESTTMP/d4.out" 'cambiados=0 eliminados=0'

# -----------------------------------------------------------------------------
echo "=== D5: purga (--delete / --no-delete) ==="
touch "$DST/run_working_v8/params/ZZZ_espurio.d"
bash "$DEPLOY" --dst "$DST" >/dev/null 2>&1
if [ -e "$DST/run_working_v8/params/ZZZ_espurio.d" ]; then
    failed "D5 --delete no purgo el obsoleto"
else
    pass "D5 --delete purga obsoletos"
fi
touch "$DST/run_working_v8/params/ZZZ_espurio.d"
bash "$DEPLOY" --dst "$DST" --no-delete >/dev/null 2>&1
if [ -e "$DST/run_working_v8/params/ZZZ_espurio.d" ]; then
    pass "D5 --no-delete conserva"
else
    failed "D5 --no-delete borro el archivo"
fi
rm -f "$DST/run_working_v8/params/ZZZ_espurio.d"

# -----------------------------------------------------------------------------
echo "=== D6: --verify-only ==="
mv "$DST/earthworm_8.0/bin/sniffring" "$TESTTMP/sniffring.bak"
bash "$DEPLOY" --dst "$DST" --verify-only >"$TESTTMP/d6.out" 2>&1
check_exit "D6 detecta binario faltante (exit 2)" 2 "$?"
mv "$TESTTMP/sniffring.bak" "$DST/earthworm_8.0/bin/sniffring"
bash "$DEPLOY" --dst "$DST" --verify-only >/dev/null 2>&1
check_exit "D6 arbol sano (exit 0)" 0 "$?"

# -----------------------------------------------------------------------------
echo "=== D7: el estado en vivo no se purga ==="
# slink2ew/pick_FP crean estos ficheros EN params/ mientras corren; el deploy no
# debe borrarlos (les quitaria la posicion de SeedLink y el indice de picks).
touch "$DST/run_working_v8/params/slink999.state" \
      "$DST/run_working_v8/params/pick_FP_999.ndx"
bash "$DEPLOY" --dst "$DST" >"$TESTTMP/d7.out" 2>&1
check_contains "D7 sin bajas (eliminados=0)" "$TESTTMP/d7.out" 'eliminados=0'
for f in slink999.state pick_FP_999.ndx; do
    if [ -e "$DST/run_working_v8/params/$f" ]; then
        pass "D7 estado en vivo preservado: $f"
    else
        failed "D7 el deploy borro $f"
    fi
done
rm -f "$DST/run_working_v8/params/slink999.state" \
      "$DST/run_working_v8/params/pick_FP_999.ndx"

summary
