#include "csnhypodbp.h"

/* Forward declarations de los helpers estaticos de alineacion por P */
static double station_pick_time(int i, int *is_real);
static double station_win_start(int i);

void actualizar_altura_canvas() {
    if (!canvas_global) return;
    int count = 0;
    for (int i = 0; i < NumEstaciones; i++) {
        if (bHasData[i] == 1 && (g_StaDist[i] * 111.19) <= g_max_dist_km) count++;
    }
    if (count > 0) gtk_widget_set_size_request(canvas_global, -1, (count * g_spacing) + 60);
    else gtk_widget_set_size_request(canvas_global, -1, 600);
}

void on_btn_fetch_clicked(GtkWidget *widget, gpointer data) {
    if (!entry_dist || !entry_time) return;
    double new_dist = atof(gtk_entry_get_text(GTK_ENTRY(entry_dist)));
    double new_time_min = atof(gtk_entry_get_text(GTK_ENTRY(entry_time)));
    gboolean changed = FALSE;
    if (new_dist > 0.0) { g_max_dist_km = new_dist; changed = TRUE; }
    if (new_time_min > 0.0) {
        if (new_time_min > 10.0) { new_time_min = 10.0; gtk_entry_set_text(GTK_ENTRY(entry_time), "10"); }
        g_dScreenTime = new_time_min * 60.0; 
        changed = TRUE;
    }
    if (changed) {
        actualizar_altura_canvas();
        if (canvas_global) gtk_widget_queue_draw(canvas_global);
        /* Re-fetch: al variar la ventana/distancia se descarga mas datos
           (la ventana de fetch escala con g_dScreenTime). */
        pending_waveform_reload = TRUE;
        logit("t", ">> Fetch UI: Mostrando estaciones hasta %.1f km, ventana de %.1f min\n", g_max_dist_km, g_dScreenTime/60.0);
    }
}

void color_rows_func(GtkTreeViewColumn *col, GtkCellRenderer *rend, GtkTreeModel *model, GtkTreeIter *iter, gpointer data) {
    GtkTreePath *path = gtk_tree_model_get_path(model, iter);
    if (path) {
        gint *indices = gtk_tree_path_get_indices(path);
        GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree_global));
        if (gtk_tree_selection_path_is_selected(selection, path)) {
            g_object_set(rend, "cell-background", "#007bff", "foreground", "#ffffff", "weight", 700, NULL);
        } else {
            if (indices && indices[0] % 2 == 0) g_object_set(rend, "cell-background", "#ffcece", "foreground", "#000000", "weight", 400, NULL);
            else g_object_set(rend, "cell-background", "#ffffff", "foreground", "#000000", "weight", 400, NULL);
        }
        gtk_tree_path_free(path);
    }
}

gboolean on_key_press(GtkWidget *widget, GdkEventKey *event, gpointer data) {
    if (!edit_mode) return FALSE; 
    if (event->keyval == GDK_KEY_Up) { g_zoom_factor *= 1.5; if (canvas_global) gtk_widget_queue_draw(canvas_global); return TRUE; } 
    else if (event->keyval == GDK_KEY_Down) { g_zoom_factor /= 1.5; if (canvas_global) gtk_widget_queue_draw(canvas_global); return TRUE; }
    return FALSE;
}

void on_row_selected(GtkTreeSelection *selection, gpointer data) {
    if (edit_mode) return;
    GtkTreeIter iter; GtkTreeModel *model;
    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        gchar *id_str; double otime, lat, lon; int qid;
        gtk_tree_model_get(model, &iter, 8, &id_str, 14, &otime, 16, &qid, 17, &lat, 18, &lon, -1);
        if (id_str) { strcpy(selected_id, id_str); g_free(id_str); }
        selected_otime = otime; selected_qid = qid; selected_lat = lat; selected_lon = lon;
        g_zoom_factor = 1.0; 
        gtk_widget_set_sensitive(btn_repick, TRUE);

        /* Debounce (portado de EW7 waveform_reload_timer): la recarga de
           waveforms se difiere al timer de 3 s para no bloquear la UI al
           navegar rapidamente por la tabla. */
        pending_waveform_reload = TRUE;
    } else {
        gtk_widget_set_sensitive(btn_repick, FALSE);
    }
}

