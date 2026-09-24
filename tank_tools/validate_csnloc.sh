#!/usr/bin/env bash
#
# validate_csnloc.sh - Corre el modo OFFLINE de csnloc sobre una captura de
# picks y guarda los resultados en una carpeta de validacion NUEVA.
#
# Para cada <slug>.picks de la captura ejecuta:
#     csnloc <csnloc.d> <slug>.picks        (reloj virtual, sin anillos)
# y escribe, en una subcarpeta con marca de tiempo:
#
#     picks/<captura>/csnlocvalidate_<YYYYMMDD-HHMMSS>/
#         <slug>.jsonl     resultados (una linea JSON por solucion)
#         <slug>.log       stderr de csnloc
#         csnloc.d         copia de la config usada
#         manifest.json    metadatos de la corrida
#
# Cada validacion es una carpeta distinta: re-validar con otro csnloc NO pisa
# la anterior, y ambas se comparan con offline_report.py.
#
# Uso:
#   validate_csnloc.sh [opciones] <captura>
#
# Opciones:
#   --out DIR        usar DIR como carpeta de validacion EXACTA (reanuda)
#   --name NAME      nombre de la subcarpeta (default: csnlocvalidate_<timestamp>)
#   --config FILE    config de csnloc (default: run_working_v8/params/csnloc.d)
#   --force          rehacer los .jsonl existentes (util con --out)
#   --no-report      no ejecutar el reporte al final
#   --report-args "X" argumentos extra para offline_report.py (p.ej. "--format all")
#   -h, --help       ayuda
#
# Salida por tank:  [offline] (n/N) <slug> -> M soluciones
# Codigos: 0 ok | 1 error de uso | 2 algun tank fallo | 3 faltan binarios
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
PARAMS_DIR="$ROOT_DIR/run_working_v8/params"

CONFIG="$PARAMS_DIR/csnloc.d"
OUT=""
NAME=""
FORCE=0
NO_REPORT=0
REPORT_ARGS=""

usage() { awk 'NR==1{next} /^#/{sub(/^# ?/,""); print; next} {exit}' "$0"; }
die() { echo "[offline] ERROR: $*" >&2; exit 1; }

ARGS=()
while [ $# -gt 0 ]; do
    case "$1" in
        --out)         OUT="$2"; shift 2 ;;
        --name)        NAME="$2"; shift 2 ;;
        --config)      CONFIG="$2"; shift 2 ;;
        --force)       FORCE=1; shift ;;
        --no-report)   NO_REPORT=1; shift ;;
        --report-args) REPORT_ARGS="$2"; shift 2 ;;
        -h|--help)     usage; exit 0 ;;
        -*)            die "opcion desconocida: $1" ;;
        *)             ARGS+=("$1"); shift ;;
    esac
done

