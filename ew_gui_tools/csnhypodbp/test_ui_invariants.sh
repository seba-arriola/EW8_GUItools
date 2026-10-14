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
ck2() { # ck2 <descripcion> <patron linea 1> <patron linea 2> <archivo>
    if grep -A1 -E "$2" "$4" | grep -qE "$3"; then echo "ok  : $1"
    else echo "FAIL: $1"; fails=$((fails+1)); fi
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
ck  "preview: radios + cabecera + valores"    'gtk_grid_attach\(GTK_GRID\(grid\), g_preview_val\[i\], i, 2' csnhypodbp_main.c
ck  "preview: radios en la fila 0"           'gtk_grid_attach\(GTK_GRID\(grid\), g_preview_radio\[k\], i, 0' csnhypodbp_main.c
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

# --- Pestañas: raiz Eventos/Procesar con sub-pestañas picking/magnitudes ---
ck  "pestañas: raiz Procesar"               'gtk_label_new\("Procesar"\)'                 csnhypodbp_main.c
ck  "pestañas: sub-pestaña picking"         'gtk_label_new\("picking"\)'                  csnhypodbp_main.c
ck  "pestañas: sub-pestaña magnitudes"      'gtk_label_new\("magnitudes"\)'               csnhypodbp_main.c
ck  "pestañas: pagina activa por widget"    'gtk_notebook_get_nth_page\(GTK_NOTEBOOK\(g_notebook\)' csnhypodbp_gui.c
ck  "pestañas: el menu compara el widget"   'page == g_page_procesar'                     csnhypodbp_main.c
ck  "pestañas: ir a Procesar por widget"    'gtk_notebook_page_num\(GTK_NOTEBOOK\(g_notebook\), g_page_procesar\)' csnhypodbp_gui.c
nck "pestañas: sin dependencia del indice"  'get_current_page\(GTK_NOTEBOOK\(g_notebook\)\) == 1' csnhypodbp_gui.c

# --- Parametros de magnitud: se leen del .d de csnmags, no se duplican ---
ck  "magnitudes: clave MagConfigFile"       'MagConfigFile'                               csnhypodbp_main.c
ck  "magnitudes: defaults del .d de csnmags" 'csnhypodbp_magcfg_load\(&g_magcfg'           csnhypodbp_main.c
ck  "magnitudes: ventana por magnitud"      'csnhypodbp_magcfg_window'                    csnhypodbp_magcfg.c
ck  "magnitudes: ventanas por (estacion, magnitud)" 'csnhypodbp_magmarks_set'            csnhypodbp_magmarks.c
ck  "magnitudes: agarre de la franja"       'csnhypodbp_magmarks_grab'                    csnhypodbp_magmarks.c
ck  "magnitudes: traza de vista por magnitud" 'csnhypodbp_magview_build'                  csnhypodbp_magview.c
ck  "magnitudes: Ms diezmado antes del paso banda" 'ewgui_decimate'                       csnhypodbp_magview.c
ck  "magnitudes: Wood-Anderson del .d"      'WoodAndersonCoefs'                           csnhypodbp_magcfg.c
ck  "magnitudes: una fila por estacion"     'csnhypodbp_magrow_build'                     csnhypodbp_magrow.c
ck  "magnitudes: residual contra la red"    'r->res = v->mag - red'                       csnhypodbp_magrow.c
ck  "magnitudes: canales por el helper compartido" 'csnhypodbp_magchan_parse\(payload, len, &ch\)' csnhypodbp_ws.c
ck  "magnitudes: el bucle respeta el tope de vals"  'n < CSNHYPODBP_MAGSTA_MAX; i\+\+'              csnhypodbp_ws.c
nck "magnitudes: ya no deduce canales restando la linea resumen" 'nch -= 1'                      csnhypodbp_ws.c
nck "magnitudes: sin rd_chan_mag con tamano propio" 'rd_chan_mag\(\(char \*\)payload'            csnhypodbp_ws.c
ck  "magnitudes: cache por solucion y magnitud" 'csnhypodbp_magsta_put'                   csnhypodbp_ws.c
ck  "magnitudes: cae a la BD si no hay vivo" 'ewguidb_query_station_magnitudes'           csnhypodbp_ws.c
ck  "magnitudes: prefiere la ajustada"      'CSNHYPODBP_MAG_MANUAL_ALGO'                  csnhypodbp_ws.c
ck  "magnitudes: origin_id en la fila"      'csnhypodbp_row_set_origin_id'                csnhypodbp_ws.c
ck  "magnitudes: tabla una fila por estacion" 'CSNHYPODBP_TYPE_MAG_ITEM'                  csnhypodbp_main.c
ck  "magnitudes: columnas de la tabla"      'add_mag_text_column\(g_mag_view, "mag"'      csnhypodbp_main.c
ck  "magnitudes: ancla y ventana en la tabla" 'add_mag_text_column\(g_mag_view, "ancla"'  csnhypodbp_main.c
ck  "magnitudes: canvas de la traza procesada" 'ewgui_canvas_set_draw\(g_mag_canvas, on_draw_mag' csnhypodbp_main.c
ck  "magnitudes: arrastre de la franja"     'gtk_gesture_drag_new'                        csnhypodbp_main.c
ck  "magnitudes: seleccion de estacion"     'G_CALLBACK\(on_mag_row_selected\)'            csnhypodbp_main.c
ck  "magnitudes: vistas cacheadas por clave" 'mag_views_sync'                             csnhypodbp_gui.c
ck  "magnitudes: trazo continuo (polyline)"  'csnhypodbp_view_use_polyline'                csnhypodbp_gui.c
ck  "magnitudes: banda por tramos"          'csnhypodbp_view_band_runs'                   csnhypodbp_gui.c
ck  "magnitudes: alto del canvas por canales" 'csnhypodbp_mag_canvas_fit'                 csnhypodbp_gui.c
ck  "magnitudes: canvas con scroll"         'scrolled_window_set_child\(GTK_SCROLLED_WINDOW\(sw_magcv\)' csnhypodbp_main.c
ck  "preview: tablita compartida en Procesar" 'gtk_box_append\(GTK_BOX\(page_procesar\), g_preview_box\)' csnhypodbp_main.c
ck  "preview: g_page_procesar es la vbox"   'g_page_procesar = page_procesar'            csnhypodbp_main.c
ck  "preview: las cuatro magnitudes"        '"Mb", "Ms"'                                 csnhypodbp_preview.c
ck  "magnitudes: filtro A/M/Teo"            'g_magf_auto, g_magf_manual'                 csnhypodbp_gui.c
ck  "magnitudes: barra inferior de ventanas" 'gtk_check_button_new_with_label\(lbl\)'     csnhypodbp_main.c
ck  "magnitudes: override de distancia"     'csnhypodbp_magcfg_apply_override'           csnhypodbp_magcfg.c
ck  "magnitudes: oculta fuera de la puerta" 'if \(!g_mag_rows\[i\]\.in_range\) continue' csnhypodbp_gui.c
ck  "magnitudes: puertas en print-config"   'MagGate'                                    csnhypodbp_main.c
ck  "magnitudes: todas las componentes"     'if \(strcmp\(StaArray\[i\]\.szStation, sta\) != 0\) continue;' csnhypodbp_gui.c
ck  "magnitudes: banda sin traza"           'cairo_show_text\(cr, "sin traza"\);'        csnhypodbp_gui.c
ck  "fetch: asegura componentes de todas"   'ensure_all_station_channels\(\);'           csnhypodbp_ws.c
ck  "layout: canvas con alto minimo"        'CSNHYPODBP_BAND_MIN'                        csnhypodbp_gui.c
ck  "layout: canvas con ancho minimo"       'CSNHYPODBP_CANVAS_MIN_W'                    csnhypodbp_main.c
ck  "layout: el paned de mapas deja encoger" 'set_shrink_end_child\(GTK_PANED\(paned_maps\), TRUE\)' csnhypodbp_main.c
ck  "layout: barra en scrolled window"      'gtk_scrolled_window_set_child\(GTK_SCROLLED_WINDOW\(barsw\), bar\)' csnhypodbp_main.c
ck  "barra: grupo a la derecha"             'gtk_widget_set_hexpand\(spacer, TRUE\)'      csnhypodbp_main.c
ck  "barra: reset en vez de auto"           'g_mag_ov_reset'                             csnhypodbp_main.c
ck  "barra: unidad solo en la cabecera"     'gtk_label_new\("distancia \[°\]"\)'          csnhypodbp_main.c
ck  "barra: grupo profundidad con piso"     'gtk_label_new\("prof\. \[km\]"\)'            csnhypodbp_main.c
ck  "barra: editar activa el override"      'ov->on = 1;'                                csnhypodbp_gui.c
ck  "barra: corte de profundidad"           'csnhypodbp_magcfg_depth_ok'                 csnhypodbp_gui.c
ck  "barra: reset al cambiar de solucion"   'csnhypodbp_mag_ov_reset_all\(\);'           csnhypodbp_gui.c
ck  "ancla: handle dibujado"                'csnhypodbp_magmarks_handle_hit'             csnhypodbp_gui.c
ck  "ancla: solo el handle arrastra"        'if \(!csnhypodbp_magmarks_handle_hit\(y, MAG_BAND_TOP\)\) return;' csnhypodbp_gui.c
ck  "ancla: zoom por dos clics"             'csnhypodbp_view_click\(&g_mag_zoomview'     csnhypodbp_gui.c
ck  "ancla: zoom se resetea"                'csnhypodbp_view_set_default\(&g_mag_zoomview' csnhypodbp_gui.c
ck  "ancla: gesto de clic en magnitudes"    'on_mag_canvas_clicked'                      csnhypodbp_main.c
ck  "mapa: clave MapZoom"                    'k_its\("MapZoom"\)'                         csnhypodbp_main.c
ck  "mapa: clave MapSplit"                   'k_its\("MapSplit"\)'                        csnhypodbp_main.c
ck  "mapa: reparto configurable"             'h \* g_map_split_frac'                      csnhypodbp_main.c
ck  "mapa: claves en print-config"           'MapSplit %.4f'                              csnhypodbp_main.c
ck  "mapa: centro en la referencia"          'csnhypodbp_mapfit_at\('                     csnhypodbp_gui.c
ck  "mapa: la referencia es la verde"        'g_map_sel >= 0 && g_map_sel < g_map_npts'   csnhypodbp_gui.c
ck  "magnitudes: una sola ventana efectiva"  'csnhypodbp_magview_frame\(const CsnhypodbpView' csnhypodbp_magview.h
ck  "magnitudes: helper mag_frame"           'static int mag_frame\(double \*win0, double \*span\)' csnhypodbp_gui.c
ck  "magnitudes: el frame usa la vista vigente" 'csnhypodbp_magview_frame\(mag_view_now\(\)' csnhypodbp_gui.c
ck  "magnitudes: freeze = frame efectivo"    'g_mag_freeze_view.active = 1;'              csnhypodbp_gui.c
ck  "magnitudes: vista vigente del arrastre" 'return g_mag_freeze \? &g_mag_freeze_view'  csnhypodbp_gui.c
ck  "magnitudes: ancla del zoom dibujada"   'g_mag_zoomview.have_anchor'                 csnhypodbp_gui.c
ck  "magnitudes: eje temporal con ticks"     'csnhypodbp_view_ticks\(win0, win0 \+ span'  csnhypodbp_gui.c
ck  "magnitudes: eje con arrastre de zoom"   'csnhypodbp_view_drag_end\(&g_mag_axis_drag'  csnhypodbp_gui.c
ck  "magnitudes: doble clic recentra"        'csnhypodbp_view_set_default\(&g_mag_zoomview, 0.0\);' csnhypodbp_gui.c
ck  "magnitudes: canvas del eje en la pagina" 'g_mag_axis_canvas = ewgui_canvas_new\(\)'   csnhypodbp_main.c
ck  "magnitudes: puntero segun zona"         'gtk_widget_set_cursor_from_name'            csnhypodbp_gui.c
ck  "magnitudes: controlador de movimiento"  'G_CALLBACK\(on_mag_canvas_motion\)'          csnhypodbp_main.c
ck  "calculate: boton en la barra"           'gtk_button_new_with_label\("calculate"\)'    csnhypodbp_main.c
ck  "calculate all: boton en la barra"       'gtk_button_new_with_label\("calculate all"\)' csnhypodbp_main.c
ck  "calculate: barra compacta"              'pango_attr_scale_new\(0.75\)'                csnhypodbp_main.c
ck  "calculate: no relocaliza"               'job->mags_only = mags_only;'                 csnhypodbp_gui.c
ck  "calculate: corrida de magnitudes"       'csnhypodbp_mag_run\('                        csnhypodbp_gui.c
ck  "calculate: mascara por magnitud"        'g_mag_calc_mask = 1u << g_mag_kind;'         csnhypodbp_gui.c
ck  "calculate: ventanas ajustadas al motor" 'WindowOverridesFile %s'                      csnhypodbp_locate.c
ck  "calculate: filas de ventanas"           'csnhypodbp_mag_ovr_write\('                  csnhypodbp_locate.c
ck  "calculate: solo filas usadas"           'g_mag_rows\[i\].used'                        csnhypodbp_gui.c
ck  "traza: etiqueta sin distancia"          'szChannel, 0\.0\);'                          csnhypodbp_gui.c
nck "picking: sin titulo historico"          'Historical Data'                            csnhypodbp_gui.c
ck  "picking: cursor segun zona"             'csnhypodbp_view_cursor'                      csnhypodbp_gui.c
ck  "puntero: linea en los dos canvas"       'draw_cursor_line'                            csnhypodbp_gui.c
ck  "puntero: x guardada en magnitudes"      'g_mag_cursor_x'                              csnhypodbp_gui.c
ck  "margen: traza ancha"                    'g_margin_left = 44'                          csnhypodbp_main.c
ck  "ancla mag: proyectada con su margen"    'g_margin_left \+ csnhypodbp_view_sample_x'   csnhypodbp_gui.c
ck  "ancla mag: clic y dibujo misma escala"  'mag_draw_w\(gtk_widget_get_width'            csnhypodbp_gui.c
ck  "calculate: filtro de la barra mag"      'filter.auto_on = g_magf_auto;'               csnhypodbp_gui.c
ck  "calculate: casillas used al envio"      'csnhypodbp_pick_on_from_used'                csnhypodbp_gui.c
ck  "calculate: mapa de filas mag->ondas"    'g_mag_sta_idx'                               csnhypodbp_gui.c
ck  "calculate: guard de 0 picadas"          'sin picadas para el calculo'                 csnhypodbp_locate.c
ck  "calculate: corrida vacia = fallo"       'csnhypodbp_mag_result_ok'                    csnhypodbp_locate.c
ck  "calculate: motivo de csnmags en el msg" 'first_line\(erp'                             csnhypodbp_locate.c
ck  "calculate: fallo distinguible del locate" '"calculate" : "locate", job->locator'     csnhypodbp_gui.c
ck  "locate: ventanas ajustadas al motor"    'mag_cfg_with_overrides'                      csnhypodbp_locate.c
ck  "locate: filas de ventana en locate_run" 'ovr && novr > 0'                             csnhypodbp_locate.c
ck  "locate: helper de ventanas reutilizado" 'mag_ovr_filtered'                             csnhypodbp_locate.c
ck  "locate: la GUI pasa las ventanas"       'job->ovr, job->novr, job->kind_mask'         csnhypodbp_gui.c
ck  "preferida: radios en el cuadro"         'g_preview_radio\[k\] = gtk_check_button_new' csnhypodbp_main.c
ck  "preferida: radios en grupo (exclusivos)" 'gtk_check_button_set_group'                  csnhypodbp_main.c
ck  "preferida: defecto con la regla de csnmags" 'csnhypodbp_mag_pref_default'              csnhypodbp_gui.c
ck  "preferida: radio insensible sin valor"  'gtk_widget_set_sensitive\(GTK_WIDGET\(g_preview_radio' csnhypodbp_gui.c
ck  "preferida: confirm la recibe"           'g_preview_pref_kind);'                        csnhypodbp_gui.c
ck  "preferida: se publican las 4 magnitudes" 'emit_mag_scnl'                               csnhypodbp_ws.c
ck  "preferida: marca manual en algorithm"   'CSNMan'                                      csnhypodbp_ws.c
ck  "preferida: 13.er token en la picada"    '%c M %c'                                     csnhypodbp_ws.c
ck  "picking: ancla congelada (no la P ganadora)" 'g_win_ref_pick'                          csnhypodbp_gui.c
nck "picking: el dibujo no re-ancla con la P" 'sta_pick_t'                                   csnhypodbp_gui.c
nck "picking: sin earliest_pick"              'earliest_pick'                               csnhypodbp_gui.c
ck  "picking: refresco del ancla"             'csnhypodbp_pick_anchor_refresh'              csnhypodbp_gui.c
ck  "picking: refresco solo al deshacer zoom" 'csnhypodbp_view_reanchor_on'                 csnhypodbp_gui.c
ck  "picking: fila de la estacion reutilizable" 'selected_station_row'                      csnhypodbp_gui.c
ck  "marca: indicador de borde"               'csnhypodbp_view_mark_x'                      csnhypodbp_gui.c
nck "marca: sin filtro que la oculte"         'rel >= g_view\.t0 && rel <= g_view\.t1'      csnhypodbp_gui.c
ck  "picado: refresca la tabla y el envio"    'pick_overlay_changed'                        csnhypodbp_gui.c
ck  "ancla: solo el RESET re-ancla"           'act == CSNHYPODBP_ACT_RESET'                 csnhypodbp_view.c
# --- Vista pegajosa ante llegadas del anillo (no cambiar lo que se mira) ---
ck  "llegada: devuelve la seleccion al evento del usuario" 'sticky_reselect'                   csnhypodbp_ws.c
nck "llegada: ya no reselecciona al que llega" 'reselect_event_by_row\(g->preferred\)'          csnhypodbp_ws.c
ck  "llegada: refresca el detalle del evento visto" 'csnhypodbp_detail_refresh'                 csnhypodbp_ws.c
ck  "llegada: guarda de reentrada de la seleccion" 'g_ring_sync'                                csnhypodbp_ws.c
ck  "vista: el reset solo con cambio del usuario" 'csnhypodbp_view_should_reset'                csnhypodbp_gui.c
ck  "detalle: conserva la solucion elegida"       'csnhypodbp_view_solution_same'               csnhypodbp_gui.c
ck  "perfil: el activo no se re-elige en llegadas" 'g_prof_pick_key'                            csnhypodbp_gui.c
ck  "fetch: no borra lo que no va a repoblar"     'csnhypodbp_fetch_clear_on'                   csnhypodbp_ws.c
ck  "mapa: la candidata se dibuja con la estrella" 'csnhypodbp_marker_star_path'                csnhypodbp_gui.c
ck  "mapa: estrella pura en mapview"              'csnhypodbp_marker_star_path'                 csnhypodbp_mapview.c
nck "mapa: la candidata ya no es el circulo rosa" 'cairo_set_source_rgb\(cr, 0\.99, 0\.91, 0\.91\)' csnhypodbp_gui.c
# --- Orden de las tablas (contrato en docs/DATA-MAPPING.md, seccion 5) ------
ck2 "orden: Events por Date DESC"          'ewgui_table_enable_sorting\(GTK_COLUMN_VIEW\(tree_global\), g_sort_events, cdate,' 'EWGUI_TABLE_DESC' csnhypodbp_main.c
ck2 "orden: Origins por Created DESC"      'ewgui_table_enable_sorting\(GTK_COLUMN_VIEW\(tree_detail\), g_sort_detail, ccreated,' 'EWGUI_TABLE_DESC' csnhypodbp_main.c
ck  "orden: estaciones sin orden inicial"  'ewgui_table_enable_sorting\(GTK_COLUMN_VIEW\(g_sta_view\), g_sort_sta, NULL, 0\)' csnhypodbp_main.c
ck  "orden: magnitudes sin orden inicial"  'ewgui_table_enable_sorting\(GTK_COLUMN_VIEW\(g_mag_view\), g_mag_sort, NULL, 0\)' csnhypodbp_main.c
nck "orden: Origins ya no se ordena por origin_time" 'ORDER BY origin_time, origin_id' ../common/src/ewgui_db.c
ck  "magnitudes: casilla inerte sin traza"  'g_mag_rows\[idx\].has_data'                  csnhypodbp_main.c
ck  "magnitudes: ventana por defecto del .d" 'csnhypodbp_magcfg_window\(&g_magcfg'         csnhypodbp_gui.c
ck  "magnitudes: radios del selector"       'csnhypodbp_magcfg_name\(k\)'                 csnhypodbp_main.c
ck  "magnitudes: casilla de uso por estacion" 'on_mag_check_toggled'                      csnhypodbp_main.c
ck  "magnitudes: se repuebla al elegir solucion" 'rebuild_mag_list\(\);'                   csnhypodbp_gui.c
ck  "magnitudes: linea de estado"           'csnhypodbp_mag_status_update'                csnhypodbp_gui.c
nck "magnitudes: sin placeholder"           'pendiente: F2/F3'                            csnhypodbp_main.c
nck "magnitudes: sin texto de F3"           'traza procesada y ventanas'                  csnhypodbp_main.c

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

# --- Ventana: el minimo debe caber en el maximizado (AGENTS §7) --------------
# Si el minimo de la ventana supera la geometria maximizada que da el compositor,
# GTK adjunta un buffer mas grande que el configurado y Wayland corta la conexion
# (`Error 71`): el modulo se CIERRA al maximizar. Nada de suelos grandes, ni de
# `shrink_*(FALSE)`; el minimo de las tablas lo acota `min_content_width`.
ck  "ventana: el panel Mww no infla el minimo"   'mww_scrolled\(g_mww\.grid, 90\)'    csnhypodbp_mww.c
ck  "ventana: idem para la salida del motor"     'mww_scrolled\(g_mww\.view, 90\)'    csnhypodbp_mww.c
nck "ventana: sin size_request en mww_scrolled"  'gtk_widget_set_size_request\(sw, -1, h\)' csnhypodbp_mww.c
ck  "ventana: la playera con suelo pequeno"      'gtk_widget_set_size_request\(vbox, 120, 120\)' csnhypodbp_mww.c
ck  "ventana: suelo pequeno del canvas de planta" '&plan_w, 160, 140'                 csnhypodbp_main.c
ck  "ventana: suelo pequeno del canvas de perfil" '&prof_w, 160, 100'                 csnhypodbp_main.c
nck "ventana: sin suelos de 300 px"              '&(plan|prof)_w, 300, [0-9]+'         csnhypodbp_main.c
nck "ventana: ya no fija el minimo de los mapas" 'set_shrink_end_child\(GTK_PANED\(paned_maps\), FALSE\)' csnhypodbp_main.c
ck  "ventana: min_content_width en Events"       'set_min_content_width\(GTK_SCROLLED_WINDOW\(sw_master\), 320\)' csnhypodbp_main.c
ck  "ventana: min_content_width en Origins"      'set_min_content_width\(GTK_SCROLLED_WINDOW\(sw_detail\), 320\)' csnhypodbp_main.c
ck  "ventana: min_content_width en estaciones"   'set_min_content_width\(GTK_SCROLLED_WINDOW\(sw_list\), 240\)'   csnhypodbp_main.c
ck  "ventana: min_content_width en magnitudes"   'set_min_content_width\(GTK_SCROLLED_WINDOW\(sw_mag\), 240\)'    csnhypodbp_main.c
ck  "ventana: columnas de texto redimensionables" 'gtk_column_view_column_set_resizable\(col, TRUE\)' csnhypodbp_main.c
ck  "ventana: reparto diferido una sola vez (maps)"   'csn-split-maps'                csnhypodbp_main.c
ck  "ventana: reparto diferido una sola vez (maestro)" 'csn-split-half'               csnhypodbp_main.c
ck  "ventana: hook de diagnostico del maximizado" 'CSNHYPODBP_MAXIMIZE_MS'            csnhypodbp_main.c

# --- Puntero (solo clic) y solucion seleccionada ---
ck  "puntero: hit test puro"                 'csnhypodbp_map_hit'                         csnhypodbp_mapview.c
ck  "puntero: umbral clic vs arrastre"       'csnhypodbp_map_drag_exceeded'               csnhypodbp_mapview.c
ck  "puntero: hit test en pixeles de pantalla" 'g_map_plan_sx\[i\] = \(x - g_map_cx\) \* zoom' csnhypodbp_gui.c
ck  "puntero: clic sin arrastre selecciona"  '!g_map_moved && g_map_press_hit'            csnhypodbp_gui.c
ck  "puntero: selecciona en la tabla"        'map_select_row'                             csnhypodbp_gui.c
ck  "puntero: fila nula (candidata) no rompe" 'if \(!row \|\| !g_selection_detail\)'       csnhypodbp_gui.c
ck  "puntero: estado en la descripcion accesible" 'GTK_ACCESSIBLE_PROPERTY_DESCRIPTION'    csnhypodbp_gui.c
nck "puntero: sin resaltado por el puntero"  'map_set_hover'                              csnhypodbp_gui.c
nck "puntero: sin cursor de mano"            '"pointer"\|"grab"\|"grabbing"'              csnhypodbp_gui.c
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
