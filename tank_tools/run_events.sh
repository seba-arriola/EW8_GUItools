#!/bin/bash
# =============================================================================
#  run_events.sh - runner de eventos reales con tankplayer
#  Proyecto: EW8_GUItools
#
#  USO
#  ---
#     ./tank_tools/run_events.sh plan <tank|dir> [opciones]   # solo planifica
#     ./tank_tools/run_events.sh run  <tank|dir> [opciones]   # ejecuta
#     ./tank_tools/run_events.sh report [opciones del reporte]
#
#  <tank|dir> admite:
#     - un fichero .tank
#     - un directorio de tanks  (p.ej. tank_repo/, con <slug>/manifest.json)
#
#  COMO REPRODUCE (y por que asi)
#  ------------------------------
#  El objetivo es pasar los eventos RAPIDO sin distorsionar la cadena. Hay tres
#  invariantes que obligan a trocear y a esperar un "asentamiento":
#
#   1. wave_serverV rechaza un paquete si starttime > now + 900 s
#      (wave_serverV.c:3024). En --mode fast el tiempo de dato avanza mas rapido
#      que el reloj, asi que el span de la sesion debe cumplir
#      chunk_span + pared <= 900 + SendLate.
#
#   2. csnloc solo intenta asociar cada AssocWindowSec/2 + 1 segundos de reloj de
#      pared (csnloc.c:340). Con el valor de produccion (120 s) son 61 s: una
#      corrida mas corta produce CERO localizaciones.
#
#   3. El ultimo pick de un evento llega al final del replay; hay que esperar a
#      la SIGUIENTE pasada de asociacion. De ahi el "asentamiento" (settle):
#      AssocWindowSec/2 + 1 + 4 s.
#
#  Por eso cada trozo es una SESION INDEPENDIENTE con stack nuevo (no se usa
#  --sequence: con 0 el trozo re-anclado va hacia atras y wave_serverV lo rechaza
#  por no avanzar; con 1 el futuro se acumula y se supera el limite de 900 s).
#
#  OPCIONES
#  --------
#     --outdir DIR        raiz de resultados                 (def: runs)
#     --workdir DIR       trozos .tank                       (def: <outdir>/_work)
#     --mode fast|realtime  fast comprime el ruido (def) / realtime fiel
#     --chunk-span S      span de dato por sesion            (def: 700)
#     --overlap S         solape entre trozos                (def: 180)
#     --min-wall S        duracion de pared objetivo (fast)  (def: 70)
#     --settle S|auto     asentamiento tras el replay        (def: auto)
#     --assoc-window S    AssocWindowSec de la copia aislada (def: 120)
#     --params-mode M     alineacion de la copia de params   (def: hhz)
#     --chan CC           filtro de canal para tankcut       (def: HH)
#     --sendlate S        SendLate                           (def: 30)
#     --startup S         StartUpDelay de tankplayer         (def: 6)
#     --pause S           Pause entre ficheros               (def: 0)
#     --timeout S         tope por sesion                    (def: 900)
#     --chain             directorio: todas seguidas
#     --interactive       directorio: parar tras cada evento (def)
#     --yes               atajo no interactivo (= --chain)
#     --keep-tanks        no borrar los .tnk de trabajo al acabar cada tank
#     --no-debug          no activar Debug/DumpHypo en la copia aislada
#     --force             repetir sesiones ya hechas
#     --no-cut            plan: no cortar trozos (estimacion gruesa)
#     --dry-run           imprime lo que haria y sale
#     -h|--help
#
#  SALIDA
#  ------
#     <outdir>/<slug>/<NNN>/session.json   manifiesto de la sesion
#     <outdir>/<slug>/<NNN>/logs/          EW_LOG aislado (csnloc, csnmags_toy,
#                                          wave_serverV, tankplayer, startstop)
#     <outdir>/<slug>/<NNN>/play.log       salida completa del replay
#     <outdir>/<slug>/params/              copia aislada de run_working_v8/params
#
#  El reporte de resultados lo hace `api_report_locs`:
#     python3 -m api_report_locs.report --runs <outdir>
#
#  ATENCION: el modo replay DETIENE el EarthWorm que este en marcha (startstop) y
#  arranca con startstop_replay.d (sin slink2ew). No usar en operacion.
#
#  CODIGOS DE SALIDA
#     0  ok
#     1  error de uso
#     2  al menos una sesion fallo
#     3  faltan binarios / ficheros
# =============================================================================

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=tank_lib.sh
source "$SCRIPT_DIR/tank_lib.sh"

