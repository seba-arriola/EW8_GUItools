#!/bin/bash
# =============================================================================
#  tank_replay.sh - Reproduce eventos miniSEED (via tank) para probar csnloc
#  Proyecto: EW8_GUItools
#
#  Uso:
#     ./tank_tools/tank_replay.sh build  <in.mseed> <outdir> [opciones mk_tank]
#     ./tank_tools/tank_replay.sh sniff  <tank>
#     ./tank_tools/tank_replay.sh play   <tank|directorio> [opciones]
#     ./tank_tools/tank_replay.sh stop
#     ./tank_tools/tank_replay.sh status
#
#  Que hace 'play':
#     1. genera run_working_v8/params/tankplayer_replay.d desde la plantilla
#        run_working_v8/params/tankplayer.d.tmpl
#     2. detiene el EarthWorm que este en marcha (ew_monitor.sh stop)
#     3. arranca startstop con run_working_v8/params/startstop_replay.d
#        (igual que el normal pero SIN slink2ew)
#     4. lanza tankplayer en primer plano y espera a que termine
#     5. deja el stack arriba para inspeccionar con csnrv/csnhypodbp
#
#  Por que se para slink2ew: slink2ew y tankplayer escriben los dos en
#  SLINK_RING; si corren a la vez se mezclan dos flujos.
#
#  ATENCION: 'play' DETIENE el EarthWorm de operacion. No usarlo en produccion.
#
#  Opciones de 'play':
#     --mode realtime|fast   realtime = ritmo real de los timestamps (default).
#                            fast = tan rapido como permita --delay-ms.
#     --sendlate S           segundos hacia atras para re-estampar (default 30).
#                            Usar 'none' para conservar los timestamps historicos
#                            (entonces csnloc NO localiza: los picks viejos se
#                            descartan por PickTTLSec/AssocWindowSec).
#     --delay-ms N           fuerza InterMessageDelayMillisecs (anula el default
#                            del modo: 0 en realtime, 10 en fast).
#     --pause S              Pause entre ficheros (default 10).
#     --startup S            StartUpDelay antes del primer fichero (default 10).
#     --sequence 0|1         Sequence del .d (default 0: re-ancla cada fichero).
#
#  Codigos de salida:
#     0  ok
#     1  error de uso
#     2  el tank no es legible por tanksniff
#     3  archivo/plantilla/binario faltante
# =============================================================================

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

# Carga el entorno EarthWorm (EW_HOME, EW_PARAMS, EW_LOG, PATH ...).
# OJO: ew8_unix.sh hace 'cd' a la raiz del repo; por eso todo lo de abajo usa
# rutas absolutas.
if [ -f "$ROOT_DIR/ew8_unix.sh" ]; then
    # shellcheck disable=SC1091
    source "$ROOT_DIR/ew8_unix.sh" >/dev/null 2>&1 || true
fi

EW_BIN="${EW_HOME:-$ROOT_DIR}/${EW_VERSION:-earthworm_8.0}/bin"
TANKPLAYER="$EW_BIN/tankplayer"
TANKSNIFF="$EW_BIN/tanksniff"
EW_MONITOR="$ROOT_DIR/ew_monitor.sh"
# TANK_REPLAY_PARAMS / TANK_REPLAY_LOG permiten apuntar el replay a un EW_PARAMS y
# un EW_LOG aislados (los usan los tests para no tocar la config ni los logs reales).
PARAMS_DIR="${TANK_REPLAY_PARAMS:-${EW_PARAMS:-$ROOT_DIR/run_working_v8/params}}"
LOG_DIR="${TANK_REPLAY_LOG:-${EW_LOG:-$ROOT_DIR/run_working_v8/log}}"
TMPL="$PARAMS_DIR/tankplayer.d.tmpl"
GEN_D="$PARAMS_DIR/tankplayer_replay.d"
STARTSTOP_CFG="$PARAMS_DIR/startstop_replay.d"

