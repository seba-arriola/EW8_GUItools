#include "csnhypodbp.h"
#include <adwaita.h>

/* Forward declaration del helper estatico de alineacion por P */
static double station_pick_time(int i, int *is_real);

/* F3: buffers reutilizables para el binning por columna (evita una segunda
   pasada sobre las muestras solo para el auto-escalado). */
#define MAX_DRAW_W 4096
static double   g_col_min[MAX_DRAW_W];
static double   g_col_max[MAX_DRAW_W];
static gboolean g_col_has[MAX_DRAW_W];
static int      g_band_runs[2 * MAX_DRAW_W];   /* pares [start,end) por tramo */

/* F3: orden por canal (channelsOrder x componentsOrder) dentro de la estacion;
   la distancia desempata. */
static int g_chan_rank_n = 0;
static char g_chan_rank[64][8];

static void build_chan_rank(void) {
    g_chan_rank_n = csnhypodbp_channel_candidates(g_channels_order, g_components_order,
                                                  g_chan_rank, 64);
    if (g_chan_rank_n > 64) g_chan_rank_n = 64;
}

static int chan_rank(const char *chan) {
    int i;
    for (i = 0; i < g_chan_rank_n; i++)
        if (strcmp(g_chan_rank[i], chan) == 0) return i;
    return 1000;
}

static int cmp_node_chan(const void *a, const void *b) {
    const StaNode *na = a, *nb = b;
    int ra = chan_rank(StaArray[na->idx].szChannel);
    int rb = chan_rank(StaArray[nb->idx].szChannel);
    if (ra != rb) return ra - rb;
    return (na->dist > nb->dist) - (na->dist < nb->dist);
}

void actualizar_altura_canvas() {
    if (!canvas_global) return;
    int count = 0;
    for (int i = 0; i < NumEstaciones; i++) {
        if (bHasData[i] != 1) continue;
        if (!g_selected_station[0] || strcmp(StaArray[i].szStation, g_selected_station) != 0) continue;
        if ((g_StaDist[i] * 111.19) > g_max_dist_km) continue;
        count++;
    }
    if (count > 0) gtk_widget_set_size_request(ewgui_canvas_widget(canvas_global), -1, (count * CSNHYPODBP_BAND_H) + 60);
    else gtk_widget_set_size_request(ewgui_canvas_widget(canvas_global), -1, 600);
}

/* --- Lista de estaciones (panel izquierdo) -------------------------------
 * Una fila por estacion (dedup por nombre), ordenada por distancia. La
 * distancia es 0 hasta que llegan datos (se re-ordenan en ws_fetch_finish).
 * ------------------------------------------------------------------------ */
typedef struct { char name[16]; double dist_km; } STA_LIST_ITEM;

void rebuild_station_list(void) {
    if (!g_list_stations) return;
    GtkWidget *child;
    char sel[16];
    /* Guarda la seleccion ANTES de vaciar: gtk_list_box_remove emite
       row-selected(NULL) al quitar la fila seleccionada (GTK4) y
       on_station_selected borraria g_selected_station; como el canvas dibuja
       solo la estacion seleccionada, perderla dejaria el canvas vacio. */
    snprintf(sel, sizeof(sel), "%s", g_selected_station);
    g_rebuilding_stations = 1;
    while ((child = gtk_widget_get_first_child(g_list_stations)))
        gtk_list_box_remove(GTK_LIST_BOX(g_list_stations), child);

    static STA_LIST_ITEM items[MAX_ESTA];
    int n = 0, a, b;
    GtkWidget *to_select = NULL;

    for (int i = 0; i < NumEstaciones; i++) {
        double dkm = g_StaDist[i] * 111.19;
        if (dkm > 0.0 && dkm > g_max_dist_km) continue;
        /* HideEmptyStations: omite estaciones sin datos en la ventana. Es un
           filtro dinamico (no borra filas): si la estacion vuelve a tener
           datos (p. ej. por SeedLink), reaparece en la siguiente pasada. */
        if (g_hide_empty &&
            !csnhypodbp_station_has_data(StaArray, NumEstaciones, bHasData, StaArray[i].szStation))
            continue;
        int found = -1;
        for (int k = 0; k < n; k++)
            if (strcmp(items[k].name, StaArray[i].szStation) == 0) { found = k; break; }
        if (found >= 0) { if (items[found].dist_km <= 0.0) items[found].dist_km = dkm; continue; }
        if (n >= MAX_ESTA) break;
        snprintf(items[n].name, sizeof(items[n].name), "%s", StaArray[i].szStation);
        items[n].dist_km = dkm;
        n++;
    }

    /* Orden por distancia (distancia desconocida = 0 -> al final). */
    for (a = 1; a < n; a++) {
        STA_LIST_ITEM key = items[a];
        double ka = key.dist_km > 0.0 ? key.dist_km : 1e12;
        for (b = a - 1; b >= 0; b--) {
            double ib = items[b].dist_km > 0.0 ? items[b].dist_km : 1e12;
            if (ib <= ka) break;
            items[b + 1] = items[b];
        }
        items[b + 1] = key;
    }

    for (a = 0; a < n; a++) {
        char label[64];
        if (items[a].dist_km > 0.0) snprintf(label, sizeof(label), "%s  (%.0f km)", items[a].name, items[a].dist_km);
        else snprintf(label, sizeof(label), "%s", items[a].name);
        GtkWidget *row = gtk_list_box_row_new();
        GtkWidget *lbl = gtk_label_new(label);
        gtk_label_set_xalign(GTK_LABEL(lbl), 0.0);
        gtk_widget_set_margin_start(lbl, 6);
        gtk_widget_set_margin_end(lbl, 6);
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), lbl);
        g_object_set_data_full(G_OBJECT(row), "sta", g_strdup(items[a].name), g_free);
        gtk_list_box_append(GTK_LIST_BOX(g_list_stations), row);
        if (sel[0] && strcmp(items[a].name, sel) == 0) to_select = row;
    }
    g_rebuilding_stations = 0;
    if (to_select) gtk_list_box_select_row(GTK_LIST_BOX(g_list_stations), GTK_LIST_BOX_ROW(to_select));
    /* Si la estacion ya no esta (filtro dinamico), NO se borra la seleccion:
       el canvas la sigue dibujando y reaparece si el filtro la readmite. */
}

void on_station_selected(GtkListBox *box, GtkListBoxRow *row, gpointer data) {
    (void)box; (void)data;
    char prev[16];
    /* Durante el repoblado (vaciado -> row-selected(NULL)) no se pierde la
       seleccion ni se reinicia el zoom. */
    if (g_rebuilding_stations) return;
    snprintf(prev, sizeof(prev), "%s", g_selected_station);
    if (row) {
        const char *sta = g_object_get_data(G_OBJECT(row), "sta");
        if (sta) {
            snprintf(g_selected_station, sizeof(g_selected_station), "%s", sta);
            EnsureAndFetchStation(g_selected_station);
        }
    } else {
        g_selected_station[0] = '\0';
    }
    /* Solo al cambiar de estacion: re-seleccionar la misma no mata el zoom. */
    if (csnhypodbp_station_changed(prev, g_selected_station))
        csnhypodbp_view_set_default(&g_view, g_dScreenTime);
    actualizar_altura_canvas();
    if (canvas_global) ewgui_canvas_queue_draw(canvas_global);
}

/* --- Dialogo modal reutilizable (patron csntvp run_dialog) ---------------- */
typedef struct { GMainLoop *loop; gboolean ok; } EwDialogCtx;

