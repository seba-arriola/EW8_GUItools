#!/bin/bash
# AC1/AC2: el instalador de binarios (common/tools/install_bin.sh) deja SIEMPRE
# el instalado al día, incluso si el destino está EJECUTÁNDOSE (donde un `cp`
# directo falla con "Text file busy"), y reporta los fallos en vez de callarlos.
set -u

MOD="$(cd "$(dirname "$0")/.." && pwd)"
HELPER="$MOD/tools/install_bin.sh"
TMP="$(mktemp -d "${TMPDIR:-/tmp}/csninstall.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT

fail=0
ok() { echo "ok: $1"; }
ko() { echo "FALLO: $1"; fail=1; }

# --- T1: destino vacío -> se instala y el contenido coincide
mkdir -p "$TMP/dst1"
printf 'v1\n' > "$TMP/src1"
sh "$HELPER" "$TMP/dst1" "$TMP/src1" >/dev/null 2>&1 || ko "T1 rc != 0"
if cmp -s "$TMP/src1" "$TMP/dst1/src1"; then ok "T1 destino vacío"; else ko "T1 no se instaló"; fi

# --- T2: destino con versión vieja -> se reemplaza
printf 'viejo\n' > "$TMP/dst1/src1"
sh "$HELPER" "$TMP/dst1" "$TMP/src1" >/dev/null 2>&1 || ko "T2 rc != 0"
if cmp -s "$TMP/src1" "$TMP/dst1/src1"; then ok "T2 reemplazo"; else ko "T2 no reemplazó"; fi

# --- T3 (AC2): instalar SOBRE un binario en ejecución no debe fallar
mkdir -p "$TMP/dst2"
if [ -x /bin/sleep ]; then cp /bin/sleep "$TMP/dst2/sleepbin"; else cp "$(command -v sleep)" "$TMP/dst2/sleepbin"; fi
"$TMP/dst2/sleepbin" 30 &
pid=$!
sleep 0.4
printf 'nueva version\n' > "$TMP/sleepbin"     # mismo basename: reemplaza al de dst2
# Prueba de la premisa: el `cp` clásico NO puede sobrescribir un ejecutable en uso.
if cp "$TMP/sleepbin" "$TMP/dst2/sleepbin" 2>/dev/null; then
    echo "aviso: el `cp` directo si pudo pisar un ejecutable en uso (entorno distinto)"
else
    echo "ok: premisa T3 (cp directo -> Text file busy)"
fi
if sh "$HELPER" "$TMP/dst2" "$TMP/sleepbin" >/dev/null 2>&1; then
    ok "T3 instala sobre binario en ejecución"
else
    ko "T3 falló instalando sobre un binario en ejecución"
fi
if cmp -s "$TMP/sleepbin" "$TMP/dst2/sleepbin"; then ok "T3 contenido nuevo"; else ko "T3 contenido viejo"; fi
if kill -0 "$pid" 2>/dev/null; then ok "T3 el proceso sigue vivo (inodo viejo)"; else ko "T3 el proceso murió"; fi
kill "$pid" 2>/dev/null
wait "$pid" 2>/dev/null

# --- T4: binario local ausente -> rc != 0 y NO crea el destino
mkdir -p "$TMP/dst3"
if sh "$HELPER" "$TMP/dst3" "$TMP/noexiste" >/dev/null 2>&1; then
    ko "T4 debió fallar con el local ausente"
else
    if [ -e "$TMP/dst3/noexiste" ]; then ko "T4 creó el destino con el local ausente"; else ok "T4 local ausente"; fi
fi

# --- T5: directorio destino inexistente -> rc 2
sh "$HELPER" "$TMP/noexiste_dir" "$TMP/src1" >/dev/null 2>&1
rc=$?
if [ "$rc" -eq 2 ]; then ok "T5 dst inexistente (rc 2)"; else ko "T5 rc=$rc (esperado 2)"; fi

# --- T6: varios binarios de una vez (caso wavestore)
mkdir -p "$TMP/dst4"
printf 'a\n' > "$TMP/a"; printf 'b\n' > "$TMP/b"
if sh "$HELPER" "$TMP/dst4" "$TMP/a" "$TMP/b" >/dev/null 2>&1 \
   && cmp -s "$TMP/a" "$TMP/dst4/a" && cmp -s "$TMP/b" "$TMP/dst4/b"; then
    ok "T6 varios binarios"
else
    ko "T6 varios binarios"
fi

if [ "$fail" -eq 0 ]; then echo "ALL INSTALL_BIN TESTS PASSED"; else echo "HAY FALLOS"; fi
exit $fail
