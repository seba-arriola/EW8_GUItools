#!/usr/bin/env bash
# Test de la CLI de `ew8_db.sh`: instancias ajenas (worktree ya borrado) y
# borrado definitivo, sin tocar ningun contenedor real.
#
# Por que: `-i 1 down` abortaba en la guarda de puerto cuando el db.env local no
# declaraba ese DB_PORT, asi que la base de un worktree ya borrado (ew8_hypodb_1
# en 5436) quedaba inalcanzable desde el principal; y no habia forma de borrar su
# volumen sin que `reset` volviera a levantar la base.
#
# No necesita docker: se copia un repo minimo a un temporal y se inyecta un
# `docker` de mentira con EWGUI_DOCKER=$STUB, que registra cada llamada (con su
# entorno) en $STUB_LOG. NUNCA se ejecuta docker de verdad.
#
# Se engancha en `make -C ew_gui_tools/common check`.
set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
SCRIPT="$ROOT/ew8_db.sh"

pass=0; fail=0
ok()  { echo "PASS $1"; pass=$((pass + 1)); }
bad() { echo "FAIL $1"; fail=$((fail + 1)); }

TMP="$(mktemp -d "${TMPDIR:-/tmp}/ew8_db_test.XXXXXX")" || exit 2
trap 'rm -rf "$TMP"' EXIT

# --- repo minimo (ew8_db.sh deriva ROOT de su propia ubicacion) --------------
REPO="$TMP/repo"
mkdir -p "$REPO/db" "$REPO/run_working_v8/params"
cp "$SCRIPT" "$REPO/ew8_db.sh"
cp "$ROOT/db/docker-compose.yml" "$REPO/db/docker-compose.yml"
cat > "$REPO/run_working_v8/params/db.env" <<'EOF'
DB_HOST=127.0.0.1
DB_PORT=5435
DB_NAME=csn_hypo
DB_USER=csn
EOF
chmod +x "$REPO/ew8_db.sh"

# --- docker de mentira ------------------------------------------------------
LOG="$TMP/calls.log"
STUB="$TMP/docker"
cat > "$STUB" <<'EOF'
#!/bin/sh
{ printf 'ARGS'; for a in "$@"; do printf ' [%s]' "$a"; done
  printf ' ENV project=%s port=%s restart=%s\n' \
      "${COMPOSE_PROJECT_NAME:-}" "${DB_PORT:-}" "${DB_RESTART:-}"; } >> "$STUB_LOG"
case "$1" in
    compose)
        case "${2:-}" in
            version) echo "Docker Compose version v2.24.0" ;;
        esac
        ;;
    --version) echo "Docker version 27.0.0, build test" ;;
    ps)
        printf 'NAME\tSTATUS\tPORTS\n'
        printf 'ew8_hypodb\tUp 3 days (healthy)\t0.0.0.0:5435->5432/tcp\n'
        printf 'ew8_hypodb_1\tExited (0) 2 hours ago\t\n'
        ;;
    volume)
        printf 'ew8_hypodb_ew8_db_data\n'
        printf 'ew8_hypodb_1_ew8_db_data\n'
        ;;
esac
exit 0
EOF
chmod +x "$STUB"
export STUB_LOG="$LOG"
export EWGUI_DOCKER="$STUB"

out="$TMP/out"; err="$TMP/err"
run() {   # run <exit esperado> <descripcion> <args...>
    local want="$1" desc="$2"; shift 2
    : > "$LOG"
    "$REPO/ew8_db.sh" "$@" >"$out" 2>"$err" </dev/null
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
if bash -n "$SCRIPT" 2>"$err"; then ok "bash -n ew8_db.sh"; else bad "bash -n ew8_db.sh"; sed 's/^/       /' "$err"; fi

# --- 2. la ayuda lista los subcomandos nuevos -------------------------------
run 0 "ayuda" -h
grep -q 'instances' "$out" && ok "la ayuda lista 'instances'" || bad "la ayuda no lista 'instances'"
grep -q 'destroy'   "$out" && ok "la ayuda lista 'destroy'"   || bad "la ayuda no lista 'destroy'"

# --- 3. guarda de puerto: bajar OTRA instancia no aborta --------------------
run 0 "-i 1 down (db.env local declara 5435)" -i 1 down
grep -q 'AVISO' "$err" && ok "avisa del puerto en vez de abortar" || bad "no aviso del puerto de la instancia 1"
calls | grep -q '\[-f\]'    && ok "llama a docker compose -f <compose>" || bad "no llamo a docker compose -f"
calls | grep -q '\[down\]'  && ok "down usa 'down'"                    || bad "down no llamo a 'down'"
calls | grep -q '\[-v\]'    && bad "down NO deberia usar -v"           || ok "down no usa -v (conserva el volumen)"
calls | grep -q 'project=ew8_hypodb_1' && ok "down apunta a ew8_hypodb_1" || bad "down no exporto COMPOSE_PROJECT_NAME=ew8_hypodb_1"
calls | grep -q 'port=5436' && ok "down exporta DB_PORT=5436"          || bad "down no exporto DB_PORT=5436"

# --- 4. destroy: borra SIN recrear ------------------------------------------
run 0 "-i 1 destroy --yes" -i 1 destroy --yes
calls | grep -q '\[down\] \[-v\]' && ok "destroy hace 'down -v'"         || bad "destroy no hizo 'down -v'"
calls | grep -q '\[up\]'          && bad "destroy NO debe recrear la base" || ok "destroy no recrea la base"

# --- 5. destroy sin --yes y sin terminal: no toca nada ----------------------
run 1 "-i 1 destroy (sin terminal)" -i 1 destroy
calls | grep -q '\[down\]' && bad "destroy sin --yes llego a docker" || ok "destroy sin --yes no llego a docker"

# --- 6. la instancia 0 (principal) exige --yes ------------------------------
run 1 "destroy de la instancia 0 sin --yes" destroy
calls | grep -q '\[down\]' && bad "destroy de la 0 sin --yes llego a docker" || ok "destroy de la 0 sin --yes no llego a docker"

# --- 7. reset sigue estricto con el puerto ----------------------------------
run 1 "-i 1 reset (el puerto no cuadra)" -i 1 reset
grep -q 'DB_PORT=5436' "$err" && ok "reset aborta y dice el DB_PORT que falta" || bad "reset no explico el DB_PORT esperado"

# --- 8. instances -----------------------------------------------------------
run 0 "instances" instances
grep -q 'ew8_hypodb_1' "$out"            && ok "instances lista la instancia 1"          || bad "instances no lista la instancia 1"
grep -q 'huerfana' "$out"                && ok "instances marca la 1 como huerfana"      || bad "instances no marca la huerfana"
grep -q 'ew8_hypodb_1_ew8_db_data' "$out" && ok "instances muestra su volumen"           || bad "instances no muestra el volumen de la 1"
grep -q '5436' "$out"                    && ok "instances muestra el puerto 5436"        || bad "instances no muestra el puerto"
grep -q 'principal' "$out"               && ok "instances marca la 0 como principal"     || bad "instances no marca la principal"

# --- 9. sin docker ----------------------------------------------------------
: > "$LOG"
EWGUI_DOCKER=/nonexistent/docker "$REPO/ew8_db.sh" instances >"$out" 2>"$err" </dev/null
rc=$?
[ "$rc" -eq 1 ] && ok "sin docker: instances sale 1" || bad "sin docker: instances salio $rc (esperaba 1)"

echo "----"
echo "PASS=$pass FAIL=$fail"
[ "$fail" -eq 0 ] || exit 1
