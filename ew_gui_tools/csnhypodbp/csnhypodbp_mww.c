/*
 * csnhypodbp_mww.c — panel `Mww` (inversión de fase W) de la pestaña
 * *magnitudes*.
 *
 * Reparto: aquí vive SOLO la UI y el disparo; la preparación (ondas store/SDS,
 * respuestas, i_master/CMTSOLUTION, run.sh auditable), la ejecución del motor
 * vendorizado y el parseo del resultado los hace `libcsnwphase` (headless), el
 * mismo código que usa el CLI. Así la GUI no duplica ni un parámetro.
 *
 * Invariantes que respeta este archivo:
 *   - ON-DEMAND: nada corre hasta pulsar `Calculate`.
 *   - UN SOLO RENDER: `csnhypodbp_mww_update()` es el único escritor de los
 *     botones, los valores, el color del estado y la playera (mismo criterio
 *     que `csnhypodbp_preview_update()`).
 *   - El resultado pesado se calcula en un hilo y vuelve al hilo de GTK con
 *     g_idle_add (patrón de `locate_thread`/`locate_finish`).
 */

#include <gtk/gtk.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "csnhypodbp.h"
#include "csnhypodbp_mww.h"
#include "csnhypodbp_mww_over.h"
#include "csnhypodbp_mwwerr.h"
#include "csnwphase.h"
#include "ewgui/db.h"
#include "ewgui/db_ids.h"
#include "ewgui/view.h"

#define MWW_PI 3.14159265358979323846

/* Alto de la fila "resultados | salida completa": el mismo que tenia el bloque
 * de resultados (playera 120x120 + margen) +50%, que es lo que se pidio para los
 * DOS cuadros. Es un valor FIJO, no un vexpand: el alto sobrante es para la tabla
 * de parametros. */
#define MWW_ROW_H 248

/* Claves de csnhypodbp.d que usa este panel (definidas en csnhypodbp_main.c). */
extern char MwwConfigFile[MAX_STR];
extern char MwwWorkDir[MAX_STR];

typedef struct {
    /* widgets */
    GtkWidget     *root;
    GtkWidget     *grid;       /* tabla de parametros (nombre|valor|fuente) */
    GtkWidget     *btn_calc;
    GtkWidget     *btn_cancel;
    GtkWidget     *spinner;
    GtkWidget     *status;
    GtkWidget     *summary;
    GtkWidget     *view;       /* visor de la salida completa */
    GtkTextBuffer *buf;
    EwGuiCanvas   *ball;
    GtkWidget     *val_w[CSNWPHASE_MAX_PARAMS]; /* celda de valor de cada fila */
    int            n_rows;

    /* estado */
    int            running;
    int            saved;      /* 0 = nada, 1 = guardado, -1 = fallo al guardar */
    char           save_msg[320];
    /* Overrides editados en la MISMA tabla (ver csnhypodbp_mww_over.h): la
     * tabla, el i_master y el run.sh salen de la misma fuente. */
    MwwOverrides   ov;
    char           last_err[256];/* error real del hilo (se muestra SIEMPRE) */
    int            abi_bad;      /* 1 = la libreria de fase W tiene otro layout */
    guint          tick_id;
    time_t         t_start;
    CsnwphaseCfg   cfg;
    int            have_cfg;
    CsnwphaseOut   out;
    int            have_out;
    unsigned       ver;        /* sube con cada resultado nuevo */
    /* Identidad de la corrida vigente: si el usuario cambia de solucion en
     * *Origins*, el resultado viejo NO se le atribuye (lo lee el preview). */
    int            run_qid;
    int            run_mod;
    char           run_origin_id[64];

    /* firma de la solucion mostrada (para no reconstruir la tabla sin cambios) */
    long long      sig;

    /* cache de la playera */
    cairo_surface_t *ball_surf;
    int              ball_size;
    unsigned         ball_ver;

    char           rundir[1024];
    char           log_path[1100];
} MwwUi;

static MwwUi g_mww;

typedef struct {
    CsnwphaseCfg   cfg;
    CsnwphaseInput in;
    CsnwphaseOut   out;
    char           rundir[1024];
    char           err[256];
    int            rc;
    int            discard;    /* 1 = el usuario pulso Cancel */
} MwwJob;

/* ------------------------------------------------------------------ */
/* Entrada: la solucion elegida en *Origins*                            */
/* ------------------------------------------------------------------ */

static double mww_selected_magnitude(void)
{
    /* La magnitud de la solucion NO esta en los globals selected_*: vive en las
     * columnas nmag0..3 de la fila (ML Mwp Mb Ms, -1 = sin valor). La semilla
     * de la fase W es la mayor disponible (un M>7 suele tener solo Mwp/Ms). */
    static const char *props[4] = { "nmag0", "nmag1", "nmag2", "nmag3" };
    GObject *row = (GObject *)gtk_single_selection_get_selected_item(g_selection_detail);
    double best = -1.0;
    int i;

    if (!row)
        return -1.0;
    for (i = 0; i < 4; i++) {
        double v = -1.0;
        g_object_get(row, props[i], &v, NULL);
        if (v > best)
            best = v;
    }
    return best;
}

static void mww_input_from_selection(CsnwphaseInput *in);

/*
 * Magnitud SEMILLA: es la que el motor usa para elegir la banda del filtro y la
 * half-duration (no es una compuerta: sin ella la fase W tambien corre). Sale de
 * la solucion elegida y, si esa no trae ninguna, del default del .d
 * (MinMagnitude); el usuario la edita en la PROPIA tabla de parametros.
 */

/*
 * Config+hipocentro de la corrida: la solucion elegida (globals `selected_*`) y
 * el .d, con los overrides de la tabla encima. Es la unica fuente de la tabla,
 * del i_master y del run.sh.
 */
static void mww_run_config(CsnwphaseInput *in, CsnwphaseCfg *cfg)
{
    mww_input_from_selection(in);
    *cfg = g_mww.cfg;
    mww_over_apply(&g_mww.ov, in, cfg);
}

/* Solucion nueva: la tabla vuelve a los valores de la solucion y del .d. */
static void mww_over_reset(void)
{
    CsnwphaseInput in;

    mww_input_from_selection(&in);
    mww_over_init(&in, &g_mww.cfg, &g_mww.ov);
}

/*
 * La banda de inversion es lo UNICO derivado de la magnitud que se muestra: al
 * editar la magnitud se recalcula su celda sin reconstruir la tabla (reconstruir
 * dentro del callback destruiria el widget que se esta editando).
 */
