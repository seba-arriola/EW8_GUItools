#include "csnhypodbp.h"
#include "csnhypodbp_statable.h"
#include <adwaita.h>

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

/* --- Tabla de estaciones (panel izquierdo) -------------------------------
 * Una fila por (estacion, fase) en un GtkColumnView (ver csnhypodbp_main.c),
 * ordenada por distancia. El dedup M>A>T y el residual los arma
 * `csnhypodbp_statable_build`; aqui solo se calculan las filas, se llena el
 * modelo y se guarda `g_sta_rows` (la entrada del localizador).
 * La distancia es 0 hasta que llegan datos (se re-ordenan en ws_fetch_finish).
 * ------------------------------------------------------------------------ */
typedef struct { char name[16]; double dist_km; int idx; } STA_LIST_ITEM;

void csnhypodbp_sta_sel_reset(void) {
    int i;
    for (i = 0; i < MAX_ESTA; i++) {
        g_pick_on[i][0] = 1;
        g_pick_on[i][1] = 1;
    }
}

/* Casilla del pick (una por fila = por (estacion, fase)): escribe g_pick_on[],
 * que es la unica fuente de verdad (la que lee el localizador). La celda la
 * relee en cada bind, porque GtkColumnView recicla las celdas al hacer scroll. */
void on_pick_check_toggled(GtkCheckButton *b, gpointer data) {
    int i = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(b), "sta_idx"));
    int ph = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(b), "phase"));
    (void)data;
    if (i >= 0 && i < MAX_ESTA && ph >= 0 && ph < 2)
        g_pick_on[i][ph] = gtk_check_button_get_active(b) ? 1 : 0;
}

/* Reune las picadas de `sta` en TODOS sus canales (el dedup es por estacion),
 * dentro de la ventana del evento. Devuelve cuantas. */
static int station_caps(const char *sta, CsnhypodbpTracePick *out, int max_out) {
    int count = 0;
    for (int i = 0; i < NumEstaciones && count < max_out; i++) {
        if (strcmp(StaArray[i].szStation, sta) != 0) continue;
        count += csnhypodbp_cap_window(&StaArray[i].pickcap, sta,
                                       selected_otime, g_pick_window_sec,
                                       out + count, max_out - count);
    }
    return count;
}

/* Solucion que aporta los residuales a la tabla de Ondas: la candidata de
   `locate` si esta vigente, si no la solucion elegida en el detalle (pestaña
   Eventos), que trae los residuales que el localizador ya calculo. */
static const CsnhypodbpSolution *resid_source(void)
{
    return csnhypodbp_resid_source(&g_preview_sol, g_preview_valid, &g_base_sol);
}

/* Fila de la tabla a partir de la estacion `i` (canal representativo). */
static CsnhypodbpStaRow build_row_for(int i, const char *name) {
    CsnhypodbpTracePick caps[CSNHYPODBP_CAP_MAX];
    CsnhypodbpManualPick man[CSNHYPODBP_MANUAL_MAX];
    CsnhypodbpStaInput si;
    CsnhypodbpStaRow row;
    int ncap, nman;

    memset(&row, 0, sizeof(row));
    memset(&si, 0, sizeof(si));
    ncap = station_caps(name, caps, CSNHYPODBP_CAP_MAX);
    nman = csnhypodbp_manual_list(man, CSNHYPODBP_MANUAL_MAX);
    snprintf(si.sta, sizeof(si.sta), "%s", name);
    si.idx = i;
    si.dist_deg = g_StaDist[i];
    si.has_data = (bHasData[i] == 1);
    si.caps = caps; si.ncap = ncap;
    si.sol = g_base_picks; si.nsol = g_base_npicks;
    si.theo_p = (g_StaTheoP[i] > 1.0) ? g_StaTheoP[i] : 0.0;
    /* SCNL de la estacion (metadata de StaFile): respaldo para las picadas que no
       traigan canal/red/loc (p.ej. las de una solucion cuyo ARC los perdio). */
    if (i >= 0 && i < NumEstaciones) {
        snprintf(si.chan, sizeof(si.chan), "%s", StaArray[i].szChannel);
        snprintf(si.net,  sizeof(si.net),  "%s", StaArray[i].szNetID);
        snprintf(si.loc,  sizeof(si.loc),  "%s", StaArray[i].szLocation);
    }
    csnhypodbp_statable_build(&row, 1, &si, 1, man, nman, resid_source());
    return row;
}

