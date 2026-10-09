#include "csntvp.h"
#include "csntvp_util.h"

typedef struct { GMainLoop *loop; int response; } DialogRun;

static void dialog_ok(GtkButton *b, gpointer data)     { (void)b; DialogRun *r = data; r->response = 1; g_main_loop_quit(r->loop); }
static void dialog_cancel(GtkButton *b, gpointer data) { (void)b; DialogRun *r = data; r->response = 0; g_main_loop_quit(r->loop); }

/* Diálogo modal síncrono (equivalente a gtk_dialog_run, que no existe en GTK4).
 * Devuelve 1 si se aceptó. `*out_win` queda para destruirlo tras leer valores. */
static int run_dialog(GtkWindow *parent, const char *title, GtkWidget *content, GtkWindow **out_win) {
    GtkWidget *win = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(win), title);
    gtk_window_set_transient_for(GTK_WINDOW(win), parent);
    gtk_window_set_modal(GTK_WINDOW(win), TRUE);
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(vbox, 15); gtk_widget_set_margin_end(vbox, 15);
    gtk_widget_set_margin_top(vbox, 15); gtk_widget_set_margin_bottom(vbox, 15);
    gtk_box_append(GTK_BOX(vbox), content);
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_halign(hbox, GTK_ALIGN_END);
    GtkWidget *cancel = gtk_button_new_with_label("Cancel");
    GtkWidget *ok = gtk_button_new_with_label("OK");
    gtk_box_append(GTK_BOX(hbox), cancel);
    gtk_box_append(GTK_BOX(hbox), ok);
    gtk_box_append(GTK_BOX(vbox), hbox);
    gtk_window_set_child(GTK_WINDOW(win), vbox);

    DialogRun r; r.loop = g_main_loop_new(NULL, FALSE); r.response = 0;
    g_signal_connect(cancel, "clicked", G_CALLBACK(dialog_cancel), &r);
    g_signal_connect(ok, "clicked", G_CALLBACK(dialog_ok), &r);
    gtk_window_present(GTK_WINDOW(win));
    g_main_loop_run(r.loop);
    g_main_loop_unref(r.loop);
    if (out_win) *out_win = GTK_WINDOW(win);
    return r.response;
}

void on_btn_hold_toggled(GtkToggleButton *togglebutton, gpointer user_data) {
    g_is_hold = gtk_toggle_button_get_active(togglebutton);
    g_bForceEnv = TRUE; /* The envelope is recalculated for the (in)active window */
    if (g_is_hold) {
        if (g_smooth_time > 0.0) g_t_hold_time = g_smooth_time;
        else { time_t t; time(&t); g_t_hold_time = (double)t; }
    } else {
        g_zoom_factor = 1.0;
    }
    if (g_drawing_waves) ewgui_canvas_queue_draw(g_drawing_waves);
    if (drawing_axis) ewgui_canvas_queue_draw(drawing_axis);
}

gboolean on_key_press(GtkEventControllerKey *ctrl, guint keyval, guint keycode, GdkModifierType state, gpointer data) {
    (void)ctrl; (void)keycode; (void)state; (void)data;
    if (!g_is_hold) return FALSE;
    if (keyval == GDK_KEY_Up) {
        g_zoom_factor *= 1.5;
        if (g_zoom_factor > MAX_ZOOM) g_zoom_factor = MAX_ZOOM;
        if (g_drawing_waves) ewgui_canvas_queue_draw(g_drawing_waves);
        return TRUE;
    } else if (keyval == GDK_KEY_Down) {
        g_zoom_factor /= 1.5;
        if (g_zoom_factor < MIN_ZOOM) g_zoom_factor = MIN_ZOOM;
        if (g_drawing_waves) ewgui_canvas_queue_draw(g_drawing_waves);
        return TRUE;
    } else if (keyval == GDK_KEY_p || keyval == GDK_KEY_P) {
        g_pick_phase = 'P';
        return TRUE;
    } else if (keyval == GDK_KEY_s || keyval == GDK_KEY_S) {
        g_pick_phase = 'S';
        return TRUE;
    }
    return FALSE;
}

