#!/bin/sh
# test_eqt_d.sh — contrato del .d de EQTransformer (run_working_v8/params/pickml_eqt.d).
#
# Cubre lo que no necesita EarthWorm arriba y NO publica nada:
#   1. --print-config: el .d carga y declara el contrato (ventana nativa de EQT,
#      anillo de salida y publicacion encendida).
#   2. --check-model : abre la sesion ONNX y valida la forma de ENTRADA
#      [B,3,6000] contra el grafo, mas la lista de estaciones.
#   3. corrida sobre el fixture dorado (60 s @100 Hz, 3C, 6000 muestras): es la
#      unica forma de validar la SALIDA del grafo, porque --check-model NO la
#      mira: el conteo nwin*nsamp*3 se comprueba dentro de pm_onnx_run.
#
# El .d apunta al modelo y a la libreria ORT absolutos de /mnt/d: si no estan
# (maquina sin los recursos) los pasos 2 y 3 se SALTAN, igual que check-engine.
# Con un binario `stub` (sin ORT_DIR al compilar) tambien se salta: check-offline
# no debe depender de ONNX.

set -e

cd "$(dirname "$0")/.."
REPO=$(cd ../.. && pwd)
# Uso: test_eqt_d.sh [<nombre>.d | <ruta>.d]   (default: pickml_eqt.d)
D="${1:-pickml_eqt.d}"
case "$D" in
    /*) ;;
    *) D="$REPO/run_working_v8/params/$D" ;;
esac

[ -f "$D" ] || { echo "FAIL: falta $D"; exit 1; }

FIX=test/fixtures
TMP=test/tmp_eqt

echo "== $(basename "$D")"

rm -rf "$TMP"
mkdir -p "$TMP"

cfgval() { awk -v k="$1" '$1 == k { print $2; exit }' "$TMP/cfg.txt"; }

# 1. configuracion: el contrato que el resto del pipeline asume.
./pickml "$D" --print-config > "$TMP/cfg.txt"
[ "$(cfgval Engine)" = "onnx" ]        || { echo "FAIL: Engine != onnx"; exit 1; }
[ "$(cfgval WindowSamples)" = "6000" ] || {
    echo "FAIL: WindowSamples != 6000 (ventana nativa de EQT)"; exit 1; }
[ "$(cfgval Rate)" = "100.000" ]       || { echo "FAIL: Rate != 100 Hz"; exit 1; }
[ "$(cfgval InRing)" = "SLINK_RING" ]  || { echo "FAIL: InRing != SLINK_RING"; exit 1; }
# El OutRing es propio de cada instancia (PICK_RING vs LEGACY_PICK_RING) y lo
# fija common/tests/test_pick_rings.sh; aqui solo se exige que publique.
[ "$(cfgval PublishRing)" = "1" ]      || { echo "FAIL: PublishRing != 1"; exit 1; }
[ "$(cfgval StaFile)" = "pickS.sta" ]  || { echo "FAIL: StaFile != pickS.sta"; exit 1; }
case "$(cfgval ModelFile)" in
    *.onnx) ;;
    *) echo "FAIL: ModelFile no apunta a un .onnx"; exit 1 ;;
esac
echo "OK 1/3 --print-config: contrato del .d (ventana 6000, PICK_RING, publica)"

# 2. y 3. motor real: SKIP si falta el modelo, la libreria o el fixture.
MODEL=$(cfgval ModelFile)
ORTLIB=$(cfgval OrtLib)
[ -f "$MODEL" ]  || { echo "test_eqt_d: SKIP 2/3 y 3/3 (no existe $MODEL)"; exit 0; }
[ -f "$ORTLIB" ] || { echo "test_eqt_d: SKIP 2/3 y 3/3 (no existe $ORTLIB)"; exit 0; }
[ -f "$FIX/win1.traces" ] || { echo "test_eqt_d: SKIP 3/3 (falta $FIX/win1.traces)"; exit 0; }

cat > "$TMP/eqt.sta" <<'EOF'
1 1 TEST XX -- HHE HHN HHZ
EOF

# Mismo modelo y OrtLib que el .d real; geometria del fixture (6000 muestras =
# 60 s) y umbrales a 0 con W3Prob 0: lo que se valida es el contrato de E/S, no
# la calidad del pick, asi que cualquier ventana emitida tiene que dejar un pick
# contable.
cat > "$TMP/eqt.d" <<EOF
MyModuleId      MOD_PICKML
StaFile         $TMP/eqt.sta
LogFile         0
Engine          onnx
ModelName       eqt
ModelFile       $MODEL
OrtLib          $ORTLIB
OrtThreads      0
WindowSamples   6000
Rate            100.0
StepSec         10.0
MinPosSec       0.0
MaxPosSec       0.0
PThreshold      0.0
SThreshold      0.0
W0Prob          0.90
W1Prob          0.70
W2Prob          0.50
W3Prob          0.00
DedupSec        0.0
MaxGapSec       0.0
EOF

if ./pickml "$TMP/eqt.d" --check-model > "$TMP/cm.txt" 2>&1; then
    grep -q 'motor onnx listo' "$TMP/cm.txt" || {
        echo "FAIL: --check-model no reporto el motor onnx"; cat "$TMP/cm.txt"; exit 1; }
elif grep -q 'ONNX Runtime no compilado' "$TMP/cm.txt"; then
    echo "test_eqt_d: SKIP 2/3 y 3/3 (binario stub: motor ONNX no compilado)"
    exit 0
else
    echo "FAIL: --check-model fallo"; cat "$TMP/cm.txt"; exit 1
fi
echo "OK 2/3 --check-model: sesion ONNX y entrada [B,3,6000]"

# 3. la salida del grafo. Si tuviera otra forma (p.ej. [B,3,T] sin el wrapper del
#    exportador) pm_onnx_run lo dice y descarta la ventana; aqui eso es un FAIL.
run() { ./pickml "$TMP/eqt.d" --traces "$FIX/win1.traces" --jsonl --stats; }
run > "$TMP/a.jsonl" 2> "$TMP/a.err"
run > "$TMP/b.jsonl" 2> "$TMP/b.err"

if grep -q 'salida del grafo con' "$TMP/a.err"; then
    echo "FAIL: la salida del grafo no tiene nwin*nsamp*3 elementos"
    cat "$TMP/a.err"
    exit 1
fi
nwin=$(sed -n 's/.*ventanas=\([0-9][0-9]*\).*/\1/p' "$TMP/a.err" | tail -1)
[ -n "$nwin" ] && [ "$nwin" -ge 1 ] || {
    echo "FAIL: no se proceso ninguna ventana"; cat "$TMP/a.err"; exit 1; }
cmp -s "$TMP/a.jsonl" "$TMP/b.jsonl" || {
    echo "FAIL: la inferencia ONNX no es determinista"; exit 1; }
grep -q '"phase":"P"' "$TMP/a.jsonl" || {
    echo "FAIL: sin picks P sobre el fixture (umbrales a 0)"; exit 1; }
echo "OK 3/3 salida del grafo [B,6000,3]: $nwin ventana(s), picks deterministas"

rm -rf "$TMP"
echo "test_eqt_d: OK"