static void ewdialog_ok(GtkButton *b, gpointer ud) { (void)b; EwDialogCtx *c = ud; c->ok = TRUE; g_main_loop_quit(c->loop); }
static void ewdialog_cancel(GtkButton *b, gpointer ud) { (void)b; EwDialogCtx *c = ud; c->ok = FALSE; g_main_loop_quit(c->loop); }

/* Muestra `content` en una ventana modal; TRUE si se acepto. El llamador debe
   leer los widgets ANTES de destruir `*out_win`. */
static gboolean ewdialog_run(GtkWindow *parent, const char *title, GtkWidget *content,
                             const char *ok_label, GtkWindow **out_win) {
    GtkWidget *win = gtk_window_new();
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *bok = gtk_button_new_with_label(ok_label ? ok_label : "OK");
    GtkWidget *bcancel = gtk_button_new_with_label("Cancel");
    EwDialogCtx ctx;

    gtk_window_set_title(GTK_WINDOW(win), title);
    gtk_window_set_modal(GTK_WINDOW(win), TRUE);
    if (parent) gtk_window_set_transient_for(GTK_WINDOW(win), parent);
    gtk_window_set_default_size(GTK_WINDOW(win), 320, -1);
    gtk_widget_set_margin_top(box, 12); gtk_widget_set_margin_bottom(box, 12);
    gtk_widget_set_margin_start(box, 12); gtk_widget_set_margin_end(box, 12);
    gtk_box_append(GTK_BOX(box), content);
    gtk_widget_set_halign(hbox, GTK_ALIGN_END);
    gtk_widget_add_css_class(bok, "suggested-action");
    gtk_box_append(GTK_BOX(hbox), bcancel);
    gtk_box_append(GTK_BOX(hbox), bok);
    gtk_box_append(GTK_BOX(box), hbox);
    gtk_window_set_child(GTK_WINDOW(win), box);

    ctx.loop = g_main_loop_new(NULL, FALSE);
    ctx.ok = FALSE;
    g_signal_connect(bcancel, "clicked", G_CALLBACK(ewdialog_cancel), &ctx);
    g_signal_connect(bok, "clicked", G_CALLBACK(ewdialog_ok), &ctx);
    gtk_window_present(GTK_WINDOW(win));
    g_main_loop_run(ctx.loop);
    g_main_loop_unref(ctx.loop);
    if (out_win) *out_win = GTK_WINDOW(win);
    else gtk_window_destroy(GTK_WINDOW(win));
    return ctx.ok;
}

static GtkWidget *labeled_entry(GtkWidget *grid, int row, const char *label, const char *value) {
    GtkWidget *l = gtk_label_new(label);
    GtkWidget *e = gtk_entry_new();
    gtk_label_set_xalign(GTK_LABEL(l), 0.0);
    gtk_editable_set_text(GTK_EDITABLE(e), value ? value : "");
    gtk_grid_attach(GTK_GRID(grid), l, 0, row, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), e, 1, row, 1, 1);
    return e;
}

void on_btn_filters_clicked(GtkWidget *w, gpointer d) {
    (void)w; (void)d;
    GtkWidget *grid = gtk_grid_new(), *r[4], *e1, *e2, *o[2], *win = NULL;
    const char *names[] = {"Raw (No Filter)","High-Pass","Low-Pass","Band-Pass"};
    char b1[16], b2[16];
    int i;
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    for (i = 0; i < 4; i++) {
        r[i] = gtk_check_button_new_with_label(names[i]);
        if (i > 0) gtk_check_button_set_group(GTK_CHECK_BUTTON(r[i]), GTK_CHECK_BUTTON(r[0]));
        gtk_grid_attach(GTK_GRID(grid), r[i], 0, i, 2, 1);
    }
    if (g_filter_type >= 0 && g_filter_type < 4)
        gtk_check_button_set_active(GTK_CHECK_BUTTON(r[g_filter_type]), TRUE);
    snprintf(b1, sizeof(b1), "%.2f", g_filter_f1);
    snprintf(b2, sizeof(b2), "%.2f", g_filter_f2);
    e1 = labeled_entry(grid, 4, "F1 (Hz)", b1);
    e2 = labeled_entry(grid, 5, "F2 (Hz)", b2);
    {
        GtkWidget *l = gtk_label_new("Order");
        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_label_set_xalign(GTK_LABEL(l), 0.0);
        o[0] = gtk_check_button_new_with_label("2");
        o[1] = gtk_check_button_new_with_label("4");
        gtk_check_button_set_group(GTK_CHECK_BUTTON(o[1]), GTK_CHECK_BUTTON(o[0]));
        gtk_check_button_set_active(GTK_CHECK_BUTTON(g_filter_order == 2 ? o[0] : o[1]), TRUE);
        gtk_box_append(GTK_BOX(box), o[0]); gtk_box_append(GTK_BOX(box), o[1]);
        gtk_grid_attach(GTK_GRID(grid), l, 0, 6, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), box, 1, 6, 1, 1);
    }
    if (ewdialog_run(window_global ? GTK_WINDOW(window_global) : NULL, "Filter Settings", grid, "Apply", &win)) {
        for (i = 0; i < 4; i++)
            if (gtk_check_button_get_active(GTK_CHECK_BUTTON(r[i]))) { g_filter_type = i; break; }
        g_filter_f1 = atof(gtk_editable_get_text(GTK_EDITABLE(e1)));
        g_filter_f2 = atof(gtk_editable_get_text(GTK_EDITABLE(e2)));
        g_filter_order = gtk_check_button_get_active(GTK_CHECK_BUTTON(o[0])) ? 2 : 4;
        ApplySelectedFilter();
        if (canvas_global) ewgui_canvas_queue_draw(canvas_global);
    }
    if (win) gtk_window_destroy(win);
}

void on_btn_timewin_clicked(GtkWidget *w, gpointer d) {
    (void)w; (void)d;
    GtkWidget *grid = gtk_grid_new(), *e, *win = NULL;
    char b[16];
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    snprintf(b, sizeof(b), "%.1f", g_dScreenTime / 60.0);
    e = labeled_entry(grid, 0, "Window (min)", b);
    if (ewdialog_run(window_global ? GTK_WINDOW(window_global) : NULL, "Time Window", grid, "Apply", &win)) {
        double v = atof(gtk_editable_get_text(GTK_EDITABLE(e)));
        if (v > 0.0) { if (v > 10.0) v = 10.0; g_dScreenTime = v * 60.0; }
        csnhypodbp_view_set_default(&g_view, g_dScreenTime);   /* nuevo ancho: sin zoom */
        actualizar_altura_canvas();
        if (canvas_global) ewgui_canvas_queue_draw(canvas_global);
    }
    if (win) gtk_window_destroy(win);
}

void on_btn_cancel_clicked(GtkWidget *w, gpointer d) {
    (void)w; (void)d;
    /* Cancela TODO el trabajo manual local del evento actual. */
    csnhypodbp_manual_clear();
    for (int i = 0; i < NumEstaciones; i++) StaArray[i].dManualPickTime = 0.0;
    logit("t", "csnhypodbp: Cancel -> overlay manual limpiado\n");
    if (canvas_global) ewgui_canvas_queue_draw(canvas_global);
}

