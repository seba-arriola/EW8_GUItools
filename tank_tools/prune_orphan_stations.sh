#!/bin/bash
# =============================================================================
#  prune_orphan_stations.sh - elimina SCNL sin metadata en estaciones_107.txt
#  Proyecto: EW8_GUItools
#
#  POR QUE EXISTE
#  --------------
#  csnloc localiza y csnmags_toy calcula ML/Mwp SOLO con las estaciones que
#  tienen metadata en estaciones_107.txt (StaFile). Un SCNL presente en
#  pick_FP.sta o en las lineas Tank de wave_serverV.d pero AUSENTE en
#  estaciones_107.txt queda inerte: no se localiza ni se mide, y rompe la
#  coherencia de la configuracion.
#
#  El conjunto "huerfano" se calcula normalizando STA CHAN NET LOC (con -- y
#  vacio equivalentes) y comparandolo contra estaciones_107.txt.
#
#  USO
#  ---
#     ./tank_tools/prune_orphan_stations.sh [--params-dir DIR]
#                                           [--only pick,tanks,streams,view|all]
#                                           [--apply] [--dry-run] [-h]
#
#     Por defecto es dry-run y actua sobre "pick,tanks".
#     --only admite varios separados por comas.
#     Con --apply hace respaldo <fichero>.bak.<AAAAMMDD_HHMMSS>.
#
#  CODIGOS DE SALIDA
#     0  ok (aplicado, o dry-run con cambios pendientes)
#     1  error de uso
#     2  nada que eliminar (configuracion ya coherente)
#     3  faltan ficheros
# =============================================================================

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

usage() {
    sed -n '2,/^# =====/p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
}

die() { echo "ERROR: $*" >&2; exit "$1"; }

ONLY="pick,tanks"
APPLY=0
PARAMS_DIR=""

case "${1:-}" in -h|--help) usage; exit 0 ;; esac

while [ $# -gt 0 ]; do
    case "$1" in
        --params-dir) PARAMS_DIR="${2:-}"; shift 2 ;;
        --only)       ONLY="${2:-}";       shift 2 ;;
        --apply)      APPLY=1;             shift ;;
        --dry-run)    APPLY=0;             shift ;;
        -h|--help)    usage; exit 0 ;;
        *) echo "ERROR: opcion desconocida: $1" >&2; usage >&2; exit 1 ;;
    esac
done

PARAMS_DIR="${PARAMS_DIR:-${EW_PARAMS:-$ROOT_DIR/run_working_v8/params}}"
[ -d "$PARAMS_DIR" ] || die 3 "no existe el directorio de params: $PARAMS_DIR"

EST="$PARAMS_DIR/estaciones_107.txt"
[ -f "$EST" ] || die 3 "no existe $EST"

# Normaliza --only a una lista con espacios
wanted=""
old_ifs="$IFS"; IFS=', '
for w in $ONLY; do
    case "$w" in
        all)     wanted="$wanted pick tanks streams view" ;;
        pick)    wanted="$wanted pick" ;;
        tanks)   wanted="$wanted tanks" ;;
        streams) wanted="$wanted streams" ;;
        view)    wanted="$wanted view" ;;
        "")      ;;
        *) IFS="$old_ifs"; die 1 "--only debe ser pick, tanks, streams, view o all" ;;
    esac
done
IFS="$old_ifs"
wanted="${wanted# }"
[ -n "$wanted" ] || die 1 "--only vacio"

STAMP="$(date +%Y%m%d_%H%M%S)"
TOTAL=0

