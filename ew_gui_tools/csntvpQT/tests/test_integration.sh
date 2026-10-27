#!/bin/bash
# Integracion en EarthWorm: identidad propia, .d propio y convivencia con el
# GTK mientras dure la etapa 1.
#
# OJO: `earthworm.d` es GENERADO (ignorado por git) a partir de
# `earthworm.d.base`; este test solo exige el .base (versionado) y avisa si el
# generado aun no se refresco.
set -u

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
PARAMS="$ROOT/run_working_v8/params"
BASE="$PARAMS/earthworm.d.base"
GEN="$PARAMS/earthworm.d"
STARTSTOP="$PARAMS/startstop_unix.d"
QTD="$PARAMS/csntvpqt.d"
GTKD="$PARAMS/csntvp.d"
BIN="$ROOT/ew_gui_tools/csntvpQT/csntvpQT"
fail=0

ok()  { echo "ok  : $1"; }
bad() { echo "FAIL: $1"; fail=1; }

# --- 1. identidad -----------------------------------------------------------
if grep -qE '^[[:space:]]*Module[[:space:]]+MOD_CSNTVPQT[[:space:]]+175([[:space:]]|$)' "$BASE"; then
    ok "MOD_CSNTVPQT 175 declarado en earthworm.d.base"
else
    bad "falta 'Module MOD_CSNTVPQT 175' en earthworm.d.base"
fi

# 174 lo gatea csnrvQT (su id se retiro): no se puede reutilizar.
if grep -qE '^[[:space:]]*Module[[:space:]]+[A-Za-z_]+[[:space:]]+174([[:space:]]|$)' "$BASE"; then
    bad "el id 174 esta en uso (csnrvQT exige que siga libre)"
else
    ok "el id 174 sigue libre"
fi

if grep -qE '^[[:space:]]*Module[[:space:]]+[A-Za-z_]+[[:space:]]+175([[:space:]]|$)' "$BASE" \
        && ! grep -qE '^[[:space:]]*Module[[:space:]]+MOD_CSNTVPQT' "$BASE"; then
    bad "dos nombres con el id 175"
else
    ok "el id 175 no esta duplicado"
fi

# --- 2. .d propio, solo con las claves declaradas de diferencia -----------
if [ -f "$QTD" ]; then
    ok "existe params/csntvpqt.d"
else
    bad "falta params/csntvpqt.d (debe versionarse)"
fi

if [ -f "$QTD" ] && grep -qE '^MyModuleId[[:space:]]+MOD_CSNTVPQT' "$QTD"; then
    ok "csntvpqt.d usa MOD_CSNTVPQT"
else
    bad "csntvpqt.d no declara MOD_CSNTVPQT"
fi

if [ -f "$GTKD" ] && grep -qE '^MyModuleId[[:space:]]+MOD_CSNTVP([[:space:]]|$)' "$GTKD"; then
    ok "csntvp.d sigue siendo del GTK (MOD_CSNTVP)"
else
    bad "csntvp.d fue modificado (debe quedar intacto)"
fi

norm() {
    sed -e 's/#.*//' "$1" \
        | sed -e 's/[[:space:]]*$//' -e 's/^[[:space:]]*//' \
        | grep -v '^$' \
        | grep -vE '^(MyModuleId|zColor|nColor|eColor)([[:space:]]|$)'
}
if [ -f "$QTD" ] && [ -f "$GTKD" ]; then
    if diff -q <(norm "$GTKD") <(norm "$QTD") >/dev/null; then
        ok "csntvpqt.d = csntvp.d + MyModuleId/zColor/nColor/eColor"
    else
        bad "csntvpqt.d difiere del GTK en claves no declaradas"
        diff <(norm "$GTKD") <(norm "$QTD") | head -20
    fi
fi

# --- 3. Process: convivencia (etapa 1) ------------------------------------
if grep -qE '^[[:space:]]*Process[[:space:]]+"csntvpQT[[:space:]]+csntvpqt\.d"' "$STARTSTOP"; then
    ok 'Process "csntvpQT csntvpqt.d" declarado'
else
    bad "falta el Process de csntvpQT en startstop_unix.d"
fi
if grep -qE '^[[:space:]]*Process[[:space:]]+"csntvp[[:space:]]+csntvp\.d"' "$STARTSTOP"; then
    ok 'Process "csntvp csntvp.d" sigue activo (etapa 1 en paralelo)'
else
    bad "el Process del GTK desaparecio (etapa 1 debe ser en paralelo)"
fi
if grep -qE '^[[:space:]]*Process[[:space:]]+"csntvpQT' "$STARTSTOP" \
        && ! grep -q 'csntvpqt\.d' "$STARTSTOP"; then
    bad "csntvpQT no apunta a csntvpqt.d"
fi

# --- 4. binario instalado --------------------------------------------------
if [ -x "$BIN" ]; then
    ok "binario csntvpQT construido"
else
    bad "falta el binario (make antes del test)"
fi
if [ -x "$ROOT/ew_8.0/bin/csntvpQT" ] || [ -x "${EW_HOME:-}/bin/csntvpQT" ]; then
    ok "binario instalado en ew_8.0/bin"
else
    echo "skip: binario no instalado todavia (make install)"
fi

# --- 5. earthworm.d generado ----------------------------------------------
if [ -f "$GEN" ]; then
    if grep -qE '^[[:space:]]*Module[[:space:]]+MOD_CSNTVPQT[[:space:]]+175' "$GEN"; then
        ok "earthworm.d generado resuelve MOD_CSNTVPQT"
    else
        bad "earthworm.d no tiene MOD_CSNTVPQT: corre worktree_bootstrap.sh . --refresh"
    fi
else
    echo "skip: earthworm.d no generado en este worktree"
fi

if [ "$fail" -eq 0 ]; then
    echo "OK: integracion de csntvpQT"
else
    echo "FALLAS: revisar los FAIL de arriba"
fi
exit "$fail"
