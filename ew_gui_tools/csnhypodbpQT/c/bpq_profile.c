/*
 * bpq_profile.c - Panel de PERFIL del prototipo (cairo, sin GTK).
 *
 * Port de on_draw_map_profile (csnhypodbp_gui.c:3274-3401) sin globales:
 * marco + ejes + rejilla, subduccion (slab) y topografia como polilineas, y las
 * soluciones del evento sobre el mismo marco (la elegida en verde, al final).
 *
 * Convencion de datos (la del nucleo): en el eje y va **-profundidad**, asi que
 * mas profundo queda mas abajo. Los valores del slab ya vienen negativos; a las
 * soluciones hay que pasarles -depth.
 */
#include "bpq_internal.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "csnhypodbp_mapview.h"
#include "csnhypodbp_profile.h"

struct BpqProfileData {
    CsnhypodbpProfile     p;
    CsnhypodbpProfileLine slab;
    CsnhypodbpProfileLine topo;
    int                   have_slab;
    int                   have_topo;
};

/* --- carga ---------------------------------------------------------------- */

BpqProfileData *bpq_profile_load(const BpqConfig *cfg, int index,
                                 char *err, size_t errn)
{
    BpqProfileData *d;
    char e[256] = "";

    if (err && errn) err[0] = '\0';
    if (!cfg || index < 0 || index >= cfg->profiles.n) {
        if (err) snprintf(err, errn, "perfil %d fuera de rango (hay %d)",
                          index, cfg ? cfg->profiles.n : 0);
        return NULL;
    }
    d = (BpqProfileData *)calloc(1, sizeof(*d));
    if (!d) {
        if (err) snprintf(err, errn, "sin memoria para el perfil");
        return NULL;
    }
    d->p = cfg->profiles.p[index];

    if (csnhypodbp_profile_line_read(d->p.slab_file, cfg->profile_lon_col,
                                     cfg->profile_val_col, &d->slab,
                                     e, sizeof(e)) == 0) {
        d->have_slab = 1;
    } else if (err) {
        snprintf(err, errn, "slab <%s>: %s", d->p.slab_file, e);
    }
    if (csnhypodbp_profile_line_read(d->p.topo_file, cfg->profile_lon_col,
                                     cfg->profile_val_col, &d->topo,
                                     e, sizeof(e)) == 0) {
        d->have_topo = 1;
    } else if (err && !err[0]) {
        snprintf(err, errn, "topo <%s>: %s", d->p.topo_file, e);
    }

    if (!d->have_slab && !d->have_topo) {
        bpq_profile_free(d);
        return NULL;
    }
    return d;
}

void bpq_profile_free(BpqProfileData *p)
{
    if (!p) return;
    if (p->have_slab) csnhypodbp_profile_line_free(&p->slab);
    if (p->have_topo) csnhypodbp_profile_line_free(&p->topo);
    free(p);
}

const char *bpq_profile_name(const BpqProfileData *p)
{
    return p ? p->p.name : "";
}

int bpq_profile_line_n(const BpqProfileData *p)
{
    return (p && p->have_slab) ? p->slab.n : 0;
}

int bpq_profile_count(const BpqConfig *cfg)
{
    return cfg ? cfg->profiles.n : 0;
}

int bpq_profile_nearest(const BpqConfig *cfg, double lat, double lon)
{
    if (!cfg) return -1;
    return csnhypodbp_profile_nearest(&cfg->profiles, lat, lon);
}

/* --- dibujo --------------------------------------------------------------- */

static void marker_rgb(int kind, int selected, double *r, double *g, double *b)
{
    if (selected) { *r = 0.05; *g = 0.65; *b = 0.15; return; }
    switch (kind) {
    case 1:  *r = 0.85; *g = 0.10; *b = 0.10; break;
    case 2:  *r = 0.95; *g = 0.55; *b = 0.05; break;
    default: *r = 0.20; *g = 0.20; *b = 0.20; break;
    }
}

static void draw_marker(cairo_t *cr, double x, double y, double radius,
                        int kind, int selected)
{
    double r, g, b;
    marker_rgb(kind, selected, &r, &g, &b);
    cairo_set_source_rgb(cr, r, g, b);
    cairo_arc(cr, x, y, radius, 0.0, 2.0 * M_PI);
    cairo_fill_preserve(cr);
    cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
    cairo_set_line_width(cr, 0.8);
    cairo_stroke(cr);
}

/* Polilinea de un perfil; NaN rompe la linea (costa afuera en el slab). */
static void polyline(cairo_t *cr, const CsnhypodbpFrame *f,
                     const CsnhypodbpProfileLine *l, double scale)
{
    int k, started = 0;

    for (k = 0; k < l->n; k++) {
        double sx, sy;
        if (!isfinite(l->val[k])) { started = 0; continue; }
        csnhypodbp_frame_project(f, l->lon[k], l->val[k] * scale, &sx, &sy);
        if (started) cairo_line_to(cr, sx, sy);
        else { cairo_move_to(cr, sx, sy); started = 1; }
    }
    if (l->n > 0) cairo_stroke(cr);
}

