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
    /* MINIMO, no altura fija: el canvas llena el area que le da el scrolled
       window y on_draw_signal reparte el alto REAL entre las bandas, asi que el
       dibujo es proporcional al tamano de la ventana. Por debajo del minimo
       (muchas componentes), aparece el scroll. */
    if (count > 0)
        gtk_widget_set_size_request(ewgui_canvas_widget(canvas_global), -1,
                                    (count * CSNHYPODBP_BAND_MIN) + 60);
    else
        gtk_widget_set_size_request(ewgui_canvas_widget(canvas_global), -1,
                                    CSNHYPODBP_BAND_MIN + 60);
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
    csnhypodbp_mag_views_invalidate();   /* puede haber trazas nuevas */
    rebuild_mag_list();   /* la tabla de magnitudes usa la MISMA lista */
}

/* --- Sub-pestaña `magnitudes` ------------------------------------------- */

/* Config de magnitud con el override de distancia de la magnitud activa (se
   define mas abajo, junto a la barra inferior). */
static void mag_effective_cfg(CsnhypodbpMagCfg *out);

/* Linea de estado: magnitud activa, valor de red, estaciones con traza, con dato
   y cuantas estan marcadas. Dice explicitamente cual de las tres cosas falta
   (trazas, valores de csnmags o la magnitud de red) en vez de dejar la tabla
   llena de guiones sin explicacion. */
void csnhypodbp_mag_status_update(void)
{
    char txt[256], tail[96] = "", gate[48] = "";
    int con = 0, fuera = 0, filtradas = 0, enuso = 0, traza = 0, i;
    double red = NAN;
    CsnhypodbpMagCfg eff;
    const char *name = csnhypodbp_magcfg_name(g_mag_kind);

    if (!g_mag_status) return;
    csnhypodbp_magrow_counts(g_mag_rows, g_mag_rows_n, &con, &fuera, &filtradas, &enuso);
    for (i = 0; i < g_mag_rows_n; i++) {
        if (g_mag_rows[i].has_data) traza++;
        /* El residual de la fila es mag_estacion - mag_red, asi que de ahi se
           recupera la de red (el residual de la magnitud va en
           magnitude.mag_uncertainty al publicarla). */
        if (!isfinite(red) && isfinite(g_mag_rows[i].res))
            red = g_mag_rows[i].mag - g_mag_rows[i].res;
    }
    mag_effective_cfg(&eff);
    csnhypodbp_magcfg_gate_text(&eff, g_mag_kind, gate, sizeof(gate));
    /* Corte de profundidad: es del EVENTO, no de la estacion. Si no pasa, la
       magnitud entera queda fuera y se dice por que. */
    if (!csnhypodbp_magcfg_depth_ok(&eff, g_mag_kind, selected_depth)) {
        snprintf(txt, sizeof(txt),
                 "%s  fuera por profundidad: %.0f km no entra en [%.0f, %.0f] "
                 "(ajusta `prof.`)",
                 name, selected_depth, csnhypodbp_magcfg_min_depth(&eff, g_mag_kind),
                 csnhypodbp_magcfg_max_depth(&eff, g_mag_kind));
        gtk_label_set_text(GTK_LABEL(g_mag_status), txt);
        return;
    }
    if (fuera > 0)
        snprintf(tail, sizeof(tail), ", %d fuera de %s", fuera, gate);
    if (g_mag_rows_n <= 0)
        snprintf(txt, sizeof(txt), "%s  sin solucion seleccionada", name);
    else if (traza == 0)
        snprintf(txt, sizeof(txt),
                 "%s  sin trazas descargadas (Fetch o doble clic en la solucion)", name);
    else if (fuera == g_mag_rows_n)
        snprintf(txt, sizeof(txt),
                 "%s  %d estaciones, ninguna dentro de la puerta (%s)", name, fuera, gate);
    else if (con == 0)
        snprintf(txt, sizeof(txt),
                 "%s  sin valores de csnmags  (%d con traza, %d filtradas por ventana%s)",
                 name, traza, filtradas, tail);
    else if (!isfinite(red))
        snprintf(txt, sizeof(txt),
                 "%s  sin magnitud de red  (%d con dato, %d en uso%s)",
                 name, con, enuso, tail);
    else
        snprintf(txt, sizeof(txt), "%s  red %.2f  (%d con dato, %d en uso%s)",
                 name, red, con, enuso, tail);
    gtk_label_set_text(GTK_LABEL(g_mag_status), txt);
}

/* Repuebla la tabla: una fila por estacion (la MISMA lista que la de picking),
   con el valor de la magnitud activa y el residual contra la de red. */
void rebuild_mag_list(void)
{
    static CsnhypodbpMagIn vals[CSNHYPODBP_MAGSTA_MAX];
    gpointer item;
    int nval = 0, i;
    double red = NAN, t0 = NAN, t1 = NAN;
    CsnhypodbpMagCfg eff;

    if (!g_mag_store) return;
    g_list_store_remove_all(g_mag_store);
    g_mag_rows_n = 0;

    item = g_selection_detail ? gtk_single_selection_get_selected_item(g_selection_detail)
                              : NULL;
    if (item && CSNHYPODBP_IS_ROW(item)) {
        char prop[8];
        double v = -1.0;
        CsnhypodbpRow *row = CSNHYPODBP_ROW(item);
        snprintf(prop, sizeof(prop), "nmag%d", g_mag_kind);
        g_object_get(row, prop, &v, NULL);
        if (v >= 0.0) red = v;
        nval = csnhypodbp_magsta_for(row, g_mag_kind, vals, CSNHYPODBP_MAGSTA_MAX);
    }
    /* La ventana por defecto de la magnitud activa (la del .d de csnmags) es la
       que se dibuja en la traza y la que se ajusta despues por estacion. */
    csnhypodbp_magcfg_window(&g_magcfg, g_mag_kind, &t0, &t1);
    if (g_sta_rows_n > 0)
        g_mag_rows_n = csnhypodbp_magrow_build(g_mag_rows, MAX_ESTA, g_sta_rows,
                                               g_sta_rows_n, vals, nval, red,
                                               t0, t1);
    /* Ventana vigente de cada fila (la ajustada por (estacion, magnitud) si
       existe, si no la default que ya trae el builder), de donde sale esa ventana
       (M/A/Teo), si la estacion cae dentro de la puerta de distancia de la
       magnitud activa y si el filtro A/M/Teo la admite. */
    mag_effective_cfg(&eff);
    for (i = 0; i < g_mag_rows_n; i++) {
        csnhypodbp_magmarks_window(&g_mag_marks, g_mag_rows[i].sta, g_mag_kind,
                                   t0, t1, &g_mag_rows[i].t0, &g_mag_rows[i].t1);
        g_mag_rows[i].win_src = csnhypodbp_magmarks_winsrc(&g_mag_marks,
                                                           g_mag_rows[i].sta,
                                                           g_mag_kind,
                                                           g_mag_rows[i].anchor);
        csnhypodbp_magcfg_gate(&eff, g_mag_kind, g_mag_rows[i].dist_deg,
                               g_mag_rows[i].dist_km, &g_mag_rows[i].in_range);
        g_mag_rows[i].eligible = (g_mag_rows[i].has_data && g_mag_rows[i].in_range &&
                                  csnhypodbp_magmarks_allowed(&g_mag_marks,
                                                              g_mag_rows[i].sta,
                                                              g_mag_kind,
                                                              g_mag_rows[i].anchor,
                                                              g_magf_auto, g_magf_manual,
                                                              g_magf_theo)) ? 1 : 0;
        g_mag_rows[i].used = g_mag_rows[i].eligible
                             ? csnhypodbp_magmarks_used(&g_mag_marks, g_mag_rows[i].sta,
                                                        g_mag_kind, 1)
                             : 0;
    }
    /* Mapa fila de magnitudes -> fila de la tabla de Ondas (lo usa el envio al
       localizador desde la pestana de magnitudes). */
    for (i = 0; i < g_mag_rows_n && i < MAX_ESTA; i++) {
        int k;

        g_mag_sta_idx[i] = -1;
        for (k = 0; k < g_sta_rows_n; k++) {
            if (strcmp(g_sta_rows[k].sta, g_mag_rows[i].sta) == 0) {
                g_mag_sta_idx[i] = k;
                break;
            }
        }
    }
    for (i = 0; i < g_mag_rows_n; i++) {
        CsnhypodbpMagItem *it;

        /* Las estaciones fuera de la puerta de distancia no se listan: el motor
           no las usaria. La linea de estado dice cuantas quedaron fuera. */
        if (!g_mag_rows[i].in_range) continue;
        it = csnhypodbp_mag_item_new(&g_mag_rows[i], i);
        if (!it) continue;
        g_list_store_append(g_mag_store, it);
        g_object_unref(it);
    }
    csnhypodbp_mag_status_update();
    csnhypodbp_mag_canvas_fit();
}

/* Selector de magnitud (radios): cambia la vista, no las ventanas. */
void on_mag_kind_toggled(GtkCheckButton *b, gpointer data)
{
    int k = GPOINTER_TO_INT(data);
    if (!gtk_check_button_get_active(b)) return;
    if (k < 0 || k >= CSNHYPODBP_MAG_NKIND || k == g_mag_kind) return;
    g_mag_kind = k;
    csnhypodbp_mag_ov_sync();   /* los campos de distancia son por magnitud */
    rebuild_mag_list();
    if (g_mag_canvas) ewgui_canvas_queue_draw(g_mag_canvas);
}

/* --- Filtro A/M/Teo y override de distancia de `magnitudes` -------------- */

/* 1 mientras csnhypodbp_mag_ov_sync() escribe los campos (para no reentrar). */
static int g_mag_ov_syncing = 0;

/* Config de magnitud con el override de distancia de la magnitud activa. */
static void mag_effective_cfg(CsnhypodbpMagCfg *out)
{
    csnhypodbp_magcfg_apply_override(&g_magcfg, g_mag_kind,
                                     &g_mag_gate_ov[g_mag_kind], out);
}

/* Vuelca en la barra la puerta vigente de la magnitud activa. */
void csnhypodbp_mag_ov_sync(void)
{
    CsnhypodbpMagGateOv *ov = &g_mag_gate_ov[g_mag_kind];
    double lo = 0.0, hi = 0.0, km = 0.0, dmin = 0.0, dmax = 0.0;

    if (!g_mag_ov_min) return;
    g_mag_ov_syncing = 1;
    if (ov->on) {
        lo = ov->min_delta; hi = ov->max_delta; km = ov->max_dist_km;
        dmin = ov->min_depth; dmax = ov->max_depth;
    } else {
        csnhypodbp_magcfg_gate_limits(&g_magcfg, g_mag_kind, &lo, &hi, &km);
        dmin = csnhypodbp_magcfg_min_depth(&g_magcfg, g_mag_kind);
        dmax = csnhypodbp_magcfg_max_depth(&g_magcfg, g_mag_kind);
    }
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(g_mag_ov_min), lo);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(g_mag_ov_max), hi);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(g_mag_ov_km), km);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(g_mag_ov_dmin), dmin);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(g_mag_ov_depth), dmax);
    g_mag_ov_syncing = 0;
}

void on_mag_filter_toggled(GtkCheckButton *b, gpointer data)
{
    int which = GPOINTER_TO_INT(data);
    int on = gtk_check_button_get_active(b) ? 1 : 0;

    if (which == 0)      g_magf_auto = on;
    else if (which == 1) g_magf_manual = on;
    else                 g_magf_theo = on;
    rebuild_mag_list();
}

/* `reset default`: las CUATRO magnitudes vuelven a la puerta del .d de csnmags.
   Se usa desde el boton y al elegir otra solucion en Origins (los valores solo
   valen mientras se procesa el evento). */
void csnhypodbp_mag_ov_reset_all(void)
{
    memset(g_mag_gate_ov, 0, sizeof(g_mag_gate_ov));
    csnhypodbp_mag_ov_sync();
    rebuild_mag_list();
    if (g_mag_canvas) ewgui_canvas_queue_draw(g_mag_canvas);
}

void on_mag_ov_reset_clicked(GtkWidget *w, gpointer data)
{
    (void)w; (void)data;
    csnhypodbp_mag_ov_reset_all();
}

/* Editar cualquier campo de la barra activa el override de ESA magnitud: no hay
   casilla (el rango que se ve es el que se aplica). */
void on_mag_ov_changed(GtkSpinButton *s, gpointer data)
{
    CsnhypodbpMagGateOv *ov = &g_mag_gate_ov[g_mag_kind];

    (void)s; (void)data;
    if (g_mag_ov_syncing || !g_mag_ov_min) return;
    ov->on = 1;
    ov->min_delta = gtk_spin_button_get_value(GTK_SPIN_BUTTON(g_mag_ov_min));
    ov->max_delta = gtk_spin_button_get_value(GTK_SPIN_BUTTON(g_mag_ov_max));
    ov->max_dist_km = gtk_spin_button_get_value(GTK_SPIN_BUTTON(g_mag_ov_km));
    ov->min_depth = gtk_spin_button_get_value(GTK_SPIN_BUTTON(g_mag_ov_dmin));
    ov->max_depth = gtk_spin_button_get_value(GTK_SPIN_BUTTON(g_mag_ov_depth));
    rebuild_mag_list();
    if (g_mag_canvas) ewgui_canvas_queue_draw(g_mag_canvas);
}

/* --- Canvas de la sub-pestaña `magnitudes` ------------------------------- */

#define MAG_GRAB_TOL_PX  5.0    /* tolerancia para agarrar un borde de la franja */
#define MAG_MIN_SPAN_SEC 0.5    /* duracion minima de la ventana ajustable */
#define MAG_BAND_TOP     24.0   /* y del borde superior de la banda (donde va el handle) */

/* Canales de la estacion del canvas, en el orden de siempre (channelsOrder x
   componentsOrder, el mismo del canvas de picking). */
static StaNode g_MagNodes[MAX_ESTA];
static int     g_NumMagNodes = 0;

/* Vista procesada por canal. Se reconstruye solo cuando cambia la clave
   (estacion, magnitud, rango dibujado) o se invalidan las trazas: cambiar de
   magnitud con la misma ventana no vuelve a filtrar. */
static CsnhypodbpMagView g_mag_views[MAX_ESTA];
static int      g_mag_views_valid = 0;
static char     g_mag_views_sta[16];
static int      g_mag_views_kind = -1;
static double   g_mag_views_win0 = 0.0, g_mag_views_win1 = 0.0;