static void mww_refresh_band(void)
{
    CsnwphaseInput in;
    CsnwphaseCfg cfg;
    CsnwphaseParamRow rows[CSNWPHASE_MAX_PARAMS];
    int n = 0, i;

    if (!g_mww.grid)
        return;
    mww_run_config(&in, &cfg);
    if (csnwphase_params_build(&cfg, &in, rows, CSNWPHASE_MAX_PARAMS, &n) != 0)
        return;
    for (i = 0; i < n && i < g_mww.n_rows; i++) {
        if (strcmp(rows[i].name, "banda de inversion [Hz]") == 0 && g_mww.val_w[i])
            gtk_label_set_text(GTK_LABEL(g_mww.val_w[i]), rows[i].value);
    }
}

/* Celda numerica (caja de texto): la corrida solo se mueve si el texto es un
 * numero valido y dentro del rango del campo (mismo criterio que el t0). Un
 * texto a medias o fuera de rango NO se aplica y ademas DESTOCA el campo, para
 * que la corrida use el valor de la solucion/`.d` y nunca un prefijo tecleado. */
static void on_mww_cell_changed(GtkEditable *ed, gpointer data)
{
    int field = GPOINTER_TO_INT(data);
    GtkWidget *w = GTK_WIDGET(ed);
    double v = 0.0;

    if (mww_field_parse(field, gtk_editable_get_text(ed), &v) != 0) {
        mww_over_unset(&g_mww.ov, field);
        gtk_widget_add_css_class(w, "mww-cell-bad");
        return;
    }
    gtk_widget_remove_css_class(w, "mww-cell-bad");
    if (mww_over_set(&g_mww.ov, field, v) != 0)
        return;
    if (field == CSNWPHASE_PF_MAGNITUDE)
        mww_refresh_band();
    csnhypodbp_mww_update();
}

/* Celda del t0: se aplica SOLO si el texto es una fecha valida (misma regla que
 * `mww_over_set_time`); si no, la celda se marca y la corrida sigue con la hora
 * de la solucion (nunca un valor a medias). */
static void on_mww_time_changed(GtkEditable *ed, gpointer data)
{
    const char *txt = gtk_editable_get_text(ed);
    GtkWidget *w = GTK_WIDGET(ed);

    (void)data;
    if (mww_over_set_time(&g_mww.ov, txt) != 0) {
        gtk_widget_add_css_class(w, "mww-cell-bad");
        return;
    }
    gtk_widget_remove_css_class(w, "mww-cell-bad");
    csnhypodbp_mww_update();
}

static void mww_input_from_selection(CsnwphaseInput *in)
{
    time_t t;

    memset(in, 0, sizeof *in);
    t = (time_t)selected_otime;
    {
        struct tm tm;
        gmtime_r(&t, &tm);
        in->year   = tm.tm_year + 1900;
        in->month  = tm.tm_mon + 1;
        in->day    = tm.tm_mday;
        in->hour   = tm.tm_hour;
        in->minute = tm.tm_min;
        in->second = fmod(selected_otime, 60.0);   /* con fraccion */
    }
    in->origin_epoch = selected_otime;
    in->latitude     = selected_lat;
    in->longitude    = selected_lon;
    in->depth_km     = selected_depth;
    {
        double m = mww_selected_magnitude();

        in->magnitude = (m > 0.0) ? m
                      : (g_mww.have_cfg ? g_mww.cfg.min_magnitude : 6.0);
    }

    if (selected_id[0])
        snprintf(in->event_id, sizeof in->event_id, "%s", selected_id);
    else
        snprintf(in->event_id, sizeof in->event_id, "qid%d", selected_qid);
    snprintf(in->location, sizeof in->location, "%s", in->event_id);
    snprintf(in->origin_id, sizeof in->origin_id, "%d/%d/%d",
             selected_qid, selected_mod, selected_version);
}

/* Firma de la solucion: solo para no repoblar la tabla de parametros de gusto. */
static long long mww_signature(void)
{
    long long s = (long long)selected_qid * 1000003LL + selected_mod * 10007LL
                + selected_version * 101LL + (long long)llround(selected_otime);
    s = s * 31 + (long long)llround(selected_lat * 1e4);
    s = s * 31 + (long long)llround(selected_lon * 1e4);
    s = s * 31 + (long long)llround(selected_depth * 1e2);
    s = s * 31 + (long long)llround(mww_selected_magnitude() * 1e2);
    return s;
}

/* ------------------------------------------------------------------ */
/* Playera (mecanismo focal) — proyeccion ortografica del hemisferio   */
/* inferior, signo de la radiacion P del tensor de momento.            */
/* ------------------------------------------------------------------ */

static void mww_ball_cache_free(void)
{
    if (g_mww.ball_surf) {
        cairo_surface_destroy(g_mww.ball_surf);
        g_mww.ball_surf = NULL;
    }
    g_mww.ball_size = 0;
}

static void mww_ball_build(int n)
{
    cairo_surface_t *surf;
    uint32_t *px;
    int stride, x, y;
    double R;
    const CsnwphaseOut *o = &g_mww.out;

    mww_ball_cache_free();
    if (n < 40 || !o->have_mt)
        return;

    surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, n, n);
    if (cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surf);
        return;
    }
    px = (uint32_t *)cairo_image_surface_get_data(surf);
    stride = cairo_image_surface_get_stride(surf) / 4;
    R = n / 2.0 - 1.0;

    for (y = 0; y < n; y++) {
        for (x = 0; x < n; x++) {
            double dx = (x + 0.5 - n / 2.0) / R;        /* + = este  */
            double dy = (n / 2.0 - (y + 0.5)) / R;      /* + = norte */
            double r  = sqrt(dx * dx + dy * dy);
            double az, ci, ur, ut, up, amp;
            uint32_t c;

            if (r > 1.0) {
                px[y * stride + x] = 0x00000000;        /* fuera del circulo */
                continue;
            }
            if (r > 1.0)
                r = 1.0;
            ci = sqrt(1.0 - r * r);                     /* cos(incidencia) */
            az = atan2(dx, dy);                         /* azimut desde el norte */
            /* Vector unitario (r=arriba, t=sur, p=este): la radiacion P se
             * evalua en la direccion de salida (hemisferio inferior). */
            ur = -ci;
            ut = -r * cos(az);
            up =  r * sin(az);
            amp = o->m_rr * ur * ur + o->m_tt * ut * ut + o->m_pp * up * up
                + 2.0 * o->m_rt * ur * ut + 2.0 * o->m_rp * ur * up
                + 2.0 * o->m_tp * ut * up;

            /* Compresion (>0) sombreada, dilatacion en blanco. */
            c = (amp > 0.0) ? 0xFF334466u : 0xFFFFFFFFu;
            px[y * stride + x] = c;
        }
    }
    cairo_surface_mark_dirty(surf);
    g_mww.ball_surf = surf;
    g_mww.ball_size = n;
    g_mww.ball_ver  = g_mww.ver;
}

