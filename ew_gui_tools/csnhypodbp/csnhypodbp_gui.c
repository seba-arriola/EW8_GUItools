#include "csnhypodbp.h"
#include <adwaita.h>

/* Forward declarations de los helpers estaticos de alineacion por P */
static double station_pick_time(int i, int *is_real);
static double station_win_start(int i);

/* F3: buffers reutilizables para el binning por columna (evita una segunda
   pasada sobre las muestras solo para el auto-escalado). */
#define MAX_DRAW_W 4096
static double   g_col_min[MAX_DRAW_W];
static double   g_col_max[MAX_DRAW_W];
static gboolean g_col_has[MAX_DRAW_W];

/* F3: orden por distancia con qsort (antes burbuja O(n^2) por frame). */
static int cmp_node_dist(const void *a, const void *b) {
    double da = ((const StaNode *)a)->dist;
    double db = ((const StaNode *)b)->dist;
    return (da > db) - (da < db);
}

void actualizar_altura_canvas() {
    if (!canvas_global) return;
    int count = 0;
    for (int i = 0; i < NumEstaciones; i++) {
        if (bHasData[i] == 1 && (g_StaDist[i] * 111.19) <= g_max_dist_km) count++;
    }
    if (count > 0) gtk_widget_set_size_request(ewgui_canvas_widget(canvas_global), -1, (count * g_spacing) + 60);
    else gtk_widget_set_size_request(ewgui_canvas_widget(canvas_global), -1, 600);
}

void on_btn_fetch_clicked(GtkWidget *widget, gpointer data) {
    if (!entry_dist || !entry_time) return;
    double new_dist = atof(gtk_editable_get_text(GTK_EDITABLE(entry_dist)));
    double new_time_min = atof(gtk_editable_get_text(GTK_EDITABLE(entry_time)));
    gboolean changed = FALSE;
    if (new_dist > 0.0) { g_max_dist_km = new_dist; changed = TRUE; }
    if (new_time_min > 0.0) {
        if (new_time_min > 10.0) { new_time_min = 10.0; gtk_editable_set_text(GTK_EDITABLE(entry_time), "10"); }
        g_dScreenTime = new_time_min * 60.0; 
        changed = TRUE;
    }
    if (changed) {
        actualizar_altura_canvas();
        if (canvas_global) ewgui_canvas_queue_draw(canvas_global);
        /* Re-fetch: al variar la ventana/distancia se descarga mas datos
           (la ventana de fetch escala con g_dScreenTime). */
        pending_waveform_reload = TRUE;
        logit("t", ">> Fetch UI: Mostrando estaciones hasta %.1f km, ventana de %.1f min\n", g_max_dist_km, g_dScreenTime/60.0);
    }
}

/* color_rows_func eliminado: en GTK4 el resaltado de fila lo da el tema. */

gboolean on_key_press(GtkEventControllerKey *ctrl, guint keyval, guint keycode, GdkModifierType state, gpointer data) {
    (void)ctrl; (void)keycode; (void)state; (void)data;
    if (!edit_mode) return FALSE;
    if (keyval == GDK_KEY_Up) { g_zoom_factor *= 1.5; if (canvas_global) ewgui_canvas_queue_draw(canvas_global); return TRUE; }
    else if (keyval == GDK_KEY_Down) { g_zoom_factor /= 1.5; if (canvas_global) ewgui_canvas_queue_draw(canvas_global); return TRUE; }
    return FALSE;
}

void on_row_selected(GtkSingleSelection *sel, GParamSpec *pspec, gpointer data) {
    (void)sel; (void)pspec; (void)data;
    if (edit_mode) return;
    /* Con GtkSortListModel la posicion de la seleccion es la de la vista
     * ordenada (no la del store), asi que se obtiene el item directamente.
     * get_selected_item es transfer-none: NO se libera. */
    gpointer item = gtk_single_selection_get_selected_item(g_selection_hypo);
    if (item) {
        CsnhypodbpRow *row = CSNHYPODBP_ROW(item);
        double otime = 0, lat = 0, lon = 0; int qid = 0, mod = 0;
        const char *id_str = csnhypodbp_row_col(row, 8);
        g_object_get(row, "otime", &otime, "qid", &qid, "lat", &lat, "lon", &lon, "mod", &mod, NULL);
        if (id_str) strcpy(selected_id, id_str);
        selected_otime = otime; selected_qid = qid; selected_lat = lat; selected_lon = lon; selected_mod = mod;
        g_zoom_factor = 1.0;
        gtk_widget_set_sensitive(btn_repick, TRUE);
    } else {
        gtk_widget_set_sensitive(btn_repick, FALSE);
    }
}