/* Ventana de dibujo CONGELADA mientras se arrastra: si no, al mover la franja el
   margen de contexto reencuadra el dibujo y el arrastre se escapa. */
static int    g_mag_freeze = 0;
/* Encuadre CONGELADO durante un arrastre: el EFECTIVO del `drag-begin` (no el
   automatico), para que la escala no cambie a mitad de la maniobra. */
static CsnhypodbpView g_mag_freeze_view;
/* Eje temporal del canvas de magnitudes: arrastre = zoom, doble clic = recentrar. */
static CsnhypodbpAxisDrag g_mag_axis_drag;
static double g_mag_axis_sx = 0.0;

/* Columnas del canvas de magnitudes (propias: el de picking usa las suyas). */
static double g_mag_col_min[MAX_DRAW_W], g_mag_col_max[MAX_DRAW_W];
static int    g_mag_col_has[MAX_DRAW_W];

/* Estado del arrastre de la franja. */
static int    g_mag_grab = CSNHYPODBP_MAGG_NONE;
static double g_mag_grab_t0 = 0.0, g_mag_grab_t1 = 0.0, g_mag_grab_sec_px = 0.0;

void csnhypodbp_mag_views_invalidate(void)
{
    int i;

    for (i = 0; i < MAX_ESTA; i++) csnhypodbp_magview_free(&g_mag_views[i]);
    g_mag_views_valid = 0;
    g_mag_views_kind = -1;
    g_mag_views_sta[0] = '\0';
}

/* Ventana vigente de la estacion del canvas (marca > default del .d). */
static void mag_current_window(double *t0, double *t1)
{
    double d0 = NAN, d1 = NAN;

    csnhypodbp_magcfg_window(&g_magcfg, g_mag_kind, &d0, &d1);
    csnhypodbp_magmarks_window(&g_mag_marks, g_mag_station, g_mag_kind, d0, d1, t0, t1);
}

/* Ancla (P efectiva) de una estacion de la tabla de magnitudes; 0 si no hay.
   `winsrc` recibe de donde sale la VENTANA ("M"/"A"/"Teo"), que es lo que
   filtran los checkboxes de la barra inferior. */
static double mag_anchor_of(const char *sta, const char **winsrc)
{
    int i;

    if (winsrc) *winsrc = "-";
    if (!sta || !sta[0]) return 0.0;
    for (i = 0; i < g_mag_rows_n; i++) {
        if (strcmp(g_mag_rows[i].sta, sta) != 0) continue;
        if (winsrc) *winsrc = csnhypodbp_magrow_vent_text(&g_mag_rows[i]);
        return g_mag_rows[i].anchor_t;
    }
    return 0.0;
}

/* Indices de los canales de `sta`, en el orden de siempre. Se incluyen TODOS los
   canales configurados de la estacion, no solo los que tienen traza: el canvas
   muestra siempre sus componentes y deja ver cual falta (banda "sin traza"). */
static int mag_nodes_for(const char *sta)
{
    int i;

    g_NumMagNodes = 0;
    if (!sta || !sta[0]) return 0;
    for (i = 0; i < NumEstaciones && g_NumMagNodes < MAX_ESTA; i++) {
        if (strcmp(StaArray[i].szStation, sta) != 0) continue;
        g_MagNodes[g_NumMagNodes].idx = i;
        g_MagNodes[g_NumMagNodes].dist = g_StaDist[i];
        g_NumMagNodes++;
    }
    if (g_NumMagNodes > 1) {
        build_chan_rank();
        qsort(g_MagNodes, (size_t)g_NumMagNodes, sizeof(StaNode), cmp_node_chan);
    }
    return g_NumMagNodes;
}

/* Altura del canvas de magnitudes segun el numero de canales de la estacion
   elegida (mismo criterio que el canvas de picking): lo que sobra lo cubre el
   scrolled window. Sin esto las filas de abajo quedaban fuera del area visible. */
void csnhypodbp_mag_canvas_fit(void)
{
    const char *sta;
    int n, h;

    if (!g_mag_canvas) return;
    sta = g_mag_station[0] ? g_mag_station : g_selected_station;
    n = mag_nodes_for(sta);
    /* Minimo por banda: el canvas llena el area del scrolled window y on_draw_mag
       reparte el alto REAL entre las componentes (proporcional a la ventana). */
    h = (n > 0) ? (n * CSNHYPODBP_BAND_MIN) + 60 : CSNHYPODBP_BAND_MIN + 60;
    gtk_widget_set_size_request(ewgui_canvas_widget(g_mag_canvas),
                                CSNHYPODBP_CANVAS_MIN_W, h);
}

/* Construye las vistas de los canales si cambio la clave. */
static void mag_views_sync(const char *sta, double win0, double win1)
{
    int n;

    if (g_mag_views_valid && g_mag_views_kind == g_mag_kind &&
        strcmp(g_mag_views_sta, sta) == 0 &&
        fabs(g_mag_views_win0 - win0) < 1e-6 && fabs(g_mag_views_win1 - win1) < 1e-6)
        return;
    csnhypodbp_mag_views_invalidate();
    for (n = 0; n < g_NumMagNodes; n++) {
        int i = g_MagNodes[n].idx;
        csnhypodbp_magview_build(&g_mag_views[n], StaArray[i].trace, g_mag_kind,
                                 &g_magcfg, win0, win1);
    }
    snprintf(g_mag_views_sta, sizeof(g_mag_views_sta), "%s", sta);
    g_mag_views_kind = g_mag_kind;
    g_mag_views_win0 = win0;
    g_mag_views_win1 = win1;
    g_mag_views_valid = 1;
}

/* Ancho dibujable del canvas de magnitudes, igual que en el dibujo
   (ncol = min(ancho - margen_izq - 10, MAX_DRAW_W)). */
static int mag_draw_w(int width);

/* Linea vertical a trazos que sigue al puntero, igual en los dos canvas.
   `in_plot` la limita al area de trazas (no sobre el margen de etiquetas). */
static void draw_cursor_line(cairo_t *cr, double x, int height, int in_plot)
{
    const double dash[] = { 2.0, 4.0 };

    if (!in_plot) return;
    cairo_set_source_rgb(cr, 0.35, 0.35, 0.45);
    cairo_set_line_width(cr, 1.0);
    cairo_set_dash(cr, dash, 2, 0.0);
    cairo_move_to(cr, x, 0);
    cairo_line_to(cr, x, (double)height);
    cairo_stroke(cr);
    cairo_set_dash(cr, NULL, 0, 0.0);
}

/* Vista vigente del canvas: durante un arrastre manda la congelada. */
static const CsnhypodbpView *mag_view_now(void)
{
    return g_mag_freeze ? &g_mag_freeze_view : &g_mag_zoomview;
}

/* Ventana efectiva del canvas de magnitudes. TODO (dibujo, clic, agarre del
   handle y arrastre) pasa por aqui: si cada camino recalculara la suya, se
   desincronizan (era el motivo de que la franja se moviera al hacer zoom). */
static int mag_frame(double *win0, double *span)
{
    const char *sta = g_mag_station[0] ? g_mag_station : g_selected_station;
    double t0 = NAN, t1 = NAN, anchor;

    mag_current_window(&t0, &t1);
    anchor = mag_anchor_of(sta, NULL);
    return csnhypodbp_magview_frame(mag_view_now(), anchor, t0, t1, win0, span);
}

void on_draw_mag(EwGuiCanvas *canvas, cairo_t *cr, int width, int height, void *data)
{
    const char *sta;
    double t0 = NAN, t1 = NAN, margin, anchor, win0, span;
    int margin_right = 10, draw_width = width - g_margin_left - margin_right;
    int n, row_h, ncol;

    (void)canvas; (void)data;
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);
    if (draw_width <= 0) return;
    ncol = draw_width < MAX_DRAW_W ? draw_width : MAX_DRAW_W;

    sta = g_mag_station[0] ? g_mag_station : g_selected_station;
    mag_current_window(&t0, &t1);
    margin = csnhypodbp_magview_margin(t0, t1);
    anchor = mag_anchor_of(sta, NULL);
    if (!mag_frame(&win0, &span)) {        /* sin ancla: avisa mas abajo y sale */
        win0 = anchor + t0 - margin;
        span = (anchor + t1 + margin) - win0;
    }

    /* (sin titulo: la magnitud activa, el ancla y la ventana ya estan en la tabla
       de magnitudes; el canvas queda entero para las trazas) */

    /* Sin estacion o sin ancla se dice el motivo en vez de dejar el cuadro vacio. */
    if (!sta || !sta[0] || anchor <= 0.0 || !(span > 0.0)) {
        cairo_set_source_rgb(cr, 0.4, 0.4, 0.4);
        cairo_set_font_size(cr, 12);
        cairo_move_to(cr, 10, 20);
        cairo_show_text(cr, (!sta || !sta[0])
                                ? "elige una estacion en la tabla de magnitudes"
                                : "sin P (ni observada ni teorica): no hay ancla");
        return;
    }

    if (mag_nodes_for(sta) <= 0) {
        cairo_set_source_rgb(cr, 0.4, 0.4, 0.4);
        cairo_set_font_size(cr, 12);
        cairo_move_to(cr, 10, 20);
        cairo_show_text(cr, "sin canales para esa estacion (revisa StaFile)");
        return;
    }

    row_h = (height - 60) / g_NumMagNodes;
    if (row_h < 80) row_h = g_spacing;
    mag_views_sync(sta, win0, win0 + span);

    /* Franja de la ventana y marca del ancla, sobre todas las filas. El arrastre
       vive SOLO en el handle (barra superior), asi el resto del canvas queda
       libre para el zoom por dos clics. */
    {
        double xa = csnhypodbp_view_sample_x(anchor + t0, win0, span, ncol);
        double xb = csnhypodbp_view_sample_x(anchor + t1, win0, span, ncol);
        double xp = csnhypodbp_view_sample_x(anchor, win0, span, ncol);
        double band_h = (height > MAG_BAND_TOP + 4.0) ? (height - MAG_BAND_TOP - 4.0) : 4.0;
        const double dash2[] = { 4.0, 3.0 };
        int k;

        cairo_save(cr);
        cairo_rectangle(cr, g_margin_left, MAG_BAND_TOP, ncol, band_h);
        cairo_clip(cr);
        cairo_set_source_rgba(cr, 0.95, 0.82, 0.35, 0.22);
        cairo_rectangle(cr, g_margin_left + xa, MAG_BAND_TOP, xb - xa, band_h);
        cairo_fill(cr);
        cairo_set_source_rgb(cr, 0.72, 0.52, 0.0);
        cairo_set_line_width(cr, 1.5);
        cairo_move_to(cr, g_margin_left + xa, MAG_BAND_TOP);
        cairo_line_to(cr, g_margin_left + xa, height);
        cairo_move_to(cr, g_margin_left + xb, MAG_BAND_TOP);
        cairo_line_to(cr, g_margin_left + xb, height);
        cairo_stroke(cr);

        cairo_set_source_rgb(cr, 0.55, 0.55, 0.75);
        cairo_set_line_width(cr, 1.0);
        cairo_set_dash(cr, dash2, 2, 0.0);
        cairo_move_to(cr, g_margin_left + xp, MAG_BAND_TOP);
        cairo_line_to(cr, g_margin_left + xp, height);
        cairo_stroke(cr);
        cairo_set_dash(cr, NULL, 0, 0.0);

        /* HANDLE: barra rellena en la parte superior de la franja, con marcas de
           agarre. Es la unica zona que mueve o redimensiona la ventana. */
        cairo_set_source_rgba(cr, 0.15, 0.35, 0.75, 0.9);
        cairo_rectangle(cr, g_margin_left + xa, MAG_BAND_TOP, xb - xa,
                        CSNHYPODBP_MAG_HANDLE_H);
        cairo_fill(cr);
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        cairo_set_line_width(cr, 1.0);
        for (k = 1; k <= 2; k++) {
            double yk = MAG_BAND_TOP + k * (CSNHYPODBP_MAG_HANDLE_H / 3.0);
            cairo_move_to(cr, g_margin_left + xa + 3.0, yk);
            cairo_line_to(cr, g_margin_left + xb - 3.0, yk);
        }
        cairo_stroke(cr);
        cairo_restore(cr);
    }

    /* Linea del puntero, por debajo del ancla y de la franja. */
    if (g_mag_cursor_valid)
        draw_cursor_line(cr, g_mag_cursor_x, height,
                         g_mag_cursor_x >= g_margin_left &&
                         g_mag_cursor_x <= g_margin_left + ncol);

    /* Ancla del zoom (el primer clic), como en `picking`: marca el punto fijo del
       proximo zoom. `g_mag_zoomview.anchor` es tiempo ABSOLUTO, igual que win0, asi
       que se proyecta con la misma escala que la franja, INCLUIDO su margen (si no,
       la marca aparece corrida g_margin_left px a la izquierda del clic). */
    if (g_mag_zoomview.have_anchor) {
        const double dashz[] = { 2.0, 4.0 };
        double xan = g_margin_left + csnhypodbp_view_sample_x(g_mag_zoomview.anchor,
                                                             win0, span, ncol);

        if (xan < g_margin_left) xan = g_margin_left;
        if (xan > g_margin_left + ncol) xan = g_margin_left + ncol;
        cairo_set_source_rgb(cr, 0.10, 0.45, 0.10);
        cairo_set_line_width(cr, 1.5);
        cairo_set_dash(cr, dashz, 2, 0.0);
        cairo_move_to(cr, xan, MAG_BAND_TOP);
        cairo_line_to(cr, xan, height);
        cairo_stroke(cr);
        cairo_set_dash(cr, NULL, 0, 0.0);
    }

    /* El zoom (clics y eje) y el encuadre congelado de un arrastre ya vienen
       aplicados por mag_frame(): unica fuente de verdad de la ventana visible. */

    for (n = 0; n < g_NumMagNodes; n++) {
        int idx = g_MagNodes[n].idx;
        int y_center = (n * row_h) + (row_h / 2) + 30;
        CsnhypodbpMagView *v = &g_mag_views[n];
        char lbl[64];
        double scale, max_abs = 0.0;
        long k;
        int px;

        cairo_set_source_rgb(cr, 0.1, 0.2, 0.7);
        csnhypodbp_trace_label(lbl, sizeof(lbl), StaArray[idx].szStation,
                               StaArray[idx].szChannel, 0.0);
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, 12);
        cairo_move_to(cr, 8, y_center + 5);
        cairo_show_text(cr, lbl);

        cairo_set_source_rgb(cr, 0.85, 0.85, 0.85);
        cairo_set_line_width(cr, 1);
        cairo_move_to(cr, g_margin_left, y_center);
        cairo_line_to(cr, width - margin_right, y_center);
        cairo_stroke(cr);

        if (!v->v || v->n <= 0) {
            /* Componente configurada pero sin traza descargada: la banda se
               mantiene y se dice, para no confundir "no hay dato" con "no
               existe" (y para que se vea de un golpe que componente falta). */
            cairo_set_source_rgb(cr, 0.6, 0.35, 0.35);
            cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                                   CAIRO_FONT_WEIGHT_NORMAL);
            cairo_set_font_size(cr, 11);
            cairo_move_to(cr, g_margin_left + 8, y_center - 8);
            cairo_show_text(cr, "sin traza");
            continue;
        }

        /* Una pasada por columna: min/max/has de la vista en ese pixel. Los
           huecos (NaN) se omiten: columna sin datos -> no se dibuja. */
        for (px = 0; px < ncol; px++) {
            double ta = win0 + ((double)px / ncol) * span;
            double tb = win0 + ((double)(px + 1) / ncol) * span;
            long k0 = (long)floor((ta - v->oldest) * v->rate);
            long k1 = (long)ceil((tb - v->oldest) * v->rate);
            double pmin = 1e30, pmax = -1e30;
            int has = 0;

            if (k0 < 0) k0 = 0;
            if (k1 > v->n) k1 = v->n;
            for (k = k0; k < k1; k++) {
                double fv = v->v[k];

                if (!isfinite(fv)) continue;
                if (fv < pmin) pmin = fv;
                if (fv > pmax) pmax = fv;
                has = 1;
            }
            g_mag_col_min[px] = has ? pmin : 0.0;
            g_mag_col_max[px] = has ? pmax : 0.0;
            g_mag_col_has[px] = has;
            if (has) {
                if (fabs(pmin) > max_abs) max_abs = fabs(pmin);
                if (fabs(pmax) > max_abs) max_abs = fabs(pmax);
            }
        }
        if (max_abs <= 0.0) continue;

        scale = (row_h * 0.45) / max_abs;
        cairo_set_source_rgb(cr, 0.05, 0.05, 0.05);
        cairo_set_line_width(cr, 1.0);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
        cairo_save(cr);
        cairo_rectangle(cr, g_margin_left, y_center - (row_h / 2.0), ncol, row_h);
        cairo_clip(cr);

        if (csnhypodbp_view_use_polyline(span, ncol, v->rate)) {
            /* Un pixel cubre menos de CSNHYPODBP_POLYLINE_SPP muestras (vistas
               diezmadas de Mwp/Ms): el envelope degenera en guiones, asi que se
               une muestra a muestra. Se corta en los huecos sin unir a traves. */
            long k0 = (long)floor((win0 - v->oldest) * v->rate) - 1;
            long k1 = (long)ceil((win0 + span - v->oldest) * v->rate) + 1;
            int pen = 0;

            if (k0 < 0) k0 = 0;
            if (k1 > v->n) k1 = v->n;
            for (k = k0; k < k1; k++) {
                double fv = v->v[k];
                double x, y;

                if (!isfinite(fv)) { pen = 0; continue; }
                x = g_margin_left + csnhypodbp_view_sample_x(
                        v->oldest + (double)k / v->rate, win0, span, ncol);
                y = y_center - fv * scale;
                if (pen) cairo_line_to(cr, x, y);
                else cairo_move_to(cr, x, y);
                pen = 1;
            }
            cairo_stroke(cr);
        } else {
            /* Envelope por tramos contiguos con datos; `min_half` garantiza que
               una zona plana (min==max) siga dejando trazo visible. */
            double min_half = csnhypodbp_view_min_half(scale, 0.8);
            int nruns = csnhypodbp_view_band_runs(g_mag_col_has, ncol, g_band_runs,
                                                  MAX_DRAW_W);
            int r;

            for (r = 0; r < nruns; r++) {
                int px0 = g_band_runs[2 * r];
                int px1 = g_band_runs[2 * r + 1];

                if (px1 <= px0) continue;
                /* max de izquierda a derecha y min de derecha a izquierda: cierra
                   la banda por arriba y por abajo. */
                for (px = px0; px < px1; px++) {
                    double ytop = y_center - (g_mag_col_max[px] + min_half) * scale;

                    if (px == px0) cairo_move_to(cr, g_margin_left + (double)px, ytop);
                    else cairo_line_to(cr, g_margin_left + (double)px, ytop);
                }
                for (px = px1 - 1; px >= px0; px--)
                    cairo_line_to(cr, g_margin_left + (double)px,
                                  y_center - (g_mag_col_min[px] - min_half) * scale);
                cairo_close_path(cr);
                cairo_fill(cr);
            }
            cairo_stroke(cr);
        }
        cairo_restore(cr);
    }
}

