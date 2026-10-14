#!/bin/bash
# AC3: la cabecera SAC que escribe csnwphase la lee el binario REAL del motor
# (saclst) con los campos que el motor necesita (trim_sac_files/rec_dec_filt/
# wpinversion), y con la location vacía en blanco (no "-12345") para que
# makeid() produzca NET_STA_--_CHAN.
#
# Uso: tests/test_sac_engine.sh [WphaseBin]
set -e

MOD="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${1:-/home/seba/Dev/wphase/CSNop/wphase-prod/bin}"
D="/tmp/csnwphase_sac_engine"
F="$D/C1.AF02.BHZ.SAC"

if [ ! -x "$BIN/saclst" ]; then
    echo "SKIP: no existe $BIN/saclst"
    exit 0
fi

mkdir -p "$D"
"$MOD/test_sac" "$D"

echo "--- campos numericos ---"
NUM="$("$BIN/saclst" delta npts o b e stla stlo evla evlo evdp gcarc f "$F" 2>&1)" || true
echo "$NUM"

echo "--- campos de texto (uno por invocacion) ---"
# saclst imprime "<fichero> <valor>": hay que DEScartar el primer token. Si se
# compara sobre la salida cruda, "C1"/"AF02"/"BHZ" del nombre del fichero hacen
# pasar las comprobaciones por vacuidad (asi se colo un desalineamiento real de
# los offsets del bloque de texto de la cabecera).
sval() { "$BIN/saclst" "$2" f "$1" 2>&1 | awk '{ $1=""; sub(/^[ \t]+/,""); sub(/[ \t]+$/,""); print }'; }
KSTNM="$(sval "$F" kstnm)" || true
KNETWK="$(sval "$F" knetwk)" || true
KCMPNM="$(sval "$F" kcmpnm)" || true
KHOLE="$(sval "$F" khole)" || true
printf 'kstnm=[%s] knetwk=[%s] kcmpnm=[%s] khole=[%s]\n' "$KSTNM" "$KNETWK" "$KCMPNM" "$KHOLE"

fail=0
chk() { # <patron> <texto> <descripcion>
    grep -qE "$1" <<<"$2" || { echo "FALLO: $3"; fail=1; }
}
chkeq() { # <esperado> <texto> <descripcion>  -- comparacion EXACTA
    [ "$1" = "$2" ] || { echo "FALLO: $3 (esperado [$1], leido [$2])"; fail=1; }
}
chk '0\.05([^0-9]|$)' "$NUM" "delta debe ser 0.05 (20 sps)"
chk '6000([^0-9]|$)'  "$NUM" "npts debe ser 6000"
chk '(^|[^0-9-])60(\.0+)?([^0-9]|$)' "$NUM" "o debe ser 60 s (origen - inicio)"
chk '(^|[^0-9.-])0(\.0+)?([^0-9]|$)' "$NUM" "b debe ser 0"
chk '299\.9'          "$NUM" "e debe ser 299.95 s"
chk '\-31\.573'       "$NUM" "evla debe ser -31.573"
chk '\-71\.674'       "$NUM" "evlo debe ser -71.674"
chk '(^|[^0-9.-])25([^0-9]|$)' "$NUM" "evdp debe ser 25 km"
chk '9\.17'           "$NUM" "gcarc debe ser ~9.17 grados"
chkeq 'AF02' "$KSTNM" "kstnm debe ser AF02"
chkeq 'C1'   "$KNETWK" "knetwk debe ser C1"
chkeq 'BHZ'  "$KCMPNM" "kcmpnm debe ser BHZ"

# location vacia: en blanco (o "--"), NUNCA "-12345" (rompe makeid()):
# NET_STA_--_CHAN. Un desalineamiento de offsets mete aqui trozos de otros campos.
chk '^(-{2})?$' "$KHOLE" "khole debe ir en blanco (location vacia), no -12345"

[ "$fail" -eq 0 ] || exit 1
echo "ALL CSNWPHASE SAC ENGINE TESTS PASSED"