void on_list_double_click(GtkGestureClick *g, int n_press, double x, double y, gpointer data) {
    (void)g; (void)x; (void)y; (void)data;
    if (n_press != 2) return;
    guint idx = gtk_single_selection_get_selected(g_selection_hypo);
    if (idx == GTK_INVALID_LIST_POSITION) return;
    if (g_notebook) gtk_notebook_set_current_page(GTK_NOTEBOOK(g_notebook), 1);  /* pestaña Ondas */
    pending_waveform_reload = TRUE;
    waveform_reload_timer(NULL);   /* fetch inmediato */
}

gboolean waveform_reload_timer(gpointer data) {
    if (pending_waveform_reload && !edit_mode && selected_qid != 0) {
        pending_waveform_reload = FALSE;
        if (entry_dist) g_max_dist_km = atof(gtk_editable_get_text(GTK_EDITABLE(entry_dist)));
        if (entry_time) g_dScreenTime = atof(gtk_editable_get_text(GTK_EDITABLE(entry_time))) * 60.0;
        if (window_global) gtk_window_set_title(GTK_WINDOW(window_global), "CSNhypodbp - Descargando ondas historicas...");
        FetchWaveformsForEvent(selected_otime, selected_lat, selected_lon, selected_qid, selected_mod);
        /* La descarga corre en un worker (no bloquea la UI); el titulo y el
           redibujado se restauran en ws_fetch_finish() al publicar el resultado. */
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
        if (box_fetch) gtk_widget_set_visible(box_fetch, TRUE);
        ewgui_canvas_queue_draw(canvas_global);
    } else {
        edit_mode = FALSE; g_zoom_factor = 1.0; 
        gtk_widget_set_sensitive(tree_global, TRUE); 
        gtk_button_set_label(GTK_BUTTON(btn_repick), "Repick mode");
        gtk_widget_set_name(btn_repick, "btn_repick");
        if (box_fetch) gtk_widget_set_visible(box_fetch, FALSE);
        on_row_selected(g_selection_hypo, NULL, NULL);
    }
}

void on_canvas_clicked(GtkGestureClick *gesture, int n_press, double x, double y, gpointer data) {
    (void)gesture; (void)n_press; (void)data;
    if (!edit_mode) return;
    int width = 0;
    ewgui_canvas_get_size(canvas_global, &width, NULL);
    int margin_right = 10;
    int draw_width = width - g_margin_left - margin_right;
    if (x <= g_margin_left || x >= width - margin_right) return;
    int target = -1;
    for (int n = 0; n < g_NumSortedNodes; n++) {
        if (y >= g_SortedNodes[n].y_top && y <= g_SortedNodes[n].y_bottom) { target = n; break; }
    }
    if (target == -1) return;
    int i = g_SortedNodes[target].idx;
    double winStart = station_win_start(i);
    double fraction = (x - g_margin_left) / (double)draw_width;
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
    snprintf(out_msg, sizeof(out_msg), "%d %d %d %d %s.%s.%s.%s ?0 %s 0 0 0 P M\n",
             TypePickSCNL, MyModId, MyInstId, pick_seq++,
             StaArray[i].szStation, StaArray[i].szChannel, StaArray[i].szNetID, StaArray[i].szLocation, time_str);
    MSG_LOGO logo = {MyInstId, MyModId, TypePickSCNL};
    if (tport_putmsg(&PRegion, &logo, strlen(out_msg), out_msg) != PUT_OK) {
        logit("e", "csnhypodbp: Error inyectando pick manual.\n");
    } else {
        logit("t", "csnhypodbp: MANUAL PICK INYECTADO -> %s", out_msg);
    }
    ewgui_canvas_queue_draw(canvas_global);
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

    if (entry_freq1) f1 = atof(gtk_editable_get_text(GTK_EDITABLE(entry_freq1)));
    if (entry_freq2) f2 = atof(gtk_editable_get_text(GTK_EDITABLE(entry_freq2)));
    if (order_radios[0] && order_radios[1]) {
        order = gtk_check_button_get_active(GTK_CHECK_BUTTON(order_radios[0])) ? 2 : 4;
    }

    EwFilterParams fp;
    fp.filter_type = g_filter_type;
    fp.f1 = f1;
    fp.f2 = f2;
    fp.order = order;

    for (int i = 0; i < NumEstaciones; i++) {
        if (ewgui_trace_length(StaArray[i].trace) <= 0) continue;
        ewgui_trace_filter(StaArray[i].trace, &fp);
    }
}

