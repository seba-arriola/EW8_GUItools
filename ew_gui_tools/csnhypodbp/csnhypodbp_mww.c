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
#include "csnhypodbp_mwwerr.h"
#include "csnwphase.h"
#include "ewgui/db.h"
#include "ewgui/view.h"

#define MWW_PI 3.14159265358979323846

/* Claves de csnhypodbp.d que usa este panel (definidas en csnhypodbp_main.c). */
extern char MwwConfigFile[MAX_STR];
extern char MwwWorkDir[MAX_STR];

typedef struct {
    /* widgets */
    GtkWidget     *root;
    GtkWidget     *grid;       /* tabla de parametros (nombre|valor|fuente) */
    GtkWidget     *btn_calc;
    GtkWidget     *btn_cancel;
    GtkWidget     *btn_save;   /* guarda en la BD: origen elegido + Mww + tensor */
    GtkWidget     *seed;       /* GtkSpinButton: magnitud SEMILLA (banda + half-dur) */
    GtkWidget     *seed_note;  /* de donde salio la semilla */
    GtkWidget     *spinner;
    GtkWidget     *status;
    GtkWidget     *summary;
    GtkWidget     *view;       /* visor de la salida completa */
    GtkTextBuffer *buf;
    EwGuiCanvas   *ball;

    /* estado */
    int            running;
    int            saved;      /* 0 = nada, 1 = guardado, -1 = fallo al guardar */
    char           save_msg[320];
    double         seed_val;     /* semilla actual (la usa la corrida y la tabla) */
    int            seed_syncing; /* evita recursion al sincronizar el spin */
    long long      sig_seed;     /* firma de la semilla ya volcada a la tabla */
    char           last_err[256];/* error real del hilo (se muestra SIEMPRE) */
    int            abi_bad;      /* 1 = la libreria de fase W tiene otro layout */
    guint          tick_id;
    time_t         t_start;
    CsnwphaseCfg   cfg;
    int            have_cfg;
    CsnwphaseOut   out;
    int            have_out;
    unsigned       ver;        /* sube con cada resultado nuevo */

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

/*
 * Magnitud SEMILLA: es la que el motor usa para elegir la banda del filtro y la
 * half-duration (no es una compuerta: sin ella la fase W tambien corre). Sale de
 * la solucion elegida y, si esa no trae ninguna, del default del .d
 * (MinMagnitude). El usuario puede editarla en el panel.
 */
static void mww_seed_reset(void)
{
    double m = mww_selected_magnitude();
    const char *nota;

    if (m <= 0.0) {
        m = g_mww.have_cfg ? g_mww.cfg.min_magnitude : 6.0;
        nota = "semilla: default de config (MinMagnitude)";
    } else {
        nota = "semilla: de la solución elegida";
    }
    g_mww.seed_val = m;
    if (g_mww.seed) {
        g_mww.seed_syncing = 1;
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(g_mww.seed), m);
        g_mww.seed_syncing = 0;
    }
    if (g_mww.seed_note)
        gtk_label_set_text(GTK_LABEL(g_mww.seed_note), nota);
}

