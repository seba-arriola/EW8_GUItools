#!/bin/sh
# diag.sh - diagnostico headless, acotado y NO destructivo de los modulos de
# EW8_GUItools. POSIX: corre igual con `sh` (dash) y con `bash`; no usa arrays
# ni sustitucion de procesos a proposito, porque el agente lo invoca con `sh`.
#
# Existe porque el permiso de shell se evalua POR SEGMENTO del comando: para un
# agente, `source ./ew8_unix.sh && cd run_working_v8/params && <bin> <bin>.d`
# son tres comandos y cada uno necesita su propia regla. Toda esa receta vive
# aca adentro, asi que el agente escribe UN solo comando:
#
#     sh ew_gui_tools/common/tools/diag.sh <subcomando> [args]
#
# NIVEL A (solo lectura, no arranca nada):
#     env                 entorno EarthWorm y presencia de los binarios
#     install             estado de los binarios instalados
#     config   <modulo>   <bin> --print-config <modulo>.d
#     profiles            csnhypodbp --check-profiles csnhypodbp.d
#     overlays <modulo>   csnrv|csnstaevdisp --check-overlays <modulo>.d
#     rings    [anillo…]  sniffring -n de los anillos del stack
#     log      <modulo> [n]  ultimas n lineas del log mas reciente de <modulo>
#     logs                inventario de logs (fecha y tamano)
#     errors   [n]        lineas de error de los logs mas recientes
#     process             binarios del proyecto que estan corriendo
#
# NIVEL B (arranca UN modulo, acotado por timeout; el permiso del agente va en
# 'ask', asi que lo aprueba el usuario y ve el comando exacto):
#     run      <modulo> [segundos] [--bg] [--force]
#     attached <modulo>   salida del ultimo `run --bg <modulo>`
#     stop     <modulo>   mata SOLO un proceso lanzado por este script
#
#     help
#
# Exit: 0 ok · 1 el diagnostico reporto un fallo · 2 uso · 3 subcomando prohibido
#       4 precondicion no cumplida (ya esta corriendo)
#
# Lista CERRADA: cualquier subcomando fuera de la de arriba sale con exit 3.
# No existe (ni existira) un subcomando que corra startstop/stopmodule/restart/
# ew_monitor.sh, make, git, sudo, rm ni un kill arbitrario: no es diagnostico.
#
# Variables de entorno, pensadas para pruebas: DIAG_PARAMS, DIAG_LOG, DIAG_TMP,
# DIAG_SECS.
set -u

SELF_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$SELF_DIR/../../.." && pwd)"
PARAMS="${DIAG_PARAMS:-$ROOT/run_working_v8/params}"
LOGDIR="${DIAG_LOG:-$ROOT/run_working_v8/log}"
TMP="${DIAG_TMP:-/tmp/opencode/diag}"
TMO=/usr/bin/timeout
SECS_DEF="${DIAG_SECS:-20}"
SECS_MAX=120
MAXLINES=40

EX_OK=0
EX_FAIL=1
EX_USAGE=2
EX_DENY=3
EX_PRECOND=4

EW_LOADED=0

say()  { printf '%s\n' "$*"; }
warn() { printf 'AVISO: %s\n' "$*" >&2; }
die()  { printf 'ERROR: %s\n' "$*" >&2; exit "$EX_FAIL"; }
deny() { printf 'DENEGADO: %s\n' "$*" >&2; exit "$EX_DENY"; }

usage() {
    cat <<'EOF'
uso: diag.sh <subcomando> [args]

nivel A (solo lectura, no arranca nada):
  env                  entorno EarthWorm y presencia de los binarios
  install              estado de los binarios instalados
  config  <modulo>     <bin> --print-config <modulo>.d (csnhypodbp|csnrv|csnwphase)
  profiles             csnhypodbp --check-profiles csnhypodbp.d
  overlays <modulo>    csnrv|csnstaevdisp --check-overlays <modulo>.d
  rings   [anillo…]    sniffring -n de los anillos del stack (5 s por anillo)
  log     <modulo> [n] ultimas n lineas del log mas reciente de <modulo>
  logs                 inventario de logs (fecha y tamano)
  errors  [n]          lineas de error de los logs mas recientes
  process              binarios del proyecto que estan corriendo

nivel B (arranca un modulo, acotado por timeout):
  run     <modulo> [segundos] [--bg] [--force]
  attached <modulo>    salida del ultimo `run --bg <modulo>`
  stop    <modulo>     mata SOLO un proceso lanzado por este script

  help                 esta ayuda

exit: 0 ok · 1 fallo del diagnostico · 2 uso · 3 subcomando prohibido
      4 precondicion no cumplida (ya esta corriendo)
EOF
}