static void on_draw_mww(EwGuiCanvas *canvas, cairo_t *cr, int w, int h, void *ud)
{
    MwwUi *u = (MwwUi *)ud;
    int n = (w < h ? w : h) - 6;
    double cx, cy;

    (void)canvas;
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_paint(cr);

    if (n < 40)
        return;
    if (!u->have_out || !u->out.have_mt) {
        cairo_set_source_rgb(cr, 0.45, 0.45, 0.45);
        cairo_select_font_face(cr, "sans", CAIRO_FONT_SLANT_NORMAL,
                               CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 12.0);
        cairo_move_to(cr, 8.0, h / 2.0);
        cairo_show_text(cr, u->have_out ? "sin mecanismo focal" : "sin resultado");
        return;
    }

    if (!u->ball_surf || u->ball_size != n || u->ball_ver != u->ver)
        mww_ball_build(n);
    if (!u->ball_surf)
        return;

    cx = (w - n) / 2.0 + n / 2.0;
    cy = (h - n) / 2.0 + n / 2.0;
    cairo_save(cr);
    cairo_arc(cr, cx, cy, n / 2.0 - 1.0, 0.0, 2.0 * MWW_PI);
    cairo_clip(cr);
    cairo_set_source_surface(cr, u->ball_surf, (w - n) / 2.0, (h - n) / 2.0);
    cairo_paint(cr);
    cairo_restore(cr);
    cairo_set_source_rgb(cr, 0.2, 0.2, 0.2);
    cairo_set_line_width(cr, 1.0);
    cairo_arc(cr, cx, cy, n / 2.0 - 1.0, 0.0, 2.0 * MWW_PI);
    cairo_stroke(cr);
}

/* ------------------------------------------------------------------ */
/* Helpers de UI                                                        */
/* ------------------------------------------------------------------ */

static void mww_grid_clear(GtkWidget *grid)
{
    GtkWidget *c;
    while ((c = gtk_widget_get_first_child(grid)) != NULL)
        gtk_grid_remove(GTK_GRID(grid), c);
}

static char *mww_read_file(const char *path, long *len_out)
{
    FILE *f = fopen(path, "rb");
    char *buf;
    long n;

    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    n = ftell(f);
    if (n < 0) { fclose(f); return NULL; }
    rewind(f);
    buf = malloc((size_t)n + 1);
    if (!buf) { fclose(f); return NULL; }
    if (n > 0 && fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        fclose(f);
        return NULL;
    }
    buf[n] = '\0';
    fclose(f);
    if (len_out)
        *len_out = n;
    return buf;
}

static void mww_view_load(const char *path)
{
    long n = 0;
    char *txt = path ? mww_read_file(path, &n) : NULL;

    if (txt) {
        gtk_text_buffer_set_text(g_mww.buf, txt, -1);
        free(txt);
    } else {
        gtk_text_buffer_set_text(g_mww.buf,
            "(sin salida del motor todavia)\n\n"
            "Pulsa `Calculate` con una solucion seleccionada en la pestana *Eventos*.\n"
            "Aqui aparece la salida COMPLETA de la inversion de fase W (comandos,\n"
            "i_master, CMTSOLUTION, WCMTSOLUTION, los LOG/wpinversion.log, o_wpinversion\n"
            "y el grid search si se activo).\n", -1);
    }
}

/* ------------------------------------------------------------------ */
/* Render unico                                                         */
/* ------------------------------------------------------------------ */

/*
 * Celda de valor de una fila: etiqueta de solo lectura, o la CAJA DE TEXTO del
 * campo editable (`CSNWPHASE_PF_*`). El valor inicial sale de la MISMA tabla que
 * se ejecuta (no de una copia aparte) y el rango/parseo de cada campo los define
 * el nucleo puro (`mww_field_limits`/`mww_field_parse`), asi que aqui no queda
 * ningun numero escrito a mano. Sin `max-width-chars`: en esta tabla las cajas
 * se quieren anchas (GTK4 da 150 px de ancho natural a todo `GtkEntry`).
 */
static GtkWidget *mww_cell_new(const CsnwphaseParamRow *r)
{
    GtkWidget *w;

    if (r->field == CSNWPHASE_PF_NONE) {
        w = gtk_label_new(r->value);
        gtk_label_set_xalign(GTK_LABEL(w), 1.0f);
        gtk_widget_add_css_class(w, "mww-param-value");
        return w;
    }

    if (r->field == CSNWPHASE_PF_ORIGIN_TIME) {
        w = gtk_entry_new();
        gtk_editable_set_text(GTK_EDITABLE(w), r->value);
        gtk_widget_set_hexpand(w, FALSE);
        gtk_widget_set_tooltip_text(w, "t0 UTC de la inversión (el de la "
            "solución elegida; editable antes de lanzar el cálculo)");
        g_signal_connect(w, "changed", G_CALLBACK(on_mww_time_changed), NULL);
        gtk_widget_add_css_class(w, "mww-param-cell");
        return w;
    }

    if (mww_field_limits(r->field, NULL, NULL) != 0)   /* campo no numerico */
        return gtk_label_new(r->value);

    w = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(w), r->value);
    gtk_entry_set_input_purpose(GTK_ENTRY(w), GTK_INPUT_PURPOSE_NUMBER);
    gtk_widget_set_hexpand(w, FALSE);
    gtk_widget_add_css_class(w, "mww-param-cell");
    /* Se conecta DESPUES de fijar el valor: si no, la propia precarga dispararia
     * el callback y marcaria el campo como editado a mano. */
    g_signal_connect(w, "changed", G_CALLBACK(on_mww_cell_changed),
                     GINT_TO_POINTER(r->field));
    return w;
}