void on_mag_row_selected(GtkSingleSelection *sel, GParamSpec *pspec, gpointer data)
{
    gpointer item;
    char *sta = NULL;

    (void)pspec; (void)data;
    item = gtk_single_selection_get_selected_item(sel);
    /* El repoblado de la tabla (vaciado -> sin seleccion) no debe perder la
       estacion que se esta mirando. */
    if (item && CSNHYPODBP_IS_MAG_ITEM(item)) {
        g_object_get(item, "sta", &sta, NULL);
        snprintf(g_mag_station, sizeof(g_mag_station), "%s", sta ? sta : "");
        g_free(sta);
        csnhypodbp_mag_canvas_fit();
        csnhypodbp_view_set_default(&g_mag_zoomview, 0.0);   /* otra estacion: sin zoom */
        if (g_mag_canvas) ewgui_canvas_queue_draw(g_mag_canvas);
    }
}

/* Zoom por dos clics en el canvas de magnitudes, igual que en `picking`: el
   primer clic ancla, el segundo a la DERECHA acerca y a la IZQUIERDA vuelve al
   encuadre automatico. El `rel` se calcula en el MISMO sistema de coordenadas
   que el dibujo (zoom activo o encuadre del ancla), para que el clic y lo que se
   ve coincidan. El arrastre de la franja vive en el handle, no aqui. */
void on_mag_canvas_clicked(GtkGestureClick *g, int n_press, double x, double y,
                           gpointer data)
{
    GtkWidget *w;
    double win0 = 0.0, span = 0.0, rel;
    CsnhypodbpView cur;
    int cw = 0;

    (void)n_press; (void)y; (void)data;
    if (!g_mag_canvas) return;
    w = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(g));
    if (w) cw = mag_draw_w(gtk_widget_get_width(w));
    if (cw <= 0) return;
    /* Mismo encuadre que el dibujo (mag_frame): asi el `rel` del clic cae en el
       sistema de coordenadas correcto tambien despues de un zoom. */
    if (!mag_frame(&win0, &span)) return;
    memset(&cur, 0, sizeof(cur));
    cur.t0 = win0;
    cur.t1 = win0 + span;
    rel = csnhypodbp_view_x_to_rel(&cur, x, g_margin_left, cw);
    csnhypodbp_view_click(&g_mag_zoomview, rel);
    ewgui_canvas_queue_draw(g_mag_canvas);
}

void on_mag_drag_begin(GtkGestureDrag *g, double x, double y, gpointer data)
{
    const char *sta;
    double t0 = NAN, t1 = NAN, anchor, win0, span, xa, xb;
    int ncol, cw = 0;

    (void)g; (void)data;
    /* Solo desde el HANDLE (barra superior de la franja): el resto del canvas es
       para el zoom por dos clics, como en `picking`. */
    if (!csnhypodbp_magmarks_handle_hit(y, MAG_BAND_TOP)) return;
    if (!g_mag_canvas) return;
    ewgui_canvas_get_size(g_mag_canvas, &cw, NULL);
    ncol = mag_draw_w(cw);
    if (ncol <= 0) return;
    sta = g_mag_station[0] ? g_mag_station : g_selected_station;
    if (!g_mag_station[0] && sta && sta[0])
        snprintf(g_mag_station, sizeof(g_mag_station), "%s", sta);
    mag_current_window(&t0, &t1);
    anchor = mag_anchor_of(sta, NULL);
    /* Ventana EFECTIVA (con el zoom incluido): la franja se agarra donde se ve y
       el arrastre convierte pixeles a segundos con esa MISMA escala. */
    if (!mag_frame(&win0, &span)) return;
    xa = csnhypodbp_view_sample_x(anchor + t0, win0, span, ncol);
    xb = csnhypodbp_view_sample_x(anchor + t1, win0, span, ncol);
    g_mag_grab = csnhypodbp_magmarks_grab(x - g_margin_left, xa, xb, MAG_GRAB_TOL_PX);
    if (g_mag_grab == CSNHYPODBP_MAGG_NONE) return;
    g_mag_grab_t0 = t0;
    g_mag_grab_t1 = t1;
    g_mag_grab_sec_px = span / (double)ncol;
    /* El encuadre se congela en el EFECTIVO: arrastrar despues de un zoom ya no
       devuelve la vista al encuadre automatico a mitad de la maniobra. */
    g_mag_freeze_view.t0 = win0;
    g_mag_freeze_view.t1 = win0 + span;
    g_mag_freeze_view.active = 1;
    g_mag_freeze_view.have_anchor = 0;
    g_mag_freeze = 1;
}

void on_mag_drag_update(GtkGestureDrag *g, double ox, double oy, gpointer data)
{
    double dt, nt0, nt1;

    (void)g; (void)oy; (void)data;
    if (g_mag_grab == CSNHYPODBP_MAGG_NONE || !g_mag_station[0]) return;
    dt = ox * g_mag_grab_sec_px;
    switch (g_mag_grab) {
        case CSNHYPODBP_MAGG_T0: nt0 = g_mag_grab_t0 + dt; nt1 = g_mag_grab_t1; break;
        case CSNHYPODBP_MAGG_T1: nt0 = g_mag_grab_t0;      nt1 = g_mag_grab_t1 + dt; break;
        default:                 nt0 = g_mag_grab_t0 + dt; nt1 = g_mag_grab_t1 + dt; break;
    }
    if (nt1 < nt0 + MAG_MIN_SPAN_SEC) return;
    if (csnhypodbp_magmarks_set(&g_mag_marks, g_mag_station, g_mag_kind, nt0, nt1) != 0)
        return;
    /* La tabla se repuebla al soltar (no en cada movimiento): repoblarla ahora
       perderia la seleccion de la fila a media maniobra. */
    if (g_mag_canvas) ewgui_canvas_queue_draw(g_mag_canvas);
}

void on_mag_drag_end(GtkGestureDrag *g, double ox, double oy, gpointer data)
{
    (void)g; (void)ox; (void)oy; (void)data;
    g_mag_grab = CSNHYPODBP_MAGG_NONE;
    g_mag_freeze = 0;
    rebuild_mag_list();   /* la tabla muestra la ventana ajustada */
    if (g_mag_canvas) ewgui_canvas_queue_draw(g_mag_canvas);
}

/* Puntero del canvas de magnitudes: informa si el clic va a arrastrar la franja,
   mover un borde o hacer zoom. La decision es pura (magview_cursor). */
static int mag_draw_w(int width);   /* definido en la seccion del eje */
void on_mag_canvas_motion(GtkEventControllerMotion *c, double x, double y, gpointer data)
{
    GtkWidget *w = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(c));
    const char *sta;
    double t0 = NAN, t1 = NAN, anchor, win0, span, xa = NAN, xb = NAN;
    int ncol = 0, on_handle = 0, on_edge = 0;

    (void)data;
    if (!w || !g_mag_canvas) return;
    ewgui_canvas_get_size(g_mag_canvas, &ncol, NULL);
    ncol = mag_draw_w(ncol);
    if (ncol > 0) {
        sta = g_mag_station[0] ? g_mag_station : g_selected_station;
        mag_current_window(&t0, &t1);
        anchor = mag_anchor_of(sta, NULL);
        if (mag_frame(&win0, &span)) {
            xa = csnhypodbp_view_sample_x(anchor + t0, win0, span, ncol);
            xb = csnhypodbp_view_sample_x(anchor + t1, win0, span, ncol);
            on_edge = (csnhypodbp_magmarks_grab(x - g_margin_left, xa, xb, MAG_GRAB_TOL_PX)
                       != CSNHYPODBP_MAGG_NONE);
        }
    }
    /* El handle solo es agarrable sobre la franja, no en toda la franja de alto. */
    on_handle = on_edge && csnhypodbp_magmarks_handle_hit(y, MAG_BAND_TOP);
    gtk_widget_set_cursor_from_name(w,
                                    csnhypodbp_magview_cursor(on_handle, on_edge));
    /* Linea del puntero: se repinta solo cuando cambia de pixel. */
    {
        int moved = !g_mag_cursor_valid || (int)x != (int)g_mag_cursor_x;

        g_mag_cursor_x = x;
        g_mag_cursor_valid = 1;
        if (moved) ewgui_canvas_queue_draw(g_mag_canvas);
    }
}

void on_mag_canvas_leave(GtkEventControllerMotion *c, gpointer data)
{
    GtkWidget *w = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(c));

    (void)data;
    if (w) gtk_widget_set_cursor_from_name(w, NULL);   /* cursor por defecto */
    if (g_mag_cursor_valid) {
        g_mag_cursor_valid = 0;
        if (g_mag_canvas) ewgui_canvas_queue_draw(g_mag_canvas);
    }
}

/* --- Eje de tiempo del canvas de magnitudes ------------------------------ */

/* Ancho dibujable del canvas de magnitudes, igual que en el dibujo
   (ncol = min(ancho - margen_izq - 10, MAX_DRAW_W)). */
static int mag_draw_w(int width)
{
    int w = width - g_margin_left - 10;

    if (w > MAX_DRAW_W) w = MAX_DRAW_W;
    return (w > 0) ? w : 0;
}

/* Encuadre efectivo listo para proyectar: vista con t0/t1 = la ventana visible. */
static int mag_frame_view(CsnhypodbpView *out)
{
    double win0 = 0.0, span = 0.0;

    memset(out, 0, sizeof(*out));
    if (!mag_frame(&win0, &span)) return 0;
    out->t0 = win0;
    out->t1 = win0 + span;
    return 1;
}

