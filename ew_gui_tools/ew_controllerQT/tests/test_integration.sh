#!/bin/sh
# test_integration.sh - C16: ew_controllerQT esta enchufado al runtime sin
# tocar el del modulo GTK.
#
# Comprueba identidad (.d + earthworm.d), arranque declarado en startstop,
# inventario de binarios y que --print-config funciona sin DISPLAY.
set -u

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
params=$root/run_working_v8/params
bin=$here/../ew_controllerQT
[ -x "$bin" ] || bin="$root/ew_8.0/bin/ew_controllerQT"

fail=0
nok=0
ok()  { echo "ok   $*"; nok=$((nok + 1)); }
bad() { echo "FALLA $*"; fail=$((fail + 1)); }

# --- 1. modid propio, sin pisar MOD_CONTROL --------------------------------
base=$params/earthworm.d.base
if [ ! -f "$base" ]; then
    bad "falta $base"
else
    if grep -qE '^[[:space:]]*Module[[:space:]]+MOD_EW_CONTROLLERQT[[:space:]]+173([[:space:]]|$)' "$base"; then
        ok "earthworm.d.base declara MOD_EW_CONTROLLERQT 173"
    else
        bad "earthworm.d.base no declara MOD_EW_CONTROLLERQT 173"
    fi
    # El id 173 no puede estar repetido (GetModId por nombre y por numero).
    n=$(grep -cE '^[[:space:]]*Module[[:space:]]+[A-Za-z_]+[[:space:]]+173([[:space:]]|$)' "$base")
    [ "$n" -eq 1 ] && ok "el id 173 es unico" || bad "el id 173 aparece $n veces"
    grep -qE '^[[:space:]]*Module[[:space:]]+MOD_CONTROL[[:space:]]+160([[:space:]]|$)' "$base" \
        && ok "MOD_CONTROL 160 sigue siendo el del modulo GTK" \
        || bad "MOD_CONTROL 160 desaparecio"
fi

# --- 2. .d propio ----------------------------------------------------------
d=$params/ew_controllerqt.d
if [ ! -f "$d" ]; then
    bad "falta $d"
else
    ok "existe ew_controllerqt.d"
    grep -qE '^[[:space:]]*MyModuleId[[:space:]]+MOD_EW_CONTROLLERQT([[:space:]]|$)' "$d" \
        && ok "MyModuleId = MOD_EW_CONTROLLERQT" || bad "MyModuleId incorrecto"
    grep -qE '^[[:space:]]*Ring[[:space:]]+CONTROL_RING([[:space:]]|$)' "$d" \
        && ok "usa CONTROL_RING (mismo anillo, modid distinto)" || bad "Ring incorrecto"
    grep -qE '^[[:space:]]*PollInt[[:space:]]+[0-9]+' "$d" \
        && ok "declara PollInt" || bad "falta PollInt"
    # No debe declarar rutas de escritura en params (AGENTS §7).
    if grep -qE '^[[:space:]]*(WorkDir|OutDir|StateDir)' "$d"; then
        bad "ew_controllerqt.d declara rutas de escritura"
    else
        ok "sin rutas de escritura en el .d"
    fi
fi

# --- 3. startstop -----------------------------------------------------------
# El port puede correr como panel PRINCIPAL —reusa `ew_controller.d`, mismo
# modid 160, con el Process del GTK comentado— o en PARALELO con su `.d` propio
# (`ew_controllerqt.d`, id 173): lo elige `startstop_unix.d`. Lo que no puede
# pasar es que apunte a un `.d` ausente ni que se pierda el Class/Priority.
ss=$params/startstop_unix.d
if [ ! -f "$ss" ]; then
    bad "falta $ss"
else
    line=$(grep -E '^[[:space:]]*Process[[:space:]]+"ew_controllerQT[[:space:]]' "$ss" | head -n 1)
    if [ -z "$line" ]; then
        bad "startstop no declara ew_controllerQT"
    else
        ok "startstop declara el Process del port Qt"
        cfg=$(printf '%s\n' "$line" | awk -F'"' '{print $2}' | awk '{print $2}')
        if [ -n "$cfg" ] && [ -f "$params/$cfg" ]; then
            ok "el Process usa un .d existente ($cfg)"
        else
            bad "el Process apunta a un .d ausente: $cfg"
        fi
        grep -A1 -E '^[[:space:]]*Process[[:space:]]+"ew_controllerQT' "$ss" \
            | grep -q 'Class/Priority' \
            && ok "el Process lleva Class/Priority" \
            || bad "el Process no lleva Class/Priority"
    fi
fi

# --- 4. inventario de binarios ---------------------------------------------
if [ -x "$root/ew_gui_tools/common/tools/check_install.sh" ]; then
    if sh "$root/ew_gui_tools/common/tools/check_install.sh" -v 2>/dev/null |
        grep -qE '^OK[[:space:]]+ew_controllerQT'; then
        ok "check_install reporta OK ew_controllerQT"
    else
        # Puede faltar la instalacion si solo se corrio `make check`.
        sh "$root/ew_gui_tools/common/tools/check_install.sh" -v 2>/dev/null |
            grep -qE 'FALTA.*ew_controllerQT|DESACTUALIZADO.*ew_controllerQT' \
            && bad "check_install: ew_controllerQT sin instalar o desactualizado (make all)" \
            || bad "check_install no conoce ew_controllerQT"
    fi
fi

grep -qxF '/ew_8.0/bin/ew_controllerQT' "$root/.gitignore" \
    && ok ".gitignore ignora /ew_8.0/bin/ew_controllerQT" \
    || bad ".gitignore no ignora el binario instalado"

# --- 5. --print-config sin DISPLAY -----------------------------------------
if [ -x "$bin" ] && [ -f "$d" ]; then
    out=$(cd "$params" && env -u DISPLAY -u WAYLAND_DISPLAY \
          EW_PARAMS="$params" "$bin" --print-config ew_controllerqt.d 2>/dev/null)
    rc=$?
    if [ "$rc" -eq 0 ]; then
        ok "--print-config sin DISPLAY (rc=0)"
        printf '%s\n' "$out" | grep -qE '^MyModuleId[[:space:]]+MOD_EW_CONTROLLERQT$' \
            && ok "print-config saca MyModuleId" || bad "print-config no saca MyModuleId"
        printf '%s\n' "$out" | grep -qE '^Ring[[:space:]]+CONTROL_RING$' \
            && ok "print-config saca Ring" || bad "print-config no saca Ring"
        n=$(printf '%s\n' "$out" | wc -l)
        [ "$n" -eq 6 ] && ok "print-config saca las 6 claves" || bad "print-config saca $n lineas"
    else
        bad "--print-config sin DISPLAY fallo (rc=$rc)"
    fi
fi

echo "== integracion checks=$((nok + fail)) fallas=$fail =="
[ "$fail" -eq 0 ] || exit 1