usage() {
    cat <<'EOF'
Uso:
   ./tank_tools/tank_replay.sh build  <in.mseed> <outdir> [opciones mk_tank]
   ./tank_tools/tank_replay.sh sniff  <tank>
   ./tank_tools/tank_replay.sh play   <tank|directorio> [opciones]
   ./tank_tools/tank_replay.sh stop
   ./tank_tools/tank_replay.sh status

Subcomandos:
   build   Convierte miniSEED a tank (delega en tank_tools/mk_tank.sh).
   sniff   Inventario de un tank con tanksniff (SCNL, nsamp, ventana temporal).
   play    Arranca EarthWorm SIN slink2ew y reproduce el tank con tankplayer.
           Para el EarthWorm que este en marcha. Deja el stack arriba al
           terminar, para inspeccionar con csnrv / csnhypodbp.
           Si <tank|directorio> es un directorio, usa sus chunk_*.tank en orden.
   stop    Detiene tankplayer y luego el stack (ew_monitor.sh stop).
   status  Estado del stack y de tankplayer + cola del ultimo log de replay.

Opciones de 'play':
   --mode realtime|fast   realtime (default): ritmo real de los timestamps.
                          fast: lo mas rapido posible segun --delay-ms.
   --sendlate S           segundos hacia atras al re-estampar (default 30).
                          'none' = conservar timestamps historicos (csnloc NO
                          localizara, porque descarta picks viejos).
   --delay-ms N           fuerza InterMessageDelayMillisecs (default: 0 en
                          realtime, 10 en fast). 0 = usar timestamps.
   --pause S              Pause entre ficheros (default 10).
   --startup S            StartUpDelay antes del primer fichero (default 10).
   --sequence 0|1         Sequence del .d. Default 0 (re-ancla cada fichero).
   --dry-run              solo genera el .d y lo imprime; NO detiene ni arranca
                          nada. Util para inspeccionar la configuracion.

Ejemplo:
   ./tank_tools/tank_replay.sh play replay/tanks/ev1.tank --mode realtime
   ./tank_tools/tank_replay.sh play replay/tanks --mode fast --delay-ms 10
EOF
}

# -----------------------------------------------------------------------------
#  Render de la plantilla
#    - @WAVEFILES@ se sustituye por una linea "WaveFile <ruta>" por fichero
#    - si un marcador queda vacio, la linea entera se elimina
#  Variables globales usadas: PAUSE STARTUPDELAY SENDLATE DELAYMS SEQUENCE
# -----------------------------------------------------------------------------
render_config() {
    local tmpl="$1" out="$2" block="$3"
    local line had_ph wf

    : > "$out"
    while IFS= read -r line || [ -n "$line" ]; do
        if [ "$line" = "@WAVEFILES@" ]; then
            while IFS= read -r wf || [ -n "$wf" ]; do
                printf 'WaveFile      %s\n' "$wf" >> "$out"
            done < "$block"
            continue
        fi

        had_ph=0
        case "$line" in
            *@*) had_ph=1 ;;
        esac

        line="${line//@PAUSE@/$PAUSE}"
        line="${line//@STARTUPDELAY@/$STARTUPDELAY}"
        line="${line//@SENDLATE@/$SENDLATE}"
        line="${line//@DELAYMS@/$DELAYMS}"
        line="${line//@SEQUENCE@/$SEQUENCE}"

        # linea que solo contenia un marcador vacio -> se descarta
        if [ "$had_ph" -eq 1 ] && [[ "$line" =~ ^[[:space:]]*[A-Za-z0-9_]+[[:space:]]*$ ]]; then
            continue
        fi

        printf '%s\n' "$line" >> "$out"
    done < "$tmpl"
}

start_replay_stack() {
    mkdir -p "$LOG_DIR"
    # shellcheck disable=SC1091
    ( cd "$ROOT_DIR" && source "$ROOT_DIR/ew8_unix.sh" >/dev/null 2>&1 && \
      { [ -z "${TANK_REPLAY_PARAMS:-}" ] || export EW_PARAMS="$TANK_REPLAY_PARAMS"; } && \
      { [ -z "${TANK_REPLAY_LOG:-}" ]    || export EW_LOG="$TANK_REPLAY_LOG"; } && \
      setsid startstop "$STARTSTOP_CFG" </dev/null \
             >"$LOG_DIR/startstop_console_replay.log" 2>&1 & disown )

    local i
    for i in $(seq 1 30); do
        sleep 1
        if pgrep -x pick_FP >/dev/null 2>&1 && pgrep -x csnloc >/dev/null 2>&1; then
            sleep 3   # margen para que abran rings/tanks
            return 0
        fi
    done
    echo "AVISO: pick_FP/csnloc no aparecen tras 30 s. Revisa $LOG_DIR/startstop_console_replay.log" >&2
    return 1
}

# Parada ACOTADA: solo startstop y sus hijos. Se usa cuando el replay corre sobre
# un EW_PARAMS aislado (tests), porque ew_monitor.sh stop mata por patron
# 'pgrep -f slink2ew|pick_FP|...' y ese patron coincide con la linea de comandos
# del propio script de test (que contiene esos nombres) -> se suicidaria.
stop_replay_stack() {
    local spid c
    spid="$(pgrep -x startstop 2>/dev/null | head -n1)"
    if [ -z "$spid" ]; then
        echo "[replay] EarthWorm ya esta detenido."
        return 0
    fi
    echo "[replay] deteniendo EarthWorm (startstop pid $spid) ..."
    for c in $(pgrep -P "$spid" 2>/dev/null); do kill -TERM "$c" 2>/dev/null; done
    kill -TERM "$spid" 2>/dev/null
    local i
    for i in $(seq 1 20); do
        sleep 1
        pgrep -x startstop >/dev/null 2>&1 || break
    done
    if pgrep -x startstop >/dev/null 2>&1; then
        spid="$(pgrep -x startstop 2>/dev/null | head -n1)"
        for c in $(pgrep -P "$spid" 2>/dev/null); do kill -KILL "$c" 2>/dev/null; done
        kill -KILL "$spid" 2>/dev/null
        sleep 1
    fi
    echo "[replay] EarthWorm detenido."
}

