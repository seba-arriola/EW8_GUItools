#!/bin/sh
# Invariantes de UI de la pestana Ondas (tabla de estaciones, barra inferior,
# Help, colores). Test de FUENTE (grep), sin GTK: complementa los tests
# headless de logica pura (test_statable, etc.).
set -u
cd "$(dirname "$0")" || exit 1
fails=0

ck() {  # ck  <descripcion> <patron ERE> <archivo>
    if grep -qE "$2" "$3"; then echo "ok  : $1"; else echo "FAIL: $1"; fails=$((fails+1)); fi
}
nck() { # nck <descripcion> <patron ERE> <archivo>  (no debe aparecer)
    if grep -qE "$2" "$3"; then echo "FAIL: $1"; fails=$((fails+1)); else echo "ok  : $1"; fi
}

# --- Tabla de estaciones (panel izquierdo): GtkColumnView con separadores ---
ck  "tabla: es un GtkColumnView"             'gtk_column_view_new'                         csnhypodbp_main.c
ck  "tabla: separadores de fila"             'gtk_column_view_set_show_row_separators'     csnhypodbp_main.c
ck  "tabla: separadores de columna"          'gtk_column_view_set_show_column_separators'  csnhypodbp_main.c
nck "tabla: ya no es un GtkListBox"          'gtk_list_box'                                csnhypodbp_main.c
ck  "tabla: columna station"                 'add_sta_station_column\(g_sta_view, "station"\)' csnhypodbp_main.c
ck  "tabla: columna dist\[km\] pegada"       'add_sta_text_column\(g_sta_view, "dist\[km\]"'   csnhypodbp_main.c
ck  "tabla: columna pick (texto, no casilla)" 'add_sta_text_column\(g_sta_view, "pick"'        csnhypodbp_main.c
ck  "tabla: columna stat"                    'add_sta_text_column\(g_sta_view, "stat"'         csnhypodbp_main.c
ck  "tabla: columna res"                     'add_sta_text_column\(g_sta_view, "res"'          csnhypodbp_main.c
ck  "tabla: modelo de fila por fase"         'CSNHYPODBP_TYPE_STA_ITEM'                    csnhypodbp_main.c
ck  "tabla: 2 filas por estacion (P y S)"    'csnhypodbp_sta_item_new\(&srow, 1\)'         csnhypodbp_gui.c
ck  "tabla: dedup M>A>T + residual"          'csnhypodbp_statable_build'                   csnhypodbp_gui.c
ck  "tabla: casilla del pick junto al nombre" 'on_pick_check_toggled'                      csnhypodbp_main.c
nck "tabla: ya no hay casilla de estacion"    'on_sta_check_toggled|csnhypodbp_sta_sync_check' csnhypodbp_main.c
nck "tabla: sin estado de estacion (gui.c)"   'on_sta_check_toggled|csnhypodbp_sta_sync_check|g_sta_on' csnhypodbp_gui.c
ck  "tabla: casilla lee g_pick_on[] en el bind" 'g_pick_on\[idx\]\[ph\]'                     csnhypodbp_main.c
ck  "tabla: sin scroll horizontal"            'sw_list\), GTK_POLICY_NEVER'                 csnhypodbp_main.c
ck  "tabla: la fila S ya no se esconde"      'gtk_widget_set_visible\(lbl, TRUE\)'         csnhypodbp_main.c
ck  "tabla: casilla del pick vacia si falta"  '\(has && idx >= 0'                          csnhypodbp_main.c
ck  "tabla: residual formateado %+.3f"       '%\+\.3f'                                     csnhypodbp_staitem.c
ck  "tabla: origen Teo"                      '"Teo"'                                       csnhypodbp_staitem.c
ck  "tabla: clic derecho por celda"          'on_sta_cell_right_click'                     csnhypodbp_main.c
ck  "tabla: fuente 0.75em"                    '\.sta-table \{ font-size: 0.75em'            csnhypodbp_main.c
ck  "tabla: borde del marco"                 '\.detail-frame \{ border'                    csnhypodbp_main.c
ck  "tabla: SCNL de la estacion en la fila"  'StaArray\[i\]\.szChannel'                    csnhypodbp_gui.c
ck  "localizador: picadas con red y canal"   'sin canal o sin red'                         csnhypodbp_locate.c
ck  "localizador: ARC con red y loc"         'sol->picks\[i\]\.net\[0\]'                   csnhypodbp_locate.c
ck  "residuales: cada solucion guarda sus picadas" 'event_sol_set_picks'                    csnhypodbp_ws.c
ck  "residuales: se liberan las picadas"      'event_sol_free'                              csnhypodbp_ws.c
ck  "residuales: la elegida trae sus picadas" 'csnhypodbp_event_sol_picks\(row'             csnhypodbp_gui.c
ck  "residuales: la tabla usa la fuente elegida" 'resid_source\(\)'                         csnhypodbp_gui.c
nck "residuales: sin fuente fija del preview" 'g_preview_valid \? &g_preview_sol : NULL'    csnhypodbp_gui.c
ck  "residuales: al localizador sin residual" 'pk->residual = NAN'                          csnhypodbp_statable.c
ck  "residuales: el JSONL manda"              'El JSONL es el residual de ESTA corrida'     csnhypodbp_locate.c
ck  "residuales: la BD los persiste"          'uncertainty\) VALUES'                        ../common/src/ewgui_db.c
ck  "residuales: la BD los lee"               'arr\[i\]\.residual = get_dbl'                ../common/src/ewgui_db.c
ck  "residuales: el ingester los escribe"     'rows\[nrows\]\.residual = pk->residual'      ../hypodb/hypodb_ingest.c
ck  "orden: las 3 tablas con sort model"      'g_sort_(events|detail|sta) = gtk_sort_list_model_new' csnhypodbp_main.c
ck  "orden: sorter de la vista al sort model" 'ewgui_table_enable_sorting'                  csnhypodbp_main.c
ck  "orden: columnas ordenables"              'ewgui_table_column_sortable'                 csnhypodbp_main.c
nck "orden: sin sorter suelto en el maestro"  'gtk_numeric_sorter_new'                      csnhypodbp_main.c
ck  "orden: magnitud con valor numerico"      'nmag%d'                                      csnhypodbp_ws.c
ck  "orden: seleccion por estacion y fase"    'g_list_model_get_n_items\(G_LIST_MODEL\(g_sort_sta\)' csnhypodbp_gui.c
nck "orden: sin seleccion por posicion"       'to_select = 2 \* a'                          csnhypodbp_gui.c
ck  "fechas: un solo formateador UTC"         'csnhypodbp_search_fmt_utc'                   csnhypodbp_search.c
nck "fechas: sin columna O-time en las tablas" '"O-time"'                                    csnhypodbp_main.c
ck  "fechas: columna Created en el detalle"   '"Created"'                                    csnhypodbp_main.c
ck  "fechas: el detalle abre por Created"     'column_at\(tree_detail, 3\)'                  csnhypodbp_main.c
ck  "fechas: la fila expone created"          'g_param_spec_string\("created"'               csnhypodbp_row.c
ck  "fechas: la BD trae creation_time"        'creation_time FROM origin'                    ../common/src/ewgui_db.c
ck  "calendario: ventana modal con GtkCalendar" 'gtk_calendar_new'                           csnhypodbp_gui.c
ck  "calendario: la fecha se lee al aceptar"   'cal_finish'                                  csnhypodbp_gui.c
ck  "calendario: modal propio (bloqueante)"    'g_main_loop_run'                             csnhypodbp_gui.c
ck  "calendario: escribe la fecha y conserva la hora" 'csnhypodbp_search_set_date'           csnhypodbp_gui.c
nck "calendario: sin popovers (WSLg #1299)"   'gtk_popover|gtk_menu_button|gtk_drop_down'     csnhypodbp_gui.c
ck  "calendario: boton junto a su caja"       'gtk_grid_attach\(GTK_GRID\(grid\), btn, 2 \+ 2 \* i' csnhypodbp_gui.c
ck  "calendario: campos de hora anchos"       'time-entry'                                   csnhypodbp_gui.c
nck "ondas: sin LED verde/rojo"              '0\.2, 0\.8, 0\.2'                            csnhypodbp_gui.c

