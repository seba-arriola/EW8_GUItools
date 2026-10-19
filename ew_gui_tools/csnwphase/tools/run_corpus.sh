#!/bin/bash
# run_corpus.sh — corre el pipeline COMPLETO de fase W sobre un corpus de
# eventos y deja una linea de resultado por evento en corpus.csv.
#
# Uso: tools/run_corpus.sh <events.tsv> [--limit N] [--tail N] [--force] [--out corpus.csv]
#      CSNWPHASE_CFG=<csnwphase.d>     default $EW_PARAMS/csnwphase.d
#      CSNWPHASE_CORPUS_DIR=<dir>      default $EW_PARAMS/../csnwphase/corpus
#
# Por evento lanza UN `csnwphase --run`, que hace todo el camino: ondas del
# store/SDS (SdsRoot), i_master + CMTSOLUTION, run.sh, motor (screening +
# grid search 3D + xy fina) y wphase.json.
#
# Es reanudable: si el run dir del evento ya tiene wphase.json se salta (--force
# lo repite). El umbral de trazas usadas (CorpusMinNused del .d) NO se aplica
# aqui: cada corrida se registra tal cual y lo juzga corpus_report.sh.
#
# Columnas del CSV: event_id,mag_bd,t0,lat,lon,dep_km,rc,ok,converged,mw,n_used,seg
set -u
# Decimales con punto: el modulo y el CSV se parsean con `.` (AGENTS §7).
export LC_ALL=C

MOD="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${CSNWPHASE_BIN:-$MOD/csnwphase}"
: "${EW_PARAMS:?falta EW_PARAMS (source ./ew8_unix.sh)}"
CFG="${CSNWPHASE_CFG:-$EW_PARAMS/csnwphase.d}"
CORPUS_DIR="${CSNWPHASE_CORPUS_DIR:-$EW_PARAMS/../csnwphase/corpus}"
OUT="corpus.csv"
LIMIT=0
TAILN=0
FORCE=0

ARGS=()
while [ $# -gt 0 ]; do
    case "$1" in
        --limit) LIMIT="$2"; shift 2 ;;
        --tail)  TAILN="$2"; shift 2 ;;
        --force) FORCE=1; shift ;;
        --out)   OUT="$2"; shift 2 ;;
        -h|--help) sed -n '2,16p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        -*) echo "run_corpus: opcion desconocida: $1" >&2; exit 2 ;;
        *)  ARGS+=("$1"); shift ;;
    esac
done
EVENTS="${ARGS[0]:-}"
[ -n "$EVENTS" ] && [ -f "$EVENTS" ] || { echo "uso: run_corpus.sh <events.tsv> [--limit N]" >&2; exit 2; }
# --tail N: los N ULTIMOS de la lista (el corpus va por magnitud descendente, asi
# que son los mas chicos: para un chequeo rapido). El tope de arriba (M8) barre
# ts hasta 168 s y puede tardar mucho mas.
if [ "$TAILN" -gt 0 ]; then
    tail -n "$TAILN" "$EVENTS" > "$EVENTS.tail"
    EVENTS="$EVENTS.tail"
fi
[ -x "$BIN" ] || { echo "run_corpus: no esta compilado $BIN (make)" >&2; exit 1; }
[ -f "$CFG" ] || { echo "run_corpus: no encuentro $CFG" >&2; exit 1; }

# El tope del motor (TimeoutSec) mas un margen para el fetch de ondas.
TMO=$("$BIN" "$CFG" --print-config | awk '/^TimeoutSec/{print $2}')
TMO=$(( ${TMO:-1800} + 300 ))

mkdir -p "$CORPUS_DIR"
printf 'event_id,mag_bd,t0,lat,lon,dep_km,rc,ok,converged,mw,n_used,seg\n' > "$OUT"

n=0
ran=0
while IFS="$(printf '\t')" read -r t0 lat lon dep mag ev; do
    [ -n "${ev:-}" ] || continue
    n=$((n + 1))
    [ "$LIMIT" -gt 0 ] && [ "$n" -gt "$LIMIT" ] && break

    run="$CORPUS_DIR/$ev"
    if [ "$FORCE" -eq 0 ] && [ -f "$run/wphase.json" ]; then
        echo "[$n] $ev: ya tiene wphase.json (--force para repetir)"
    else
        mkdir -p "$run"
        echo "[$n] $ev: mag=$mag t0=$t0 prof=$dep (run dir $run)"
        t_start=$(date +%s)
        /usr/bin/timeout "$TMO" "$BIN" "$CFG" --run "$run" "$t0" "$lat" "$lon" \
            "$dep" "$mag" "$ev" > "$run/csnwphase.out" 2>&1
        rc=$?
        seg=$(( $(date +%s) - t_start ))
        ran=$((ran + 1))
        ok=$(sed -n 's/^ok=\([0-9]*\).*/\1/p'        "$run/csnwphase.out" | tail -1)
        cv=$(sed -n 's/^ok=[0-9]* converged=\([0-9]*\).*/\1/p' "$run/csnwphase.out" | tail -1)
        mw=$(sed -n 's/.*Mw=\([-0-9.]*\).*/\1/p'     "$run/csnwphase.out" | tail -1)
        nu=$(sed -n 's/.*n_used=\([0-9]*\).*/\1/p'   "$run/csnwphase.out" | tail -1)
        printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n' "$ev" "$mag" "$t0" "$lat" \
            "$lon" "$dep" "$rc" "${ok:-}" "${cv:-}" "${mw:-}" "${nu:-}" "$seg" >> "$OUT"
        [ "$rc" -eq 0 ] || echo "    rc=$rc (ver $run/csnwphase.out)" >&2
        continue
    fi

    # Reanudado: se recupera lo que ya estaba parseado.
    json="$run/wphase.json"
    mw=$(sed -n 's/.*"mw": *\([-0-9.]*\).*/\1/p'       "$json" | head -1)
    nu=$(sed -n 's/.*"n_used": *\([0-9]*\).*/\1/p'      "$json" | head -1)
    cv=$(sed -n 's/.*"converged": *\([0-9]*\).*/\1/p'   "$json" | head -1)
    printf '%s,%s,%s,%s,%s,%s,0,%s,%s,%s,%s,\n' "$ev" "$mag" "$t0" "$lat" \
        "$lon" "$dep" "${cv:-}" "${cv:-}" "${mw:-}" "${nu:-}" >> "$OUT"
done < "$EVENTS"

echo "corridas nuevas en esta pasada: $ran; CSV: $OUT"
echo "informe: tools/corpus_report.sh $OUT $CFG"