gboolean waveform_reload_timer(gpointer data) {
    if (pending_waveform_reload && !edit_mode && selected_qid != 0) {
        pending_waveform_reload = FALSE;
        if (entry_dist) g_max_dist_km = atof(gtk_entry_get_text(GTK_ENTRY(entry_dist)));
        if (entry_time) g_dScreenTime = atof(gtk_entry_get_text(GTK_ENTRY(entry_time))) * 60.0;
        if (window_global) gtk_window_set_title(GTK_WINDOW(window_global), "CSNhypodbp - Descargando ondas historicas...");
        FetchWaveformsForEvent(selected_otime, selected_lat, selected_lon, selected_qid);
        if (window_global) gtk_window_set_title(GTK_WINDOW(window_global), "CSNhypodbp - Hypocenter database picker (EW8)");
        if (canvas_global) {
            actualizar_altura_canvas();
            gtk_widget_queue_draw(canvas_global);
        }
    }
    return TRUE;
}

void on_btn_repick_clicked(GtkWidget *widget, gpointer data) {
    if (selected_qid == 0) return;
    if (!edit_mode) {
        edit_mode = TRUE; g_zoom_factor = 1.0; 
        gtk_widget_set_sensitive(tree_global, FALSE); 
        gtk_button_set_label(GTK_BUTTON(btn_repick), "Finish");
        gtk_widget_set_name(btn_repick, "btn_relocate");
        if (box_fetch) gtk_widget_show_all(box_fetch);
        gtk_widget_queue_draw(canvas_global);
    } else {
        edit_mode = FALSE; g_zoom_factor = 1.0; 
        gtk_widget_set_sensitive(tree_global, TRUE); 
        gtk_button_set_label(GTK_BUTTON(btn_repick), "Repick mode");
        gtk_widget_set_name(btn_repick, "btn_repick");
        if (box_fetch) gtk_widget_hide(box_fetch);
        GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree_global));
        g_signal_emit_by_name(selection, "changed");
    }
}

gboolean on_canvas_clicked(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    if (!edit_mode || event->button != 1) return TRUE; 
    int width = gtk_widget_get_allocated_width(widget);
    int margin_right = 10;
    int draw_width = width - g_margin_left - margin_right;
    
    if (event->x <= g_margin_left || event->x >= width - margin_right) return TRUE;

    int target = -1;
    for (int n = 0; n < g_NumSortedNodes; n++) {
        if (event->y >= g_SortedNodes[n].y_top && event->y <= g_SortedNodes[n].y_bottom) { target = n; break; }
    }
    if (target == -1) return TRUE;

    int i = g_SortedNodes[target].idx;
    /* Alineacion por P: el tiempo absoluto del click se calcula usando
       la ventana propia de la estacion (winStart), no la global. */
    double winStart = station_win_start(i);
    double fraction = (event->x - g_margin_left) / (double)draw_width;
    double clicked_time = winStart + fraction * g_dScreenTime;
    StaArray[i].dManualPickTime = clicked_time;
    
    char out_msg[256], time_str[32];
    time_t t_sec = (time_t)clicked_time;
    double t_msec = clicked_time - (double)t_sec;
    struct tm *ptm = gmtime(&t_sec);
    
    snprintf(time_str, sizeof(time_str), "%04d%02d%02d%02d%02d%06.3f", 
             ptm->tm_year + 1900, ptm->tm_mon + 1, ptm->tm_mday, 
             ptm->tm_hour, ptm->tm_min, (double)ptm->tm_sec + t_msec);
             
    for (int k = 0; time_str[k] != '\0'; k++) { if (time_str[k] == ',') time_str[k] = '.'; }
    static unsigned char pick_seq = 0;
    /* Formato TYPE_PICK_SCNL: type mod inst seq S.C.N.L fmwt time amp1 amp2 amp3
       (mismo formato que csntvp, ver ew2glass ConvertAndSendPick). */
    snprintf(out_msg, sizeof(out_msg), "%d %d %d %d %s.%s.%s.%s ?0 %s 0 0 0\n", 
             TypePickSCNL, MyModId, MyInstId, pick_seq++, 
             StaArray[i].szStation, StaArray[i].szChannel, StaArray[i].szNetID, StaArray[i].szLocation, time_str);
             
    MSG_LOGO logo = {MyInstId, MyModId, TypePickSCNL};
    if (tport_putmsg(&PRegion, &logo, strlen(out_msg), out_msg) != PUT_OK) {
        logit("e", "csnhypodbp: Error inyectando pick manual.\n");
    } else {
        logit("t", "csnhypodbp: MANUAL PICK INYECTADO -> %s", out_msg);
    }
    gtk_widget_queue_draw(widget);
    return TRUE;
}

