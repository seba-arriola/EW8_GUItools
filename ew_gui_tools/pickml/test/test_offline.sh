#!/bin/sh
# test_offline.sh — modo offline determinista con el motor stub.
#
# Comprueba la plomería completa: fuente (trace y store) -> buffer 3C ->
# ventanas de 60 s -> post-proceso -> JSON. NO valida el modelo (eso es
# check-engine, que exige ONNX Runtime y el golden fixture).
#
# El dato sintético tiene un estallido en t0+95 s cuyo máximo de |Z| cae
# exactamente ahí, así que el stub (argmax de |Z|) debe picar P en t0+95 y
# S en t0+107.

set -e

cd "$(dirname "$0")/.."
TMP=test/tmp
T0=1700000000
TP=$((T0 + 95))
TS=$((T0 + 107))

rm -rf "$TMP"
mkdir -p "$TMP"

./test/gen_traces --traces "$TMP/burst.traces" --epoch "$T0" --dur 180
./test/gen_traces --store "$TMP/store"     --epoch "$T0" --dur 180

cat > "$TMP/pickml_test.sta" <<'EOF'
# PickFlag Pin Sta Net Loc ChanE ChanN ChanZ
1 1 TEST XX -- HHE HHN HHZ
EOF

cat > "$TMP/pickml_test.d" <<EOF
MyModuleId      MOD_PICKML
InRing          SLINK_RING
OutRing         PICK_RING
StaFile         $TMP/pickml_test.sta
LogFile         0
Engine          stub
WindowSamples   6000
Rate            100.0
RateTolHz       0.01
StepSec         10.0
MinPosSec       20.0
MaxPosSec       0.0
BlindSamples    500
PThreshold      0.30
SThreshold      0.30
W0Prob          0.90
W1Prob          0.70
W2Prob          0.50
W3Prob          0.30
DedupSec        2.0
MaxGapSec       0.10
EOF

# --- 1. trace: picks esperados, en los tiempos esperados ---
./pickml "$TMP/pickml_test.d" --traces "$TMP/burst.traces" --jsonl --stats \
    > "$TMP/traces_a.jsonl" 2> "$TMP/traces_a.err"

nP=$(grep -c '"phase":"P"' "$TMP/traces_a.jsonl" || true)
nS=$(grep -c '"phase":"S"' "$TMP/traces_a.jsonl" || true)
if [ "$nP" -ne 1 ] || [ "$nS" -ne 1 ]; then
    echo "FALLO: esperaba 1 P y 1 S, obtuve P=$nP S=$nS"
    cat "$TMP/traces_a.jsonl"
    exit 1
fi

grep -q "\"phase\":\"P\",\"t_epoch\":$TP.000000" "$TMP/traces_a.jsonl" || {
    echo "FALLO: la P no está en $TP"; cat "$TMP/traces_a.jsonl"; exit 1; }
grep -q "\"phase\":\"S\",\"t_epoch\":$TS.000000" "$TMP/traces_a.jsonl" || {
    echo "FALLO: la S no está en $TS"; cat "$TMP/traces_a.jsonl"; exit 1; }

grep -q '"weight":0' "$TMP/traces_a.jsonl" || {
    echo "FALLO: prob 0.95 debería mapear a weight 0"; exit 1; }

grep -q 'ventanas=' "$TMP/traces_a.err" || {
    echo "FALLO: falta la línea de stats"; cat "$TMP/traces_a.err"; exit 1; }

# --- 2. determinismo: dos corridas byte a byte ---
./pickml "$TMP/pickml_test.d" --traces "$TMP/burst.traces" --jsonl --stats \
    > "$TMP/traces_b.jsonl" 2> /dev/null
cmp -s "$TMP/traces_a.jsonl" "$TMP/traces_b.jsonl" || {
    echo "FALLO: la salida no es determinista"; diff "$TMP/traces_a.jsonl" "$TMP/traces_b.jsonl" | head; exit 1; }

# --- 3. store: el mismo dato por la otra fuente da EXACTAMENTE lo mismo ---
./pickml "$TMP/pickml_test.d" --store "$TMP/store" \
    --t0 "$T0" --t1 "$((T0 + 180))" --jsonl --stats \
    > "$TMP/store.jsonl" 2> "$TMP/store.err"
cmp -s "$TMP/traces_a.jsonl" "$TMP/store.jsonl" || {
    echo "FALLO: trace y store no coinciden"
    diff "$TMP/traces_a.jsonl" "$TMP/store.jsonl" | head
    exit 1; }

# --- 4. tasa no soportada: deshabilita con log, no revienta ---
./test/gen_traces --traces "$TMP/burst50.trace" --epoch "$T0" --dur 20 > /dev/null
# (reusa el mismo generador; el caso de 50 Hz se cubre en test_buffer)

# --- 5. ventana sin datos (ventana de tiempo fuera del trace) ---
if ./pickml "$TMP/pickml_test.d" --store "$TMP/store" \
       --t0 "$((T0 + 86400))" --t1 "$((T0 + 86400 + 120))" --jsonl \
       > "$TMP/empty.jsonl" 2> /dev/null; then
    [ ! -s "$TMP/empty.jsonl" ] || { echo "FALLO: store vacío devolvió picks"; exit 1; }
else
    echo "FALLO: un store sin datos no debe ser un error"; exit 1
fi

echo "test_offline: OK"