/* Geometria del panel: margenes y marco BASE (sin zoom ni paneo). 0 si el area
 * es util; -1 si no hay donde dibujar. */
static int profile_base_frame(const BpqProfileCtx *c, int w, int h,
                              CsnhypodbpFrame *f)
{
    const double dpr = (c && c->dpr > 0.0) ? c->dpr : 1.0;
    const double mx = 40.0 * dpr, mt = 10.0 * dpr, mb = 20.0 * dpr;
    const double pw = w - mx - 8.0 * dpr, ph = h - mt - mb;
    CsnhypodbpProfileLine empty;
    const CsnhypodbpProfileLine *slab, *topo;
    double x0, x1, y0, y1;

    if (!c || !f || w <= 0 || h <= 0 || pw < 20.0 || ph < 20.0) return -1;

    memset(&empty, 0, sizeof(empty));
    slab = (c->prof && c->prof->have_slab) ? &c->prof->slab : &empty;
    topo = (c->prof && c->prof->have_topo) ? &c->prof->topo : &empty;

    if (slab->n <= 0 && topo->n <= 0) {
        x0 = -75.0;
        x1 = -66.0;
        y0 = -210.0;
        y1 = 8.0;
    } else {
        csnhypodbp_profile_frame(slab, topo, c->depth_min_km, &x0, &x1, &y0, &y1);
    }
    csnhypodbp_frame_set(f, x0, x1, y0, y1, mx, mt, pw, ph);
    return 0;
}

/* Marco tal como se dibuja: base + zoom/paneo del usuario. */
static int profile_view_frame(const BpqProfileCtx *c, int w, int h,
                              CsnhypodbpFrame *f)
{
    if (profile_base_frame(c, w, h, f) != 0) return -1;
    csnhypodbp_frame_view(f, (c->zoom > 0.0) ? c->zoom : 1.0, c->pan_x, c->pan_y);
    return 0;
}

