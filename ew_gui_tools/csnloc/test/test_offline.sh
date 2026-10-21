#!/usr/bin/env bash
#
# test_offline.sh - Verifica el modo OFFLINE de csnloc (reloj virtual).
#
# Genera picks sinteticos (gen_offline_picks), corre csnloc en modo offline
# dos veces y comprueba: hay eventos, es determinista, y la localizacion cae
# dentro de la tolerancia del hipocentro sintetico.
#
# Requiere EW_PARAMS/EW_LOG (source ../../ew8_unix.sh) porque csnloc resuelve
# tipos y anillos por nombre aunque en offline no los use.
#
set -euo pipefail
cd "$(dirname "$0")/.."   # ew_gui_tools/csnloc

BIN=./csnloc
[ -x "$BIN" ] || { echo "FAIL: falta ./csnloc (compila con make)"; exit 1; }

echo "=== test_offline ==="

./test/gen_offline_picks > test/test_offline.picks
npk="$(wc -l < test/test_offline.picks)"
echo "ok  : generados $npk picks sinteticos"
[ "$npk" -ge 5 ] || { echo "FAIL: muy pocos picks"; exit 1; }

out1="$("$BIN" test/csnloc_offline.d test/test_offline.picks 2>/dev/null)"
out2="$("$BIN" test/csnloc_offline.d test/test_offline.picks 2>/dev/null)"

if [ -z "$out1" ]; then echo "FAIL: sin salida JSON"; exit 1; fi
if [ "$out1" != "$out2" ]; then echo "FAIL: no determinista"; exit 1; fi
echo "ok  : determinista (2 corridas identicas)"

printf '%s\n' "$out1" | python3 -c '
import json, sys
recs = [json.loads(l) for l in sys.stdin if l.strip()]
assert recs, "sin eventos localizados"
r = recs[0]
print("ok  : %d evento(s); lat=%.3f lon=%.3f z=%.1f nph=%d rms=%.2f"
      % (len(recs), r["lat"], r["lon"], r["depth_km"], r["nphases"], r["rms_sec"]))
assert -24.5 <= r["lat"] <= -22.5, "lat fuera de rango: %r" % r["lat"]
assert -71.5 <= r["lon"] <= -68.5, "lon fuera de rango: %r" % r["lon"]
assert r["nphases"] >= 3, "pocas fases: %r" % r["nphases"]
assert len(r.get("phases", [])) >= 3, "sin fases en el JSON"
print("ok  : localizacion dentro de la tolerancia y con fases")
'

# Profundidad fija: la solucion debe salir EXACTAMENTE en ese valor (el ARC la
# escribe en centesimas). Se usa una copia del .d con la clave anadida.
fixd="$(mktemp /tmp/csnloc_fixdepth_XXXXXX.d)"
trap 'rm -f "$fixd"' EXIT
cat test/csnloc_offline.d > "$fixd"
echo "FixedDepthKm 33.0" >> "$fixd"
out3="$("$BIN" "$fixd" test/test_offline.picks 2>/dev/null)"
if [ -z "$out3" ]; then echo "FAIL: sin salida JSON con FixedDepthKm"; exit 1; fi
printf '%s\n' "$out3" | python3 -c '
import json, sys
recs = [json.loads(l) for l in sys.stdin if l.strip()]
assert recs, "sin eventos localizados con FixedDepthKm"
z = recs[0]["depth_km"]
print("ok  : profundidad fija z=%.2f (objetivo 33.00)" % z)
assert abs(z - 33.0) <= 0.005, "profundidad no fija: %r" % z
'

echo "OK test_offline"
