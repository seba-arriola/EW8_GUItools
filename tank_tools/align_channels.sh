#!/bin/bash
# =============================================================================
#  align_channels.sh - alinea el codigo de canal (HHZ/BHZ) de la configuracion
#  Proyecto: EW8_GUItools
#
#  POR QUE EXISTE
#  --------------
#  El sistema tiene que ser coherente en el codigo de canal en TRES sitios:
#
#    1. la fuente real de formas de onda  -> slink2ew_*.d   (aqui: 100% HHZ)
#    2. los picks                         -> pick_FP.sta    (col. 4)
#    3. la metadata de estaciones         -> estaciones_107.txt (col. 3)
#       y los tanks de formas             -> wave_serverV.d (linea Tank)
#
#  Si (3) no coincide con (1)/(2):
#    - csnmags_toy descarta el 100% de las fases EN SILENCIO (exige strcmp exacto
#      de sta+net+chan contra estaciones_107.txt; csnmagsutils.c:58-68), asi que
#      nunca calcula ML/Mwp.
#    - wave_serverV NO almacena ninguna forma (FindSCNL exige strcmp exacto de los
#      4 campos, canal incluido; wave_serverV.c:2939 + compare.c:41-47, y el
#      descarte es un `continue` sin log), asi que csnhypodbp no puede pedir
#      trazas y csnmags no puede calcular nada.
#    - csnhypodbp tampoco superpone fases (exige strcmp del canal del StaFile con
#      el del pick; csnhypodbp_ws.c:255).
#
#  NO se ve afectado: csnloc y csnstaevdisp (descartan el canal al parsear) y
#  csntvp (usa stations_to_view.sta, que ya es HHZ, y lee del ring).
#
#  USO
#  ---
#     ./tank_tools/align_channels.sh [--params-dir DIR] [--mode hhz|bhz]
#                                    [--only estaciones|pickfp|tanks]
#                                    [--apply] [--prune-orphans DIR] [-h]
#
#     Por defecto NO modifica nada (dry-run): solo informa de lo que cambiaria.
#     Con --apply hace copia de seguridad <fichero>.bak.<AAAAMMDD_HHMMSS>.
#
#  CODIGOS DE SALIDA
#     0  ok (aplicado, o dry-run con cambios pendientes)
#     1  error de uso
#     2  nada que cambiar (ya estaba alineado)
#     3  faltan ficheros
#
#  EJEMPLO (lo que hay que hacer en este repo, porque la fuente es HHZ)
#     ./tank_tools/align_channels.sh --dry-run
#     ./tank_tools/align_channels.sh --apply
#     ./tank_tools/align_channels.sh --prune-orphans run_working_v8/tanks/_orphan_bhz --apply
# =============================================================================

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

usage() {
    sed -n '2,/^# =====/p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
}

die() { echo "ERROR: $*" >&2; exit "$1"; }

MODE="hhz"
ONLY=""
APPLY=0
PRUNE_DIR=""

case "${1:-}" in -h|--help) usage; exit 0 ;; esac

while [ $# -gt 0 ]; do
    case "$1" in
        --params-dir) PARAMS_DIR="${2:-}"; shift 2 ;;
        --mode)       MODE="${2:-}";       shift 2 ;;
        --only)       ONLY="${2:-}";       shift 2 ;;
        --apply)      APPLY=1;             shift ;;
        --dry-run)    APPLY=0;             shift ;;
        --prune-orphans) PRUNE_DIR="${2:-}"; shift 2 ;;
        -h|--help)    usage; exit 0 ;;
        *) echo "ERROR: opcion desconocida: $1" >&2; usage >&2; exit 1 ;;
    esac
done

PARAMS_DIR="${PARAMS_DIR:-${EW_PARAMS:-$ROOT_DIR/run_working_v8/params}}"

