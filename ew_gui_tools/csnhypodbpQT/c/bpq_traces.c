/*
 * bpq_traces.c - Canvas de trazas del panel de picking (cairo, sin GTK).
 *
 * Una banda horizontal por estacion, con la traza dibujada como envolvente
 * min/max por columna de pixel (rapido aunque la traza tenga 30 k muestras y el
 * panel 400 px) y como polilinea muestra a muestra en el zoom alto, donde la
 * envolvente degenera en escalones (mismo criterio que la GUI GTK). La etiqueta
 * de la estacion va a la izquierda y las marcas de P y S en su instante,
 * coloreadas por origen con csnhypodbp_color_resolve (manual / automatica /
 * teorica, con la derivacion del manual que ya trae ese nucleo).
 *
 * El eje de tiempos va en su propio canvas (BPQ_SCENE_TRACE_AXIS), igual que en
 * la GUI GTK, y comparte `t0`/`window` con el de trazas.
 */
#include "bpq_internal.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "csnhypodbp_trace.h"

/* Margen izquierdo reservado al canal en 27 px bold. */
static double left_margin(double dpr) { return 72.0 * dpr; }

/* Version publica para la UI y los tests: el gesto del eje tiene que usar la
 * MISMA geometria que el dibujo. */
double bpq_traces_left_margin(double dpr) { return left_margin(dpr); }

/* Alto de banda con `tracesPerScreen`: el nucleo puro decide cuantas bandas
 * caben en el viewport (y con que suelo), asi per_screen=1 da una traza a todo
 * el alto y per_screen=3 tres tercios. Publico: lo usan el hit-test y los tests,
 * y el llamador lo necesita para el scroll. */
int bpq_traces_band_h(const BpqTracesCtx *c, int h)
{
    const double dpr = (c && c->dpr > 0.0) ? c->dpr : 1.0;
    int per = (c && c->per_screen >= CSNHYPODBP_TRACES_PER_SCREEN_MIN &&
               c->per_screen <= CSNHYPODBP_TRACES_PER_SCREEN_MAX)
                  ? c->per_screen : 1;
    CsnhypodbpTracesLayout lay = csnhypodbp_traces_layout(
        c ? c->n : 0, per, (int)(h / dpr), CSNHYPODBP_TRACES_BAND_MIN);
    int rowh = (int)(lay.band_h * dpr + 0.5);

    if (rowh < (int)(14.0 * dpr)) rowh = (int)(14.0 * dpr);
    return rowh;
}

/* Cuantas filas caben de verdad (por si el viewport es mas chico que el suelo). */
static int rows_fit(const BpqTracesCtx *c, int h, int rowh, double dpr)
{
    int nvis;

    if (!c || rowh <= 0) return 0;
    nvis = (int)((h - 2.0 * dpr) / rowh);
    if (nvis > c->n) nvis = c->n;
    if (nvis < 1 && c->n > 0) nvis = 1;
    return nvis;
}

/* Primera fila visible: `scroll_row` acotado para que la ultima pagina quede
 * llena (si no, con 6 canales y 1 banda el scroll dejaria huecos al final). */
int bpq_traces_visible(const BpqTracesCtx *c, int h, int *nvis_out)
{
    const double dpr = (c && c->dpr > 0.0) ? c->dpr : 1.0;
    const int rowh = bpq_traces_band_h(c, h);
    int nvis, first;

    if (!c || c->n <= 0 || rowh <= 0) {
        if (nvis_out) *nvis_out = 0;
        return 0;
    }
    nvis = rows_fit(c, h, rowh, dpr);
    first = c->scroll_row;
    if (first > c->n - nvis) first = c->n - nvis;
    if (first < 0) first = 0;
    if (nvis_out) *nvis_out = nvis;
    return first;
}

/* Filas visibles: la UI las necesita para el pageStep de la barra. */
int bpq_traces_page_rows(const BpqTracesCtx *c, int h)
{
    int nvis = 0;

    (void)bpq_traces_visible(c, h, &nvis);
    return nvis;
}

