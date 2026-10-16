#!/usr/bin/env bash
# Invariante: `db/test_schema.sh` no vuelve a tener compose propio (delega la
# instancia en `ew8_db.sh -i N`) y ninguna doc operativa lo manda correr sin decir cual.
#
# Por que: el script montaba su propio compose sin proyecto ni puerto de instancia y a
# la vez cargaba el db.env del worktree, asi que en un worktree levanta el contenedor de
# la instancia 0 (`ew8_hypodb`) pidiendo el puerto del worktree y montando el volumen del
# principal: "Bind for 0.0.0.0:5436 failed: port is already allocated" y, cuando
# arrancaba, las 6 tablas se leian de la base equivocada. El comportamiento (que `-i N`
# vaya a la instancia N) lo cubre `tests/test_db_test_schema_instance.sh`.
#
# El compose que se prohibe es el del CODIGO (los comentarios pueden contar la historia).
#
# Se engancha en `make -C ew_gui_tools/common check`.
set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
SCRIPT="$ROOT/db/test_schema.sh"

pass=0; fail=0
ok()  { echo "PASS $1"; pass=$((pass + 1)); }
bad() { echo "FAIL $1"; fail=$((fail + 1)); }

code="$(grep -v '^[[:space:]]*#' "$SCRIPT")"

if printf '%s\n' "$code" | grep -qE 'docker[ -]compose'; then
    bad "db/test_schema.sh invoca docker compose por su cuenta (debe delegar en ew8_db.sh)"
    printf '%s\n' "$code" | grep -nE 'docker[ -]compose' | sed 's/^/       /'
else
    ok "db/test_schema.sh no tiene compose propio"
fi

if printf '%s\n' "$code" | grep -qF 'COMPOSE=('; then
    bad "db/test_schema.sh define su propio COMPOSE=("
else
    ok "db/test_schema.sh no define COMPOSE=("
fi

if grep -qF 'ew8_db.sh' "$SCRIPT" && grep -qF -- '-i "$INSTANCE"' "$SCRIPT"; then
    ok 'delega la instancia: ew8_db.sh -i $INSTANCE'
else
    bad 'db/test_schema.sh no delega en ew8_db.sh con -i $INSTANCE'
fi

# --- la doc operativa no lo manda sin decir la instancia --------------------
DOCS=(README.md db/README.md docs/HYPODB.md docs/WORKTREES.md docs/WorktreePipeline.md)
for d in "${DOCS[@]}"; do
    f="$ROOT/$d"
    [ -f "$f" ] || continue
    hits="$(grep -n 'test_schema\.sh' "$f" | grep -v -- '-i' || true)"
    if [ -n "$hits" ]; then
        bad "$d manda 'db/test_schema.sh' sin la instancia (-i N)"
        printf '%s\n' "$hits" | sed 's/^/       /'
    else
        ok "$d nombra la instancia junto a test_schema.sh"
    fi
done

echo "----"
echo "PASS=$pass FAIL=$fail"
[ "$fail" -eq 0 ] || exit 1