need_tmp() { [ -d "$TMP" ] || mkdir -p "$TMP" 2>/dev/null || die "no puedo crear $TMP"; }

valid_name() { case "${1:-}" in ""|*[!A-Za-z0-9_.-]*) return 1;; esac; return 0; }
valid_ring() { case "${1:-}" in ""|*[!A-Z0-9_]*)       return 1;; esac; return 0; }

cap() {  # cap <archivo> : hasta MAXLINES lineas, avisando si trunco
    local n
    n=$(wc -l < "$1" 2>/dev/null || echo 0)
    head -n "$MAXLINES" "$1" 2>/dev/null
    [ "${n:-0}" -gt "$MAXLINES" ] && say "... (truncado: $n lineas en total)"
    return 0
}

ew_src() {  # carga el entorno EarthWorm en ESTE shell (una sola vez)
    [ "$EW_LOADED" = 1 ] && return 0
    EW_LOADED=1
    need_tmp
    [ -r "$ROOT/ew8_unix.sh" ] || { warn "no existe $ROOT/ew8_unix.sh"; return 1; }
    # `set +u` porque el script es de otro y puede tocar variables sin definir.
    set +u
    . "$ROOT/ew8_unix.sh" >"$TMP/ew8_unix.out" 2>&1
    local rc=$?
    set -u
    [ "$rc" = 0 ] || warn "ew8_unix.sh salio con $rc (salida en $TMP/ew8_unix.out)"
    return 0
}

module_list() {  # binarios declarados en el .d principal
    # Formato real:  Process\t  "slink2ew slink2ew_HHZ.d"   -> hay que quitar
    # las comillas o `pgrep -x` no casa nunca (módulo "invisible" para siempre).
    local d="$PARAMS/startstop_unix.d"
    [ -f "$d" ] || return 0
    awk '/^[[:space:]]*Process[[:space:]]/ {gsub(/"/,""); print $2}' "$d" | sort -u
}

ring_list() {    # anillos declarados en el .d principal
    # Formato real:  Ring   WAVE_RING   51200   -> el NOMBRE es $2 ($3 es el tamano)
    local d="$PARAMS/startstop_unix.d"
    [ -f "$d" ] || return 0
    awk '/^[[:space:]]*Ring[[:space:]]/ {print $2}' "$d" | sort -u
}

run_capture() {  # run_capture <archivo_salida> <segundos> <cmd…>
    local out="$1" secs="$2"
    shift 2
    "$TMO" "$secs" "$@" >"$out" 2>&1
}

# --- nivel A -----------------------------------------------------------------

cmd_env() {
    need_tmp
    ew_src || true
    say "EW_HOME    = ${EW_HOME:-<sin cargar>}"
    say "EW_VERSION = ${EW_VERSION:-<sin cargar>}"
    say "EW_PARAMS  = ${EW_PARAMS:-<sin cargar>}"
    say "EW_LOG     = ${EW_LOG:-<sin cargar>}"
    say "DISPLAY    = ${DISPLAY:-<vacio>}"
    say "WAYLAND_DISPLAY = ${WAYLAND_DISPLAY:-<vacio>}"
    say "cwd        = $PWD"
    say "params     = $PARAMS"
    say "logs       = $LOGDIR"
    say "binarios:"
    local b p
    for b in startstop csnloc pickS csnmags csnhypodbp csnrv csntvp csnstaevdisp \
             ew_controller hyp2000_ring nlloc_ring wstore_ingest hypodb_ingest \
             sniffring; do
        p="$(command -v "$b" 2>/dev/null || true)"
        printf '  %-14s %s\n' "$b" "${p:-<falta>}"
    done
    return 0
}

