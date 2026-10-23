#!/bin/bash
# Invariantes de FUENTE de ew_controllerQT (sin compilar y sin DISPLAY).
#
# Son las reglas que se pagaron caro en los modulos anteriores:
#   - la capa C no conoce Qt y la capa Qt no conoce EarthWorm/GTK (frontera
#     c/ecq_capi.h, docs/testLibQT.md R1/R2);
#   - la identidad de un modulo es el PID, nunca el nombre (startstop_unix.d
#     tiene dos `Process` con el mismo nombre);
#   - la sensibilidad de los botones sale de ecq_can(), no de comparar textos;
#   - los selectores no son desplegables (WSLg #1299), son la ventana modal;
#   - el arnés usa TEST_CTRL_RING, jamas CONTROL_RING;
#   - el Makefile declara MOD_KIT=qt6, instala con install_bin.sh y su TARGET
#     usa `=` simple (ew8_compile.sh lo deriva con sed).
set -u

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
DIR="$ROOT/ew_gui_tools/ew_controllerQT"
fail=0

ok()  { echo "ok  : $1"; }
bad() { echo "FAIL: $1"; fail=1; }

hits() {  # hits <patron> <archivos...>
    local pat=$1
    shift
    grep -rEn "$pat" "$@" 2>/dev/null | grep -v '^[^:]*:[0-9]*:[[:space:]]*/\*' || true
}

# --- 1. Makefile -------------------------------------------------------------
MK="$DIR/Makefile"
grep -qE '^MOD_KIT[[:space:]]*=[[:space:]]*qt6' "$MK" \
    && ok "MOD_KIT = qt6" || bad "falta MOD_KIT = qt6"
grep -qE '^TARGET = ew_controllerQT[[:space:]]*$' "$MK" \
    && ok "TARGET = ew_controllerQT (= simple)" || bad "TARGET no es '= ew_controllerQT'"
grep -qE 'install_bin\.sh' "$MK" \
    && ok "instala con install_bin.sh" || bad "falta install_bin.sh"
if grep -qE '^[[:space:]]*#[[:space:]]*MOD_GUI:' "$MK"; then
    bad "vuelve a estar el marcador MOD_GUI: (usa MOD_KIT)"
else
    ok "sin marcador MOD_GUI:"
fi
grep -qE '^\$\(TARGET\):.*libewguiqt' "$MK" && ok "enlaza libewguiqt" \
    || grep -q 'commonqt/build/libewguiqt.a' "$MK" && ok "enlaza libewguiqt" \
    || bad "no enlaza libewguiqt.a"

# --- 2. frontera C / C++ -----------------------------------------------------
# Ningun .c incluye Qt.
if grep -rEn '#include[[:space:]]*[<"]Q[A-Za-z]' "$DIR/c" >/dev/null 2>&1; then
    bad "c/ incluye headers de Qt:"
    grep -rEn '#include[[:space:]]*[<"]Q[A-Za-z]' "$DIR/c" | head -5
else
    ok "c/ no incluye Qt"
fi

# Ningun .cxx incluye EarthWorm/GTK/cairo/libpq.
BADQ='gtk/gtk\.h|gdk/gdk\.h|adwaita|glib|gio/|cairo|earthworm|transport\.h|kom\.h|libpq|wstore|csn[a-z]+\.h'
H=$(grep -rEn '#include[[:space:]]*[<"][^>"]*' "$DIR/ui" 2>/dev/null | grep -E "$BADQ" || true)
if [ -n "$H" ]; then
    bad "ui/ incluye headers prohibidos:"
    printf '%s\n' "$H" | head -5
else
    ok "ui/ solo incluye Qt + ewguiqt + ecq_capi.h"
fi

# La unica puerta a C es ecq_capi.h (y los headers propios de ui/).
H=$(grep -rhoE '#include[[:space:]]*"[^"]+"' "$DIR/ui" 2>/dev/null |
    sed 's/.*"\(.*\)"/\1/' | grep -vE '^(ecq_capi|ecq_mainwindow|ewguiqt/)' | sort -u)
if [ -n "$H" ]; then
    bad "ui/ incluye headers que no son la frontera: $(printf '%s ' $H)"
else
    ok "ui/ cruza solo por ecq_capi.h y ewguiqt/"
fi

# --- 3. identidad por PID ----------------------------------------------------
if grep -rEn 'g_sel_name|g_log_name|sel_by_name|por_nombre' "$DIR/ui" "$DIR/c" >/dev/null 2>&1; then
    bad "aparece seleccion por nombre"
else
    ok "sin seleccion por nombre"
