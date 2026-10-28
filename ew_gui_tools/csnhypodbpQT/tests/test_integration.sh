#!/bin/sh
# test_integration.sh - fase 6.5: el prototipo Qt esta integrado como modulo
# propio (MOD_CSNHYPODBPQT 172) sin tocar el modulo GTK.
#
# Verifica: identidad en earthworm.d.base y en el .d vivo, el .d propio
# (csnhypodbpqt.d) y sus 3 claves, que solo difiera del GTK en esas claves,
# el Process de startstop, el runtime ignorado y el .d en check_install.
#
# No abre anillos: NO necesita DISPLAY ni EarthWorm en marcha.
# Uso: sh tests/test_integration.sh

set -u

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
params=$root/run_working_v8/params
base=$params/earthworm.d.base
gen=$params/earthworm.d
cfg=$params/csnhypodbpqt.d
cfg_gtk=$params/csnhypodbp.d
ss=$params/startstop_unix.d
bin=$here/../csnhypodbpQT
inst=$root/ew_8.0/bin/csnhypodbpQT

pass=0
fail=0
ok()  { pass=$((pass + 1)); echo "ok   $*"; }
bad() { fail=$((fail + 1)); echo "FALLA $*"; }
skip() { echo "SKIP $*"; }

# --- 1. identidad del modulo ------------------------------------------------
if [ -f "$base" ]; then
    ok "existe earthworm.d.base"
    if grep -qE '^[[:space:]]*Module[[:space:]]+MOD_CSNHYPODBPQT[[:space:]]+172([[:space:]]|$)' "$base"; then
        ok "earthworm.d.base declara MOD_CSNHYPODBPQT 172"
    else
        bad "earthworm.d.base no declara MOD_CSNHYPODBPQT 172"
    fi
else
    bad "falta run_working_v8/params/earthworm.d.base"
fi

if [ -f "$gen" ]; then
    if grep -qE '^[[:space:]]*Module[[:space:]]+MOD_CSNHYPODBPQT[[:space:]]+172([[:space:]]|$)' "$gen"; then
        ok "earthworm.d vivo hereda MOD_CSNHYPODBPQT 172"
    else
        bad "earthworm.d vivo sin MOD_CSNHYPODBPQT 172 (corre worktree_bootstrap.sh --refresh)"
    fi
else
    skip "no hay earthworm.d vivo (lo genera worktree_bootstrap.sh)"
fi

# --- 2. .d propio -----------------------------------------------------------
if [ -f "$cfg" ] && [ -f "$cfg_gtk" ]; then
    ok "existen csnhypodbpqt.d y csnhypodbp.d"
else
    bad "falta csnhypodbpqt.d o csnhypodbp.d"
fi

if [ -f "$cfg" ]; then
    if grep -qE '^[[:space:]]*MyModuleId[[:space:]]+MOD_CSNHYPODBPQT([[:space:]]|$)' "$cfg"; then
        ok "csnhypodbpqt.d usa MyModuleId MOD_CSNHYPODBPQT"
    else
        bad "csnhypodbpqt.d no usa MyModuleId MOD_CSNHYPODBPQT"
    fi
    if grep -qE '^ManualStateFile[[:space:]]+\.\./csnhypodbpqt/' "$cfg"; then
        ok "ManualStateFile va al runtime propio (../csnhypodbpqt/)"
    else
        bad "ManualStateFile no apunta a ../csnhypodbpqt/"
    fi
    if grep -qE '^MwwWorkDir[[:space:]]+\.\./csnhypodbpqt/mww([[:space:]]|$)' "$cfg"; then
        ok "MwwWorkDir va al runtime propio (../csnhypodbpqt/mww)"
    else
        bad "MwwWorkDir no apunta a ../csnhypodbpqt/mww"
    fi
fi

# Diff filtrado: sin comentarios y sin las 3 claves que difieren a proposito.
filter_cfg() {
    sed -e 's/#.*$//' -e '/^[[:space:]]*$/d' \
        -e '/^[[:space:]]*MyModuleId[[:space:]]/d' \
        -e '/^[[:space:]]*ManualStateFile[[:space:]]/d' \
        -e '/^[[:space:]]*MwwWorkDir[[:space:]]/d' \
        -e '/^[[:space:]]*locator[[:space:]]/d' "$1" |
        sed -e 's/[[:space:]][[:space:]]*/ /g' -e 's/^ //' -e 's/ $//'
}

if [ -f "$cfg" ] && [ -f "$cfg_gtk" ]; then
    a=$(mktemp)
    b=$(mktemp)
    trap 'rm -f "$a" "$b"' EXIT INT TERM
    filter_cfg "$cfg_gtk" > "$a"
    filter_cfg "$cfg" > "$b"
    if d=$(diff -u "$a" "$b"); then
        ok "csnhypodbpqt.d solo difiere en MyModuleId/ManualStateFile/MwwWorkDir"
    else
        bad "csnhypodbpqt.d difiere del GTK en algo mas que las 3 claves"
        printf '%s\n' "$d" | sed 's/^/       /'
    fi
    rm -f "$a" "$b"
    trap - EXIT INT TERM
fi

