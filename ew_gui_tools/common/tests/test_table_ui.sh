#!/usr/bin/env bash
# Invariantes del helper de tablas ordenables (common). Test de FUENTE (grep),
# sin GTK ni display: complementa tests/test_table_cmp.c (comparador puro).
set -u
root=$(cd "$(dirname "$0")/../.." && pwd)
hdr="$root/common/include/ewgui/table_ui.h"
impl="$root/common/src/ewgui_table_ui_gtk4.c"
cmp="$root/common/src/ewgui_table_cmp.c"
mk="$root/common/Makefile"
fails=0

must_have() {  # archivo, patron ERE, mensaje
    if grep -qE "$2" "$1"; then echo "ok  : $3"; else echo "FAIL: $3"; fails=$((fails + 1)); fi
}
must_not_have() {
    if grep -qE "$2" "$1"; then echo "FAIL: $3"; fails=$((fails + 1)); else echo "ok  : $3"; fi
}

# 1) El header es GTK-free (forward declarations, como menu_ui.h).
must_not_have "$hdr" '#[[:space:]]*include[[:space:]]*<gtk/gtk\.h>' "header sin gtk/gtk.h"
must_have     "$hdr" 'typedef struct _GtkColumnView '                 "header con forward decls"

# 2) La implementacion es una ventana/columna normal: sin popups (bug WSLg #1299).
must_not_have "$impl" 'gtk_popover|gtk_menu_button|gtk_drop_down' "sin popups (WSLg #1299)"

# 3) Engancha el sorter de la VISTA al sort model: sin eso las cabeceras no ordenan.
must_have "$impl" 'gtk_column_view_get_sorter'     "usa el sorter de la vista"
must_have "$impl" 'gtk_sort_list_model_set_sorter' "lo pone en el sort model"
must_have "$impl" 'gtk_column_view_sort_by_column' "fija el orden inicial por API"

# 4) Direccion del primer clic declarada por columna.
must_have "$impl" 'ewgui-first-dir'                "direccion declarada por columna"

# 5) El comparador es puro (sin GTK) y el Makefile compila el helper por rama.
must_not_have "$cmp" 'gtk/gtk\.h'                  "comparador sin GTK"
must_have     "$mk"  'TABLE_UI_SRC'                "el Makefile compila el helper"
must_have     "$mk"  'test_table_ui\.sh'           "el check lo ejecuta"

if [ "$fails" -ne 0 ]; then
    echo "test_table_ui: $fails fallo(s)"
    exit 1
fi
echo "test_table_ui: OK"
