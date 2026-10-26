#!/bin/sh
# calib_run.sh — arma el corpus de calibracion y lo corre con `pickml`.
#
#   cd run_working_v8/params
#   sh ../../ew_gui_tools/pickml/tools/calib_run.sh \
#        pickml_fetch_calib.d /tmp/opencode/events.list ../pickml/calib \
#        /tmp/opencode/pnwc/calib.d
#
# Por cada evento:
#   1. `pickml_fetch` escribe <out>/<event>/{event.traces,picks_ref.csv,stations.sta}
#      (del store local si esta; el SDS solo como respaldo).
#   2. `pickml` corre sobre ese trace y deja `<out>/<event>/curve.{p,s,det}.f32`
#      + `curve.idx` (el volcado autocontenido que consume tools/sweep_post.py)
#      y `picks.jsonl`.
#
# El `StaFile` del `.d` plantilla se reemplaza por la UNION de los stations.sta
# de todos los eventos, para poder usar un solo .d. `pickml` descarta las trazas
# que no estan en el archivo, asi que la union no cambia ningun resultado.
#
# Todo queda en <out> (nunca en params/). Ver docs/DL_to-do.md.
set -e

FETCH_D=${1:?uso: calib_run.sh <fetch.d> <events.list> <outdir> <pickml.d>}
EVENTS=${2:?}
OUT=${3:?}
PM_D=${4:?}

mkdir -p "$OUT"
: > "$OUT/run.log"

# 1. fetch por evento
while IFS= read -r line; do
    ev=$(printf '%s' "$line" | awk '{print $1}')
    [ -z "$ev" ] && continue
    case "$ev" in \#*) continue ;; esac
    d="$OUT/$ev"
    mkdir -p "$d"
    if [ -s "$d/event.traces" ] && [ -s "$d/picks_ref.csv" ]; then
        echo "fetch $ev: ya existe" >> "$OUT/run.log"
        continue
    fi
    pickml_fetch "$FETCH_D" --event "$ev" --out "$d" >> "$OUT/run.log" 2>&1 \
        || echo "fetch $ev: FALLO" >> "$OUT/run.log"
done < "$EVENTS"

# 2. union de estaciones
{
    echo "# PickFlag Pin Sta Net Loc ChanE ChanN ChanZ"
    for f in "$OUT"/*/stations.sta; do
        [ -f "$f" ] || continue
        grep -hv '^#' "$f"
    done | awk '!seen[$3 $4 $5 $6 $7 $8]++'
} > "$OUT/stations_all.sta"
nsta=$(grep -vc '^#' "$OUT/stations_all.sta" || true)
echo "estaciones en la union: $nsta" >> "$OUT/run.log"

# 3. .d con la union
sed "s#^StaFile.*#StaFile $OUT/stations_all.sta#" "$PM_D" > "$OUT/pickml_calib.d"

# 4. corrida + volcado
for d in "$OUT"/*/; do
    [ -f "$d/event.traces" ] || continue
    pickml "$OUT/pickml_calib.d" --traces "$d/event.traces" \
           --dump-curves "$d/curve" --out "$d/picks.jsonl" --stats \
           >> "$OUT/run.log" 2>&1 || echo "pickml $d: FALLO" >> "$OUT/run.log"
done

echo "listo: $OUT"