/* Ultima fila que puede ser la primera sin dejar huecos al final. */
int bpq_traces_scroll_max(const BpqTracesCtx *c, int h)
{
    const int nvis = bpq_traces_page_rows(c, h);

    if (!c || c->n <= 0 || nvis <= 0) return 0;
    return (c->n > nvis) ? c->n - nvis : 0;
}

/* Scroll acotado; el llamador solo tiene que repintar. */
int bpq_traces_scroll_to(BpqTracesCtx *c, int h, int row)
{
    const int max = bpq_traces_scroll_max(c, h);

    if (!c) return 0;
    if (row < 0) row = 0;
    if (row > max) row = max;
    c->scroll_row = row;
    return row;
}

/* Muestra ausente (el store rellena con INT_MAX). */
static int is_gap(int32_t v) { return v >= INT32_MAX - 1; }

/* Traza sobre la ventana del contexto. Lee el buffer FILTRADO si la vista lo
 * pide; el crudo queda intacto para `locate`.
 *
 * Dos regimenes, con el mismo criterio que la GUI GTK
 * (csnhypodbp_gui.c:4198-4247):
 *   - spp < 2 (zoom alto): polilinea muestra a muestra. La envolvente degenera
 *     en segmentos de 1 px por columna (escalones) y la onda no se puede leer.
 *   - spp >= 2: banda rellena con el min/max de cada columna, con grosor minimo
 *     para que una zona plana siga dejando trazo.
 * La escala vertical es la de la VENTANA VISIBLE (no la de toda la traza): al
 * hacer zoom el detalle visible llena la fila en vez de quedar aplastado por un
 * maximo que esta fuera del encuadre. */