case "$MODE" in
    hhz|HHZ) WANT="HHZ" ;;
    bhz|BHZ) WANT="BHZ" ;;
    *)
        WANT="$(printf '%s' "$MODE" | tr 'a-z' 'A-Z')"
        if [ -z "$WANT" ] || [ "${#WANT}" -gt 3 ]; then
            die 1 "--mode debe ser hhz, bhz o un codigo de canal de 1-3 caracteres (ej: lh)"
        fi
        ;;
esac
case "$ONLY" in ""|estaciones|pickfp|tanks) ;; *) die 1 "--only debe ser estaciones, pickfp o tanks" ;; esac

EST="$PARAMS_DIR/estaciones_107.txt"
PFP="$PARAMS_DIR/pick_FP.sta"
WSV="$PARAMS_DIR/wave_serverV.d"

[ -d "$PARAMS_DIR" ] || die 3 "no existe el directorio de params: $PARAMS_DIR"

STAMP="$(date +%Y%m%d_%H%M%S)"
TOTAL=0

# -----------------------------------------------------------------------------
#  Reescritura de una columna, preservando comentarios y lineas vacias.
#  Imprime en stdout el numero de lineas cambiadas.
# -----------------------------------------------------------------------------
rewrite_col() {  # <fichero> <columna> <valor> <min_campos>  -> deja el nº en CHANGED
    local f="$1" col="$2" val="$3" min="$4"
    local cntf="$f.aln.cnt" tmp="$f.aln.tmp"
    : > "$cntf"
    awk -v c="$col" -v v="$val" -v m="$min" -v cntf="$cntf" '
        /^[[:space:]]*#/ || NF == 0 { print; next }
        { if (NF >= m && $c != v) { $c = v; n++ }
          print }
        END { print n + 0 > cntf }' "$f" > "$tmp"
    CHANGED="$(cat "$cntf" 2>/dev/null || echo 0)"
    rm -f "$cntf"
    if [ "$CHANGED" -gt 0 ]; then
        if [ "$APPLY" -eq 1 ]; then
            cp -p "$f" "$f.bak.$STAMP"
            mv "$tmp" "$f"
            printf '  %-22s %4d linea(s) -> %s (respaldo %s.bak.%s)\n' \
                   "$(basename "$f")" "$CHANGED" "$val" "$(basename "$f")" "$STAMP"
        else
            rm -f "$tmp"
            printf '  %-22s %4d linea(s) -> %s   [dry-run]\n' "$(basename "$f")" "$CHANGED" "$val"
        fi
    else
        rm -f "$tmp"
        printf '  %-22s sin cambios\n' "$(basename "$f")"
    fi
}

# -----------------------------------------------------------------------------
#  wave_serverV.d: ademas del canal, reescribe el nombre del .tnk
# -----------------------------------------------------------------------------
rewrite_tanks() {  # <fichero> <valor>  -> deja el nº en CHANGED
    local f="$1" val="$2"
    local cntf="$f.aln.cnt" tmp="$f.aln.tmp"
    : > "$cntf"
    awk -v v="$val" -v cntf="$cntf" '
        /^[[:space:]]*#/ || NF == 0 { print; next }
        $1 == "Tank" && NF >= 11 {
            if ($3 != v) { $3 = v; n++ }
            dir = $11; sub(/\/[^\/]*$/, "", dir)
            $11 = dir "/" $2 "_" $3 "_" $4 "_" $5 ".tnk"
            print; next
        }
        { print }
        END { print n + 0 > cntf }' "$f" > "$tmp"
    CHANGED="$(cat "$cntf" 2>/dev/null || echo 0)"
    rm -f "$cntf"
    if [ "$CHANGED" -gt 0 ]; then
        if [ "$APPLY" -eq 1 ]; then
            cp -p "$f" "$f.bak.$STAMP"
            mv "$tmp" "$f"
            printf '  %-22s %4d linea(s) Tank -> canal %s y .tnk renombrado (respaldo .bak.%s)\n' \
                   "$(basename "$f")" "$CHANGED" "$val" "$STAMP"
        else
            rm -f "$tmp"
            printf '  %-22s %4d linea(s) Tank -> canal %s   [dry-run]\n' "$(basename "$f")" "$CHANGED" "$val"
        fi
    else
        rm -f "$tmp"
        printf '  %-22s sin cambios\n' "$(basename "$f")"
    fi
}

