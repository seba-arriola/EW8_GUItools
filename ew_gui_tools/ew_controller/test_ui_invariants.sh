#!/usr/bin/env bash
# Invariantes de FUENTE de ew_controller (grep, sin GTK ni display):
#   1) las tablas de Modules y Rings ordenan por clic en la cabecera con el helper
#      compartido ewgui/table_ui.h (mismo contrato que csnhypodbp);
#   2) con el GtkSortListModel en medio, ningún índice de la VISTA indexa el store;
#   3) el refresco de estado se ve (se reconstruyen las filas) y no se congela.
# Complementa common/tests/test_ctrl.c (lógica pura) y el arnés GUI de
# docs/GUI-TESTS.md.
set -u
dir=$(cd "$(dirname "$0")" && pwd)
main="$dir/ew_controller_main.c"
gui="$dir/ew_controller_gui.c"
fails=0

ck() {  # mensaje, patron ERE, archivo -> debe aparecer
   if grep -qE "$2" "$3"; then echo "ok  : $1"; else echo "FAIL: $1"; fails=$((fails + 1)); fi
}
nck() {  # mensaje, patron ERE, archivo -> NO debe aparecer
   if grep -qE "$2" "$3"; then echo "FAIL: $1"; fails=$((fails + 1)); else echo "ok  : $1"; fi
}
ckn() {  # mensaje, patron ERE, archivo, minimo de coincidencias
   local n; n=$(grep -cE "$2" "$3")
   if [ "$n" -ge "$4" ]; then echo "ok  : $1"
   else echo "FAIL: $1 (encontradas $n, se esperaban >= $4)"; fails=$((fails + 1)); fi
}

# --- 1) Tablas ordenables por cabecera -------------------------------------
ck "orden: usa el helper compartido" 'ewgui/table_ui\.h' "$main"
ck "orden: sort model de Modules" 'g_mod_sort = gtk_sort_list_model_new' "$main"
ck "orden: sort model de Rings" 'g_rings_sort = gtk_sort_list_model_new' "$main"
ck "orden: la seleccion va sobre el sort model" 'gtk_single_selection_new\(G_LIST_MODEL\(g_mod_sort\)\)' "$main"
ck "orden: Rings cuelga del sort model" 'gtk_no_selection_new\(G_LIST_MODEL\(g_rings_sort\)\)' "$main"
ck "orden: columnas ordenables (Modules y Rings)" 'ewgui_table_column_sortable' "$main"
ck "orden: se enganchan los dos sorters" 'ewgui_table_enable_sorting' "$main"
ck "orden: orden inicial Module ASC" 'ewgui_table_enable_sorting\(GTK_COLUMN_VIEW\(g_tree\), g_mod_sort, col_mod, EWGUI_TABLE_ASC\)' "$main"
ck "orden: orden inicial Ring ASC" 'ewgui_table_enable_sorting\(GTK_COLUMN_VIEW\(g_rings_tree\), g_rings_sort, col_ring, EWGUI_TABLE_ASC\)' "$main"
nck "orden: sin sorters sueltos" 'gtk_numeric_sorter_new|gtk_string_sorter_new' "$main"
nck "orden: sin popups (WSLg #1299)" 'gtk_popover|gtk_menu_button|gtk_drop_down' "$main"

# --- 2) El indice de la VISTA no indexa el store ---------------------------
ck "seleccion: se resuelve por ITEM" 'gtk_single_selection_get_selected_item' "$gui"
ck "seleccion: la posicion se busca en el sort model" 'ec_view_pos_of_name' "$gui"
nck "seleccion: ningun indice de vista indexa el store" 'g_list_model_get_item\(G_LIST_MODEL\(g_store\), *idx' "$gui"