usage() { sed -n '2,/^# =====/p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; }

die() { echo "ERROR: $*" >&2; exit 1; }

# -----------------------------------------------------------------------------
#  Parametros por defecto
# -----------------------------------------------------------------------------
OUTDIR="runs"
WORKDIR=""
MODE="fast"
CHUNK_SPAN="700"
OVERLAP="180"
MIN_WALL="70"
SETTLE="auto"
ASSOC_WINDOW="120"
PARAMS_MODE="hhz"
CHAN="HH"
SENDLATE="30"
STARTUP="6"
PAUSE="0"
TIMEOUT="900"
DIRMODE="interactive"
KEEP_TANKS=0
DEBUG=1
FORCE=0
NOCUT=0
DRYRUN=0

CMD="${1:-}"
case "$CMD" in -h|--help|"") usage; exit 0 ;; esac
shift

# --- subcomando report: delega en api_report_locs pasando el resto tal cual ---
if [ "$CMD" = "report" ]; then
    RUNS_ARG=""
    PASSTHRU=()
    while [ $# -gt 0 ]; do
        case "$1" in
            --outdir|--runs) RUNS_ARG="${2:-}"; shift 2 ;;
            -h|--help)       usage; exit 0 ;;
            *)               PASSTHRU+=("$1"); shift ;;
        esac
    done
    command -v python3 >/dev/null 2>&1 || die "python3 no esta en PATH"
    cd "$ROOT_DIR" || die "no se pudo entrar en $ROOT_DIR"
    exec python3 -m api_report_locs.report --runs "${RUNS_ARG:-runs}" \
         ${PASSTHRU[@]+"${PASSTHRU[@]}"}
fi

TARGET="${1:-}"
[ -n "$TARGET" ] || { usage >&2; exit 1; }
shift

while [ $# -gt 0 ]; do
    case "$1" in
        --outdir)       OUTDIR="${2:-}";       shift 2 ;;
        --workdir)      WORKDIR="${2:-}";      shift 2 ;;
        --mode)         MODE="${2:-}";         shift 2 ;;
        --chunk-span)   CHUNK_SPAN="${2:-}";   shift 2 ;;
        --overlap)      OVERLAP="${2:-}";      shift 2 ;;
        --min-wall)     MIN_WALL="${2:-}";     shift 2 ;;
        --settle)       SETTLE="${2:-}";       shift 2 ;;
        --assoc-window) ASSOC_WINDOW="${2:-}"; shift 2 ;;
        --params-mode)  PARAMS_MODE="${2:-}";  shift 2 ;;
        --chan)         CHAN="${2:-}";         shift 2 ;;
        --sendlate)     SENDLATE="${2:-}";     shift 2 ;;
        --startup)      STARTUP="${2:-}";      shift 2 ;;
        --pause)        PAUSE="${2:-}";        shift 2 ;;
        --timeout)      TIMEOUT="${2:-}";      shift 2 ;;
        --chain)        DIRMODE="chain";       shift ;;
        --interactive)  DIRMODE="interactive"; shift ;;
        --yes)          DIRMODE="chain";       shift ;;
        --keep-tanks)   KEEP_TANKS=1;          shift ;;
        --no-debug)     DEBUG=0;               shift ;;
        --force)        FORCE=1;               shift ;;
        --no-cut)       NOCUT=1;               shift ;;
        --dry-run)      DRYRUN=1;              shift ;;
        -h|--help)      usage; exit 0 ;;
        *) echo "ERROR: opcion desconocida: $1" >&2; exit 1 ;;
    esac