void on_btn_locators_clicked(GtkWidget *w, gpointer d) {
    (void)w; (void)d;
    GtkWidget *grid = gtk_grid_new(), *r[32], *win = NULL;
    int i, sel = -1;
    if (g_nlocators <= 0) return;
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    for (i = 0; i < g_nlocators && i < 32; i++) {
        r[i] = gtk_check_button_new_with_label(g_locators[i].name);
        if (i > 0) gtk_check_button_set_group(GTK_CHECK_BUTTON(r[i]), GTK_CHECK_BUTTON(r[0]));
        gtk_grid_attach(GTK_GRID(grid), r[i], 0, i, 1, 1);
        if (g_active_locator[0] && strcmp(g_active_locator, g_locators[i].name) == 0) sel = i;
    }
    if (sel < 0) sel = 0;
    gtk_check_button_set_active(GTK_CHECK_BUTTON(r[sel]), TRUE);
    if (ewdialog_run(window_global ? GTK_WINDOW(window_global) : NULL, "Locators", grid, "Select", &win)) {
        for (i = 0; i < g_nlocators && i < 32; i++)
            if (gtk_check_button_get_active(GTK_CHECK_BUTTON(r[i]))) {
                snprintf(g_active_locator, sizeof(g_active_locator), "%s", g_locators[i].name);
                break;
            }
    }
    if (win) gtk_window_destroy(win);
}

/* --- locate: worker + preview --- */
typedef struct {
    CsnhypodbpSolution base;
    CsnhypodbpManualPick mp[CSNHYPODBP_MANUAL_MAX];
    int nmp;
    CsnhypodbpTankTrace tank[MAX_ESTA];
    int ntank;
    char locator[32];
    CsnhypodbpSolution out;
    CsnhypodbpMagOut mag;
    char err[256];
    int rc;
} LocateJob;

static gboolean locate_finish(gpointer data);

static gpointer locate_thread(gpointer data) {
    LocateJob *job = data;
    job->rc = csnhypodbp_locate_run(g_locators, g_nlocators, job->locator,
                                    getenv("EW_PARAMS"), getenv("EW_HOME"), getenv("EW_VERSION"),
                                    NULL, &job->base, job->mp, job->nmp,
                                    job->tank, job->ntank, &job->out, &job->mag,
                                    job->err, sizeof(job->err));
    g_idle_add(locate_finish, job);
    return NULL;
}

static gboolean locate_finish(gpointer data) {
    LocateJob *job = data;
    if (job->rc == 0) {
        g_preview_sol = job->out;
        g_preview_mag = job->mag;
        g_preview_valid = TRUE;
        csnhypodbp_show_preview();
        logit("t", "csnhypodbp: locate OK [%s] nph=%d rms=%.2f ML=%s Mwp=%s\n",
              job->locator, job->out.nph, job->out.rms,
              job->mag.ml[0] ? job->mag.ml : "-", job->mag.mwp[0] ? job->mag.mwp : "-");
    } else {
        logit("e", "csnhypodbp: locate fallo [%s]: %s\n", job->locator, job->err);
    }
    g_free(job);
    return G_SOURCE_REMOVE;
}

void csnhypodbp_show_preview(void) {
    char buf[512];
    if (!g_preview_box || !g_preview_label) return;
    snprintf(buf, sizeof(buf),
        "Candidata [%s]: lat %.2f  lon %.2f  prof %.0f km   nph %d   rms %.2f   ML %s   Mwp %s",
        g_active_locator[0] ? g_active_locator : "-",
        g_preview_sol.lat, g_preview_sol.lon, g_preview_sol.depth,
        g_preview_sol.nph, g_preview_sol.rms,
        g_preview_mag.ml[0] ? g_preview_mag.ml : "-",
        g_preview_mag.mwp[0] ? g_preview_mag.mwp : "-");
    gtk_label_set_text(GTK_LABEL(g_preview_label), buf);
    gtk_widget_set_visible(g_preview_box, TRUE);
}

void on_preview_discard(GtkWidget *w, gpointer d) {
    (void)w; (void)d;
    g_preview_valid = FALSE;
    if (g_preview_box) gtk_widget_set_visible(g_preview_box, FALSE);
}

void on_preview_confirm(GtkWidget *w, gpointer d) {
    (void)w; (void)d;
    if (!g_preview_valid) return;
    csnhypodbp_confirm_manual(&g_preview_sol, &g_preview_mag);
    g_preview_valid = FALSE;
    if (g_preview_box) gtk_widget_set_visible(g_preview_box, FALSE);
    csnhypodbp_manual_clear();
    if (canvas_global) ewgui_canvas_queue_draw(canvas_global);
}

void on_btn_locate_clicked(GtkWidget *w, gpointer d) {
    LocateJob *job;
    GThread *th;
    gpointer item;
    CsnhypodbpRow *row;
    EventGroup *g;
    int i;
    (void)w; (void)d;
    if (selected_qid == 0) { logit("t", "csnhypodbp: locate sin evento seleccionado\n"); return; }
    if (g_nlocators <= 0) { logit("t", "csnhypodbp: locate sin localizadores en el .d\n"); return; }
    item = gtk_single_selection_get_selected_item(g_selection_detail);
    if (!item) return;
    row = CSNHYPODBP_ROW(item);
    g = csnhypodbp_event_group_of(row);
    job = g_new0(LocateJob, 1);
    job->base.qid = selected_qid; job->base.version = (unsigned)selected_version; job->base.mod = selected_mod;
    job->base.otime = selected_otime; job->base.lat = selected_lat;
    job->base.lon = selected_lon; job->base.depth = selected_depth;
    job->base.npicks = g ? csnhypodbp_event_group_picks(g, job->base.picks, CSNHYPODBP_MAX_PICKS) : 0;
    job->base.nph = job->base.npicks;
    job->nmp = csnhypodbp_manual_list(job->mp, CSNHYPODBP_MANUAL_MAX);
    if (job->nmp > CSNHYPODBP_MANUAL_MAX) job->nmp = CSNHYPODBP_MANUAL_MAX;
    for (i = 0; i < NumEstaciones && job->ntank < MAX_ESTA; i++) {
        EwGuiTrace *tr = StaArray[i].trace;
        CsnhypodbpTankTrace *t;
        if (bHasData[i] != 1 || !tr || ewgui_trace_length(tr) <= 0) continue;
        t = &job->tank[job->ntank++];
        snprintf(t->sta, sizeof(t->sta), "%s", StaArray[i].szStation);
        snprintf(t->net, sizeof(t->net), "%s", StaArray[i].szNetID);
        snprintf(t->chan, sizeof(t->chan), "%s", StaArray[i].szChannel);
        snprintf(t->loc, sizeof(t->loc), "%s", StaArray[i].szLocation);
        t->rate = ewgui_trace_rate(tr);
        t->oldest = ewgui_trace_oldest(tr);
        t->samples = ewgui_trace_raw(tr);
        t->nsamp = ewgui_trace_length(tr);
    }
    snprintf(job->locator, sizeof(job->locator), "%s", g_active_locator);
    logit("t", "csnhypodbp: locate [%s] qid=%d npicks=%d overlay=%d tank=%d\n",
          job->locator, job->base.qid, job->base.npicks, job->nmp, job->ntank);
    th = g_thread_new("locate", locate_thread, job);
    g_thread_unref(th);
}

void on_btn_fetch_clicked(GtkWidget *widget, gpointer data) {
    (void)widget; (void)data;
    GtkWidget *grid = gtk_grid_new(), *e, *win = NULL;
    char b[16];
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    snprintf(b, sizeof(b), "%.0f", g_max_dist_km);
    e = labeled_entry(grid, 0, "Distance (km)", b);
    if (ewdialog_run(window_global ? GTK_WINDOW(window_global) : NULL, "Fetch Waveforms", grid, "Fetch", &win)) {
        double v = atof(gtk_editable_get_text(GTK_EDITABLE(e)));
        if (v > 0.0) g_max_dist_km = v;
        rebuild_station_list();
        actualizar_altura_canvas();
        if (canvas_global) ewgui_canvas_queue_draw(canvas_global);
        pending_waveform_reload = TRUE;
    }
    if (win) gtk_window_destroy(win);
}

