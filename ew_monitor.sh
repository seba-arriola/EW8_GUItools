#!/bin/bash
# =============================================================================
#  ew_monitor.sh - Control de EarthWorm 8 (startstop + modulos)
#  Proyecto: EW8_GUItools
#
#  SCRIPT_DIR resuelve a la carpeta que lo contiene, de modo que EW_UNIX/EW_LOG
#  apuntan a la instancia de este repo (no se mantiene una copia aparte).
#
#  Uso:
#     ./ew_monitor.sh status                  startstop + modulos vivos
#     ./ew_monitor.sh list                    modulos del .d de startstop + pid/estado
#     ./ew_monitor.sh start   [<modulo> ...]  sin argumentos: todo el stack
#     ./ew_monitor.sh stop    [<modulo> ...]  con argumentos: solo esos modulos
#     ./ew_monitor.sh restart [<modulo> ...]
#     ./ew_monitor.sh -h
#
#  <modulo> es el binario, el tallo de su .d (cuando binario y .d no se llaman
#  igual) o el pid. La lista de modulos NUNCA se escribe aqui: sale de la
#  configuracion de startstop (EW_MONITOR_CFG, por defecto
#  $EW_PARAMS/startstop_unix.d) y de la tabla viva del binario de fabrica
#  `status`. Con dos modulos homonimos (mismo binario, distinto .d) un nombre
#  ambiguo NO se resuelve "al primero": se avisa y no se envia nada.
#
#  Las acciones por modulo NO usan la consola de startstop: `fgets(stdin)` sale
#  con ESPIPE/ENOTTY cuando no hay tty (y aqui se lanza con </dev/null), asi que
#  la consola ya esta muerta. Se usan los binarios de fabrica `restart` y
#  `stopmodule`, que van POR PID por el anillo meta. Ninguno de los dos verifica
#  nada (salen 0 aunque no hagan nada): la comprobacion de que el modulo haya
#  vuelto (o muerto) la hace este script.
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
# Segundos que se le dan a un modulo para confirmar que volvio (`Alive` con pid
# nuevo) o que murio. Los binarios de fabrica no lo dicen, asi que se sondea.
EWMON_VERIFY_TIMEOUT="${EWMON_VERIFY_TIMEOUT:-15}"

# --- identidad del stack: DOS worktrees pueden tener un `startstop` cada uno ----
# El pid sale del pidfile (vive en NUESTRO $EW_LOG) validado por el entorno del
# proceso, y si no, de `pgrep -x startstop` FILTRADO por nuestro EW_PARAMS. Nunca
# "el primer startstop del sistema": `stop` mataba el stack del otro worktree.
EWMON_PIDFILE="${EWMON_PIDFILE:-$EW_LOG_DIR/startstop.pid}"
# Los tests de common/tests usan pids SINTETICOS (no hay /proc real que los
# valide): EWMON_PID_SCOPE=loose acepta sin evidencia. Default: strict.
EWMON_PID_SCOPE="${EWMON_PID_SCOPE:-strict}"

# 0 si <pid> puede ser de NUESTRA instancia: solo se RECHAZA lo que se puede
# probar que es de otra (legible y con otro EW_PARAMS, o sin el). Si /proc no es
# legible (pids sinteticos de los tests, procesos de otro usuario) no se puede
# probar nada y se acepta: en un sistema real, dos stacks del mismo usuario son
# legibles y se distinguen por EW_PARAMS, que es justo el caso que nos importa.
ewmon_pid_is_ours() {
    local pid="$1" want env
    [ "$EWMON_PID_SCOPE" = loose ] && return 0
    want="$(ewmon_params_dir)"
    [ -n "$want" ] || return 0
    env="/proc/$pid/environ"
    [ -r "$env" ] || return 0
    tr '\0' '\n' < "$env" 2>/dev/null | grep -qxF "EW_PARAMS=$want"
}