done

[ -n "$WORKDIR" ] || WORKDIR="$OUTDIR/_work"

case "$MODE" in fast|realtime) ;; *) die "--mode debe ser fast o realtime" ;; esac
for v in CHUNK_SPAN OVERLAP MIN_WALL ASSOC_WINDOW SENDLATE STARTUP PAUSE TIMEOUT; do
    eval "val=\$$v"
    case "$val" in ''|*[!0-9]*) die "--$(printf '%s' "$v" | tr '_' '-' | tr 'A-Z' 'a-z') debe ser un entero" ;; esac
done
[ "$CHUNK_SPAN" -ge 30 ] || die "--chunk-span minimo 30 s"
[ "$OVERLAP" -lt "$CHUNK_SPAN" ] || die "--overlap debe ser menor que --chunk-span"
[ "$SENDLATE" -lt 900 ] || die "--sendlate debe ser < 900"

# Asentamiento: AssocWindowSec/2 + 1 (cadencia de asociacion) + 4 s de margen.
if [ "$SETTLE" = "auto" ]; then
    SETTLE=$(( ASSOC_WINDOW / 2 + 1 + 4 ))
else
    case "$SETTLE" in ''|*[!0-9]*) die "--settle debe ser 'auto' o un entero" ;; esac
fi
CADENCE=$(( ASSOC_WINDOW / 2 + 1 ))

have_bin tanksniff || die "no existe $(ew_bin)/tanksniff"
have_bin tankcut   || die "no existe $(ew_bin)/tankcut"
[ -x "$TANK_REPLAY" ] || die "no existe $TANK_REPLAY"

# Un trozo de 700 s de canal HH a 100 sps ocupa ~280 KB por canal; 2 MB de tank
# dan margen de sobra (y se borran al acabar cada tank salvo --keep-tanks).
export PARAMS_TANK_MB="${PARAMS_TANK_MB:-2}"

mkdir -p "$OUTDIR" "$WORKDIR"

[ "$CMD" = "plan" ] || [ "$CMD" = "run" ] || { usage >&2; exit 1; }

# -----------------------------------------------------------------------------
#  Resolucion de objetivos
# -----------------------------------------------------------------------------
# Imprime una linea por tank: <slug>|<tank>|<manifest|->>
target_list() {
    local t="$1"
    if [ -d "$t" ]; then
        local found=0 d mf
        for mf in "$t"/*/manifest.json; do
            [ -f "$mf" ] || continue
            d="$(dirname "$mf")"
            if [ -f "$d/master.tank" ]; then
                printf '%s|%s|%s\n' "$(basename "$d")" "$d/master.tank" "$mf"
                found=1
            fi
        done
        if [ "$found" -eq 0 ]; then
            local f
            for f in "$t"/*.tank; do
                [ -f "$f" ] || continue
                printf '%s|%s|-\n' "$(basename "$f" .tank)" "$f"
                found=1
            done
        fi
        [ "$found" -eq 1 ] || die "no encontre tanks en $t (ni */manifest.json ni *.tank)"
    elif [ -f "$t" ]; then
        local mf="$(dirname "$t")/manifest.json"
        if [ -f "$mf" ] && [ "$(basename "$t")" = "master.tank" ]; then
            printf '%s|%s|%s\n' "$(basename "$(dirname "$t")")" "$t" "$mf"
        else
            printf '%s|%s|-\n' "$(basename "$t" .tank)" "$t"
        fi
    else
        die "no existe: $t"
    fi
}