/* color_rows_func eliminado: en GTK4 el resaltado de fila lo da el tema. */

/* 1 si la pestaña Ondas (indice 1) es la activa. */
static gboolean waves_page_active(void) {
    return g_notebook && gtk_notebook_get_current_page(GTK_NOTEBOOK(g_notebook)) == 1;
}

/* Loguea los colores de fase en hex, listos para copiar al .d. */
void csnhypodbp_log_colors(void) {
    logit("t", "csnhypodbp: pColor %02X%02X%02X  sColor %02X%02X%02X (copiar a csnhypodbp.d)\n",
          (unsigned)(g_color_p[0] * 255.0 + 0.5),
          (unsigned)(g_color_p[1] * 255.0 + 0.5),
          (unsigned)(g_color_p[2] * 255.0 + 0.5),
          (unsigned)(g_color_s[0] * 255.0 + 0.5),
          (unsigned)(g_color_s[1] * 255.0 + 0.5),
          (unsigned)(g_color_s[2] * 255.0 + 0.5));
}

/* --- Colores de fase (grupo "Colours" del menu ☰) ---------------------- */
static void on_color_chosen(GObject *src, GAsyncResult *res, gpointer data) {
    int type = GPOINTER_TO_INT(data);
    GdkRGBA *c = gtk_color_dialog_choose_rgba_finish(GTK_COLOR_DIALOG(src), res, NULL);
    if (c) {
        double *dst = (type == 2) ? g_color_s : g_color_p;
        dst[0] = c->red; dst[1] = c->green; dst[2] = c->blue;
        csnhypodbp_log_colors();
        if (canvas_global) ewgui_canvas_queue_draw(canvas_global);
        gdk_rgba_free(c);
    }
    g_object_unref(src);
}

/* type: 1 = P, 2 = S (parametro "i" de la accion win.colour). */
void on_colour_select(GtkWidget *widget, gpointer data) {
    (void)widget;
    int type = GPOINTER_TO_INT(data);
    const double *src = (type == 2) ? g_color_s : g_color_p;
    GdkRGBA cur = { src[0], src[1], src[2], 1.0 };
    GtkColorDialog *cd = gtk_color_dialog_new();
    gtk_color_dialog_set_title(cd, (type == 2) ? "S phase colour" : "P phase colour");
    gtk_color_dialog_choose_rgba(cd, NULL, &cur, NULL, on_color_chosen, GINT_TO_POINTER(type));
}

void act_colour(GSimpleAction *action, GVariant *param, gpointer user_data) {
    (void)action; (void)user_data;
    int idx = 0;
    if (param) g_variant_get(param, "i", &idx);
    on_colour_select(NULL, GINT_TO_POINTER(idx));
}

/* Etiqueta de ayuda de la barra inferior, segun el modo de clic. */
void csnhypodbp_update_help(void) {
    if (!g_help_label) return;
    if (g_click_mode == CSNHYPODBP_CLICK_PICK)
        gtk_label_set_text(GTK_LABEL(g_help_label),
            "P/S activo: clic izq coloca la fase y vuelve a zoom | Esc: cancelar");
    else
        gtk_label_set_text(GTK_LABEL(g_help_label),
            "P/S: elegir fase | clic izq: 1.º ancla, 2.º (der) zoom / (izq) reset | clic der: quitar pick | eje: arrastrar = zoom | Shift+←/→: contraer/expandir tiempo | Shift+↑/↓: escala");
}

gboolean on_key_press(GtkEventControllerKey *ctrl, guint keyval, guint keycode, GdkModifierType state, gpointer data) {
    (void)ctrl; (void)keycode; (void)data;
    gboolean shift;
    if (!waves_page_active()) return FALSE;   /* no interceptar en Eventos/Config */
    shift = (state & GDK_SHIFT_MASK) != 0;
    /* Shift+flechas: exageracion vertical. Las ↑/↓ solas NO se consumen aqui,
       asi siguen navegando la lista de estaciones (el listbox tiene el foco). */
    if (shift && (keyval == GDK_KEY_Up || keyval == GDK_KEY_KP_Up)) {
        g_zoom_factor = csnhypodbp_zoom_step(g_zoom_factor, +1);
        if (canvas_global) ewgui_canvas_queue_draw(canvas_global);
        return TRUE;
    }
    if (shift && (keyval == GDK_KEY_Down || keyval == GDK_KEY_KP_Down)) {
        g_zoom_factor = csnhypodbp_zoom_step(g_zoom_factor, -1);
        if (canvas_global) ewgui_canvas_queue_draw(canvas_global);
        return TRUE;
    }
    /* Shift+←/→: contrae/expande el eje temporal (anclado al centro). */
    if (shift && (keyval == GDK_KEY_Left || keyval == GDK_KEY_KP_Left)) {
        double sp = csnhypodbp_view_time_zoom(&g_view, -1);
        logit("t", "csnhypodbp: Shift+izq tiempo span=%.3f\n", sp);
        if (canvas_global) ewgui_canvas_queue_draw(canvas_global);
        return TRUE;
    }
    if (shift && (keyval == GDK_KEY_Right || keyval == GDK_KEY_KP_Right)) {
        double sp = csnhypodbp_view_time_zoom(&g_view, +1);
        logit("t", "csnhypodbp: Shift+der tiempo span=%.3f\n", sp);
        if (canvas_global) ewgui_canvas_queue_draw(canvas_global);
        return TRUE;
    }
    if (shift && (keyval == GDK_KEY_0 || keyval == GDK_KEY_KP_0)) {
        g_zoom_factor = csnhypodbp_zoom_step(g_zoom_factor, 0);
        csnhypodbp_view_time_zoom(&g_view, 0);   /* resetea tambien el tiempo */
        if (canvas_global) ewgui_canvas_queue_draw(canvas_global);
        return TRUE;
    }
    if (keyval == GDK_KEY_Escape) {
        g_click_mode = CSNHYPODBP_CLICK_ZOOM;
        csnhypodbp_view_cancel_anchor(&g_view);   /* cancela el zoom a medias, no el zoom */
        csnhypodbp_update_help();
        if (canvas_global) ewgui_canvas_queue_draw(canvas_global);
        return TRUE;
    }
    if (keyval == GDK_KEY_p || keyval == GDK_KEY_P) {
        g_pick_phase = 'P'; g_click_mode = CSNHYPODBP_CLICK_PICK;
        csnhypodbp_update_help();
        if (canvas_global) ewgui_canvas_queue_draw(canvas_global);
        return TRUE;
    }
    if (keyval == GDK_KEY_s || keyval == GDK_KEY_S) {
        g_pick_phase = 'S'; g_click_mode = CSNHYPODBP_CLICK_PICK;
        csnhypodbp_update_help();
        if (canvas_global) ewgui_canvas_queue_draw(canvas_global);
        return TRUE;
    }
    return FALSE;
}

