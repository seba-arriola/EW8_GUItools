#!/bin/bash
# Invariantes de FUENTE de csntvpQT (sin compilar y sin DISPLAY).
#
# Reglas que se pagaron caras en los modulos anteriores:
#   - la capa C no conoce Qt y la capa Qt no conoce EarthWorm/GTK/cairo
#     (frontera c/tvq_capi.h, docs/testLibQT.md R1/R2);
#   - cairo vive SOLO en c/; ui/ no lo incluye;
#   - el parser de TYPE_PICK_SCNL NO se copia: se compila ../csntvp/csntvp_util.c
#     (si se copiara, el formato de picada podria derivar del GTK);
#   - el log es el LITERAL del proceso (diag.sh log csntvpQT);
#   - nada de desplegables propios (WSLg #1299);
#   - el Makefile declara MOD_KIT=qt6, instala con install_bin.sh y su TARGET
#     usa `=` simple (ew8_compile.sh lo deriva con sed).
set -u

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
DIR="$ROOT/ew_gui_tools/csntvpQT"
PARAMS="$ROOT/run_working_v8/params"
fail=0

ok()  { echo "ok  : $1"; }
bad() { echo "FAIL: $1"; fail=1; }

# --- 1. frontera C -> C++ ---------------------------------------------------
if grep -rEn '#include[[:space:]]*[<"](gtk|adwaita|cairo|earthworm|transport|kom|rw_mag|libpq|sys/shm)' \
        "$DIR/ui" >/dev/null 2>&1; then
    bad "ui/ incluye EarthWorm/GTK/cairo/libpq"
else
    ok "ui/ solo Qt + ewguiqt + tvq_capi.h"
fi

if grep -rEn '#include[[:space:]]*<Q|QApplication|QString|QWidget' \
        "$DIR/c" >/dev/null 2>&1; then
    bad "c/ incluye Qt"
else
    ok "c/ no conoce Qt"
fi

if grep -E '^#include' "$DIR/c/tvq_capi.h" \
        | grep -vE '<(stddef|stdio|time)\.h>' >/dev/null 2>&1; then
    bad "tvq_capi.h incluye algo que no es C estandar"
else
    ok "tvq_capi.h es C puro (sin Qt/cairo/EarthWorm)"
fi

# --- 2. cairo solo en c/ ----------------------------------------------------
if grep -rEn 'cairo_[a-z]|#include[[:space:]]*<cairo' "$DIR/ui" >/dev/null 2>&1; then
    bad "ui/ usa cairo (debe quedar en c/)"
else
    ok "ui/ no toca cairo"
fi

# --- 3. el parser de picadas se reutiliza, no se copia ---------------------
grep -q 'C_SIBLING.*csntvp_util\|csntvp_util' "$DIR/Makefile" \
    && ok "compila csntvp_util desde ../csntvp" \
    || bad "no compila ../csntvp/csntvp_util.c"
grep -q 'cs_%.o: ../csntvp/%.c' "$DIR/Makefile" \
    && ok "regla de compilacion del hermano" \
    || bad "falta la regla cs_%.o ../csntvp"
if grep -rEn 'csntvp_pick_parse[[:space:]]*\(' "$DIR/c" | grep -q '^\S*:[0-9]*:int'; then
    bad "c/ define su propio csntvp_pick_parse (debe usar el del GTK)"
else
    ok "c/ no duplica el parser de picadas"
fi

# --- 4. Makefile ------------------------------------------------------------
grep -qE '^MOD_KIT[[:space:]]*=[[:space:]]*qt6' "$DIR/Makefile" \
    && ok "MOD_KIT = qt6" || bad "MOD_KIT no es qt6"
grep -qE '^TARGET[[:space:]]*=[[:space:]]*csntvpQT' "$DIR/Makefile" \
    && ok "TARGET = csntvpQT" || bad "TARGET incorrecto"
grep -q 'install_bin.sh' "$DIR/Makefile" \
    && ok "instala con install_bin.sh" || bad "no usa install_bin.sh"
