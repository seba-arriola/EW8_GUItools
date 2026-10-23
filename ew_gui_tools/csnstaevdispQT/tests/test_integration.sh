#!/bin/bash
# Integracion de csnstaevdispQT con la configuracion del worktree.
#
# Esquema decidido: SUSTITUCION. csnstaevdispQT corre con el .d del GTK
# (csnstaevdisp.d, MOD_STAEVDISP 161) y el Process del GTK queda retirado de
# startstop_unix.d. No hay id nuevo ni .d propio; el log sigue siendo propio
# (csnstaevdispQT_<fecha>.log).
set -u

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
DIR="$ROOT/ew_gui_tools/csnstaevdispQT"
PARAMS="$ROOT/run_working_v8/params"
base="$PARAMS/earthworm.d.base"
gen="$PARAMS/earthworm.d"
d="$PARAMS/csnstaevdisp.d"
ss="$PARAMS/startstop_unix.d"
fail=0

ok()  { echo "ok  : $1"; }
bad() { echo "FAIL: $1"; fail=1; }

# --- 1. earthworm.d: MOD_STAEVDISP 161 --------------------------------------
if [ ! -f "$base" ]; then
    bad "falta $base"
else
    grep -qE '^[[:space:]]*Module[[:space:]]+MOD_STAEVDISP[[:space:]]+161' "$base" \
        && ok "MOD_STAEVDISP 161 declarado" || bad "falta MOD_STAEVDISP 161"
fi
if [ ! -f "$gen" ]; then
    bad "falta $gen (worktree_bootstrap.sh . --refresh)"
else
    grep -qE '^[[:space:]]*Module[[:space:]]+MOD_STAEVDISP[[:space:]]+161' "$gen" \
        && ok "earthworm.d generado tiene MOD_STAEVDISP" \
        || bad "earthworm.d sin MOD_STAEVDISP (falta refresh)"
fi

# --- 2. .d compartido con el GTK -------------------------------------------
if [ ! -f "$d" ]; then
    bad "falta $d"
else
    ok "existe csnstaevdisp.d"
    grep -qE '^[[:space:]]*MyModuleId[[:space:]]+MOD_STAEVDISP([[:space:]]|$)' "$d" \
        && ok "MyModuleId = MOD_STAEVDISP" || bad "MyModuleId incorrecto"
    grep -qE '^[[:space:]]*RingName[[:space:]]+HYPO_RING' "$d" \
        && ok "RingName = HYPO_RING" || bad "RingName incorrecto"
    grep -qE '^[[:space:]]*MapImageFile[[:space:]]+' "$d" \
        && ok "declara MapImageFile" || bad "sin MapImageFile"
    grep -qE '^[[:space:]]*StaFile[[:space:]]+' "$d" \
        && ok "declara StaFile" || bad "sin StaFile"
    grep -qE '^[[:space:]]*StationSize[[:space:]]+' "$d" \
        && ok "declara StationSize" || bad "sin StationSize"
    grep -qE '^[[:space:]]*LogFile[[:space:]]+[012]' "$d" \
        && ok "declara LogFile" || bad "sin LogFile"
fi
[ ! -f "$PARAMS/csnstaevdispqt.d" ] \
    && ok "no hay csnstaevdispqt.d" \
    || bad "csnstaevdispqt.d no debe existir (esquema de sustitucion)"

# --- 3. startstop: csnstaevdispQT con csnstaevdisp.d -----------------------
if [ ! -f "$ss" ]; then
    bad "falta $ss"
else
    if grep -qE '^[[:space:]]*Process[[:space:]]+"csnstaevdispQT[[:space:]]+csnstaevdisp\.d"' "$ss"; then
        ok "startstop declara csnstaevdispQT csnstaevdisp.d"
        grep -A1 -E '^[[:space:]]*Process[[:space:]]+"csnstaevdispQT[[:space:]]+csnstaevdisp\.d"' "$ss" \
            | grep -q 'Class/Priority' \
            && ok "el Process de csnstaevdispQT lleva Class/Priority" \
            || bad "el Process de csnstaevdispQT no lleva Class/Priority"
    else
        bad "startstop no declara csnstaevdispQT con csnstaevdisp.d (cambio del usuario pendiente)"
    fi
    if grep -qE '^[[:space:]]*Process[[:space:]]+"csnstaevdisp[[:space:]]+csnstaevdisp\.d"' "$ss"; then
        bad "hay un Process ACTIVO de csnstaevdisp GTK (se duplicaria el visor)"
    else
        ok "sin Process activo de csnstaevdisp GTK"
    fi
fi

# --- 4. binario instalado y flags de validacion ----------------------------
if [ -n "${EW_HOME:-}" ] && [ -x "$EW_HOME/${EW_VERSION:-}/bin/csnstaevdispQT" ]; then
    ok "binario instalado en ew_8.0/bin"
else
    bad "csnstaevdispQT no esta instalado en ew_8.0/bin"
fi
grep -qE '^VALIDATE_FLAGS[^#]*csnstaevdisp\.d' "$DIR/Makefile" \
    && ok "VALIDATE_FLAGS usa csnstaevdisp.d" \
    || bad "VALIDATE_FLAGS no usa csnstaevdisp.d"

if [ "$fail" -eq 0 ]; then
    echo "OK: integracion de csnstaevdispQT (csnstaevdisp.d/161 y startstop)"
else
    echo "FALLAS: revisar los FAIL de arriba"
fi
exit "$fail"