void on_row_selected(GtkSingleSelection *sel, GParamSpec *pspec, gpointer data) {
    (void)sel; (void)pspec; (void)data;
    /* Con GtkSortListModel la posicion de la seleccion es la de la vista
     * ordenada (no la del store), asi que se obtiene el item directamente.
     * get_selected_item es transfer-none: NO se libera. */
    gpointer item = gtk_single_selection_get_selected_item(g_selection_detail);
    if (item) {
        CsnhypodbpRow *row = CSNHYPODBP_ROW(item);
        double otime = 0, lat = 0, lon = 0, depth = 0; int qid = 0, mod = 0, ver = 0;
        const char *id_str = csnhypodbp_row_col(row, 8);
        g_object_get(row, "otime", &otime, "qid", &qid, "lat", &lat, "lon", &lon,
                     "depth", &depth, "mod", &mod, "qver", &ver, NULL);
        if (id_str) strcpy(selected_id, id_str);
        /* Solo se reinicia el zoom si cambia la solucion elegida: reconstruir
           el detalle del mismo evento (upsert del anillo) no debe matar el
           zoom que el usuario ajusto. */
        {
            int changed = csnhypodbp_solution_changed(selected_qid, selected_mod,
                                                      selected_version, qid, mod, ver);
            selected_otime = otime; selected_qid = qid; selected_lat = lat; selected_lon = lon;
            selected_depth = depth;
            selected_mod = mod; selected_version = ver;
            if (changed) {
                g_zoom_factor = 1.0;
                csnhypodbp_view_set_default(&g_view, g_dScreenTime);
            }
        }
    }
}

/* Al seleccionar un evento (lista maestra) se despliega abajo la lista de sus
   soluciones, con la preferida en primer lugar. */
void on_event_selected(GtkSingleSelection *sel, GParamSpec *pspec, gpointer data) {
    (void)pspec; (void)data;
    gpointer item = gtk_single_selection_get_selected_item(sel);
    if (!g_store_detail) return;
    g_list_store_remove_all(g_store_detail);
    if (!item) return;
    {
        CsnhypodbpRow *row = CSNHYPODBP_ROW(item);
        EventGroup *g = csnhypodbp_event_group_of(row);
        CsnhypodbpRow *pref;
        size_t n, i;
        if (!g) return;
        pref = csnhypodbp_event_group_preferred(g);
        if (pref) g_list_store_append(g_store_detail, pref);
        n = csnhypodbp_event_group_nsol(g);
        for (i = 0; i < n; i++) {
            CsnhypodbpRow *s = csnhypodbp_event_group_solution(g, i);
            if (s && s != pref) g_list_store_append(g_store_detail, s);
        }
        if (g_selection_detail)
            gtk_selection_model_select_item(GTK_SELECTION_MODEL(g_selection_detail), 0, TRUE);
    }
}

void on_list_double_click(GtkGestureClick *g, int n_press, double x, double y, gpointer data) {
    (void)g; (void)x; (void)y; (void)data;
    if (n_press != 2) return;
    guint idx = gtk_single_selection_get_selected(g_selection_detail);
    if (idx == GTK_INVALID_LIST_POSITION) return;
    if (g_notebook) gtk_notebook_set_current_page(GTK_NOTEBOOK(g_notebook), 1);  /* pestaña Ondas */
    csnhypodbp_manual_clear();   /* nuevo evento: overlay manual limpio */
    pending_waveform_reload = TRUE;
    waveform_reload_timer(NULL);   /* fetch inmediato */
}

gboolean waveform_reload_timer(gpointer data) {
    (void)data;
    if (pending_waveform_reload && selected_qid != 0) {
        pending_waveform_reload = FALSE;
        if (window_global) gtk_window_set_title(GTK_WINDOW(window_global), "CSNhypodbp - Descargando ondas historicas...");
        FetchWaveformsForEvent(selected_otime, selected_lat, selected_lon, selected_qid, selected_mod, selected_version);
        /* La descarga corre en un worker (no bloquea la UI); el titulo y el
           redibujado se restauran en ws_fetch_finish() al publicar el resultado. */
    }
    return TRUE;
}

void on_btn_repick_clicked(GtkWidget *widget, gpointer data) {
    /* Retirado: el picado P/S ya es permanente (ver on_key_press/on_canvas_clicked). */
    (void)widget; (void)data;
}

void on_canvas_clicked(GtkGestureClick *gesture, int n_press, double x, double y, gpointer data) {
    (void)gesture; (void)n_press; (void)data;
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

    if (g_click_mode == CSNHYPODBP_CLICK_PICK) {
        /* Modo picado (activado con p/S): coloca la fase en la traza clickeada
           y vuelve al modo zoom. */
        double clicked_time = g_win_start_cur
                            + csnhypodbp_view_x_to_rel(&g_view, x, g_margin_left, draw_width);
        CsnhypodbpManualPick mp;
        memset(&mp, 0, sizeof(mp));
        snprintf(mp.sta, sizeof(mp.sta), "%s", StaArray[i].szStation);
        snprintf(mp.chan, sizeof(mp.chan), "%s", StaArray[i].szChannel);
        mp.phase = g_pick_phase;
        mp.t = clicked_time;
        csnhypodbp_manual_add(&mp);
        logit("t", "csnhypodbp: pick manual local %s.%s %c @ %.3f (overlay; no emitido)\n",
              mp.sta, mp.chan, mp.phase, mp.t);
        g_click_mode = CSNHYPODBP_CLICK_ZOOM;
        csnhypodbp_update_help();
        ewgui_canvas_queue_draw(canvas_global);
        return;
    }

    /* Modo zoom (Seisan): 1.er clic ancla; 2.º a la derecha hace zoom, a la
       izquierda vuelve al default. */
    {
        double rel = csnhypodbp_view_x_to_rel(&g_view, x, g_margin_left, draw_width);
        csnhypodbp_view_click(&g_view, rel);
        ewgui_canvas_queue_draw(canvas_global);
    }
}

/* Clic derecho sobre una traza: quita las picadas manuales de ese canal. */
void on_canvas_right_click(GtkGestureClick *gesture, int n_press, double x, double y, gpointer data) {
    (void)gesture; (void)n_press; (void)x; (void)data;
    int target = -1, i, r;
    for (int n = 0; n < g_NumSortedNodes; n++) {
        if (y >= g_SortedNodes[n].y_top && y <= g_SortedNodes[n].y_bottom) { target = n; break; }
    }
    if (target == -1) return;
    i = g_SortedNodes[target].idx;
    r = csnhypodbp_manual_remove_trace(StaArray[i].szStation, StaArray[i].szChannel);
    logit("t", "csnhypodbp: clic der: -%d picks de %s.%s\n",
          r, StaArray[i].szStation, StaArray[i].szChannel);
    ewgui_canvas_queue_draw(canvas_global);
}

/* Puntero sobre el canvas: guarda la posicion para el indicador de picado. */
void on_canvas_motion(GtkEventControllerMotion *ctrl, double x, double y, gpointer data) {
    (void)ctrl; (void)data;
    g_cursor_x = x; g_cursor_y = y;
    g_cursor_valid = 1;
    if (g_click_mode == CSNHYPODBP_CLICK_PICK && canvas_global)
        ewgui_canvas_queue_draw(canvas_global);
}

/* El puntero sale del canvas: oculta el indicador. */
void on_canvas_leave(GtkEventControllerMotion *ctrl, gpointer data) {
    (void)ctrl; (void)data;
    if (!g_cursor_valid) return;
    g_cursor_valid = 0;
    if (g_click_mode == CSNHYPODBP_CLICK_PICK && canvas_global)
        ewgui_canvas_queue_draw(canvas_global);
}

