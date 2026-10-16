#!/usr/bin/env bash
# Invariante de doc: dentro de un MISMO bloque de codigo, `source ./ew8_unix.sh` no puede
# aparecer DESPUES de un `make -C`.
#
# Por que: el entorno fija EW_HOME/EW_VERSION, y de ahi salen las cabeceras, las librerias y
# el DESTINO de instalacion (`install_bin.sh $(EW_HOME)/$(EW_VERSION)/bin`). Un bloque que
# compila y despues "source" se lee al reves: parece que hubiera que cargar el entorno para
# trabajar, y esconde el error real (compilar sin entorno no instala donde debe; con el
# entorno de OTRO repo, instala en el otro repo). Ya paso dos veces en la doc de worktrees.
#
# Solo mira bloques cercados (```...```), salta los comentarios `#` (pueden contar la
# historia) y solo exige el ORDEN cuando el bloque compila: un bloque suelto que solo
# recompila un modulo se asume continuacion de una sesion ya cargada.
#
# Se engancha en `make -C ew_gui_tools/common check`.
set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"

pass=0; fail=0
ok()  { echo "PASS $1"; pass=$((pass + 1)); }
bad() { echo "FAIL $1"; fail=$((fail + 1)); }

TMP="$(mktemp -d "${TMPDIR:-/tmp}/docs_env_order.XXXXXX")" || exit 2
trap 'rm -rf "$TMP"' EXIT

# El detector: recorre bloques cercados; en cada uno apunta la 1.a linea con `make -C` y
# avisa de cualquier `source ./ew8_unix.sh` de codigo que venga despues.
cat > "$TMP/order.awk" <<'AWK'
/^[[:space:]]*```/ {
    if (inblk && late)
        printf "%d: 'source ./ew8_unix.sh' despues de 'make -C' (linea %d)\n", late_ln, mk
    if (!inblk) { mk = 0; late = 0 }
    inblk = !inblk
    next
}
!inblk { next }
/^[[:space:]]*#/ { next }
/source \.\/ew8_unix\.sh/ { if (mk && !late) { late = 1; late_ln = NR } next }
/make -C/ { if (!mk) mk = NR }
AWK

# --- 1. el detector detecta de verdad (fixture con el patron malo) -----------
cat > "$TMP/malo.md" <<'EOF'
```bash
make -C ew_gui_tools/common GUI_TOOLKIT=gtk4
for d in csnloc; do make -C ew_gui_tools/$d; done
# (un comentario con "source ./ew8_unix.sh" NO cuenta)
source ./ew8_unix.sh
```
EOF
if [ -n "$(awk -f "$TMP/order.awk" "$TMP/malo.md")" ]; then
    ok "detecta el patron malo (fixture)"
else
    bad "el detector no vio el 'source' despues del 'make' de la fixture"
fi

# --- 2. y no da falso positivo con el orden correcto ------------------------
cat > "$TMP/bueno.md" <<'EOF'
```bash
source ./ew8_unix.sh
make -C ew_gui_tools/common GUI_TOOLKIT=gtk4
```
EOF
if [ -z "$(awk -f "$TMP/order.awk" "$TMP/bueno.md")" ]; then
    ok "no da falso positivo con 'source' antes del 'make'"
else
    bad "dio falso positivo con el orden correcto"
    awk -f "$TMP/order.awk" "$TMP/bueno.md" | sed 's/^/       /'
fi

# --- 3. la doc operativa tiene el orden correcto ----------------------------
DOCS=(docs/WORKTREES.md docs/WorktreePipeline.md)
for d in "${DOCS[@]}"; do
    f="$ROOT/$d"
    [ -f "$f" ] || continue
    hits="$(awk -f "$TMP/order.awk" "$f")"
    if [ -n "$hits" ]; then
        bad "$d carga el entorno DESPUES de compilar (dentro de un bloque)"
        printf '%s\n' "$hits" | sed 's/^/       /'
    else
        ok "$d: el entorno se carga antes de compilar"
    fi
done

echo "----"
echo "PASS=$pass FAIL=$fail"
[ "$fail" -eq 0 ] || exit 1