static void draw_trace(cairo_t *cr, const BpqTracesCtx *c, const BpqTrace *t,
                       double x0, double yc, double pw, double half)
{
    const int32_t *s = NULL;
    const double dpr = (c->dpr > 0.0) ? c->dpr : 1.0;
    double t_trace, rate, max_abs = 0.0, sc, gain, row_h, min_half;
    long n = 0, k0, k1, k;
    int ncol, x;

    t_trace = bpq_trace_t0(t);
    rate = bpq_trace_rate(t);
    if (!(rate > 0.0) || !(c->window > 0.0)) return;

    if (c->use_filtered) s = bpq_trace_filtered(t, &n);
    else (void)bpq_trace_data(t, &s, &n, NULL, NULL);
    if (!s || n <= 0) return;

    /* Rango visible (una muestra de holgura para que la polilinea entre y salga
     * del marco) y maximo |muestra| SOLO de ese rango. */
    k0 = (long)floor((c->t0 - t_trace) * rate) - 1;
    k1 = (long)ceil((c->t0 + c->window - t_trace) * rate) + 1;
    if (k1 < 0 || k0 >= n) return;
    if (k0 < 0) k0 = 0;
    if (k1 > n) k1 = n;
    if (k1 <= k0) return;

    for (k = k0; k < k1; k++) {
        const double v = fabs((double)s[k]);

        if (is_gap(s[k])) continue;
        if (v > max_abs) max_abs = v;
    }
    if (!(max_abs > 0.0)) return;   /* traza plana: el GTK tampoco pinta */

    /* Shift+Up/Down de la GTK: exageracion vertical de la vista. Con gain > 1
     * la traza se recorta contra la banda (el clip lo pone el llamador). */
    gain = (c->y_gain > 0.0) ? c->y_gain : 1.0;
    if (gain < 0.1) gain = 0.1;
    if (gain > 50.0) gain = 50.0;
    /* GTK: scale = row_h * 0.45 / max_abs * g_zoom_factor. `half` excluye
     * 3*dpr por lado del clip, asi que la fila completa es 2*half + 6*dpr. */
    row_h = 2.0 * half + 6.0 * dpr;
    sc = 0.45 * row_h * gain / max_abs;

    ncol = (int)pw;
    if (ncol < 1) ncol = 1;

    cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
    cairo_set_line_width(cr, 0.8 * dpr);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);

    if (csnhypodbp_view_use_polyline(c->window, ncol, rate)) {
        /* Zoom alto: un pixel cubre menos de 2 muestras y la envolvente deja
         * rectas de altura 0 (traza "a guiones"/escalones). Se une muestra a
         * muestra y se corta en los gaps, sin unir a traves del hueco. */
        int pen = 0;

        for (k = k0; k < k1; k++) {
            double x, y;

            if (is_gap(s[k])) {
                pen = 0;
                continue;
            }
            x = x0 + csnhypodbp_view_sample_x(t_trace + (double)k / rate,
                                              c->t0, c->window, ncol);
            y = yc - (double)s[k] * sc;
            if (pen) cairo_line_to(cr, x, y);
            else cairo_move_to(cr, x, y);
            pen = 1;
        }
        cairo_stroke(cr);
        return;
    }

    /* Envolvente: banda rellena por columnas con datos. Las columnas de un gap
     * no entran al path, asi que la banda se corta ahi; `min_half` (0.8 px
     * logicos) mantiene visible una zona plana (min == max). */
    min_half = csnhypodbp_view_min_half(sc, 0.8 * dpr);
    cairo_new_path(cr);
    for (x = 0; x < ncol; x++) {
        const double ta = c->t0 + ((double)x / ncol) * c->window;
        const double tb = c->t0 + ((double)(x + 1) / ncol) * c->window;
        long ka = (long)floor((ta - t_trace) * rate);
        long kb = (long)ceil((tb - t_trace) * rate);
        double vmin = 0.0, vmax = 0.0, ytop, ybot;
        int any = 0;

        if (kb < 0 || ka >= n) continue;
        if (ka < 0) ka = 0;
        if (kb > n - 1) kb = n - 1;
        if (kb < ka) kb = ka;

        for (k = ka; k <= kb; k++) {
            const double v = (double)s[k];

            if (is_gap(s[k])) continue;
            if (!any) {
                vmin = vmax = v;
                any = 1;
            } else {
                if (v < vmin) vmin = v;
                if (v > vmax) vmax = v;
            }
        }
        if (!any) continue;
        ytop = yc - (vmax + min_half) * sc;   /* positivo arriba */
        ybot = yc - (vmin - min_half) * sc;
        if (ybot <= ytop) continue;
        cairo_rectangle(cr, x0 + x, ytop, 1.0, ybot - ytop);
    }
    cairo_fill(cr);
}

/* Curva CF del STA/LTA normalizada a la banda de la fila + disparo estimado. */
static void draw_stalta(cairo_t *cr, const BpqTracesCtx *c, const BpqTrace *t,
                        const BpqStaLta *st, double x0, double yc, double pw,
                        double half, double dpr)
{
    const double *cf;
    double peak, t_trace, rate, trig;
    long n = 0, k, step;

    if (!st) return;
    t_trace = bpq_trace_t0(t);
    rate = bpq_trace_rate(t);
    if (!(rate > 0.0)) return;
    cf = bpq_stalta_cf(st, &n);
    if (!cf || n <= 0) return;

    peak = bpq_stalta_peak(st);
    if (!(peak > 0.0)) peak = 1.0;

    step = (long)fmax(1.0, (double)n / (pw * 2.0));
    cairo_set_source_rgba(cr, 0.55, 0.35, 0.75, 0.85);
    cairo_set_line_width(cr, 1.0 * dpr);
    for (k = 0; k < n; k += step) {
        const double ts = t_trace + (double)k / rate;
        const double xs = x0 + ((ts - c->t0) / c->window) * pw;
        const double ys = yc + half - (cf[k] / peak) * (2.0 * half);

        if (xs < x0 - 1.0 || xs > x0 + pw + 1.0) continue;
        if (k == 0) cairo_move_to(cr, xs, ys);
        else cairo_line_to(cr, xs, ys);
    }
    cairo_stroke(cr);

    trig = bpq_stalta_trigger_t(st);
    if (trig > 0.0) {
        const double xs = x0 + ((trig - c->t0) / c->window) * pw;
        const double dash[2] = { 3.0 * dpr, 3.0 * dpr };

        if (xs >= x0 && xs <= x0 + pw) {
            cairo_set_source_rgba(cr, 0.45, 0.20, 0.65, 0.9);
            cairo_set_dash(cr, dash, 2, 0.0);
            cairo_move_to(cr, xs, yc - half);
            cairo_line_to(cr, xs, yc + half);
            cairo_stroke(cr);
            cairo_set_dash(cr, NULL, 0, 0.0);
        }
    }
}

