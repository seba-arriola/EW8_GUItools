#!/bin/sh
# test_engine.sh — gate de PARIDAD NUMÉRICA del motor ONNX.
#
# Se ejecuta desde `make check-engine ORT_DIR=...` y se SALTA (exit 0) si no
# hay fixture dorado: el build base nunca depende de ONNX Runtime.
#
# Criterio: las curvas P y S que produce el C con ONNX Runtime deben coincidir
# con las que produce PyTorch (referencia) a 1e-4 en valor absoluto. Si además
# la referencia tiene un pico P claro, el pick emitido debe caer en él (±2
# muestras = 20 ms).
#
# Fixtures (los genera tools/export_onnx.py con --fixture-tank/--fixture-t0):
#   test/fixtures/win1.tank        ventana 3C de 60 s, 100 Hz
#   test/fixtures/win1.ref.p.f32   curva P de referencia (float32 x 6000)
#   test/fixtures/win1.ref.s.f32   curva S de referencia
#
# Variables:
#   PICKML_MODEL  ruta del .onnx             (obligatoria si hay fixtures)
#   PICKML_ORT    ruta de libonnxruntime.so  (obligatoria si hay fixtures)

set -e

cd "$(dirname "$0")/.."
FIX=test/fixtures
TMP=test/tmp

if [ ! -f "$FIX/win1.tank" ]; then
    echo "check-engine: SKIP (falta $FIX/win1.tank; generarlo con tools/export_onnx.py)"
    exit 0
fi
if [ -z "$PICKML_MODEL" ] || [ -z "$PICKML_ORT" ]; then
    echo "check-engine: SKIP (definir PICKML_MODEL y PICKML_ORT)"
    exit 0
fi

mkdir -p "$TMP"

cat > "$TMP/engine_gate.sta" <<'EOF'
1 1 TEST XX -- HHE HHN HHZ
EOF

cat > "$TMP/engine_gate.d" <<EOF
MyModuleId      MOD_PICKML
StaFile         $TMP/engine_gate.sta
Engine          onnx
ModelFile       $PICKML_MODEL
OrtLib          $PICKML_ORT
OrtThreads      1
WindowSamples   6000
Rate            100.0
StepSec         10.0
MinPosSec       0.0
MaxPosSec       0.0
BlindSamples    0
PThreshold      0.0
SThreshold      0.0
DedupSec        0.0
MaxGapSec       0.0
EOF

# 1. el grafo debe aceptar [B,3,6000]: falla al arrancar, no en silencio
./pickml "$TMP/engine_gate.d" --check-model

# 2. inferencia de la ventana dorada, curvas crudas + picks
./pickml "$TMP/engine_gate.d" --tank "$FIX/win1.tank" --jsonl \
        --dump-curves "$TMP/curve" > "$TMP/eng_a.jsonl"
./pickml "$TMP/engine_gate.d" --tank "$FIX/win1.tank" --jsonl \
        > "$TMP/eng_b.jsonl"
cmp -s "$TMP/eng_a.jsonl" "$TMP/eng_b.jsonl" || {
    echo "FALLO: la inferencia ONNX no es determinista"; exit 1; }

# 3. paridad de curvas contra PyTorch
python3 - "$TMP/curve.p.f32" "$FIX/win1.ref.p.f32" \
          "$TMP/curve.s.f32" "$FIX/win1.ref.s.f32" \
          "$FIX/win1.tank" "$TMP/eng_a.jsonl" <<'PY'
import json, struct, sys

got_p, ref_p, got_s, ref_s, tank, picks = sys.argv[1:7]

def read_f32(path):
    blob = open(path, "rb").read()
    n = len(blob) // 4
    return struct.unpack(f"<{n}f", blob[:4 * n])

gp, rp = read_f32(got_p), read_f32(ref_p)
gs, rs = read_f32(got_s), read_f32(ref_s)

if min(len(gp), len(rp), len(gs), len(rs)) < 6000:
    sys.exit(f"FALLO: curvas incompletas (P {len(gp)}/{len(rp)}, S {len(gs)}/{len(rs)})")

dp = max(abs(gp[i] - rp[i]) for i in range(6000))
ds = max(abs(gs[i] - rs[i]) for i in range(6000))
if dp > 1e-4 or ds > 1e-4:
    sys.exit(f"FALLO: paridad fuera de tolerancia (P {dp:.2e}, S {ds:.2e} > 1e-4)")

# Picks: sólo si la referencia tiene un pico claro (el fixture puede ser sintético)
HDR = struct.Struct("<iiddd7s9s4s3s2s3s2s2s")
with open(tank, "rb") as f:
    t0 = HDR.unpack(f.read(HDR.size))[2]

best = max(range(6000), key=lambda i: rp[i])
if rp[best] >= 0.30:
    pick = None
    for line in open(picks):
        r = json.loads(line)
        if r["phase"] == "P":
            pick = r
            break
    if pick is None:
        sys.exit(f"FALLO: la referencia tiene P={rp[best]:.3f} y no se emitió ningún pick")
    idx = int(round((pick["t_epoch"] - t0) * 100.0))
    if abs(idx - best) > 2:
        sys.exit(f"FALLO: P en la muestra {idx}, pico de referencia en {best}")

print(f"check-engine: paridad OK (P {dp:.2e}, S {ds:.2e}; pico ref {rp[best]:.3f} en {best})")
PY

echo "check-engine: OK"
