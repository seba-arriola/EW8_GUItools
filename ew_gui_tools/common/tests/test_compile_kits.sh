#!/bin/bash
# Invariante de build: la clasificacion de toolkit es DERIVADA del Makefile de
# cada modulo, no una lista en ew8_compile.sh.
#
# Por que: `ew8_compile.sh` marcaba GUI con `grep -qE 'gtk4|libadwaita'` en el
# Makefile, asi que un modulo Qt caia en `headless` y no disparaba su preflight.
# El port de csnhypodbpQT lo rodeo con un comentario-mentira `MOD_GUI: qt6`
# (docs/testLibQT.md §5). Ahora el Makefile declara `MOD_KIT = gtk4|qt6` y el
# grep queda solo como fallback para los que aun no lo declaran.
#
# Verifica:
#   (a) `--list` expone la columna KIT y clasifica bien los modulos reales;
#   (b) `libs` / `gui` / `headless` (--dry-run, sin compilar nada) seleccionan
#       por kit y nunca meten una libreria en `gui`/`headless`;
#   (c) ningun `MOD_KIT` declarado es un valor distinto de gtk4|qt6.
set -u

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
EC="$ROOT/ew8_compile.sh"
fail=0

ok()  { echo "ok  : $1"; }
bad() { echo "FAIL: $1"; fail=1; }

# --- (a) clasificacion -------------------------------------------------------
LIST=$("$EC" --list 2>&1)
if [ $? -ne 0 ]; then
    bad "--list fallo"
    printf '%s\n' "$LIST"
    exit 1
fi
if ! printf '%s\n' "$LIST" | head -n1 | grep -q 'KIT'; then
    bad "--list no expone la columna KIT"
fi

kit_of() { printf '%s\n' "$LIST" | awk -v m="$1" '$1==m {print $NF}'; }

expect_kit() {  # expect_kit <modulo> <kit>
    local got
    got=$(kit_of "$1")
    if [ "$got" = "$2" ]; then
        ok "$1 -> $2"
    else
        bad "$1 -> '${got:-<ausente>}' (esperado $2)"
    fi
}

expect_kit csnhypodbp   gtk4
expect_kit ew_controller gtk4
expect_kit common        gtk4
expect_kit csnloc        -
expect_kit csnhypodbpQT  qt6
# Solo si existe: las fases 0 crean el modulo Qt compartido.
if [ -f "$ROOT/ew_gui_tools/commonqt/Makefile" ]; then
    expect_kit commonqt qt6
fi
if [ -f "$ROOT/ew_gui_tools/ew_controllerQT/Makefile" ]; then
    expect_kit ew_controllerQT qt6
fi

# Todo KIT de la tabla debe ser uno de los tres valores (nada de basura).
# Solo se miran filas cuyo primer campo es un directorio real de ew_gui_tools/:
# la tabla termina con lineas de ayuda ("grupos:", "verbos:", ...).
MODS=$(find "$ROOT/ew_gui_tools" -mindepth 1 -maxdepth 1 -type d -printf '%f\n' | sort | paste -sd'|' -)
BADKIT=$(printf '%s\n' "$LIST" | awk -v mods="^($MODS)\$" \
    'NR>2 && $1 ~ mods && $NF!="gtk4" && $NF!="qt6" && $NF!="-"' | head -n5)
if [ -n "$BADKIT" ]; then
    bad "la columna KIT tiene valores no reconocidos:"
    printf '%s\n' "$BADKIT"
else
    ok "la columna KIT solo usa gtk4|qt6|-"
fi

# --- (b) seleccion por grupo (--dry-run: no compila ni exige GTK/Qt) ---------
sel() {  # sel <grupo>  -> nombres de modulo seleccionados
    "$EC" "$1" --build --dry-run 2>&1 | awk '/^\[build\]/ {print $2}'
}

has() { printf '%s\n' "$2" | grep -qxF "$1"; }

G=$(sel gui)
has csnhypodbpQT "$G" && ok "gui incluye csnhypodbpQT (qt6)" || bad "gui NO incluye csnhypodbpQT"
has ew_controller "$G" && ok "gui incluye ew_controller (gtk4)" || bad "gui NO incluye ew_controller"
if [ -f "$ROOT/ew_gui_tools/ew_controllerQT/Makefile" ]; then
    has ew_controllerQT "$G" && ok "gui incluye ew_controllerQT (qt6)" || bad "gui NO incluye ew_controllerQT"
fi
has common "$G" && bad "gui incluye la libreria common" || ok "gui no incluye librerias"

H=$(sel headless)
has csnhypodbpQT "$H" && bad "headless incluye csnhypodbpQT" || ok "headless excluye los modulos con kit"
has ew_controller "$H" && bad "headless incluye ew_controller" || ok "headless excluye ew_controller"
has csnloc "$H" && ok "headless incluye csnloc" || bad "headless NO incluye csnloc"

L=$(sel libs)
has common "$L" && ok "libs incluye common" || bad "libs NO incluye common"
if [ -f "$ROOT/ew_gui_tools/commonqt/Makefile" ]; then
    has commonqt "$L" && ok "libs incluye commonqt" || bad "libs NO incluye commonqt"
fi

# --- (c) valores declarados ---------------------------------------------------
BADVAL=$(
    find "$ROOT/ew_gui_tools" -maxdepth 2 -name Makefile -print | while IFS= read -r mk; do
        sed -n 's/^[[:space:]]*MOD_KIT[[:space:]]*[:?]*=[[:space:]]*\(.*\)$/\1/p' "$mk" |
        while IFS= read -r v; do
            case "$v" in
                gtk4|qt6) ;;
                *) printf '%s: MOD_KIT=%s\n' "${mk#"$ROOT"/}" "$v" ;;
            esac
        done
    done
)
if [ -n "$BADVAL" ]; then
    bad "MOD_KIT solo admite gtk4|qt6:"
    printf '%s\n' "$BADVAL"
else
    ok "todo MOD_KIT declarado es gtk4|qt6"
fi

if [ "$fail" -eq 0 ]; then
    echo "OK: clasificacion de kits derivada y coherente"
fi
exit "$fail"
