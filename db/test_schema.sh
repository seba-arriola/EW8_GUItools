#!/usr/bin/env bash
# AC1: el esquema aplica y crea las 6 tablas esperadas, en la instancia que se pida.
# Requiere docker y run_working_v8/params/db.env (copiado de db.env.example; el
# bootstrap de un worktree lo deja con DB_PORT=5435+<N>).
#
# Uso: db/test_schema.sh [-i N]
#        -i N   instancia de la base: 0 = principal (default), N >= 1 = worktree
#               (contenedor ew8_hypodb_<N>, puerto 5435+N, volumen propio)
#
# Por que delega TODO en ew8_db.sh: este script montaba su propio
# `docker compose -f db/docker-compose.yml` (sin proyecto ni puerto de instancia) pero
# SI cargaba el db.env del worktree (DB_PORT=5436), asi que en un worktree levantaba el
# contenedor de la instancia 0 —`docker-compose.yml` usa
# `${COMPOSE_PROJECT_NAME:-ew8_hypodb}`— montando el VOLUMEN del principal y pidiendo el
# puerto del worktree: "Bind for 0.0.0.0:5436 failed: port is already allocated"; y
# cuando arrancaba, las 6 tablas se leian de la base equivocada. Quien sabe de proyecto,
# puerto y volumen por instancia es ew8_db.sh (unica fuente de verdad).
set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
DB="$ROOT/ew8_db.sh"
ENV_FILE="$ROOT/run_working_v8/params/db.env"

usage() {
    echo "Uso: ${0#$ROOT/} [-i N]" >&2
    echo "  -i N   instancia de la base: 0 = principal (default), N >= 1 = worktree" >&2
    exit "${1:-2}"
}

INSTANCE="${EWGUI_INSTANCE:-0}"
while [ $# -gt 0 ]; do
    case "$1" in
        -i|--instance)  [ $# -ge 2 ] || usage; INSTANCE="$2"; shift 2 ;;
        -i*)            INSTANCE="${1#-i}"; shift ;;
        --instance=*)   INSTANCE="${1#--instance=}"; shift ;;
        -h|--help)      usage 0 ;;
        *)              echo "ERROR: opcion desconocida: '$1'" >&2; usage ;;
    esac
done
case "$INSTANCE" in
    ''|*[!0-9]*) echo "ERROR: instancia invalida: '$INSTANCE' (usa un entero >= 0)" >&2; exit 2 ;;
esac

if [ ! -f "$ENV_FILE" ]; then
    echo "FALTA ${ENV_FILE#$ROOT/} (copiar de run_working_v8/params/db.env.example)" >&2
    exit 2
fi

echo "== levantando postgres (instancia $INSTANCE) =="
if ! "$DB" -i "$INSTANCE" up; then
    echo "FAIL: no arranca el contenedor de la instancia $INSTANCE" >&2
    echo "      revisa: ./ew8_db.sh -i $INSTANCE logs" >&2
    exit 1
fi

echo "== tablas (instancia $INSTANCE) =="
TABLES="$("$DB" -i "$INSTANCE" psql -tAc \
    "SELECT tablename FROM pg_tables WHERE schemaname='public' ORDER BY tablename;")" || {
    echo "FAIL: no se pudieron leer las tablas de la instancia $INSTANCE" >&2
    exit 1
}
echo "$TABLES"

EXPECTED="event magnitude origin pick station_magnitude tensor"
rc=0
for t in $EXPECTED; do
    if ! printf '%s\n' "$TABLES" | grep -qx "$t"; then
        echo "FAIL: falta la tabla $t" >&2
        rc=1
    fi
done

if [ "$rc" -eq 0 ]; then
    echo "OK: 6 tablas creadas (instancia $INSTANCE)"
else
    echo "FAIL"
fi
exit "$rc"
