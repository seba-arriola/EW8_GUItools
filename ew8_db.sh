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
#  USO
#  ---
#     ./ew8_db.sh up            levanta la base (espera al healthcheck)
#     ./ew8_db.sh status        estado del contenedor + healthcheck + tablas
#     ./ew8_db.sh down          detiene el contenedor (CONSERVA los datos)
#     ./ew8_db.sh reset         borra el volumen y vuelve a crear la base
#     ./ew8_db.sh logs          logs del contenedor (Ctrl-C para salir)
#     ./ew8_db.sh psql [args]   consola psql (por defecto: -U $DB_USER -d $DB_NAME)
#     ./ew8_db.sh deps          comprueba docker/compose y dice que falta
#     ./ew8_db.sh -h|--help
#
#  CODIGOS DE SALIDA
#     0 ok | 1 error de uso o docker ausente | 2 la base no esta sana
# =============================================================================

set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
COMPOSE="$ROOT/db/docker-compose.yml"
ENVFILE="$ROOT/run_working_v8/params/db.env"

die() { echo "ERROR: $*" >&2; exit "$1"; }

usage() {
    sed -n '2,/^# =====/p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
    exit "${1:-0}"
}

compose() { docker compose -f "$COMPOSE" "$@"; }

need_docker() {
    command -v docker >/dev/null 2>&1 \
        || die 1 "docker no esta instalado. Instalalo en el destino (ver '$(basename "$0") deps')"
    docker compose version >/dev/null 2>&1 \
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
    if command -v docker >/dev/null 2>&1; then
        echo "ok  : docker $(docker --version 2>/dev/null | awk '{print $3}' | tr -d ,)"
    else
        echo "FALTA: docker"
        ok=0
    fi
    if docker compose version >/dev/null 2>&1; then
        echo "ok  : $(docker compose version 2>/dev/null | head -1)"
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

cmd_psql() {
    need_docker
    need_env
    compose exec db psql -U "$(db_var DB_USER)" -d "$(db_var DB_NAME)" "$@"
}

[ $# -gt 0 ] || usage 1
case "$1" in
    up)             shift; cmd_up "$@" ;;
    status|ps)      shift; cmd_status "$@" ;;
    down|stop)      shift; cmd_down "$@" ;;
    reset)          shift; cmd_reset "$@" ;;
    logs)           shift; cmd_logs "$@" ;;
    psql)           shift; cmd_psql "$@" ;;
    deps|check)     shift; cmd_deps "$@" ;;
    -h|--help|help) shift; usage 0 ;;
    *) echo "ERROR: opcion desconocida: $1" >&2; usage 1 ;;
esac