static void on_color_chosen(GObject *src, GAsyncResult *res, gpointer data) {
    int type = GPOINTER_TO_INT(data);
    GdkRGBA *c = gtk_color_dialog_choose_rgba_finish(GTK_COLOR_DIALOG(src), res, NULL);
    if (c) {
        if (type == 1) { g_color_wave[0]=c->red; g_color_wave[1]=c->green; g_color_wave[2]=c->blue; }
        else if (type == 2) { g_color_bg[0]=c->red; g_color_bg[1]=c->green; g_color_bg[2]=c->blue; }
        else if (type == 3) { g_color_font[0]=c->red; g_color_font[1]=c->green; g_color_font[2]=c->blue; }
        else if (type == 4) { g_color_sep[0]=c->red; g_color_sep[1]=c->green; g_color_sep[2]=c->blue; }
        else if (type == 5) { g_color_p[0]=c->red; g_color_p[1]=c->green; g_color_p[2]=c->blue; }
        else if (type == 6) { g_color_s[0]=c->red; g_color_s[1]=c->green; g_color_s[2]=c->blue; }
        LogColorConfig();
        if (g_drawing_waves) ewgui_canvas_queue_draw(g_drawing_waves);
        gdk_rgba_free(c);
    }
    g_object_unref(src);
}

void on_colour_select(GtkWidget *widget, gpointer data) {
    (void)widget;
    int type = GPOINTER_TO_INT(data);
    GdkRGBA current_color = {0};
    current_color.alpha = 1.0;
    if (type == 1) { current_color.red=g_color_wave[0]; current_color.green=g_color_wave[1]; current_color.blue=g_color_wave[2]; }
    else if (type == 2) { current_color.red=g_color_bg[0]; current_color.green=g_color_bg[1]; current_color.blue=g_color_bg[2]; }
    else if (type == 3) { current_color.red=g_color_font[0]; current_color.green=g_color_font[1]; current_color.blue=g_color_font[2]; }
    else if (type == 4) { current_color.red=g_color_sep[0]; current_color.green=g_color_sep[1]; current_color.blue=g_color_sep[2]; }
    else if (type == 5) { current_color.red=g_color_p[0]; current_color.green=g_color_p[1]; current_color.blue=g_color_p[2]; }
    else if (type == 6) { current_color.red=g_color_s[0]; current_color.green=g_color_s[1]; current_color.blue=g_color_s[2]; }
    GtkColorDialog *cd = gtk_color_dialog_new();
    gtk_color_dialog_set_title(cd, "Select Colour");
    gtk_color_dialog_choose_rgba(cd, NULL, &current_color, NULL, on_color_chosen, GINT_TO_POINTER(type));
}

void RecalcTrackHeight() {
    if (!g_scrolled_window) return;
    int h = gtk_widget_get_height(g_scrolled_window);
    if (h > 0 && iVisStas > 0) {
        dTrackHeight = (double)h / iVisStas;
        if (dTrackHeight < 2.0) dTrackHeight = 2.0;
        gtk_widget_set_size_request(ewgui_canvas_widget(g_drawing_waves), -1, iNumStas * dTrackHeight);
    }
    if (g_drawing_waves) ewgui_canvas_queue_draw(g_drawing_waves);
}

void on_stas_per_screen_activate(GtkWidget *widget, gpointer data) {
    (void)widget;
    GtkWidget *window = GTK_WIDGET(data);
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_append(GTK_BOX(hbox), gtk_label_new("Visible stations:"));
    GtkWidget *spin = gtk_spin_button_new_with_range(1, MAX_STATIONS, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), iVisStas);
    gtk_box_append(GTK_BOX(hbox), spin);
    GtkWindow *dlg = NULL;
    if (run_dialog(GTK_WINDOW(window), "Stations per screen", hbox, &dlg)) {
        iVisStas = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin));
        RecalcTrackHeight();
    }
    if (dlg) gtk_window_destroy(dlg);
}

void on_time_window_activate(GtkWidget *widget, gpointer data) {
    (void)widget;
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_append(GTK_BOX(hbox), gtk_label_new("Window size (minutes):"));
    GtkWidget *spin = gtk_spin_button_new_with_range(1, MAX_MINUTES, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), iTimeWindowMinutes);
    gtk_box_append(GTK_BOX(hbox), spin);
    GtkWindow *dlg = NULL;
    if (run_dialog(GTK_WINDOW(data), "Time window", hbox, &dlg)) {
        iTimeWindowMinutes = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin));
        g_bForceEnv = TRUE;
        if (g_drawing_waves) ewgui_canvas_queue_draw(g_drawing_waves);
        if (drawing_axis) ewgui_canvas_queue_draw(drawing_axis);
    }
    if (dlg) gtk_window_destroy(dlg);
}