# Evidencia POSITIVA de que <pid> es NUESTRO startstop: el pidfile no puede
# bastar (puede quedar obsoleto y el pid reusarse por otro proceso).
ewmon_pid_is_startstop_ours() {
    local pid="$1" want comm
    [ "$EWMON_PID_SCOPE" = loose ] && return 0
    want="$(ewmon_params_dir)"
    [ -r "/proc/$pid" ] || return 1                       # el pid no existe
    comm="$(cat "/proc/$pid/comm" 2>/dev/null)"
    [ "$comm" = "startstop" ] || return 1                 # no es un startstop
    if [ -r "/proc/$pid/environ" ] && [ -n "$want" ]; then
        tr '\0' '\n' < "/proc/$pid/environ" 2>/dev/null | grep -qxF "EW_PARAMS=$want" || return 1
    fi
    return 0
}

get_startstop_pid() {
    local p ids="" n=0
    if [ -f "$EWMON_PIDFILE" ]; then
        p="$(cat "$EWMON_PIDFILE" 2>/dev/null)"
        if [ -n "$p" ] && ewmon_pid_is_startstop_ours "$p"; then
            printf '%s\n' "$p"
            return 0
        fi
        rm -f "$EWMON_PIDFILE" 2>/dev/null      # pidfile obsoleto
    fi
    for p in $(pgrep -x startstop 2>/dev/null); do
        if ewmon_pid_is_ours "$p"; then ids="$ids${ids:+ }$p"; n=$((n + 1)); fi
    done
    if [ "$n" -gt 1 ]; then
        echo "ERROR: hay $n startstop de ESTA instancia ($ids): no se cual es el nuestro" >&2
        return 1
    fi
    [ "$n" -eq 1 ] && printf '%s\n' "$ids"
    return 0
}

is_running() { [ -n "$(get_startstop_pid)" ]; }

child_pids() {
    local spid; spid="$(get_startstop_pid)"
    [ -n "$spid" ] && pgrep -P "$spid" 2>/dev/null
}

# =============================================================================
#  Modulos: la lista sale de la configuracion de startstop, nunca literal
# =============================================================================

# Ejecuta "$@" con el entorno de EarthWorm cargado (cd a la raiz del repo y
# `source ew8_unix.sh`). Preserva EW_PARAMS/EW_LOG del llamante: ew8_unix.sh los
# sobreescribe SIN condicion (ew8_unix.sh:12-15) y eso romperia a quien corra
# sobre un directorio de params aislado (modo replay).
ewmon_env_run() {
    local keep_params="${EW_PARAMS:-}" keep_log="${EW_LOG:-}"
    (
        cd "$SCRIPT_DIR" || exit 1
        if [ -f "$EW_UNIX" ]; then
            # shellcheck disable=SC1090
            source "$EW_UNIX" >/dev/null 2>&1
        fi
        [ -n "$keep_params" ] && export EW_PARAMS="$keep_params"
        [ -n "$keep_log" ] && export EW_LOG="$keep_log"
        "$@"
    )
}

# Invoca un binario de fabrica. `command` es obligatorio: este mismo script
# define funciones `status` y `restart` que ensombrecen a los binarios homonimos.
ewmon_bin() {
    local tool="$1"; shift
    ewmon_env_run bash -c 'command "$@"' _ "$tool" "$@"
}

ewmon_params_dir() {
    if [ -n "${EW_PARAMS:-}" ]; then printf '%s\n' "$EW_PARAMS"; return 0; fi
    ewmon_env_run bash -c 'printf "%s\n" "${EW_PARAMS:-}"'
}

# Ruta del .d de startstop en uso (EW_MONITOR_CFG la sobreescribe).
ewmon_cfg() {
    if [ -n "${EW_MONITOR_CFG:-}" ]; then printf '%s\n' "$EW_MONITOR_CFG"; return 0; fi
    local d; d="$(ewmon_params_dir)"
    [ -n "$d" ] || return 3
    printf '%s/startstop_unix.d\n' "$d"
}

# Modulos declarados en el .d: `Process ["]<binario> <cfgfile>["]`; las lineas
# comentadas se ignoran. stdout TSV: binario <TAB> cfgfile
ewmon_cfg_modules() {
    local cfg line rest bin
    cfg="$(ewmon_cfg)" || return 3
    [ -r "$cfg" ] || return 3
    while IFS= read -r line; do
        line="${line%"${line##*[![:space:]]}"}"      # rstrip (incluye el CR)
        line="${line#"${line%%[![:space:]]*}"}"      # lstrip
        case "$line" in ''|\#*) continue ;; esac
        case "$line" in Process[[:space:]]*) ;; *) continue ;; esac
        rest="${line#Process}"
        rest="${rest#"${rest%%[![:space:]]*}"}"
        case "$rest" in \"*) rest="${rest#\"}"; rest="${rest%%\"*}" ;; esac
        rest="${rest#"${rest%%[![:space:]]*}"}"
        bin="${rest%%[[:space:]]*}"
        [ -n "$bin" ] || continue
        rest="${rest#"$bin"}"
        rest="${rest#"${rest%%[![:space:]]*}"}"
        rest="${rest%%[[:space:]]*}"
        printf '%s\t%s\n' "$bin" "$rest"
    done < "$cfg"
}

