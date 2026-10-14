#!/bin/bash
# AC3: check_install.sh (el guard que avisa si el instalado falta o está viejo)
# sobre un árbol de mentira: no toca el repo ni el bin real de EarthWorm.
set -u

MOD="$(cd "$(dirname "$0")/.." && pwd)"
CHECK="$MOD/tools/check_install.sh"
TMP="$(mktemp -d "${TMPDIR:-/tmp}/csncheckinstall.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT

fail=0
ok() { echo "ok: $1"; }
ko() { echo "FALLO: $1"; fail=1; }

# Árbol falso: sólo dos módulos compilados (csnloc y pickS); el resto no existe
# y debe salir por SKIP, no como problema.
mkdir -p "$TMP/root/ew_gui_tools/csnloc" "$TMP/root/ew_gui_tools/pickS" "$TMP/bin"
printf 'csnloc\n' > "$TMP/root/ew_gui_tools/csnloc/csnloc"
printf 'pickS\n'  > "$TMP/root/ew_gui_tools/pickS/pickS"
cp "$TMP/root/ew_gui_tools/csnloc/csnloc" "$TMP/bin/csnloc"
cp "$TMP/root/ew_gui_tools/pickS/pickS"  "$TMP/bin/pickS"

run() { EWGUI_REPO_ROOT="$TMP/root" EWGUI_INSTALL_DST="$TMP/bin" sh "$CHECK" "$@" 2>&1; }

# T1: todo al día -> exit 0 y sin reportes
out="$(run)"; rc=$?
if [ "$rc" -eq 0 ] && ! grep -q 'FALTA\|DESACTUALIZADO' <<<"$out"; then
    ok "T1 todo al día"
else
    ko "T1 rc=$rc [$out]"
fi

# T2: falta un instalado -> exit 1 + FALTA
rm -f "$TMP/bin/pickS"
out="$(run)"; rc=$?
if [ "$rc" -eq 1 ] && grep -q 'FALTA .*pickS' <<<"$out"; then
    ok "T2 FALTA"
else
    ko "T2 rc=$rc [$out]"
fi

# T3: instalado desactualizado -> exit 1 + DESACTUALIZADO
cp "$TMP/root/ew_gui_tools/pickS/pickS" "$TMP/bin/pickS"
printf 'viejo\n' > "$TMP/bin/csnloc"
out="$(run)"; rc=$?
if [ "$rc" -eq 1 ] && grep -q 'DESACTUALIZADO .*csnloc' <<<"$out"; then
    ok "T3 DESACTUALIZADO"
else
    ko "T3 rc=$rc [$out]"
fi

# T4: sin directorio de instalación -> exit 2
EWGUI_REPO_ROOT="$TMP/root" EWGUI_INSTALL_DST="$TMP/nope" sh "$CHECK" >/dev/null 2>&1
rc=$?
if [ "$rc" -eq 2 ]; then ok "T4 dst inexistente (rc 2)"; else ko "T4 rc=$rc (esperado 2)"; fi

# T5: -v lista los OK
cp "$TMP/root/ew_gui_tools/csnloc/csnloc" "$TMP/bin/csnloc"
out="$(run -v)"
if grep -q '^OK    csnloc' <<<"$out"; then ok "T5 -v lista OK"; else ko "T5 -v [$out]"; fi

# T6: instalado huérfano (sin binario local) -> AVISO, pero NO es un fallo
printf 'huerfano\n' > "$TMP/bin/hyp2000_ring"
out="$(run)"; rc=$?
if [ "$rc" -eq 0 ] && grep -q 'AVISO .*hyp2000_ring' <<<"$out"; then
    ok "T6 aviso de instalado huérfano"
else
    ko "T6 rc=$rc [$out]"
fi

if [ "$fail" -eq 0 ]; then echo "ALL INSTALL CHECK TESTS PASSED"; else echo "HAY FALLOS"; fi
exit $fail