cmd_install() {
    local s="$ROOT/ew_gui_tools/common/tools/check_install.sh"
    [ -f "$s" ] || die "falta $s"
    need_tmp
    local rc=0
    run_capture "$TMP/install.out" 60 sh "$s" -v || rc=$?
    cap "$TMP/install.out"
    say "EXIT: $rc"
    return "$rc"
}

cmd_config() {  # config <modulo>
    local mod="${1:-}"
    [ -n "$mod" ] || { usage; exit "$EX_USAGE"; }
    case "$mod" in
        csnhypodbp|csnrv)   set -- "$mod" --print-config "$mod.d" ;;
        csnwphase)          set -- "$mod" "$mod.d" --print-config ;;
        *) deny "config solo esta definido para csnhypodbp, csnrv y csnwphase (pedido: $mod)";;
    esac
    [ -f "$PARAMS/$mod.d" ] || die "no existe $PARAMS/$mod.d"
    ew_src || die "sin entorno EarthWorm"
    local rc=0
    ( cd "$PARAMS" && "$TMO" 60 "$@" ) >"$TMP/config_$mod.out" 2>&1 || rc=$?
    cap "$TMP/config_$mod.out"
    say "EXIT: $rc"
    return "$rc"
}

cmd_profiles() {
    [ -f "$PARAMS/csnhypodbp.d" ] || die "no existe $PARAMS/csnhypodbp.d"
    ew_src || die "sin entorno EarthWorm"
    local rc=0
    ( cd "$PARAMS" && "$TMO" 60 csnhypodbp --check-profiles csnhypodbp.d ) >"$TMP/profiles.out" 2>&1 || rc=$?
    cap "$TMP/profiles.out"
    say "EXIT: $rc"
    return "$rc"
}

cmd_overlays() {  # overlays <modulo>
    local mod="${1:-}"
    [ -n "$mod" ] || { usage; exit "$EX_USAGE"; }
    case "$mod" in
        csnrv|csnstaevdisp) ;;
        *) deny "overlays solo esta definido para csnrv y csnstaevdisp (pedido: $mod)";;
    esac
    [ -f "$PARAMS/$mod.d" ] || die "no existe $PARAMS/$mod.d"
    ew_src || die "sin entorno EarthWorm"
    local rc=0
    ( cd "$PARAMS" && "$TMO" 60 "$mod" --check-overlays "$mod.d" ) >"$TMP/overlays_$mod.out" 2>&1 || rc=$?
    cap "$TMP/overlays_$mod.out"
    say "EXIT: $rc"
    return "$rc"
}

cmd_rings() {  # rings [anillo…]
    local r rings="" rc=0 bad=0
    if [ "$#" -gt 0 ]; then
        for r in "$@"; do
            valid_ring "$r" || deny "nombre de anillo invalido: $r (esperado: MAYUSCULAS_Y_GUION_BAJO)"
            rings="$rings $r"
        done
    else
        rings="$(ring_list | tr '\n' ' ')"
        [ -n "$(printf '%s' "$rings" | tr -d ' \t')" ] || rings="SLINK_RING PICK_RING HYPO_RING HYPO_RING_REF CONTROL_RING"
    fi
    ew_src || die "sin entorno EarthWorm"
    need_tmp
    for r in $rings; do
        say "--- $r ---"
        "$TMO" 5 sniffring -n "$r" >"$TMP/ring_$r.raw" 2>&1
        local trc=$?
        tr -d '\000' <"$TMP/ring_$r.raw" >"$TMP/ring_$r.out" 2>/dev/null || true
        cap "$TMP/ring_$r.out"
        # 124 = lo corto el timeout: es lo normal, `sniffring -n` no termina solo
        if [ "$trc" != 0 ] && [ "$trc" != 124 ]; then bad=1; fi
    done
    say "EXIT: $bad"
    return "$bad"
}

cmd_log() {  # log <modulo> [n]
    local mod="${1:-}" n="${2:-$MAXLINES}" f
    [ -n "$mod" ] || { usage; exit "$EX_USAGE"; }
    valid_name "$mod" || deny "nombre de modulo invalido: $mod"
    case "$n" in ""|*[!0-9]*) n="$MAXLINES";; esac
    [ "$n" -gt 200 ] && n=200
    f="$(ls -1t "$LOGDIR"/"${mod}"_*.log 2>/dev/null | head -n 1)"
    [ -n "$f" ] || die "no hay logs de '$mod' en $LOGDIR (wstore_ingest y csnwphase no usan logit_init: escriben en la terminal)"
    say "archivo: $(printf '%s' "$f" | sed "s|^$ROOT/||")"
    tail -n "$n" "$f"
    return 0
}