void bpq_profile_draw(cairo_t *cr, int w, int h, const BpqProfileCtx *c)
{
    const double dpr = (c && c->dpr > 0.0) ? c->dpr : 1.0;
    const double mx = 40.0 * dpr, mt = 10.0 * dpr, mb = 20.0 * dpr;
    const double pw = w - mx - 8.0 * dpr, ph = h - mt - mb;
    CsnhypodbpFrame f;
    CsnhypodbpProfileLine empty;
    const CsnhypodbpProfileLine *slab, *topo;
    int i, k;

    if (!cr || !c || w <= 0 || h <= 0) return;

    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_paint(cr);
    if (pw < 20.0 || ph < 20.0) return;

    memset(&empty, 0, sizeof(empty));
    slab = (c->prof && c->prof->have_slab) ? &c->prof->slab : &empty;
    topo = (c->prof && c->prof->have_topo) ? &c->prof->topo : &empty;

    /* Marco: SOLO los datos del perfil, y encima el zoom/paneo del usuario. */
    if (profile_view_frame(c, w, h, &f) != 0) return;

    /* Marco y rejilla. El trazo va a medio pixel para que quede nitido (1 px
     * lleno, no dos a media intensidad). */
    cairo_set_source_rgb(cr, 0.28, 0.30, 0.33);
    cairo_set_line_width(cr, 1.0 * dpr);
    cairo_rectangle(cr, mx + 0.5 * dpr, mt + 0.5 * dpr, pw - dpr, ph - dpr);
    cairo_stroke(cr);

    cairo_save(cr);
    cairo_rectangle(cr, mx, mt, pw, ph);
    cairo_clip(cr);
    {
        double dash[2] = { 3.0 * dpr, 3.0 * dpr };
        double v, sx, sy;
        cairo_set_source_rgba(cr, 0.6, 0.6, 0.6, 0.5);
        cairo_set_line_width(cr, 0.6 * dpr);
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

    /* Subduccion (km, ya negativa) y topografia (m -> km). */
    cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
    cairo_set_line_width(cr, 1.0 * dpr);
    if (c->prof && c->prof->have_slab) polyline(cr, &f, &c->prof->slab, 1.0);
    if (c->prof && c->prof->have_topo) polyline(cr, &f, &c->prof->topo, 1.0 / 1000.0);

    /* Soluciones: primero el resto, luego preferida/candidata, y al final la
     * elegida en verde (paridad gui.c:3380-3399). */
    for (k = 0; k <= 2; k++) {
        for (i = 0; i < c->npts; i++) {
            const CsnhypodbpMapPt *p = &c->pts[i];
            double sx, sy;
            if (p->kind != k) continue;
            if (!isfinite(p->lon) || !isfinite(p->depth)) continue;
            csnhypodbp_frame_project(&f, p->lon, -p->depth, &sx, &sy);
            draw_marker(cr, sx, sy, ((p->kind == 1 || p->kind == 2) ? 5.5 : 3.5) * dpr,
                        p->kind, 0);
        }
    }
    if (c->selected >= 0 && c->selected < c->npts) {
        const CsnhypodbpMapPt *p = &c->pts[c->selected];
        double sx, sy;
        if (isfinite(p->lon) && isfinite(p->depth)) {
            csnhypodbp_frame_project(&f, p->lon, -p->depth, &sx, &sy);
            draw_marker(cr, sx, sy, 7.0 * dpr, p->kind, 1);
        }
    }
    cairo_restore(cr);

    /* Etiquetas de los ejes (fuera del recorte). */
    cairo_select_font_face(cr, "sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 9.0 * dpr);
    cairo_set_source_rgb(cr, 0.25, 0.25, 0.25);
    {
        double v, sx, sy, span = f.x1 - f.x0;
        const char *fmt = (span < 0.5) ? "%.3f" : (span < 5.0 ? "%.2f" : "%.1f");
        char lbl[32];
        for (k = 0; k <= 4; k++) {
            v = f.x0 + k * span / 4.0;
            csnhypodbp_frame_project(&f, v, f.y0, &sx, &sy);
            snprintf(lbl, sizeof(lbl), fmt, v);
            cairo_move_to(cr, sx - 10.0 * dpr, mt + ph + 13.0 * dpr);
            cairo_show_text(cr, lbl);

            v = f.y1 - k * (f.y1 - f.y0) / 4.0;
            csnhypodbp_frame_project(&f, f.x0, v, &sx, &sy);
            snprintf(lbl, sizeof(lbl), "%.0f", v);
            cairo_move_to(cr, 4.0 * dpr, sy + 3.0 * dpr);
            cairo_show_text(cr, lbl);
        }
    }
}

/* --- interaccion ---------------------------------------------------------- */

int bpq_profile_project(const BpqProfileCtx *c, int w, int h,
                        double lon, double depth, double *sx, double *sy)
{
    CsnhypodbpFrame f;

    if (profile_view_frame(c, w, h, &f) != 0) return -1;
    csnhypodbp_frame_project(&f, lon, -depth, sx, sy);
    return 0;
}

void bpq_profile_pan(BpqProfileCtx *c, int w, int h, double dx, double dy)
{
    CsnhypodbpFrame f;
    double ux, uy;

    if (!c || profile_view_frame(c, w, h, &f) != 0) return;
    if (!isfinite(dx)) dx = 0.0;
    if (!isfinite(dy)) dy = 0.0;

    ux = (f.pw > 0.0) ? (f.x1 - f.x0) / f.pw : 0.0;
    uy = (f.ph > 0.0) ? (f.y1 - f.y0) / f.ph : 0.0;
    if (!isfinite(ux)) ux = 0.0;
    if (!isfinite(uy)) uy = 0.0;

    /* frame_project deja x creciendo a la derecha y la y invertida, asi que el
     * signo es opuesto en x para que el contenido siga al cursor en ambos ejes. */
    c->pan_x -= dx * ux;
    c->pan_y += dy * uy;
}

void bpq_profile_zoom_at(BpqProfileCtx *c, int w, int h, double mx, double my,
                         double factor)
{
    CsnhypodbpFrame f;
    double z, zn, fct, spanx, spany, u, v;

    if (!c || profile_view_frame(c, w, h, &f) != 0) return;
    if (!isfinite(factor) || factor <= 0.0) return;

    z = (isfinite(c->zoom) && c->zoom > 0.0) ? c->zoom : 1.0;
    zn = z * factor;
    if (zn < 1e-3) zn = 1e-3;
    if (zn > 1e4) zn = 1e4;
    fct = zn / z;
    if (!isfinite(fct) || fct == 1.0) return;

    spanx = f.x1 - f.x0;
    spany = f.y1 - f.y0;
    u = (f.pw > 0.0) ? (mx - f.px) / f.pw : 0.5;
    v = (f.ph > 0.0) ? 1.0 - (my - f.py) / f.ph : 0.5;

    /* Ancla el dato que hay bajo el cursor: el marco se recentra en cada dibujo,
     * asi que el desplazamiento es pan' = pan + span*(1 - 1/f)*(u - 1/2). */
    c->pan_x += spanx * (1.0 - 1.0 / fct) * (u - 0.5);
    c->pan_y += spany * (1.0 - 1.0 / fct) * (v - 0.5);
    c->zoom = zn;
}