/* Tiempo ABSOLUTO del x del eje, recortado al area dibujable. */
static double mag_axis_rel_at(double x)
{
    CsnhypodbpView cur;
    int dw, right;

    if (!g_mag_axis_canvas) return 0.0;
    dw = mag_draw_w(gtk_widget_get_width(ewgui_canvas_widget(g_mag_axis_canvas)));
    right = g_margin_left + dw;
    if (dw <= 0 || !mag_frame_view(&cur)) return 0.0;
    if (x < g_margin_left) x = g_margin_left;
    if (x > right) x = right;
    return csnhypodbp_view_x_to_rel(&cur, x, g_margin_left, dw);
}

void on_draw_mag_axis(EwGuiCanvas *canvas, cairo_t *cr, int width, int height, void *data)
{
    CsnhypodbpView fv;
    double win0 = 0.0, span = 0.0, xa = 0.0, xb = 0.0;
    double ticks[128];
    int dw = mag_draw_w(width);
    int max_ticks, n, i;

    (void)canvas; (void)data;
    cairo_set_source_rgb(cr, 0.97, 0.97, 0.98);
    cairo_paint(cr);
    cairo_set_source_rgb(cr, 0.35, 0.35, 0.35);
    cairo_set_font_size(cr, 10);
    cairo_move_to(cr, 6, height - 4);
    cairo_show_text(cr, "UTC");

    if (dw <= 0 || !mag_frame(&win0, &span)) return;

    /* Banda del arrastre en curso, con el mismo helper que el eje de `picking`. */
    memset(&fv, 0, sizeof(fv));
    fv.t0 = win0;
    fv.t1 = win0 + span;
    if (csnhypodbp_view_axis_band_x(&fv, &g_mag_axis_drag, g_margin_left, dw, &xa, &xb)) {
        cairo_set_source_rgba(cr, 0.5, 0.5, 0.5, 0.35);
        cairo_rectangle(cr, xa, 0.0, xb - xa, (double)height);
        cairo_fill(cr);
    }

    max_ticks = dw / 90;
    if (max_ticks < 1) max_ticks = 1;
    n = csnhypodbp_view_ticks(win0, win0 + span, max_ticks, ticks, 128);
    for (i = 0; i < n; i++) {
        char b[16];
        cairo_text_extents_t ext;
        double x = csnhypodbp_view_sample_x(ticks[i], win0, span, dw);

        if (x < g_margin_left - 0.5 || x > g_margin_left + dw + 0.5) continue;
        cairo_set_source_rgb(cr, 0.4, 0.4, 0.4);
        cairo_move_to(cr, x, 1);
        cairo_line_to(cr, x, 7);
        cairo_stroke(cr);
        csnhypodbp_view_fmt_utc(ticks[i], b, sizeof(b));
        cairo_text_extents(cr, b, &ext);
        cairo_set_source_rgb(cr, 0.15, 0.15, 0.15);
        cairo_move_to(cr, x - ext.width / 2.0, height - 4);
        cairo_show_text(cr, b);
    }
}

void on_mag_axis_drag_begin(GtkGestureDrag *g, double sx, double sy, gpointer data)
{
    int dw, right;

    (void)g; (void)sy; (void)data;
    if (!g_mag_axis_canvas) return;
    g_mag_axis_sx = sx;
    dw = mag_draw_w(gtk_widget_get_width(ewgui_canvas_widget(g_mag_axis_canvas)));
    right = g_margin_left + dw;
    if (dw <= 0 || sx < g_margin_left || sx > right) {
        g_mag_axis_drag.active = 0;      /* press fuera del area: ignorar */
        return;
    }
    csnhypodbp_view_drag_begin(&g_mag_axis_drag, mag_axis_rel_at(sx));
    ewgui_canvas_queue_draw(g_mag_axis_canvas);
}

void on_mag_axis_drag_update(GtkGestureDrag *g, double ox, double oy, gpointer data)
{
    (void)g; (void)oy; (void)data;
    if (!g_mag_axis_drag.active) return;
    csnhypodbp_view_drag_update(&g_mag_axis_drag, mag_axis_rel_at(g_mag_axis_sx + ox));
    if (g_mag_axis_canvas) ewgui_canvas_queue_draw(g_mag_axis_canvas);
}

void on_mag_axis_drag_end(GtkGestureDrag *g, double ox, double oy, gpointer data)
{
    (void)g; (void)oy; (void)data;
    if (g_mag_axis_drag.active) {
        csnhypodbp_view_drag_update(&g_mag_axis_drag, mag_axis_rel_at(g_mag_axis_sx + ox));
        csnhypodbp_view_drag_end(&g_mag_axis_drag, &g_mag_zoomview);
    }
    if (g_mag_canvas) ewgui_canvas_queue_draw(g_mag_canvas);
    if (g_mag_axis_canvas) ewgui_canvas_queue_draw(g_mag_axis_canvas);
}

/* Doble clic en el eje: vuelve al encuadre automatico del ancla, es decir a ver
   la franja entera y centrada (funciona aunque el zoom la haya dejado fuera). */
void on_mag_axis_clicked(GtkGestureClick *g, int n_press, double x, double y,
                         gpointer data)
{
    (void)g; (void)x; (void)y; (void)data;
    if (n_press != 2) return;
    csnhypodbp_view_set_default(&g_mag_zoomview, 0.0);
    g_mag_axis_drag.active = 0;
    if (g_mag_canvas) ewgui_canvas_queue_draw(g_mag_canvas);
    if (g_mag_axis_canvas) ewgui_canvas_queue_draw(g_mag_axis_canvas);
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
    if (csnhypodbp_station_changed(prev, g_selected_station)) {
        csnhypodbp_view_set_default(&g_view, g_dScreenTime);
        csnhypodbp_pick_anchor_refresh();   /* otra estacion: otra P de referencia */
    }
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
        csnhypodbp_pick_anchor_refresh();
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
    /* `calculate`/`calculate all`: solo magnitudes, con las ventanas ajustadas. */
    int mags_only;
    unsigned kind_mask;
    CsnhypodbpMagOvrRow ovr[CSNHYPODBP_MAGMARKS_MAX];
    int novr;
} LocateJob;

static gboolean locate_finish(gpointer data);

static gpointer locate_thread(gpointer data) {
    LocateJob *job = data;
    if (job->mags_only) {
        /* Sin relocalizar: la solucion se queda como esta y solo se recalculan
           las magnitudes con las ventanas que el usuario ajusto. */
        job->out = job->base;
        job->rc = csnhypodbp_mag_run(getenv("EW_PARAMS"), getenv("EW_HOME"),
                                     getenv("EW_VERSION"), NULL, &job->base,
                                     job->tank, job->ntank,
                                     job->ovr, job->novr, job->kind_mask,
                                     &job->mag, job->err, sizeof(job->err));
    } else {
        job->rc = csnhypodbp_locate_run(g_locators, g_nlocators, job->locator,
                                        getenv("EW_PARAMS"), getenv("EW_HOME"), getenv("EW_VERSION"),
                                        NULL, &job->base, job->mp, job->nmp,
                                        job->tank, job->ntank,
                                        job->ovr, job->novr, job->kind_mask,
                                        &job->out, &job->mag,
                                        job->err, sizeof(job->err));
    }
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
        logit("t", "csnhypodbp: %s OK [%s] nph=%d rms=%.2f ML=%s Mwp=%s\n",
              job->mags_only ? "calculate" : "locate",
              job->locator, job->out.nph, job->out.rms,
              job->mag.ml[0] ? job->mag.ml : "-", job->mag.mwp[0] ? job->mag.mwp : "-");
    } else {
        logit("e", "csnhypodbp: %s fallo [%s]: %s\n",
              job->mags_only ? "calculate" : "locate", job->locator, job->err);
    }
    g_free(job);
    return G_SOURCE_REMOVE;
}

/* Unico punto que escribe la tablita del preview, para que estado, valores,
   color y sensibilidad de los botones no se puedan desincronizar: la candidata
   del `locate` si esta vigente, si no la solucion elegida en el detalle
   (pestana Eventos), si no guiones. */
/* Celda de magnitud del preview: el valor numerico de la fila en "%.2f", o un
   guion si la solucion no tiene esa magnitud (`nmag*` < 0). */
static void preview_mag_text(double v, char *out, size_t n)
{
    if (!isfinite(v) || v < 0.0) snprintf(out, n, "-");
    else snprintf(out, n, "%.2f", v);
}

/* Radio de magnitud preferida del cuadro de la candidata. */
void on_preview_pref_toggled(GtkCheckButton *b, gpointer data) {
    int k = GPOINTER_TO_INT(data);

    if (g_preview_pref_sync) return;                  /* lo mueve preview_update */
    if (!gtk_check_button_get_active(b)) return;
    if (k < 0 || k >= CSNHYPODBP_PREVIEW_MAGS) return;
    g_preview_pref_kind = k;
}