cmd_logs() {
    [ -d "$LOGDIR" ] || die "no existe $LOGDIR"
    ls -lt "$LOGDIR" | head -n $((MAXLINES + 1))
    say "total: $(ls -1 "$LOGDIR" | wc -l) entradas"
    return 0
}

cmd_errors() {  # errors [n]
    local n="${1:-3}" f hits found=0
    case "$n" in ""|*[!0-9]*) n=3;; esac
    [ -d "$LOGDIR" ] || die "no existe $LOGDIR"
    for f in $(ls -1t "$LOGDIR"/*.log 2>/dev/null | head -n 8); do
        hits="$(grep -iE 'error|fatal|abort|sigabrt|segfault|cannot open|no such file|failed|died' "$f" 2>/dev/null | head -n "$n")"
        [ -n "$hits" ] || continue
        found=1
        say "--- $(printf '%s' "$f" | sed "s|^$ROOT/||") ---"
        printf '%s\n' "$hits"
    done
    [ "$found" = 1 ] || say "(sin coincidencias en los 8 logs mas recientes)"
    return 0
}

cmd_process() {
    local names pids any=0 b
    names="$(module_list)"
    [ -n "$names" ] || names="startstop slink2ew pick_FP csnloc pickS csnmags csnhypodbp csnrv csntvp csnstaevdisp ew_controller hyp2000_ring nlloc_ring wstore_ingest hypodb_ingest"
    for b in $names; do
        pids="$(pgrep -x "$b" 2>/dev/null | tr '\n' ' ')"
        [ -n "$pids" ] || continue
        any=1
        ps -o pid=,stat=,etime=,args= -p $pids 2>/dev/null | sed 's/^\(.\{120\}\).*/\1…/'
    done
    [ "$any" = 1 ] || say "(no corre ninguno de los binarios del .d)"
    return 0
}

# --- nivel B -----------------------------------------------------------------

cmd_run() {  # run <modulo> [segundos] [--bg] [--force]
    local mod="" secs="$SECS_DEF" bg=0 force=0 a stamp out="" rc=0
    for a in "$@"; do
        case "$a" in
            --bg)    bg=1 ;;
            --force) force=1 ;;
            --*)     deny "opcion no permitida en run: $a" ;;
            *)
                if   [ -z "$mod" ];                 then mod="$a"
                elif [ "$secs" = "$SECS_DEF" ];     then secs="$a"
                else deny "argumento de mas en run: $a"
                fi
                ;;
        esac
    done
    [ -n "$mod" ] || { usage; exit "$EX_USAGE"; }
    valid_name "$mod" || deny "nombre de modulo invalido: $mod"
    case "$secs" in ""|*[!0-9]*) deny "segundos invalidos: $secs";; esac
    if [ "$secs" -lt 1 ] || [ "$secs" -gt "$SECS_MAX" ]; then
        deny "segundos fuera de rango (1..$SECS_MAX): $secs"
    fi
    [ -f "$PARAMS/$mod.d" ] || deny "'$mod' no esta declarado: falta $PARAMS/$mod.d"
    ew_src || die "sin entorno EarthWorm"
    command -v "$mod" >/dev/null 2>&1 || die "'$mod' no esta en PATH (proba: diag.sh install)"

    local pids
    pids="$(pgrep -x "$mod" 2>/dev/null | tr '\n' ' ')"
    if [ -n "$pids" ] && [ "$force" != 1 ]; then
        say "YA CORRE: $mod (pids: $pids)"
        say "No arranco una segunda instancia (duplicaria los anillos). Si de verdad lo queres: run --force $mod"
        return "$EX_PRECOND"
    fi

    need_tmp
    stamp="$(date -u +%Y%m%dT%H%M%SZ)"
    out="$TMP/run_${mod}_$stamp.log"
    say "CMD: (cd $(basename "$PARAMS") && $TMO $secs $mod $mod.d)"
    if [ "$bg" = 1 ]; then
        ( cd "$PARAMS" && exec "$TMO" "$secs" "$mod" "$mod.d" ) >"$out" 2>&1 &
        local pid=$!
        echo "$pid" >"$TMP/$mod.pid"
        sleep 1
        say "PID: $pid   segundos: $secs"
        say "log: $out"
        if kill -0 "$pid" 2>/dev/null; then
            say "estado: corriendo (lo corta el timeout)"
        else
            say "estado: YA TERMINO (mira el log)"
        fi
        return 0
    fi
    ( cd "$PARAMS" && "$TMO" "$secs" "$mod" "$mod.d" ) >"$out" 2>&1 || rc=$?
    if [ "$rc" = 124 ]; then
        say "EXIT: 124 (cortado por timeout a los ${secs}s: sigue vivo)"
    else
        say "EXIT: $rc"
    fi
    cap "$out"
    say "log: $out"
    return 0
}