void rebuild_station_list(void) {
    if (!g_sta_store) return;
    char sel[16];
    /* Guarda la seleccion ANTES de vaciar: g_list_store_remove_all invalida la
       posicion seleccionada y emite notify::selected, y on_station_selected
       borraria g_selected_station; como el canvas dibuja solo la estacion
       seleccionada, perderla dejaria el canvas vacio. */
    snprintf(sel, sizeof(sel), "%s", g_selected_station);
    g_rebuilding_stations = 1;
    g_list_store_remove_all(g_sta_store);

    static STA_LIST_ITEM items[MAX_ESTA];
    int n = 0, a, b, to_select = -1;

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
        items[n].idx = i;
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

    g_sta_rows_n = 0;
    for (a = 0; a < n; a++) {
        CsnhypodbpStaRow srow = build_row_for(items[a].idx, items[a].name);
        CsnhypodbpStaItem *it;
        if (g_sta_rows_n < MAX_ESTA) g_sta_rows[g_sta_rows_n++] = srow;
        /* Dos filas por estacion: P (0) y S (1); cada una con su casilla, que es
           la del pick de esa fase (estado en g_pick_on[], releido en el bind). */
        it = csnhypodbp_sta_item_new(&srow, 0);
        g_list_store_append(g_sta_store, it);
        g_object_unref(it);
        it = csnhypodbp_sta_item_new(&srow, 1);
        g_list_store_append(g_sta_store, it);
        g_object_unref(it);
    }
    g_rebuilding_stations = 0;
    /* La seleccion se restaura por (estacion, fase), no por posicion: la tabla
       puede estar ordenada por cualquier columna. */
    if (sel[0]) {
        guint i, nit = g_list_model_get_n_items(G_LIST_MODEL(g_sort_sta));
        for (i = 0; i < nit; i++) {
            gpointer it = g_list_model_get_item(G_LIST_MODEL(g_sort_sta), i);
            int hit = 0;
            if (CSNHYPODBP_IS_STA_ITEM(it)) {
                const char *sta = NULL;
                int ph = -1;
                g_object_get(it, "sta", &sta, "phase", &ph, NULL);
                hit = (sta && ph == 0 && strcmp(sta, sel) == 0);
            }
            g_object_unref(it);
            if (hit) { to_select = (int)i; break; }
        }
    }
    if (to_select >= 0)
        gtk_selection_model_select_item(GTK_SELECTION_MODEL(g_sta_sel), (guint)to_select, FALSE);
    /* Si la estacion ya no esta (filtro dinamico), NO se borra la seleccion:
       el canvas la sigue dibujando y reaparece si el filtro la readmite. */
}

void on_station_selected(GtkSingleSelection *sel, GParamSpec *pspec, gpointer data) {
    (void)pspec; (void)data;
    char prev[16];
    gpointer item;
    char *sta = NULL;
    /* Durante el repoblado (vaciado -> selected invalido) no se pierde la
       seleccion ni se reinicia el zoom. */
    if (g_rebuilding_stations) return;
    snprintf(prev, sizeof(prev), "%s", g_selected_station);
    item = gtk_single_selection_get_selected_item(sel);
    if (item && CSNHYPODBP_IS_STA_ITEM(item)) {
        g_object_get(item, "sta", &sta, NULL);
        if (sta && sta[0]) {
            snprintf(g_selected_station, sizeof(g_selected_station), "%s", sta);
            EnsureAndFetchStation(g_selected_station);
        }
    } else {
        g_selected_station[0] = '\0';
    }
    g_free(sta);
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

/* Aviso modal simple (solo OK) para errores/avisos de la busqueda. */
static void search_info(const char *title, const char *msg) {
    GtkWidget *lbl = gtk_label_new(msg);
    GtkWindow *win = NULL;
    gtk_label_set_wrap(GTK_LABEL(lbl), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(lbl), 48);
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0);
    ewdialog_run(window_global ? GTK_WINDOW(window_global) : NULL, title, lbl, "OK", &win);
    if (win) gtk_window_destroy(win);
}