fi
grep -qE 'index\(idx\.row\(\), 1\)' "$DIR/ui/ecq_mainwindow.cxx" \
    && ok "la seleccion sale de la columna PID" || bad "selectedPid no lee la columna 1 (PID)"

# La sensibilidad de los botones sale de ecq_can().
for b in stop restart start reconfig; do
    grep -qE "m_btn[A-Za-z]+->setEnabled\(ecq_can\(" "$DIR/ui/ecq_mainwindow.cxx" \
        && break
done
[ "$(grep -c 'setEnabled(ecq_can(' "$DIR/ui/ecq_mainwindow.cxx")" -ge 4 ] \
    && ok "los 4 botones se gobiernan con ecq_can()" \
    || bad "alguna sensibilidad no sale de ecq_can()"

# --- 4. sin estado oculto ni desplegables -----------------------------------
if grep -rn 'QSettings' "$DIR/ui" >/dev/null 2>&1; then
    bad "QSettings: el modulo no debe persistir estado oculto"
else
    ok "sin QSettings"
fi
if grep -rn 'QComboBox' "$DIR/ui" >/dev/null 2>&1; then
    bad "QComboBox: los selectores son la ventana modal (WSLg #1299)"
else
    ok "sin QComboBox"
fi
grep -q 'ewguiqt::ListSelectDialog::pick' "$DIR/ui/ecq_mainwindow.cxx" \
    && ok "los selectores usan ListSelectDialog" || bad "no se usa ListSelectDialog"

# --- 5. arnés sobre el anillo propio ---------------------------------------
# Lo que no puede pasar: publicar en el anillo de produccion, ni declararse
# MOD_CONTROL (la identidad del modulo GTK). Nombrar CONTROL_RING para
# comprobar que NO aparece en un volcado si es legitimo.
H=$(grep -rnE --exclude=test_ui_invariants.sh \
        'Ring[[:space:]]+CONTROL_RING|MyModuleId[[:space:]]+MOD_CONTROL[(]|Ring[[:space:]]+CONTROL_RING' \
        "$DIR/tests" 2>/dev/null |
    grep -v '^[^:]*:[0-9]*:[[:space:]]*#' || true)
if [ -n "$H" ]; then
    bad "los tests nombran CONTROL_RING:"
    printf '%s\n' "$H" | head -3
else
    ok "los tests no tocan CONTROL_RING"
fi
grep -rq 'TEST_CTRL_RING' "$DIR/tests" && ok "los tests usan TEST_CTRL_RING" \
    || bad "los tests no usan TEST_CTRL_RING"

# --- 6. la UI no manda acciones por nombre ---------------------------------
if grep -nE 'ecq_action\([^;]*(->name|\.name)' "$DIR/ui/ecq_mainwindow.cxx" >/dev/null 2>&1; then
    bad "ecq_action con nombre de modulo (debe ir el pid)"
else
    ok "ecq_action siempre por pid"
fi

# --- 7. log propio -----------------------------------------------------------
# `logit_init` solo se llama desde la capa C y con el LITERAL del proceso: con
# `argv[1]` el fichero seria `ew_controllerqt_<fecha>.log` y `diag.sh log
# ew_controllerQT` (case-sensitive) no lo encontraria. La clave `LogFile` del
# .d se consume en main.cxx por ecq_log_init().
grep -qE '^#define +ECQ_LOG_NAME +"ew_controllerQT"' "$DIR/c/ecq_logit.c" \
    && ok "ECQ_LOG_NAME = ew_controllerQT (nombre del proceso)" \
    || bad "ECQ_LOG_NAME no es el nombre del proceso"
grep -q 'logit_init(ECQ_LOG_NAME' "$DIR/c/ecq_logit.c" \
    && ok "logit_init usa el literal del proceso" \
    || bad "logit_init no usa el literal (diag.sh log ew_controllerQT no lo hallaria)"
if grep -rn 'logit_init' "$DIR/ui" >/dev/null 2>&1; then
    bad "ui/ llama a logit_init (debe pasar por ecq_log_init)"
else
    ok "ui/ no llama a logit_init"
fi
grep -q 'ecq_log_init(cfg->log_file)' "$DIR/ui/main.cxx" \
    && ok "main.cxx consume la clave LogFile" \
    || bad "main.cxx no consume LogFile"
grep -q 'ecq_logf(' "$DIR/c/ecq_core.c" \
    && ok "el nucleo escribe eventos en el log propio" \
    || bad "el nucleo no escribe en el log propio"

if [ "$fail" -eq 0 ]; then
    echo "OK: invariantes de ew_controllerQT"
fi
exit "$fail"