/* Marca de una picada, con su color por origen. */
static void draw_pick(cairo_t *cr, const BpqTracesCtx *c,
                      const CsnhypodbpStaPick *pk, char phase,
                      double x0, double ytop, double rowh, double pw, double dpr)
{
    const CsnhypodbpColorSet *set;
    CsnhypodbpPickOrigin origin;
    double rgb[3] = { 0.0, 0.0, 0.0 };
    double xs;
    char lbl[4];

    if (!pk->present) return;
    if (!(c->window > 0.0)) return;
    set = (phase == 'P') ? c->colors_p : c->colors_s;
    if (!set) return;

    xs = x0 + ((pk->t - c->t0) / c->window) * pw;
    if (xs < x0 - 1.0 || xs > x0 + pw + 1.0) return;

    origin = csnhypodbp_pick_origin(pk->origin);
    csnhypodbp_color_resolve(set, origin, rgb);

    cairo_set_source_rgb(cr, rgb[0], rgb[1], rgb[2]);
    cairo_set_line_width(cr, 1.2 * dpr);
    cairo_move_to(cr, xs + 0.5, ytop + 1.0 * dpr);
    cairo_line_to(cr, xs + 0.5, ytop + rowh - 1.0 * dpr);
    cairo_stroke(cr);

    /* Etiqueta de fase arriba de la marca. */
    snprintf(lbl, sizeof(lbl), "%c", phase);
    cairo_select_font_face(cr, "sans", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 8.0 * dpr);
    cairo_move_to(cr, xs + 2.0 * dpr, ytop + 8.0 * dpr);
    cairo_show_text(cr, lbl);
}