# Nombre corto de un modulo: el tallo del .d (asi se distinguen los homonimos)
# o el binario si el .d no declara uno.
ewmon_mod_name() {
    local bin="$1" cfg="$2"
    if [ -n "$cfg" ]; then printf '%s\n' "${cfg%.d}"; else printf '%s\n' "$bin"; fi
}

# Fila de la tabla de `status`. OJO: ANTES de la tabla hay lineas cuya 2.a
# columna tambien es un numero (los anillos: "Ring  1 name/key/size:  ..."), asi
# que solo se mira lo que viene DESPUES del separador de la tabla. Se descartan
# los pids <= 0 (filas sin proceso, igual que ewgui_ctrl_parse_status).
# stdout TSV: nombre <TAB> pid <TAB> estado <TAB> resto
ewmon_parse_status() {
    local line t1 t2 t3 rest started=0
    while IFS= read -r line; do
        line="${line%$'\r'}"
        read -r t1 t2 t3 rest <<< "$line"
        if [ "$started" = 0 ]; then
            [ "${t1:-}" = '-------' ] && started=1
            continue
        fi
        case "${t2:-}" in ''|*[!0-9]*) continue ;; esac
        [ "$t2" -gt 0 ] || continue
        printf '%s\t%s\t%s\t%s\n' "$t1" "$t2" "$t3" "${rest:-}"
    done
}

# Modulos vivos segun startstop. Descarta la fila del propio startstop: el
# estado la imprime aparte (el padre, no la tabla de hijos) y jamas debe ser
# objetivo de stop/restart.
ewmon_live_status() {
    local cfg spid n p s a
    cfg="$(ewmon_cfg)" || return 3
    is_running || return 3
    spid="$(get_startstop_pid)"
    ewmon_bin status "$cfg" 2>/dev/null | ewmon_parse_status | \
    while IFS=$'\t' read -r n p s a; do
        [ "$p" = "$spid" ] && continue
        [ "$n" = startstop ] && continue
        printf '%s\t%s\t%s\t%s\n' "$n" "$p" "$s" "$a"
    done
}

# binario y .d de un pid, leidos del kernel: la columna Argument de `status`
# puede venir truncada y entonces dos modulos homonimos son indistinguibles.
ewmon_proc_module() {
    local pid="$1" args
    [ -r "/proc/$pid/cmdline" ] || return 1
    args="$(tr '\0' '\n' < "/proc/$pid/cmdline" 2>/dev/null)" || return 1
    printf '%s\t%s\n' "${args%%$'\n'*}" "$(printf '%s\n' "$args" | sed -n '2p')"
}

# Tabla viva con el .d resuelto. TSV: nombre <TAB> pid <TAB> estado <TAB> cfg
# startstop cosecha a sus hijos de forma perezosa: la 1.a consulta tras una muerte
# reapea al hijo y lo llama `Zombie`; la 2.a ya dice `Dead`. Si alguna fila queda
# en Zombie se repite UNA vez (maximo 2 consultas), para que un modulo que esta
# muerto se reporte `Dead` y no un `Zombie` que startstop todavia no ha enterrado.
ewmon_live_table() {
    local n p s a pm raw retry=0
    raw="$(ewmon_live_status 2>/dev/null)"
    while IFS=$'\t' read -r n p s a; do
        [ "$s" = Zombie ] && { retry=1; break; }
    done <<< "$raw"
    [ "$retry" = 1 ] && raw="$(ewmon_live_status 2>/dev/null)"
    while IFS=$'\t' read -r n p s a; do
        [ -n "$n" ] || continue
        pm="$(ewmon_proc_module "$p" 2>/dev/null | cut -f2)"
        # Sin /proc (p. ej. el pid ya murio) queda la fila de `status`: el
        # argumento es su ULTIMA columna, porque antes vienen la clase y el CPU
        # ("??/ 0  00:00:01  <modulo>.d").
        [ -n "$pm" ] || pm="${a##* }"
        printf '%s\t%s\t%s\t%s\n' "$n" "$p" "$s" "${pm##*/}"
    done <<< "$raw"
}

