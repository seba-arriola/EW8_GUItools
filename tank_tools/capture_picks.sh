#!/usr/bin/env bash
#
# capture_picks.sh - Captura picks de uno o varios tanks con pick_FP OFFLINE.
#
# Usa el modo offline incorporado de pick_FP: lee el master.tank directo
# (TRACE2_HEADER + muestras) y emite las lineas TYPE_PICK_SCNL a stdout.
# NO usa tankplayer, NI anillos, NI csnloc: es CPU-bound, sin reloj de pared.
#
# Cada ejecucion crea una carpeta NUEVA e inmutable, con marca de tiempo:
#
#     picks/20260923-171530/<slug>.picks
#
# Los ficheros <slug>.picks son la entrada del modo offline de csnloc:
#     csnloc <csnloc.d> <slug>.picks
#
# Uso:
#   capture_picks.sh [opciones] <tank|dir> [contenedor]
#
#   <tank|dir>    un master.tank, o un directorio (p.ej. tank_repo/) que se
#                 recorre buscando */master.tank
#   [contenedor]  carpeta donde crear la subcarpeta con marca de tiempo
#                 (default: <repo>/picks)
#
# Opciones:
#   --out DIR      usar DIR como carpeta de captura EXACTA (reanuda: salta los
#                  .picks ya hechos, salvo --force). Anula [contenedor].
#   --name NAME    nombre de la subcarpeta (default: YYYYMMDD-HHMMSS)
#   --config FILE  config de pick_FP (default: run_working_v8/params/pick_FP.d)
#   --only GLOB    procesar solo los slugs que casen con el glob
#   --force        rehacer aunque exista el .picks (util con --out)
#   --dry-run      mostrar que haria y salir
#   -h, --help     ayuda
#
# Salida por tank:  [picks] (n/N) <slug> -> M picks
# Codigos: 0 ok | 1 error de uso | 2 algun tank fallo | 3 faltan binarios
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
PARAMS_DIR="$ROOT_DIR/run_working_v8/params"

CONTAINER="$ROOT_DIR/picks"
OUT=""
NAME=""
CONFIG="$PARAMS_DIR/pick_FP.d"
ONLY="*"
FORCE=0
DRYRUN=0

usage() { awk 'NR==1{next} /^#/{sub(/^# ?/,""); print; next} {exit}' "$0"; }

die() { echo "[picks] ERROR: $*" >&2; exit 1; }

# --- parseo ---------------------------------------------------------------
ARGS=()
while [ $# -gt 0 ]; do
    case "$1" in
        --out)     OUT="$2"; shift 2 ;;
        --name)    NAME="$2"; shift 2 ;;
        --config)  CONFIG="$2"; shift 2 ;;
        --only)    ONLY="$2"; shift 2 ;;
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

# --- carpeta de captura ---------------------------------------------------
# Por defecto: <contenedor>/<marca de tiempo>, NUEVA en cada ejecucion.
# Con --out DIR: esa carpeta exacta (reanuda saltando los .picks existentes).
if [ -n "$OUT" ]; then
    CAPDIR="$OUT"
else
    stamp="$(date +%Y%m%d-%H%M%S)"
    [ -n "$NAME" ] && stamp="$NAME"
    CAPDIR="$CONTAINER/$stamp"
fi