void bpq_traces_draw(cairo_t *cr, int w, int h, const BpqTracesCtx *c)
{
    const double dpr = (c && c->dpr > 0.0) ? c->dpr : 1.0;
    double mx, pw;
    int rowh, nvis, i;

    if (!cr) return;
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_paint(cr);
    if (!c || w <= 0 || h <= 0 || c->n <= 0 || c->rows == NULL) return;

    mx = left_margin(dpr);
    pw = w - mx - 6.0 * dpr;
    if (pw < 20.0) return;

    rowh = bpq_traces_band_h(c, h);
    if (rowh <= 0) return;
    nvis = rows_fit(c, h, rowh, dpr);
    {
        int first = bpq_traces_visible(c, h, NULL);
        cairo_select_font_face(cr, "sans", CAIRO_FONT_SLANT_NORMAL,
                               CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 9.0 * dpr);

        for (i = 0; i < nvis && first + i < c->n; i++) {
        const double ytop = 1.0 * dpr + i * rowh;
        const double yc = ytop + rowh / 2.0;
        const double half = (rowh - 6.0 * dpr) / 2.0;
        const CsnhypodbpStaRow *r = &c->rows[first + i];

        /* Fila elegida y separador. */
        if (first + i == c->selected) {
            cairo_set_source_rgba(cr, 0.20, 0.45, 0.85, 0.10);
            cairo_rectangle(cr, 0.0, ytop, (double)w, rowh);
            cairo_fill(cr);
        }
        cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.12);
        cairo_set_line_width(cr, 0.7 * dpr);
        cairo_move_to(cr, 0.0, ytop + rowh - 0.5 * dpr);
        cairo_line_to(cr, (double)w, ytop + rowh - 0.5 * dpr);
        cairo_stroke(cr);

        /* Etiqueta: SOLO el canal, en negro, negrita y 3x el tamano anterior.
         * La estacion ya la elige la tabla; la distancia vive en la tabla de
         * estaciones, no en el lienzo. */
        {
            char lbl[16];

            if (bpq_traces_band_label(c, first + i, lbl, sizeof(lbl))) {
                cairo_text_extents_t ext;

                cairo_select_font_face(cr, "sans", CAIRO_FONT_SLANT_NORMAL,
                                       CAIRO_FONT_WEIGHT_BOLD);
                cairo_set_font_size(cr, 27.0 * dpr);
                cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
                cairo_text_extents(cr, lbl, &ext);
                cairo_move_to(cr, 3.0 * dpr,
                              yc - (ext.y_bearing + ext.height / 2.0));
                cairo_show_text(cr, lbl);
            }
        }

        /* Traza (si ese canal tiene datos) y su STA/LTA, recortados a la banda
         * para que la exageracion vertical no invada la de al lado. */
        cairo_save(cr);
        cairo_rectangle(cr, mx, ytop, pw, rowh);
        cairo_clip(cr);
        if (c->tr && c->tr[first + i]) {
            draw_trace(cr, c, c->tr[first + i], mx, yc, pw, half);
            if (c->show_stalta && c->stalta && c->stalta[first + i])
                draw_stalta(cr, c, c->tr[first + i], c->stalta[first + i], mx,
                            yc, pw, half, dpr);
        }
        cairo_restore(cr);

        /* Picadas. */
        if (c->show_p) draw_pick(cr, c, &r->p, 'P', mx, ytop, rowh, pw, dpr);
        if (c->show_s) draw_pick(cr, c, &r->s, 'S', mx, ytop, rowh, pw, dpr);
        }
    }

    /* Ancla del zoom por dos clics: sin esto el segundo clic es a ciegas. */
    if (c->zoom_anchor_t > 0.0 && c->window > 0.0) {
        const double xs = mx + ((c->zoom_anchor_t - c->t0) / c->window) * pw;
        const double dash[2] = { 4.0 * dpr, 4.0 * dpr };

        if (xs >= mx && xs <= mx + pw) {
            cairo_set_source_rgba(cr, 0.10, 0.35, 0.85, 0.85);
            cairo_set_dash(cr, dash, 2, 0.0);
            cairo_set_line_width(cr, 1.2 * dpr);
            cairo_move_to(cr, xs, dpr);
            cairo_line_to(cr, xs, h - dpr);
            cairo_stroke(cr);
            cairo_set_dash(cr, NULL, 0, 0.0);
        }
    }

    /* Sin estaciones que mostrar. */
    if (c->n <= 0) {
        cairo_set_source_rgb(cr, 0.45, 0.45, 0.48);
        cairo_move_to(cr, 8.0 * dpr, h / 2.0);
        cairo_show_text(cr, "sin estaciones");
    }
}

void bpq_traces_zoom_span(double *t0, double *window, int dir)
{
    double c, w;

    if (!t0 || !window || !(*window > 0.0)) return;
    c = *t0 + *window / 2.0;
    w = *window;
    if (dir > 0) w *= 1.5;
    else if (dir < 0) w /= 1.5;
    else return;
    if (w < 0.1) w = 0.1;
    if (w > 86400.0) w = 86400.0;
    *window = w;
    *t0 = c - w / 2.0;
}

/* --- ventana con el raton -------------------------------------------------- */