static void on_filter_combo_changed(GObject *obj, GParamSpec *pspec, gpointer data) {
    (void)pspec;
    GtkWidget **entries = (GtkWidget **)data;
    int type = (int)gtk_drop_down_get_selected(GTK_DROP_DOWN(obj));
    gtk_widget_set_sensitive(entries[0], (type != 0));
    gtk_widget_set_sensitive(entries[1], (type == 3));
    gtk_widget_set_sensitive(entries[2], (type != 0));
}

void on_filter_menu_activate(GtkWidget *widget, gpointer data) {
    (void)widget;
    GtkWidget *window = GTK_WIDGET(data);
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 5);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);

    const char *type_items[] = { "Raw (No Filter)", "High-Pass", "Low-Pass", "Band-Pass", NULL };
    GtkWidget *cb_type = gtk_drop_down_new_from_strings(type_items);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(cb_type), g_filter_type);

    char buf[16];
    GtkWidget *e_f1 = gtk_entry_new();
    snprintf(buf, sizeof(buf), "%.2f", g_f1); gtk_editable_set_text(GTK_EDITABLE(e_f1), buf);
    GtkWidget *e_f2 = gtk_entry_new();
    snprintf(buf, sizeof(buf), "%.2f", g_f2); gtk_editable_set_text(GTK_EDITABLE(e_f2), buf);

    const char *order_items[] = { "2", "4", NULL };
    GtkWidget *cb_order = gtk_drop_down_new_from_strings(order_items);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(cb_order), (g_order==2)?0:1);

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Type:"), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), cb_type, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("F1 (Hz):"), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), e_f1, 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("F2 (Hz):"), 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), e_f2, 1, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Order:"), 0, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), cb_order, 1, 3, 1, 1);

    GtkWidget *entries[3] = { e_f1, e_f2, cb_order };
    g_signal_connect(cb_type, "notify::selected", G_CALLBACK(on_filter_combo_changed), entries);
    on_filter_combo_changed(G_OBJECT(cb_type), NULL, entries);

    GtkWindow *dlg = NULL;
    if (run_dialog(GTK_WINDOW(window), "Filter Settings", grid, &dlg)) {
        g_filter_type = (int)gtk_drop_down_get_selected(GTK_DROP_DOWN(cb_type));
        g_f1 = atof(gtk_editable_get_text(GTK_EDITABLE(e_f1)));
        g_f2 = atof(gtk_editable_get_text(GTK_EDITABLE(e_f2)));
        g_order = (gtk_drop_down_get_selected(GTK_DROP_DOWN(cb_order)) == 0) ? 2 : 4;
        g_bForceEnv = TRUE;
        if (g_drawing_waves) ewgui_canvas_queue_draw(g_drawing_waves);
    }
    if (dlg) gtk_window_destroy(dlg);
}

void on_clean_view_activate(GtkWidget *widget, gpointer data) {
    (void)widget;
    GtkWidget *window = GTK_WIDGET(data);
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *spin = gtk_spin_button_new_with_range(1, 10080, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), 60);
    gtk_box_append(GTK_BOX(hbox), gtk_label_new("Max time without data (min):"));
    gtk_box_append(GTK_BOX(hbox), spin);
    GtkWindow *dlg = NULL;
    if (run_dialog(GTK_WINDOW(window), "Clean View (Remove Inactive)", hbox, &dlg)) {
        int min_val = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin));
        double max_age_secs = min_val * 60.0;
        double sys_time = (double)g_get_real_time() / 1000000.0;
        int active_count = 0;
        for (int i = 0; i < iNumStas; i++) {
            if (StaArray[i].dLastPacketSysTime > 0.0 && (sys_time - StaArray[i].dLastPacketSysTime) <= max_age_secs) {
                if (i != active_count) {
                    StaArray[active_count] = StaArray[i];
                    StaArray[i].plRawCircBuff = NULL;
                    memset(&StaArray[i].cache, 0, sizeof(StaArray[i].cache));
                }
                active_count++;
            } else {
                if (StaArray[i].plRawCircBuff) { free(StaArray[i].plRawCircBuff); StaArray[i].plRawCircBuff = NULL; }
                ewgui_trace_cache_free(&StaArray[i].cache);
            }
        }
        iNumStas = active_count;
        RecalcTrackHeight();
    }
    if (dlg) gtk_window_destroy(dlg);
}