# Filas de modulos: los declarados en el .d (en su orden) con el pid/estado de la
# tabla viva, mas los vivos que el .d no declare (config distinta a la del proceso
# vivo). Lo comparten `list` y `status`: es el unico sitio donde se decide el
# estado de cada modulo. stdout TSV: modulo <TAB> binario <TAB> cfg <TAB> pid|-
# <TAB> estado. Devuelve 3 si no hay .d legible (entonces solo salen los vivos).
#
# OJO con el TSV: NINGUN campo va vacio (un modulo sin .d sale con cfg `-`). Para
# `read`, el tabulador cuenta como ESPACIO aunque se ponga en IFS, asi que los
# campos vacios EN MEDIO se colapsan y las columnas se desplazan (medido en bash
# 5.2.21). Los consumidores imprimen `${cfg:--}`, asi que el centinela no cambia
# ninguna salida.
ewmon_module_rows() {
    local cfg bin mcfg pid st i total n p s c rrc=0
    local -a ln=() lp=() ls=() lc=() seen=()

    cfg="$(ewmon_cfg 2>/dev/null)" || cfg=""
    [ -n "$cfg" ] && [ -r "$cfg" ] || rrc=3

    while IFS=$'\t' read -r n p s c; do
        [ -n "$n" ] || continue
        ln+=("$n"); lp+=("$p"); ls+=("$s"); lc+=("$c")
    done < <(ewmon_live_table)

    if [ "$rrc" = 0 ]; then
        while IFS=$'\t' read -r bin mcfg; do
            [ -n "$bin" ] || continue
            pid="-"; st="ausente"
            i=0; total="${#ln[@]}"
            while [ "$i" -lt "$total" ]; do
                if [ "${ln[$i]}" = "$bin" ] && \
                   { [ -z "$mcfg" ] || [ -z "${lc[$i]}" ] || [ "${lc[$i]}" = "$mcfg" ]; }; then
                    pid="${lp[$i]}"; st="${ls[$i]}"; seen[$i]=1
                    break
                fi
                i=$((i + 1))
            done
            printf '%s\t%s\t%s\t%s\t%s\n' \
                "$(ewmon_mod_name "$bin" "$mcfg")" "$bin" "${mcfg:--}" "$pid" "$st"
        done < <(ewmon_cfg_modules)
    fi

    i=0; total="${#ln[@]}"
    while [ "$i" -lt "$total" ]; do
        if [ "${seen[$i]:-}" != 1 ]; then
            printf '%s\t%s\t%s\t%s\t%s\n' \
                "$(ewmon_mod_name "${ln[$i]}" "${lc[$i]}")" "${ln[$i]}" \
                "${lc[$i]:--}" "${lp[$i]}" "${ls[$i]}"
        fi
        i=$((i + 1))
    done
    return $rrc
}

# Subcomando `list`: una fila por modulo, con su estado, en tabla.
ewmon_list() {
    local cfg mod bin mcfg pid st
    cfg="$(ewmon_cfg)" || { echo "ERROR: no se pudo determinar la configuracion de startstop." >&2; return 3; }
    if [ ! -r "$cfg" ]; then
        echo "ERROR: no existe o no se puede leer $cfg" >&2
        return 3
    fi
    is_running || echo "NOTA: startstop no esta corriendo; la lista sale solo del .d." >&2
    printf '%-16s %-16s %-20s %-8s %s\n' MODULO BINARIO CFG PID ESTADO
    while IFS=$'\t' read -r mod bin mcfg pid st; do
        [ -n "$bin" ] || continue
        printf '%-16s %-16s %-20s %-8s %s\n' "$mod" "$bin" "${mcfg:--}" "$pid" "$st"
    done < <(ewmon_module_rows)
    return 0
}