# --- 3) El estado se ve y no se congela ------------------------------------
ck "estado: se reconstruyen las filas ante cualquier cambio" 'if \(plan != EWCTRL_SYNC_NONE\)' "$gui"
nck "estado: sin actualizacion in situ invisible" 'g_object_set\(row, *"status"' "$gui"
ck "estado: acepta GET_MISS/GET_NOTRACK (mensaje ya copiado)" 'res == GET_OK \|\| res == GET_MISS \|\| res == GET_NOTRACK' "$main"
ck "estado: sigue drenando el anillo mientras haya mensaje" 'while \(res == GET_OK \|\| res == GET_MISS \|\| res == GET_NOTRACK\)' "$main"
ck "estado: acota recsize antes de cerrar la cadena" 'recsize < \(long\) sizeof\(msg\)' "$main"
nck "estado: sin GET_TOOBIG (no hubo copia)" 'res == GET_TOOBIG' "$main"
ck "estado: se marca la falta de respuesta" 'ec_set_noreply\(1\)' "$main"
ck "estado: la falta de respuesta se pinta como '?'" 'g_no_reply \? "\?" : ' "$gui"

# --- 4) Layout: la ventana encoge y mandan los divisores --------------------
# set_size_request fija el MINIMO del widget ("leaves the window at least as
# large"): 620 y 340 px eran el minimo de TODA la ventana (~1051 px), y con
# shrink FALSE el divisor no se podia arrastrar hacia dentro.
nck "layout: sin minimo artificial en la tabla de Modulos" 'set_size_request\(sw_mod,' "$main"
nck "layout: sin minimo artificial en la tabla de Anillos" 'set_size_request\(sw_rings,' "$main"
# El minimo del panel debe salir del acotado propio, no del texto del contenido
# (que fijaba el minimo de la ventana en ~1051 px).
ckn "layout: los scrolled acotan su propio minimo" 'gtk_scrolled_window_set_min_content_width\(' "$main" 3
# El minimo de un GtkLabel es su texto completo: las etiquetas de estado deben
# llevar elipsis o fijan el minimo de la ventana.
ckn "layout: las etiquetas de estado no imponen su texto" 'gtk_label_set_ellipsize\(GTK_LABEL\(g_lbl_' "$main" 4
ck "layout: el divisor izquierdo se puede encoger/arrastrar" 'gtk_paned_set_shrink_start_child\(GTK_PANED\(paned\), TRUE\)' "$main"
ck "layout: el divisor derecho se puede encoger/arrastrar" 'gtk_paned_set_shrink_end_child\(GTK_PANED\(paned\), TRUE\)' "$main"
ck "layout: el ancho extra es de la tabla, no del panel derecho" 'gtk_paned_set_resize_end_child\(GTK_PANED\(paned\), FALSE\)' "$main"
ck "layout: la columna derecha tiene divisor propio (vertical)" 'gtk_paned_new\(GTK_ORIENTATION_VERTICAL\)' "$main"
ck "layout: el alto extra es de los logs" 'gtk_paned_set_resize_end_child\(GTK_PANED\(paned_v\), TRUE\)' "$main"
ck "layout: la tabla de Anillos crece en alto" 'gtk_widget_set_vexpand\(sw_rings, TRUE\)' "$main"
ck "layout: reparto inicial del divisor horizontal (al mapear)" 'g_signal_connect\(paned, "map", G_CALLBACK\(ec_split_h\), NULL\)' "$main"
ck "layout: reparto inicial del divisor vertical (al mapear)" 'g_signal_connect\(paned_v, "map", G_CALLBACK\(ec_split_v\), NULL\)' "$main"
ck "layout: la posicion inicial se aplica cuando hay tamano" 'g_timeout_add\(50, \(GSourceFunc\) ec_split_' "$main"
nck "layout: el reparto inicial no se re-aplica al re-mapear" 'g_signal_connect\(paned, "notify::position"' "$main"

# --- 5) Las columnas se reparten el ancho ----------------------------------
# Sin ninguna columna con expand, el espacio sobrante de la vista NO se reparte
# y queda en blanco a la derecha (medido: 221 px con la tabla de 670 px).
ckn "columnas: las dos tablas tienen columnas que se quedan el ancho" 'gtk_column_view_column_set_expand\(c, TRUE\)' "$main" 2
ckn "columnas: las dos tablas tienen columnas de ancho fijo" 'gtk_column_view_column_set_fixed_width\(c, ' "$main" 2
ckn "columnas: las dos tablas son ajustables a mano" 'gtk_column_view_column_set_resizable\(c, TRUE\)' "$main" 2

if [ "$fails" -ne 0 ]; then
   echo "ew_controller invariants: $fails fallo(s)"
   exit 1
fi
echo "ew_controller invariants: OK"
