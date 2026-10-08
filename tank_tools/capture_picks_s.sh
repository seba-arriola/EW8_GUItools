#!/usr/bin/env bash
#
# capture_picks_s.sh - Captura picks de ONDA S de uno o varios tanks con pickS OFFLINE.
#
# Usa el modo offline de pickS: lee el master.tank directo (TRACE2_HEADER +
# muestras) y emite las lineas TYPE_PICK_SCNL (fase S) a stdout.
# NO usa tankplayer, NI anillos, NI csnloc: es CPU-bound, sin reloj de pared.
#
# Cada ejecucion crea una carpeta NUEVA e inmutable, con marca de tiempo:
#
#     picks_s/20260923-171530/<slug>.picks
#
# Uso:
#   capture_picks_s.sh [opciones] <tank|dir> [contenedor]
#
# Opciones:
#   --out DIR       carpeta de captura EXACTA (reanuda salvo --force)
#   --name NAME     nombre de la subcarpeta (default: YYYYMMDD-HHMMSS)
#   --config FILE   config de pickS (default: run_working_v8/params/pickS.d)
#   --ppicks DIR    directorio con picks P (de capture_picks.sh) para guiar el S
#                   (busca <slug>.picks); activa el modo guiado si la config es hybrid
#   --metrics-out DIR  guarda <slug>.metrics con las metricas por deteccion
#                   (fuerza MetricsLog 1 en una copia temporal de la config)
#   --only GLOB     procesar solo los slugs que casen con el glob
#   --force         rehacer aunque exista el .picks
#   --dry-run       mostrar que haria y salir
#   -h, --help      ayuda
#
# Salida por tank:  [picksS] (n/N) <slug> -> M picks
# Codigos: 0 ok | 1 error de uso | 2 algun tank fallo | 3 faltan binarios
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
PARAMS_DIR="$ROOT_DIR/run_working_v8/params"

CONTAINER="$ROOT_DIR/picks_s"
OUT=""
NAME=""
CONFIG="$PARAMS_DIR/pickS.d"
PPICKS=""
METRICS_OUT=""
ONLY="*"
RANGE=""
FORCE=0
DRYRUN=0

usage() { awk 'NR==1{next} /^#/{sub(/^# ?/,""); print; next} {exit}' "$0"; }

die() { echo "[picksS] ERROR: $*" >&2; exit 1; }

ARGS=()
while [ $# -gt 0 ]; do
    case "$1" in
        --out)     OUT="$2"; shift 2 ;;
        --name)    NAME="$2"; shift 2 ;;
        --config)  CONFIG="$2"; shift 2 ;;
        --ppicks)  PPICKS="$2"; shift 2 ;;
        --metrics-out) METRICS_OUT="$2"; shift 2 ;;
        --only)    ONLY="$2"; shift 2 ;;
        --range)   RANGE="$2"; shift 2 ;;
        --force)   FORCE=1; shift ;;
        --dry-run) DRYRUN=1; shift ;;
        -h|--help) usage; exit 0 ;;
        -*)        die "opcion desconocida: $1" ;;
        *)         ARGS+=("$1"); shift ;;
    esac
done