echo "[align] params : $PARAMS_DIR"
echo "[align] modo   : $MODE ($WANT)"
echo "[align] accion : $([ "$APPLY" -eq 1 ] && echo APLICAR || echo 'dry-run (usa --apply)')"
echo

if [ -z "$ONLY" ] || [ "$ONLY" = "estaciones" ]; then
    [ -f "$EST" ] || die 3 "no existe $EST"
    rewrite_col "$EST" 3 "$WANT" 8; TOTAL=$(( TOTAL + CHANGED ))
fi
if [ -z "$ONLY" ] || [ "$ONLY" = "pickfp" ]; then
    [ -f "$PFP" ] || die 3 "no existe $PFP"
    rewrite_col "$PFP" 4 "$WANT" 5; TOTAL=$(( TOTAL + CHANGED ))
fi
if [ -z "$ONLY" ] || [ "$ONLY" = "tanks" ]; then
    [ -f "$WSV" ] || die 3 "no existe $WSV"
    rewrite_tanks "$WSV" "$WANT"; TOTAL=$(( TOTAL + CHANGED ))
fi

# -----------------------------------------------------------------------------
#  Mover tanks huerfanos (los que ya no corresponden a ninguna linea Tank)
# -----------------------------------------------------------------------------
if [ -n "$PRUNE_DIR" ]; then
    [ -f "$WSV" ] || die 3 "no existe $WSV"
    TANKS_DIR="$(awk '$1 == "Tank" && NF >= 11 { print $11 }' "$WSV" | head -n1 | xargs -r dirname)"
    if [ -z "$TANKS_DIR" ] || [ ! -d "$TANKS_DIR" ]; then
        echo "[align] prune : no se pudo determinar el directorio de tanks; se omite" >&2
    else
        echo
        echo "[align] prune : directorio $TANKS_DIR"
        mkdir -p "$PRUNE_DIR"
        expected="$(awk '$1 == "Tank" && NF >= 11 { n = split($11, a, "/"); print a[n] }' "$WSV" | sort -u)"
        moved=0
        for f in "$TANKS_DIR"/*.tnk "$TANKS_DIR"/*.tnk-*.inx; do
            [ -e "$f" ] || continue
            base="$(basename "$f")"
            # un .inx pertenece a su tank base: X.tnk-1.inx -> X.tnk
            tankbase="$base"
            case "$base" in
                *.tnk-*.inx) tankbase="${base%.tnk-*}.tnk" ;;
            esac
            if printf '%s\n' "$expected" | grep -qxF "$tankbase"; then continue; fi
            if [ "$APPLY" -eq 1 ]; then
                mv -f "$f" "$PRUNE_DIR/"
            fi
            moved=$(( moved + 1 ))
        done
        printf '  %-22s %4d fichero(s) huerfano(s) %s\n' "$(basename "$TANKS_DIR")" "$moved" \
               "$([ "$APPLY" -eq 1 ] && echo "movidos a $PRUNE_DIR" || echo '[dry-run]')"
        TOTAL=$(( TOTAL + moved ))
    fi
fi

echo
if [ "$TOTAL" -eq 0 ]; then
    echo "[align] nada que cambiar: la configuracion ya esta alineada a $WANT."
    exit 2
fi
if [ "$APPLY" -eq 1 ]; then
    echo "[align] aplicado. Reinicia EarthWorm para que wave_serverV cree los .tnk nuevos."
else
    echo "[align] hay $TOTAL cambio(s) pendientes. Repite con --apply."
fi
exit 0