void on_reload_default_activate(GtkWidget *widget, gpointer data) {
    FreeAllStations(); LoadStationsFromFile();
    RecalcTrackHeight();
}

void on_canvas_button_press(GtkGestureClick *gesture, int n_press, double x, double y, gpointer data) {
    (void)gesture; (void)n_press; (void)data;
    if (!g_is_hold || x <= PANEL_WIDTH) return;
    int width = 0; ewgui_canvas_get_size(g_drawing_waves, &width, NULL);
    double draw_area_width = width - PANEL_WIDTH;
    int sta_idx = (int)(y / dTrackHeight);
    if (sta_idx < 0 || sta_idx >= iNumStas) return;
    double window_secs = iTimeWindowMinutes * 60.0;
    double t_right = g_t_hold_time, t_left = t_right - window_secs;
    double fraction = (x - PANEL_WIDTH) / draw_area_width;
    double clicked_time = t_left + fraction * window_secs;

    DEV_STATION *dev = &StaArray[sta_idx];

    /* Window-based replacement (PickReplaceWindow): if a pick already exists
       for the same station within +/-g_pick_replace_secs, it is marked as
       replaced locally. NOTE: the TYPE_PICK_SCNL protocol has no retract
       message, so the old pick is only deactivated in the local overlay; the
       associator (ew2glass/GLASS3) will see the corrected pick as a new one. */
    if (g_pick_replace_secs > 0.0) {
        for (int pi = 0; pi < MAX_PICKS_PER_STA; pi++) {
            if (dev->picks[pi].iUseMe > 0 &&
                fabs(dev->picks[pi].dTime - clicked_time) <= g_pick_replace_secs) {
                logit("et", "csntvp: replacing pick %ld of %s (%.3f -> %.3f)\n",
                      dev->picks[pi].lPickIndex, dev->szStation, dev->picks[pi].dTime, clicked_time);
                dev->picks[pi].iUseMe = 0;
            }
        }
    }

    int seq = g_pick_seq;
    g_pick_seq = (g_pick_seq + 1) % 1000000; /* spec: 0-999999 (bugfix: was unsigned char) */

    /* Show the manual pick immediately at the station, without waiting for it
       to circulate back through PICK_RING. */
    {
        char dph[8];
        csntvp_pick_label(g_pick_phase, 'M', dph, sizeof(dph));
        int slot = (int)(dev->lPickRingNext % MAX_PICKS_PER_STA);
        dev->picks[slot].dTime = clicked_time;
        snprintf(dev->picks[slot].szPhase, sizeof(dev->picks[slot].szPhase), "%s", dph);
        dev->picks[slot].lPickIndex = seq;
        dev->picks[slot].cPhase = g_pick_phase;
        dev->picks[slot].cOrigin = 'M';
        dev->picks[slot].iUseMe = 1;
        dev->lPickRingNext++;
        if (dev->iNumPicks < MAX_PICKS_PER_STA) dev->iNumPicks++;
    }

    char out_msg[256], time_str[64];
    time_t t_sec = (time_t)clicked_time;
    double t_msec = clicked_time - (double)t_sec;
    struct tm *ptm = gmtime(&t_sec);
    snprintf(time_str, sizeof(time_str), "%04d%02d%02d%02d%02d%06.3f", ptm->tm_year+1900, ptm->tm_mon+1, ptm->tm_mday, ptm->tm_hour, ptm->tm_min, (double)ptm->tm_sec + t_msec);
    for (int k = 0; time_str[k] != '\0'; k++) if (time_str[k] == ',') time_str[k] = '.';
    snprintf(out_msg, sizeof(out_msg), "%d %d %d %d %s.%s.%s.%s ?0 %s 0 0 0 %c M\n", TypePickSCNL, MyModId, MyInstId, seq, StaArray[sta_idx].szStation, StaArray[sta_idx].szChannel, StaArray[sta_idx].szNetID, StaArray[sta_idx].szLocation, time_str, g_pick_phase);
    MSG_LOGO logo = {MyInstId, MyModId, TypePickSCNL};
    if (tport_putmsg(&PickRegion, &logo, strlen(out_msg), out_msg) != PUT_OK) logit("e", "csntvp: Error inyectando pick.\n");
    else logit("t", "csntvp: INYECTADO: %s", out_msg);
    ewgui_canvas_queue_draw(g_drawing_waves);
}