cmd_build() {
    exec "$SCRIPT_DIR/mk_tank.sh" "$@"
}

cmd_sniff() {
    local t="${1:-}"
    [ -n "$t" ] || { usage >&2; exit 1; }
    [ -f "$t" ] || { echo "ERROR: no existe $t" >&2; exit 3; }
    [ -x "$TANKSNIFF" ] || { echo "ERROR: falta $TANKSNIFF" >&2; exit 3; }
    mkdir -p "$LOG_DIR"
    "$TANKSNIFF" "$t" | tee "$LOG_DIR/tanksniff_$(basename "$t").$(date +%Y%m%d_%H%M%S).txt"
}

cmd_play() {
    local target="${1:-}"
    shift || true
    [ -n "$target" ] || { usage >&2; exit 1; }

    MODE="realtime"
    SENDLATE="30"
    DELAYMS=""
    PAUSE="10"
    STARTUPDELAY="10"
    SEQUENCE="0"
    DRYRUN=0

    while [ $# -gt 0 ]; do
        case "$1" in
            --mode)     MODE="${2:-}";        shift 2 ;;
            --sendlate) SENDLATE="${2:-}";    shift 2 ;;
            --delay-ms) DELAYMS="${2:-}";     shift 2 ;;
            --pause)    PAUSE="${2:-}";       shift 2 ;;
            --startup)  STARTUPDELAY="${2:-}"; shift 2 ;;
            --sequence) SEQUENCE="${2:-}";    shift 2 ;;
            --dry-run)  DRYRUN=1;             shift ;;
            -h|--help)  usage; exit 0 ;;
            *) echo "ERROR: opcion desconocida: $1" >&2; exit 1 ;;
        esac
    done

    case "$MODE" in
        realtime) [ -n "$DELAYMS" ] || DELAYMS=0 ;;
        fast)     [ -n "$DELAYMS" ] || DELAYMS=10 ;;
        *) echo "ERROR: --mode debe ser 'realtime' o 'fast'" >&2; exit 1 ;;
    esac
    case "$SENDLATE" in
        ""|none|off|no|None|NONE) SENDLATE="" ;;
    esac

    [ -x "$TANKPLAYER" ] || { echo "ERROR: falta $TANKPLAYER (carga ./ew8_unix.sh)" >&2; exit 3; }
    [ -x "$TANKSNIFF" ]  || { echo "ERROR: falta $TANKSNIFF" >&2; exit 3; }
    [ -f "$TMPL" ]       || { echo "ERROR: falta la plantilla $TMPL" >&2; exit 3; }
    [ -f "$STARTSTOP_CFG" ] || { echo "ERROR: falta $STARTSTOP_CFG" >&2; exit 3; }
    [ -x "$EW_MONITOR" ] || { echo "ERROR: falta $EW_MONITOR" >&2; exit 3; }

    if pgrep -x tankplayer >/dev/null 2>&1; then
        echo "ERROR: ya hay un tankplayer corriendo. Usa primero: $0 stop" >&2
        exit 1
    fi

    # --- lista de ficheros a reproducir --------------------------------------
    local files=() f
    if [ -d "$target" ]; then
        while IFS= read -r f; do files+=( "$f" ); done \
            < <(find "$target" -maxdepth 1 -name 'chunk_*.tank' | sort)
        if [ "${#files[@]}" -eq 0 ]; then
            echo "ERROR: no hay archivos chunk_*.tank en $target" >&2
            exit 3
        fi
    else
        [ -f "$target" ] || { echo "ERROR: no existe $target" >&2; exit 3; }
        files+=( "$(readlink -f "$target")" )
    fi

    if [ "${#files[@]}" -gt 1000 ]; then
        echo "ERROR: ${#files[@]} ficheros; tankplayer admite maximo 1000 WaveFile." >&2
        echo "       Reproduce un subconjunto o trocea mas grueso." >&2
        exit 3
    fi

    "$TANKSNIFF" "${files[0]}" >/dev/null 2>&1 \
        || { echo "ERROR: ${files[0]} no es legible por tanksniff (no parece un tank)" >&2; exit 2; }

    # --- config generada -----------------------------------------------------
    local block; block="$(mktemp)"
    for f in "${files[@]}"; do printf '%s\n' "$f" >> "$block"; done
    render_config "$TMPL" "$GEN_D" "$block"
    rm -f "$block"

    echo "[replay] config  : $GEN_D"
    if [ -n "$SENDLATE" ]; then
        echo "[replay] modo    : $MODE  (SendLate=$SENDLATE, InterMessageDelayMillisecs=$DELAYMS)"
    else
        echo "[replay] modo    : $MODE  (SendLate DESACTIVADO -> timestamps historicos; csnloc NO localizara)"
    fi
    echo "[replay] ficheros: ${#files[@]}"

    if [ "$DRYRUN" -eq 1 ]; then
        echo "[replay] --dry-run: config generada, no se arranca nada."
        grep -vE '^[[:space:]]*#|^[[:space:]]*$' "$GEN_D"
        exit 0
    fi

    # --- stack en modo replay -----------------------------------------------
    if [ -n "${TANK_REPLAY_PARAMS:-}" ]; then
        echo "[replay] deteniendo EarthWorm (parada acotada: EW_PARAMS aislado) ..."
        stop_replay_stack
    else
        echo "[replay] deteniendo cualquier EarthWorm en marcha ..."
        "$EW_MONITOR" stop || true
    fi

    echo "[replay] arrancando EarthWorm en modo replay (sin slink2ew) ..."
    start_replay_stack || true

    # --- reproduccion --------------------------------------------------------
    mkdir -p "$LOG_DIR"
    local rlog; rlog="$LOG_DIR/replay_$(date +%Y%m%d_%H%M%S).log"
    echo "[replay] lanzando tankplayer (log: $rlog)"
    echo "[replay] Ctrl-C para abortar la reproduccion."
    echo "----------------------------------------------------------------------"

    "$TANKPLAYER" "$GEN_D" 2>&1 | tee "$rlog"
    local rc=${PIPESTATUS[0]}

    echo "----------------------------------------------------------------------"
    echo "[replay] tankplayer termino (rc=$rc)"
    echo "  El stack sigue arriba. Verificar (OJO: usa 'sniffring -n' para ver lo"
    echo "  que YA esta en el ring; sin '-n' lo drena y solo muestra lo NUEVO):"
    echo "    earthworm_8.0/bin/sniffring -n PICK_RING   # picks ya en el ring"
    echo "    earthworm_8.0/bin/sniffring -n HYPO_RING   # hipocentros ya en el ring"
    echo "    earthworm_8.0/bin/sniffring    PICK_RING   # picks NUEVOS, en vivo"
    echo "    grep evento $LOG_DIR/csnloc_*.log"
    echo "  (sniffring no termina solo: Ctrl-C, o 'timeout 15 ... | head')"
    echo "  Para volver al modo normal:"
    echo "    $0 stop      # y luego: ./ew_monitor.sh start"
    return 0
}

