#!/bin/bash
# Integracion de csnrvQT con la configuracion del worktree.
#
# Esquema decidido: SUSTITUCION. csnrvQT corre con el .d del GTK (csnrv.d,
# MOD_CSNRV 159) y el Process del csnrv GTK queda comentado en startstop_unix.d.
# El id MOD_CSNRVQT 174 y el viejo csnrvqt.d ya no existen; el log sigue siendo
# propio (csnrvQT_<fecha>.log), asi que no se mezcla con el del GTK.
set -u

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
DIR="$ROOT/ew_gui_tools/csnrvQT"
PARAMS="$ROOT/run_working_v8/params"
base="$PARAMS/earthworm.d.base"
gen="$PARAMS/earthworm.d"
d="$PARAMS/csnrv.d"
ss="$PARAMS/startstop_unix.d"
fail=0

ok()  { echo "ok  : $1"; }
bad() { echo "FAIL: $1"; fail=1; }

# --- 1. earthworm.d: MOD_CSNRV 159 y sin rastro del 174 --------------------
if [ ! -f "$base" ]; then
    bad "falta $base"
else
    grep -qE '^[[:space:]]*Module[[:space:]]+MOD_CSNRV[[:space:]]+159' "$base" \
        && ok "MOD_CSNRV 159 declarado" || bad "falta MOD_CSNRV 159"
    n=$(grep -cE '^[[:space:]]*Module[[:space:]]+[A-Za-z_]+[[:space:]]+174([[:space:]]|$)' "$base")
    [ "$n" -eq 0 ] && ok "el id 174 ya no existe" \
                   || bad "el id 174 sigue en earthworm.d.base ($n)"
    if grep -q 'MOD_CSNRVQT' "$base"; then
        bad "MOD_CSNRVQT sigue en earthworm.d.base"
    else
        ok "sin MOD_CSNRVQT (esquema de sustitucion)"
    fi
fi

# El generado se deriva del base: si no coincide, falta worktree_bootstrap.
if [ ! -f "$gen" ]; then
    bad "falta $gen (worktree_bootstrap.sh . --refresh)"
else
    grep -qE '^[[:space:]]*Module[[:space:]]+MOD_CSNRV[[:space:]]+159' "$gen" \
        && ok "earthworm.d generado tiene MOD_CSNRV" \
        || bad "earthworm.d sin MOD_CSNRV (falta refresh)"
    if grep -qE '^[[:space:]]*Module[[:space:]]+[A-Za-z_]+[[:space:]]+174([[:space:]]|$)' "$gen"; then
        bad "earthworm.d generado conserva el id 174 (falta refresh)"
    else
        ok "earthworm.d generado sin el id 174"
    fi
fi

# --- 2. .d compartido con el GTK -------------------------------------------
if [ ! -f "$d" ]; then
    bad "falta $d"
else
    ok "existe csnrv.d"
    grep -qE '^[[:space:]]*MyModuleId[[:space:]]+MOD_CSNRV([[:space:]]|$)' "$d" \
        && ok "MyModuleId = MOD_CSNRV" || bad "MyModuleId incorrecto"
    grep -qE '^[[:space:]]*RingName[[:space:]]+HYPO_RING_REF' "$d" \
        && ok "RingName = HYPO_RING_REF" || bad "RingName incorrecto"
    grep -qE '^[[:space:]]*MapImageFile[[:space:]]+' "$d" \
        && ok "declara MapImageFile" || bad "sin MapImageFile"
    grep -qE '^[[:space:]]*LogFile[[:space:]]+[01]' "$d" \
        && ok "declara LogFile" || bad "sin LogFile"
fi
[ ! -f "$PARAMS/csnrvqt.d" ] \
    && ok "no queda csnrvqt.d" || bad "csnrvqt.d deberia estar retirado"

# --- 3. startstop: csnrvQT con csnrv.d y el GTK comentado ------------------
if [ ! -f "$ss" ]; then
    bad "falta $ss"
else
    grep -qE '^[[:space:]]*Process[[:space:]]+"csnrvQT[[:space:]]+csnrv\.d"' "$ss" \
        && ok "startstop declara csnrvQT csnrv.d" \
        || bad "startstop no declara csnrvQT con csnrv.d"
    grep -A1 -E '^[[:space:]]*Process[[:space:]]+"csnrvQT[[:space:]]+csnrv\.d"' "$ss" \
        | grep -q 'Class/Priority' \
        && ok "el Process de csnrvQT lleva Class/Priority" \
        || bad "el Process de csnrvQT no lleva Class/Priority"
    # El GTK puede estar comentado o retirado; lo que NO puede es estar activo,
    # o habria dos visores sobre el mismo anillo.
    if grep -qE '^[[:space:]]*Process[[:space:]]+"csnrv[[:space:]]+csnrv\.d"' "$ss"; then
        bad "hay un Process ACTIVO de csnrv GTK (se duplicaria el visor)"
    else
        ok "sin Process activo de csnrv GTK (retirado o comentado)"
    fi
fi

# --- 4. binario instalado y flags de validacion ----------------------------
if [ -n "${EW_HOME:-}" ] && [ -x "$EW_HOME/${EW_VERSION:-}/bin/csnrvQT" ]; then
    ok "binario instalado en ew_8.0/bin"
else
    bad "csnrvQT no esta instalado en ew_8.0/bin"
fi
grep -qE '^VALIDATE_FLAGS[^#]*csnrv\.d' "$DIR/Makefile" \
    && ok "VALIDATE_FLAGS usa csnrv.d" || bad "VALIDATE_FLAGS no usa csnrv.d"
if grep -q 'csnrvqt\.d' "$DIR/Makefile" "$DIR/README.md" 2>/dev/null; then
    bad "Makefile/README siguen citando csnrvqt.d"
else
    ok "Makefile y README sin csnrvqt.d"
fi

if [ "$fail" -eq 0 ]; then
    echo "OK: integracion de csnrvQT (csnrv.d/159, sustitucion y startstop)"
else
    echo "FALLAS: revisar los FAIL de arriba"
fi
exit "$fail"
