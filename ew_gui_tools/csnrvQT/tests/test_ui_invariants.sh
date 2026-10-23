#!/bin/bash
# Invariantes de FUENTE de csnrvQT (sin compilar y sin DISPLAY).
#
# Reglas que se pagaron caras en los modulos anteriores:
#   - la capa C no conoce Qt y la capa Qt no conoce EarthWorm/GTK/cairo
#     (frontera c/crv_capi.h, docs/testLibQT.md R1/R2);
#   - cairo vive SOLO en c/ (mapa/export); ui/ no lo incluye;
#   - el log es el LITERAL del proceso (diag.sh log csnrvQT);
#   - el Makefile declara MOD_KIT=qt6, instala con install_bin.sh y su TARGET
#     usa `=` simple (ew8_compile.sh lo deriva con sed);
#   - el arnes no toca CONTROL_RING: es un visor de HYPO_RING_REF.
set -u

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
DIR="$ROOT/ew_gui_tools/csnrvQT"
fail=0

ok()  { echo "ok  : $1"; }
bad() { echo "FAIL: $1"; fail=1; }

# --- 1. frontera C -> C++ ---------------------------------------------------
if grep -rEn '#include[[:space:]]*[<"](gtk|adwaita|cairo|earthworm|transport|kom|rw_mag|libpq|sys/shm)' \
        "$DIR/ui" >/dev/null 2>&1; then
    bad "ui/ incluye EarthWorm/GTK/cairo/libpq"
else
    ok "ui/ solo Qt + ewguiqt + crv_capi.h"
fi

if grep -rEn '#include[[:space:]]*<Q|QApplication|QString|QWidget' \
        "$DIR/c" >/dev/null 2>&1; then
    bad "c/ incluye Qt"
else
    ok "c/ no conoce Qt"
fi

if grep -E '^#include' "$DIR/c/crv_capi.h" \
        | grep -vE '<(stddef|stdio|time)\.h>' >/dev/null 2>&1; then
    bad "crv_capi.h incluye algo que no es C estandar"
else
    ok "crv_capi.h es C puro (sin Qt/cairo/EarthWorm)"
fi

# --- 2. cairo solo en c/ ----------------------------------------------------
if grep -rEn 'cairo_[a-z]|#include[[:space:]]*<cairo' "$DIR/ui" >/dev/null 2>&1; then
    bad "ui/ usa cairo (debe quedar en c/)"
else
    ok "ui/ no toca cairo"
fi

# --- 3. Makefile ------------------------------------------------------------
grep -qE '^MOD_KIT[[:space:]]*=[[:space:]]*qt6' "$DIR/Makefile" \
    && ok "MOD_KIT = qt6" || bad "MOD_KIT no es qt6"
grep -qE '^TARGET[[:space:]]*=[[:space:]]*csnrvQT' "$DIR/Makefile" \
    && ok "TARGET = csnrvQT" || bad "TARGET incorrecto"
grep -q 'install_bin.sh' "$DIR/Makefile" \
    && ok "instala con install_bin.sh" || bad "no usa install_bin.sh"
grep -q 'libewguiqt.a' "$DIR/Makefile" \
    && ok "enlaza libewguiqt.a" || bad "no enlaza libewguiqt.a"
if grep -qE '^[[:space:]]*(EWLIBS|LDFLAGS)[^#]*libewgui_gtk4\.a' "$DIR/Makefile"; then
    bad "enlaza libewgui_gtk4.a (arrastraria GTK)"
else
    ok "no enlaza libewgui_gtk4.a"
fi
grep -qE '^VALIDATE_FLAGS[^#]*csnrv\.d' "$DIR/Makefile" \
    && ok "VALIDATE_FLAGS usa csnrv.d" || bad "VALIDATE_FLAGS no usa csnrv.d"

# --- 4. log literal y modos instrumentados ---------------------------------
grep -qE '^#define +CRV_LOG_NAME +"csnrvQT"' "$DIR/c/crv_logit.c" \
    && ok "CRV_LOG_NAME = csnrvQT" || bad "CRV_LOG_NAME no es el proceso"
grep -q 'crv_log_init(cfg->log_file)' "$DIR/ui/main.cxx" \
    && ok "main.cxx consume LogFile" || bad "main.cxx no consume LogFile"
grep -q 'CSNRVQT_MODE' "$DIR/ui/main.cxx" \
    && ok "modos instrumentados (--dump/--layout/--shot/--interact)" \
    || bad "faltan los modos instrumentados"
grep -q '"--interact"' "$DIR/ui/main.cxx" \
    && ok "--interact cableado en main.cxx" || bad "falta --interact"
if grep -rn 'CONTROL_RING' "$DIR/tests/test_ui.sh" "$DIR/tests/test_live.sh" \
        >/dev/null 2>&1; then
    bad "el arnes usa CONTROL_RING (debe ser HYPO_RING_REF)"
else
    ok "el arnes no toca CONTROL_RING"
fi

# --- 5. el mapa responde a rueda y arrastre ---------------------------------
# Sin estos cables el visor parece "muerto": CanvasWidget emite las senales
# pero nadie mueve la vista (fue el fallo reportado en vivo).
grep -q 'crv_map_zoom_at' "$DIR/c/crv_capi.h" \
    && ok "crv_capi.h declara crv_map_zoom_at" || bad "falta crv_map_zoom_at"
grep -q 'crv_map_zoom_at' "$DIR/c/crv_map.c" \
    && ok "crv_map.c implementa crv_map_zoom_at" || bad "sin zoom_at en crv_map.c"
grep -q '&CrvCanvasWidget::zoomed' "$DIR/ui/crv_mainwindow.cxx" \
    && ok "la ventana conecta zoomed (rueda)" \
    || bad "zoomed sin conectar: la rueda no hace nada"
grep -q '&CrvCanvasWidget::dragged' "$DIR/ui/crv_mainwindow.cxx" \
    && ok "la ventana conecta dragged (arrastre)" \
    || bad "dragged sin conectar: no se puede panear"
grep -q 'devicePixelRatioF' "$DIR/ui/crv_mainwindow.cxx" \
    && ok "gestos convertidos a pixeles fisicos" \
    || bad "faltan pixeles fisicos (DPR) en los gestos"
grep -q 'CRV_CHANGED_EVENT' "$DIR/ui/crv_mainwindow.cxx" \
    && grep -q 'crv_map_reset' "$DIR/ui/crv_mainwindow.cxx" \
    && ok "qid nuevo resetea zoom/pan" \
    || bad "el evento nuevo no resetea la vista"

if [ "$fail" -eq 0 ]; then
    echo "OK: invariantes de csnrvQT"
else
    echo "FALLAS: revisar los FAIL de arriba"
fi
exit "$fail"