grep -q 'libewguiqt.a' "$DIR/Makefile" \
    && ok "enlaza libewguiqt.a" || bad "no enlaza libewguiqt.a"
if grep -qE '^[[:space:]]*(EWLIBS|LDFLAGS)[^#]*libewgui_gtk4\.a' "$DIR/Makefile"; then
    bad "enlaza libewgui_gtk4.a (arrastraria GTK)"
else
    ok "no enlaza libewgui_gtk4.a"
fi
grep -qE '^VALIDATE_FLAGS[^#]*csntvpqt\.d' "$DIR/Makefile" \
    && ok "VALIDATE_FLAGS usa csntvpqt.d" || bad "VALIDATE_FLAGS no usa csntvpqt.d"

# --- 5. log literal, modos instrumentados, sin desplegables ---------------
grep -qE '^#define +TVQ_LOG_NAME +"csntvpQT"' "$DIR/c/tvq_logit.c" \
    && ok "TVQ_LOG_NAME = csntvpQT" || bad "TVQ_LOG_NAME no es el proceso"
grep -q 'tvq_log_init(cfg->log_file)' "$DIR/ui/main.cxx" \
    && ok "main.cxx consume LogFile" || bad "main.cxx no consume LogFile"
grep -q 'CSNTVPQT_MODE' "$DIR/ui/main.cxx" \
    && ok "modos instrumentados (--dump/--layout/--shot/--interact)" \
    || bad "faltan los modos instrumentados"
grep -q '"--interact"' "$DIR/ui/main.cxx" \
    && ok "--interact cableado en main.cxx" || bad "falta --interact"
if grep -rEn 'QComboBox|QMenuButton|GtkDropDown' "$DIR/ui" >/dev/null 2>&1; then
    bad "ui/ usa desplegables (WSLg #1299)"
else
    ok "sin desplegables propios"
fi

# --- 6. el .d del port difiere del GTK solo en lo declarado ---------------
QT_D="$PARAMS/csntvpqt.d"
GTK_D="$PARAMS/csntvp.d"
if [ -f "$QT_D" ] && [ -f "$GTK_D" ]; then
    norm() {
        sed -e 's/#.*//' "$1" \
            | sed -e 's/[[:space:]]*$//' -e 's/^[[:space:]]*//' \
            | grep -v '^$' \
            | grep -vE '^(MyModuleId)([[:space:]]|$)'
    }
    if diff -q <(norm "$GTK_D") <(norm "$QT_D") >/dev/null; then
        ok "csntvpqt.d solo difiere en MyModuleId"
    else
        bad "csntvpqt.d difiere del GTK en algo mas que MyModuleId"
    fi
else
    bad "faltan csntvp.d / csntvpqt.d en params/"
fi

# --- 7. el arnes no toca params/ ni CONTROL_RING -------------------------
if grep -rn 'CONTROL_RING' "$DIR/tests/test_ui.sh" "$DIR/tests/test_live.sh" \
        >/dev/null 2>&1; then
    bad "el arnes usa CONTROL_RING"
else
    ok "el arnes no toca CONTROL_RING"
fi
if grep -rn 'stations_to_view.sta' "$DIR/tests/test_ui.sh" >/dev/null 2>&1; then
    bad "test_ui.sh depende del .sta de produccion (usa su fixture)"
else
    ok "test_ui.sh usa su propio fixture"
fi

# --- 8. normalizacion vertical y modo picada ------------------------------
# La pista se escala SOLO por env_max_abs y zoom (paridad csntvp_gui.c:378-381);
# el scale del StaFile no se lee en el GTK (csntvp_data.c:209 es el unico
# write de dScreenScale y no hay ningun read).
if grep -q 'auto_scale \*= s->scale' "$DIR/c/tvq_paint.c"; then
    bad "tvq_paint.c multiplica por el scale del StaFile (aplasta la traza)"
else
    ok "normalizacion por env_max_abs + zoom (sin scale del StaFile)"
fi