static void mww_params_fill(void)
{
    CsnwphaseInput in;
    CsnwphaseCfg cfg;
    CsnwphaseParamRow rows[CSNWPHASE_MAX_PARAMS];
    int n = 0, i;

    mww_grid_clear(g_mww.grid);
    g_mww.n_rows = 0;
    for (i = 0; i < CSNWPHASE_MAX_PARAMS; i++)
        g_mww.val_w[i] = NULL;
    if (!g_mww.have_cfg)
        return;
    mww_run_config(&in, &cfg);
    if (csnwphase_params_build(&cfg, &in, rows, CSNWPHASE_MAX_PARAMS, &n) != 0 || n <= 0)
        return;

    for (i = 0; i < n; i++) {
        GtkWidget *a = gtk_label_new(rows[i].name);
        GtkWidget *b = mww_cell_new(&rows[i]);
        GtkWidget *c = gtk_label_new(rows[i].source);

        gtk_label_set_xalign(GTK_LABEL(a), 0.0f);
        gtk_label_set_xalign(GTK_LABEL(c), 0.0f);
        gtk_widget_set_valign(a, GTK_ALIGN_CENTER);
        gtk_widget_set_valign(b, GTK_ALIGN_CENTER);
        gtk_widget_set_valign(c, GTK_ALIGN_CENTER);
        gtk_widget_add_css_class(a, "mww-param-name");
        gtk_widget_add_css_class(c, "mww-param-src");
        gtk_grid_attach(GTK_GRID(g_mww.grid), a, 0, i, 1, 1);
        gtk_grid_attach(GTK_GRID(g_mww.grid), b, 1, i, 1, 1);
        gtk_grid_attach(GTK_GRID(g_mww.grid), c, 2, i, 1, 1);
        g_mww.val_w[i] = b;
    }
    g_mww.n_rows = n;
}

/*
 * Motivo real del fallo, en orden de fidelidad: lo que reporto el hilo de trabajo,
 * lo que dijo el parser y, si no hay nada, la primera linea de error del propio log
 * del motor (`run.sh` abortado solo deja un rc generico).
 */
static void mww_why(char *out, size_t n)
{
    if (g_mww.last_err[0]) {
        snprintf(out, n, "%s", g_mww.last_err);
        return;
    }
    if (g_mww.out.err[0]) {
        snprintf(out, n, "%s", g_mww.out.err);
        return;
    }
    if (csnhypodbp_mww_first_error(g_mww.rundir, out, n) == 0)
        return;
    snprintf(out, n, "revisa la salida completa");
}

void csnhypodbp_mww_update(void)
{
    char txt[1200];
    long long sig = mww_signature();
    if (!g_mww.root)
        return;   /* el panel todavia no se construyo (seleccion previa) */

    /* --- tabla de parametros --- */
    {
        if (sig != g_mww.sig) {
            /* Solucion nueva: la tabla vuelve a la solucion y al .d (se
             * descartan los ajustes a mano, que son de la solucion anterior). */
            g_mww.sig = sig;
            mww_over_reset();
            mww_params_fill();
        }
    }

    /* --- botones y estado --- */
    /* La magnitud NO es una compuerta: es la semilla de la banda (editable). */
    gtk_widget_set_sensitive(g_mww.btn_calc,
                             !g_mww.running && g_mww.have_cfg && !g_mww.abi_bad
                             && selected_id[0] != '\0' && selected_otime > 0.0);
    gtk_widget_set_sensitive(g_mww.btn_cancel, g_mww.running);
    if (g_mww.running) {
        gtk_spinner_start(GTK_SPINNER(g_mww.spinner));
        gtk_widget_set_visible(g_mww.spinner, TRUE);
        snprintf(txt, sizeof txt, "Corriendo… (%s)", g_mww.rundir);
        gtk_label_set_text(GTK_LABEL(g_mww.status), txt);
        gtk_widget_add_css_class(g_mww.status, "mww-busy");
        gtk_widget_remove_css_class(g_mww.status, "mww-error");
        gtk_widget_remove_css_class(g_mww.status, "mww-ok");
    } else {
        gtk_spinner_stop(GTK_SPINNER(g_mww.spinner));
        gtk_widget_set_visible(g_mww.spinner, FALSE);
        /* Las clases se limpian ANTES de decidir: si no, un color viejo
           sobrevive al cambio de estado. */
        gtk_widget_remove_css_class(g_mww.status, "mww-ok");
        gtk_widget_remove_css_class(g_mww.status, "mww-error");
        gtk_widget_remove_css_class(g_mww.status, "mww-busy");
        if (!g_mww.have_cfg) {
            snprintf(txt, sizeof txt,
                     "Falta config: no se pudo leer MwwConfigFile <%s>", MwwConfigFile);
            gtk_label_set_text(GTK_LABEL(g_mww.status), txt);
            gtk_widget_add_css_class(g_mww.status, "mww-error");
        } else if (g_mww.abi_bad) {
            gtk_label_set_text(GTK_LABEL(g_mww.status),
                "Librería de fase W incompatible (objetos desfasados): "
                "recompila con `make clean && make`");
            gtk_widget_add_css_class(g_mww.status, "mww-error");
        } else if (selected_id[0] == '\0' || selected_otime <= 0.0) {
            gtk_label_set_text(GTK_LABEL(g_mww.status),
                               "Selecciona una solucion en *Eventos* para calcular Mww");
        } else if (!g_mww.have_out) {
            gtk_label_set_text(GTK_LABEL(g_mww.status), "Listo (sin resultado todavia)");
        } else if (g_mww.out.ok && g_mww.saved != 0 && g_mww.save_msg[0]) {
            gtk_label_set_text(GTK_LABEL(g_mww.status), g_mww.save_msg);
            gtk_widget_add_css_class(g_mww.status,
                                     g_mww.saved > 0 ? "mww-ok" : "mww-error");
        } else if (g_mww.out.ok) {
            snprintf(txt, sizeof txt, "OK: Mww %.2f — %s", g_mww.out.mw, g_mww.rundir);
            gtk_label_set_text(GTK_LABEL(g_mww.status), txt);
            gtk_widget_add_css_class(g_mww.status, "mww-ok");
        } else {
            /* Motivo real: lo que reporto el hilo, lo que dijo el parser o la
               primera linea de error del log del motor (un `run.sh` abortado solo
               deja un rc generico: asi nunca queda un "sin resultado" mudo). */
            char why[512];

            mww_why(why, sizeof why);
            snprintf(txt, sizeof txt, "Sin solucion: %s", why);
            gtk_label_set_text(GTK_LABEL(g_mww.status), txt);
            gtk_widget_add_css_class(g_mww.status, "mww-error");
        }
    }

    /* --- resumen --- */
    if (g_mww.have_out && g_mww.out.ok) {
        const CsnwphaseOut *o = &g_mww.out;
        char planes[160] = "";
        if (o->have_planes)
            snprintf(planes, sizeof planes,
                     "planos nodales: (%.0f/%.0f/%.0f) y (%.0f/%.0f/%.0f)",
                     o->strike1, o->dip1, o->rake1, o->strike2, o->dip2, o->rake2);
        snprintf(txt, sizeof txt,
                 "Mw %.2f   M0 %.3e N·m%s\n"
                 "centroide: %.3f, %.3f  %.1f km   (shift %.1f s, half-dur %.1f s)\n"
                 "%s\n"
                 "estaciones usadas: %d   RMS global: %.4g",
                 o->mw, o->m0, o->converged ? "" : "  (sin Wmag en el log)",
                 o->lat, o->lon, o->depth_km, o->t_shift_s, o->half_dur_s,
                 planes[0] ? planes : "planos nodales: (no declarados)",
                 o->n_used, o->global_rms);
        gtk_label_set_text(GTK_LABEL(g_mww.summary), txt);
    } else if (g_mww.have_out) {
        char why[512];

        mww_why(why, sizeof why);
        snprintf(txt, sizeof txt, "sin resultado\n%s", why);
        gtk_label_set_text(GTK_LABEL(g_mww.summary), txt);
    } else {
        gtk_label_set_text(GTK_LABEL(g_mww.summary),
                           "sin resultado: pulsa `Calculate`");
    }

    ewgui_canvas_queue_draw(g_mww.ball);
}