# --- 2b. tabla unica de locators (csnhypodbpqt.d) ---------------------------
if [ -f "$cfg" ]; then
    nloc=$(grep -c '^[[:space:]]*locator[[:space:]]' "$cfg" 2>/dev/null || true)
    if [ "$nloc" -ge 1 ] && \
       ! grep -E '^[[:space:]]*locator[[:space:]]' "$cfg" | \
         grep -qvE '^[[:space:]]*locator[[:space:]]+[^[:space:]]+[[:space:]]+[^[:space:]]+[[:space:]]+[01][[:space:]]+[0-9]+[[:space:]]+[0-9]+[[:space:]]*$'; then
        ok "tabla unica de locators ($nloc filas; formato valido)"
    else
        bad "csnhypodbpqt.d no tiene una tabla de locators valida"
    fi
    for f in "$cfg_gtk" "$here/../../../run_working_v8/params/csnloc.d" \
             "$here/../../../run_working_v8/params/hyp2000_ring.d" \
             "$here/../../../run_working_v8/params/nlloc_ring.d"; do
        if [ -f "$f" ] && grep -qE '^[[:space:]]*(locator|LocatorTable|ModuleLabel)[[:space:]]' "$f"; then
            bad "$(basename "$f") declara locators/etiquetas: debe leer csnhypodbpqt.d"
        fi
    done
    if grep -qE '^[[:space:]]*InPickRing[[:space:]]+PICK_RING' "$here/../../../run_working_v8/params/hyp2000_ring.d" &&
       grep -qE '^[[:space:]]*OutRing[[:space:]]+HYPO_RING_REF' "$here/../../../run_working_v8/params/hyp2000_ring.d" &&
       grep -qE '^[[:space:]]*OutRing[[:space:]]+HYPO_RING' "$here/../../../run_working_v8/params/csnloc.d"; then
        ok "anillos: csnloc->HYPO_RING, hyp2000 PICK_RING->HYPO_RING_REF"
    else
        bad "anillos del pipeline no coinciden"
    fi
fi

# --- 3. startstop -----------------------------------------------------------
if [ -f "$ss" ]; then
    if grep -qE '^[[:space:]]*Process[[:space:]]+"csnhypodbpQT csnhypodbpqt\.d"' "$ss"; then
        ok "startstop_unix.d declara el Process del port Qt (activo)"
    else
        bad "startstop_unix.d no declara Process \"csnhypodbpQT csnhypodbpqt.d\" activo"
    fi
    if grep -A1 -E '^[[:space:]]*Process[[:space:]]+"csnhypodbpQT' "$ss" | grep -q 'Class/Priority'; then
        ok "el Process del port Qt lleva Class/Priority"
    else
        bad "el Process del port Qt no lleva Class/Priority en la linea siguiente"
    fi
else
    bad "falta run_working_v8/params/startstop_unix.d"
fi

# --- 4. runtime ignorado ----------------------------------------------------
if grep -qE '^/?run_working_v8/csnhypodbpqt/$' "$root/.gitignore"; then
    ok ".gitignore ignora /run_working_v8/csnhypodbpqt/"
else
    bad ".gitignore no ignora /run_working_v8/csnhypodbpqt/"
fi
if command -v git >/dev/null 2>&1; then
    if ( cd "$root" && git check-ignore -q run_working_v8/csnhypodbpqt/csnloc.events ); then
        ok "git check-ignore confirma el runtime del port Qt"
    else
        bad "git check-ignore NO ignora run_working_v8/csnhypodbpqt/"
    fi
else
    skip "sin git: no se puede comprobar check-ignore"
fi
if grep -q 'run_working_v8/csnhypodbpqt' "$root/ew_gui_tools/common/tests/test_params_stays_clean.sh"; then
    ok "test_params_stays_clean.sh cubre la carpeta runtime del port Qt"
else
    bad "test_params_stays_clean.sh no cubre run_working_v8/csnhypodbpqt"
fi

# --- 5. binario y su config (headless) -------------------------------------
runbin=$bin
[ -x "$runbin" ] || runbin=$inst
if [ -x "$runbin" ] && [ -f "$cfg" ]; then
    if out=$( cd "$params" && "$runbin" --print-config csnhypodbpqt.d 2>&1 ); then
        ok "csnhypodbpQT --print-config csnhypodbpqt.d (sin DISPLAY)"
        if printf '%s\n' "$out" | grep -qE '^MwwWorkDir[[:space:]]+\.\./csnhypodbpqt/mww$'; then
            ok "print-config del .d propio resuelve MwwWorkDir del runtime"
        else
            bad "print-config del .d propio no muestra MwwWorkDir ../csnhypodbpqt/mww"
        fi
    else
        bad "csnhypodbpQT --print-config csnhypodbpqt.d termino con error"
        printf '%s\n' "$out" | sed 's/^/       /'
    fi
else
    skip "csnhypodbpQT no construido: se omite --print-config"
fi

if [ -x "$inst" ]; then
    out=$(cd "$root" && sh ew_gui_tools/common/tools/check_install.sh -v 2>&1 || true)
    if printf '%s\n' "$out" | grep -qE '^OK[[:space:]]+csnhypodbpQT$'; then
        ok "check_install: csnhypodbpQT instalado y al dia"
    else
        bad "check_install no reporta OK csnhypodbpQT (hace falta 'make all')"
        printf '%s\n' "$out" | grep 'csnhypodbpQT' | sed 's/^/       /'
    fi
else
    skip "check_install: csnhypodbpQT aun no instalado"
fi

echo "----"
echo "== integracion checks=$((pass + fail)) fallas=$fail =="
[ "$fail" -eq 0 ] || exit 1
exit 0
