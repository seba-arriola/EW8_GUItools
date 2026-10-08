#!/usr/bin/env bash
#
# capture_picks_ps.sh - Barrido OFFLINE de P y S sobre tanks y combinacion por slug.
#
# Encadena los capturadores existentes y el merge idempotente:
#   1) capture_picks.sh    -> <pdir>/<slug>.picks      (P, pick_FP)
#   2) capture_picks_s.sh  -> <sdir>/<slug>.picks      (S, pickS guiado por las P)
#   3) merge_picks.py      -> <out>/<slug>.picks       (P+S, o solo una fase)
#
# La salida combinada es la entrada del modo offline de csnloc:
#   validate_csnloc.sh <out>/<marca de tiempo>
#
# Uso:
#   capture_picks_ps.sh [opciones] <tank|dir> [contenedor]
#
# Opciones:
#   --out DIR        carpeta combinada EXACTA (reanuda salvo --force)
#   --name NAME      subcarpeta compartida P/S/PS (default: YYYYMMDD-HHMMSS)
#   --pdir DIR       carpeta exacta de la captura P (default: picks/<name>)
#   --sdir DIR       carpeta exacta de la captura S (default: picks_s/<name>)
#   --config-p FILE  config de pick_FP (default: run_working_v8/params/pick_FP.d)
#   --config-s FILE  config de pickS  (default: run_working_v8/params/pickS.d)
#   --phases LIST    P | S | PS (default: PS)  <- "solo las S" / "solo las P"
#   --metrics-out DIR  pasa a capture_picks_s.sh (metricas por deteccion)
#   --only GLOB      procesar solo los slugs que casen
#   --force          rehacer (se pasa a los 3 pasos)
#   --dry-run        mostrar que haria y salir
#   -h, --help       ayuda
#
# Reanudacion: cada capturador salta si ya existe <slug>.picks, y el merge
# reconoce los picks ya presentes y los omite; re-ejecutar es inocuo.
#
# Codigos: 0 ok | 1 error de uso | 2 algun tank fallo | 3 faltan binarios
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
PARAMS_DIR="$ROOT_DIR/run_working_v8/params"

CONTAINER="$ROOT_DIR/picks_ps"
OUT=""
NAME=""
PDIR=""
SDIR=""
CONFIG_P="$PARAMS_DIR/pick_FP.d"
CONFIG_S="$PARAMS_DIR/pickS.d"
PHASES="PS"
METRICS_OUT=""
ONLY="*"
FORCE=0
DRYRUN=0

usage() { awk 'NR==1{next} /^#/{sub(/^# ?/,""); print; next} {exit}' "$0"; }

die() { echo "[picksPS] ERROR: $*" >&2; exit 1; }

ARGS=()
while [ $# -gt 0 ]; do
    case "$1" in
        --out)         OUT="$2"; shift 2 ;;
        --name)        NAME="$2"; shift 2 ;;
        --pdir)        PDIR="$2"; shift 2 ;;
        --sdir)        SDIR="$2"; shift 2 ;;
        --config-p)    CONFIG_P="$2"; shift 2 ;;
        --config-s)    CONFIG_S="$2"; shift 2 ;;
        --phases)      PHASES="$2"; shift 2 ;;
        --metrics-out) METRICS_OUT="$2"; shift 2 ;;
        --only)        ONLY="$2"; shift 2 ;;
        --force)       FORCE=1; shift ;;
        --dry-run)     DRYRUN=1; shift ;;
        -h|--help)     usage; exit 0 ;;
        -*)            die "opcion desconocida: $1" ;;
        *)             ARGS+=("$1"); shift ;;
    esac
done

[ ${#ARGS[@]} -ge 1 ] || { usage; exit 1; }
SRC="${ARGS[0]}"
[ ${#ARGS[@]} -ge 2 ] && CONTAINER="${ARGS[1]}"

stamp="$(date +%Y%m%d-%H%M%S)"
[ -n "$NAME" ] && stamp="$NAME"

[ -n "$PDIR" ] || PDIR="$ROOT_DIR/picks/$stamp"
[ -n "$SDIR" ] || SDIR="$ROOT_DIR/picks_s/$stamp"
if [ -n "$OUT" ]; then PS_DIR="$OUT"; else PS_DIR="$CONTAINER/$stamp"; fi

PH_UP="$(printf '%s' "$PHASES" | tr '[:lower:]' '[:upper:]')"
want_p=0; want_s=0
case "$PH_UP" in
    *P*) want_p=1 ;;
esac
case "$PH_UP" in
    *S*) want_s=1 ;;
esac
[ "$want_p" -eq 1 ] || [ "$want_s" -eq 1 ] || die "--phases invalido: $PHASES (usa P, S o PS)"
# La guia P hace falta siempre que se quiera S.
need_p=$want_p
[ "$want_s" -eq 1 ] && need_p=1

case "$SRC" in /*) ;; *) SRC="$(pwd)/$SRC" ;; esac
case "$PDIR" in /*) ;; *) PDIR="$(pwd)/$PDIR" ;; esac
case "$SDIR" in /*) ;; *) SDIR="$(pwd)/$SDIR" ;; esac
case "$PS_DIR" in /*) ;; *) PS_DIR="$(pwd)/$PS_DIR" ;; esac
case "$CONFIG_P" in /*) ;; *) CONFIG_P="$(pwd)/$CONFIG_P" ;; esac
case "$CONFIG_S" in /*) ;; *) CONFIG_S="$(pwd)/$CONFIG_S" ;; esac
[ -n "$METRICS_OUT" ] && case "$METRICS_OUT" in /*) ;; *) METRICS_OUT="$(pwd)/$METRICS_OUT" ;; esac

FLAGS=()
[ "$FORCE" -eq 1 ] && FLAGS+=(--force)
[ "$DRYRUN" -eq 1 ] && FLAGS+=(--dry-run)

echo "[picksPS] fuente : $SRC"
echo "          P      : $PDIR"
echo "          S      : $SDIR"
echo "          combinado: $PS_DIR  (phases=$PH_UP)"

if [ "$need_p" -eq 1 ]; then
    "$SCRIPT_DIR/capture_picks.sh" --out "$PDIR" --config "$CONFIG_P" \
        --only "$ONLY" ${FLAGS[@]+"${FLAGS[@]}"} "$SRC"
fi

if [ "$want_s" -eq 1 ]; then
    MFLAGS=()
    [ -n "$METRICS_OUT" ] && MFLAGS+=(--metrics-out "$METRICS_OUT")
    "$SCRIPT_DIR/capture_picks_s.sh" --out "$SDIR" --config "$CONFIG_S" --ppicks "$PDIR" \
        --only "$ONLY" ${MFLAGS[@]+"${MFLAGS[@]}"} ${FLAGS[@]+"${FLAGS[@]}"} "$SRC"
fi

if [ "$DRYRUN" -eq 1 ]; then
    echo "[picksPS] dry-run: no se ejecuta el merge (las capturas no se escriben)"
    echo "[picksPS] listo (dry-run) -> $PS_DIR"
    exit 0
fi

python3 "$SCRIPT_DIR/merge_picks.py" --out "$PS_DIR" --phases "$PH_UP" --only "$ONLY" \
    ${FLAGS[@]+"${FLAGS[@]}"} "$PDIR" "$SDIR"

echo "[picksPS] listo -> $PS_DIR"