/*
 * Resultado de la última inversión, si corresponde a la solución (qid, mod).
 *
 * El panel NO borra el resultado al cambiar de selección, así que hay que
 * comprobar a qué solución pertenece: sin esto el preview le atribuiría a una
 * solución el Mww calculado para otra. La identidad es (qid, mod) — el MISMO
 * evento y el mismo localizador — porque la corrida se dispara desde la solución
 * elegida y el candidato del `locate` es otra solución de ese evento (otra
 * versión y otra hora de origen). NULL si no hay resultado o no es de esa
 * solución.
 */
const CsnwphaseOut *csnhypodbp_mww_result_for(int qid, int mod)
{
    if (!g_mww.have_out || !g_mww.out.ok)
        return NULL;
    if (g_mww.run_qid != qid || g_mww.run_mod != mod)
        return NULL;
    return &g_mww.out;
}

void csnhypodbp_mww_report(int ok, const char *msg)
{
    g_mww.saved = ok ? 1 : -1;
    snprintf(g_mww.save_msg, sizeof g_mww.save_msg, "%s", msg ? msg : "");
    csnhypodbp_mww_update();
}

/* ------------------------------------------------------------------ */
/* Hilo de trabajo                                                      */
/* ------------------------------------------------------------------ */
static gboolean mww_finish(gpointer data)
{
    MwwJob *job = (MwwJob *)data;

    if (g_mww.tick_id) {
        g_source_remove(g_mww.tick_id);
        g_mww.tick_id = 0;
    }
    g_mww.running = 0;

    if (!job->discard) {
        g_mww.out = job->out;
        g_mww.have_out = 1;
        g_mww.ver++;
        /* El error REAL del hilo (preparacion, ondas, motor) se guarda SIEMPRE:
           antes se liberaba sin mostrarse y el panel solo decia "sin resultado". */
        snprintf(g_mww.last_err, sizeof g_mww.last_err, "%s", job->err);
        mww_view_load(g_mww.log_path);
    }
    csnhypodbp_mww_update();
    g_free(job);
    return G_SOURCE_REMOVE;
}