cmd_stop() {
    local pids
    pids="$(pgrep -x tankplayer 2>/dev/null)"
    if [ -n "$pids" ]; then
        echo "[replay] deteniendo tankplayer: $(echo $pids | tr '\n' ' ')"
        kill -TERM $pids 2>/dev/null
        sleep 2
        pids="$(pgrep -x tankplayer 2>/dev/null)"
        [ -n "$pids" ] && kill -KILL $pids 2>/dev/null
    else
        echo "[replay] tankplayer no estaba corriendo."
    fi

    if [ -x "$EW_MONITOR" ] && [ -z "${TANK_REPLAY_PARAMS:-}" ]; then
        "$EW_MONITOR" stop || true
    else
        stop_replay_stack
    fi
    echo "[replay] replay detenido."
}

cmd_status() {
    if [ -x "$EW_MONITOR" ]; then
        "$EW_MONITOR" status || true
    fi

    local pids; pids="$(pgrep -x tankplayer 2>/dev/null)"
    if [ -n "$pids" ]; then
        echo "tankplayer : RUNNING (pid $(echo $pids | tr '\n' ' '))"
    else
        echo "tankplayer : STOPPED"
    fi

    local last; last="$(ls -t "$LOG_DIR"/replay_*.log 2>/dev/null | head -n1)"
    if [ -n "$last" ]; then
        echo "ultimo replay log: $last"
        tail -n 5 "$last" | sed 's/^/    /'
    fi
    return 0
}

case "${1:-}" in
    build)  shift; cmd_build  "$@" ;;
    sniff)  shift; cmd_sniff  "$@" ;;
    play)   shift; cmd_play   "$@" ;;
    stop)   cmd_stop ;;
    status) cmd_status ;;
    -h|--help) usage; exit 0 ;;
    *) usage >&2; exit 1 ;;
esac