/* Clic derecho sobre la lista: quita todas las picadas de esa estacion. */
void on_station_right_click(GtkGestureClick *gesture, int n_press, double x, double y, gpointer data) {
    (void)gesture; (void)n_press; (void)x; (void)data;
    GtkListBoxRow *row;
    const char *sta;
    int r;
    if (!g_list_stations) return;
    row = gtk_list_box_get_row_at_y(GTK_LIST_BOX(g_list_stations), (int)y);
    if (!row) return;
    sta = g_object_get_data(G_OBJECT(row), "sta");
    if (!sta) return;
    r = csnhypodbp_manual_remove_station(sta);
    logit("t", "csnhypodbp: clic der: -%d picks de la estacion %s\n", r, sta);
    ewgui_canvas_queue_draw(canvas_global);
}

/* --------------------------------------------------------------------
 * Helper de alineacion por onda P (portado de EW7 new_hypo_display):
 *  - station_pick_time: marca P de la estacion (pick real del PICK_RING, o
 *    la P teorica IASP91, o la hora origen). El inicio de ventana ya no es
 *    por fila sino COMUN a la estacion (ver on_draw_signal / g_win_start_cur),
 *    para que el eje UTC y el picado valgan igual en todos los canales.
 * -------------------------------------------------------------------- */
static double station_pick_time(int i, int *is_real) {
    CsnhypodbpTracePick pk;
    if (is_real) *is_real = 0;
    /* P capturada del PICK_RING (A/M); si no, P teorica (IASP91). */
    if (csnhypodbp_cap_find(&StaArray[i].pickcap, StaArray[i].szStation, 'P',
                            selected_otime, g_pick_window_sec, &pk)) {
        if (is_real) *is_real = 1;
        return pk.t;
    }
    if (g_StaTheoP[i] > 1.0) return g_StaTheoP[i];
    return selected_otime;
}

/* Nota: el inicio de ventana ya no es por fila sino COMUN a la estacion
   (se calcula en on_draw_signal y queda en g_win_start_cur), para que el eje
   UTC y el picado valgan igual en todos los canales. */

/* --------------------------------------------------------------------
 * ApplySelectedFilter: aplica el filtro IIR configurado (Filters) a todas
 * las estaciones con datos, validando Nyquist y quitando el DC antes.
 * -------------------------------------------------------------------- */
void ApplySelectedFilter(void) {
    EwFilterParams fp;
    fp.filter_type = g_filter_type;
    fp.f1 = g_filter_f1;
    fp.f2 = g_filter_f2;
    fp.order = g_filter_order;

    for (int i = 0; i < NumEstaciones; i++) {
        if (ewgui_trace_length(StaArray[i].trace) <= 0) continue;
        ewgui_trace_filter(StaArray[i].trace, &fp);
    }
}