static gpointer mww_thread(gpointer data)
{
    MwwJob *job = (MwwJob *)data;
    int n_sta = 0, n_tr = 0;

    /* La ventana de ondas ([t0-win_pre, t0+win_post]) la aplica input_prep
     * desde el .d; aqui solo se orquesta, igual que el CLI. */
    job->rc = csnwphase_prep_write(&job->cfg, &job->in, job->rundir,
                                   job->err, sizeof job->err);
    if (job->rc != 0)
        goto done;

    /* Guard anti-borrado de la base de funciones de Green. */
    {
        char gerr[256] = "";
        if (csnwphase_gfdir_guard(&job->cfg, job->rundir, gerr, sizeof gerr) != 0) {
            snprintf(job->err, sizeof job->err, "%s", gerr);
            job->rc = -1;
            goto done;
        }
    }

    job->rc = csnwphase_input_prep(&job->cfg, &job->in, job->rundir,
                                   &n_sta, &n_tr, job->err, sizeof job->err);
    if (job->rc != 0)
        goto done;

    /* `run.sh` lo escribe csnwphase_run() DENTRO del run dir; llamar aqui a
       run_script con un nombre relativo lo dejaba en el CWD del proceso. */
    job->rc = csnwphase_run(&job->cfg, job->rundir, 0, job->in.magnitude,
                            job->err, sizeof job->err);

    /* La solucion y la salida completa se capturan aunque la corrida falle. */
    csnwphase_parse(job->rundir, &job->out);
    csnwphase_log_write(job->rundir, g_mww.log_path);
    if (job->rc != 0 && job->err[0] == '\0' && job->out.err[0])
        snprintf(job->err, sizeof job->err, "%s", job->out.err);

done:
    g_idle_add(mww_finish, job);
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Callbacks                                                            */
/* ------------------------------------------------------------------ */

static gboolean mww_tick(gpointer data)
{
    char txt[1200];
    long el;

    (void)data;
    if (!g_mww.running) {
        g_mww.tick_id = 0;
        return G_SOURCE_REMOVE;
    }
    el = (long)(time(NULL) - g_mww.t_start);
    snprintf(txt, sizeof txt, "Corriendo… %ld s  (%s)", el, g_mww.rundir);
    gtk_label_set_text(GTK_LABEL(g_mww.status), txt);
    return G_SOURCE_CONTINUE;
}

static void on_mww_calculate(GtkButton *btn, gpointer data)
{
    MwwJob *job;
    char path[512];
    time_t now;
    struct tm tm;

    (void)btn;
    (void)data;
    if (g_mww.running)
        return;

    if (!g_mww.have_cfg) {
        /* Reintenta leer el .d por si aparecio despues de arrancar. */
        csnwphase_resolve_path(path, sizeof path, MwwConfigFile);
        g_mww.have_cfg = (csnwphase_cfg_load(&g_mww.cfg, path) == 0);
        if (!g_mww.have_cfg)
            g_mww.have_cfg = (csnwphase_cfg_load(&g_mww.cfg, MwwConfigFile) == 0);
        if (!g_mww.have_cfg) {
            csnhypodbp_mww_update();
            return;
        }
    }

    now = time(NULL);
    gmtime_r(&now, &tm);
    snprintf(g_mww.rundir, sizeof g_mww.rundir, "%s/%s-%04d%02d%02dT%02d%02d%02d",
             MwwWorkDir[0] ? MwwWorkDir : "/tmp/csnhypodbp_mww",
             selected_id[0] ? selected_id : "mww",
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec);
    snprintf(g_mww.log_path, sizeof g_mww.log_path, "%s/full_output.txt", g_mww.rundir);

    job = g_new0(MwwJob, 1);
    /* Config + hipocentro con los overrides de la tabla: lo que se ve es lo que
     * corre (la tabla se construye de la misma fuente). */
    mww_run_config(&job->in, &job->cfg);
    snprintf(job->rundir, sizeof job->rundir, "%s", g_mww.rundir);
    /* Identidad de esta corrida: el preview solo muestra/persiste su Mww si sigue
     * correspondiendo a la solucion confirmada (mismo evento y localizador). */
    g_mww.run_qid = selected_qid;
    g_mww.run_mod = selected_mod;
    snprintf(g_mww.run_origin_id, sizeof g_mww.run_origin_id, "%s",
             selected_origin_id);

    g_mww.log_path[0] = '\0';
    snprintf(g_mww.log_path, sizeof g_mww.log_path, "%s/full_output.txt", g_mww.rundir);
    gtk_text_buffer_set_text(g_mww.buf, "", -1);
    g_mww.saved = 0;
    g_mww.save_msg[0] = '\0';
    g_mww.last_err[0] = '\0';
    g_mww.running = 1;
    g_mww.t_start = now;
    csnhypodbp_mww_update();
    g_mww.tick_id = g_timeout_add(1000, mww_tick, NULL);

    g_thread_unref(g_thread_new("mww", mww_thread, job));
}

static void on_mww_cancel(GtkButton *btn, gpointer data)
{
    (void)btn;
    (void)data;
    if (!g_mww.running)
        return;
    /* No se puede matar al hijo del motor desde aqui sin arriesgar procesos
     * ajenos (el TiempoMax de la corrida ya la acota): Cancel descarta el
     * resultado en curso y vuelve a mostrar el ultimo valido. */
    g_mww.running = 0;
    if (g_mww.tick_id) {
        g_source_remove(g_mww.tick_id);
        g_mww.tick_id = 0;
    }
    csnhypodbp_mww_update();
    gtk_label_set_text(GTK_LABEL(g_mww.status),
                       "Cancelado: se descarta el resultado de la corrida en curso");
}

/*
 * Persistencia del resultado de la fase W sobre UNA solución: la magnitud (Mww)
 * y el mecanismo focal (tensor). La llama el `Confirm` del cuadro de la
 * candidata (`on_preview_confirm`): ya NO hay botón propio en el panel, porque el
 * mecanismo se guarda al confirmar la solución (el usuario marca el radio `Mww`).
 *
 * El origen es el de la SOLUCIÓN (no el centroide de la inversión). El ARC
 * confirmado no pasa por ningún anillo, así que hypodb no escribe ese origen: si
 * no está en la base se CREA (evento + origen) con los ids de `ewgui/db_ids.h` —
 * los mismos que usa el ingester, así que un re-guardado o el propio ingester
 * caen en la MISMA fila. Si el origen ya existe no se pisa (solo se le añaden la
 * magnitud y el tensor).
 *
 * Idempotente: `<origin_id>/mag/Mww` y `<origin_id>/tensor/WPHASE`.
 */
static int mww_perr(char *err, size_t errlen, const char *msg)
{
    if (err && errlen)
        snprintf(err, errlen, "%s", msg);
    return -1;
}

/*
 * Copia acotada con precision explicita: el destino tiene el mismo tamano que la
 * fuente (ids de 64), asi que `snprintf(dst, n, "%s", src)` dispara el aviso de
 * truncado de gcc aunque los ids nunca lo hagan (las funciones de `db_ids`
 * devuelven -1 si no caben).
 */
static void mww_copy(char *dst, size_t n, const char *src)
{
    if (!dst || n == 0)
        return;
    snprintf(dst, n, "%.*s", (int)(n - 1), src ? src : "");
}

/*
 * Ids de BD de una solución. Si la solución es la elegida en *Origins* y esa fila
 * ya trae su `origin_id` (vino del historial), se usa ESE; si no (candidata recién
 * localizada, o el ARC confirmado), se derivan del qid + hora UTC + mod.
 */
static int mww_db_ids(const CsnhypodbpSolution *sol, char *oid, size_t no,
                      char *eid, size_t ne)
{
    char qid[24];

    if (!sol || !oid || !eid || no == 0 || ne == 0)
        return -1;

    if (selected_origin_id[0] && sol->qid == selected_qid &&
        sol->mod == selected_mod && sol->version == selected_version) {
        const char *colon = strrchr(selected_origin_id, ':');
        size_t len;

        if (!colon)
            return -1;
        len = (size_t)(colon - selected_origin_id);
        if (len == 0 || len + 1 > ne)
            return -1;
        memcpy(eid, selected_origin_id, len);
        eid[len] = '\0';
        snprintf(oid, no, "%s", selected_origin_id);
        return 0;
    }

    snprintf(qid, sizeof qid, "%d", sol->qid);
    if (ewgui_event_id(eid, ne, qid, sol->otime) != 0)
        return -1;
    return ewgui_origin_id(oid, no, eid, sol->mod);
}

int csnhypodbp_mww_persist(const CsnhypodbpSolution *sol, const CsnwphaseOut *out,
                           char *err, size_t errlen)
{
    EwGuiDb *db = csnhypodbp_db();
    EwGuiEventRow ev;
    EwGuiOriginRow ori;
    EwGuiMagRow mag;
    EwGuiTensorRow ten;
    char eid[64] = "", oid[64] = "", mid[128], tid[128];

    if (err && errlen)
        err[0] = '\0';
    if (!db || !csnhypodbp_db_ready())
        return mww_perr(err, errlen, "no hay conexion con la base hipocentral");
    if (!sol || !out || !out->ok)
        return mww_perr(err, errlen, "sin resultado de la inversion de fase W");
    if (mww_db_ids(sol, oid, sizeof oid, eid, sizeof eid) != 0)
        return mww_perr(err, errlen, "no se pudieron componer los ids del origen");
    if (ewgui_magnitude_id(mid, sizeof mid, oid, "Mww") != 0 ||
        ewgui_tensor_id(tid, sizeof tid, oid, "WPHASE") != 0)
        return mww_perr(err, errlen, "id de magnitud/tensor demasiado largo");

    /* 1. evento + origen (solo si faltan). */
    memset(&ev, 0, sizeof ev);
    mww_copy(ev.event_id, sizeof ev.event_id, eid);
    snprintf(ev.event_type, sizeof ev.event_type, "EARTHQUAKE");
    if (ewguidb_upsert_event(db, &ev) != 0)
        return mww_perr(err, errlen, ewguidb_last_error(db));

    memset(&ori, 0, sizeof ori);
    mww_copy(ori.origin_id, sizeof ori.origin_id, oid);
    mww_copy(ori.event_id, sizeof ori.event_id, eid);
    ewgui_epoch_to_iso(sol->otime, ori.origin_time, sizeof ori.origin_time);
    ori.latitude = sol->lat;
    ori.longitude = sol->lon;
    ori.depth = sol->depth;
    snprintf(ori.author, sizeof ori.author, "%s", "csnhypodbp");
    snprintf(ori.software, sizeof ori.software, "%s", ewgui_software());
    snprintf(ori.location_algorithm, sizeof ori.location_algorithm, "%s",
             ewgui_mod_algorithm(sol->mod));
    ori.azimuthal_gap = (sol->gap > 0) ? (double)sol->gap : NAN;
    ori.station_count = sol->nph;
    ori.standard_error = (sol->rms > 0.0) ? sol->rms : NAN;
    /* Confirmada a mano => REVIEWED (mismo criterio que hypodb con el ARC manual). */
    snprintf(ori.status, sizeof ori.status, "%s", ewgui_mod_status(sol->mod, 1));
    if (ewguidb_upsert_origin_if_absent(db, &ori) != 0)
        return mww_perr(err, errlen, ewguidb_last_error(db));

    /* 2. lo que aporta la fase W: magnitud y mecanismo focal. */
    memset(&mag, 0, sizeof mag);
    mww_copy(mag.magnitude_id, sizeof mag.magnitude_id, mid);
    mww_copy(mag.origin_id, sizeof mag.origin_id, oid);
    mag.magnitude_value = out->mw;
    snprintf(mag.magnitude_type, sizeof mag.magnitude_type, "%s", "Mww");
    mag.mag_uncertainty = NAN;
    snprintf(mag.calculation_method, sizeof mag.calculation_method, "%s", "WPHASE");
    mag.station_count = out->n_used;

    memset(&ten, 0, sizeof ten);
    mww_copy(ten.tensor_id, sizeof ten.tensor_id, tid);
    mww_copy(ten.origin_id, sizeof ten.origin_id, oid);
    snprintf(ten.method, sizeof ten.method, "%s", "WPHASE");
    snprintf(ten.mechanism, sizeof ten.mechanism, "%s", "WCMT");
    ten.have_planes = out->have_planes;
    ten.strike1 = out->strike1;
    ten.dip1 = out->dip1;
    ten.rake1 = out->rake1;
    ten.strike2 = out->strike2;
    ten.dip2 = out->dip2;
    ten.rake2 = out->rake2;
    ten.m_rr = out->m_rr;
    ten.m_tt = out->m_tt;
    ten.m_pp = out->m_pp;
    ten.m_rt = out->m_rt;
    ten.m_rp = out->m_rp;
    ten.m_tp = out->m_tp;
    ten.scalar_moment = out->m0;

    if (ewguidb_upsert_magnitude(db, &mag) != 0 ||
        ewguidb_upsert_tensor(db, &ten) != 0)
        return mww_perr(err, errlen, ewguidb_last_error(db));

    /* 3. la solución confirmada pasa a ser la preferida del evento, con su Mww. */
    if (ewguidb_set_preferred(db, eid, oid, mid) != 0)
        return mww_perr(err, errlen, ewguidb_last_error(db));

    return 0;
}

/* ------------------------------------------------------------------ */
/* Construccion del panel                                               */
/* ------------------------------------------------------------------ */

/* El alto es un SUELO PEQUENO, no un minimo grande: cada pixel de minimo se SUMA
   al de la ventana y, si el total supera la geometria maximizada que da el
   compositor, GTK adjunta un buffer mas grande que el configurado y Wayland corta
   la conexion (`Error 71` al maximizar; ver AGENTS §7). El reparto fino lo hace
   `vexpand` (la salida del motor se lleva el alto sobrante). */
static GtkWidget *mww_scrolled(GtkWidget *child, int h)
{
    GtkWidget *sw = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(sw), h);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw), child);
    return sw;
}

