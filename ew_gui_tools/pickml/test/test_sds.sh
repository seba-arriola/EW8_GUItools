#!/bin/sh
# test_sds.sh — modo offline `--sds` contra un SDS sintetico (sin red).
#
# Cubre:
#   AC1  --sds es exclusivo y exige --t0/--t1; root inexistente falla
#   AC2  100 Hz: --sds y --tank con el mismo contenido dan JSONL byte a byte
#   AC3  200 Hz: la decimacion 2x conserva las picadas (+-0.05 s)
#   AC4  rango que cruza medianoche UTC (dos archivos de dia)
#   AC5  estacion/dia ausente: exit 0, contadores y resto procesado
#   AC6  tasa no soportada (300 Hz): exit 0, estacion omitida, contador
#
# El dato sintetico es el de gen_tank: P en t0+95 s y S en t0+107 s (motor
# stub = argmax de |Z|), igual que test_offline.sh.

set -e
LC_ALL=C
export LC_ALL

cd "$(dirname "$0")/.."
TMP=test/tmp_sds
T0=1700000000
TP=$((T0 + 95))
TS=$((T0 + 107))
# 90 s antes de la medianoche UTC siguiente: el burst de 180 s cruza el dia.
MID=$(( ((T0 / 86400) + 1) * 86400 - 90 ))

rm -rf "$TMP"
mkdir -p "$TMP"

fail() { echo "FALLO: $*"; exit 1; }

# ---------------------------------------------------------------- datos ------
./test/gen_tank --tank "$TMP/burst.tank" --epoch "$T0" --dur 180
./test/gen_sds  --tank "$TMP/burst.tank" --root "$TMP/sds"

cat > "$TMP/sds.sta" <<'EOF'
# PickFlag Pin Sta Net Loc ChanE ChanN ChanZ
1 1 TEST XX -- HHE HHN HHZ
1 2 NOPE YY -- HHE HHN HHZ
EOF

cat > "$TMP/sds.d" <<EOF
MyModuleId      MOD_PICKML
InRing          SLINK_RING
OutRing         PICK_RING
StaFile         $TMP/sds.sta
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

# ------------------------------------------------- AC1: CLI y errores --------
./pickml "$TMP/sds.d" --sds "$TMP/sds" >/dev/null 2>"$TMP/e1" \
    && fail "--sds sin --t0/--t1 no fallo"
grep -q -- "--sds exige" "$TMP/e1" || fail "mensaje de --sds sin rango"

./pickml "$TMP/sds.d" --sds "$TMP/sds" --tank "$TMP/burst.tank" \
    >/dev/null 2>"$TMP/e2" && fail "--sds + --tank no fallo"
grep -q "exactamente un modo" "$TMP/e2" || fail "mensaje de exclusividad"

./pickml "$TMP/sds.d" --sds "$TMP/noexiste" --t0 "$T0" --t1 "$((T0 + 180))" \
    >/dev/null 2>&1 && fail "SDS inexistente no fallo"

# ------------------------------------- AC2/AC5: paridad 100 Hz y contadores --
./pickml "$TMP/sds.d" --tank "$TMP/burst.tank" --jsonl \
    > "$TMP/tank.jsonl" 2> "$TMP/tank.err"
./pickml "$TMP/sds.d" --sds "$TMP/sds" --t0 "$T0" --t1 "$((T0 + 180))" \
    --jsonl --stats > "$TMP/sds.jsonl" 2> "$TMP/sds.err"

cmp -s "$TMP/tank.jsonl" "$TMP/sds.jsonl" || {
    diff "$TMP/tank.jsonl" "$TMP/sds.jsonl" | head -n 6
    fail "--tank y --sds difieren a 100 Hz"
}
grep -q "\"phase\":\"P\",\"t_epoch\":$TP.000000" "$TMP/sds.jsonl" \
    || fail "falta la P en $TP"
grep -q "\"phase\":\"S\",\"t_epoch\":$TS.000000" "$TMP/sds.jsonl" \
    || fail "falta la S en $TS"
grep -q "sds sin_dia=" "$TMP/sds.err" || fail "faltan los contadores sds"
grep -q "sin_dia=3" "$TMP/sds.err" \
    || fail "NOPE deberia contar 3 (una por componente)"

# ------------------------------------------------ AC3: decimacion 2x --------
./test/gen_sds --tank "$TMP/burst.tank" --root "$TMP/sds2" --factor 2
./pickml "$TMP/sds.d" --sds "$TMP/sds2" --t0 "$T0" --t1 "$((T0 + 180))" \
    --jsonl > "$TMP/sds2.jsonl" 2>/dev/null
tP=$(grep -o '"phase":"P","t_epoch":[0-9.]*' "$TMP/sds2.jsonl" | head -n 1 \
     | sed 's/.*://')
[ -n "$tP" ] || fail "200 Hz: sin P"
awk -v t="$tP" -v p="$TP" 'BEGIN { d = t - p; if (d < 0) d = -d; exit (d > 0.05) }' \
    || fail "200 Hz: P en $tP, fuera de +-0.05 s de $TP"

# ------------------------------------- AC4: rango que cruza medianoche -------
./test/gen_tank --tank "$TMP/mid.tank" --epoch "$MID" --dur 180
./test/gen_sds  --tank "$TMP/mid.tank" --root "$TMP/sdsmid"
./pickml "$TMP/sds.d" --tank "$TMP/mid.tank" --jsonl \
    > "$TMP/mid_tank.jsonl" 2>/dev/null
./pickml "$TMP/sds.d" --sds "$TMP/sdsmid" --t0 "$MID" --t1 "$((MID + 180))" \
    --jsonl > "$TMP/mid_sds.jsonl" 2>/dev/null
cmp -s "$TMP/mid_tank.jsonl" "$TMP/mid_sds.jsonl" || {
    diff "$TMP/mid_tank.jsonl" "$TMP/mid_sds.jsonl" | head -n 6
    fail "el rango que cruza medianoche difiere"
}

# ---------------------------------------------- AC6: tasa no soportada ------
./test/gen_sds --tank "$TMP/burst.tank" --root "$TMP/sds3" --factor 3
./pickml "$TMP/sds.d" --sds "$TMP/sds3" --t0 "$T0" --t1 "$((T0 + 180))" \
    --jsonl --stats > "$TMP/sds3.jsonl" 2> "$TMP/sds3.err"
[ -s "$TMP/sds3.jsonl" ] && fail "300 Hz no deberia producir picadas"
grep -E -q "tasa=[1-9]" "$TMP/sds3.err" || fail "300 Hz no conto en tasa="

echo "SDS OFFLINE TESTS OK"