void on_draw_signal(EwGuiCanvas *canvas, cairo_t *cr, int width, int height, void *data) {
    (void)data;
    cairo_set_source_rgb(cr, 1, 1, 1); cairo_paint(cr);

    int margin_right = 10;
    int draw_width = width - g_margin_left - margin_right;
    if (draw_width <= 0) return;

    /* Titulo */
    cairo_set_source_rgb(cr, 0.5, 0.1, 0.1);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 14); cairo_move_to(cr, width / 2 - 120, 20);
    char buf[128];
    snprintf(buf, sizeof(buf), "Historical Data - Quake ID: %s%s%s",
             selected_id,
             g_selected_station[0] ? " - " : "",
             g_selected_station[0] ? g_selected_station : "");
    cairo_show_text(cr, buf);

    /* Solo los canales de la estacion seleccionada (por defecto: ninguna). */
    g_NumSortedNodes = 0;
    for (int i = 0; i < NumEstaciones; i++) {
        if (bHasData[i] != 1) continue;
        if (!g_selected_station[0] || strcmp(StaArray[i].szStation, g_selected_station) != 0) continue;
        if ((g_StaDist[i] * 111.19) <= g_max_dist_km) {
            g_SortedNodes[g_NumSortedNodes].idx = i;
            g_SortedNodes[g_NumSortedNodes].dist = g_StaDist[i];
            g_NumSortedNodes++;
        }
    }
    if (g_NumSortedNodes > 1) {
        build_chan_rank();
        qsort(g_SortedNodes, (size_t)g_NumSortedNodes, sizeof(StaNode), cmp_node_chan);
    }

    /* Alto de banda: reparte la altura del canvas entre las trazas visibles
       (~una componente por pantalla por defecto). */
    int row_h = (g_NumSortedNodes > 0 && height > 160) ? (height - 60) / g_NumSortedNodes : g_spacing;
    if (row_h < 80) row_h = g_spacing;

    /* Marca P mas temprana -> ventana global de referencia */
    double earliest_pick = 1e15;
    for (int n = 0; n < g_NumSortedNodes; n++) {
        int i = g_SortedNodes[n].idx;
        double pt = station_pick_time(i, NULL);
        if (pt > 1.0 && pt < earliest_pick) earliest_pick = pt;
    }
    if (earliest_pick == 1e15) earliest_pick = selected_otime + 15.0;
    g_dWindowStart = earliest_pick - g_align_lead;

    /* Ventana visible (zoom) y winStart COMUN de la estacion (para el eje UTC).
       La P de referencia es la real si algun canal la trae (p. ej. HHZ), si no
       la teorica; asi TODAS las trazas comparten ventana y un unico eje UTC
       vale para todas (la P capturada vive solo en la fila que la recibio). */
    double span = g_view.t1 - g_view.t0;
    if (span <= 0.0) span = g_dScreenTime;
    double sta_pick_t = selected_otime;
    int    sta_is_real = 0;
    if (g_NumSortedNodes > 0) {
        int n;
        for (n = 0; n < g_NumSortedNodes; n++) {
            int is_real = 0;
            double pt = station_pick_time(g_SortedNodes[n].idx, &is_real);
            if (is_real) { sta_pick_t = pt; sta_is_real = 1; break; }
        }
        if (!sta_is_real) sta_pick_t = station_pick_time(g_SortedNodes[0].idx, NULL);
    }
    double sta_win_start = sta_pick_t - g_align_lead;
    g_win_start_cur = sta_win_start;

    /* Linea de referencia vertical discontinua (todas las P alineadas) */
    int x_ref = (int)csnhypodbp_view_rel_to_x(&g_view, g_align_lead, g_margin_left, draw_width);
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
        int y_center = (n * row_h) + (row_h / 2) + 30;
        g_SortedNodes[n].y_top = y_center - (row_h / 2);
        g_SortedNodes[n].y_bottom = y_center + (row_h / 2);

        int is_real = sta_is_real;
        double pick_t = sta_pick_t;
        double winStart = sta_win_start;

        /* LED de estado: verde = pick real, rojo = referencia origen */
        cairo_new_path(cr); cairo_set_line_width(cr, 1.0);
        if (is_real) cairo_set_source_rgb(cr, 0.2, 0.8, 0.2); else cairo_set_source_rgb(cr, 0.8, 0.2, 0.2);
        cairo_arc(cr, 15, y_center + 1, 5, 0, 2 * M_PI); cairo_fill_preserve(cr);
        cairo_set_source_rgb(cr, 0.0, 0.0, 0.0); cairo_stroke(cr);

        /* Etiqueta por componente (HHZ/HHN/HHE); la estacion va en el titulo. */
        cairo_set_source_rgb(cr, 0.1, 0.2, 0.7);
        char sta_label[64];
        csnhypodbp_trace_label(sta_label, sizeof(sta_label),
                               StaArray[i].szStation, StaArray[i].szChannel,
                               g_SortedNodes[n].dist * 111.19);
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
                double px_t_start = winStart + g_view.t0 + ((double)px / ncol) * span;
                double px_t_end   = winStart + g_view.t0 + ((double)(px + 1) / ncol) * span;
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
                double trgb[3];
                csnhypodbp_trace_rgb(is_real, trgb);
                cairo_set_source_rgb(cr, trgb[0], trgb[1], trgb[2]);
                cairo_set_line_width(cr, 0.8);
                cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
                cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
                double scale = (row_h * 0.45) / max_abs * g_zoom_factor;
                cairo_save(cr);
                cairo_rectangle(cr, g_margin_left, y_center - (row_h / 2.0), draw_width, row_h);
                cairo_clip(cr);

                if (csnhypodbp_view_use_polyline(span, ncol, tr_rate)) {
                    /* Zoom alto: un pixel cubre <2 muestras y el envelope
                       degenera en rectas de altura 0 (traza "a guiones"). Se
                       dibuja una polilinea por muestras (continua); se corta
                       en los gaps (INT_MAX) sin unir a traves del hueco. */
                    long k0 = (long)floor((winStart + g_view.t0 - tr_oldest) * tr_rate) - 1;
                    long k1 = (long)ceil ((winStart + g_view.t1 - tr_oldest) * tr_rate) + 1;
                    gboolean pen = FALSE;
                    if (k0 < 0) k0 = 0;
                    if (k1 > tr_len) k1 = tr_len;
                    for (long k = k0; k < k1; k++) {
                        int32_t val = tr_filt[k];
                        if (val == INT_MAX) { pen = FALSE; continue; }
                        double x = g_margin_left + csnhypodbp_view_sample_x(
                                       tr_oldest + (double)k / tr_rate,
                                       winStart + g_view.t0, span, ncol);
                        double y = y_center - (double)val * scale;
                        if (pen) cairo_line_to(cr, x, y);
                        else     cairo_move_to(cr, x, y);
                        pen = TRUE;
                    }
                } else {
                    /* Envolvente continua: por cada tramo de columnas con
                       datos se cierra una banda (max izq->der, min der->izq) y
                       se rellena; al conectar las columnas no quedan huecos ni
                       con spp intermedio. `min_half` garantiza que una zona
                       plana (min==max) siga dejando trazo visible. */
                    double min_half = csnhypodbp_view_min_half(scale, 0.8);
                    int nruns = csnhypodbp_view_band_runs(g_col_has, ncol,
                                                          g_band_runs, MAX_DRAW_W);
                    for (int r = 0; r < nruns; r++) {
                        int px0 = g_band_runs[2 * r];
                        int px1 = g_band_runs[2 * r + 1];
                        for (int px = px0; px < px1; px++) {
                            double x = g_margin_left + px;
                            double ytop = y_center - (g_col_max[px] + min_half) * scale;
                            if (px == px0) cairo_move_to(cr, x, ytop);
                            else           cairo_line_to(cr, x, ytop);
                        }
                        for (int px = px1 - 1; px >= px0; px--) {
                            double x = g_margin_left + px;
                            double ybot = y_center - (g_col_min[px] - min_half) * scale;
                            cairo_line_to(cr, x, ybot);
                        }
                        cairo_close_path(cr);
                        cairo_fill(cr);
                        cairo_new_path(cr);
                    }
                }
                cairo_stroke(cr); cairo_restore(cr);
            }
        }

        /* Marca P sobre la linea de referencia (todas alineadas). */
        if (pick_t > 1.0 && pick_t >= winStart + g_view.t0 && pick_t <= winStart + g_view.t1) {
            CsnhypodbpTracePick pcap;
            int has_p = csnhypodbp_cap_find(&StaArray[i].pickcap, StaArray[i].szStation,
                                            'P', selected_otime, g_pick_window_sec, &pcap);
            cairo_set_source_rgb(cr, g_color_p[0], g_color_p[1], g_color_p[2]);
            cairo_set_line_width(cr, 2);
            cairo_move_to(cr, x_ref, y_center - (row_h / 2.5));
            cairo_line_to(cr, x_ref, y_center + (row_h / 2.5));
            cairo_stroke(cr);
            cairo_set_font_size(cr, 11);
            cairo_move_to(cr, x_ref + 4, y_center - (row_h / 3));
            char plabel[16];
            if (has_p) snprintf(plabel, sizeof(plabel), "P(%c)", pcap.origin);
            else       snprintf(plabel, sizeof(plabel), "P(theo)");
            cairo_show_text(cr, plabel);
        }

        /* Marcas S capturadas (A/M) de la estacion, sobre la traza. */
        {
            CsnhypodbpTracePick scaps[8];
            int ns = csnhypodbp_cap_find_all(&StaArray[i].pickcap, StaArray[i].szStation,
                                             'S', selected_otime, g_pick_window_sec, scaps, 8);
            for (int s = 0; s < ns; s++) {
                double rel = scaps[s].t - winStart;
                if (rel < g_view.t0 || rel > g_view.t1) continue;
                double xs = csnhypodbp_view_rel_to_x(&g_view, rel, g_margin_left, draw_width);
                cairo_set_source_rgb(cr, g_color_s[0], g_color_s[1], g_color_s[2]);
                cairo_set_line_width(cr, 1.5);
                cairo_move_to(cr, xs, y_center - (row_h / 2.5));
                cairo_line_to(cr, xs, y_center + (row_h / 2.5));
                cairo_stroke(cr);
                cairo_set_font_size(cr, 11);
                char slab[16];
                snprintf(slab, sizeof(slab), "S(%c)", scaps[s].origin);
                cairo_move_to(cr, xs + 4, y_center + (row_h / 3));
                cairo_show_text(cr, slab);
            }
        }

        /* Picadas manuales del overlay local para este canal. */
        {
            CsnhypodbpManualPick mp[CSNHYPODBP_MANUAL_MAX];
            int nm = csnhypodbp_manual_list(mp, CSNHYPODBP_MANUAL_MAX);
            for (int k = 0; k < nm; k++) {
                if (strcmp(mp[k].sta, StaArray[i].szStation) != 0) continue;
                if (strcmp(mp[k].chan, StaArray[i].szChannel) != 0) continue;
                double rel = mp[k].t - winStart;
                if (rel < g_view.t0 || rel > g_view.t1) continue;
                double x_pos = csnhypodbp_view_rel_to_x(&g_view, rel, g_margin_left, draw_width);
                if (mp[k].phase == 'S')
                    cairo_set_source_rgb(cr, g_color_s[0], g_color_s[1], g_color_s[2]);
                else
                    cairo_set_source_rgb(cr, g_color_p[0], g_color_p[1], g_color_p[2]);
                cairo_set_line_width(cr, 1.5);
                cairo_move_to(cr, x_pos, y_center - (row_h / 2.5));
                cairo_line_to(cr, x_pos, y_center + (row_h / 2.5));
                cairo_stroke(cr);
                cairo_move_to(cr, x_pos + 4, y_center + (row_h / 3));
                char mlab[16];
                snprintf(mlab, sizeof(mlab), "%c(m)", mp[k].phase);
                cairo_show_text(cr, mlab);
            }
        }
    }

    /* Marca del ancla del zoom (1.er clic pendiente). */
    if (g_view.have_anchor) {
        int xa = (int)csnhypodbp_view_rel_to_x(&g_view, g_view.anchor, g_margin_left, draw_width);
        if (xa >= g_margin_left && xa <= width - margin_right) {
            cairo_set_source_rgb(cr, 0.9, 0.6, 0.1);
            cairo_set_line_width(cr, 1.5);
            const double dash_a[] = { 5.0, 3.0 };
            cairo_set_dash(cr, dash_a, 2, 0.0);
            cairo_move_to(cr, xa, 5);
            cairo_line_to(cr, xa, height - 5);
            cairo_stroke(cr);
            cairo_set_dash(cr, NULL, 0, 0.0);
        }
    }

    /* Indicador del modo picado: circulo del color de la fase con la letra
       blanca, siguiendo al puntero. `canvas != NULL` excluye la exportacion
       (ewgui_export_render llama al callback con canvas==NULL). */
    if (csnhypodbp_view_show_pick_cue(canvas != NULL,
                                      g_click_mode == CSNHYPODBP_CLICK_PICK,
                                      g_cursor_valid)) {
        const double *cc = (g_pick_phase == 'S') ? g_color_s : g_color_p;
        double cx = g_cursor_x + 16.0, cy = g_cursor_y - 16.0;
        char cl[2] = { g_pick_phase, '\0' };
        cairo_text_extents_t ce;
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, 13);
        cairo_text_extents(cr, cl, &ce);
        cairo_set_source_rgb(cr, cc[0], cc[1], cc[2]);
        cairo_arc(cr, cx, cy, 11.0, 0.0, 2.0 * M_PI);
        cairo_fill(cr);
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        cairo_move_to(cr, cx - (ce.width / 2.0 + ce.x_bearing),
                          cy - (ce.height / 2.0 + ce.y_bearing));
        cairo_show_text(cr, cl);
    }

    /* Mantiene el eje de tiempo en sincronia con cada repintado de trazas. */
    if (g_axis_canvas) ewgui_canvas_queue_draw(g_axis_canvas);
}