/* --- Selector de fecha: VENTANA MODAL propia con GtkCalendar -----------------
 * Nada de popups: bajo WSLg/Wayland quedan detras de la ventana (bug #1299).
 * La fecha se lee en el handler de "Accept", con el calendario todavia vivo:
 * leerla despues de cerrar el dialogo (o depender de una señal) no es fiable. */
typedef struct {
    GtkWidget *cal;
    GMainLoop *loop;
    int        accepted;
    int        y, m, d;
} CalPick;

static void cal_finish(CalPick *cp)
{
    if (cp->accepted) {
        GDateTime *sel = gtk_calendar_get_date(GTK_CALENDAR(cp->cal));
        if (sel) {
            cp->y = g_date_time_get_year(sel);
            cp->m = g_date_time_get_month(sel);
            cp->d = g_date_time_get_day_of_month(sel);
            g_date_time_unref(sel);
        }
    }
    if (cp->loop) g_main_loop_quit(cp->loop);
}

static void on_cal_ok(GtkButton *b, gpointer ud)
{
    CalPick *cp = ud;
    (void)b;
    cp->accepted = 1;
    cal_finish(cp);
}

static void on_cal_cancel(GtkButton *b, gpointer ud)
{
    CalPick *cp = ud;
    (void)b;
    cp->accepted = 0;
    cal_finish(cp);
}

static int calendar_pick(GtkWindow *parent, int *y, int *m, int *d)
{
    CalPick cp;
    GtkWidget *win, *box, *bb, *okb, *cb;
    int ok;

    memset(&cp, 0, sizeof(cp));
    cp.y = *y; cp.m = *m; cp.d = *d;
    cp.cal = gtk_calendar_new();
    gtk_calendar_set_year(GTK_CALENDAR(cp.cal), cp.y);
    gtk_calendar_set_month(GTK_CALENDAR(cp.cal), cp.m);
    gtk_calendar_set_day(GTK_CALENDAR(cp.cal), cp.d);

    win = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(win), "Pick date");
    gtk_window_set_modal(GTK_WINDOW(win), TRUE);
    gtk_window_set_resizable(GTK_WINDOW(win), FALSE);
    if (parent) gtk_window_set_transient_for(GTK_WINDOW(win), parent);
    box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_margin_top(box, 10);
    gtk_widget_set_margin_bottom(box, 10);
    gtk_widget_set_margin_start(box, 10);
    gtk_widget_set_margin_end(box, 10);
    gtk_box_append(GTK_BOX(box), cp.cal);
    bb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_halign(bb, GTK_ALIGN_END);
    cb = gtk_button_new_with_label("Cancel");
    okb = gtk_button_new_with_label("Accept");
    g_signal_connect(cb, "clicked", G_CALLBACK(on_cal_cancel), &cp);
    g_signal_connect(okb, "clicked", G_CALLBACK(on_cal_ok), &cp);
    gtk_box_append(GTK_BOX(bb), cb);
    gtk_box_append(GTK_BOX(bb), okb);
    gtk_box_append(GTK_BOX(box), bb);
    gtk_window_set_child(GTK_WINDOW(win), box);

    cp.loop = g_main_loop_new(NULL, FALSE);
    gtk_window_present(GTK_WINDOW(win));
    g_main_loop_run(cp.loop);
    g_main_loop_unref(cp.loop);
    gtk_window_destroy(GTK_WINDOW(win));

    ok = cp.accepted;
    if (ok) { *y = cp.y; *m = cp.m; *d = cp.d; }
    return ok;
}

/* Lee "YYYY-MM-DD[ HH:MM:SS]": 1 si la fecha es valida. */
static int date_from_entry(const char *s, int *y, int *m, int *d)
{
    if (!s || strlen(s) < 10) return 0;
    if (sscanf(s, "%4d-%2d-%2d", y, m, d) != 3) return 0;
    return (s[4] == '-' && s[7] == '-' && *y >= 1900 && *y <= 2999 &&
            *m >= 1 && *m <= 12 && *d >= 1 && *d <= 31);
}

