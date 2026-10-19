#!/bin/bash
# gen_corpus.sh — arma el corpus de validacion del Mww (TSV) desde la base
# hipocentral HISTORICA (el worktree principal, con el catalogo del CSN).
#
# Uso: tools/gen_corpus.sh [salida.tsv] [--min-mag 6.5] [--desde 2015-01-01]
#                          [--max-prof 700]
#      DB_ROOT=<worktree con db/>      default /home/seba/Dev/ew8/EW8_GUItools
#
# La consulta usa la receta de docs/HYPODB.md:104-109 contra la instancia 0 de
# ese worktree (puerto 5435). Un evento entra solo si tiene origen PREFERIDO y
# magnitud PREFERIDA (el filtro de magnitud va sobre la preferida, ver
# ew_gui_tools/common/include/ewgui/db.h:166-170).
#
# Salida (una linea por evento, separado por tabuladores; lo come run_corpus.sh):
#     t0<TAB>lat<TAB>lon<TAB>prof_km<TAB>mag<TAB>event_id
#
# Notas de datos (medidas 2026-10-16):
#   - el catalogo restaurado termina en 2025-03-31: `--desde` mas viejo que eso
#     solo devuelve lo que haya;
#   - profundidad <= 700 km: la base de Green del motor llega a H760.5;
#   - el SDS `/mnt/sds/archive` cubre 2010-2026, asi que las ondas estan.
set -eu
# Decimales con punto en los numeros que se imprimen (AGENTS §7).
export LC_ALL=C

MOD="$(cd "$(dirname "$0")/.." && pwd)"
DB_ROOT="${DB_ROOT:-/home/seba/Dev/ew8/EW8_GUItools}"
OUT="${1:-$MOD/tests/data/corpus.tsv}"
MIN_MAG=6.5
DESDE="$(date -u -d '10 years ago' +%F)"
MAX_PROF=700

shift || true
while [ $# -gt 0 ]; do
    case "$1" in
        --min-mag)  MIN_MAG="$2"; shift 2 ;;
        --desde)    DESDE="$2";   shift 2 ;;
        --max-prof) MAX_PROF="$2"; shift 2 ;;
        -h|--help)  sed -n '2,12p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "gen_corpus: opcion desconocida: $1" >&2; exit 2 ;;
    esac
done

[ -x "$DB_ROOT/ew8_db.sh" ] || {
    echo "gen_corpus: no encuentro $DB_ROOT/ew8_db.sh" >&2
    echo "           exporta DB_ROOT=<worktree del catalogo historico>" >&2
    exit 1
}

SQL="SELECT to_char(o.origin_time AT TIME ZONE 'UTC','YYYY-MM-DD\"T\"HH24:MI:SS'),
            o.latitude, o.longitude, o.depth, m.magnitude_value, e.event_id
     FROM event e
     JOIN origin o ON o.origin_id = e.preferred_origin_id
     JOIN magnitude m ON m.magnitude_id = e.preferred_magnitude_id
     WHERE m.magnitude_value >= $MIN_MAG
       AND o.depth <= $MAX_PROF
       AND o.origin_time >= '$DESDE'
       AND o.origin_time <= now()
     ORDER BY m.magnitude_value DESC, o.origin_time"

echo "consultando $DB_ROOT (M>=$MIN_MAG, desde $DESDE, prof<=$MAX_PROF km)..." >&2
: > "$OUT"
( cd "$DB_ROOT" && ./ew8_db.sh psql -tA -F"$(printf '\t')" -c "$SQL" ) >> "$OUT" || {
    echo "gen_corpus: la consulta fallo (esta arriba la instancia? ./ew8_db.sh instances)" >&2
    exit 1
}

n=$(grep -c . "$OUT" || true)
[ "${n:-0}" -gt 0 ] || { echo "gen_corpus: el corpus quedo vacio" >&2; exit 1; }
echo "corpus: $n eventos -> $OUT" >&2
head -n 5 "$OUT" >&2