# Bloque `modulos` de `status`: resumen por estado y una linea por modulo, con el
# estado junto al nombre (el pid es el de `restart <pid>`; `-` si no lo hay).
ewmon_status_modules() {
    local i total j bin mcfg pid stt pid_txt summary="" rows rrc
    local -a ab=() af=() ap=() ae=()
    local -A counts=()

    rows="$(ewmon_module_rows)"; rrc=$?
    [ "$rrc" = 0 ] || echo "  modulos   : OJO: no se pudo leer la configuracion de startstop" >&2

    while IFS=$'\t' read -r _ bin mcfg pid stt; do
        [ -n "$bin" ] || continue
        ab+=("$bin"); af+=("$mcfg"); ap+=("$pid"); ae+=("$stt")
        counts[$stt]=$(( ${counts[$stt]:-0} + 1 ))
    done <<< "$rows"

    total="${#ab[@]}"
    if [ "${#counts[@]}" -gt 0 ]; then
        for stt in $(printf '%s\n' "${!counts[@]}" | LC_ALL=C sort); do
            summary="$summary${summary:+, }$stt:${counts[$stt]}"
        done
    fi
    printf '  modulos   : %s declarados%s\n' "$total" "${summary:+ ($summary)}"

    i=0
    while [ "$i" -lt "$total" ]; do
        if [ "${ap[$i]}" = - ]; then pid_txt="-"; else pid_txt="pid ${ap[$i]}"; fi
        printf '    - %-14s %-18s %-8s %s\n' \
            "${ab[$i]}" "${af[$i]:--}" "${ae[$i]}" "$pid_txt"
        i=$((i + 1))
    done
    return 0
}

# Resuelve un selector a un modulo VIVO: binario, tallo del .d o pid. Con 0 o
# >1 candidatos el detalle va por stderr y sale 2: nunca se elige "el primero"
# a ciegas (con dos modulos homonimos eso reiniciaria siempre el mismo).
# stdout TSV: modulo <TAB> pid <TAB> binario <TAB> cfg
ewmon_resolve() {
    local sel="$1" i total hit=-1 hits=0 n p s c
    local -a ln=() lp=() ls=() lc=()

    while IFS=$'\t' read -r n p s c; do
        [ -n "$n" ] || continue
        ln+=("$n"); lp+=("$p"); ls+=("$s"); lc+=("$c")
    done < <(ewmon_live_table)

    case "$sel" in
        ''|*[!0-9]*)
            i=0; total="${#ln[@]}"
            while [ "$i" -lt "$total" ]; do
                if [ "${ln[$i]}" = "$sel" ] || [ "${lc[$i]}" = "$sel" ] || [ "${lc[$i]}" = "$sel.d" ]; then
                    hits=$((hits + 1)); hit=$i
                fi
                i=$((i + 1))
            done
            ;;
        *)
            i=0; total="${#ln[@]}"
            while [ "$i" -lt "$total" ]; do
                if [ "${lp[$i]}" = "$sel" ]; then hits=$((hits + 1)); hit=$i; fi
                i=$((i + 1))
            done
            ;;
    esac

    if [ "$hits" -eq 0 ]; then
        echo "ERROR: '$sel' no es un modulo vivo de startstop." >&2
        if ewmon_cfg_modules 2>/dev/null | cut -f1 | grep -qxF -- "$sel"; then
            echo "       el .d lo declara, pero startstop no le da pid (¿no existe el binario?)." >&2
        else
            echo "       usa '$0 list' para ver los modulos disponibles." >&2
        fi
        return 2
    fi
    if [ "$hits" -gt 1 ]; then
        echo "ERROR: '$sel' es ambiguo: $hits modulos lo cumplen. Usa el pid o el tallo del .d:" >&2
        i=0; total="${#ln[@]}"
        while [ "$i" -lt "$total" ]; do
            if [ "${ln[$i]}" = "$sel" ] || [ "${lc[$i]}" = "$sel" ] || [ "${lc[$i]}" = "$sel.d" ]; then
                printf '         %-16s pid %-8s %-20s %s\n' \
                    "${ln[$i]}" "${lp[$i]}" "${lc[$i]:--}" "${ls[$i]}" >&2
            fi
            i=$((i + 1))
        done
        return 2
    fi
    printf '%s\t%s\t%s\t%s\n' \
        "$(ewmon_mod_name "${ln[$hit]}" "${lc[$hit]}")" "${lp[$hit]}" "${ln[$hit]}" "${lc[$hit]}"
}