static void on_calendar_clicked(GtkButton *btn, gpointer data)
{
    GtkWidget *entry = g_object_get_data(G_OBJECT(btn), "cal-entry");
    GtkRoot *root = gtk_widget_get_root(GTK_WIDGET(btn));
    int y, m, d;
    (void)data;
    if (!entry) return;
    if (!date_from_entry(gtk_editable_get_text(GTK_EDITABLE(entry)), &y, &m, &d)) {
        time_t now = time(NULL);
        struct tm *tm = gmtime(&now);
        y = tm ? tm->tm_year + 1900 : 2000;
        m = tm ? tm->tm_mon + 1 : 1;
        d = tm ? tm->tm_mday : 1;
    }
    if (!calendar_pick(root ? GTK_WINDOW(root) : NULL, &y, &m, &d)) return;
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "%s", gtk_editable_get_text(GTK_EDITABLE(entry)));
        csnhypodbp_search_set_date(buf, sizeof(buf), y, m, d);
        gtk_editable_set_text(GTK_EDITABLE(entry), buf);
    }
}

/* Busqueda hipocentral por rangos (menu ☰, pestaña Eventos). */
void on_btn_search_clicked(GtkWidget *w, gpointer d) {
    (void)w; (void)d;
    GtkWidget *grid = gtk_grid_new(), *e[CSNHYPODBP_SEARCH_FIELDS], *win = NULL;
    static const char *labels[5] = {"Origin time", "Latitude", "Longitude", "Depth", "Magnitude"};
    int i;

    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    /* Columnas: 0 = etiqueta, 1 = Min, 2 = calendario de Min, 3 = Max,
       4 = calendario de Max: cada boton pegado a su caja. */
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Min"), 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Max"), 3, 0, 1, 1);
    for (i = 0; i < 5; i++) {
        GtkWidget *l = gtk_label_new(labels[i]);
        gtk_label_set_xalign(GTK_LABEL(l), 0.0);
        gtk_grid_attach(GTK_GRID(grid), l, 0, i + 1, 1, 1);
        e[2 * i]     = gtk_entry_new();
        e[2 * i + 1] = gtk_entry_new();
        gtk_grid_attach(GTK_GRID(grid), e[2 * i],     1, i + 1, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), e[2 * i + 1], 3, i + 1, 1, 1);
    }
    /* Los campos de hora de origen llevan fecha Y hora completas: fuente algo
       mas chica y ancho minimo para que entren enteras en la caja. */
    gtk_widget_add_css_class(e[0], "time-entry");
    gtk_widget_add_css_class(e[1], "time-entry");
    /* Boton de calendario junto a cada campo de hora de origen (min y max). */
    for (i = 0; i < 2; i++) {
        GtkWidget *btn = gtk_button_new_with_label("...");
        gtk_widget_set_tooltip_text(btn, "Pick the date in a calendar");
        gtk_accessible_update_property(GTK_ACCESSIBLE(btn),
                                       GTK_ACCESSIBLE_PROPERTY_LABEL,
                                       i == 0 ? "calendar-min" : "calendar-max", -1);
        g_object_set_data(G_OBJECT(btn), "cal-entry", e[i]);
        g_signal_connect(btn, "clicked", G_CALLBACK(on_calendar_clicked), NULL);
        gtk_grid_attach(GTK_GRID(grid), btn, 2 + 2 * i, 1, 1, 1);
    }
    {
        GtkWidget *note = gtk_label_new("Only the origin time range is required.\n"
                                        "Format: YYYY-MM-DD[ HH:MM:SS] (UTC)");
        gtk_label_set_xalign(GTK_LABEL(note), 0.0);
        gtk_grid_attach(GTK_GRID(grid), note, 0, 6, 5, 1);
    }

    if (ewdialog_run(window_global ? GTK_WINDOW(window_global) : NULL,
                     "Search events", grid, "Search", &win)) {
        const char *fields[CSNHYPODBP_SEARCH_FIELDS];
        CsnhypodbpSearch s;
        char err[128];
        int n;
        for (i = 0; i < CSNHYPODBP_SEARCH_FIELDS; i++)
            fields[i] = gtk_editable_get_text(GTK_EDITABLE(e[i]));
        if (!csnhypodbp_search_build(fields, g_search_max_events, &s, err, sizeof err)) {
            search_info("Search events", err);
        } else if (!csnhypodbp_db_ready()) {
            search_info("Search events",
                        "Base de datos no configurada.\n"
                        "Falta DbEnvFile (db.env) o no se pudo conectar; "
                        "revisa el log de csnhypodbp.");
        } else if ((n = search_apply(&s)) < 0) {
            search_info("Search events",
                        "No se pudo consultar la base de datos. Revisa el log de csnhypodbp.");
        } else if (n == 0) {
            search_info("Search events", "0 events found.");
        }
    }
    if (win) gtk_window_destroy(win);
}