# Modo picada: p/s arman, Esc desarma y el clic exige fase. El indicador es el
# circulo con la letra bajo el cursor (paridad csnhypodbpQT).
if grep -q 'Qt::Key_Escape' "$DIR/ui/tvq_mainwindow.cxx"; then
    ok "ui: Escape desarma el modo picada"
else
    bad "ui: falta Qt::Key_Escape (no se puede cancelar el modo picada)"
fi
if grep -q 'void TvqMainWindow::updatePickCursor' "$DIR/ui/tvq_mainwindow.cxx"; then
    ok "ui: updatePickCursor()"
else
    bad "falta updatePickCursor() (indicador de fase)"
fi
if grep -q 'setCursor(QCursor' "$DIR/ui/tvq_mainwindow.cxx" &&
   grep -q 'unsetCursor' "$DIR/ui/tvq_mainwindow.cxx"; then
    ok "ui: cursor de fase y restauracion al desarmar"
else
    bad "el cursor de fase no se pone o no se restaura"
fi
if grep -q '!tvq_core_pick_phase(m_core)' "$DIR/ui/tvq_mainwindow.cxx"; then
    ok "ui: el clic exige fase armada"
else
    bad "onPlace no comprueba la fase armada (picaria sin modo)"
fi

# --- 9. scroll con rueda y guia vertical ---------------------------------
if grep -q 'CanvasWidget::hovered' "$DIR/ui/tvq_mainwindow.cxx" &&
   grep -q 'CanvasWidget::left' "$DIR/ui/tvq_mainwindow.cxx"; then
    ok "ui: guia conectada a hovered/left"
else
    bad "la guia no usa hovered/left (no seguiria al puntero ni se ocultaria)"
fi
if grep -q 'installEventFilter' "$DIR/ui/tvq_mainwindow.cxx" &&
   grep -q 'QEvent::Wheel' "$DIR/ui/tvq_mainwindow.cxx"; then
    ok "ui: la rueda pasa por eventFilter"
else
    bad "falta eventFilter de la rueda (el canvas la consume como zoom)"
fi
if grep -q 'tvq_core_set_guide' "$DIR/ui/tvq_mainwindow.cxx"; then
    ok "ui: el puntero alimenta tvq_core_set_guide"
else
    bad "la UI no actualiza la guia del core"
fi
if grep -q 'guide_valid' "$DIR/c/tvq_paint.c" &&
   grep -q 'cairo_set_dash' "$DIR/c/tvq_paint.c" &&
   grep -q 'pick_phase' "$DIR/c/tvq_paint.c"; then
    ok "paint: guia discontinua y solo con fase armada"
else
    bad "tvq_paint.c no pinta la guia o no la condiciona a la fase"
fi
if grep -q 'tvq_core_set_guide' "$DIR/c/tvq_capi.h"; then
    ok "capi: contrato de la guia"
else
    bad "falta tvq_core_set_guide en tvq_capi.h"
fi

# --- 10. sin color por componente ----------------------------------------
# El color de la traza sale de waveformsColor (paridad GTK); no quedan claves,
# roles ni nombres de componente en el modulo ni en su documentacion.
if grep -rEn -I --exclude=test_ui_invariants.sh \
        '\b[zZnNeE]Color\b|TVQ_COLOUR_(Z|N|E)\b|tvq_colour_for_chan|Component [ZNE]|Z/N/E|z/n/e' \
        "$DIR/c" "$DIR/ui" "$DIR/tests" >/dev/null 2>&1 ||
   grep -qE '\b[zZnNeE]Color\b|color por componente|Component [ZNE]|Z/N/E|z/n/e' \
        "$DIR/README.md" "$DIR/../../AGENTS.md" "$PARAMS/csntvpqt.d" \
        >/dev/null 2>&1; then
    bad "quedan restos del color por componente"
else
    ok "sin color por componente (traza = waveformsColor)"
fi

if [ "$fail" -eq 0 ]; then
    echo "OK: invariantes de csntvpQT"
else
    echo "FALLAS: revisar los FAIL de arriba"
fi
exit "$fail"