[ ${#ARGS[@]} -ge 1 ] || { usage; exit 1; }
SRC="${ARGS[0]}"
[ ${#ARGS[@]} -ge 2 ] && CONTAINER="${ARGS[1]}"

if [ -n "$OUT" ]; then
    CAPDIR="$OUT"
else
    stamp="$(date +%Y%m%d-%H%M%S)"
    [ -n "$NAME" ] && stamp="$NAME"
    CAPDIR="$CONTAINER/$stamp"
fi

case "$SRC" in /*) ;; *) SRC="$(pwd)/$SRC" ;; esac
case "$CAPDIR" in /*) ;; *) CAPDIR="$(pwd)/$CAPDIR" ;; esac
case "$CONFIG" in /*) ;; *) CONFIG="$(pwd)/$CONFIG" ;; esac
[ -n "$PPICKS" ] && case "$PPICKS" in /*) ;; *) PPICKS="$(pwd)/$PPICKS" ;; esac
[ -n "$METRICS_OUT" ] && case "$METRICS_OUT" in /*) ;; *) METRICS_OUT="$(pwd)/$METRICS_OUT" ;; esac

if [ -z "${EW_HOME:-}" ] || [ -z "${EW_VERSION:-}" ]; then
    [ -f "$ROOT_DIR/ew8_unix.sh" ] || die "no hay EW_HOME y no encuentro ew8_unix.sh"
    # shellcheck disable=SC1091
    source "$ROOT_DIR/ew8_unix.sh" >/dev/null
fi

PICKS="${EW_HOME}/${EW_VERSION}/bin/pickS"
[ -x "$PICKS" ] || die "no existe el binario pickS: $PICKS"
[ -f "$CONFIG" ] || die "no existe la config: $CONFIG"

TANKS=()
if [ -d "$SRC" ]; then
    while IFS= read -r t; do TANKS+=("$t"); done \
        < <(find "$SRC" -maxdepth 2 -type f -name 'master.tank' | sort)
elif [ -f "$SRC" ]; then
    TANKS+=("$SRC")
else
    die "no existe <tank|dir>: $SRC"
fi
[ ${#TANKS[@]} -gt 0 ] || die "no encontre ningun master.tank en $SRC"

slug_of() {
    local t="$1" d b
    d="$(basename "$(dirname "$t")")"; b="$(basename "$t")"
    if [ "$b" = "master.tank" ]; then echo "$d"; else echo "${b%.tank}"; fi
}

SELECTED=()
for t in "${TANKS[@]}"; do
    s="$(slug_of "$t")"
    sel=0
    if [ -n "$RANGE" ]; then
        rlo="${RANGE%-*}"; rhi="${RANGE#*-}"
        num="${s#test}"
        if [ "$s" != "$num" ] && [ "$num" -ge "$rlo" ] 2>/dev/null && [ "$num" -le "$rhi" ] 2>/dev/null; then
            sel=1
        fi
    else
        # shellcheck disable=SC2254
        case "$s" in $ONLY) sel=1 ;; esac
    fi
    [ "$sel" -eq 1 ] && SELECTED+=("$t")
done
[ ${#SELECTED[@]} -gt 0 ] || die "ningun tank seleccionado (--only '$ONLY' --range '$RANGE')"

if [ "$DRYRUN" -eq 1 ]; then
    echo "[picksS] dry-run: ${#SELECTED[@]} tank(s) -> $CAPDIR"
    for t in "${SELECTED[@]}"; do
        s="$(slug_of "$t")"
        if [ -f "$CAPDIR/$s.picks" ] && [ "$FORCE" -eq 0 ]; then
            echo "  SKIP  $s (ya existe $CAPDIR/$s.picks)"
        else
            echo "  DRY   $s <- $t"
        fi
    done
    exit 0
fi

mkdir -p "$CAPDIR"
[ -n "$METRICS_OUT" ] && mkdir -p "$METRICS_OUT"

PICK_RE='^[0-9]+ +[0-9]+ +[0-9]+ +[0-9]+ +[^ ]+ +[^ ]+ +[0-9]{14}\.'

n_ok=0; n_skip=0; n_fail=0
FAILED=()
for t in "${SELECTED[@]}"; do
    i=$(( n_ok + n_skip + n_fail + 1 ))
    s="$(slug_of "$t")"
    out="$CAPDIR/$s.picks"

    if [ -f "$out" ] && [ "$FORCE" -eq 0 ]; then
        echo "[picksS] ($i/${#SELECTED[@]}) SKIP  $s (ya existe)"
        n_skip=$(( n_skip + 1 )); continue
    fi

    echo "[picksS] ($i/${#SELECTED[@]}) PICK  $s <- $t"

    tabs="$(cd "$(dirname "$t")" && pwd)/$(basename "$t")"
    raw="$(mktemp "${TMPDIR:-/tmp}/capture_picks_s.XXXXXX")"
    err="$(mktemp "${TMPDIR:-/tmp}/capture_picks_s_err.XXXXXX")"

    pp=""
    if [ -n "$PPICKS" ] && [ -f "$PPICKS/$s.picks" ]; then
        pp="$PPICKS/$s.picks"
    fi

    # Config efectiva: si se piden metricas, copia temporal con MetricsLog 1 al
    # final (kom toma la ultima ocurrencia). No altera el picking ni el pick.
    cfg="$CONFIG"
    tmpcfg=""
    if [ -n "$METRICS_OUT" ]; then
        tmpcfg="$(mktemp "${TMPDIR:-/tmp}/pickS_metrics.XXXXXX")"
        cat "$CONFIG" > "$tmpcfg"
        printf '\nMetricsLog 1\n' >> "$tmpcfg"
        cfg="$tmpcfg"
    fi

    rc=0
    if [ -n "$pp" ]; then
        ( cd "$PARAMS_DIR" && "$PICKS" "$cfg" "$tabs" "$pp" ) >"$raw" 2>"$err" || rc=$?
    else
        ( cd "$PARAMS_DIR" && "$PICKS" "$cfg" "$tabs" ) >"$raw" 2>"$err" || rc=$?
    fi

    if [ "$rc" -ne 0 ]; then
        echo "        FALLO pickS rc=$rc"
        tail -n 3 "$err" | sed 's/^/        | /' || true
        rm -f "$raw" "$err"
        if [ -n "$tmpcfg" ]; then rm -f "$tmpcfg"; fi
        n_fail=$(( n_fail + 1 )); FAILED+=("$s"); continue
    fi

    grep -E "$PICK_RE" "$raw" > "$out" || true
    npk="$(wc -l < "$out")"

    if [ -n "$METRICS_OUT" ]; then
        grep -E 'pickS: METRICS' "$raw" > "$METRICS_OUT/$s.metrics" || true
    fi

    if [ "$npk" -eq 0 ]; then
        echo "        0 picks (revisa pickS.sta/canales); se guarda igual"
    else
        echo "        OK  $npk picks"
    fi

    rm -f "$raw" "$err"
    if [ -n "$tmpcfg" ]; then rm -f "$tmpcfg"; fi
    n_ok=$(( n_ok + 1 ))
done

echo "[picksS] listo"
echo "        captura   : $CAPDIR"
echo "        generados : $n_ok"
echo "        saltados  : $n_skip"
echo "        fallidos  : $n_fail"
if [ "$n_fail" -gt 0 ]; then
    echo "        fallos:"
    for x in "${FAILED[@]}"; do echo "          - $x"; done
    exit 2
fi
exit 0