/* Boton "Clear search" (junto al titulo "Events"): vuelve a vivo+historial. */
void on_btn_clear_search_clicked(GtkWidget *w, gpointer d) {
    (void)w; (void)d;
    search_clear();
}

void on_btn_cancel_clicked(GtkWidget *w, gpointer d) {    (void)w; (void)d;
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
    csnhypodbp_update_locator_label();
}

/* --- Filtro de picadas (A/M/Teo) de la barra inferior --- */
void on_filter_toggled(GtkCheckButton *b, gpointer data) {
    int which = GPOINTER_TO_INT(data);
    int on = gtk_check_button_get_active(b) ? 1 : 0;
    if (which == 0)      g_filter_auto = on;
    else if (which == 1) g_filter_manual = on;
    else                 g_filter_theo = on;
}

/* Caja con el nombre del localizador activo. */
void csnhypodbp_update_locator_label(void) {
    char b[64];
    if (!g_locator_label) return;
    snprintf(b, sizeof(b), "Locator: %s", g_active_locator[0] ? g_active_locator : "(none)");
    gtk_label_set_text(GTK_LABEL(g_locator_label), b);
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
        csnhypodbp_preview_update();
        rebuild_station_list();   /* residual de la candidata en la tabla */
        logit("t", "csnhypodbp: locate OK [%s] nph=%d rms=%.2f ML=%s Mwp=%s\n",
              job->locator, job->out.nph, job->out.rms,
              job->mag.ml[0] ? job->mag.ml : "-", job->mag.mwp[0] ? job->mag.mwp : "-");
    } else {
        logit("e", "csnhypodbp: locate fallo [%s]: %s\n", job->locator, job->err);
    }
    g_free(job);
    return G_SOURCE_REMOVE;
}

/* Unico punto que escribe la tablita del preview, para que estado, valores,
   color y sensibilidad de los botones no se puedan desincronizar: la candidata
   del `locate` si esta vigente, si no la solucion elegida en el detalle
   (pestana Eventos), si no guiones. */
void csnhypodbp_preview_update(void) {
    CsnhypodbpPreviewVals v;
    int i;
    if (!g_preview_box || !g_preview_title) return;
    if (g_preview_valid) {
        csnhypodbp_preview_from_candidate(&v, g_active_locator,
                                          g_preview_sol.lat, g_preview_sol.lon,
                                          g_preview_sol.depth, g_preview_sol.nph,
                                          g_preview_sol.rms,
                                          g_preview_mag.ml, g_preview_mag.mwp);
    } else {
        gpointer item = g_selection_detail
                        ? gtk_single_selection_get_selected_item(g_selection_detail)
                        : NULL;
        if (item) {
            CsnhypodbpRow *row = CSNHYPODBP_ROW(item);
            double lat = 0, lon = 0, depth = 0;
            g_object_get(row, "lat", &lat, "lon", &lon, "depth", &depth, NULL);
            /* rms/nph/mags: las celdas ya formateadas de la fila (c5/c7/c9/c10). */
            csnhypodbp_preview_from_selected(&v, lat, lon, depth,
                                             csnhypodbp_row_col(row, 5),
                                             csnhypodbp_row_col(row, 7),
                                             csnhypodbp_row_col(row, 9),
                                             csnhypodbp_row_col(row, 10));
        } else {
            csnhypodbp_preview_clear(&v);
        }
    }
    gtk_label_set_text(GTK_LABEL(g_preview_title), v.title);
    for (i = 0; i < CSNHYPODBP_PREVIEW_NVALS; i++)
        gtk_label_set_text(GTK_LABEL(g_preview_val[i]), v.val[i]);
    if (v.candidate) gtk_widget_add_css_class(g_preview_box, "candidate");
    else             gtk_widget_remove_css_class(g_preview_box, "candidate");
    if (g_preview_btn_confirm)
        gtk_widget_set_sensitive(g_preview_btn_confirm, v.candidate);
    if (g_preview_btn_discard)
        gtk_widget_set_sensitive(g_preview_btn_discard, v.candidate);
}