ewmon_bin_exists() { ewmon_env_run bash -c 'command -v -- "$1" >/dev/null 2>&1' _ "$1"; }

ewmon_hint() {
    echo "        consola de startstop: $STARTSTOP_CONSOLE" >&2
    echo "        log del modulo:       ${EW_LOG_DIR}/${1}_*.log" >&2
}

# Espera a que el modulo (binario + .d) este Alive con un pid DISTINTO del viejo
# (todo relanzamiento cambia el pid). stdout: el pid nuevo.
ewmon_wait_alive() {
    local want_bin="$1" want_cfg="$2" old="$3" n="${EWMON_VERIFY_TIMEOUT}" i=0 cur p s c
    while :; do
        while IFS=$'\t' read -r cur p s c; do
            [ "$cur" = "$want_bin" ] || continue
            if [ -n "$want_cfg" ] && [ -n "$c" ] && [ "$c" != "$want_cfg" ]; then continue; fi
            if [ "$s" = Alive ] && [ "$p" != "$old" ]; then printf '%s\n' "$p"; return 0; fi
        done < <(ewmon_live_table 2>/dev/null)
        i=$((i + 1))
        [ "$i" -ge "$n" ] && return 1
        sleep 1
    done
}

# Espera a que el pid deje de estar vivo (Dead/Stop/Zombie o fuera de la tabla).
ewmon_wait_dead() {
    local want_pid="$1" n="${EWMON_VERIFY_TIMEOUT}" i=0 cur p s c any
    while :; do
        any=0
        while IFS=$'\t' read -r cur p s c; do
            [ "$p" = "$want_pid" ] || continue
            any=1
            case "$s" in Dead|Stop|Zombie) return 0 ;; esac
        done < <(ewmon_live_table 2>/dev/null)
        [ "$any" = 0 ] && return 0
        i=$((i + 1))
        [ "$i" -ge "$n" ] && return 1
        sleep 1
    done
}

# Accion sobre uno o mas modulos. Los binarios de fabrica no verifican nada (salen
# 0 aunque no hagan nada), asi que la confirmacion es nuestra: sin ella, 4.
ewmon_action() {
    local action="$1"; shift
    local dry=0 force=0 sel res rc=0 i n p b c cfg spid newp
    local -a sels=() names=() pids=() bins=() cfgs=()

    while [ "$#" -gt 0 ]; do
        case "$1" in
            --dry-run) dry=1 ;;
            --force)   force=1 ;;
            -h|--help) ewmon_usage; return 0 ;;
            -*) echo "ERROR: opcion desconocida: $1" >&2; ewmon_usage >&2; return 2 ;;
            *) sels+=("$1") ;;
        esac
        shift
    done
    if [ "${#sels[@]}" -eq 0 ]; then
        echo "ERROR: falta el modulo (binario, tallo del .d o pid)." >&2
        return 2
    fi
    if ! is_running; then
        echo "ERROR: EarthWorm no esta corriendo (no hay startstop que reinicie nada)." >&2
        return 3
    fi

    # Primero se resuelven TODOS: si alguno falla no se manda nada a medias.
    for sel in "${sels[@]}"; do
        if ! res="$(ewmon_resolve "$sel")"; then
            return 2
        fi
        read -r n p b c <<< "$res"
        names+=("$n"); pids+=("$p"); bins+=("$b"); cfgs+=("$c")
    done

    cfg="$(ewmon_cfg)" || { echo "ERROR: no se pudo determinar la configuracion de startstop." >&2; return 3; }
    spid="$(get_startstop_pid)"

    i=0
    while [ "$i" -lt "${#sels[@]}" ]; do
        n="${names[$i]}"; p="${pids[$i]}"; b="${bins[$i]}"; c="${cfgs[$i]}"
        if [ "$p" = "$spid" ]; then
            echo "ERROR: '$n' es el propio startstop; no se toca." >&2
            rc=2; i=$((i + 1)); continue
        fi
        case "$action" in
            stop)
                if [ "$dry" = 1 ]; then
                    echo "DRY-RUN: stopmodule $cfg $p   # $n ($b ${c:--})"
                    i=$((i + 1)); continue
                fi
                echo "Parando $n (pid $p) ..."
                ewmon_bin stopmodule "$cfg" "$p" >/dev/null
                if ewmon_wait_dead "$p"; then
                    echo "  $n parado (startstop lo da por detenido)."
                else
                    echo "  AVISO: $n no confirmo su parada en ${EWMON_VERIFY_TIMEOUT}s." >&2
                    ewmon_hint "$b"; rc=4
                fi
                ;;
            start|restart)
                if [ "$force" != 1 ] && ! ewmon_bin_exists "$b"; then
                    echo "ERROR: no encuentro el binario '$b' en el PATH de EarthWorm." >&2
                    echo "       relanzarlo lo dejaria en NoExec; compila/instala y repite (o --force)." >&2
                    rc=4; i=$((i + 1)); continue
                fi
                if [ "$dry" = 1 ]; then
                    echo "DRY-RUN: restart -c $cfg $p   # $n ($b ${c:--})"
                    i=$((i + 1)); continue
                fi
                echo "Reiniciando $n (pid $p) ..."
                ewmon_bin restart -c "$cfg" "$p" >/dev/null
                newp=""
                if newp="$(ewmon_wait_alive "$b" "$c" "$p")"; then
                    echo "  $n: pid $p -> $newp (Alive)."
                else
                    echo "  AVISO: $n no volvio a Alive con pid nuevo en ${EWMON_VERIFY_TIMEOUT}s." >&2
                    ewmon_hint "$b"; rc=4
                fi
                ;;
        esac
        i=$((i + 1))
    done
    return $rc
}

