#!/bin/bash
# =============================================================================
#  ew8_db.sh - base hipocentral de EW8 (PostgreSQL 16 en docker)
#  Proyecto: EW8_GUItools
#
#  POR QUE EXISTE
#  --------------
#  No trae el motor de docker: si lo tienes instalado,
#  esto deja la BD hipocentral lista para que arranque EarthWorm:
#
#        source ./ew8_unix.sh
#        ./ew8_db.sh up
#        ./ew_monitor.sh start
#
#  Necesita run_working_v8/params/db.env: lo genera el deploy con --with-new-db
#  (DB_HOST=127.0.0.1 / DB_PORT=5435, los mismos valores que db.env.example).
#  `db/docker-compose.yml` monta `db/schema.sql` en /docker-entrypoint-initdb.d,
#  asi que el primer `up` crea la base CON esquema y sin pasos manuales.
#
#  INSTANCIAS (worktrees)
#  ----------------------
#  Con `-i N` (o EWGUI_INSTANCE=N) cada worktree usa SU base: proyecto y
#  contenedor ew8_hypodb_<N>, volumen propio y puerto 5435+N. `db.env` debe
#  declarar ese mismo DB_PORT para LEVANTAR o CONECTAR la base (`up`/`psql`/
#  `reset` abortan si no coincide); mirar o borrar la de OTRA instancia no lo
#  exige, y asi se limpia la base de un worktree ya borrado.
#  La instancia 0 (por defecto) es la de siempre: ew8_hypodb en 5435.
#  Las instancias >=1 no se auto-arrancan al bootear (restart: no).
#
#  USO
#  ---
#     ./ew8_db.sh [-i N] up            levanta la base (espera al healthcheck)
#     ./ew8_db.sh [-i N] status        estado del contenedor + healthcheck + tablas
#     ./ew8_db.sh [-i N] down          detiene el contenedor (CONSERVA los datos)
#     ./ew8_db.sh [-i N] reset         borra el volumen y vuelve a crear la base
#     ./ew8_db.sh [-i N] destroy [--yes]  BORRA contenedor+volumen+red (sin recrear)
#     ./ew8_db.sh instances            lista las instancias que existen (huerfanas incl.)
#     ./ew8_db.sh [-i N] logs          logs del contenedor (Ctrl-C para salir)
#     ./ew8_db.sh [-i N] psql [args]   consola psql (por defecto: -U $DB_USER -d $DB_NAME)
#     ./ew8_db.sh deps                 comprueba docker/compose y dice que falta
#     ./ew8_db.sh -h|--help
#
#  CODIGOS DE SALIDA
#     0 ok | 1 error de uso o docker ausente | 2 la base no esta sana
# =============================================================================

set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
COMPOSE="$ROOT/db/docker-compose.yml"
ENVFILE="$ROOT/run_working_v8/params/db.env"
# Binario de docker. EWGUI_DOCKER permite inyectar uno de prueba (los tests
# corren asi, sin tocar ningun contenedor real).
DOCKER="${EWGUI_DOCKER:-docker}"

die() { local code=$1; shift; echo "ERROR: $*" >&2; exit "$code"; }

usage() {
    sed -n '2,/^# =====/p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
    exit "${1:-0}"
}

compose() { "$DOCKER" compose -f "$COMPOSE" "$@"; }

need_docker() {
    command -v "$DOCKER" >/dev/null 2>&1 \
        || die 1 "docker no esta instalado. Instalalo en el destino (ver '$(basename "$0") deps')"
    "$DOCKER" compose version >/dev/null 2>&1 \
        || die 1 "docker no tiene el plugin 'compose' v2. Ver '$(basename "$0") deps'"
    [ -f "$COMPOSE" ] || die 1 "no existe $COMPOSE (despliega con db/)"
}

# Credenciales del .env (solo para mensajes y para psql).
db_var() { sed -n "s/^$1=//p" "$ENVFILE" 2>/dev/null | tail -1; }

need_env() {
    [ -f "$ENVFILE" ] || die 1 "no existe ${ENVFILE#$ROOT/}: despliega con --with-new-db"
}

