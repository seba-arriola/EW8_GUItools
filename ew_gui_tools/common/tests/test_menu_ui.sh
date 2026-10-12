#!/usr/bin/env bash
# Invariantes del menú propio de los módulos GUI (GTK4):
#   - el header compartido es GTK-free;
#   - el helper no crea superficie popup (GtkMenuButton/GtkPopoverMenu);
#   - los 5 módulos ya NO usan GtkMenuButton y SÍ montan el helper;
#   - las acciones "win.*" de cada módulo se conservan.
# Se ejecuta desde `make -C ew_gui_tools/common check`.
set -euo pipefail

root="$(cd "$(dirname "$0")/../.." && pwd)"   # ew_gui_tools/
fail=0

must_have() { # archivo, patrón, mensaje
    if ! grep -qE "$2" "$1"; then
        echo "FAIL: $3 ($1 no contiene '$2')"
        fail=1
    fi
}

must_not_have() { # archivo, patrón, mensaje
    if grep -qE "$2" "$1"; then
        echo "FAIL: $3 ($1 contiene '$2')"
        fail=1
    fi
}

hdr="$root/common/include/ewgui/menu_ui.h"
impl="$root/common/src/ewgui_menu_ui_gtk4.c"

# 1) header GTK-free
if [ ! -f "$hdr" ]; then
    echo "FAIL: falta $hdr"
    fail=1
else
    must_not_have "$hdr" '#[[:space:]]*include[[:space:]]*<gtk/gtk\.h>' \
        "menu_ui.h no debe incluir gtk/gtk.h"
fi

# 2) helper: ventana propia, sin popup
if [ ! -f "$impl" ]; then
    echo "FAIL: falta $impl"
    fail=1
else
    must_have "$impl" 'gtk_window_new' "el helper debe abrir una GtkWindow propia"
    must_not_have "$impl" 'gtk_popover' "el helper no debe usar GtkPopover"
    must_not_have "$impl" 'gtk_menu_button' "el helper no debe usar GtkMenuButton"
fi

# 2b) API de actualizacion (menu dependiente de la vista)
must_have "$hdr" 'ewgui_menu_button_set_groups' "menu_ui.h debe declarar ewgui_menu_button_set_groups"
must_have "$impl" 'ewgui_menu_button_set_groups' "el helper debe implementar set_groups"
must_have "$impl" 'ewgui-menu-ctx' "el helper debe leer los grupos del object-data"

# 3) módulos: sin GtkMenuButton, con el helper
modules="csnrv/csnrv.c csnstaevdisp/csnstaevdisp.c csntvp/csntvp_main.c csnhypodbp/csnhypodbp_main.c ew_controller/ew_controller_main.c"
for f in $modules; do
    p="$root/$f"
    must_not_have "$p" 'gtk_menu_button_new' "$f no debe instanciar GtkMenuButton"
    must_not_have "$p" 'gtk_menu_button_set_menu_model' "$f no debe usar set_menu_model"
    must_have "$p" 'ewgui_menu_button_new' "$f debe montar ewgui_menu_button_new"
done

# 4) acciones preservadas por módulo
must_have "$root/csnrv/csnrv.c" 'win\.export' "csnrv pierde win.export"
must_have "$root/csnrv/csnrv.c" 'win\.dark-mode' "csnrv pierde win.dark-mode"
must_have "$root/csnrv/csnrv.c" 'win\.overlay-layers' "csnrv pierde win.overlay-layers"
must_have "$root/csnrv/csnrv.c" 'win\.overlay-reload' "csnrv pierde win.overlay-reload"

must_have "$root/csnstaevdisp/csnstaevdisp.c" 'win\.events-number' "csnstaevdisp pierde win.events-number"
must_have "$root/csnstaevdisp/csnstaevdisp.c" 'win\.export' "csnstaevdisp pierde win.export"
must_have "$root/csnstaevdisp/csnstaevdisp.c" 'win\.dark-mode' "csnstaevdisp pierde win.dark-mode"
must_have "$root/csnstaevdisp/csnstaevdisp.c" 'win\.overlay-layers' "csnstaevdisp pierde win.overlay-layers"
must_have "$root/csnstaevdisp/csnstaevdisp.c" 'win\.overlay-reload' "csnstaevdisp pierde win.overlay-reload"

for a in stas-per-screen time-window clean-view reload-default filter-options colour; do
    must_have "$root/csntvp/csntvp_main.c" "win\\.$a" "csntvp pierde win.$a"
done
must_have "$root/csntvp/csntvp_main.c" 'win\.dark-mode' "csntvp pierde win.dark-mode"
must_have "$root/csntvp/csntvp_main.c" 'win\.export' "csntvp pierde win.export"

must_have "$root/csnhypodbp/csnhypodbp_main.c" 'win\.export' "csnhypodbp pierde win.export"
must_have "$root/csnhypodbp/csnhypodbp_main.c" 'win\.dark-mode' "csnhypodbp pierde win.dark-mode"

must_have "$root/ew_controller/ew_controller_main.c" 'win\.dark-mode' "ew_controller pierde win.dark-mode"

# 5) csnhypodbp: busqueda hipocentral (solo Eventos) + backend SDS
must_have "$root/csnhypodbp/csnhypodbp_main.c" 'win\.search' "csnhypodbp pierde win.search"
must_have "$root/csnhypodbp/csnhypodbp_main.c" 'win\.clear-search' "csnhypodbp pierde win.clear-search"
must_have "$root/csnhypodbp/csnhypodbp_main.c" '"Search", search_items' "csnhypodbp pierde el grupo Search (pestaña Eventos)"
must_have "$root/csnhypodbp/csnhypodbp_main.c" 'base_groups, 2' "csnhypodbp debe montar los 2 grupos base (Search+View)"
must_have "$root/csnhypodbp/csnhypodbp_main.c" 'gtk_label_new\("Events"\)' "el titulo maestro debe ser Events"
must_have "$root/csnhypodbp/csnhypodbp_main.c" 'gtk_label_new\("Origins"\)' "el titulo detalle debe ser Origins"
must_have "$root/csnhypodbp/csnhypodbp_main.c" 'g_btn_clear_search' "falta el boton Clear search junto al titulo"
must_have "$root/csnhypodbp/csnhypodbp_gui.c" 'on_btn_search_clicked' "falta el dialogo de busqueda"
must_have "$root/csnhypodbp/csnhypodbp_ws.c" 'ewguidb_search_events' "la busqueda no consulta la BD"
must_have "$root/csnhypodbp/csnhypodbp_ws.c" 'csnhypodbp_sds_fetch' "el fetch no integra el backend SDS"

if [ "$fail" -eq 0 ]; then
    echo "OK: menú propio (helper + 5 módulos) consistente"
fi
exit "$fail"