[ ${#ARGS[@]} -ge 1 ] || { usage; exit 1; }
SRC="${ARGS[0]}"

[ -d "$SRC" ] || die "no existe la carpeta de captura: $SRC"
[ -f "$CONFIG" ] || die "no existe la config: $CONFIG"

# --- carpeta de validacion (nueva en cada corrida) ------------------------
if [ -n "$OUT" ]; then
    VDIR="$OUT"
else
    stamp="$(date +%Y%m%d-%H%M%S)"
    [ -n "$NAME" ] && stamp="$NAME"
    VDIR="$SRC/csnlocvalidate_$stamp"
fi

# Resolver a absoluto ANTES de source (ew8_unix.sh hace `cd` al repo).
case "$SRC" in /*) ;; *) SRC="$(pwd)/$SRC" ;; esac
case "$VDIR" in /*) ;; *) VDIR="$(pwd)/$VDIR" ;; esac
case "$CONFIG" in /*) ;; *) CONFIG="$(pwd)/$CONFIG" ;; esac

if [ -z "${EW_HOME:-}" ] || [ -z "${EW_VERSION:-}" ]; then
    [ -f "$ROOT_DIR/ew8_unix.sh" ] || die "no hay EW_HOME y no encuentro ew8_unix.sh"
    # shellcheck disable=SC1091
    source "$ROOT_DIR/ew8_unix.sh" >/dev/null
fi

CSNLOC="${EW_HOME}/${EW_VERSION}/bin/csnloc"
[ -x "$CSNLOC" ] || die "no existe el binario csnloc: $CSNLOC"

mkdir -p "$VDIR"
SRCABS="$(cd "$SRC" && pwd)"
VDIRABS="$(cd "$VDIR" && pwd)"

PICKS=()
while IFS= read -r p; do PICKS+=("$p"); done \
    < <(find "$SRC" -maxdepth 1 -type f -name '*.picks' | sort)
[ ${#PICKS[@]} -gt 0 ] || die "no encontre ningun .picks en $SRC"

n_ok=0; n_skip=0; n_fail=0
FAILED=()
for p in "${PICKS[@]}"; do
    i=$(( n_ok + n_skip + n_fail + 1 ))
    s="$(basename "$p" .picks)"
    out="$VDIR/$s.jsonl"

    if [ -f "$out" ] && [ "$FORCE" -eq 0 ]; then
        echo "[offline] ($i/${#PICKS[@]}) SKIP  $s (ya existe)"
        n_skip=$(( n_skip + 1 ))
        continue
    fi

    pabs="$(cd "$(dirname "$p")" && pwd)/$(basename "$p")"
    log="$VDIR/$s.log"

    rc=0
    ( cd "$PARAMS_DIR" && "$CSNLOC" "$CONFIG" "$pabs" ) >"$out" 2>"$log" || rc=$?

    if [ "$rc" -ne 0 ]; then
        echo "[offline] ($i/${#PICKS[@]}) FALLO $s (rc=$rc)"
        tail -n 3 "$log" | sed 's/^/       | /' || true
        n_fail=$(( n_fail + 1 )); FAILED+=("$s")
        continue
    fi

    nh="$(wc -l < "$out")"
    echo "[offline] ($i/${#PICKS[@]}) OK  $s -> $nh hipocentros"
    n_ok=$(( n_ok + 1 ))
done

echo "[offline] listo: generados=$n_ok saltados=$n_skip fallidos=$n_fail"

if [ "$n_fail" -gt 0 ]; then
    for x in "${FAILED[@]}"; do echo "          - $x"; done
fi

# --- manifest de la corrida ----------------------------------------------
cp -f "$CONFIG" "$VDIR/csnloc.d" 2>/dev/null || true
csz="$(stat -c %s "$CSNLOC" 2>/dev/null || echo 0)"
cmt="$(stat -c %Y "$CSNLOC" 2>/dev/null || echo 0)"
csha="$(sha256sum "$CSNLOC" 2>/dev/null | cut -d' ' -f1 || true)"

python3 - "$VDIRABS" "$SRCABS" "$CSNLOC" "$csz" "$cmt" "$csha" "$CONFIG" <<'PY'
import datetime, json, os, sys

vdir, src, binpath, size, mtime, sha, cfg = sys.argv[1:8]
tanks = []
total = 0
for name in sorted(os.listdir(vdir)):
    if not name.endswith(".jsonl"):
        continue
    slug = name[: -len(".jsonl")]
    with open(os.path.join(vdir, name), "r", encoding="utf-8",
              errors="replace") as fh:
        n = sum(1 for line in fh if line.strip())
    tanks.append({"slug": slug, "solutions": n,
                  "jsonl": name, "log": slug + ".log"})
    total += n

man = {
    "kind": "csnlocvalidate",
    "created_utc": datetime.datetime.now(
        datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
    "capture_dir": src,
    "config": cfg,
    "config_copy": "csnloc.d",
    "csnloc_bin": {
        "path": binpath,
        "size": int(size or 0),
        "mtime": int(mtime or 0),
        "sha256": (sha or "").strip(),
    },
    "n_tanks": len(tanks),
    "n_solutions": total,
    "tanks": tanks,
}
with open(os.path.join(vdir, "manifest.json"), "w", encoding="utf-8") as fh:
    json.dump(man, fh, indent=2)
PY

echo "[offline] validacion: $VDIR"

if [ "$NO_REPORT" -eq 0 ]; then
    echo
    # shellcheck disable=SC2086
    python3 "$SCRIPT_DIR/offline_report.py" "$VDIR" $REPORT_ARGS
fi

[ "$n_fail" -gt 0 ] && exit 2
exit 0