void on_filter_changed(GtkCheckButton *b, gpointer data) {
    (void)b; (void)data;
    g_filter_type = 0;
    for (int i = 0; i < 4; i++)
        if (filter_radios[i] && gtk_check_button_get_active(GTK_CHECK_BUTTON(filter_radios[i]))) {
            g_filter_type = i; break;
        }
    gboolean is_hp_lp = (g_filter_type == 1 || g_filter_type == 2);
    gboolean is_bp = (g_filter_type == 3);
    if (entry_freq1) gtk_widget_set_sensitive(entry_freq1, is_hp_lp || is_bp);
    if (entry_freq2) gtk_widget_set_sensitive(entry_freq2, is_bp);
    if (order_radios[0]) gtk_widget_set_sensitive(order_radios[0], is_hp_lp || is_bp);
    if (order_radios[1]) gtk_widget_set_sensitive(order_radios[1], is_hp_lp || is_bp);
    if (btn_apply_filter) gtk_widget_set_sensitive(btn_apply_filter, g_filter_type != 0);
    ApplySelectedFilter();
    if (canvas_global) ewgui_canvas_queue_draw(canvas_global);
}

void on_btn_apply_filter_clicked(GtkWidget *widget, gpointer data) {
    ApplySelectedFilter();
    if (canvas_global) ewgui_canvas_queue_draw(canvas_global);
}

void on_draw_signal(EwGuiCanvas *canvas, cairo_t *cr, int width, int height, void *data) {
    (void)canvas; (void)data;
    cairo_set_source_rgb(cr, 1, 1, 1); cairo_paint(cr);

    int margin_right = 10;
    int draw_width = width - g_margin_left - margin_right;
    if (draw_width <= 0) return;

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
    if (g_NumSortedNodes > 1)
        qsort(g_SortedNodes, (size_t)g_NumSortedNodes, sizeof(StaNode), cmp_node_dist);

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

        EwGuiTrace *tr = StaArray[i].trace;
        double tr_rate = ewgui_trace_rate(tr);
        double tr_oldest = ewgui_trace_oldest(tr);
        long tr_len = ewgui_trace_length(tr);
        int32_t *tr_filt = ewgui_trace_filtered(tr);

        if (tr_rate > 0 && tr_len > 0) {
            /* F3: una sola pasada sobre las muestras. Se guardan min/max por
               columna (unidades crudas) y el max_abs global; el trazado usa
               esos valores escalados (antes eran dos pasadas sobre muestras).
               Los gaps (INT_MAX) se omiten: columna sin datos -> no se dibuja
               linea horizontal falsa a traves del hueco. */
            int ncol = draw_width < MAX_DRAW_W ? draw_width : MAX_DRAW_W;
            double max_abs = 0.0;
            for (int px = 0; px < ncol; px++) {
                double px_t_start = winStart + ((double)px / ncol) * g_dScreenTime;
                double px_t_end   = winStart + ((double)(px + 1) / ncol) * g_dScreenTime;
                long p_start_k = (long)((px_t_start - tr_oldest) * tr_rate);
                long p_end_k   = (long)((px_t_end   - tr_oldest) * tr_rate);
                if (p_end_k == p_start_k) p_end_k++;
                if (p_start_k < 0) p_start_k = 0;
                if (p_end_k > tr_len) p_end_k = tr_len;

                double p_min = 1e12, p_max = -1e12;
                gboolean has = FALSE;
                for (long k = p_start_k; k < p_end_k; k++) {
                    int32_t val = tr_filt[k];
                    if (val == INT_MAX) continue;
                    double fv = (double)val;
                    if (fv < p_min) p_min = fv;
                    if (fv > p_max) p_max = fv;
                    if (fabs(fv) > max_abs) max_abs = fabs(fv);
                    has = TRUE;
                }
                g_col_min[px] = p_min;
                g_col_max[px] = p_max;
                g_col_has[px] = has;
            }

            if (max_abs > 0.0) {
                if (!is_real) cairo_set_source_rgb(cr, 0.7, 0.7, 0.7); else cairo_set_source_rgb(cr, 0.1, 0.1, 0.9);
                cairo_set_line_width(cr, 0.8);
                double scale = (g_spacing * 0.45) / max_abs * g_zoom_factor;
                cairo_save(cr);
                cairo_rectangle(cr, g_margin_left, y_center - (g_spacing / 2.0), draw_width, g_spacing);
                cairo_clip(cr);

                for (int px = 0; px < ncol; px++) {
                    if (!g_col_has[px]) continue;
                    double x = g_margin_left + px;
                    cairo_move_to(cr, x, y_center - g_col_min[px] * scale);
                    cairo_line_to(cr, x, y_center - g_col_max[px] * scale);
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
}