# Rellena START_EPOCH/END_EPOCH. Prefiere el manifest (evita recorrer 600 MB con
# tanksniff); si no hay, o si el manifest no tiene la ventana del canal pedido,
# la calcula con tanksniff.
tank_bounds() {  # <tank> <manifest|-> <canal>
    local tank="$1" mf="$2" chan="$3" a b
    if [ -n "$mf" ] && [ "$mf" != "-" ]; then
        if [ "$chan" = "HH" ]; then
            a="$(manifest_field "$mf" hh_start_epoch)"; b="$(manifest_field "$mf" hh_end_epoch)"
        fi
        if [ -z "${a:-}" ] || [ -z "${b:-}" ]; then
            a="$(manifest_field "$mf" start_epoch)"; b="$(manifest_field "$mf" end_epoch)"
        fi
        case "$a" in ''|*[!0-9]*) a="" ;; esac
        case "$b" in ''|*[!0-9]*) b="" ;; esac
        if [ -n "$a" ] && [ -n "$b" ]; then
            START_EPOCH="$a"; END_EPOCH="$b"; return 0
        fi
    fi
    local vals
    vals="$(tank_span_chan "$tank" "$chan")" || vals="$(tank_span "$tank")" || return 1
    START_EPOCH="$(printf '%s' "$vals" | awk '{ print $1 }')"
    END_EPOCH="$(printf '%s' "$vals" | awk '{ print $2 }')"
}

# -----------------------------------------------------------------------------
#  Troceo: deja los chunk_*.tank en el workdir y describe cada sesion
#  Imprime: <idx>|<chunk>|<start_epoch>|<nmsgs>|<span>|<max_start>|<delay_ms>|<est_wall>
# -----------------------------------------------------------------------------
plan_tank() {  # <slug> <tank> <start> <end>
    local slug="$1" tank="$2" start="$3" end="$4"
    local span=$(( end - start ))
    local step=$(( CHUNK_SPAN - OVERLAP ))
    local n=1
    if [ "$span" -gt "$CHUNK_SPAN" ]; then
        n=$(( (span - CHUNK_SPAN + step - 1) / step + 1 ))
    fi
    [ "$n" -ge 1 ] || n=1

    local cdir="$WORKDIR/$slug"
    mkdir -p "$cdir"

    local i t0 ts chunk nmsgs cspan cmax delay est_wall
    for i in $(seq 0 $(( n - 1 ))); do
        t0=$(( start + i * step ))
        ts="$(epoch_to_stamp "$t0")"
        chunk="$(printf '%s/chunk_%03d.tank' "$cdir" "$i")"

        if [ "$NOCUT" -eq 0 ]; then
            rm -f "$chunk"
            "$(ew_bin)/tankcut" -s "$ts" -d "$CHUNK_SPAN" -C "$CHAN" "$tank" "$chunk" >/dev/null 2>&1
            if [ ! -s "$chunk" ]; then
                echo "    AVISO: trozo $i sin datos en la ventana $ts (se omite)" >&2
                continue
            fi
        elif [ ! -s "$chunk" ]; then
            echo "    AVISO: falta el trozo $chunk y se paso --no-cut (se omite)" >&2
            continue
        fi

        nmsgs="$(tank_msgcount "$chunk")"
        cspan="$(tank_span "$chunk" | awk '{ print $3 }')"
        cmax="$(tank_max_start "$chunk" 2>/dev/null || echo "$t0")"

        if [ "$MODE" = "fast" ]; then
            delay=$(( (MIN_WALL * 1000 + nmsgs - 1) / nmsgs ))
            [ "$delay" -ge 1 ] || delay=1
            est_wall=$(( nmsgs * delay / 1000 ))
        else
            delay=0
            est_wall="$cspan"
        fi
        printf '%s|%s|%s|%s|%s|%s|%s|%s\n' \
               "$i" "$chunk" "$t0" "$nmsgs" "$cspan" "$cmax" "$delay" "$est_wall"
    done
}

