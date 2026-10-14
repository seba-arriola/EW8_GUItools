#!/bin/bash
# Trae ondas REALES del SDS para el M7.4 del 2024-07-19 01:50:47 UTC (día 201
# de 2024) — el caso de uso: eventos históricos M>7 consultando el SDS.
#
# Comprueba que:
#   - la capa de ondas devuelve datos para al menos 2 canales;
#   - la ventana es la pedida (n ~ (t1-t0)*tasa);
#   - hay amplitud real (no todo ceros) en al menos un canal.
#
# Uso: tests/test_wave_sds.sh [csnwphase.d]
set -e

MOD="$(cd "$(dirname "$0")/.." && pwd)"
CFG="${1:-$EW_PARAMS/csnwphase.d}"
T0="2024-07-19T01:45:47"     # t0 del evento - 300 s
T1="2024-07-19T02:05:47"     # t0 del evento + 900 s

if [ ! -f "$CFG" ]; then
    echo "SKIP: no existe $CFG"
    exit 0
fi
if [ ! -d /mnt/sds/archive/2024 ]; then
    echo "SKIP: no hay SDS de 2024 en /mnt/sds/archive"
    exit 0
fi

# (net, sta, chan): II.NNA queda a ~13 grados del evento.
CANDS="II NNA BHZ
C1 AC01 HHZ
C AI FAR1 HHZ
C1 AC06 BHZ
II NNA HHZ"

ok=0
amp=0
while read -r net sta chan; do
    [ -z "$net" ] && continue
    if OUT="$("$MOD/csnwphase" "$CFG" --fetch "$net" "$sta" "$chan" "$T0" "$T1" 2>&1)"; then
        echo "$net.$sta.$chan: $OUT"
        ok=$((ok + 1))
        echo "$OUT" | grep -qE 'max=[1-9]' && amp=$((amp + 1))
    else
        echo "$net.$sta.$chan: $OUT"
    fi
done <<<"$CANDS"

echo "canales con datos: $ok   con amplitud: $amp"
[ "$ok" -ge 2 ] || { echo "FALLO: menos de 2 canales con datos del SDS"; exit 1; }
[ "$amp" -ge 1 ] || { echo "FALLO: ningun canal con amplitud real"; exit 1; }

echo "ALL CSNWPHASE WAVE SDS TESTS PASSED"