/* --------------------------------------------------------------------
 * Helpers de alineacion por onda P (portado de EW7 new_hypo_display):
 *  - station_pick_time: marca P de la estacion (primer pick real del
 *    cache HYPO_RING). Si no hay pick, se usa el tiempo de origen como
 *    referencia (marca "teorica").
 *  - station_win_start: inicio de ventana tal que la marca P cae sobre
 *    la linea de referencia x_ref.
 * -------------------------------------------------------------------- */
static double station_pick_time(int i, int *is_real) {
    double pt = 0.0;
    if (is_real) *is_real = 0;
    for (int p = 0; p < StaArray[i].iNumPicks; p++) {
        if (StaArray[i].picks[p].dTime > 1.0) {
            pt = StaArray[i].picks[p].dTime;
            if (is_real) *is_real = 1;
            break;
        }
    }
    if (pt < 1.0) pt = selected_otime;
    return pt;
}

static double station_win_start(int i) {
    return station_pick_time(i, NULL) - g_align_lead;
}

/* --------------------------------------------------------------------
 * ApplySelectedFilter: aplica el filtro IIR seleccionado a todas las
 * estaciones con datos, validando Nyquist y quitando el DC antes.
 * Portado de EW7 new_hypo_display (ApplySelectedFilter).
 * -------------------------------------------------------------------- */
void ApplySelectedFilter(void) {
    double f1 = 0.7, f2 = 2.0;
    int order = 4;

    if (entry_freq1) f1 = atof(gtk_entry_get_text(GTK_ENTRY(entry_freq1)));
    if (entry_freq2) f2 = atof(gtk_entry_get_text(GTK_ENTRY(entry_freq2)));
    if (combo_order) {
        int active = gtk_combo_box_get_active(GTK_COMBO_BOX(combo_order));
        order = (active == 0) ? 2 : 4;
    }

    for (int i = 0; i < NumEstaciones; i++) {
        if (StaArray[i].lRawCircCtr <= 0) continue;
        if (StaArray[i].lRawCircCtr > StaArray[i].lRawCircSize)
            StaArray[i].lRawCircCtr = StaArray[i].lRawCircSize;

        double nyquist = StaArray[i].dSampRate / 2.0;
        double safe_f1 = f1;
        double safe_f2 = f2;

        if (g_filter_type != 0) {
            if (safe_f1 >= nyquist) safe_f1 = nyquist * 0.95;
            if (g_filter_type == 3) {
                if (safe_f2 >= nyquist) safe_f2 = nyquist * 0.95;
                if (safe_f1 >= safe_f2) safe_f1 = safe_f2 * 0.5;
            }
        }

        demean_trace_station(&StaArray[i]);

        if (g_filter_type == 1) {
            aplicar_filtro_iir_int32(StaArray[i].plFiltCircBuff, StaArray[i].lRawCircCtr, StaArray[i].dSampRate, 1, safe_f1, order);
        } else if (g_filter_type == 2) {
            aplicar_filtro_iir_int32(StaArray[i].plFiltCircBuff, StaArray[i].lRawCircCtr, StaArray[i].dSampRate, 2, safe_f1, order);
        } else if (g_filter_type == 3) {
            aplicar_filtro_iir_int32(StaArray[i].plFiltCircBuff, StaArray[i].lRawCircCtr, StaArray[i].dSampRate, 1, safe_f1, order);
            aplicar_filtro_iir_int32(StaArray[i].plFiltCircBuff, StaArray[i].lRawCircCtr, StaArray[i].dSampRate, 2, safe_f2, order);
        }
    }
}