cmd_attached() {  # attached <modulo>
    local mod="${1:-}" f pid
    [ -n "$mod" ] || { usage; exit "$EX_USAGE"; }
    valid_name "$mod" || deny "nombre de modulo invalido: $mod"
    f="$(ls -1t "$TMP"/run_${mod}_*.log 2>/dev/null | head -n 1)"
    [ -n "$f" ] || die "no hay salida de 'run --bg $mod' en $TMP"
    say "archivo: $f"
    tail -n "$MAXLINES" "$f"
    if [ -f "$TMP/$mod.pid" ]; then
        pid="$(cat "$TMP/$mod.pid" 2>/dev/null)"
        case "$pid" in ""|*[!0-9]*) say "estado: desconocido (pidfile corrupto)";;
            *) if [ -d "/proc/$pid" ]; then say "estado: corriendo (pid $pid)"
               else say "estado: terminado"; fi;;
        esac
    fi
    return 0
}

cmd_stop() {  # stop <modulo>
    local mod="${1:-}" pf pid
    [ -n "$mod" ] || { usage; exit "$EX_USAGE"; }
    valid_name "$mod" || deny "nombre de modulo invalido: $mod"
    pf="$TMP/$mod.pid"
    [ -f "$pf" ] || deny "'$mod' no lo lanzo diag.sh (no existe $pf): no mato procesos ajenos"
    pid="$(cat "$pf" 2>/dev/null)"
    case "$pid" in ""|*[!0-9]*) die "pidfile corrupto: $pf";; esac
    if [ ! -d "/proc/$pid" ]; then
        rm -f "$pf"
        say "ya no corria (pid $pid): limpiado el pidfile"
        return 0
    fi
    if ! tr '\0' ' ' <"/proc/$pid/cmdline" 2>/dev/null | grep -q -- "$mod"; then
        deny "el pid $pid no corresponde a '$mod': no lo mato (pidfile $pf)"
    fi
    kill -TERM "$pid" 2>/dev/null || die "no pude enviar TERM a $pid"
    rm -f "$pf"
    say "TERM enviado a $mod (pid $pid)"
    return 0
}

# --- despacho ----------------------------------------------------------------

main() {
    [ "$#" -ge 1 ] || { usage; exit "$EX_USAGE"; }
    local sub="$1"; shift
    case "$sub" in
        help|--help|-h)  usage; exit "$EX_OK" ;;
        env)             cmd_env ;;
        install)         cmd_install ;;
        config)          cmd_config "$@" ;;
        profiles)        cmd_profiles ;;
        overlays)        cmd_overlays "$@" ;;
        rings)           cmd_rings "$@" ;;
        log)             cmd_log "$@" ;;
        logs)            cmd_logs ;;
        errors)          cmd_errors "$@" ;;
        process)         cmd_process ;;
        run)             cmd_run "$@" ;;
        attached)        cmd_attached "$@" ;;
        stop)            cmd_stop "$@" ;;
        *) deny "subcomando no permitido: '$sub'. Es lista cerrada (diag.sh help). startstop, stopmodule, restart, ew_monitor.sh, make, git, sudo, rm y kill NO son diagnostico." ;;
    esac
}

main "$@"
