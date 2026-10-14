#!/usr/bin/env bash
# Invariantes de FUENTE de ew_controller (grep, sin GTK ni display):
#   1) las tablas de Modules y Rings ordenan por clic en la cabecera con el helper
#      compartido ewgui/table_ui.h (mismo contrato que csnhypodbp);
#   2) con el GtkSortListModel en medio, ningún índice de la VISTA indexa el store;
#   3) el refresco de estado se ve (se reconstruyen las filas) y no se congela;
#   4) la IDENTIDAD de un módulo es su PID: ninguna resolución de objetivo puede
#      hacerse por nombre (startstop_unix.d tiene dos `Process` llamados csnmags:
#      csnmags.d y csnmags_raw.d, y por nombre se actuaba siempre sobre la 1.ª).
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
ck "seleccion: la posicion se busca en el sort model" 'ec_view_pos_of_pid' "$gui"
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

# --- 6) Propiedad de referencias GObject (use-after-free) ------------------
# gtk_single_selection_get_selected_item() devuelve un PRESTAMO (GIR de GTK4:
# transfer-ownership="none"; "the returned data is owned by the instance").
# Liberarlo finaliza la fila que el GListStore y el GtkSortListModel siguen
# usando; el siguiente rebuild hace g_object_unref sobre memoria liberada y el
# proceso muere con SIGSEGV en libgobject (paso en produccion).
if sed -n '/^void on_row_selected/,/^}/p' "$gui" | grep -q 'g_object_unref *('; then
   echo "FAIL: on_row_selected libera el item PRESTADO de get_selected_item()"
   fails=$((fails + 1))
else
   echo "ok  : on_row_selected no libera el item prestado de get_selected_item()"
fi
# El unref correcto (transfer full) sigue en su sitio, para no perder el arreglo
# de la resolucion por ITEM (y no por la posicion del modelo ordenado).
ck "refs: la seleccion se resuelve por item (get_selected_item)" \
   'gtk_single_selection_get_selected_item\(g_mod_sel\)' "$gui"
ck "refs: el item del modelo si es transfer-full" 'g_list_model_get_item\(G_LIST_MODEL\(g_mod_sort\)' "$gui"

# --- 7) La identidad de un modulo es su PID (nunca el nombre) --------------
# Con dos `Process` del mismo nombre (csnmags.d / csnmags_raw.d) resolver por
# nombre apunta siempre a la primera instancia: reiniciar la fila de la 2.ª
# mandaba Stop/Restart con el pid de la 1.ª. La identidad se lee del ITEM
# seleccionado y la memoria de la selección es el pid.
nck "identidad: la seleccion no se recuerda por nombre" 'g_sel_name' "$gui"
ck  "identidad: la seleccion se recuerda por pid" 'g_sel_pid' "$gui"
nck "identidad: el panel de log no se resuelve por nombre" 'g_log_name' "$gui"
ck  "identidad: el panel de log usa el pid" 'g_log_pid' "$gui"
ck  "identidad: se busca el modulo por pid" 'ewgui_ctrl_idx_por_pid' "$gui"
ck  "identidad: el objetivo sale del item (pid)" \
   'g_object_get\(item, *"pid", *&pid, *"name", *&nm, *"cfgfile", *&cf, *NULL\)' "$gui"
ck  "identidad: el cuadro de confirmacion nombra el pid y el .d" 'ec_modref_where' "$gui"
ck  "identidad: los selectores distinguen instancias repetidas" 'ewgui_ctrl_etiqueta_modulo' "$gui"
if sed -n '/^static EcModRef \*ec_modref_from_sel/,/^}/p' "$gui" | grep -q 'g_status'; then
   echo "FAIL: ec_modref_from_sel vuelve a resolver el objetivo con g_status (por indice/nombre)"
   fails=$((fails + 1))
else
   echo "ok  : ec_modref_from_sel no toca g_status (lee el item seleccionado)"
fi
if sed -n '/^void on_row_selected/,/^}/p' "$gui" | grep -qE 'strcmp|->name'; then
   echo "FAIL: on_row_selected vuelve a emparejar por nombre"
   fails=$((fails + 1))
else
   echo "ok  : on_row_selected identifica la fila por pid, sin nombres"
fi

# --- 8) El arnes en vivo usa SU propio anillo ------------------------------
# El CONTROL_RING de produccion lo comparten startstop y la instancia real de
# ew_controller, y el puntero de lectura de un anillo es UNO SOLO: los TYPE_STATUS
# sinteticos se los llevaba otro lector y el test era intermitente (y ensuciaba la
# GUI del usuario con modulos falsos).
ck  "arnes: el test inyecta en su propio anillo" 'EWGUI_FAKE" TEST_CTRL_RING' "$dir/tests/test_sort_and_status.sh"
nck "arnes: el test no inyecta en el CONTROL_RING de produccion" 'EWGUI_FAKE" CONTROL_RING' "$dir/tests/test_sort_and_status.sh"
ck  "arnes: el fixture crea el anillo antes de arrancar el modulo" 'fake_status" --create TEST_CTRL_RING' "$dir/tests/test_sort_and_status.sh"

if [ "$fails" -ne 0 ]; then
   echo "ew_controller invariants: $fails fallo(s)"
   exit 1
fi
echo "ew_controller invariants: OK"