void on_filter_changed(GtkComboBox *widget, gpointer data) {
    g_filter_type = gtk_combo_box_get_active(widget);

    gboolean is_hp_lp = (g_filter_type == 1 || g_filter_type == 2);
    gboolean is_bp = (g_filter_type == 3);

    if (entry_freq1) gtk_widget_set_sensitive(entry_freq1, is_hp_lp || is_bp);
    if (entry_freq2) gtk_widget_set_sensitive(entry_freq2, is_bp);
    if (combo_order) gtk_widget_set_sensitive(combo_order, is_hp_lp || is_bp);
    if (btn_apply_filter) gtk_widget_set_sensitive(btn_apply_filter, g_filter_type != 0);

    ApplySelectedFilter();
    if (canvas_global) gtk_widget_queue_draw(canvas_global);
}

void on_btn_apply_filter_clicked(GtkWidget *widget, gpointer data) {
    ApplySelectedFilter();
    if (canvas_global) gtk_widget_queue_draw(canvas_global);
}

gboolean on_draw_signal(GtkWidget *widget, cairo_t *cr, gpointer data) {
    int width = gtk_widget_get_allocated_width(widget);
    int height = gtk_widget_get_allocated_height(widget);
    cairo_set_source_rgb(cr, 1, 1, 1); cairo_paint(cr);

    int margin_right = 10;
    int draw_width = width - g_margin_left - margin_right;
    if (draw_width <= 0) return FALSE;

    /* Titulo */
    cairo_set_source_rgb(cr, 0.5, 0.1, 0.1);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 14); cairo_move_to(cr, width / 2 - 120, 20);
    char buf[128];
    if (edit_mode) snprintf(buf, sizeof(buf), "Repick Mode (Up/Down to Zoom) - Quake ID: %s", selected_id);
    else snprintf(buf, sizeof(buf), "Historical Data - Quake ID: %s", selected_id);
    cairo_show_text(cr, buf);

    /* Ordena las estaciones por distancia */
    g_NumSortedNodes = 0;
    for (int i = 0; i < NumEstaciones; i++) {
        if (bHasData[i] == 1 && (g_StaDist[i] * 111.19) <= g_max_dist_km) {
            g_SortedNodes[g_NumSortedNodes].idx = i;
            g_SortedNodes[g_NumSortedNodes].dist = g_StaDist[i];
            g_NumSortedNodes++;
        }
    }
    for (int i = 0; i < g_NumSortedNodes - 1; i++) {
        for (int j = 0; j < g_NumSortedNodes - i - 1; j++) {
            if (g_SortedNodes[j].dist > g_SortedNodes[j+1].dist) {
                StaNode temp = g_SortedNodes[j]; g_SortedNodes[j] = g_SortedNodes[j+1]; g_SortedNodes[j+1] = temp;
            }
        }
    }

    /* Marca P mas temprana -> ventana global de referencia */
    double earliest_pick = 1e15;
    for (int n = 0; n < g_NumSortedNodes; n++) {
        int i = g_SortedNodes[n].idx;
        double pt = station_pick_time(i, NULL);
        if (pt > 1.0 && pt < earliest_pick) earliest_pick = pt;
    }
    if (earliest_pick == 1e15) earliest_pick = selected_otime + 15.0;
    g_dWindowStart = earliest_pick - g_align_lead;

    /* Linea de referencia vertical discontinua (todas las P alineadas) */
    int x_ref = g_margin_left + (int)((g_align_lead / g_dScreenTime) * draw_width);
    cairo_set_source_rgb(cr, 0.55, 0.55, 0.75);
    cairo_set_line_width(cr, 1.0);
    const double dash2[] = { 4.0, 3.0 };
    cairo_set_dash(cr, dash2, 2, 0.0);
    cairo_move_to(cr, x_ref, 5);
    cairo_line_to(cr, x_ref, height - 5);
    cairo_stroke(cr);
    cairo_set_dash(cr, NULL, 0, 0.0);

    for (int n = 0; n < g_NumSortedNodes; n++) {
        int i = g_SortedNodes[n].idx;
        int y_center = (n * g_spacing) + (g_spacing / 2) + 30;
        g_SortedNodes[n].y_top = y_center - (g_spacing / 2);
        g_SortedNodes[n].y_bottom = y_center + (g_spacing / 2);

        int is_real = 0;
        double pick_t = station_pick_time(i, &is_real);
        double winStart = pick_t - g_align_lead;

        /* LED de estado: verde = pick real, rojo = referencia origen */
        cairo_new_path(cr); cairo_set_line_width(cr, 1.0);
        if (is_real) cairo_set_source_rgb(cr, 0.2, 0.8, 0.2); else cairo_set_source_rgb(cr, 0.8, 0.2, 0.2);
        cairo_arc(cr, 15, y_center + 1, 5, 0, 2 * M_PI); cairo_fill_preserve(cr);
        cairo_set_source_rgb(cr, 0.0, 0.0, 0.0); cairo_stroke(cr);

        /* Etiqueta de estacion + distancia */
        cairo_set_source_rgb(cr, 0.1, 0.2, 0.7);
        char sta_label[64];
        snprintf(sta_label, sizeof(sta_label), "%s (%.0f km)", StaArray[i].szStation, g_SortedNodes[n].dist * 111.19);
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, 12); cairo_move_to(cr, 28, y_center + 5); cairo_show_text(cr, sta_label);

        /* Linea media */
        cairo_set_source_rgb(cr, 0.8, 0.8, 0.8); cairo_set_line_width(cr, 1);
        cairo_move_to(cr, g_margin_left, y_center); cairo_line_to(cr, width - margin_right, y_center); cairo_stroke(cr);

        if (StaArray[i].dSampRate > 0 && StaArray[i].lRawCircCtr > 0) {
            long start_k = (long)((winStart - StaArray[i].dOldestTime) * StaArray[i].dSampRate);
            long end_k = (long)(((winStart + g_dScreenTime) - StaArray[i].dOldestTime) * StaArray[i].dSampRate);
            if (start_k < 0) start_k = 0;
            if (end_k > StaArray[i].lRawCircSize) end_k = StaArray[i].lRawCircSize;
            if (end_k > StaArray[i].lRawCircCtr) end_k = StaArray[i].lRawCircCtr;

            long max_abs = 0;
            for (long k = start_k; k < end_k; k++) {
                if (StaArray[i].plFiltCircBuff[k] == INT_MAX) continue;
                long abs_val = labs(StaArray[i].plFiltCircBuff[k]);
                if (abs_val > max_abs) max_abs = abs_val;
            }

            if (max_abs > 0) {
                if (!is_real) cairo_set_source_rgb(cr, 0.7, 0.7, 0.7); else cairo_set_source_rgb(cr, 0.1, 0.1, 0.9);
                cairo_set_line_width(cr, 0.8);
                double scale = (g_spacing * 0.45) / (double)max_abs * g_zoom_factor;
                cairo_save(cr);
                cairo_rectangle(cr, g_margin_left, y_center - (g_spacing / 2.0), draw_width, g_spacing);
                cairo_clip(cr);

                /* Pixel binning sobre la ventana alineada de la estacion.
                   Los gaps (INT_MAX) se omiten: columna sin datos -> no se
                   dibuja linea horizontal falsa a traves del hueco. */
                for (int px = 0; px < draw_width; px++) {
                    double px_t_start = winStart + ((double)px / draw_width) * g_dScreenTime;
                    double px_t_end   = winStart + ((double)(px + 1) / draw_width) * g_dScreenTime;
                    long p_start_k = (long)((px_t_start - StaArray[i].dOldestTime) * StaArray[i].dSampRate);
                    long p_end_k   = (long)((px_t_end   - StaArray[i].dOldestTime) * StaArray[i].dSampRate);
                    if (p_end_k == p_start_k) p_end_k++;
                    if (p_start_k < 0) p_start_k = 0;
                    if (p_end_k > StaArray[i].lRawCircCtr) p_end_k = StaArray[i].lRawCircCtr;

                    double p_min = 1e12, p_max = -1e12;
                    gboolean px_has_data = FALSE;
                    for (long k = p_start_k; k < p_end_k; k++) {
                        int32_t val = StaArray[i].plFiltCircBuff[k];
                        if (val != INT_MAX) {
                            double scaled_val = val * scale;
                            if (scaled_val < p_min) p_min = scaled_val;
                            if (scaled_val > p_max) p_max = scaled_val;
                            px_has_data = TRUE;
                        }
                    }
                    if (px_has_data) {
                        double x = g_margin_left + px;
                        cairo_move_to(cr, x, y_center - p_min);
                        cairo_line_to(cr, x, y_center - p_max);
                    }
                }
                cairo_stroke(cr); cairo_restore(cr);
            }
        }

        /* Marca P sobre la linea de referencia (todas alineadas) */
        if (pick_t > 1.0 && pick_t >= winStart && pick_t <= (winStart + g_dScreenTime)) {
            if (is_real) cairo_set_source_rgb(cr, 1.0, 0.0, 0.0); else cairo_set_source_rgb(cr, 1.0, 0.5, 0.0);
            cairo_set_line_width(cr, 2);
            cairo_move_to(cr, x_ref, y_center - (g_spacing / 2.5));
            cairo_line_to(cr, x_ref, y_center + (g_spacing / 2.5));
            cairo_stroke(cr);
            cairo_set_font_size(cr, 11);
            cairo_move_to(cr, x_ref + 4, y_center - (g_spacing / 3));
            if (is_real && StaArray[i].iNumPicks > 0 && strlen(StaArray[i].picks[0].szPhase) > 0)
                cairo_show_text(cr, StaArray[i].picks[0].szPhase);
            else
                cairo_show_text(cr, is_real ? "P" : "P(theo)");
        }

        /* Pick manual (si existe) mapeado en la ventana alineada */
        if (StaArray[i].dManualPickTime > 0.0) {
            double rel = (StaArray[i].dManualPickTime - winStart) / g_dScreenTime;
            if (rel >= 0.0 && rel <= 1.0) {
                double x_pos = g_margin_left + rel * draw_width;
                cairo_set_source_rgb(cr, 0.0, 0.5, 1.0);
                cairo_set_line_width(cr, 1.5);
                cairo_move_to(cr, x_pos, y_center - (g_spacing / 2.5));
                cairo_line_to(cr, x_pos, y_center + (g_spacing / 2.5));
                cairo_stroke(cr);
                cairo_move_to(cr, x_pos + 4, y_center + (g_spacing / 3));
                cairo_show_text(cr, "P(m)");
            }
        }
    }

    return FALSE;
}