# --- Preview de la solucion (pestana Ondas): tablita de 2 filas ---
ck  "preview: caja con clase propia"        'gtk_widget_add_css_class\(g_preview_box, "preview-box"\)' csnhypodbp_main.c
ck  "preview: siempre visible"              'gtk_widget_set_visible\(g_preview_box, TRUE\)' csnhypodbp_main.c
ck  "preview: cabecera + valores (2 filas)" 'gtk_grid_attach\(GTK_GRID\(grid\), g_preview_val\[i\], i, 1' csnhypodbp_main.c
ck  "preview: 7 valores en la fila"         'g_preview_val\[CSNHYPODBP_PREVIEW_NVALS\]'   csnhypodbp_main.c
ck  "preview: cabeceras del nucleo puro"    'csnhypodbp_preview_headers'                  csnhypodbp_main.c
ck  "preview: botones a la derecha"         'gtk_box_append\(GTK_BOX\(g_preview_box\), pc\)' csnhypodbp_main.c
nck "preview: sin etiqueta de texto unica"  'g_preview_label'                             csnhypodbp_main.c
ck  "preview: fondo rojo leve de la candidata" '\.preview-box\.candidate'                  csnhypodbp_main.c
ck  "preview: estilos del titulo y del valor" '\.preview-title|\.preview-val'              csnhypodbp_main.c
ck  "preview: un solo render"               'csnhypodbp_preview_update'                   csnhypodbp_gui.c
nck "preview: ya no se oculta al descartar" 'gtk_widget_set_visible\(g_preview_box, FALSE\)' csnhypodbp_gui.c
nck "preview: sin la etiqueta vieja"        'g_preview_label'                             csnhypodbp_gui.c
ck  "preview: refresco al elegir solucion"  'la tablita sigue a la solucion elegida'      csnhypodbp_gui.c
ck  "preview: botones sensibles solo con candidata" 'set_sensitive\(g_preview_btn_confirm, v\.candidate\)' csnhypodbp_gui.c
ck  "preview: refresco al llegar magnitudes" 'csnhypodbp_preview_update'                  csnhypodbp_ws.c
ck  "preview: titulos por estado"           'New solution|Selected solution'              csnhypodbp_preview.c
ck  "preview: sin seleccion -> guiones"     'No solution selected'                        csnhypodbp_preview.c