GtkWidget *csnhypodbp_mww_panel_new(void)
{
    GtkWidget *root, *row, *frame, *hb, *vbox, *sw;
    char path[512];

    memset(&g_mww, 0, sizeof g_mww);

    csnwphase_resolve_path(path, sizeof path, MwwConfigFile);
    g_mww.have_cfg = (csnwphase_cfg_load(&g_mww.cfg, path) == 0);
    if (!g_mww.have_cfg)
        g_mww.have_cfg = (csnwphase_cfg_load(&g_mww.cfg, MwwConfigFile) == 0);

    /* Guardia de ABI: si este modulo se compilo contra un `csnwphase.h` mas viejo
       que el de la libreria, los structs tienen otro layout y la libreria lee
       memoria ajena (el i_master salia con los `int` del evento leidos como
       `double` y la fase W no arrancaba). Se compara con NUESTROS tamanos. */
    g_mww.abi_bad = (csnwphase_abi_check(sizeof(CsnwphaseCfg),
                                         sizeof(CsnwphaseInput)) != 0);

    root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_margin_top(root, 6);
    gtk_widget_set_margin_bottom(root, 6);
    gtk_widget_set_margin_start(root, 6);
    gtk_widget_set_margin_end(root, 6);

    /* 1. tabla de parametros de la inversion */
    g_mww.grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(g_mww.grid), 14);
    gtk_grid_set_row_spacing(GTK_GRID(g_mww.grid), 2);
    frame = gtk_frame_new("Parámetros de la inversión de fase W");
    gtk_frame_set_child(GTK_FRAME(frame), mww_scrolled(g_mww.grid, 90));
    /* La tabla es lo que el usuario ajusta antes de calcular: se lleva el alto
     * sobrante (los resultados y la salida tienen alto fijo, ver MWW_ROW_H). */
    gtk_widget_set_vexpand(frame, TRUE);
    gtk_box_append(GTK_BOX(root), frame);

    /* 2. barra: Calculate / Cancel / estado */
    hb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    g_mww.btn_calc = gtk_button_new_with_label("Calculate");
    gtk_widget_add_css_class(g_mww.btn_calc, "suggested-action");
    gtk_widget_set_tooltip_text(g_mww.btn_calc,
        "Prepara y corre la inversión de fase W de la solución elegida "
        "(ondas del store/SDS + motor eost/wphase) con los parámetros de la tabla");
    g_signal_connect(g_mww.btn_calc, "clicked", G_CALLBACK(on_mww_calculate), NULL);
    g_mww.btn_cancel = gtk_button_new_with_label("Cancel");
    gtk_widget_set_tooltip_text(g_mww.btn_cancel,
        "Descarta el resultado de la corrida en curso");
    g_signal_connect(g_mww.btn_cancel, "clicked", G_CALLBACK(on_mww_cancel), NULL);
    g_mww.spinner = gtk_spinner_new();
    gtk_widget_set_visible(g_mww.spinner, FALSE);
    g_mww.status = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(g_mww.status), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(g_mww.status), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(g_mww.status, TRUE);
    gtk_widget_add_css_class(g_mww.status, "mww-status");
    gtk_box_append(GTK_BOX(hb), g_mww.btn_calc);
    gtk_box_append(GTK_BOX(hb), g_mww.btn_cancel);
    gtk_box_append(GTK_BOX(hb), g_mww.spinner);
    gtk_box_append(GTK_BOX(hb), g_mww.status);
    gtk_box_append(GTK_BOX(root), hb);

    /* 3. resultados (resumen + playera) y salida completa, UNO AL LADO DEL OTRO, con
     *    la misma altura y en proporcion 1/3 - 2/3: un grid de columnas HOMOGENEAS
     *    reparte el ancho ASIGNADO por igual entre sus tres columnas no vacias, asi
     *    que el cuadro de resultados (1 columna) se queda con un tercio y la salida
     *    (2 columnas) con dos tercios, y el reparto aguanta el redimensionado. El
     *    alto sobrante se lo lleva la tabla de parametros (es lo que el usuario
     *    modifica antes de lanzar el calculo).
     *
     *    OJO: el alto de `MWW_ROW_H` lo cobran los DOS frames con `vexpand`. Un grid
     *    NO reparte entre sus filas el alto que sus hijos no piden (el sobrante se
     *    queda vacio al fondo): sin el `vexpand` de los frames, subir `MWW_ROW_H`
     *    solo movia el bloque hacia arriba y los cuadros seguian con su alto natural
     *    (~165 px: la playera de 120 + margen), dejando el hueco abajo. */
    row = gtk_grid_new();
    gtk_grid_set_column_homogeneous(GTK_GRID(row), TRUE);
    /* Una sola fila: con `row-homogeneous` el bloque se lleva TODO el alto del
     * grid (y el `vexpand` de los frames, el de la fila). */
    gtk_grid_set_row_homogeneous(GTK_GRID(row), TRUE);
    gtk_grid_set_column_spacing(GTK_GRID(row), 10);
    gtk_widget_set_size_request(row, -1, MWW_ROW_H);
    gtk_widget_set_vexpand(row, FALSE);

    g_mww.summary = gtk_label_new("sin resultado");
    gtk_label_set_xalign(GTK_LABEL(g_mww.summary), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(g_mww.summary), TRUE);
    gtk_label_set_selectable(GTK_LABEL(g_mww.summary), TRUE);
    /* El resumen NO se estira: con `hexpand` el texto se quedaba a la izquierda y
       la playera al fondo del cuadro, con un hueco en medio. El ancho sobrante se
       lo lleva la salida del motor (el frame de la derecha). */
    gtk_widget_set_hexpand(g_mww.summary, FALSE);
    gtk_widget_set_valign(g_mww.summary, GTK_ALIGN_CENTER);
    gtk_widget_add_css_class(g_mww.summary, "mww-summary");

    g_mww.ball = ewgui_canvas_new();
    ewgui_canvas_set_draw(g_mww.ball, on_draw_mww, &g_mww);
    vbox = ewgui_canvas_widget(g_mww.ball);
    /* Suelo pequeno (ver mww_scrolled): 120 px bastan para leer la playera. */
    gtk_widget_set_size_request(vbox, 120, 120);

    hb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_append(GTK_BOX(hb), g_mww.summary);
    gtk_box_append(GTK_BOX(hb), vbox);
    /* Se lleva el alto de la fila (ver el comentario del grid). */
    frame = gtk_frame_new("Resultados de la inversión (WCMT)");
    gtk_widget_set_vexpand(frame, TRUE);
    /* El cuadro de resultados se queda con su tercio: su scrolled window no propaga
       el ancho natural del texto (`EXTERNAL`) y lo acota con un minimo, para que el
       resumen se ajuste por lineas dentro del tercio en vez de sacar una barra
       horizontal, y para que el minimo del grid no se multiplique por tres
       (AGENTS §7: el minimo de la ventana tiene que caber en el maximizado). */
    sw = mww_scrolled(hb, 90);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
                                   GTK_POLICY_EXTERNAL, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(sw), 90);
    gtk_frame_set_child(GTK_FRAME(frame), sw);
    gtk_grid_attach(GTK_GRID(row), frame, 0, 0, 1, 1);

    g_mww.view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(g_mww.view), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(g_mww.view), TRUE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(g_mww.view), GTK_WRAP_NONE);
    gtk_widget_add_css_class(g_mww.view, "mww-output");
    g_mww.buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(g_mww.view));
    /* Se lleva el alto de la fila (ver el comentario del grid). */
    frame = gtk_frame_new("Salida completa del motor (fase W)");
    gtk_widget_set_vexpand(frame, TRUE);
    gtk_frame_set_child(GTK_FRAME(frame), mww_scrolled(g_mww.view, 90));
    /* Las columnas 1 y 2 del grid: dos tercios del ancho. */
    gtk_grid_attach(GTK_GRID(row), frame, 1, 0, 2, 1);

    gtk_box_append(GTK_BOX(root), row);

    g_mww.root = root;
    mww_view_load(NULL);
    csnhypodbp_mww_update();
    return root;
}