void csnhypodbp_preview_update(void) {
    CsnhypodbpPreviewVals v;
    int i;
    if (!g_preview_box || !g_preview_title) return;
    if (g_preview_valid) {
        csnhypodbp_preview_from_candidate(&v, g_active_locator,
                                          g_preview_sol.lat, g_preview_sol.lon,
                                          g_preview_sol.depth, g_preview_sol.nph,
                                          g_preview_sol.rms,
                                          g_preview_mag.ml, g_preview_mag.mwp,
                                          g_preview_mag.mb, g_preview_mag.ms);
    } else {
        gpointer item = g_selection_detail
                        ? gtk_single_selection_get_selected_item(g_selection_detail)
                        : NULL;
        if (item) {
            CsnhypodbpRow *row = CSNHYPODBP_ROW(item);
            double lat = 0, lon = 0, depth = 0;
            double nm[CSNHYPODBP_MAG_NKIND] = { -1.0, -1.0, -1.0, -1.0 };
            char mtxt[CSNHYPODBP_MAG_NKIND][16];
            int k;

            g_object_get(row, "lat", &lat, "lon", &lon, "depth", &depth,
                         "nmag0", &nm[0], "nmag1", &nm[1],
                         "nmag2", &nm[2], "nmag3", &nm[3], NULL);
            for (k = 0; k < CSNHYPODBP_MAG_NKIND; k++)
                preview_mag_text(nm[k], mtxt[k], sizeof(mtxt[k]));
            /* rms/nph: las celdas ya formateadas de la fila (c5/c7). Las cuatro
               magnitudes salen de nmag0..3, asi el formato es el mismo en las 4. */
            csnhypodbp_preview_from_selected(&v, lat, lon, depth,
                                             csnhypodbp_row_col(row, 5),
                                             csnhypodbp_row_col(row, 7),
                                             mtxt[0], mtxt[1], mtxt[2], mtxt[3]);
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
    /* Radios de magnitud preferida: sensibles solo si esa magnitud vino con valor
       (y solo con candidata: es ella la que se confirma). La eleccion del usuario
       se respeta mientras la candidata viva; si la preferida ya no tiene valor (o
       no hay eleccion), se cae a la regla de csnmags. */
    {
        int has[CSNHYPODBP_PREVIEW_MAGS];

        has[0] = g_preview_valid && g_preview_mag.ml[0] != 0;
        has[1] = g_preview_valid && g_preview_mag.mwp[0] != 0;
        has[2] = g_preview_valid && g_preview_mag.mb[0] != 0;
        has[3] = g_preview_valid && g_preview_mag.ms[0] != 0;
        if (g_preview_valid && !csnhypodbp_mag_pref_ok(has, g_preview_pref_kind))
            g_preview_pref_kind = csnhypodbp_mag_pref_default(g_preview_mag.ml,
                                                             g_preview_mag.mwp,
                                                             g_preview_mag.mb,
                                                             g_preview_mag.ms);
        g_preview_pref_sync = 1;
        for (i = 0; i < CSNHYPODBP_PREVIEW_MAGS; i++) {
            if (!g_preview_radio[i]) continue;
            gtk_widget_set_sensitive(GTK_WIDGET(g_preview_radio[i]), has[i]);
            gtk_check_button_set_active(GTK_CHECK_BUTTON(g_preview_radio[i]),
                                        g_preview_pref_kind == i);
        }
        g_preview_pref_sync = 0;
    }
    /* La candidata del locate tambien se ve en los dos mapas. */
    csnhypodbp_maps_refresh();
}

void on_preview_discard(GtkWidget *w, gpointer d) {
    (void)w; (void)d;
    g_preview_valid = FALSE;
    g_preview_pref_kind = -1;
    csnhypodbp_preview_update();   /* vuelve a mostrar la solucion elegida */
    rebuild_station_list();        /* quita el residual de la candidata */
}

void on_preview_confirm(GtkWidget *w, gpointer d) {
    (void)w; (void)d;
    if (!g_preview_valid) return;
    csnhypodbp_confirm_manual(&g_preview_sol, &g_preview_mag, g_preview_pref_kind);
    g_preview_valid = FALSE;
    g_preview_pref_kind = -1;
    csnhypodbp_preview_update();
    csnhypodbp_manual_clear();
    rebuild_station_list();
    if (canvas_global) ewgui_canvas_queue_draw(canvas_global);
}

/* Mascara de magnitudes del proximo `calculate` (bit 0=ML..3=Ms); la fijan los
   botones antes de arrancar el job. */
static unsigned g_mag_calc_mask = 0u;

/* Filas del WindowOverridesFile: la ventana ajustada a mano de cada estacion,
   para las magnitudes de la mascara y solo las que se van a enviar (`used`).
   En tiempo ABSOLUTO (ancla + t0/t1), que es lo que espera csnmags. */
static int mag_ovr_rows_build(unsigned kind_mask, CsnhypodbpMagOvrRow *rows, int max)
{
    int i, k, n = 0;

    if (kind_mask == 0u) kind_mask = (1u << CSNHYPODBP_MAGMARKS_KIND) - 1u;
    for (i = 0; i < g_mag_rows_n && n < max; i++) {
        if (!g_mag_rows[i].used || !(g_mag_rows[i].anchor > 0.0)) continue;
        for (k = 0; k < CSNHYPODBP_MAGMARKS_KIND && n < max; k++) {
            double t0 = 0.0, t1 = 0.0;

            if (!(kind_mask & (1u << k))) continue;
            if (!csnhypodbp_magmarks_touched(&g_mag_marks, g_mag_rows[i].sta, k)) continue;
            csnhypodbp_magmarks_window(&g_mag_marks, g_mag_rows[i].sta, k,
                                       g_mag_rows[i].t0, g_mag_rows[i].t1, &t0, &t1);
            if (!(t1 > t0)) continue;
            snprintf(rows[n].sta, sizeof(rows[n].sta), "%s", g_mag_rows[i].sta);
            rows[n].kind = k;
            rows[n].t0 = g_mag_rows[i].anchor + t0;
            rows[n].t1 = g_mag_rows[i].anchor + t1;
            n++;
        }
    }
    return n;
}

/* Arranca un job. `mags_only` = recalcular SOLO magnitudes con las ventanas
   ajustadas (sin relocalizar): lo usan `calculate`/`calculate all`. */
static void locate_start(int mags_only) {
    LocateJob *job;
    GThread *th;
    int i;
    if (selected_qid == 0) {
        logit("t", "csnhypodbp: %s sin evento seleccionado\n",
              mags_only ? "calculate" : "locate");
        return;
    }
    if (!mags_only && g_nlocators <= 0) {
        logit("t", "csnhypodbp: locate sin localizadores en el .d\n");
        return;
    }
    job = g_new0(LocateJob, 1);
    job->base.qid = selected_qid; job->base.version = (unsigned)selected_version; job->base.mod = selected_mod;
    job->base.otime = selected_otime; job->base.lat = selected_lat;
    job->base.lon = selected_lon; job->base.depth = selected_depth;
    /* Picadas = las filas de la tabla (una P y una S por estacion), filtradas
       por la seleccion de estaciones/picks y por el origen A/M/Teo. Desde la
       pestana de magnitudes mandan SU barra A/M/Teo y la casilla de su tabla
       (es lo que el usuario tiene delante); en picking, la tabla de Ondas. */
    {
        CsnhypodbpPickFilter filter;
        int pick_on_mag[MAX_ESTA][2];
        int used[MAX_ESTA];
        const int (*pon)[2] = (const int (*)[2])g_pick_on;

        if (mags_only) {
            filter.auto_on = g_magf_auto;
            filter.manual_on = g_magf_manual;
            filter.theo_on = g_magf_theo;
            for (i = 0; i < g_mag_rows_n && i < MAX_ESTA; i++) used[i] = g_mag_rows[i].used;
            if (csnhypodbp_pick_on_from_used(g_mag_sta_idx, used, g_mag_rows_n,
                                             pick_on_mag, g_sta_rows_n) == 0)
                pon = (const int (*)[2])pick_on_mag;
        } else {
            filter.auto_on = g_filter_auto;
            filter.manual_on = g_filter_manual;
            filter.theo_on = g_filter_theo;
        }
        job->base.npicks = csnhypodbp_locate_picks(g_sta_rows, g_sta_rows_n, pon,
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
    job->mags_only = mags_only;
    /* Las ventanas ajustadas van al motor en LAS DOS rutas: `calculate` con la
       mascara del boton y `locate` con todas las que haya a mano. */
    job->kind_mask = mags_only ? g_mag_calc_mask : 0u;
    job->novr = mag_ovr_rows_build(job->kind_mask, job->ovr, CSNHYPODBP_MAGMARKS_MAX);
    logit("t", "csnhypodbp: %s [%s] qid=%d npicks=%d overlay=%d tank=%d winovr=%d\n",
          mags_only ? "calculate" : "locate", job->locator, job->base.qid,
          job->base.npicks, job->nmp, job->ntank, job->novr);
    th = g_thread_new("locate", locate_thread, job);
    g_thread_unref(th);
}

/* Boton `locate` (localiza y refina) y los dos de magnitudes: `calculate` aplica
   las ventanas ajustadas de la magnitud ACTIVA y `calculate all` las de las
   cuatro (en los dos casos, una sola corrida de csnmags). */
void on_btn_locate_clicked(GtkWidget *w, gpointer d) {
    (void)w; (void)d;
    locate_start(0);
}

void on_mag_calc_clicked(GtkWidget *w, gpointer d) {
    (void)w; (void)d;
    if (g_mag_kind < 0 || g_mag_kind >= CSNHYPODBP_MAGMARKS_KIND) return;
    g_mag_calc_mask = 1u << g_mag_kind;
    locate_start(1);
}

void on_mag_calc_all_clicked(GtkWidget *w, gpointer d) {
    (void)w; (void)d;
    g_mag_calc_mask = (1u << CSNHYPODBP_MAGMARKS_KIND) - 1u;
    locate_start(1);
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

/* 1 si la pestaña raiz Procesar es la activa (cualquiera de sus sub-pestañas).
   Se compara el WIDGET de la pagina, no su indice: añadir sub-pestañas no
   altera este predicado. */
static gboolean waves_page_active(void) {
    int n;
    if (!g_notebook || !g_page_procesar) return FALSE;
    n = gtk_notebook_get_current_page(GTK_NOTEBOOK(g_notebook));
    return n >= 0 &&
           gtk_notebook_get_nth_page(GTK_NOTEBOOK(g_notebook), n) == g_page_procesar;
}

/* Lleva la ventana a la pestaña raiz Procesar (su primera sub-pestaña). */
void csnhypodbp_show_procesar(void) {
    int n;
    if (!g_notebook || !g_page_procesar) return;
    n = gtk_notebook_page_num(GTK_NOTEBOOK(g_notebook), g_page_procesar);
    if (n >= 0) gtk_notebook_set_current_page(GTK_NOTEBOOK(g_notebook), n);
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

/* ====================================================================
 * Columna derecha: mapa de planta (contrato de csnrv) y panel de perfil
 * ==================================================================== */

/* Estado de vista del mapa de planta: transformacion de csnrv
   screen = (imagen - centro) * zoom + canvas/2 + paneo. */
static double g_map_zoom = 1.0;
static double g_map_pan_x = 0.0, g_map_pan_y = 0.0;
static double g_map_cx = 0.0, g_map_cy = 0.0;
static int    g_map_fit_key = -1;              /* identidad del ultimo auto-encuadre */
static int    g_map_need_fit = 1;
static double g_map_fit_lon0 = 0, g_map_fit_lon1 = 0, g_map_fit_lat0 = 0, g_map_fit_lat1 = 0;
/* Referencia del encuadre: la solucion verde (elegida en Origins) o, si no hay,
   la preferida. El mapa se centra SIEMPRE en ella, con cualquier MapZoom. */
static double g_map_fit_ref_lat = NAN, g_map_fit_ref_lon = NAN;
static int    g_map_panning = 0;
static double g_map_pan_last_x = 0.0, g_map_pan_last_y = 0.0;
/* Puntero: posiciones ya proyectadas de cada punto (las llena el dibujo, las usa
   el hit test del clic) y estado del clic-vs-arrastre. */
#define MAP_HIT_RADIUS 7.0
#define MAP_DRAG_PX    3.0
static double g_map_plan_sx[CSNHYPODBP_MAX_MAP_PTS], g_map_plan_sy[CSNHYPODBP_MAX_MAP_PTS];
static double g_prof_sx[CSNHYPODBP_MAX_MAP_PTS], g_prof_sy[CSNHYPODBP_MAX_MAP_PTS];
static CsnhypodbpRow *g_map_row[CSNHYPODBP_MAX_MAP_PTS];   /* fila de cada punto */
static char g_map_id[CSNHYPODBP_MAX_MAP_PTS][CSNHYPODBP_MAP_ID_LEN];  /* "qid/mod" de cada punto */
static int  g_map_sel = -1;                    /* punto de la solucion elegida en Origins */
static double g_map_press_x = 0.0, g_map_press_y = 0.0;
static int    g_map_press_hit = -1;
static int    g_map_moved = 0;
static double g_prof_press_x = 0.0, g_prof_press_y = 0.0;
static int    g_prof_press_hit = -1;
static int    g_prof_moved = 0;
static int    g_prof_loaded = -1;              /* perfil con datos ya cargados */
static double g_prof_zoom = 1.0;               /* zoom del panel de perfil */
static double g_prof_pan_x = 0.0, g_prof_pan_y = 0.0;   /* paneo en unidades de datos */
static double g_prof_units_px_x = 1.0, g_prof_units_px_y = 1.0;  /* traduce el arrastre */
static int    g_prof_fit_key = -1;             /* identidad del ultimo encuadre */
static int    g_prof_pick_key = -1;            /* identidad del ultimo perfil elegido */
static int    g_prof_panning = 0;
static double g_prof_last_x = 0.0, g_prof_last_y = 0.0;

static double row_mag(const CsnhypodbpRow *row)
{
    const char *ml = csnhypodbp_row_col((CsnhypodbpRow *)row, 9);
    double m = (ml && ml[0]) ? atof(ml) : 0.0;
    return (m > 0.0 && m < 10.0) ? m : 0.0;
}

static void row_id_str(CsnhypodbpRow *row, char *out, size_t n);

static void map_pt_push(double lat, double lon, double depth, double mag, int kind,
                        CsnhypodbpRow *row)
{
    CsnhypodbpMapPt *p;
    if (g_map_npts >= CSNHYPODBP_MAX_MAP_PTS) return;
    if (!isfinite(lat) || !isfinite(lon)) return;
    if (lat == 0.0 && lon == 0.0) return;          /* solucion sin posicion */
    p = &g_map_pts[g_map_npts];
    g_map_row[g_map_npts] = row;
    if (row) row_id_str(row, g_map_id[g_map_npts], CSNHYPODBP_MAP_ID_LEN);
    else g_map_id[g_map_npts][0] = '\0';           /* la candidata no tiene fila */
    g_map_npts++;
    p->lat = lat; p->lon = lon; p->depth = depth; p->mag = mag; p->kind = kind;
}

/* "qid/mod" de una fila, o "-" si no hay. */
static void row_id_str(CsnhypodbpRow *row, char *out, size_t n)
{
    int qid = 0, mod = 0;
    if (!row) { snprintf(out, n, "-"); return; }
    g_object_get(row, "qid", &qid, "mod", &mod, NULL);
    snprintf(out, n, "%d/%d", qid, mod);
}

/* Radio del marcador en pixeles de pantalla: la preferida escala con la
 * magnitud. `panel` = 0 planta, 1 perfil. */
static double marker_radius(const CsnhypodbpMapPt *p, int panel)
{
    if (p->kind == 1) {
        if (p->mag > 0.0) return panel ? (1.2 * p->mag + 2.5) : (2.2 * p->mag + 3.5);
        return panel ? 4.0 : 6.0;
    }
    if (p->kind == 2) return panel ? 4.5 : 6.0;
    return panel ? 3.0 : 4.0;
}

/* Posicion de una fila en la vista ORDENADA del detalle, o -1. La seleccion vive
   sobre g_sort_detail, no sobre g_store_detail: buscar en el store daria la
   posicion del store y select_item acabaria seleccionando OTRA fila. */
static int detail_row_pos(CsnhypodbpRow *row)
{
    GListModel *m;
    guint i, n;

    if (!row || !g_selection_detail) return -1;
    m = G_LIST_MODEL(g_selection_detail);
    n = g_list_model_get_n_items(m);
    for (i = 0; i < n; i++) {
        gpointer it = g_list_model_get_item(m, i);
        gboolean same = (it == (gpointer)row);
        if (it) g_object_unref(it);
        if (same) return (int)i;
    }
    return -1;
}

/* Selecciona en la tabla de Origins la fila de un marcador (clic sin arrastre). */
static void map_select_row(CsnhypodbpRow *row)
{
    int pos = detail_row_pos(row);
    if (pos < 0) return;
    gtk_selection_model_select_item(GTK_SELECTION_MODEL(g_selection_detail),
                                    (guint)pos, TRUE);
}

/* Carga (o recarga) las polilineas de slab/topografia del perfil activo. */
static void map_load_profile_lines(void)
{
    const CsnhypodbpProfile *pr;
    char err[256];

    if (g_prof_loaded == g_active_profile) return;
    csnhypodbp_profile_line_free(&g_prof_slab);
    csnhypodbp_profile_line_free(&g_prof_topo);
    g_prof_loaded = g_active_profile;
    if (g_active_profile < 0 || g_active_profile >= g_profiles.n) return;
    pr = &g_profiles.p[g_active_profile];
    if (csnhypodbp_profile_line_read(pr->slab_file, g_profile_lon_col, g_profile_val_col,
                                     &g_prof_slab, err, sizeof(err)) != 0)
        logit("et", "csnhypodbp: perfil %s sin slab (%s)\n", pr->name, err);
    if (csnhypodbp_profile_line_read(pr->topo_file, g_profile_lon_col, g_profile_val_col,
                                     &g_prof_topo, err, sizeof(err)) != 0)
        logit("et", "csnhypodbp: perfil %s sin topografia (%s)\n", pr->name, err);
}

/* Texto de estado de los dos paneles (etiquetas OCULTAS: no se ven, pero el
   arnes GUI las lee). Incluye el origen seleccionado en Origins. */
static void map_caption_update(void)
{
    char plan_txt[192], prof_txt[192], sel[48] = "-";
    gpointer item = g_selection_detail
                    ? gtk_single_selection_get_selected_item(g_selection_detail)
                    : NULL;
    int nsol = 0, i;

    for (i = 0; i < g_map_npts; i++)
        if (g_map_pts[i].kind != 2) nsol++;
    if (item) row_id_str(CSNHYPODBP_ROW(item), sel, sizeof(sel));

    if (item && nsol > 0)
        snprintf(plan_txt, sizeof(plan_txt), "Event %s - %d sol%s sel=%s",
                 selected_id, nsol, g_preview_valid ? " + candidate" : "", sel);
    else
        snprintf(plan_txt, sizeof(plan_txt), "No solution selected");

    if (g_active_profile >= 0 && g_active_profile < g_profiles.n)
        snprintf(prof_txt, sizeof(prof_txt), "Profile %s - %d slab + %d topo - %d pts",
                 g_profiles.p[g_active_profile].name,
                 g_prof_slab.n, g_prof_topo.n, g_map_npts);
    else if (g_profiles.n == 0)
        snprintf(prof_txt, sizeof(prof_txt), "No profiles declared");
    else
        snprintf(prof_txt, sizeof(prof_txt), "No profile (sin solucion preferida)");

    if (g_map_plan_cap) gtk_label_set_text(GTK_LABEL(g_map_plan_cap), plan_txt);
    if (g_map_prof_cap) gtk_label_set_text(GTK_LABEL(g_map_prof_cap), prof_txt);

    /* Las etiquetas de arriba estan OCULTAS y GTK4 no las expone por AT-SPI, asi
       que el estado va tambien como descripcion accesible de cada canvas (lo lee
       gui_probe.py --assert-desc). */
    if (g_map_plan) {
        GtkWidget *w = ewgui_canvas_widget(g_map_plan);
        if (w) gtk_accessible_update_property(GTK_ACCESSIBLE(w),
                   GTK_ACCESSIBLE_PROPERTY_DESCRIPTION, plan_txt, -1);
    }
    if (g_map_prof) {
        GtkWidget *w = ewgui_canvas_widget(g_map_prof);
        if (w) gtk_accessible_update_property(GTK_ACCESSIBLE(w),
                   GTK_ACCESSIBLE_PROPERTY_DESCRIPTION, prof_txt, -1);
    }
}

/* UNICO punto que recalcula el estado de los dos paneles: puntos a dibujar
   (preferida + resto de soluciones de Origins + candidata del locate), perfil
   activo (el mas cercano a la preferida), datos del perfil y captions. */
void csnhypodbp_maps_refresh(void)
{
    gpointer item = g_selection_detail
                    ? gtk_single_selection_get_selected_item(g_selection_detail)
                    : NULL;
    CsnhypodbpRow *pref = NULL;
    double plat = 0.0, plon = 0.0;
    int have_pref = 0, key, i;

    g_map_npts = 0;
    g_map_press_hit = g_prof_press_hit = -1;
    if (item) {
        EventGroup *g = csnhypodbp_event_group_of(CSNHYPODBP_ROW(item));
        size_t k, ns;
        pref = g ? csnhypodbp_event_group_preferred(g) : NULL;
        if (pref) {
            double lat = 0, lon = 0, depth = 0;
            g_object_get(pref, "lat", &lat, "lon", &lon, "depth", &depth, NULL);
            plat = lat; plon = lon;
            have_pref = 1;
            map_pt_push(lat, lon, depth, row_mag(pref), 1, pref);
        }
        ns = g ? csnhypodbp_event_group_nsol(g) : 0;
        for (k = 0; k < ns; k++) {
            CsnhypodbpRow *r = csnhypodbp_event_group_solution(g, k);
            double lat = 0, lon = 0, depth = 0;
            if (!r || r == pref) continue;
            g_object_get(r, "lat", &lat, "lon", &lon, "depth", &depth, NULL);
            map_pt_push(lat, lon, depth, row_mag(r), 0, r);
        }
    }
    if (g_preview_valid) {
        double mag = g_preview_mag.ml[0] ? atof(g_preview_mag.ml) : 0.0;
        map_pt_push(g_preview_sol.lat, g_preview_sol.lon, g_preview_sol.depth, mag, 2, NULL);
    }

    /* Identidad de la VISTA (evento + solucion elegida + candidata): gobierna el
       auto-encuadre y la eleccion del perfil activo. Una llegada del anillo no
       la cambia, asi que no mueve ni el encuadre ni el perfil. */
    key = ((selected_qid * 31 + selected_mod) * 7 + selected_version) * 2
          + (g_preview_valid ? 1 : 0);

    /* Solucion elegida en Origins -> se dibuja verde. Se busca por id (el orden de
       g_map_pts no es el de la tabla) y en la seleccion vigente, no en selected_id
       (este refresco tambien corre antes de que la seleccion se recomponga). */
    {
        char sel_id[CSNHYPODBP_MAP_ID_LEN] = "";
        if (item) row_id_str(CSNHYPODBP_ROW(item), sel_id, sizeof(sel_id));
        g_map_sel = csnhypodbp_map_find_id(g_map_id, g_map_npts, sel_id);
    }

    /* Perfil activo: el mas cercano a la solucion preferida del evento. Se
       re-elige SOLO cuando cambia la vista del usuario (evento o solucion
       elegida), no cuando una llegada del anillo cambia la preferida: si no, el
       panel de perfil cambiaba solo mientras se procesaba el evento. */
    if (key != g_prof_pick_key) {
        g_prof_pick_key = key;
        g_active_profile = have_pref ? csnhypodbp_profile_nearest(&g_profiles, plat, plon) : -1;
    }
    map_load_profile_lines();

    /* Auto-encuadre del mapa de planta al cambiar de evento o de candidata. */
    if (key != g_map_fit_key) {
        double lon0, lon1, lat0, lat1;
        g_map_fit_key = key;
        if (csnhypodbp_map_bbox(g_map_pts, g_map_npts, &lon0, &lon1, &lat0, &lat1) == 0) {
            /* Referencia del centro: la verde si hay, si no la preferida (kind 1). */
            int ri = (g_map_sel >= 0 && g_map_sel < g_map_npts) ? g_map_sel : -1;

            if (ri < 0) {
                int k;
                for (k = 0; k < g_map_npts; k++) {
                    if (g_map_pts[k].kind == 1) { ri = k; break; }
                }
            }
            if (ri >= 0) {
                g_map_fit_ref_lat = g_map_pts[ri].lat;
                g_map_fit_ref_lon = g_map_pts[ri].lon;
            } else {
                g_map_fit_ref_lat = g_map_fit_ref_lon = NAN;
            }
            csnhypodbp_map_span_min(&lon0, &lon1, &lat0, &lat1, g_map_min_span_deg);
            g_map_fit_lon0 = lon0; g_map_fit_lon1 = lon1;
            g_map_fit_lat0 = lat0; g_map_fit_lat1 = lat1;
            g_map_need_fit = 1;
        }
    }

    /* El perfil tambien se re-encuadra (a sus propios datos) al cambiar de
       evento o de candidata; el zoom/paneo manual se respeta hasta entonces. */
    if (key != g_prof_fit_key) {
        g_prof_fit_key = key;
        g_prof_zoom = 1.0;
        g_prof_pan_x = g_prof_pan_y = 0.0;
    }

    /* Captions ocultos: ademas de informar, son la asercion del arnes GUI
       (evento, nº de soluciones, perfil activo, origen bajo el puntero y
       seleccionado). Ver map_caption_update(). */
    map_caption_update();

    for (i = 0; i < 2; i++) {
        EwGuiCanvas *cv = i ? g_map_prof : g_map_plan;
        if (cv) ewgui_canvas_queue_draw(cv);
    }
}

/* --- mapa de planta --- */

static void plan_overlay_map(double lat, double lon, void *ud, double *sx, double *sy)
{
    double iw = 1.0, ih = 1.0;
    (void)ud;
    if (g_map_pixbuf) {
        iw = gdk_pixbuf_get_width(g_map_pixbuf);
        ih = gdk_pixbuf_get_height(g_map_pixbuf);
    }
    ewgui_geo_project(lat, lon, iw, ih, sx, sy);
}

/* Marcador: preferida (rojo con borde blanco), resto (gris azulado), candidata
   del locate (mismos colores que la tablita del preview) y la solucion elegida en
   Origins en verde, que reemplaza al color que le tocaria. */
static void draw_marker(cairo_t *cr, double x, double y, double r, int kind, int selected)
{
    if (r < 1.0) r = 1.0;
    if (selected) {
        cairo_set_source_rgb(cr, 0.0, 0.78, 0.33);
        cairo_arc(cr, x, y, r, 0.0, 2.0 * M_PI);
        cairo_fill_preserve(cr);
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        cairo_set_line_width(cr, r * 0.3);
        cairo_stroke(cr);
        return;
    }
    if (kind == 2) {
        /* Candidata del `locate` (el estado "New solution" de la tablita):
           ESTRELLA roja con borde blanco, para que se distinga de la preferida
           (circulo rojo oscuro) a cualquier zoom. */
        double sx[10], sy[10];
        int ns = csnhypodbp_marker_star_path(x, y, r * 1.6, 5, 0.45, sx, sy, 10);
        if (ns > 0) {
            cairo_set_source_rgb(cr, 0.85, 0.10, 0.10);
            cairo_move_to(cr, sx[0], sy[0]);
            for (int k = 1; k < ns; k++) cairo_line_to(cr, sx[k], sy[k]);
            cairo_close_path(cr);
            cairo_fill_preserve(cr);
            cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
            cairo_set_line_width(cr, r * 0.3);
            cairo_stroke(cr);
        }
    } else if (kind == 1) {
        cairo_set_source_rgb(cr, 0.83, 0.18, 0.18);
        cairo_arc(cr, x, y, r, 0.0, 2.0 * M_PI);
        cairo_fill_preserve(cr);
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        cairo_set_line_width(cr, r * 0.3);
        cairo_stroke(cr);
    } else {
        cairo_set_source_rgba(cr, 0.38, 0.49, 0.55, 0.55);
        cairo_arc(cr, x, y, r, 0.0, 2.0 * M_PI);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, 0.10, 0.10, 0.10, 0.6);
        cairo_set_line_width(cr, r * 0.25);
        cairo_stroke(cr);
    }
}

/* Rejilla de meridianos/paralelos (paso segun el zoom, como csnrv). */
static void draw_plan_grid(cairo_t *cr, double iw, double ih, double zoom)
{
    double step = ewgui_geo_grid_step(zoom);
    double lon, lat, x1, y1, x2, y2;
    if (!(step > 0.0)) return;
    cairo_set_line_width(cr, 0.5 / zoom);
    cairo_set_source_rgba(cr, 0.3, 0.3, 0.3, 0.22);
    for (lon = -180.0; lon <= 180.0; lon += step) {
        ewgui_geo_project(90.0, lon, iw, ih, &x1, &y1);
        ewgui_geo_project(-90.0, lon, iw, ih, &x2, &y2);
        cairo_move_to(cr, x1, y1);
        cairo_line_to(cr, x2, y2);
    }
    for (lat = -90.0; lat <= 90.0; lat += step) {
        ewgui_geo_project(lat, -180.0, iw, ih, &x1, &y1);
        ewgui_geo_project(lat, 180.0, iw, ih, &x2, &y2);
        cairo_move_to(cr, x1, y1);
        cairo_line_to(cr, x2, y2);
    }
    cairo_stroke(cr);
}

void on_draw_map_plan(EwGuiCanvas *canvas, cairo_t *cr, int width, int height, void *data)
{
    double iw = 1.0, ih = 1.0, zoom;
    int i;

    (void)canvas; (void)data;
    cairo_set_source_rgb(cr, 0.94, 0.95, 0.96);
    cairo_paint(cr);
    if (g_map_pixbuf) {
        iw = gdk_pixbuf_get_width(g_map_pixbuf);
        ih = gdk_pixbuf_get_height(g_map_pixbuf);
    }
    if (g_map_need_fit && g_map_pixbuf && width > 40 && height > 40) {
        csnhypodbp_mapfit_at(g_map_fit_ref_lat, g_map_fit_ref_lon,
                             g_map_fit_lon0, g_map_fit_lon1,
                             g_map_fit_lat0, g_map_fit_lat1,
                             iw, ih, width, height, 12.0, g_map_zoom_factor,
                             &g_map_zoom, &g_map_cx, &g_map_cy);
        g_map_pan_x = g_map_pan_y = 0.0;
        g_map_need_fit = 0;
    }
    zoom = (g_map_zoom > 0.0) ? g_map_zoom : 1.0;

    cairo_save(cr);
    cairo_translate(cr, width / 2.0 + g_map_pan_x, height / 2.0 + g_map_pan_y);
    cairo_scale(cr, zoom, zoom);
    cairo_translate(cr, -g_map_cx, -g_map_cy);

    if (g_map_pixbuf) {
        gdk_cairo_set_source_pixbuf(cr, g_map_pixbuf, 0, 0);
        cairo_paint(cr);
    }
    draw_plan_grid(cr, iw, ih, zoom);

    /* Rectas de los perfiles; la activa resaltada y recortada a sus datos. */
    for (i = 0; i < g_profiles.n; i++) {
        const CsnhypodbpProfile *pr = &g_profiles.p[i];
        double lo0 = pr->lon1, lo1 = pr->lon2, x1, y1, x2, y2;
        if (pr->lon1 == pr->lon2) continue;                 /* vertical */
        if (i == g_active_profile && g_prof_slab.n > 0 &&
            g_prof_slab.lon_min <= g_prof_slab.lon_max) {
            lo0 = g_prof_slab.lon_min;
            lo1 = g_prof_slab.lon_max;
        }
        ewgui_geo_project(pr->a * lo0 + pr->b, lo0, iw, ih, &x1, &y1);
        ewgui_geo_project(pr->a * lo1 + pr->b, lo1, iw, ih, &x2, &y2);
        if (i == g_active_profile) {
            cairo_set_source_rgb(cr, 0.08, 0.40, 0.75);
            cairo_set_line_width(cr, 2.0 / zoom);
        } else {
            cairo_set_source_rgba(cr, 0.45, 0.45, 0.45, 0.65);
            cairo_set_line_width(cr, 1.0 / zoom);
        }
        cairo_move_to(cr, x1, y1);
        cairo_line_to(cr, x2, y2);
        cairo_stroke(cr);
    }

    /* Capas .lines (subduccion, fallas, limites de regiones). */
    if (g_map_overlay)
        ewgui_overlay_draw(cr, g_map_overlay, plan_overlay_map, NULL, 1.0 / zoom);

    /* Soluciones del evento (preferida destacada) y candidata del locate.
       Primero el resto y encima la preferida y la candidata: si no, los
       marcadores superpuestos (mismo epicentro) tapan a la preferida. */
    {
        int k;
        for (k = 0; k <= 2; k++) {
            for (i = 0; i < g_map_npts; i++) {
                const CsnhypodbpMapPt *p = &g_map_pts[i];
                double x, y;
                if (p->kind != k) continue;
                ewgui_geo_project(p->lat, p->lon, iw, ih, &x, &y);
                /* El hit test del clic trabaja en pixeles del widget: hay que
                   deshacer la transformacion (imagen -> pantalla). */
                g_map_plan_sx[i] = (x - g_map_cx) * zoom + width / 2.0 + g_map_pan_x;
                g_map_plan_sy[i] = (y - g_map_cy) * zoom + height / 2.0 + g_map_pan_y;
                draw_marker(cr, x, y, marker_radius(p, 0) / zoom, p->kind, 0);
            }
        }
        /* La solucion elegida en Origins, al final y en verde. Dentro de la
           transformacion van las coordenadas de IMAGEN (las _sx/_sy son de
           pantalla, solo para el hit test del clic). */
        if (g_map_sel >= 0 && g_map_sel < g_map_npts) {
            const CsnhypodbpMapPt *sp = &g_map_pts[g_map_sel];
            double sx2, sy2;
            ewgui_geo_project(sp->lat, sp->lon, iw, ih, &sx2, &sy2);
            draw_marker(cr, sx2, sy2, marker_radius(sp, 0) / zoom, sp->kind, 1);
        }
    }
    cairo_restore(cr);
}

/* --- panel de perfil (x = longitud, y = -profundidad) --- */

void on_draw_map_profile(EwGuiCanvas *canvas, cairo_t *cr, int width, int height, void *data)
{
    CsnhypodbpFrame f;
    const double mx = 40.0, mt = 10.0, mb = 20.0;
    double pw = width - mx - 8.0, ph = height - mt - mb;
    double x0, x1, y0, y1;
    int i;

    (void)canvas; (void)data;
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_paint(cr);
    if (pw < 20.0 || ph < 20.0) return;

    /* Marco por defecto: SOLO los datos del perfil, asi el slab y la topografia
       siempre quedan dentro. El zoom y el paneo del usuario se aplican encima;
       una solucion que caiga fuera se ve acercando o arrastrando. */
    csnhypodbp_profile_frame(&g_prof_slab, &g_prof_topo, g_profile_depth_min_km,
                             &x0, &x1, &y0, &y1);
    csnhypodbp_frame_set(&f, x0, x1, y0, y1, mx, mt, pw, ph);
    csnhypodbp_frame_view(&f, g_prof_zoom, g_prof_pan_x, g_prof_pan_y);
    /* Unidades de datos por pixel: traducen el arrastre del paneo. */
    g_prof_units_px_x = (f.x1 - f.x0) / pw;
    g_prof_units_px_y = (f.y1 - f.y0) / ph;

    /* Marco, etiquetas de los ejes y rejilla punteada. */
    cairo_set_source_rgb(cr, 0.35, 0.35, 0.35);
    cairo_set_line_width(cr, 1.0);
    cairo_rectangle(cr, mx, mt, pw, ph);
    cairo_stroke(cr);

    cairo_select_font_face(cr, "sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 9.0);
    cairo_set_source_rgb(cr, 0.25, 0.25, 0.25);
    {
        double v, sx, sy, span = f.x1 - f.x0;
        const char *fmt = (span < 0.5) ? "%.3f" : (span < 5.0 ? "%.2f" : "%.1f");
        int k;
        for (k = 0; k <= 4; k++) {
            char lbl[32];
            v = f.x0 + k * span / 4.0;
            csnhypodbp_frame_project(&f, v, f.y0, &sx, &sy);
            snprintf(lbl, sizeof(lbl), fmt, v);
            cairo_move_to(cr, sx - 10.0, mt + ph + 13.0);
            cairo_show_text(cr, lbl);
            v = f.y1 - k * (f.y1 - f.y0) / 4.0;
            csnhypodbp_frame_project(&f, f.x0, v, &sx, &sy);
            snprintf(lbl, sizeof(lbl), "%.0f", v);
            cairo_move_to(cr, 4.0, sy + 3.0);
            cairo_show_text(cr, lbl);
        }
    }

    /* Todo lo que es dato va recortado al marco: nada puede salirse del cuadro
       (el slab de un perfil del norte baja mas de 200 km). */
    cairo_save(cr);
    cairo_rectangle(cr, mx, mt, pw, ph);
    cairo_clip(cr);
    {
        double dash[2] = { 3.0, 3.0 };
        double v, sx, sy;
        int k;
        cairo_set_source_rgba(cr, 0.6, 0.6, 0.6, 0.5);
        cairo_set_line_width(cr, 0.6);
        cairo_set_dash(cr, dash, 2, 0.0);
        for (k = 0; k <= 4; k++) {
            v = f.x0 + k * (f.x1 - f.x0) / 4.0;
            csnhypodbp_frame_project(&f, v, f.y0, &sx, &sy);
            cairo_move_to(cr, sx, mt);
            cairo_line_to(cr, sx, mt + ph);
            v = f.y1 - k * (f.y1 - f.y0) / 4.0;
            csnhypodbp_frame_project(&f, f.x0, v, &sx, &sy);
            cairo_move_to(cr, mx, sy);
            cairo_line_to(cr, mx + pw, sy);
        }
        cairo_stroke(cr);
        cairo_set_dash(cr, NULL, 0, 0.0);
    }

    /* Subduccion (slab, km) y topografia (m/1000), ambas lineas negras; NaN
       rompe la polilinea (costa afuera en el slab). */
    {
        int k, started = 0;
        cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
        cairo_set_line_width(cr, 1.0);
        for (k = 0; k < g_prof_slab.n; k++) {
            double sx, sy;
            if (!isfinite(g_prof_slab.val[k])) { started = 0; continue; }
            csnhypodbp_frame_project(&f, g_prof_slab.lon[k], g_prof_slab.val[k], &sx, &sy);
            if (started) cairo_line_to(cr, sx, sy);
            else { cairo_move_to(cr, sx, sy); started = 1; }
        }
        if (g_prof_slab.n > 0) cairo_stroke(cr);
        started = 0;
        for (k = 0; k < g_prof_topo.n; k++) {
            double sx, sy;
            if (!isfinite(g_prof_topo.val[k])) { started = 0; continue; }
            csnhypodbp_frame_project(&f, g_prof_topo.lon[k],
                                     g_prof_topo.val[k] / 1000.0, &sx, &sy);
            if (started) cairo_line_to(cr, sx, sy);
            else { cairo_move_to(cr, sx, sy); started = 1; }
        }
        if (g_prof_topo.n > 0) cairo_stroke(cr);
    }

    /* Soluciones del evento y candidata (mismo codigo de color que la planta;
       la preferida y la candidata se dibujan al final, por encima del resto). */
    {
        int k;
        for (k = 0; k <= 2; k++) {
            for (i = 0; i < g_map_npts; i++) {
                const CsnhypodbpMapPt *p = &g_map_pts[i];
                double sx, sy;
                if (p->kind != k) continue;
                csnhypodbp_frame_project(&f, p->lon, -p->depth, &sx, &sy);
                g_prof_sx[i] = sx;
                g_prof_sy[i] = sy;
                draw_marker(cr, sx, sy, marker_radius(p, 1), p->kind, 0);
            }
        }
        /* La solucion elegida en Origins, al final y en verde (aqui las _sx/_sy
           ya son pixeles de pantalla: no hay transformacion de cairo). */
        if (g_map_sel >= 0 && g_map_sel < g_map_npts)
            draw_marker(cr, g_prof_sx[g_map_sel], g_prof_sy[g_map_sel],
                        marker_radius(&g_map_pts[g_map_sel], 1),
                        g_map_pts[g_map_sel].kind, 1);
    }
    cairo_restore(cr);   /* fin del recorte al marco del perfil */
}

/* --- interaccion del mapa de planta (rueda = zoom, arrastre = paneo) --- */

void on_map_plan_scroll(GtkEventControllerScroll *ctrl, double dx, double dy, gpointer data)
{
    (void)ctrl; (void)dx; (void)data;
    g_map_zoom = ewgui_geo_zoom_step(g_map_zoom, dy < 0.0, 0.05, 200.0);
    if (g_map_plan) ewgui_canvas_queue_draw(g_map_plan);
}

void on_map_plan_press(GtkGestureClick *g, int n_press, double x, double y, gpointer data)
{
    (void)g; (void)n_press; (void)data;
    g_map_panning = 1;
    g_map_moved = 0;
    g_map_press_x = g_map_pan_last_x = x;
    g_map_press_y = g_map_pan_last_y = y;
    g_map_press_hit = csnhypodbp_map_hit(g_map_pts, g_map_plan_sx, g_map_plan_sy,
                                         g_map_npts, x, y, MAP_HIT_RADIUS);
}

void on_map_plan_release(GtkGestureClick *g, int n_press, double x, double y, gpointer data)
{
    (void)g; (void)n_press; (void)x; (void)y; (void)data;
    g_map_panning = 0;
    /* Sin arrastre fue un clic: selecciona ese origen en la tabla de Origins. */
    if (!g_map_moved && g_map_press_hit >= 0 && g_map_press_hit < g_map_npts)
        map_select_row(g_map_row[g_map_press_hit]);
    g_map_press_hit = -1;
}

void on_map_plan_motion(GtkEventControllerMotion *c, double x, double y, gpointer data)
{
    (void)c; (void)data;
    if (csnhypodbp_map_drag_exceeded(x - g_map_press_x, y - g_map_press_y, MAP_DRAG_PX))
        g_map_moved = 1;
    if (g_map_panning) {
        g_map_pan_x += x - g_map_pan_last_x;
        g_map_pan_y += y - g_map_pan_last_y;
        g_map_pan_last_x = x;
        g_map_pan_last_y = y;
        if (g_map_plan) ewgui_canvas_queue_draw(g_map_plan);
    }
}

/* --- interaccion del panel de perfil (rueda = zoom, arrastre = paneo) --- */

void on_map_prof_scroll(GtkEventControllerScroll *ctrl, double dx, double dy, gpointer data)
{
    (void)ctrl; (void)dx; (void)data;
    g_prof_zoom = ewgui_geo_zoom_step(g_prof_zoom, dy < 0.0, 0.2, 50.0);
    if (g_map_prof) ewgui_canvas_queue_draw(g_map_prof);
}

void on_map_prof_press(GtkGestureClick *g, int n_press, double x, double y, gpointer data)
{
    (void)g; (void)n_press; (void)data;
    g_prof_panning = 1;
    g_prof_moved = 0;
    g_prof_press_x = g_prof_last_x = x;
    g_prof_press_y = g_prof_last_y = y;
    g_prof_press_hit = csnhypodbp_map_hit(g_map_pts, g_prof_sx, g_prof_sy,
                                          g_map_npts, x, y, MAP_HIT_RADIUS);
}

void on_map_prof_release(GtkGestureClick *g, int n_press, double x, double y, gpointer data)
{
    (void)g; (void)n_press; (void)x; (void)y; (void)data;
    g_prof_panning = 0;
    if (!g_prof_moved && g_prof_press_hit >= 0 && g_prof_press_hit < g_map_npts)
        map_select_row(g_map_row[g_prof_press_hit]);
    g_prof_press_hit = -1;
}

void on_map_prof_motion(GtkEventControllerMotion *c, double x, double y, gpointer data)
{
    (void)c; (void)data;
    if (csnhypodbp_map_drag_exceeded(x - g_prof_press_x, y - g_prof_press_y, MAP_DRAG_PX))
        g_prof_moved = 1;
    if (g_prof_panning) {
        /* El paneo se guarda en unidades de datos (lon/km), no en pixeles. */
        g_prof_pan_x -= (x - g_prof_last_x) * g_prof_units_px_x;
        g_prof_pan_y += (y - g_prof_last_y) * g_prof_units_px_y;
        g_prof_last_x = x;
        g_prof_last_y = y;
        if (g_map_prof) ewgui_canvas_queue_draw(g_map_prof);
    }
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
            /* El reset (zoom, ancla de la P, marcas y ventanas de magnitud) es
               SOLO si el cambio de solucion lo pidio el usuario: una llegada del
               anillo recompone la seleccion con g_ring_sync puesto y no debe
               mover la vista que se esta mirando. */
            if (csnhypodbp_view_should_reset(changed, g_ring_sync)) {
                g_zoom_factor = 1.0;
                csnhypodbp_view_set_default(&g_view, g_dScreenTime);
                csnhypodbp_pick_anchor_refresh();   /* otra solucion: otra P */
                /* Otra solucion: otras anclas y otras trazas, asi que las
                   ventanas ajustadas a mano ya no valen. */
                csnhypodbp_magmarks_clear(&g_mag_marks);
                csnhypodbp_mag_views_invalidate();
                /* El zoom por clics tampoco sobrevive al cambio de solucion. */
                csnhypodbp_view_set_default(&g_mag_zoomview, 0.0);
                /* Las puertas tampoco: vuelven a los valores del .d. Los ajustes
                   de la barra valen solo mientras se procesa ESE evento. */
                csnhypodbp_mag_ov_reset_all();
            }
        }
    }
    csnhypodbp_preview_update();   /* la tablita sigue a la solucion elegida */
    csnhypodbp_maps_refresh();     /* y los mapas, al evento elegido */
    rebuild_mag_list();            /* y la tabla de magnitudes, al evento elegido */
}

/* Rellena Origins con las soluciones del grupo de `master_row` (la preferida
   primero). Devuelve la preferida, o NULL si no hay fila/grupo. */
static CsnhypodbpRow *detail_fill(CsnhypodbpRow *master_row)
{
    EventGroup *g;
    CsnhypodbpRow *pref;
    size_t n, i;

    if (!g_store_detail) return NULL;
    g_list_store_remove_all(g_store_detail);
    if (!master_row) return NULL;
    g = csnhypodbp_event_group_of(master_row);
    if (!g) return NULL;
    pref = csnhypodbp_event_group_preferred(g);
    if (pref) g_list_store_append(g_store_detail, pref);
    n = csnhypodbp_event_group_nsol(g);
    for (i = 0; i < n; i++) {
        CsnhypodbpRow *s = csnhypodbp_event_group_solution(g, i);
        if (s && s != pref) g_list_store_append(g_store_detail, s);
    }
    return pref;
}

/* Selecciona en Origins la solucion (qid,mod,ver); si ya no esta, `fallback`. */
static void detail_select_solution(CsnhypodbpRow *fallback, int qid, int mod, int ver)
{
    guint i, n;

    if (!g_selection_detail) return;
    n = g_list_model_get_n_items(G_LIST_MODEL(g_store_detail));
    for (i = 0; i < n; i++) {
        gpointer it = g_list_model_get_item(G_LIST_MODEL(g_store_detail), i);
        CsnhypodbpRow *r = it ? CSNHYPODBP_ROW(it) : NULL;
        int rq = 0, rm = 0, rv = 0;
        if (r) g_object_get(r, "qid", &rq, "mod", &rm, "qver", &rv, NULL);
        if (r && csnhypodbp_view_solution_same(qid, mod, ver, rq, rm, rv)) {
            map_select_row(r);
            g_object_unref(it);
            return;
        }
        if (it) g_object_unref(it);
    }
    if (fallback) map_select_row(fallback);
}

/* Una solucion que llega del anillo para el evento que el usuario tiene abierto
   no debe mover su vista: se recompone Origins conservando la solucion elegida
   (qid,mod,ver). Con `g_ring_sync` puesto, la re-seleccion no dispara el reset
   del zoom/anclas (ver csnhypodbp_view_should_reset). */
void csnhypodbp_detail_refresh(void)
{
    gpointer sel, master;
    CsnhypodbpRow *pref;
    int qid = 0, mod = 0, ver = 0;

    if (!g_store_detail || !g_selection_detail || !g_selection_events) return;
    sel = gtk_single_selection_get_selected_item(g_selection_detail);
    if (sel) g_object_get(CSNHYPODBP_ROW(sel), "qid", &qid, "mod", &mod, "qver", &ver, NULL);
    master = gtk_single_selection_get_selected_item(g_selection_events);
    pref = detail_fill(master ? CSNHYPODBP_ROW(master) : NULL);
    if (!master) return;
    g_ring_sync = 1;
    if (sel) detail_select_solution(pref, qid, mod, ver);
    else if (pref) map_select_row(pref);
    g_ring_sync = 0;
}

/* Al seleccionar un evento (lista maestra) se despliega abajo la lista de sus
   soluciones, con la preferida en primer lugar. */
void on_event_selected(GtkSingleSelection *sel, GParamSpec *pspec, gpointer data) {
    (void)pspec; (void)data;
    gpointer item = gtk_single_selection_get_selected_item(sel);
    if (!g_store_detail) return;
    detail_fill(item ? CSNHYPODBP_ROW(item) : NULL);
    if (!item) return;
    if (g_selection_detail) {
        /* La primera fila del store es la preferida: se selecciona por fila, no
           por la posicion 0 (la vista del detalle esta ordenada). */
        gpointer first = g_list_model_get_item(G_LIST_MODEL(g_store_detail), 0);
        if (first) {
            map_select_row(CSNHYPODBP_ROW(first));
            g_object_unref(first);
        }
    }
    csnhypodbp_maps_refresh();     /* el evento cambio: perfil y planta nuevos */
}

void on_list_double_click(GtkGestureClick *g, int n_press, double x, double y, gpointer data) {
    (void)g; (void)x; (void)y; (void)data;
    if (n_press != 2) return;
    guint idx = gtk_single_selection_get_selected(g_selection_detail);
    if (idx == GTK_INVALID_LIST_POSITION) return;
    if (g_notebook) csnhypodbp_show_procesar();   /* pestaña Procesar */
    csnhypodbp_manual_clear();   /* nuevo evento: overlay manual limpio */
    csnhypodbp_pick_anchor_refresh();   /* evento nuevo: ancla nueva */
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

/* El overlay manual cambio (picar o borrar): la tabla y las picadas que van al
   localizador salen de `g_sta_rows`, asi que hay que reconstruirla ademas de
   repintar. Sin esto, picar y pulsar `locate` no enviaba la manual. */
static void pick_overlay_changed(void)
{
    rebuild_station_list();
    if (canvas_global) ewgui_canvas_queue_draw(canvas_global);
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
        pick_overlay_changed();   /* la tabla y el envio al localizador, al dia */
        return;
    }

    /* Modo zoom (Seisan): 1.er clic ancla; 2.º a la derecha hace zoom, a la
       izquierda vuelve al default. Al deshacer el zoom manda la P actual (puede
       ser una manual recien picada): ahi SI se re-ancla la ventana. */
    {
        double rel = csnhypodbp_view_x_to_rel(&g_view, x, g_margin_left, draw_width);
        CsnhypodbpAct act = csnhypodbp_view_click(&g_view, rel);

        if (csnhypodbp_view_reanchor_on(act))
            csnhypodbp_pick_anchor_refresh();
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
    pick_overlay_changed();
}

/* Puntero sobre el canvas: guarda la posicion para el indicador de picado y
   para la linea que sigue al puntero, y pone el cursor de cruz sobre las trazas. */
void on_canvas_motion(GtkEventControllerMotion *ctrl, double x, double y, gpointer data) {
    GtkWidget *w = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(ctrl));
    int moved = !g_cursor_valid || (int)x != (int)g_cursor_x || (int)y != (int)g_cursor_y;

    (void)data;
    g_cursor_x = x; g_cursor_y = y;
    g_cursor_valid = 1;
    if (w) gtk_widget_set_cursor_from_name(w, csnhypodbp_view_cursor(x >= g_margin_left));
    /* En modo picado el indicador sigue al puntero en las dos direcciones; en el
       resto basta con repintar cuando cambia de pixel. */
    if (canvas_global && (moved || g_click_mode == CSNHYPODBP_CLICK_PICK))
        ewgui_canvas_queue_draw(canvas_global);
}

/* El puntero sale del canvas: oculta el indicador y la linea. */
void on_canvas_leave(GtkEventControllerMotion *ctrl, gpointer data) {
    GtkWidget *w = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(ctrl));

    (void)data;
    if (w) gtk_widget_set_cursor_from_name(w, NULL);
    if (!g_cursor_valid) return;
    g_cursor_valid = 0;
    if (canvas_global) ewgui_canvas_queue_draw(canvas_global);
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

/* --- Ancla de la ventana del picking -------------------------------------- */

/* Indice en StaArray de una estacion, o -1. */
static int station_index_of(const char *sta)
{
    int i;

    if (!sta || !sta[0]) return -1;
    for (i = 0; i < NumEstaciones; i++)
        if (strcmp(StaArray[i].szStation, sta) == 0) return i;
    return -1;
}

/* Fila de la estacion seleccionada. `g_SortedNodes` son canales de UNA estacion,
 * asi que una sola fila vale para todos; la usan el dibujo y el ancla. */
static int selected_station_row(CsnhypodbpStaRow *row)
{
    CsnhypodbpTracePick wc[CSNHYPODBP_CAP_MAX];
    CsnhypodbpManualPick man[CSNHYPODBP_MANUAL_MAX];
    CsnhypodbpStaInput si;
    int i0, nwc, nman;

    memset(row, 0, sizeof(*row));
    if (g_NumSortedNodes <= 0) return 0;
    i0 = g_SortedNodes[0].idx;
    memset(&si, 0, sizeof(si));
    nwc = station_caps(StaArray[i0].szStation, wc, CSNHYPODBP_CAP_MAX);
    nman = csnhypodbp_manual_list(man, CSNHYPODBP_MANUAL_MAX);
    snprintf(si.sta, sizeof(si.sta), "%s", StaArray[i0].szStation);
    si.dist_deg = g_StaDist[i0];
    si.has_data = (bHasData[i0] == 1);
    si.caps = wc; si.ncap = nwc;
    si.theo_p = (g_StaTheoP[i0] > 1.0) ? g_StaTheoP[i0] : 0.0;
    return csnhypodbp_statable_build(row, 1, &si, 1, man, nman, resid_source()) == 1;
}

/* P de referencia de una estacion: la ganadora (M > A > T). Devuelve 1 si es una
 * P real y deja su tiempo en `*t`; 0 si no hay P (queda el respaldo). */
static int pick_ref_for_station(int i, double *t)
{
    CsnhypodbpTracePick wc[CSNHYPODBP_CAP_MAX];
    CsnhypodbpManualPick man[CSNHYPODBP_MANUAL_MAX];
    CsnhypodbpStaInput si;
    CsnhypodbpStaRow row;
    int nman;

    if (t) *t = selected_otime + 15.0;
    if (i < 0 || i >= NumEstaciones) return 0;
    memset(&si, 0, sizeof(si));
    memset(&row, 0, sizeof(row));
    si.dist_deg = g_StaDist[i];
    si.has_data = (bHasData[i] == 1);
    si.ncap = station_caps(StaArray[i].szStation, wc, CSNHYPODBP_CAP_MAX);
    si.caps = wc;
    si.theo_p = (g_StaTheoP[i] > 1.0) ? g_StaTheoP[i] : 0.0;
    snprintf(si.sta, sizeof(si.sta), "%s", StaArray[i].szStation);
    nman = csnhypodbp_manual_list(man, CSNHYPODBP_MANUAL_MAX);
    if (csnhypodbp_statable_build(&row, 1, &si, 1, man, nman, resid_source()) != 1)
        return 0;
    if (!row.p.present) return 0;
    if (t) *t = csnhypodbp_view_ref_pick(1, row.p.t, selected_otime + 15.0);
    return 1;
}

/* Re-ancla la ventana del picking a la P de referencia. Se llama al deshacer el
 * zoom, al cambiar de estacion/solucion/evento y al arrancar; NUNCA al picar (la
 * vista no se mueve y la marca aparece donde el usuario hizo clic). */
void csnhypodbp_pick_anchor_refresh(void)
{
    double t = selected_otime + 15.0;
    int have = pick_ref_for_station(station_index_of(g_selected_station), &t);

    g_win_ref_pick = t;
    g_win_ref_has_p = have;
}

void on_draw_signal(EwGuiCanvas *canvas, cairo_t *cr, int width, int height, void *data) {
    (void)data;
    cairo_set_source_rgb(cr, 1, 1, 1); cairo_paint(cr);

    int margin_right = 10;
    int draw_width = width - g_margin_left - margin_right;
    if (draw_width <= 0) return;

    /* (sin rotulo: el id del evento ya esta en la tabla de estaciones de al lado,
       asi que el canvas queda entero para las trazas) */

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
    int have_row;
    have_row = selected_station_row(&sta_row);

    /* Ventana del picking anclada a la P de REFERENCIA CONGELADA, no a la P
       ganadora: picar no mueve la vista (la marca nueva aparece donde el usuario
       hizo clic) y la referencia solo se refresca al deshacer el zoom o cambiar
       de estacion/solucion/evento. Si el ancla aun no tenia P (estacion recien
       cambiada, picadas sin llegar), se engancha en cuanto aparece. */
    if (g_win_ref_pick <= 0.0)
        csnhypodbp_pick_anchor_refresh();
    /* El enganche perezoso es solo para la P que LLEGA (automatica o teorica): una
       manual nunca mueve la ventana, ni siquiera si el ancla venia del respaldo. */
    if (!g_win_ref_has_p && have_row && sta_row.p.present && sta_row.p.t > 1.0 &&
        sta_row.p.origin != 'M') {
        g_win_ref_pick = csnhypodbp_view_ref_pick(1, sta_row.p.t, selected_otime + 15.0);
        g_win_ref_has_p = 1;
    }
    g_dWindowStart = g_win_ref_pick - g_align_lead;

    /* Ventana visible (zoom) y winStart COMUN de la estacion (para el eje UTC):
       todas las trazas comparten ventana y un unico eje UTC vale para todas. */
    double span = g_view.t1 - g_view.t0;
    if (span <= 0.0) span = g_dScreenTime;
    int sta_is_real = (have_row && sta_row.p.present &&
                       (sta_row.p.origin == 'M' || sta_row.p.origin == 'A'));
    double sta_win_start = g_win_ref_pick - g_align_lead;
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

        /* Etiqueta: solo la componente (HHZ/HHN/HHE), pegada a la tabla. */
        cairo_set_source_rgb(cr, 0.1, 0.2, 0.7);
        char sta_label[64];
        csnhypodbp_trace_label(sta_label, sizeof(sta_label),
                               StaArray[i].szStation, StaArray[i].szChannel, 0.0);
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, 12); cairo_move_to(cr, 8, y_center + 5); cairo_show_text(cr, sta_label);

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

        /* Marca P (una sola por estacion: M > A > T). Si el zoom la deja fuera de
           la ventana se dibuja pegada al borde con una flecha: nunca desaparece
           en silencio (era el motivo de "no aparece lo que pique"). */
        if (have_row && sta_row.p.present) {
            double rel = sta_row.p.t - winStart;
            int edge = 0;
            double xp = csnhypodbp_view_mark_x(&g_view, rel, g_margin_left, draw_width, &edge);
            char plabel[24];

            cairo_set_source_rgb(cr, g_color_p[0], g_color_p[1], g_color_p[2]);
            cairo_set_line_width(cr, 2);
            cairo_move_to(cr, xp, y_center - (row_h / 2.5));
            cairo_line_to(cr, xp, y_center + (row_h / 2.5));
            cairo_stroke(cr);
            cairo_set_font_size(cr, 11);
            cairo_move_to(cr, edge > 0 ? (double)(g_margin_left + draw_width - 52) : xp + 4,
                          y_center - (row_h / 3));
            if (sta_row.p.origin == 'T')
                snprintf(plabel, sizeof(plabel), "P(theo)%s",
                         edge < 0 ? "<" : (edge > 0 ? ">" : ""));
            else
                snprintf(plabel, sizeof(plabel), "P(%c)%s", sta_row.p.origin,
                         edge < 0 ? "<" : (edge > 0 ? ">" : ""));
            cairo_show_text(cr, plabel);
        }

        /* Marca S (una sola por estacion: M > A). Igual: al borde si el zoom la
           deja fuera de la ventana. */
        if (have_row && sta_row.s.present) {
            double rel = sta_row.s.t - winStart;
            int edge = 0;
            double xs = csnhypodbp_view_mark_x(&g_view, rel, g_margin_left, draw_width, &edge);
            char slab[24];

            cairo_set_source_rgb(cr, g_color_s[0], g_color_s[1], g_color_s[2]);
            cairo_set_line_width(cr, 1.5);
            cairo_move_to(cr, xs, y_center - (row_h / 2.5));
            cairo_line_to(cr, xs, y_center + (row_h / 2.5));
            cairo_stroke(cr);
            cairo_set_font_size(cr, 11);
            snprintf(slab, sizeof(slab), "S(%c)%s", sta_row.s.origin,
                     edge < 0 ? "<" : (edge > 0 ? ">" : ""));
            cairo_move_to(cr, edge > 0 ? (double)(g_margin_left + draw_width - 52) : xs + 4,
                          y_center + (row_h / 3));
            cairo_show_text(cr, slab);
        }
    }

    /* Linea vertical a trazos siguiendo al puntero (por debajo del ancla). */
    if (g_cursor_valid)
        draw_cursor_line(cr, g_cursor_x, height,
                         g_cursor_x >= g_margin_left && g_cursor_x <= width - margin_right);

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