# --- Mapas (columna derecha): planta + perfil ---
ck  "mapas: columna a nivel ventana"        'build_maps_column'                           csnhypodbp_main.c
ck  "mapas: notebook + columna en un paned" 'gtk_paned_set_start_child\(GTK_PANED\(paned_maps\), g_notebook\)' csnhypodbp_main.c
ck  "mapas: dos canvas via map_panel_new"   'map_panel_new\(&g_map_plan'                  csnhypodbp_main.c
ck  "mapas: canvas de planta"               'on_draw_map_plan'                            csnhypodbp_main.c
ck  "mapas: canvas de perfil"               'on_draw_map_profile'                         csnhypodbp_main.c
ck  "mapas: planta usa la proyeccion de csnrv" 'ewgui_geo_project'                        csnhypodbp_gui.c
ck  "mapas: planta dibuja las capas .lines" 'ewgui_overlay_draw'                          csnhypodbp_gui.c
ck  "mapas: imagen base MapImageFile"       'gdk_pixbuf_new_from_file\(MapImageFile'      csnhypodbp_main.c
ck  "mapas: perfil dibuja el slab"          'g_prof_slab'                                 csnhypodbp_gui.c
ck  "mapas: perfil dibuja la topografia"    'g_prof_topo'                                 csnhypodbp_gui.c
ck  "mapas: perfil elige el mas cercano"    'csnhypodbp_profile_nearest'                  csnhypodbp_gui.c
ck  "mapas: un solo refresco"               'void csnhypodbp_maps_refresh'                csnhypodbp_gui.c
ck  "mapas: refresco al elegir solucion"    'y los mapas, al evento elegido'              csnhypodbp_gui.c
ck  "mapas: refresco al elegir evento"      'el evento cambio: perfil y planta nuevos'    csnhypodbp_gui.c
ck  "mapas: refresco desde el preview"      'La candidata del locate tambien se ve'       csnhypodbp_gui.c
ck  "mapas: refresco al llegar un ARC"      'csnhypodbp_maps_refresh'                     csnhypodbp_ws.c
ck  "mapas: marcador de la candidata"       'kind == 2'                                   csnhypodbp_gui.c
ck  "mapas: caption del perfil"             'slab \+ %d topo'                             csnhypodbp_gui.c
ck  "mapas: perfil con x = lon e y = -depth" 'frame_project\(&f, p->lon, -p->depth'        csnhypodbp_gui.c
nck "mapas: sin columna duplicada por pestaña" 'build_maps_column\(\);.*build_maps_column' csnhypodbp_main.c

# --- Puntero (solo clic) y solucion seleccionada ---
ck  "puntero: hit test puro"                 'csnhypodbp_map_hit'                         csnhypodbp_mapview.c
ck  "puntero: umbral clic vs arrastre"       'csnhypodbp_map_drag_exceeded'               csnhypodbp_mapview.c
ck  "puntero: hit test en pixeles de pantalla" 'g_map_plan_sx\[i\] = \(x - g_map_cx\) \* zoom' csnhypodbp_gui.c
ck  "puntero: clic sin arrastre selecciona"  '!g_map_moved && g_map_press_hit'            csnhypodbp_gui.c
ck  "puntero: selecciona en la tabla"        'map_select_row'                             csnhypodbp_gui.c
ck  "puntero: fila nula (candidata) no rompe" 'if \(!row \|\| !g_selection_detail\)'       csnhypodbp_gui.c
ck  "puntero: estado en la descripcion accesible" 'GTK_ACCESSIBLE_PROPERTY_DESCRIPTION'    csnhypodbp_gui.c
nck "puntero: sin resaltado por el puntero"  'map_set_hover'                              csnhypodbp_gui.c
nck "puntero: sin cursor de mano"            'set_cursor_from_name'                       csnhypodbp_gui.c
nck "puntero: sin resaltado amarillo"        '0\.84, 0\.0'                                csnhypodbp_gui.c