double bpq_traces_time_at(const BpqTracesCtx *c, int w, double mx)
{
    const double dpr = (c && c->dpr > 0.0) ? c->dpr : 1.0;
    double x0, pw;

    if (!c || w <= 0 || !(c->window > 0.0)) return 0.0;
    x0 = left_margin(dpr);
    pw = w - x0 - 6.0 * dpr;
    if (pw < 20.0) return 0.0;
    if (mx < x0) mx = x0;
    if (mx > x0 + pw) mx = x0 + pw;
    return c->t0 + ((mx - x0) / pw) * c->window;
}

/* La ventana se acota a 1 s..1 h y el zoom conserva el instante que hay bajo el
 * cursor: si no, el pico que el usuario esta mirando se va de la pantalla. Se
 * usa la MISMA geometria que bpq_traces_hit (margen izquierdo y ancho de la
 * pista), no el ancho del widget. */
void bpq_traces_zoom_at(BpqTracesCtx *c, int w, double mx, double factor)
{
    const double dpr = (c && c->dpr > 0.0) ? c->dpr : 1.0;
    double x0, pw, t_at, win;

    if (!c || w <= 0 || !(factor > 0.0) || !(c->window > 0.0)) return;
    x0 = left_margin(dpr);
    pw = w - x0 - 6.0 * dpr;
    if (pw < 20.0) return;
    if (mx < x0) mx = x0;
    if (mx > x0 + pw) mx = x0 + pw;

    t_at = c->t0 + ((mx - x0) / pw) * c->window;
    win = c->window / factor;
    if (win < 1.0) win = 1.0;
    if (win > 3600.0) win = 3600.0;
    c->t0 = t_at - ((mx - x0) / pw) * win;
    c->window = win;
}

/* El contenido sigue al cursor: arrastrar a la derecha va hacia el pasado. */
void bpq_traces_pan(BpqTracesCtx *c, int w, double dx)
{
    const double dpr = (c && c->dpr > 0.0) ? c->dpr : 1.0;
    double x0, pw;

    if (!c || w <= 0 || !(c->window > 0.0)) return;
    x0 = left_margin(dpr);
    pw = w - x0 - 6.0 * dpr;
    if (pw < 20.0) return;
    c->t0 -= (dx / pw) * c->window;
}

/* Zoom al intervalo entre dos clics. El orden importa: x1 > x0 acerca (dos
 * clics de izquierda a derecha) y el llamador usa el orden inverso para
 * deshacer. */
int bpq_traces_zoom_range(BpqTracesCtx *c, int w, double x0, double x1)
{
    const double dpr = (c && c->dpr > 0.0) ? c->dpr : 1.0;
    double mx0, pw, t0, t1;

    if (!c || w <= 0 || !(c->window > 0.0)) return 0;
    mx0 = left_margin(dpr);
    pw = w - mx0 - 6.0 * dpr;
    if (pw < 20.0) return 0;
    if (x0 < mx0) x0 = mx0;
    if (x1 < mx0) x1 = mx0;
    if (x0 > mx0 + pw) x0 = mx0 + pw;
    if (x1 > mx0 + pw) x1 = mx0 + pw;

    t0 = c->t0 + ((x0 - mx0) / pw) * c->window;
    t1 = c->t0 + ((x1 - mx0) / pw) * c->window;
    if (t1 < t0) {
        const double tmp = t0;

        t0 = t1;
        t1 = tmp;
    }
    if (t1 - t0 < 0.5) return 0;          /* dos clics casi en el mismo pixel */

    c->t0 = t0;
    c->window = t1 - t0;
    if (c->window < 1.0) c->window = 1.0;
    if (c->window > 3600.0) c->window = 3600.0;
    return 1;
}

/* Fin del arrastre sobre el eje: izq->der = zoom al intervalo; der->izq =
 * reset a la vista original del evento. El umbral de 2 px evita que un clic
 * tembloroso cambie la ventana; el de 0.1 s es el mismo de la GTK. */
