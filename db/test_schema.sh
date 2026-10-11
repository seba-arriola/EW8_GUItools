#!/usr/bin/env bash
# AC1: el esquema aplica y crea las 6 tablas esperadas.
# Requiere docker y run_working_v8/params/db.env (copiado de db.env.example).
#
# Uso: db/test_schema.sh
set -u

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
ENV_FILE="$ROOT/run_working_v8/params/db.env"
COMPOSE=(docker compose -f "$HERE/docker-compose.yml")

if [ ! -f "$ENV_FILE" ]; then
    echo "FALTA $ENV_FILE (copiar de run_working_v8/params/db.env.example)" >&2
    exit 2
fi

# Cargar credenciales (POSTGRES_*)
set -a
# shellcheck disable=SC1090
. "$ENV_FILE"
set +a
PGUSER_="${POSTGRES_USER:-csn}"
PGDB_="${POSTGRES_DB:-csn_hypo}"

echo "== levantando postgres =="
if ! "${COMPOSE[@]}" up -d --wait db; then
    echo "FAIL: no arranca el contenedor" >&2
    "${COMPOSE[@]}" logs db | tail -n 20
    exit 1
fi

echo "== tablas =="
TABLES="$("${COMPOSE[@]}" exec -T db psql -U "$PGUSER_" -d "$PGDB_" -tAc \
    "SELECT tablename FROM pg_tables WHERE schemaname='public' ORDER BY tablename;")"
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
    echo "OK: 6 tablas creadas"
else
    echo "FAIL"
fi
exit "$rc"
