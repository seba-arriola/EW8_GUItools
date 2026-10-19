#!/bin/bash
# corpus_report.sh — juzga el corpus corrido contra los criterios de validacion.
#
# Uso: tools/corpus_report.sh [corpus.csv] [csnwphase.d]
#      CSNWPHASE_MIN_NUSED=<n>   sobreescribe la clave CorpusMinNused del .d
#
# Criterios (los del plan, con el umbral de trazas del .d):
#   1. >= 80% de las corridas con rc=0 y converged=1 (el motor no falla);
#   2. |Mw - magnitud del catalogo| <= 0.3 en los eventos con
#      n_used >= CorpusMinNused (default 7): una inversion con pocas trazas no
#      se juzga, se cuenta aparte como "sin trazas suficientes";
#   3. ninguna corrida con el fallo del cmtfile de la pasada xy fina
#      (`field(s) redundancy`, la regresion arreglada en run.sh).
#
# Devuelve 0 si se cumplen, 1 si no (para `make check-corpus`).
set -u
# OJO: sin esto `awk` compara/printf con la coma decimal del locale (AGENTS §7)
# y las diferencias de magnitud salen mal.
export LC_ALL=C

MOD="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${CSNWPHASE_BIN:-$MOD/csnwphase}"
CSV="${1:-corpus.csv}"
CFG="${2:-$EW_PARAMS/csnwphase.d}"
: "${EW_PARAMS:?falta EW_PARAMS (source ./ew8_unix.sh)}"
[ -f "$CSV" ] || { echo "corpus_report: no encuentro $CSV" >&2; exit 2; }
CORPUS_DIR="${CSNWPHASE_CORPUS_DIR:-$EW_PARAMS/../csnwphase/corpus}"

MIN_NUSED="${CSNWPHASE_MIN_NUSED:-}"
if [ -z "$MIN_NUSED" ] && [ -x "$BIN" ] && [ -f "$CFG" ]; then
    MIN_NUSED=$("$BIN" "$CFG" --print-config | awk '/^CorpusMinNused/{print $2}')
fi
MIN_NUSED="${MIN_NUSED:-7}"

TOL=0.3
tot=0; okc=0; valid=0; dentro=0; pocas=0
sum=0.0; worst=""; worstd=0.0; redund=0

echo "evento         mag_bd    Mww   n_used   |dM|   veredicto"
while IFS=, read -r ev mag t0 lat lon dep rc ok cv mw nu seg; do
    [ "$ev" = "event_id" ] && continue
    [ -n "${ev:-}" ] || continue
    tot=$((tot + 1))
    if [ "${rc:-1}" = "0" ] && [ "${cv:-0}" = "1" ]; then okc=$((okc + 1)); fi
    if [ "${nu:-0}" -ge "$MIN_NUSED" ] && [ -n "${mw:-}" ] && [ "$mw" != "-" ]; then
        valid=$((valid + 1))
        d=$(awk -v a="$mw" -v b="$mag" 'BEGIN{v=a-b; if (v<0) v=-v; printf "%.3f", v}')
        sum=$(awk -v s="$sum" -v d="$d" 'BEGIN{printf "%.3f", s+d}')
        if awk -v d="$d" -v t="$TOL" 'BEGIN{exit !(d <= t)}'; then
            dentro=$((dentro + 1)); ver="ok"
        else
            ver="FUERA"; [ -z "$worst" ] && worst="$ev"
        fi
        if awk -v d="$d" -v w="$worstd" 'BEGIN{exit !(d > w)}'; then worstd="$d"; worst="$ev"; fi
        printf '%-14s %6s %7s %6s %7s   %s\n' "$ev" "$mag" "$mw" "$nu" "$d" "$ver"
    else
        pocas=$((pocas + 1))
        printf '%-14s %6s %7s %6s %7s   %s\n' "$ev" "$mag" "${mw:--}" "${nu:-0}" "-" \
               "sin trazas suficientes (< $MIN_NUSED)"
    fi
done < "$CSV"

if [ -s "$CSV" ] && grep -q 'redundancy' "$CSV" 2>/dev/null; then redund=1; fi

echo
echo "corridas: $tot | rc=0 y convergidas: $okc | juzgadas (n_used >= $MIN_NUSED): $valid | dentro de ${TOL}: $dentro | sin trazas: $pocas"
if [ "$valid" -gt 0 ]; then
    echo "|dM| medio (juzgadas): $(awk -v s="$sum" -v n="$valid" 'BEGIN{printf "%.3f", s/n}') | peor: $worst ($worstd)"
fi

fail=0
if [ "$tot" -eq 0 ]; then
    echo "FALLO: el CSV no tiene corridas"; fail=1
fi
if [ "$tot" -gt 0 ] && [ $((okc * 100)) -lt $((tot * 80)) ]; then
    echo "FALLO: solo $okc/$tot corridas rc=0 y convergidas (< 80%)"; fail=1
fi
if [ "$valid" -gt 0 ] && [ $((dentro * 100)) -lt $((valid * 80)) ]; then
    echo "FALLO: solo $dentro/$valid dentro de $TOL de la magnitud del catalogo (< 80%)"; fail=1
fi
[ "$pocas" -eq "$tot" ] && { echo "FALLO: ninguna corrida alcanzo $MIN_NUSED trazas"; fail=1; }
# El fallo del cmtfile de la pasada xy fina no se ve en el CSV (solo rc!=0):
# se busca en la salida de cada corrida. Criterio 3 del plan. Se busca el
# mensaje COMPLETO del motor: el texto del comentario del run.sh tambien dice
# `field(s) redundancy` y el run.log ecoa el script entero.
if [ -d "$CORPUS_DIR" ] && \
   grep -rl 'ERROR: reading cmtfile' "$CORPUS_DIR" >/dev/null 2>&1; then
    echo "FALLO: hay corridas con 'ERROR: reading cmtfile' (regresion del cmtfile)"; fail=1
fi

[ "$fail" -eq 0 ] || exit 1
echo "CORPUS OK"