# -----------------------------------------------------------------------------
#  Elimina las lineas huerfanas de un fichero. Deja el nº en CHANGED.
# -----------------------------------------------------------------------------
prune_file() {  # <modo> <fichero>
    local mode="$1" f="$2"
    local tmp="$f.prn.tmp" cntf="$f.prn.cnt" remf="$f.prn.rem"
    : > "$cntf"; : > "$remf"
    awk -v mode="$mode" -v cntf="$cntf" -v remf="$remf" '
        FNR == NR {
            # estaciones_107.txt: STA NET CHAN LOC ...
            if (NF >= 4) {
                loc = ($4 == "" ? "--" : $4)
                known[$1 "|" $3 "|" $2 "|" loc] = 1
            }
            next
        }
        function key(sta, chan, net, loc) {
            if (loc == "") loc = "--"
            return sta "|" chan "|" net "|" loc
        }
        /^[[:space:]]*#/ || NF == 0 { print; next }
        {
            k = ""
            if (mode == "pick") {
                if (NF < 6) { print; next }
                k = key($3, $4, $5, $6)
            } else if (mode == "tanks") {
                if ($1 != "Tank" || NF < 11) { print; next }
                k = key($2, $3, $4, $5)
            } else if (mode == "view") {
                if (NF < 4) { print; next }
                k = key($1, $2, $3, $4)
            } else if (mode == "streams") {
                if ($1 != "Stream" || NF < 3) { print; next }
                nss = $2; sel = $3; gsub(/"/, "", sel)
                p = index(nss, "_")
                net = substr(nss, 1, p - 1); sta = substr(nss, p + 1)
                if (sel ~ /^[0-9][0-9]/) { loc = substr(sel, 1, 2); chan = substr(sel, 3, 3) }
                else { loc = "--"; chan = substr(sel, 1, 3) }
                k = key(sta, chan, net, loc)
            }
            if (k in known) { print; next }
            n++; print k > remf
        }
        END { print n + 0 > cntf }
    ' "$EST" "$f" > "$tmp"
    CHANGED="$(cat "$cntf" 2>/dev/null || echo 0)"
    rm -f "$cntf"

    if [ "${CHANGED:-0}" -gt 0 ]; then
        printf '  %-24s %4d huerfano(s)\n' "$(basename "$f")" "$CHANGED"
        if [ -s "$remf" ]; then
            sort -u "$remf" | sed 's/^/       - /'
        fi
        if [ "$APPLY" -eq 1 ]; then
            cp -p "$f" "$f.bak.$STAMP"
            mv "$tmp" "$f"
            printf '       -> aplicado (respaldo %s.bak.%s)\n' "$(basename "$f")" "$STAMP"
        else
            rm -f "$tmp"
            printf '       -> [dry-run]\n'
        fi
    else
        rm -f "$tmp"
        printf '  %-24s sin huerfanos\n' "$(basename "$f")"
    fi
    rm -f "$remf"
}

echo "[prune] params    : $PARAMS_DIR"
echo "[prune] metadata  : $EST"
echo "[prune] objetivo  : $wanted"
echo "[prune] accion    : $([ "$APPLY" -eq 1 ] && echo APLICAR || echo 'dry-run (usa --apply)')"
echo

case " $wanted " in *" pick "*)
    [ -f "$PARAMS_DIR/pick_FP.sta" ] || die 3 "no existe pick_FP.sta"
    prune_file pick "$PARAMS_DIR/pick_FP.sta"; TOTAL=$(( TOTAL + CHANGED )) ;;
esac
case " $wanted " in *" tanks "*)
    [ -f "$PARAMS_DIR/wave_serverV.d" ] || die 3 "no existe wave_serverV.d"
    prune_file tanks "$PARAMS_DIR/wave_serverV.d"; TOTAL=$(( TOTAL + CHANGED )) ;;
esac
case " $wanted " in *" streams "*)
    [ -f "$PARAMS_DIR/slink2ew_HHZ.d" ] || die 3 "no existe slink2ew_HHZ.d"
    prune_file streams "$PARAMS_DIR/slink2ew_HHZ.d"; TOTAL=$(( TOTAL + CHANGED )) ;;
esac
case " $wanted " in *" view "*)
    [ -f "$PARAMS_DIR/stations_to_view.sta" ] || die 3 "no existe stations_to_view.sta"
    prune_file view "$PARAMS_DIR/stations_to_view.sta"; TOTAL=$(( TOTAL + CHANGED )) ;;
esac

echo
if [ "$TOTAL" -eq 0 ]; then
    echo "[prune] nada que eliminar: la configuracion ya es coherente con estaciones_107.txt."
    exit 2
fi
if [ "$APPLY" -eq 1 ]; then
    echo "[prune] aplicado. Reinicia EarthWorm; despues mueve los .tnk huerfanos con:"
    echo "        ./tank_tools/align_channels.sh --only tanks --prune-orphans DIR --apply"
else
    echo "[prune] hay $TOTAL linea(s) huerfana(s) pendientes. Repite con --apply."
fi
exit 0
