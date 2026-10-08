#!/usr/bin/env bash
#
# validate_manual.sh - Ensayo de validación del picker S (pickS) contra las
# picadas manuales de picks_por_tests410.dat y la solución hipocentral.
#
# Pasos:
#   1) extrae los eventos del .dat a TSV
#   2) predice tiempos P/S con ttp (motor IASP91 de csnloc)
#   3) genera la guía P por evento (para el modo hybrid)
#   4) captura picks S offline (independent y/o hybrid) + métricas intrínsecas
#   5) reporte de validación (chequeo suelto vs S manuales + certeza por parámetros)
#   6) calibración de umbrales intrínsecos (barrido sobre keep+drop)
#
# Uso:
#   ./tank_tools/validate_manual.sh [opciones]
#
#   --dat FILE        .dat de picadas manuales (default: picks_por_tests410.dat)
#   --tanks DIR       repo de tanks (default: tank_repo)
#   --work DIR        directorio de trabajo (default: /tmp/opencode/picks_manual)
#   --only GLOB       procesar solo slugs que casen (default: *)
#   --mode both|ind|hyb  qué capturas hacer (default: both)
#   --match-tol S     tolerancia de emparejamiento con S manual (default: 2.0)
#   --resid-tol S     tolerancia de residual vs solución (default: 2.0)
#   --max-weight N    weight máximo para "BIEN" (default: 4)
#   --skip-capture    no re-capturar (reusar capturas existentes)
#   --config FILE     config de pickS (default: run_working_v8/params/pickS.d)
#   -h, --help        ayuda
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
PARAMS_DIR="$ROOT_DIR/run_working_v8/params"

DAT="$ROOT_DIR/picks_por_tests410.dat"
TANKS="$ROOT_DIR/tank_repo"
WORK="${TMPDIR:-/tmp}/opencode/picks_manual"
ONLY="*"
RANGE=""
MODE="both"
MATCH_TOL="2.0"
RESID_TOL="2.0"
MAX_WEIGHT="4"
SKIP_CAPTURE=0
CONFIG="$PARAMS_DIR/pickS.d"

usage() { awk 'NR==1{next} /^#/{sub(/^# ?/,""); print; next} {exit}' "$0"; }
die() { echo "[validate-manual] ERROR: $*" >&2; exit 1; }

while [ $# -gt 0 ]; do
    case "$1" in
        --dat)          DAT="$2"; shift 2 ;;
        --tanks)        TANKS="$2"; shift 2 ;;
        --work)         WORK="$2"; shift 2 ;;
        --only)         ONLY="$2"; shift 2 ;;
        --range)        RANGE="$2"; shift 2 ;;
        --mode)         MODE="$2"; shift 2 ;;
        --match-tol)    MATCH_TOL="$2"; shift 2 ;;
        --resid-tol)    RESID_TOL="$2"; shift 2 ;;
        --max-weight)   MAX_WEIGHT="$2"; shift 2 ;;
        --skip-capture) SKIP_CAPTURE=1; shift ;;
        --config)       CONFIG="$2"; shift 2 ;;
        -h|--help)      usage; exit 0 ;;
        *) die "opcion desconocida: $1" ;;
    esac
done

[ -f "$DAT" ]  || die "no existe el .dat: $DAT"
[ -d "$TANKS" ] || die "no existe el repo de tanks: $TANKS"

if [ -z "${EW_HOME:-}" ] || [ -z "${EW_VERSION:-}" ]; then
    [ -f "$ROOT_DIR/ew8_unix.sh" ] || die "no hay EW_HOME y no encuentro ew8_unix.sh"
    # shellcheck disable=SC1091
    source "$ROOT_DIR/ew8_unix.sh" >/dev/null
fi

mkdir -p "$WORK"

PY="python3"
PSM="$ROOT_DIR/tank_tools/picks_s_manual.py"
GEN="$ROOT_DIR/tank_tools/gen_guide_p.py"
CAP="$ROOT_DIR/tank_tools/capture_picks_s.sh"
TTP="$ROOT_DIR/ew_gui_tools/csnloc/ttpred"

echo "[validate-manual] work=$WORK  only=$ONLY  range=$RANGE  mode=$MODE"

# 1) eventos
"$PY" "$PSM" events --dat "$DAT" --out "$WORK/events.tsv" --only "$ONLY" --range "$RANGE"

# 2) tiempos de viaje
[ -x "$TTP" ] || make -C "$ROOT_DIR/ew_gui_tools/csnloc" ttpred >/dev/null
( cd "$ROOT_DIR/ew_gui_tools/csnloc" && "$TTP" "$PARAMS_DIR/estaciones_107.txt" "$WORK/events.tsv" ) > "$WORK/tt.tsv"
echo "[validate-manual] tt.tsv: $(wc -l < "$WORK/tt.tsv") filas"

# 3) guía P
"$PY" "$GEN" --dat "$DAT" --out "$WORK/picks_p_manual" --only "$ONLY"

# 4) capturas
if [ "$SKIP_CAPTURE" -eq 0 ]; then
    if [ "$MODE" = "both" ] || [ "$MODE" = "ind" ]; then
        "$CAP" --only "$ONLY" --range "$RANGE" --config "$CONFIG" --out "$WORK/picks_s_ind" \
               --metrics-out "$WORK/metrics_ind" "$TANKS"
    fi
    if [ "$MODE" = "both" ] || [ "$MODE" = "hyb" ]; then
        "$CAP" --only "$ONLY" --range "$RANGE" --config "$CONFIG" --ppicks "$WORK/picks_p_manual" \
               --out "$WORK/picks_s_hyb" --metrics-out "$WORK/metrics_hyb" "$TANKS"
    fi
fi

# 5) reportes
report_one() {
    local tag="$1" dir="$2"
    [ -d "$dir" ] || { echo "[validate-manual] (sin captura $tag)"; return 0; }
    echo "================= REPORTE $tag ================="
    "$PY" "$PSM" report \
        --dat "$DAT" --tanks "$TANKS" --picks-s "$dir" --events-tt "$WORK/tt.tsv" \
        --picksta "$PARAMS_DIR/pickS.sta" --config "$CONFIG" \
        --metrics "$WORK/metrics_$tag" \
        --match-tol "$MATCH_TOL" --resid-tol "$RESID_TOL" --max-weight "$MAX_WEIGHT" \
        --only "$ONLY" --range "$RANGE" --out "$WORK/report_$tag" --format all
}

if [ "$MODE" = "both" ] || [ "$MODE" = "ind" ]; then
    report_one ind "$WORK/picks_s_ind"
fi
if [ "$MODE" = "both" ] || [ "$MODE" = "hyb" ]; then
    report_one hyb "$WORK/picks_s_hyb"
fi

# 6) calibración de umbrales intrínsecos (sobre la captura hybrid)
if [ -d "$WORK/metrics_hyb" ]; then
    echo "================= CALIBRATE hyb ================="
    "$PY" "$PSM" calibrate --dat "$DAT" --tanks "$TANKS" --metrics "$WORK/metrics_hyb" \
        --events-tt "$WORK/tt.tsv" --picksta "$PARAMS_DIR/pickS.sta" --config "$CONFIG" \
        --match-tol "$MATCH_TOL" --only "$ONLY" --range "$RANGE" \
        --out "$WORK/calibrate_hyb.json"
fi

echo "[validate-manual] listo. Reportes en $WORK/report_{ind,hyb}/"
