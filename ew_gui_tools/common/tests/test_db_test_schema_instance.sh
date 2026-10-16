#!/usr/bin/env bash
# Test de `db/test_schema.sh`: verifica las 6 tablas de LA INSTANCIA que se pide.
#
# Por que: el script montaba su propio `docker compose -f db/docker-compose.yml` (sin
# proyecto ni puerto de instancia) y a la vez cargaba el db.env del worktree
# (DB_PORT=5436), asi que en un worktree levantaba el contenedor de la instancia 0
# (`ew8_hypodb`, que es el default de docker-compose.yml) con el puerto del worktree y
# el volumen del principal: "Bind for 0.0.0.0:5436 failed: port is already allocated", y
# cuando arrancaba leia las 6 tablas de la base equivocada. Ahora delega en
# `ew8_db.sh -i N`, que es quien sabe de proyecto, puerto y volumen por instancia.
#
# No necesita docker: se copia un repo minimo a un temporal y se inyecta un `docker` de
# mentira con EWGUI_DOCKER=$STUB, que registra cada llamada (con su entorno) en
# $STUB_LOG. NUNCA se ejecuta docker de verdad.
#
# Se engancha en `make -C ew_gui_tools/common check`.
set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
SCRIPT="$ROOT/db/test_schema.sh"

pass=0; fail=0
ok()  { echo "PASS $1"; pass=$((pass + 1)); }
bad() { echo "FAIL $1"; fail=$((fail + 1)); }

TMP="$(mktemp -d "${TMPDIR:-/tmp}/db_test_schema.XXXXXX")" || exit 2
trap 'rm -rf "$TMP"' EXIT

# --- repo minimo (los scripts derivan ROOT de su propia ubicacion) -----------
REPO="$TMP/repo"
mkdir -p "$REPO/db" "$REPO/run_working_v8/params"
cp "$ROOT/ew8_db.sh"        "$REPO/ew8_db.sh"
cp "$SCRIPT"                "$REPO/db/test_schema.sh"
cp "$ROOT/db/docker-compose.yml" "$REPO/db/docker-compose.yml"
chmod +x "$REPO/ew8_db.sh" "$REPO/db/test_schema.sh"

# db.env de la instancia que se este probando (en un worktree lo deja el bootstrap
# con DB_PORT=5435+N).
set_env() {
    cat > "$REPO/run_working_v8/params/db.env" <<EOF
DB_HOST=127.0.0.1
DB_PORT=$1
DB_NAME=csn_hypo
DB_USER=csn
EOF
}

# --- docker de mentira ------------------------------------------------------
LOG="$TMP/calls.log"
STUB="$TMP/docker"
cat > "$STUB" <<'EOF'
#!/bin/sh
{ printf 'ARGS'; for a in "$@"; do printf ' [%s]' "$(printf '%s' "$a" | tr '\n' ' ')"; done
  printf ' ENV project=%s port=%s restart=%s\n' \
      "${COMPOSE_PROJECT_NAME:-}" "${DB_PORT:-}" "${DB_RESTART:-}"; } >> "$STUB_LOG"
all="$*"
case "$1" in
    compose)
        case "${2:-}" in version) echo "Docker Compose version v2.24.0" ;; esac
        case "$all" in
            # shellcheck disable=SC2086
            *pg_tables*)   printf '%s\n' $STUB_TABLES ;;
            *tablas=*)     echo "tablas=6" ;;
            *pg_isready*)  : ;;
            *'ps --format'*) echo "healthy" ;;
        esac
        ;;
    --version) echo "Docker version 27.0.0, build test" ;;
esac
exit 0
EOF
chmod +x "$STUB"

export STUB_LOG="$LOG"
export EWGUI_DOCKER="$STUB"
export STUB_TABLES="event magnitude origin pick station_magnitude tensor"
unset COMPOSE_PROJECT_NAME DB_PORT DB_RESTART EWGUI_INSTANCE 2>/dev/null || true

