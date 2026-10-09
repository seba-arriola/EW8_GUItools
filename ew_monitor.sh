#!/bin/bash
# =============================================================================
#  ew_monitor.sh - Control de EarthWorm 8 (startstop + modulos)
#  Proyecto: EW8_GUItools
#
#  Mismo archivo en el repo y en el portable: SCRIPT_DIR resuelve a la carpeta
#  que lo contiene, de modo que EW_UNIX/EW_LOG apuntan a cada instancia.
#  deploy_portable.sh lo copia TAL CUAL (no se mantiene una copia aparte).
#
#  Uso:
#     ./ew_monitor.sh status
#     ./ew_monitor.sh start
#     ./ew_monitor.sh stop
#     ./ew_monitor.sh restart
#
#  Nota: esta distribucion de EarthWorm no incluye el binario `stop` /
#  `quakessterminate`. La parada ordenada se logra enviando SIGTERM al proceso
#  `startstop`, cuyo manejador llama a StopEarthworm(): pone el flag TERMINATE
#  en los anillos y detiene a todos los modulos hijos.
# =============================================================================

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

EW_UNIX="${EW_UNIX:-$SCRIPT_DIR/ew8_unix.sh}"
EW_LOG_DIR="${EW_LOG:-$SCRIPT_DIR/run_working_v8/log}"
STARTSTOP_CONSOLE="${STARTSTOP_CONSOLE:-$EW_LOG_DIR/startstop_console.log}"
STOP_TIMEOUT="${STOP_TIMEOUT:-30}"

get_startstop_pid() { pgrep -x startstop 2>/dev/null | head -n1; }

is_running() { [ -n "$(get_startstop_pid)" ]; }

child_pids() {
    local spid; spid="$(get_startstop_pid)"
    [ -n "$spid" ] && pgrep -P "$spid" 2>/dev/null
}

status() {
    local pid; pid="$(get_startstop_pid)"
    if [ -n "$pid" ]; then
        echo "EarthWorm: RUNNING (startstop pid $pid)"
        echo "  EW_PARAMS : ${EW_PARAMS:-<no definido>}"
        echo "  log dir   : $EW_LOG_DIR"
        echo "  modulos   :"
        local c
        for c in $(child_pids); do
            printf '    - %s\n' "$(ps -o args= -p "$c" 2>/dev/null)"
        done
        return 0
    else
        echo "EarthWorm: STOPPED"
        return 1
    fi
}

start() {
    if is_running; then
        echo "EarthWorm ya esta corriendo (startstop pid $(get_startstop_pid))."
        return 0
    fi

    [ -f "$EW_UNIX" ] || { echo "ERROR: no existe $EW_UNIX"; return 1; }
    mkdir -p "$EW_LOG_DIR"

    # Cargar el entorno y lanzar startstop de forma independiente (setsid)
    # shellcheck disable=SC1090
    ( cd "$SCRIPT_DIR" && source "$EW_UNIX" >/dev/null 2>&1 && \
      setsid startstop </dev/null >"$STARTSTOP_CONSOLE" 2>&1 & disown )

    echo "Iniciando EarthWorm ..."
    local i pid=""
    for i in $(seq 1 20); do
        sleep 1
        pid="$(get_startstop_pid)"
        [ -n "$pid" ] && break
    done

    if [ -n "$pid" ]; then
        echo "EarthWorm iniciado (startstop pid $pid)."
        echo "  consola: $STARTSTOP_CONSOLE"
        return 0
    else
        echo "ERROR: startstop no arranco. Revise $STARTSTOP_CONSOLE"
        return 1
    fi
}

stop() {
    if ! is_running; then
        echo "EarthWorm ya esta detenido."
        return 0
    fi

    local pid i; pid="$(get_startstop_pid)"
    echo "Deteniendo EarthWorm (startstop pid $pid) ..."
    kill -TERM "$pid" 2>/dev/null

    for i in $(seq 1 "$STOP_TIMEOUT"); do
        sleep 1
        is_running || break
    done

    # Si aun queda startstop, insistir; luego limpiar modulos huerfanos
    if is_running; then
        echo "  startstop no respondio; enviando SIGTERM a los modulos ..."
        local c
        for c in $(child_pids); do kill -TERM "$c" 2>/dev/null; done
        sleep 3
    fi

    if is_running; then
        local spid; spid="$(get_startstop_pid)"
        kill -KILL "$spid" 2>/dev/null
        sleep 1
    fi

    # Limpiar cualquier modulo que siga vivo
    # (ew2glass|glass2ew son del camino LEGACY GLASS3+Kafka: solo aparecen si
    #  se reactivo ese flujo; ver AGENTS.md seccion 8)
    local leftovers
    leftovers="$(pgrep -f 'slink2ew|pick_FP|ew2glass|glass2ew|wave_serverV|csnmags|csntvp|csnhypodbp|csnrv|csnstaevdisp|ew_controller' 2>/dev/null)"
    if [ -n "$leftovers" ]; then
        echo "  matando modulos remanentes: $(echo $leftovers | tr '\n' ' ')"
        kill -KILL $leftovers 2>/dev/null
        sleep 1
    fi

    is_running && { echo "ERROR: no se pudo detener EarthWorm."; return 1; } || { echo "EarthWorm detenido."; return 0; }
}

restart() { stop && sleep 2 && start; }

case "${1:-}" in
    status)  status ;;
    start)   start ;;
    stop)    stop ;;
    restart) restart ;;
    *) echo "Uso: $0 {status|start|stop|restart}"; exit 1 ;;
esac