/* Arrastre de zoom sobre el eje de tiempo (estado + geometria comun). */
static CsnhypodbpAxisDrag g_axis_drag;
static double             g_axis_drag_sx = 0.0;  /* x del press, para el update */

/* Geometria comun del eje y de su arrastre: mismo ancho que el canvas de
   trazas (la barra de scroll puede restar ancho al canvas y no al eje). */
static void axis_geom(int fallback_width, int *right, int *draw_width) {
    int margin_right = 10;
    int cw = 0;
    ewgui_canvas_get_size(canvas_global, &cw, NULL);
    if (cw <= 0) cw = fallback_width;
    if (right) *right = cw - margin_right;
    if (draw_width) *draw_width = (cw - margin_right) - g_margin_left;
}

/* Eje de tiempo UTC absoluto bajo el canvas (no scrollea). */
void on_draw_axis(EwGuiCanvas *canvas, cairo_t *cr, int width, int height, void *data) {
    (void)canvas; (void)data;
    int right, draw_width;
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);
    axis_geom(width, &right, &draw_width);
    if (draw_width <= 0) return;

    /* Banda gris del arrastre de zoom (izq->der zoom, der->izq reset). */
    {
        double xa, xb;
        if (csnhypodbp_view_axis_band_x(&g_view, &g_axis_drag, g_margin_left,
                                        draw_width, &xa, &xb)) {
            cairo_set_source_rgba(cr, 0.5, 0.5, 0.5, 0.35);
            cairo_rectangle(cr, xa, 1.0, xb - xa, height - 2.0);
            cairo_fill(cr);
        }
    }

    cairo_set_source_rgb(cr, 0.4, 0.4, 0.4);
    cairo_set_line_width(cr, 1.0);
    cairo_move_to(cr, g_margin_left, 1);
    cairo_line_to(cr, right, 1);
    cairo_stroke(cr);

    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 10);
    cairo_set_source_rgb(cr, 0.35, 0.35, 0.6);
    cairo_move_to(cr, 8, height - 8);
    cairo_show_text(cr, "UTC");

    if (g_NumSortedNodes <= 0) return;
    {
        double win_start = g_win_start_cur;
        double t0 = win_start + g_view.t0;
        double t1 = win_start + g_view.t1;
        double ticks[128];
        int max_ticks = draw_width / 90;
        int n, i;
        if (max_ticks < 1) max_ticks = 1;
        n = csnhypodbp_view_ticks(t0, t1, max_ticks, ticks, 128);
        for (i = 0; i < n; i++) {
            double rel = ticks[i] - win_start;
            double x = csnhypodbp_view_rel_to_x(&g_view, rel, g_margin_left, draw_width);
            char b[16];
            cairo_text_extents_t ext;
            if (x < g_margin_left - 0.5 || x > right + 0.5) continue;
            cairo_set_source_rgb(cr, 0.4, 0.4, 0.4);
            cairo_move_to(cr, x, 1);
            cairo_line_to(cr, x, 7);
            cairo_stroke(cr);
            csnhypodbp_view_fmt_utc(ticks[i], b, sizeof(b));
            cairo_text_extents(cr, b, &ext);
            cairo_set_source_rgb(cr, 0.15, 0.15, 0.15);
            cairo_move_to(cr, x - ext.width / 2.0, height - 8);
            cairo_show_text(cr, b);
        }
    }
}

/* --- Arrastre de zoom sobre el eje de tiempo -------------------------- */

/* Tiempo relativo (a winStart) del x del eje, recortado al area dibujable. */
static double axis_rel_at(double x) {
    int right, draw_width;
    axis_geom(0, &right, &draw_width);
    if (draw_width <= 0) return g_view.t0;
    if (x < g_margin_left) x = g_margin_left;
    if (x > right) x = right;
    return csnhypodbp_view_x_to_rel(&g_view, x, g_margin_left, draw_width);
}

void on_axis_drag_begin(GtkGestureDrag *g, double sx, double sy, gpointer data) {
    int right, draw_width;
    (void)g; (void)sy; (void)data;
    g_axis_drag_sx = sx;
    axis_geom(0, &right, &draw_width);
    if (draw_width <= 0 || sx < g_margin_left || sx > right) {
        g_axis_drag.active = 0;   /* press fuera del area: ignorar */
        return;
    }
    csnhypodbp_view_drag_begin(&g_axis_drag, axis_rel_at(sx));
    logit("t", "csnhypodbp: eje drag begin x=%.0f\n", sx);
}

void on_axis_drag_update(GtkGestureDrag *g, double ox, double oy, gpointer data) {
    (void)g; (void)oy; (void)data;
    if (!g_axis_drag.active) return;
    csnhypodbp_view_drag_update(&g_axis_drag, axis_rel_at(g_axis_drag_sx + ox));
    if (g_axis_canvas) ewgui_canvas_queue_draw(g_axis_canvas);
}

void on_axis_drag_end(GtkGestureDrag *g, double ox, double oy, gpointer data) {
    CsnhypodbpAct act;
    (void)g; (void)oy; (void)data;
    if (!g_axis_drag.active) return;
    csnhypodbp_view_drag_update(&g_axis_drag, axis_rel_at(g_axis_drag_sx + ox));
    act = csnhypodbp_view_drag_end(&g_axis_drag, &g_view);
    logit("t", "csnhypodbp: eje drag end -> %s t=[%.3f..%.3f]\n",
          act == CSNHYPODBP_ACT_ZOOM ? "zoom" :
          (act == CSNHYPODBP_ACT_RESET ? "reset" : "ignore"),
          g_view.t0, g_view.t1);
    if (canvas_global) ewgui_canvas_queue_draw(canvas_global);
}