cmd_deps() {
    local ok=1
    if command -v "$DOCKER" >/dev/null 2>&1; then
        echo "ok  : docker $("$DOCKER" --version 2>/dev/null | awk '{print $3}' | tr -d ,)"
    else
        echo "FALTA: docker"
        ok=0
    fi
    if "$DOCKER" compose version >/dev/null 2>&1; then
        echo "ok  : $("$DOCKER" compose version 2>/dev/null | head -1)"
    else
        echo "FALTA: docker compose (plugin v2)"
        ok=0
    fi
    if command -v rsync >/dev/null 2>&1; then
        echo "ok  : rsync"
    else
        echo "FALTA: rsync (lo necesita el deploy, no la BD)"
    fi
    if [ "$ok" -eq 0 ]; then
        cat <<'EOF'

Instalacion (Ubuntu 24.04, necesita sudo):
  # repo oficial de Docker
  sudo apt-get install -y ca-certificates curl
  sudo install -m 0755 -d /etc/apt/keyrings
  sudo curl -fsSL https://download.docker.com/linux/ubuntu/gpg -o /etc/apt/keyrings/docker.asc
  sudo chmod a+r /etc/apt/keyrings/docker.asc
  sudo tee /etc/apt/sources.list.d/docker.sources >/dev/null <<'SRC'
  Types: deb
  URIs: https://download.docker.com/linux/ubuntu
  Suites: noble
  Components: stable
  Signed-By: /etc/apt/keyrings/docker.asc
  SRC
  sudo apt-get update
  sudo apt-get install -y docker-ce docker-ce-cli containerd.io \
                          docker-buildx-plugin docker-compose-plugin
  sudo systemctl enable --now docker
  sudo groupadd -f docker && sudo usermod -aG docker "$USER"   # luego RE-LOGIN

  # o la via corta de Ubuntu:
  #   sudo apt-get install -y docker.io docker-compose-v2
EOF
        return 1
    fi
    return 0
}

cmd_up() {
    need_docker
    need_env
    echo "[ew8_db] levantando la BD ($(db_var DB_HOST):$(db_var DB_PORT)/$(db_var DB_NAME))"
    compose up -d --wait || die 1 "fallo 'docker compose up' (revisa: $(basename "$0") logs)"
    cmd_status
}

cmd_status() {
    need_docker
    local st
    compose ps
    st="$(compose ps --format '{{.Health}}' 2>/dev/null | head -1)"
    if [ -n "${st:-}" ] && [ "$st" != "healthy" ]; then
        echo "[ew8_db] healthcheck: $st"
    fi
    if [ -f "$ENVFILE" ] && compose exec -T db pg_isready -q 2>/dev/null; then
        compose exec -T db psql -U "$(db_var DB_USER)" -d "$(db_var DB_NAME)" \
            -Atc "select 'tablas=' || count(*) from information_schema.tables
                  where table_schema='public'" 2>/dev/null || true
        return 0
    fi
    echo "[ew8_db] la base no responde (usa '$(basename "$0") up' o revisa los logs)"
    return 2
}

cmd_down()   { need_docker; compose down; }
cmd_logs()   { need_docker; compose logs -f db; }

cmd_reset() {
    need_docker
    read -r -p "[ew8_db] esto BORRA el volumen (todos los eventos) y reaplica el esquema. ¿Seguir? [s/N] " r
    case "${r:-n}" in
        s|S|y|Y) ;;
        *) echo "[ew8_db] cancelado"; return 0 ;;
    esac
    compose down -v || die 1 "fallo 'docker compose down -v'"
    cmd_up
}

# --- Instancias de otros worktrees -------------------------------------------
# `instances` y `destroy` cubren el hueco que dejaba una base huerfana: el
# worktree se borra con `git worktree remove` y su contenedor/volumen se quedan
# ahi, sin alcanzarlos (el db.env de esa instancia ya no existe). Por eso estos
# dos subcomandos NO exigen que db.env cuadre: solo miran o borran.

# `ew8_hypodb` -> 0 ; `ew8_hypodb_<N>` -> N ; cualquier otra cosa -> error.
inst_of_name() {
    case "$1" in
        ew8_hypodb)        printf '0\n' ;;
        ew8_hypodb_[0-9]*) printf '%s\n' "${1#ew8_hypodb_}" ;;
        *)                 return 1 ;;
    esac
}