# Comprueba los invariantes de una sesion. Imprime "ok" o el motivo.
check_session() {  # <cspan> <est_wall> <mode>
    local cspan="$1" wall="$2" mode="$3"
    if [ "$mode" = "fast" ]; then
        # wave_serverV rechaza si starttime > now + 900 (wave_serverV.c:3024).
        # El ultimo paquete arranca a ~cspan del ancla, y la pared ya avanzo wall.
        if [ $(( cspan + wall )) -gt $(( 900 + SENDLATE )) ]; then
            printf 'NO: span(%ss)+pared(%ss) > 900+%s\n' "$cspan" "$wall" "$SENDLATE"
            return 1
        fi
    fi
    # csnloc necesita una pasada de asociacion DESPUES del ultimo pick
    if [ $(( wall + SETTLE )) -lt "$CADENCE" ]; then
        printf 'NO: pared(%ss)+settle(%ss) < cadencia(%ss)\n' "$wall" "$SETTLE" "$CADENCE"
        return 1
    fi
    printf 'ok\n'
}

# -----------------------------------------------------------------------------
#  Sesion: stack nuevo + replay + asentamiento + parada
# -----------------------------------------------------------------------------
run_session() {  # <slug> <idx> <chunk> <start> <nmsgs> <cspan> <delay> <params> <tanks>
    local slug="$1" idx="$2" chunk="$3" start="$4" nmsgs="$5" cspan="$6" delay="$7"
    local params="$8" tanks="$9"
    local sdir="$OUTDIR/$slug/$(printf '%03d' "$idx")"
    local ldir="$sdir/logs"
    local manifest="$sdir/session.json"

    if [ -f "$manifest" ] && [ "$FORCE" -eq 0 ]; then
        echo "      sesion $idx: SKIP (ya hecha)"
        return 0
    fi

    mkdir -p "$sdir" "$ldir"
    # Tanks frescos por sesion: si se reutilizan los .tnk de la sesion anterior,
    # wave_serverV los abre con el indice viejo y rechaza los paquetes por
    # "no avanzar el tiempo del tank" (wave_serverV.c:3018).
    rm -f "$tanks"/* 2>/dev/null || true

    local t0 t1 t2 rc=0 timeout_flag=false
    t0="$(date +%s)"

    if [ "$DRYRUN" -eq 1 ]; then
        echo "      sesion $idx: DRY  $(basename "$chunk") nmsgs=$nmsgs delay=${delay}ms"
        return 0
    fi

    /usr/bin/timeout "$TIMEOUT" env \
        TANK_REPLAY_PARAMS="$params" TANK_REPLAY_LOG="$ldir" \
        EW_PARAMS="$params" EW_LOG="$ldir" \
        "$TANK_REPLAY" play "$chunk" \
            --mode "$MODE" --delay-ms "$delay" --sendlate "$SENDLATE" \
            --startup "$STARTUP" --pause "$PAUSE" \
        > "$sdir/play.log" 2>&1 || rc=$?
    t1="$(date +%s)"
    [ "$rc" -eq 124 ] && timeout_flag=true

    # Asentamiento: dar tiempo a una pasada de asociacion con todos los picks.
    sleep "$SETTLE"
    t2="$(date +%s)"

    # Parada acotada (no usa ew_monitor.sh: mataria por patron de linea de comandos)
    TANK_REPLAY_PARAMS="$params" "$TANK_REPLAY" stop >/dev/null 2>&1 || true

    # Pared real del replay, medida con los CurrentTime de tankplayer.
    local wall
    wall="$(awk '
        /CurrentTime=\[/ {
            match($0, /CurrentTime=\[[0-9.]+\]/)
            s = substr($0, RSTART + 13, RLENGTH - 14)
            if (cnt == 0) first = s + 0
            last = s + 0; cnt++
        }
        END { if (cnt >= 2) printf "%.0f", last - first; else print "" }' "$sdir/play.log" 2>/dev/null)"
    if [ -z "$wall" ]; then wall=$(( t1 - t0 )); fi

    local csl cml wsv nev nmag ndisc
    csl="$(latest_log "$ldir" csnloc)"
    cml="$(latest_log "$ldir" csnmags_toy)"
    wsv="$(latest_log "$ldir" wave_serverV)"
    nev="$(count_in "$csl" 'csnloc: evento')"
    nmag="$(count_in "$cml" 'CSNmags_Red')"
    ndisc="$(count_in "$wsv" 'fails validity check, discarding')"

    # offsetTime de tankplayer: desplazamiento exacto entre el tiempo de dato y el
    # reloj de pared. Permite al reporte recuperar la hora HISTORICA del evento
    # (el replay re-estampa el dato a "ahora": tankplayer.c:650).
    local offs
    offs="$(awk '
        { if (match($0, /offsetTime=\[[0-9.eE+-]+\]/)) {
              print substr($0, RSTART + 12, RLENGTH - 13); exit } }' "$sdir/play.log" 2>/dev/null)"
    case "$offs" in ''|*[!0-9.eE+-]*) offs=0 ;; esac

    {
        printf '{\n'
        printf '  "session_id": %s,\n'      "$(json_str "$slug/$(printf '%03d' "$idx")")"
        printf '  "slug": %s,\n'            "$(json_str "$slug")"
        printf '  "source_file": %s,\n'     "$(json_str "${SRC_FILE:-}")"
        printf '  "chunk_index": %s,\n'     "$idx"
        printf '  "chunk": %s,\n'           "$(json_str "$chunk")"
        printf '  "chunk_start_epoch": %s,\n' "$start"
        printf '  "chunk_start_utc": %s,\n' "$(json_str "$(epoch_to_iso "$start")")"
        printf '  "chunk_span_s": %s,\n'    "$cspan"
        printf '  "nmsgs": %s,\n'           "$nmsgs"
        printf '  "mode": %s,\n'            "$(json_str "$MODE")"
        printf '  "delay_ms": %s,\n'        "$delay"
        printf '  "sendlate_s": %s,\n'      "$SENDLATE"
        printf '  "assoc_window_s": %s,\n'  "$ASSOC_WINDOW"
        printf '  "settle_s": %s,\n'        "$SETTLE"
        printf '  "wall_s": %s,\n'          "$wall"
        printf '  "offset_time_s": %s,\n'   "$offs"
        printf '  "play_total_s": %s,\n'    "$(( t1 - t0 ))"
        printf '  "session_total_s": %s,\n' "$(( t2 - t0 ))"
        printf '  "exit_code": %s,\n'       "$rc"
        printf '  "timeout": %s,\n'         "$timeout_flag"
        printf '  "counts": { "events": %s, "magnitudes": %s, "ws_discards": %s },\n' \
               "$nev" "$nmag" "$ndisc"
        printf '  "logs": { "csnloc": %s, "csnmags_toy": %s, "wave_serverV": %s, "play": %s }\n' \
               "$(json_str "$csl")" "$(json_str "$cml")" "$(json_str "$wsv")" \
               "$(json_str "$sdir/play.log")"
        printf '}\n'
    } > "$manifest"

    if [ "$timeout_flag" = true ]; then
        echo "      sesion $idx: TIMEOUT (${TIMEOUT}s)  eventos=$nev mag=$nmag descartes=$ndisc"
        return 2
    fi
    echo "      sesion $idx: pared=${wall}s  eventos=$nev  magnitudes=$nmag  descartes_ws=$ndisc"
    [ "$rc" -eq 0 ] || return 2
    return 0
}

# -----------------------------------------------------------------------------
#  Reporte de un slug (para el modo interactivo)
# -----------------------------------------------------------------------------
show_report() {  # <slug>
    command -v python3 >/dev/null 2>&1 || return 0
    ( cd "$ROOT_DIR" && python3 -m api_report_locs.report \
        --runs "$OUTDIR" --slug "$1" --format table 2>/dev/null ) || true
}

ask_continue() {  # <slug>
    [ "$DIRMODE" = "interactive" ] || return 0
    show_report "$1"
    if [ ! -t 0 ]; then
        echo "[eventos] (stdin no es una terminal: continuo automaticamente)"
        return 0
    fi
    local ans
    printf '[eventos] ¿continuar con el siguiente evento? [s/N] '
    read -r ans || ans="n"
    case "$ans" in
        s|S|si|Si|SI|y|Y|yes) return 0 ;;
        *) echo "[eventos] detenido por el usuario."; return 1 ;;
    esac
}

# -----------------------------------------------------------------------------
#  Bucle principal
# -----------------------------------------------------------------------------
mapfile -t TARGETS < <(target_list "$TARGET")
[ "${#TARGETS[@]}" -gt 0 ] || die "sin objetivos"

echo "[eventos] comando : $CMD"
echo "[eventos] objetivos: ${#TARGETS[@]}"
echo "[eventos] modo    : $MODE  (chunk=${CHUNK_SPAN}s overlap=${OVERLAP}s min_wall=${MIN_WALL}s)"
echo "[eventos] settle  : ${SETTLE}s  (cadencia de csnloc = ${CADENCE}s con AssocWindowSec=$ASSOC_WINDOW)"
echo "[eventos] salida  : $OUTDIR"
if [ "$MODE" = "fast" ]; then
    echo "[eventos] invariante wave_serverV: span + pared <= $(( 900 + SENDLATE ))s"
fi
echo

if [ "$CMD" = "run" ] && [ "$DRYRUN" -eq 0 ]; then
    echo "[eventos] AVISO: el modo replay DETIENE el EarthWorm en marcha (startstop)"
    echo "[eventos]        y arranca con startstop_replay.d (sin slink2ew)."
    if [ "$DIRMODE" = "interactive" ] && [ -t 0 ]; then
        printf '[eventos] ¿continuar? [s/N] '
        read -r ans || ans="n"
        case "$ans" in s|S|si|S|y|Y) ;; *) echo "[eventos] cancelado."; exit 0 ;; esac
    fi
    echo
fi

n_tanks=0; n_sess=0; n_fail=0
declare -a FAILED=()

for row in "${TARGETS[@]}"; do
    IFS='|' read -r slug tank mf <<< "$row"
    n_tanks=$(( n_tanks + 1 ))
    echo "=== [$n_tanks/${#TARGETS[@]}] $slug"
    echo "    tank: $tank"

    # Fichero miniSEED de origen (informativo, para el reporte)
    SRC_FILE=""
    if [ -n "$mf" ] && [ "$mf" != "-" ]; then
        SRC_FILE="$(manifest_field "$mf" source)"
        g_s="$(manifest_field "$mf" start_utc)"
        g_e="$(manifest_field "$mf" end_utc)"
        [ -n "$g_s" ] && echo "    ventana global : $g_s .. $g_e"
    fi

    # La ventana operativa es la del canal pedido: los canales de periodo largo
    # llegan mas lejos que los HH y el span global dejaria trozos vacios al final.
    if ! tank_bounds "$tank" "$mf" "$CHAN"; then
        echo "    ERROR: no se pudo determinar la ventana del canal $CHAN" >&2
        n_fail=$(( n_fail + 1 )); FAILED+=("$slug: sin ventana $CHAN"); continue
    fi
    echo "    ventana $CHAN     : $(epoch_to_iso "$START_EPOCH") .. $(epoch_to_iso "$END_EPOCH")  ($(( END_EPOCH - START_EPOCH ))s)"

    # --- plan de trozos ---
    PLAN="$(plan_tank "$slug" "$tank" "$START_EPOCH" "$END_EPOCH")" || {
        n_fail=$(( n_fail + 1 )); FAILED+=("$slug: troceo fallo"); continue; }

    if [ "$CMD" = "plan" ]; then
        printf '    %-3s %-20s %-9s %-8s %-6s %-9s %-8s %-6s %s\n' \
               '#' 'inicio_utc' 'span' 'nmsgs' 'delay' 'pared_est' 'settle' 'lead' 'check'
    fi

    # --- params aislados (una copia por tank, se reutiliza entre sesiones) ---
    params="$OUTDIR/$slug/params"
    tanks="$OUTDIR/$slug/tanks"
    if [ "$DRYRUN" -eq 0 ]; then
        if [ ! -f "$params/csnloc.d" ] || [ "$FORCE" -eq 1 ]; then
            make_params_copy "$params" "$PARAMS_MODE" "$tanks" >/dev/null || {
                echo "    ERROR: make_params_copy fallo" >&2
                n_fail=$(( n_fail + 1 )); FAILED+=("$slug: params"); continue; }
            # AssocWindowSec de la copia aislada (NO toca produccion)
            aw="$ASSOC_WINDOW"; case "$aw" in *.*) ;; *) aw="$aw.0" ;; esac
            params_set "$params/csnloc.d" AssocWindowSec "$aw"
            if [ "$DEBUG" -eq 1 ]; then
                params_set "$params/csnloc.d" DumpHypo 1
                params_set "$params/csnloc.d" Debug 1
            fi
        fi
        mkdir -p "$tanks"
    fi

    # --- sesiones ---
    while IFS='|' read -r idx chunk cstart cnmsgs cspan cmax cdelay cwall; do
        [ -n "$idx" ] || continue
        chk="$(check_session "$cspan" "$cwall" "$MODE")"
        lead=$(( cmax - cstart ))
        if [ "$CMD" = "plan" ]; then
            printf '    %-3s %-20s %-9s %-8s %-6s %-9s %-8s %-6s %s\n' \
                   "$idx" "$(epoch_to_iso "$cstart")" "${cspan}s" "$cnmsgs" \
                   "${cdelay}ms" "${cwall}s" "${SETTLE}s" "${lead}s" "$chk"
            [ "$chk" = "ok" ] || { n_fail=$(( n_fail + 1 )); FAILED+=("$slug/$idx: $chk"); }
            continue
        fi
        if [ "$chk" != "ok" ]; then
            echo "    sesion $idx: RECHAZADA por invariante: $chk" >&2
            n_fail=$(( n_fail + 1 )); FAILED+=("$slug/$idx: $chk"); continue
        fi
        n_sess=$(( n_sess + 1 ))
        run_session "$slug" "$idx" "$chunk" "$cstart" "$cnmsgs" "$cspan" "$cdelay" "$params" "$tanks" || {
            n_fail=$(( n_fail + 1 )); FAILED+=("$slug/$idx: replay"); }
    done <<< "$PLAN"

    if [ "$CMD" = "run" ] && [ "$DRYRUN" -eq 0 ]; then
        if [ "$KEEP_TANKS" -eq 0 ]; then
            rm -rf "$tanks"
        fi
        if ! ask_continue "$slug"; then
            break
        fi
    fi
    echo
done

echo "[eventos] tanks procesados : $n_tanks"
[ "$CMD" = "run" ] && echo "[eventos] sesiones ejecutadas: $n_sess"
echo "[eventos] fallos           : $n_fail"
if [ "$n_fail" -gt 0 ]; then
    for x in "${FAILED[@]}"; do echo "  - $x"; done
    exit 2
fi

if [ "$CMD" = "plan" ]; then
    echo
    echo "[eventos] plan listo. Para ejecutar:"
    echo "  $0 run $TARGET $([ "$MODE" != fast ] && echo "--mode $MODE")"
elif [ "$DRYRUN" -eq 0 ]; then
    echo
    echo "[eventos] resultados en: $OUTDIR"
    echo "[eventos] reporte:  python3 -m api_report_locs.report --runs $OUTDIR"
fi
exit 0