void on_draw_waves(EwGuiCanvas *canvas, cairo_t *cr, int width, int height, void *user_data) {
    (void)canvas; (void)user_data;
    if (iNumStas == 0 || g_latest_time <= 0.0) return;

    cairo_set_source_rgb(cr, g_color_bg[0], g_color_bg[1], g_color_bg[2]); cairo_paint(cr);
    cairo_set_source_rgb(cr, 0.96, 0.96, 0.96); cairo_rectangle(cr, 0, 0, PANEL_WIDTH, height); cairo_fill(cr);

    double draw_area_width = width - PANEL_WIDTH; if (draw_area_width <= 0) return;

    double font_size = dTrackHeight * 0.7;
    if (font_size > 12.0) font_size = 12.0;
    if (font_size < 4.0) font_size = 4.0;

    double window_secs = iTimeWindowMinutes * 60.0;
    double t_right = g_is_hold ? g_t_hold_time : g_smooth_time, t_left = t_right - window_secs;

    for (int i = 0; i < iNumStas; i++) {
        double y_top = i * dTrackHeight, y_center = y_top + (dTrackHeight / 2.0);

        /* --- ETIQUETA DE ESTACION Y CANAL --- */
        cairo_set_source_rgb(cr, g_color_font[0], g_color_font[1], g_color_font[2]);
        cairo_select_font_face(cr, "Monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, font_size);
        cairo_move_to(cr, 6, y_center + (font_size * 0.35));
        cairo_show_text(cr, StaArray[i].szStation);

        cairo_text_extents_t extents;
        cairo_text_extents(cr, StaArray[i].szStation, &extents);
        double next_x = 6 + extents.x_advance + 6;

        cairo_select_font_face(cr, "Monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_move_to(cr, next_x, y_center + (font_size * 0.35));
        char comp_net[32];
        snprintf(comp_net, sizeof(comp_net), "%s %s", StaArray[i].szChannel, StaArray[i].szNetID);
        cairo_show_text(cr, comp_net);

        /* --- SEPARADOR --- */
        cairo_set_source_rgb(cr, g_color_sep[0], g_color_sep[1], g_color_sep[2]);
        cairo_move_to(cr, PANEL_WIDTH, y_top);
        cairo_line_to(cr, width, y_top);
        cairo_stroke(cr);

        /* --- ENVELOPE CACHEADO --- */
        DEV_STATION *sta = &StaArray[i];
        if (sta->cache.env_valid && sta->cache.env_cap >= (int)draw_area_width && sta->cache.env_found_first) {
            double max_abs = sta->cache.env_max_abs;
            if (max_abs < 1.0) max_abs = 1.0;
            double auto_scale = (dTrackHeight * 0.425) / max_abs;
            auto_scale *= g_zoom_factor;

            cairo_save(cr);
            cairo_rectangle(cr, PANEL_WIDTH, y_top, draw_area_width, dTrackHeight);
            cairo_clip(cr);
            cairo_set_source_rgb(cr, g_color_wave[0], g_color_wave[1], g_color_wave[2]);
            cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);

            int ncol = (int)draw_area_width;
            for (int px = 0; px < ncol; px++) {
                if (!sta->cache.env_has[px]) continue;
                double s_max = sta->cache.env_max[px] * auto_scale;
                double s_min = sta->cache.env_min[px] * auto_scale;
                if (s_max - s_min < 2.0) {
                    double avg = (s_max + s_min) / 2.0;
                    s_max = avg + 1.0;
                    s_min = avg - 1.0;
                }
                double x = PANEL_WIDTH + px;
                cairo_rectangle(cr, x, y_center - s_max, 1.0, s_max - s_min);
            }
            cairo_fill(cr);
            cairo_restore(cr);
        }

        /* --- PICKS --- */
        cairo_set_line_width(cr, 2.0);
        cairo_select_font_face(cr, "Monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);

        int n_draw = (StaArray[i].iNumPicks < MAX_PICKS_PER_STA) ? StaArray[i].iNumPicks : MAX_PICKS_PER_STA;
        int start_slot = (int)((StaArray[i].lPickRingNext - n_draw + MAX_PICKS_PER_STA) % MAX_PICKS_PER_STA);
        for (int pi = 0; pi < n_draw; pi++) {
            int p = (start_slot + pi) % MAX_PICKS_PER_STA;
            if (StaArray[i].picks[p].iUseMe <= 0 || StaArray[i].picks[p].dTime <= 0.0) continue;
            double pTime = StaArray[i].picks[p].dTime;
            if (pTime >= t_left && pTime <= t_right + 15.0) {
                double x_pos = PANEL_WIDTH + ((pTime - t_left) / window_secs * draw_area_width);
                if (x_pos > PANEL_WIDTH && x_pos < width) {
                    const double *pc = (StaArray[i].picks[p].cPhase == 'S') ? g_color_s : g_color_p;
                    cairo_set_source_rgb(cr, pc[0], pc[1], pc[2]);
                    cairo_move_to(cr, x_pos, y_top); cairo_line_to(cr, x_pos, y_top + dTrackHeight); cairo_stroke(cr);
                    cairo_move_to(cr, x_pos + 4, y_top + font_size + 2); cairo_show_text(cr, StaArray[i].picks[p].szPhase);
                }
            }
        }
    }
    cairo_set_source_rgb(cr, 0.7, 0.7, 0.7); cairo_move_to(cr, PANEL_WIDTH, 0); cairo_line_to(cr, PANEL_WIDTH, height); cairo_stroke(cr);
}

void on_draw_axis(EwGuiCanvas *canvas, cairo_t *cr, int width, int height, void *user_data) {
    (void)canvas; (void)user_data;
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0); cairo_rectangle(cr, 0, 0, width, height); cairo_fill(cr);
    cairo_set_source_rgb(cr, 0.7, 0.7, 0.7); cairo_set_line_width(cr, 2.0); cairo_move_to(cr, 0, 0); cairo_line_to(cr, width, 0); cairo_stroke(cr);
    if (g_latest_time > 0) {
        cairo_set_source_rgb(cr, 0.0, 0.0, 0.0); cairo_set_font_size(cr, 13); cairo_select_font_face(cr, "Monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        double ws = iTimeWindowMinutes * 60.0; time_t tr = (time_t)(g_is_hold ? g_t_hold_time : g_smooth_time), tl = (time_t)(tr - ws), tc = (time_t)(tr - (ws/2.0));
        char sl[32], sc[32], sr[32]; strftime(sl, 32, "%H:%M:%S", gmtime(&tl)); strftime(sc, 32, "%H:%M:%S", gmtime(&tc)); strftime(sr, 32, "%H:%M:%S UTC", gmtime(&tr));
        cairo_move_to(cr, 10, height - 10); cairo_show_text(cr, "UTC TIME"); cairo_move_to(cr, PANEL_WIDTH + 10, height - 10); cairo_show_text(cr, sl);
        cairo_move_to(cr, PANEL_WIDTH + (width - PANEL_WIDTH)/2 - 30, height - 10); cairo_show_text(cr, sc); cairo_move_to(cr, width - 100, height - 10); cairo_show_text(cr, sr);

        if (g_data_stale) {
            cairo_set_source_rgb(cr, 1.0, 0.0, 0.0);
            cairo_move_to(cr, 10, 14);
            cairo_show_text(cr, "STALE DATA");
        }
    }
}

void act_stas_per_screen(GSimpleAction *a, GVariant *p, gpointer ud) { (void)a; (void)p; on_stas_per_screen_activate(NULL, ud); }

void act_time_window(GSimpleAction *a, GVariant *p, gpointer ud) { (void)a; (void)p; on_time_window_activate(NULL, ud); }

void act_clean_view(GSimpleAction *a, GVariant *p, gpointer ud) { (void)a; (void)p; on_clean_view_activate(NULL, ud); }

void act_reload_default(GSimpleAction *a, GVariant *p, gpointer ud) { (void)a; (void)p; on_reload_default_activate(NULL, ud); }

void act_filter_options(GSimpleAction *a, GVariant *p, gpointer ud) { (void)a; (void)p; on_filter_menu_activate(NULL, ud); }

void act_colour(GSimpleAction *a, GVariant *p, gpointer ud) {
    (void)a; (void)ud;
    int idx = 0;
    if (p) g_variant_get(p, "i", &idx);
    on_colour_select(NULL, GINT_TO_POINTER(idx));
}