void on_preview_discard(GtkWidget *w, gpointer d) {
    (void)w; (void)d;
    g_preview_valid = FALSE;
    csnhypodbp_preview_update();   /* vuelve a mostrar la solucion elegida */
    rebuild_station_list();        /* quita el residual de la candidata */
}

void on_preview_confirm(GtkWidget *w, gpointer d) {
    (void)w; (void)d;
    if (!g_preview_valid) return;
    csnhypodbp_confirm_manual(&g_preview_sol, &g_preview_mag);
    g_preview_valid = FALSE;
    csnhypodbp_preview_update();
    csnhypodbp_manual_clear();
    rebuild_station_list();
    if (canvas_global) ewgui_canvas_queue_draw(canvas_global);
}

void on_btn_locate_clicked(GtkWidget *w, gpointer d) {
    LocateJob *job;
    GThread *th;
    int i;
    (void)w; (void)d;
    if (selected_qid == 0) { logit("t", "csnhypodbp: locate sin evento seleccionado\n"); return; }
    if (g_nlocators <= 0) { logit("t", "csnhypodbp: locate sin localizadores en el .d\n"); return; }
    job = g_new0(LocateJob, 1);
    job->base.qid = selected_qid; job->base.version = (unsigned)selected_version; job->base.mod = selected_mod;
    job->base.otime = selected_otime; job->base.lat = selected_lat;
    job->base.lon = selected_lon; job->base.depth = selected_depth;
    /* Picadas = las filas de la tabla (una P y una S por estacion), filtradas
       por la seleccion de estaciones/picks y por el origen A/M/Teo. */
    {
        CsnhypodbpPickFilter filter;
        filter.auto_on = g_filter_auto;
        filter.manual_on = g_filter_manual;
        filter.theo_on = g_filter_theo;
        job->base.npicks = csnhypodbp_locate_picks(g_sta_rows, g_sta_rows_n,
                                                   (const int (*)[2])g_pick_on,
                                                   &filter, job->base.picks,
                                                   CSNHYPODBP_MAX_PICKS);
    }
    job->base.nph = job->base.npicks;
    /* Red/canal/loc: si la solucion base los perdio (p.ej. un ARC refinado por
       NLLoc, que escribe la red en blanco y parte el canal), se completan con la
       metadata de la estacion. Sin COMP3, HYPOINVERSE empareja
       STA+NET+COMP3+LOC y rechaza TODAS las fases. */
    for (i = 0; i < job->base.npicks; i++) {
        CsnhypodbpPick *pk = &job->base.picks[i];
        int k;
        if (pk->chan[0] && pk->net[0]) continue;
        for (k = 0; k < NumEstaciones; k++) {
            if (strcmp(StaArray[k].szStation, pk->sta) != 0) continue;
            if (!pk->chan[0]) snprintf(pk->chan, sizeof(pk->chan), "%s", StaArray[k].szChannel);
            if (!pk->net[0])  snprintf(pk->net,  sizeof(pk->net),  "%s", StaArray[k].szNetID);
            if (!pk->loc[0])  snprintf(pk->loc,  sizeof(pk->loc),  "%s", StaArray[k].szLocation);
            break;
        }
    }
    job->nmp = 0;   /* el overlay manual ya va incluido en las filas de la tabla */
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

/* --- Ayuda (grupo "Help" del menu ☰): lista de comandos en ventana modal --- */
void csnhypodbp_help_dialog_run(GtkWindow *parent) {
    GtkWidget *win = gtk_window_new();
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    GtkWidget *lbl = gtk_label_new(NULL);
    GtkWidget *btn = gtk_button_new_with_label("Close");

    static const char *help =
        "<b>ZOOM (modo por defecto)</b>\n"
        "  P / S            elegir fase a picar (entra en modo PICAR)\n"
        "  clic izq         1.º clic: ancla; 2.º clic der: zoom / izq: reset\n"
        "  clic der         quitar el pick de la traza\n"
        "  eje (arrastrar)  zoom al rango / reset\n"
        "  Shift+← / →      contraer / expandir tiempo\n"
        "  Shift+↑ / ↓      escala vertical\n"
        "\n"
        "<b>PICAR (tras pulsar P o S)</b>\n"
        "  clic izq         coloca la fase en la traza y vuelve a ZOOM\n"
        "  Esc              cancelar";

    gtk_window_set_title(GTK_WINDOW(win), "Commands");
    if (parent) gtk_window_set_transient_for(GTK_WINDOW(win), parent);
    gtk_window_set_modal(GTK_WINDOW(win), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(win), 480, -1);

    gtk_widget_set_margin_start(box, 14);
    gtk_widget_set_margin_end(box, 14);
    gtk_widget_set_margin_top(box, 14);
    gtk_widget_set_margin_bottom(box, 14);
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0);
    gtk_label_set_justify(GTK_LABEL(lbl), GTK_JUSTIFY_LEFT);
    gtk_label_set_selectable(GTK_LABEL(lbl), TRUE);
    gtk_label_set_markup(GTK_LABEL(lbl), help);
    gtk_widget_set_hexpand(lbl, TRUE);
    gtk_box_append(GTK_BOX(box), lbl);

    gtk_widget_set_halign(btn, GTK_ALIGN_END);
    g_signal_connect_swapped(btn, "clicked", G_CALLBACK(gtk_window_destroy), win);
    gtk_box_append(GTK_BOX(box), btn);

    gtk_window_set_child(GTK_WINDOW(win), box);
    gtk_window_present(GTK_WINDOW(win));
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
        if (canvas_global) ewgui_canvas_queue_draw(canvas_global);
        return TRUE;
    }
    if (keyval == GDK_KEY_p || keyval == GDK_KEY_P) {
        g_pick_phase = 'P'; g_click_mode = CSNHYPODBP_CLICK_PICK;
        if (canvas_global) ewgui_canvas_queue_draw(canvas_global);
        return TRUE;
    }
    if (keyval == GDK_KEY_s || keyval == GDK_KEY_S) {
        g_pick_phase = 'S'; g_click_mode = CSNHYPODBP_CLICK_PICK;
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
    g_base_npicks = 0;
    memset(&g_base_sol, 0, sizeof(g_base_sol));
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
            /* Picadas de la solucion elegida: candidatas A/M de la tabla. */
            g_base_npicks = csnhypodbp_event_group_picks(csnhypodbp_event_group_of(row),
                                                          g_base_picks, CSNHYPODBP_MAX_PICKS);
            /* Residuales de ESTA solucion: lo que la tabla de Ondas muestra en
               `res` sin necesidad de pulsar locate. */
            g_base_sol.npicks = csnhypodbp_event_sol_picks(row, g_base_sol.picks,
                                                           CSNHYPODBP_MAX_PICKS);
            if (changed) {
                g_zoom_factor = 1.0;
                csnhypodbp_view_set_default(&g_view, g_dScreenTime);
            }
        }
    }
    csnhypodbp_preview_update();   /* la tablita sigue a la solucion elegida */
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