out="$TMP/out"; err="$TMP/err"
run() {   # run <exit esperado> <descripcion> <args...>
    local want="$1" desc="$2"; shift 2
    : > "$LOG"
    "$REPO/db/test_schema.sh" "$@" >"$out" 2>"$err" </dev/null
    local rc=$?
    if [ "$rc" -eq "$want" ]; then
        ok "$desc (exit $rc)"
    else
        bad "$desc: exit $rc (esperaba $want)"
        sed 's/^/       /' "$err"
    fi
}
calls() { cat "$LOG"; }

# --- 1. sintaxis ------------------------------------------------------------
if bash -n "$SCRIPT" 2>"$err"; then ok "bash -n db/test_schema.sh"; else bad "bash -n db/test_schema.sh"; sed 's/^/       /' "$err"; fi

# --- 2. instancia 1: todo va al proyecto/puerto de ESA instancia -------------
set_env 5436
run 0 "-i 1 (db.env declara 5436)" -i 1
grep -q 'OK: 6 tablas creadas (instancia 1)' "$out" && ok "dice que verifico la instancia 1" || bad "no verifico la instancia 1"
if calls | grep -qv 'project=ew8_hypodb_1'; then
    bad "hubo alguna llamada de docker SIN el proyecto de la instancia 1"
    calls | sed 's/^/       /'
else
    ok "todas las llamadas van a ew8_hypodb_1"
fi
if calls | grep -qv 'port=5436'; then
    bad "hubo alguna llamada de docker con otro puerto que 5436"
    calls | sed 's/^/       /'
else
    ok "todas las llamadas exportan DB_PORT=5436"
fi
calls | grep -q '\[psql\]' && ok "las tablas se leen con 'ew8_db.sh psql'" || bad "las tablas no se leen con 'ew8_db.sh psql'"

# --- 3. la instancia 0 no se contamina con el proyecto de la 1 --------------
set_env 5435
run 0 "sin -i (= instancia 0)"
grep -q 'instancia 0' "$out" && ok "sin -i usa la instancia 0" || bad "sin -i no reporto la instancia 0"
if calls | grep -qv 'port=5435'; then
    bad "la instancia 0 no uso el puerto 5435"
    calls | sed 's/^/       /'
else
    ok "la instancia 0 usa DB_PORT=5435"
fi
calls | grep -q 'project=ew8_hypodb_1' && bad "la instancia 0 exporto el proyecto de la 1" || ok "la instancia 0 no exporta proyecto de worktree"

# --- 4. falta una tabla -> exit 1 nombrandola -------------------------------
set_env 5436
STUB_TABLES="event magnitude origin pick station_magnitude"
run 1 "-i 1 con una tabla ausente" -i 1
STUB_TABLES="event magnitude origin pick station_magnitude tensor"
grep -q 'falta la tabla tensor' "$err" && ok "nombra la tabla que falta" || bad "no nombro la tabla ausente"
grep -q 'OK:' "$out" && bad "dijo OK con una tabla ausente" || ok "no dice OK si falta una tabla"

# --- 5. instancia invalida: exit 2 y no toca docker -------------------------
run 2 "-i x (invalida)" -i x
[ -s "$LOG" ] && bad "con instancia invalida llego a docker" || ok "con instancia invalida no llega a docker"

# --- 6. sin db.env: exit 2 y no toca docker ---------------------------------
mv "$REPO/run_working_v8/params/db.env" "$TMP/db.env.keep"
run 2 "sin db.env" -i 1
[ -s "$LOG" ] && bad "sin db.env llego a docker" || ok "sin db.env no llega a docker"
grep -q 'FALTA' "$err" && ok "explica que falta db.env" || bad "no explica que falta db.env"
mv "$TMP/db.env.keep" "$REPO/run_working_v8/params/db.env"

# --- 7. -h: exit 0 y no toca docker ----------------------------------------
run 0 "-h" -h
[ -s "$LOG" ] && bad "-h llego a docker" || ok "-h no llega a docker"

echo "----"
echo "PASS=$pass FAIL=$fail"
[ "$fail" -eq 0 ] || exit 1
