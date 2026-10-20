#!/usr/bin/env bash
# Invariante de configuracion del anillo de CONFIRMACIONES manuales:
#   startstop_unix.d  declara el Ring          (lo crea al arrancar)
#   earthworm.d(.base) le da la clave          (GetKey)
#   csnhypodbp.d      ConfRing   <nombre>      (por ahi publica el ARC Confirm)
#   hypodb.d          InRingConf <nombre>      (por ahi lo ingiere)
#
# Si los tres nombres no coinciden, la confirmacion se publica a un anillo que
# nadie lee (o no existe) y la fila `origin` no llega a la BD, en silencio.
# Se ejecuta desde `make -C ew_gui_tools/common check`.
set -euo pipefail
export LC_ALL=C

root="$(cd "$(dirname "$0")/../../.." && pwd)"   # raiz del repo
params="$root/run_working_v8/params"
fail=0

err() { echo "FAIL: $*"; fail=1; }

ss="$params/startstop_unix.d"
hyp="$params/hypodb.d"
bp="$params/csnhypodbp.d"

for f in "$ss" "$hyp" "$bp"; do
    [ -f "$f" ] || { echo "FAIL: falta $f"; exit 1; }
done

# 1) El anillo de confirmaciones esta declarado en startstop.
conf_bp="$(awk '$1 == "ConfRing" { print $2; exit }' "$bp")"
conf_hyp="$(awk '$1 == "InRingConf" { print $2; exit }' "$hyp")"
[ -n "$conf_bp" ] || err "csnhypodbp.d no declara ConfRing"
[ -n "$conf_hyp" ] || err "hypodb.d no declara InRingConf"
[ "$conf_bp" = "$conf_hyp" ] || \
    err "ConfRing ($conf_bp) != InRingConf ($conf_hyp)"
[ -n "$conf_bp" ] && awk -v r="$conf_bp" \
    '$1 == "Ring" && $2 == r { found = 1 } END { exit !found }' "$ss" || \
    err "startstop_unix.d no declara Ring $conf_bp"

# 2) nRing coincide con las lineas Ring (un anillo de mas NO se crea).
n_decl="$(awk '$1 == "nRing" { print $2; exit }' "$ss")"
n_ring="$(awk '$1 == "Ring" { n++ } END { print n + 0 }' "$ss")"
[ -n "$n_decl" ] || err "startstop_unix.d no declara nRing"
if [ -n "$n_decl" ] && [ "$n_decl" != "$n_ring" ]; then
    err "nRing=$n_decl pero hay $n_ring lineas Ring"
fi

# 3) La clave del anillo existe (earthworm.d.base es el fichero versionado).
base="$params/earthworm.d.base"
[ -f "$base" ] || err "falta $base"
if [ -f "$base" ] && ! awk -v r="$conf_bp" \
    '$1 == "Ring" && $2 == r { found = 1 } END { exit !found }' "$base"; then
    err "earthworm.d.base no da clave a $conf_bp"
fi

if [ "$fail" -eq 0 ]; then
    echo "OK: anillo de confirmaciones ($conf_bp) declarado y coherente"
fi
exit "$fail"