# --- Solucion elegida en Origins: verde en los dos mapas ---
ck  "seleccion: helper puro por id"          'csnhypodbp_map_find_id'                     csnhypodbp_mapview.c
ck  "seleccion: verde del seleccionado"      '0\.0, 0\.78, 0\.33'                         csnhypodbp_gui.c
ck  "seleccion: indice por id en el refresco" 'csnhypodbp_map_find_id\(g_map_id'          csnhypodbp_gui.c
ck  "seleccion: la candidata no lleva id"    'g_map_id\[g_map_npts\]\[0\]'                csnhypodbp_gui.c
ck  "seleccion: elegida al final (planta)"   'draw_marker\(cr, sx2, sy2'                  csnhypodbp_gui.c
ck  "seleccion: elegida al final (perfil)"   'draw_marker\(cr, g_prof_sx\[g_map_sel\]'    csnhypodbp_gui.c
nck "seleccion: sin verde en el dibujo normal" 'marker_radius\(p, 0\) / zoom, p->kind, 1\)' csnhypodbp_gui.c
ck  "seleccion: posicion en la vista ordenada" 'g_list_model_get_item\(m, i\)'            csnhypodbp_gui.c
nck "seleccion: sin buscar la posicion en el store" 'g_list_store_find'                    csnhypodbp_gui.c
ck  "seleccion: la preferida por fila, no por posicion 0" 'map_select_row\(CSNHYPODBP_ROW\(first\)\)' csnhypodbp_gui.c

# --- Una P y una S por estacion (dibujo) ---
ck  "dibujo: usa la fila P (sta_row.p)"     'sta_row\.p'                   csnhypodbp_gui.c
ck  "dibujo: usa la fila S (sta_row.s)"     'sta_row\.s'                   csnhypodbp_gui.c
nck "dibujo: sin multiples S por estacion"  'csnhypodbp_cap_find_all'      csnhypodbp_gui.c

# --- Barra inferior: locators/locate a la derecha + filtro + localizador ---
ck  "barra: spacer antes del grupo derecho" 'gtk_widget_set_hexpand\(spacer' csnhypodbp_main.c
ck  "barra: checkbox A"                     'gtk_check_button_new_with_label\("A"\)'   csnhypodbp_main.c
ck  "barra: checkbox M"                     'gtk_check_button_new_with_label\("M"\)'   csnhypodbp_main.c
ck  "barra: checkbox Teo"                   'gtk_check_button_new_with_label\("Teo"\)' csnhypodbp_main.c
ck  "barra: M marcada por defecto"          'g_filter_manual = 1'          csnhypodbp_main.c
ck  "barra: A no marcada por defecto"       'g_filter_auto = 0'            csnhypodbp_main.c
ck  "barra: Teo no marcada por defecto"     'g_filter_theo = 0'            csnhypodbp_main.c
ck  "barra: caja del localizador activo"    'g_locator_label'              csnhypodbp_main.c
ck  "barra: etiqueta del localizador"       'csnhypodbp_update_locator_label' csnhypodbp_gui.c
ck  "barra: botones locators y locate"      'btn_locators'                 csnhypodbp_main.c
ck  "localizador: picadas desde la tabla"   'csnhypodbp_locate_picks'      csnhypodbp_gui.c
ck  "tabla: clave MaxDistKm del .d"          'k_its\("MaxDistKm"\)'         csnhypodbp_main.c

# --- Help en el menu ☰, sin texto de instrucciones ---
nck "sin etiqueta de instrucciones"         'g_help_label'                 csnhypodbp_main.c
nck "sin csnhypodbp_update_help"            'csnhypodbp_update_help'       csnhypodbp_gui.c
ck  "menu: grupo Help al final"             '"Help", help_items'           csnhypodbp_main.c
ck  "menu: item Commands -> win.help"       'win\.help'                    csnhypodbp_main.c
ck  "menu: dialogo de comandos"             'csnhypodbp_help_dialog_run'   csnhypodbp_gui.c

# --- Color de traza no leida ---
ck  "traza no leida: gris mas oscuro 0.55"  '0\.55'                        csnhypodbp_trace.c

if [ "$fails" -gt 0 ]; then echo; echo "$fails FALLOS"; exit 1; fi
echo; echo "OK test_ui_invariants"