static void on_mww_seed_changed(GtkSpinButton *sp, gpointer data)
{
    (void)data;
    if (g_mww.seed_syncing)
        return;
    g_mww.seed_val = gtk_spin_button_get_value(sp);
    if (g_mww.seed_note)
        gtk_label_set_text(GTK_LABEL(g_mww.seed_note), "semilla: editada a mano");
    csnhypodbp_mww_update();   /* recalcula la tabla: la banda depende de ella */
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
    in->magnitude    = (g_mww.seed_val > 0.0) ? g_mww.seed_val
                      : (g_mww.have_cfg ? g_mww.cfg.min_magnitude : 6.0);

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

static void mww_params_fill(void)
{
    CsnwphaseInput in;
    CsnwphaseParamRow rows[CSNWPHASE_MAX_PARAMS];
    int n = 0, i;

    mww_grid_clear(g_mww.grid);
    if (!g_mww.have_cfg)
        return;
    mww_input_from_selection(&in);
    if (csnwphase_params_build(&g_mww.cfg, &in, rows, CSNWPHASE_MAX_PARAMS, &n) != 0 || n <= 0)
        return;

    for (i = 0; i < n; i++) {
        GtkWidget *a = gtk_label_new(rows[i].name);
        GtkWidget *b = gtk_label_new(rows[i].value);
        GtkWidget *c = gtk_label_new(rows[i].source);
        gtk_label_set_xalign(GTK_LABEL(a), 0.0f);
        gtk_label_set_xalign(GTK_LABEL(b), 1.0f);
        gtk_label_set_xalign(GTK_LABEL(c), 0.0f);
        gtk_widget_add_css_class(a, "mww-param-name");
        gtk_widget_add_css_class(b, "mww-param-value");
        gtk_widget_add_css_class(c, "mww-param-src");
        gtk_grid_attach(GTK_GRID(g_mww.grid), a, 0, i, 1, 1);
        gtk_grid_attach(GTK_GRID(g_mww.grid), b, 1, i, 1, 1);
        gtk_grid_attach(GTK_GRID(g_mww.grid), c, 2, i, 1, 1);
    }
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

    /* --- semilla y tabla de parametros --- */
    {
        long long ssig = (long long)llround(g_mww.seed_val * 100.0);

        if (sig != g_mww.sig) {
            /* Solucion nueva: la semilla vuelve a la de la fila (o al default). */
            g_mww.sig = sig;
            mww_seed_reset();
            ssig = (long long)llround(g_mww.seed_val * 100.0);
        }
        if (sig != g_mww.sig || ssig != g_mww.sig_seed) {
            g_mww.sig_seed = ssig;
            mww_params_fill();
        }
    }

    /* --- botones y estado --- */
    /* La magnitud NO es una compuerta: es la semilla de la banda (editable). */
    gtk_widget_set_sensitive(g_mww.btn_calc,
                             !g_mww.running && g_mww.have_cfg && !g_mww.abi_bad
                             && selected_id[0] != '\0' && selected_otime > 0.0);
    gtk_widget_set_sensitive(g_mww.btn_cancel, g_mww.running);
    gtk_widget_set_sensitive(g_mww.btn_save,
                             !g_mww.running && g_mww.have_out && g_mww.out.ok
                             && selected_id[0] != '\0' && csnhypodbp_db_ready());
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
    job->rc = csnwphase_run(&job->cfg, job->rundir, 0, job->err, sizeof job->err);

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
    job->cfg = g_mww.cfg;
    mww_input_from_selection(&job->in);
    snprintf(job->rundir, sizeof job->rundir, "%s", g_mww.rundir);

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
 * `Save to DB`: guarda LA SOLUCION QUE SE ESTA ANALIZANDO (sus parametros
 * hipocentrales ya viven en `origin`, asi que solo se enlaza por su origin_id)
 * mas lo que aporta la inversion de fase W: la magnitud (Mww) y el mecanismo
 * focal. El centroide NO se escribe: el origen es el de la solucion elegida.
 *
 * Los ids siguen la convencion de hypodb_ids.c: `<origin_id>/mag/<tipo>` y
 * `<origin_id>/tensor/<metodo>`, para que un re-guardado sea idempotente.
 */
static void on_mww_save(GtkButton *btn, gpointer data)
{
    EwGuiDb *db = csnhypodbp_db();
    EwGuiMagRow mag;
    EwGuiTensorRow ten;
    char mid[64], tid[64];

    (void)btn;
    (void)data;
    if (!db || !g_mww.have_out || !g_mww.out.ok || !selected_id[0]) {
        g_mww.saved = -1;
        snprintf(g_mww.save_msg, sizeof g_mww.save_msg,
                 "Save to DB: falta la BD o no hay solucion");
        csnhypodbp_mww_update();
        return;
    }

    memset(&mag, 0, sizeof mag);
    memset(&ten, 0, sizeof ten);
    snprintf(mid, sizeof mid, "%s/mag/Mww", selected_id);
    snprintf(tid, sizeof tid, "%s/tensor/WPHASE", selected_id);

    snprintf(mag.magnitude_id, sizeof mag.magnitude_id, "%s", mid);
    snprintf(mag.origin_id, sizeof mag.origin_id, "%s", selected_id);
    mag.magnitude_value = g_mww.out.mw;
    snprintf(mag.magnitude_type, sizeof mag.magnitude_type, "Mww");
    mag.mag_uncertainty = NAN;
    snprintf(mag.calculation_method, sizeof mag.calculation_method, "WPHASE");
    mag.station_count = g_mww.out.n_used;

    snprintf(ten.tensor_id, sizeof ten.tensor_id, "%s", tid);
    snprintf(ten.origin_id, sizeof ten.origin_id, "%s", selected_id);
    snprintf(ten.method, sizeof ten.method, "WPHASE");
    snprintf(ten.mechanism, sizeof ten.mechanism, "WCMT");
    ten.have_planes = g_mww.out.have_planes;
    ten.strike1 = g_mww.out.strike1; ten.dip1 = g_mww.out.dip1;
    ten.rake1   = g_mww.out.rake1;
    ten.strike2 = g_mww.out.strike2; ten.dip2 = g_mww.out.dip2;
    ten.rake2   = g_mww.out.rake2;
    ten.m_rr = g_mww.out.m_rr; ten.m_tt = g_mww.out.m_tt;
    ten.m_pp = g_mww.out.m_pp; ten.m_rt = g_mww.out.m_rt;
    ten.m_rp = g_mww.out.m_rp; ten.m_tp = g_mww.out.m_tp;
    ten.scalar_moment = g_mww.out.m0;

    if (ewguidb_upsert_magnitude(db, &mag) != 0 ||
        ewguidb_upsert_tensor(db, &ten) != 0) {
        g_mww.saved = -1;
        snprintf(g_mww.save_msg, sizeof g_mww.save_msg,
                 "Save to DB: FALLO (%s)", ewguidb_last_error(db));
    } else {
        g_mww.saved = 1;
        snprintf(g_mww.save_msg, sizeof g_mww.save_msg,
                 "Guardado en la BD: %s (Mw %.2f) + %s", mid, g_mww.out.mw, tid);
    }
    csnhypodbp_mww_update();
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
    GtkWidget *root, *row, *frame, *hb, *vbox;
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
    gtk_box_append(GTK_BOX(root), frame);

    /* 2. semilla de magnitud: decide la banda del filtro y la half-duration */
    hb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_append(GTK_BOX(hb), gtk_label_new("magnitud semilla (M):"));
    g_mww.seed = gtk_spin_button_new_with_range(0.0, 9.9, 0.1);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(g_mww.seed), 1);
    gtk_widget_set_tooltip_text(g_mww.seed,
        "El motor NO la usa como umbral: la usa para elegir la banda del filtro y "
        "la half-duration. Se precarga con la magnitud de la solución elegida (o "
        "MinMagnitude del .d) y se puede ajustar a mano.");
    g_signal_connect(g_mww.seed, "value-changed", G_CALLBACK(on_mww_seed_changed), NULL);
    g_mww.seed_note = gtk_label_new("");
    gtk_widget_add_css_class(g_mww.seed_note, "mww-seed-note");
    gtk_box_append(GTK_BOX(hb), g_mww.seed);
    gtk_box_append(GTK_BOX(hb), g_mww.seed_note);
    gtk_box_append(GTK_BOX(root), hb);

    /* 3. barra: Calculate / Cancel / estado */
    hb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    g_mww.btn_calc = gtk_button_new_with_label("Calculate");
    gtk_widget_add_css_class(g_mww.btn_calc, "suggested-action");
    gtk_widget_set_tooltip_text(g_mww.btn_calc,
        "Prepara y corre la inversión de fase W de la solución elegida "
        "(ondas del store/SDS + motor eost/wphase)");
    g_signal_connect(g_mww.btn_calc, "clicked", G_CALLBACK(on_mww_calculate), NULL);
    g_mww.btn_cancel = gtk_button_new_with_label("Cancel");
    gtk_widget_set_tooltip_text(g_mww.btn_cancel,
        "Descarta el resultado de la corrida en curso");
    g_signal_connect(g_mww.btn_cancel, "clicked", G_CALLBACK(on_mww_cancel), NULL);
    g_mww.btn_save = gtk_button_new_with_label("Save to DB");
    gtk_widget_set_tooltip_text(g_mww.btn_save,
        "Guarda la solución analizada (sus parámetros hipocentrales) con la "
        "magnitud Mww y el mecanismo focal de la fase W. El origen es el de la "
        "solución elegida, no el centroide de la inversión");
    g_signal_connect(g_mww.btn_save, "clicked", G_CALLBACK(on_mww_save), NULL);
    g_mww.spinner = gtk_spinner_new();
    gtk_widget_set_visible(g_mww.spinner, FALSE);
    g_mww.status = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(g_mww.status), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(g_mww.status), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(g_mww.status, TRUE);
    gtk_widget_add_css_class(g_mww.status, "mww-status");
    gtk_box_append(GTK_BOX(hb), g_mww.btn_calc);
    gtk_box_append(GTK_BOX(hb), g_mww.btn_cancel);
    gtk_box_append(GTK_BOX(hb), g_mww.btn_save);
    gtk_box_append(GTK_BOX(hb), g_mww.spinner);
    gtk_box_append(GTK_BOX(hb), g_mww.status);
    gtk_box_append(GTK_BOX(root), hb);

    /* 4. resumen + playera */
    row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    g_mww.summary = gtk_label_new("sin resultado");
    gtk_label_set_xalign(GTK_LABEL(g_mww.summary), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(g_mww.summary), TRUE);
    gtk_label_set_selectable(GTK_LABEL(g_mww.summary), TRUE);
    gtk_widget_set_hexpand(g_mww.summary, TRUE);
    gtk_widget_set_valign(g_mww.summary, GTK_ALIGN_CENTER);
    gtk_widget_add_css_class(g_mww.summary, "mww-summary");

    g_mww.ball = ewgui_canvas_new();
    ewgui_canvas_set_draw(g_mww.ball, on_draw_mww, &g_mww);
    vbox = ewgui_canvas_widget(g_mww.ball);
    /* Suelo pequeno (ver mww_scrolled): 120 px bastan para leer la playera. */
    gtk_widget_set_size_request(vbox, 120, 120);
    frame = gtk_frame_new("Mecanismo focal (WCMT)");
    gtk_frame_set_child(GTK_FRAME(frame), vbox);
    gtk_box_append(GTK_BOX(row), g_mww.summary);
    gtk_box_append(GTK_BOX(row), frame);
    gtk_box_append(GTK_BOX(root), row);

    /* 5. visor de la salida completa */
    g_mww.view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(g_mww.view), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(g_mww.view), TRUE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(g_mww.view), GTK_WRAP_NONE);
    gtk_widget_add_css_class(g_mww.view, "mww-output");
    g_mww.buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(g_mww.view));
    frame = gtk_frame_new("Salida completa del motor (fase W)");
    gtk_frame_set_child(GTK_FRAME(frame), mww_scrolled(g_mww.view, 90));
    gtk_widget_set_vexpand(frame, TRUE);
    gtk_box_append(GTK_BOX(root), frame);

    g_mww.root = root;
    mww_view_load(NULL);
    mww_seed_reset();
    csnhypodbp_mww_update();
    return root;
}