# Worktree PRINCIPAL (el primero de `git worktree list`, como documenta git),
# sin importar desde que worktree se ejecute este script: N=0 es SIEMPRE el
# principal del repo, no la carpeta desde donde se invoca (que en un worktree
# hacia parecer que la base de la 0 vivia alli).
main_worktree() {
    local w=""
    if command -v git >/dev/null 2>&1; then
        w="$(git -C "$ROOT" worktree list --porcelain 2>/dev/null | sed -n 's/^worktree //p' | head -1)"
    fi
    [ -n "$w" ] || w="$ROOT"
    printf '%s\n' "$w"
}

# Instancias >=1 cuyo worktree sigue registrado en ESTE repo, deducidas del
# DB_PORT de cada worktree (5435+N). stdout: "<N> <ruta>".
inst_registered() {
    command -v git >/dev/null 2>&1 || return 0
    git -C "$ROOT" worktree list --porcelain 2>/dev/null \
        | sed -n 's/^worktree //p' \
        | while IFS= read -r w; do
            [ -f "$w/run_working_v8/params/db.env" ] || continue
            sed -n 's/^DB_PORT=//p' "$w/run_working_v8/params/db.env" | tail -1 \
                | while IFS= read -r p; do
                    case "$p" in ''|*[!0-9]*) continue ;; esac
                    [ "$p" -gt 5435 ] && printf '%s %s\n' "$((p - 5435))" "$w"
                done
        done
}

cmd_instances() {
    need_docker
    [ $# -eq 0 ] || die 1 "instances no lleva argumentos"
    local reg names vols ns n line cname cstate cport vol wt main_wt

    reg="$(inst_registered)"
    main_wt="$(main_worktree)"
    names="$("$DOCKER" ps -a --filter 'name=^ew8_hypodb' --format '{{.Names}}\t{{.Status}}\t{{.Ports}}' 2>/dev/null)"
    vols="$("$DOCKER" volume ls --filter 'name=ew8_hypodb' --format '{{.Name}}' 2>/dev/null)"

    # N que aparecen en un contenedor o en un volumen, ordenados y sin repetir.
    ns="$(
        { printf '%s\n' "$names" | cut -f1
          printf '%s\n' "$vols" | sed 's/_ew8_db_data$//'
        } 2>/dev/null | while IFS= read -r x; do
            [ -n "$x" ] || continue
            inst_of_name "$x" 2>/dev/null || continue
        done | sort -n -u
    )"

    if [ -z "$ns" ]; then
        echo "[ew8_db] no hay ninguna base ew8_hypodb* en este docker"
        return 0
    fi

    printf '%-3s %-14s %-26s %-6s %-34s %s\n' N CONTENEDOR ESTADO PUERTO VOLUMEN WORKTREE
    for n in $ns; do
        cname="ew8_hypodb"; [ "$n" -gt 0 ] && cname="ew8_hypodb_$n"
        line="$(printf '%s\n' "$names" | awk -F'\t' -v c="$cname" '$1==c {print; exit}')"
        cstate="$(printf '%s' "$line" | cut -f2)"; [ -n "$cstate" ] || cstate='-'
        # Puerto publicado (si el contenedor esta parado no hay linea, y vale el
        # 5435+N de la convencion de `-i N`).
        cport="$(printf '%s' "$line" | cut -f3 | sed -n 's/.*:\([0-9][0-9]*\)->5432.*/\1/p' | head -1)"
        [ -n "$cport" ] || cport="$((5435 + n))"

        vol="ew8_hypodb_ew8_db_data"; [ "$n" -gt 0 ] && vol="ew8_hypodb_${n}_ew8_db_data"
        printf '%s\n' "$vols" | grep -qxF "$vol" || vol='-'

        wt="$(printf '%s\n' "$reg" | awk -v i="$n" '$1==i {print $2; exit}')"
        if [ "$n" -eq 0 ]; then
            wt="principal ($main_wt)"
        elif [ -z "$wt" ]; then
            wt='huerfana (sin worktree registrado)'
        fi

        printf '%-3s %-14s %-26s %-6s %-34s %s\n' "$n" "$cname" "$cstate" "$cport" "$vol" "$wt"
    done
    return 0
}

