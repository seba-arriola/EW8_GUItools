#!/bin/bash
# AC3: check_install.sh (el guard que avisa si el instalado falta o está viejo)
# sobre un árbol de mentira: no toca el repo ni el bin real de EarthWorm.
# El inventario del guard se DERIVA de las recetas de instalación de los
# Makefiles, así que el árbol falso tiene que traerlas (T7-T9).
set -u

MOD="$(cd "$(dirname "$0")/.." && pwd)"
CHECK="$MOD/tools/check_install.sh"
TMP="$(mktemp -d "${TMPDIR:-/tmp}/csncheckinstall.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT

fail=0
ok() { echo "ok: $1"; }
ko() { echo "FALLO: $1"; fail=1; }

# Árbol falso: sólo unos módulos compilados; el resto no existe y debe salir por
# SKIP, no como problema. El inventario se DERIVA de la receta de instalación de
# cada Makefile, así que un módulo sin Makefile no cuenta (ver T9).
mkdir -p "$TMP/root/ew_gui_tools/csnloc" "$TMP/root/ew_gui_tools/pickS" \
         "$TMP/root/ew_gui_tools/hyp2000_ring" "$TMP/root/ew_gui_tools/csnstaevdisp" \
         "$TMP/root/ew_gui_tools/wavestore" "$TMP/bin"

mk_recipe() {   # mk_recipe <dir> <linea de variables> <binarios>
    printf '%s\ninstall:\n\tsh ../common/tools/install_bin.sh $(EW_HOME)/$(EW_VERSION)/bin %s\n' \
        "$2" "$3" > "$TMP/root/ew_gui_tools/$1/Makefile"
}
mk_recipe csnloc       'TARGET = csnloc'          '$(TARGET)'
mk_recipe pickS        'TARGET = pickS'           '$(TARGET)'
mk_recipe hyp2000_ring 'TARGET = hyp2000_ring'    '$(TARGET)'
mk_recipe csnstaevdisp 'MODULE = csnstaevdisp'    '$(MODULE)'
mk_recipe wavestore    ''                         'wstore_ingest wstore_query'

printf 'csnloc\n'        > "$TMP/root/ew_gui_tools/csnloc/csnloc"
printf 'pickS\n'         > "$TMP/root/ew_gui_tools/pickS/pickS"
printf 'csnstaevdisp\n'  > "$TMP/root/ew_gui_tools/csnstaevdisp/csnstaevdisp"
printf 'wstore_ingest\n' > "$TMP/root/ew_gui_tools/wavestore/wstore_ingest"
printf 'wstore_query\n'  > "$TMP/root/ew_gui_tools/wavestore/wstore_query"
cp "$TMP/root/ew_gui_tools/csnloc/csnloc"             "$TMP/bin/csnloc"
cp "$TMP/root/ew_gui_tools/pickS/pickS"               "$TMP/bin/pickS"
cp "$TMP/root/ew_gui_tools/csnstaevdisp/csnstaevdisp" "$TMP/bin/csnstaevdisp"
cp "$TMP/root/ew_gui_tools/wavestore/wstore_ingest"   "$TMP/bin/wstore_ingest"
cp "$TMP/root/ew_gui_tools/wavestore/wstore_query"    "$TMP/bin/wstore_query"

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

# T7: el inventario NO está literal en el script (se deriva de las recetas)
if grep -q 'csnmags:csnmags' "$CHECK" || grep -q 'wavestore:wstore_ingest' "$CHECK"; then
    ko "T7 el inventario sigue literal en check_install.sh"
else
    ok "T7 inventario derivado (sin lista literal)"
fi
if grep -q 'install_bin' "$CHECK"; then
    ok "T7 deriva de la receta de instalación"
else
    ko "T7 no lee la receta de instalación"
fi

# T8: un módulo que instala VARIOS binarios (los dos se comprueban)
printf 'vieja\n' > "$TMP/bin/wstore_query"
out="$(run)"; rc=$?
if [ "$rc" -eq 1 ] && grep -q 'DESACTUALIZADO .*wstore_query' <<<"$out"; then
    ok "T8 varios binarios por módulo"
else
    ko "T8 rc=$rc [$out]"
fi
cp "$TMP/root/ew_gui_tools/wavestore/wstore_query" "$TMP/bin/wstore_query"

# T9: sin ninguna receta de instalación -> no hay nada que comprobar (exit 2)
mkdir -p "$TMP/nada/ew_gui_tools/csnloc" "$TMP/nadabin"
printf 'csnloc\n' > "$TMP/nada/ew_gui_tools/csnloc/csnloc"
EWGUI_REPO_ROOT="$TMP/nada" EWGUI_INSTALL_DST="$TMP/nadabin" sh "$CHECK" >/dev/null 2>&1
rc=$?
if [ "$rc" -eq 2 ]; then ok "T9 sin recetas (rc 2)"; else ko "T9 rc=$rc (esperado 2)"; fi

if [ "$fail" -eq 0 ]; then echo "ALL INSTALL CHECK TESTS PASSED"; else echo "HAY FALLOS"; fi
exit $fail