int bpq_traces_axis_release(BpqTracesCtx *c, int w, double x0, double x1,
                            double def_t0, double def_win)
{
    const double dpr = (c && c->dpr > 0.0) ? c->dpr : 1.0;
    double left, right, ta, tb;
    int action = BPQ_AXIS_NONE;

    if (!c || w <= 0) return BPQ_AXIS_NONE;
    c->rubber_active = 0;

    left = left_margin(dpr);
    right = (double)w - 6.0 * dpr;
    if (right - left < 20.0) return BPQ_AXIS_NONE;
    if (x0 < left) x0 = left;
    if (x1 < left) x1 = left;
    if (x0 > right) x0 = right;
    if (x1 > right) x1 = right;
    if (fabs(x1 - x0) < 2.0 * dpr) return BPQ_AXIS_NONE;

    ta = bpq_traces_time_at(c, w, x0);
    tb = bpq_traces_time_at(c, w, x1);
    if (ta <= 0.0 || tb <= 0.0 || fabs(tb - ta) < 0.1) return BPQ_AXIS_NONE;

    if (x1 > x0) {
        if (bpq_traces_zoom_range(c, w, x0, x1) == 1) {
            c->zoom_anchor_t = 0.0;
            action = BPQ_AXIS_ZOOM;
        }
    } else if (def_win > 0.0) {
        c->t0 = def_t0;
        c->window = def_win;
        c->zoom_anchor_t = 0.0;
        action = BPQ_AXIS_RESET;
    }
    return action;
}

int bpq_traces_band_label(const BpqTracesCtx *c, int i, char *out, size_t n)
{
    const char *ch;

    if (!out || n == 0) return 0;
    out[0] = '\0';
    if (!c || i < 0 || i >= c->n || !c->chan) return 0;
    ch = c->chan[i];
    if (!ch || !ch[0]) return 0;
    snprintf(out, n, "%s", ch);
    return 1;
}

int bpq_traces_hit(const BpqTracesCtx *c, int w, int h, double mx, double my,
                   int *row, double *t)
{
    const double dpr = (c && c->dpr > 0.0) ? c->dpr : 1.0;
    double x0, pw;
    int rowh, nvis, r;

    if (row) *row = -1;
    if (!c || !c->rows || c->n <= 0 || w <= 0 || h <= 0) return -1;
    if (!isfinite(mx) || !isfinite(my) || !(c->window > 0.0)) return -1;

    x0 = left_margin(dpr);
    pw = w - x0 - 6.0 * dpr;
    rowh = bpq_traces_band_h(c, h);
    nvis = rows_fit(c, h, rowh, dpr);
    {
        const int first = bpq_traces_visible(c, h, NULL);

        if (nvis <= 0 || rowh <= 0) return -1;
        /* Fuera del area de trazas (etiquetas a la izquierda o margenes). */
        if (mx < x0 || mx > x0 + pw) return -1;
        if (my < dpr) return -1;

        r = first + (int)((my - dpr) / rowh);
        if (r < first || r >= first + nvis || r >= c->n) return -1;
        if (row) *row = r;
    }
    if (t) *t = c->t0 + ((mx - x0) / pw) * c->window;
    return 0;
}

void bpq_traces_draw_axis(cairo_t *cr, int w, int h, const BpqTracesCtx *c)
{
    BpqAxisPaint p;
    const double dpr = (c && c->dpr > 0.0) ? c->dpr : 1.0;

    if (!cr) return;
    memset(&p, 0, sizeof(p));
    p.dpr = dpr;
    p.left = left_margin(dpr);
    p.right = (double)w - 6.0 * dpr;
    if (c) {
        p.t0 = c->t0;
        p.span = c->window;
        p.band = c->rubber_active;
        p.band_x0 = c->rubber_x0;
        p.band_x1 = c->rubber_x1;
    }
    bpq_axis_paint(cr, w, h, &p);
}