ewmon_usage() {
    cat <<EOF
Uso: $0 {status|list|start|stop|restart} [<modulo> ...]

  status                  startstop y los modulos vivos (dinamico)
  list                    modulos del .d de startstop, con pid y estado
  start   [<modulo> ...]  sin argumentos: todo el stack
  stop    [<modulo> ...]  con argumentos: solo esos modulos
  restart [<modulo> ...]
  -h, --help              esta ayuda

<modulo> = binario | tallo del .d | pid.  Con un nombre ambiguo (dos modulos
homonimos) se avisa y NO se envia nada.

Opciones:  --dry-run   enseña que se enviaria, sin enviarlo
           --force     salta la guarda de "binario ausente" (dejaria el modulo
                       en NoExec, que es justo lo que la guarda evita)

Configuracion: EW_MONITOR_CFG=<ruta al .d de startstop>  (default \$EW_PARAMS/startstop_unix.d)
               EWMON_VERIFY_TIMEOUT=<segundos de espera> (default $EWMON_VERIFY_TIMEOUT)
EOF
}

status() {
    local pid params
    pid="$(get_startstop_pid)"
    if [ -n "$pid" ]; then
        echo "EarthWorm: RUNNING (startstop pid $pid)"
    else
        echo "EarthWorm: STOPPED (no hay startstop)"
    fi
    # La ruta de params se RESUELVE (ewmon_params_dir): imprimir la variable a
    # secas daba "<no definido>" siempre que el shell que invoca no tuviera el
    # entorno cargado, aunque el script ya supiera de que instancia habla.
    params="$(ewmon_params_dir 2>/dev/null)"
    echo "  EW_PARAMS : ${params:-<no definido>}"
    echo "  log dir   : $EW_LOG_DIR"
    # Un modulo muerto ya no es hijo de startstop (queda reparentado al ser
    # cosechado), asi que el `pgrep -P`/`ps` de antes lo perdia en silencio: el
    # estado sale ahora de la tabla del binario de fabrica `status`, que conserva
    # una fila por modulo declarado con su estado real.
    ewmon_status_modules
    [ -n "$pid" ] || return 1
    return 0
}