# Resolver a absoluto ANTES de source (ew8_unix.sh hace `cd` al repo).
case "$SRC" in /*) ;; *) SRC="$(pwd)/$SRC" ;; esac
case "$CAPDIR" in /*) ;; *) CAPDIR="$(pwd)/$CAPDIR" ;; esac
case "$CONFIG" in /*) ;; *) CONFIG="$(pwd)/$CONFIG" ;; esac

# --- entorno EarthWorm ----------------------------------------------------
if [ -z "${EW_HOME:-}" ] || [ -z "${EW_VERSION:-}" ]; then
    [ -f "$ROOT_DIR/ew8_unix.sh" ] || die "no hay EW_HOME y no encuentro ew8_unix.sh"
    # shellcheck disable=SC1091
    source "$ROOT_DIR/ew8_unix.sh" >/dev/null
fi

PICKFP="${EW_HOME}/${EW_VERSION}/bin/pick_FP"
[ -x "$PICKFP" ] || die "no existe el binario pick_FP: $PICKFP"
[ -f "$CONFIG" ] || die "no existe la config: $CONFIG"

# --- descubrir tanks ------------------------------------------------------
TANKS=()
if [ -d "$SRC" ]; then
    while IFS= read -r t; do
        TANKS+=("$t")
    done < <(find "$SRC" -maxdepth 2 -type f -name 'master.tank' | sort)
elif [ -f "$SRC" ]; then
    TANKS+=("$SRC")
else
    die "no existe <tank|dir>: $SRC"
fi

[ ${#TANKS[@]} -gt 0 ] || die "no encontre ningun master.tank en $SRC"

slug_of() {
    # tank_repo/<slug>/master.tank -> <slug> ; /x/<name>.tank -> <name>
    local t="$1" d b
    d="$(basename "$(dirname "$t")")"
    b="$(basename "$t")"
    if [ "$b" = "master.tank" ]; then echo "$d"; else echo "${b%.tank}"; fi
}

# --- filtrar por --only ---------------------------------------------------
SELECTED=()
for t in "${TANKS[@]}"; do
    s="$(slug_of "$t")"
    # shellcheck disable=SC2254
    case "$s" in $ONLY) SELECTED+=("$t") ;; esac
done

[ ${#SELECTED[@]} -gt 0 ] || die "ningun tank casa con --only '$ONLY'"

if [ "$DRYRUN" -eq 1 ]; then
    echo "[picks] dry-run: ${#SELECTED[@]} tank(s) -> $CAPDIR"
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

# pick_FP resuelve StaFile relativa -> ejecutar desde params/.
PICK_RE='^[0-9]+ +[0-9]+ +[0-9]+ +[0-9]+ +[^ ]+ +[^ ]+ +[0-9]{14}\.'

n_ok=0; n_skip=0; n_fail=0; i=0
FAILED=()
for t in "${SELECTED[@]}"; do
    i=$(( n_ok + n_skip + n_fail + 1 ))
    s="$(slug_of "$t")"
    out="$CAPDIR/$s.picks"

    if [ -f "$out" ] && [ "$FORCE" -eq 0 ]; then
        echo "[picks] ($i/${#SELECTED[@]}) SKIP  $s (ya existe)"
        n_skip=$(( n_skip + 1 ))
        continue
    fi

    echo "[picks] ($i/${#SELECTED[@]}) PICK  $s <- $t"

    tabs="$(cd "$(dirname "$t")" && pwd)/$(basename "$t")"
    raw="$(mktemp "${TMPDIR:-/tmp}/capture_picks.XXXXXX")"
    err="$(mktemp "${TMPDIR:-/tmp}/capture_picks_err.XXXXXX")"

    rc=0
    ( cd "$PARAMS_DIR" && "$PICKFP" "$CONFIG" "$tabs" ) >"$raw" 2>"$err" || rc=$?

    if [ "$rc" -ne 0 ]; then
        echo "       FALLO pick_FP rc=$rc"
        tail -n 3 "$err" | sed 's/^/       | /' || true
        rm -f "$raw" "$err"
        n_fail=$(( n_fail + 1 )); FAILED+=("$s")
        continue
    fi

    grep -E "$PICK_RE" "$raw" > "$out" || true
    npk="$(wc -l < "$out")"

    if [ "$npk" -eq 0 ]; then
        echo "       0 picks (revisa StaFile/canal); se guarda igual"
    else
        echo "       OK  $npk picks"
    fi

    rm -f "$raw" "$err"
    n_ok=$(( n_ok + 1 ))
done

echo "[picks] listo"
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