cmd_destroy() {
    need_docker
    local yes=0 a
    for a in "$@"; do
        case "$a" in
            --yes|-y) yes=1 ;;
            *)        die 1 "destroy: opcion desconocida '$a' (solo --yes)" ;;
        esac
    done
    # La base principal nunca se borra "sin querer": exige --yes hasta de forma
    # interactiva.
    if [ "$INSTANCE" -eq 0 ] && [ "$yes" -eq 0 ]; then
        die 1 "destroy en la instancia 0 (la base PRINCIPAL) exige --yes explicito"
    fi
    if [ "$yes" -eq 0 ]; then
        [ -t 0 ] || die 1 "destroy sin --yes y sin terminal: abortado (no se toco nada)"
        read -r -p "[ew8_db] esto BORRA contenedor+volumen+red de la instancia $INSTANCE (los eventos NO se recuperan). ¿Seguir? [s/N] " r
        case "${r:-n}" in
            s|S|y|Y) ;;
            *) echo "[ew8_db] cancelado"; return 0 ;;
        esac
    fi
    compose down -v || die 1 "fallo 'docker compose down -v'"
    echo "[ew8_db] instancia $INSTANCE borrada: contenedor, volumen y red"
}

cmd_psql() {
    need_docker
    need_env
    compose exec db psql -U "$(db_var DB_USER)" -d "$(db_var DB_NAME)" "$@"
}

[ $# -gt 0 ] || usage 1

# --- Instancia (worktrees) ---------------------------------------------------
# Se puede dar como -i N / --instance N / --instance=N / EWGUI_INSTANCE=N.
INSTANCE="${EWGUI_INSTANCE:-0}"
args=()
while [ $# -gt 0 ]; do
    case "$1" in
        -i|--instance)  [ $# -ge 2 ] || die 1 "falta el valor de $1"; INSTANCE="$2"; shift 2 ;;
        -i*)            INSTANCE="${1#-i}"; shift ;;
        --instance=*)   INSTANCE="${1#--instance=}"; shift ;;
        *)              args+=("$1"); shift ;;
    esac
done
set -- ${args[@]+"${args[@]}"}
[ $# -gt 0 ] || usage 1

case "$INSTANCE" in
    ''|*[!0-9]*) die 1 "instancia invalida: '$INSTANCE' (usa un entero >= 0)" ;;
esac

# El puerto de la instancia manda: sale de db.env (instancia 0) o de 5435+N.
if [ -f "$ENVFILE" ]; then
    env_port="$(db_var DB_PORT)"
    [ -n "$env_port" ] && export DB_PORT="$env_port"
fi
if [ "$INSTANCE" -gt 0 ]; then
    export COMPOSE_PROJECT_NAME="ew8_hypodb_$INSTANCE"
    export DB_RESTART="no"
    want_port=$((5435 + INSTANCE))
    if [ -f "$ENVFILE" ] && [ "${DB_PORT:-}" != "$want_port" ]; then
        # Solo es ERROR si vamos a LEVANTAR o CONECTAR esa base (up/psql/reset):
        # ahi el db.env local tiene que cuadrar o los modulos apuntarian a otra.
        # Para mirar o borrar la base de OTRA instancia (p. ej. la de un worktree
        # ya borrado, que no tiene db.env) basta con avisar y seguir.
        case "$1" in
            up|psql|reset)
                die 1 "instancia $INSTANCE: ${ENVFILE#$ROOT/} declara DB_PORT=${DB_PORT:-?} y deberia declarar DB_PORT=$want_port (si no, los modulos apuntarian a otra base)" ;;
            *)
                echo "AVISO: instancia $INSTANCE: se usa DB_PORT=$want_port (${ENVFILE#$ROOT/} declara ${DB_PORT:-?})" >&2 ;;
        esac
    fi
    export DB_PORT="$want_port"
fi

case "$1" in
    up)             shift; cmd_up "$@" ;;
    status|ps)      shift; cmd_status "$@" ;;
    down|stop)      shift; cmd_down "$@" ;;
    reset)          shift; cmd_reset "$@" ;;
    destroy)        shift; cmd_destroy "$@" ;;
    instances)      shift; cmd_instances "$@" ;;
    logs)           shift; cmd_logs "$@" ;;
    psql)           shift; cmd_psql "$@" ;;
    deps|check)     shift; cmd_deps "$@" ;;
    -h|--help|help) shift; usage 0 ;;
    *) echo "ERROR: opcion desconocida: $1" >&2; usage 1 ;;
esac