start() {
    if is_running; then
        echo "EarthWorm ya esta corriendo (startstop pid $(get_startstop_pid))."
        return 0
    fi

    [ -f "$EW_UNIX" ] || { echo "ERROR: no existe $EW_UNIX"; return 1; }
    mkdir -p "$EW_LOG_DIR"
    # La consola se AÑADE (>>), no se trunca: truncarla borraba el rastro del
    # arranque anterior (p. ej. el SIGABRT de un modulo) justo cuando hacía falta
    # mirarlo. Cada arranque queda delimitado por su cabecera.
    printf '\n===== %s arranque de startstop =====\n' \
        "$(date -u '+%Y-%m-%dT%H:%M:%SZ')" >> "$STARTSTOP_CONSOLE"

    # Cargar el entorno y lanzar startstop de forma independiente (setsid)
    # shellcheck disable=SC1090
    ( cd "$SCRIPT_DIR" && source "$EW_UNIX" >/dev/null 2>&1 && \
      setsid startstop </dev/null >>"$STARTSTOP_CONSOLE" 2>&1 & disown )

    echo "Iniciando EarthWorm ..."
    local i pid=""
    for i in $(seq 1 20); do
        sleep 1
        pid="$(get_startstop_pid)"
        [ -n "$pid" ] && break
    done

    if [ -n "$pid" ]; then
        printf '%s\n' "$pid" > "$EWMON_PIDFILE" 2>/dev/null
        echo "EarthWorm iniciado (startstop pid $pid)."
        echo "  consola: $STARTSTOP_CONSOLE"
        echo "  pidfile: $EWMON_PIDFILE"
        return 0
    else
        echo "ERROR: startstop no arranco. Revise $STARTSTOP_CONSOLE"
        return 1
    fi
}

# pids de modulos que sobreviven al stack: los hijos de startstop capturados
# ANTES de matarlo (despues quedan reparentados y ya no se enumeran) mas los
# binarios que declara el .d, buscados por nombre EXACTO de proceso. Antes se
# buscaban por linea de comandos con una lista literal: esa lista omitia modulos
# de la configuracion y ademas mataba a cualquier proceso -el script que llama
# incluido- cuya linea de comandos mencionara esos nombres.
ewmon_leftovers() {
    local pre="$1" names alt b out q
    out="$pre"
    if names="$(ewmon_cfg_modules 2>/dev/null)"; then
        alt=""
        while IFS=$'\t' read -r b _; do
            [ -n "$b" ] || continue
            case "$b" in *[!A-Za-z0-9_.+-]*) continue ;; esac     # apto para el ERE
            case "|$alt|" in *"|$b|"*) continue ;; esac           # sin repetidos
            alt="$alt${alt:+|}$b"
        done <<< "$names"
        if [ -n "$alt" ]; then
            # Solo los que son de NUESTRA instancia (el nombre se repite entre
            # worktrees: matarlos por nombre se llevaba por delante el otro stack).
            for q in $(pgrep -x "$alt" 2>/dev/null); do
                ewmon_pid_is_ours "$q" || continue
                out="$out
$q"
            done
        fi
    fi
    printf '%s\n' "$out" | tr -s '[:space:]' '\n' | grep -v '^$' | sort -u
}

stop() {
    if ! is_running; then
        echo "EarthWorm ya esta detenido."
        return 0
    fi

    local pid i; pid="$(get_startstop_pid)"
    echo "Deteniendo EarthWorm (startstop pid $pid) ..."
    # Los hijos se enumeran AHORA, mientras startstop vive y cuelgan de el: si
    # startstop muere quedan reparentados y no hay forma de encontrarlos.
    local pre_children; pre_children="$(child_pids)"
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

    # Limpiar cualquier modulo que siga vivo (ninguna lista literal aqui)
    local leftovers
    leftovers="$(ewmon_leftovers "$pre_children")"
    if [ -n "$leftovers" ]; then
        echo "  matando modulos remanentes: $(echo $leftovers | tr '\n' ' ')"
        kill -KILL $leftovers 2>/dev/null
        sleep 1
    fi

    is_running || rm -f "$EWMON_PIDFILE" 2>/dev/null
    is_running && { echo "ERROR: no se pudo detener EarthWorm."; return 1; } || { echo "EarthWorm detenido."; return 0; }
}

restart() { stop && sleep 2 && start; }

case "${1:-}" in
    status)  status ;;
    list)    ewmon_list ;;
    start)   shift; if [ "$#" -eq 0 ]; then start;   else ewmon_action start   "$@"; fi ;;
    stop)    shift; if [ "$#" -eq 0 ]; then stop;    else ewmon_action stop    "$@"; fi ;;
    restart) shift; if [ "$#" -eq 0 ]; then restart; else ewmon_action restart "$@"; fi ;;
    -h|--help) ewmon_usage ;;
    *) ewmon_usage >&2; exit 2 ;;
esac