/* Nota: el clic derecho sobre una estacion vive ahora en las celdas de la tabla
   (on_sta_cell_right_click, csnhypodbp_main.c), que es quien conoce la fila. */

/* Nota: el inicio de ventana no es por fila sino COMUN a la estacion
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

    /* Una P y una S por estacion (M > A > T), con el overlay manual incluido.
       `g_SortedNodes` son canales de UNA estacion (la seleccionada), asi que la
       fila se calcula una sola vez y vale para todos sus canales. */
    CsnhypodbpStaRow sta_row;
    int have_row = 0;
    memset(&sta_row, 0, sizeof(sta_row));
    if (g_NumSortedNodes > 0) {
        int i0 = g_SortedNodes[0].idx;
        CsnhypodbpTracePick wc[CSNHYPODBP_CAP_MAX];
        CsnhypodbpManualPick man[CSNHYPODBP_MANUAL_MAX];
        CsnhypodbpStaInput si;
        int nwc, nman;
        memset(&si, 0, sizeof(si));
        nwc = station_caps(StaArray[i0].szStation, wc, CSNHYPODBP_CAP_MAX);
        nman = csnhypodbp_manual_list(man, CSNHYPODBP_MANUAL_MAX);
        snprintf(si.sta, sizeof(si.sta), "%s", StaArray[i0].szStation);
        si.dist_deg = g_StaDist[i0];
        si.has_data = (bHasData[i0] == 1);
        si.caps = wc; si.ncap = nwc;
        si.theo_p = (g_StaTheoP[i0] > 1.0) ? g_StaTheoP[i0] : 0.0;
        have_row = csnhypodbp_statable_build(&sta_row, 1, &si, 1, man, nman,
                                             resid_source()) == 1;
    }

    /* Marca P de referencia -> ventana global (todas las P alineadas). */
    double earliest_pick = 1e15;
    if (have_row && sta_row.p.present && sta_row.p.t > 1.0) earliest_pick = sta_row.p.t;
    if (earliest_pick == 1e15) earliest_pick = selected_otime + 15.0;
    g_dWindowStart = earliest_pick - g_align_lead;

    /* Ventana visible (zoom) y winStart COMUN de la estacion (para el eje UTC).
       La P de referencia es la leida (real o manual) si hay; si no la teorica.
       Asi TODAS las trazas comparten ventana y un unico eje UTC vale para
       todas. */
    double span = g_view.t1 - g_view.t0;
    if (span <= 0.0) span = g_dScreenTime;
    double sta_pick_t = selected_otime;
    int    sta_is_real = 0;
    if (have_row && sta_row.p.present) {
        sta_pick_t = sta_row.p.t;
        sta_is_real = (sta_row.p.origin == 'M' || sta_row.p.origin == 'A');
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
        double winStart = sta_win_start;

        /* (sin LED verde/rojo: era ambiguo y ya no se usa) */
        cairo_new_path(cr);
        cairo_set_line_width(cr, 1.0);

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

        /* Marca P (una sola por estacion: M > A > T). */
        if (have_row && sta_row.p.present) {
            double rel = sta_row.p.t - winStart;
            if (rel >= g_view.t0 && rel <= g_view.t1) {
                double xp = csnhypodbp_view_rel_to_x(&g_view, rel, g_margin_left, draw_width);
                cairo_set_source_rgb(cr, g_color_p[0], g_color_p[1], g_color_p[2]);
                cairo_set_line_width(cr, 2);
                cairo_move_to(cr, xp, y_center - (row_h / 2.5));
                cairo_line_to(cr, xp, y_center + (row_h / 2.5));
                cairo_stroke(cr);
                cairo_set_font_size(cr, 11);
                cairo_move_to(cr, xp + 4, y_center - (row_h / 3));
                char plabel[16];
                if (sta_row.p.origin == 'T') snprintf(plabel, sizeof(plabel), "P(theo)");
                else                         snprintf(plabel, sizeof(plabel), "P(%c)", sta_row.p.origin);
                cairo_show_text(cr, plabel);
            }
        }

        /* Marca S (una sola por estacion: M > A). */
        if (have_row && sta_row.s.present) {
            double rel = sta_row.s.t - winStart;
            if (rel >= g_view.t0 && rel <= g_view.t1) {
                double xs = csnhypodbp_view_rel_to_x(&g_view, rel, g_margin_left, draw_width);
                cairo_set_source_rgb(cr, g_color_s[0], g_color_s[1], g_color_s[2]);
                cairo_set_line_width(cr, 1.5);
                cairo_move_to(cr, xs, y_center - (row_h / 2.5));
                cairo_line_to(cr, xs, y_center + (row_h / 2.5));
                cairo_stroke(cr);
                cairo_set_font_size(cr, 11);
                char slab[16];
                snprintf(slab, sizeof(slab), "S(%c)", sta_row.s.origin);
                cairo_move_to(cr, xs + 4, y_center + (row_h / 3));
                cairo_show_text(cr, slab);
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
