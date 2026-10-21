/*
 * Nucleo puro de la geometria de los dos paneles de mapa (ver
 * csnhypodbp_mapview.h). Sin GTK; testeable headless.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ewgui/geo.h"

#include "csnhypodbp_mapview.h"

/* Span minimo para no dividir por cero con un bbox degenerado. */
#define SPAN_MIN 1e-9

int csnhypodbp_map_bbox(const CsnhypodbpMapPt *pt, int n,
                        double *lon0, double *lon1, double *lat0, double *lat1)
{
    double lo0 = 0, lo1 = 0, la0 = 0, la1 = 0;
    int i, k = 0;

    if (!pt || n <= 0) return -1;
    for (i = 0; i < n; i++) {
        if (!isfinite(pt[i].lat) || !isfinite(pt[i].lon)) continue;
        if (pt[i].lat == 0.0 && pt[i].lon == 0.0) continue;   /* sin posicion */
        if (k == 0) {
            lo0 = lo1 = pt[i].lon;
            la0 = la1 = pt[i].lat;
        } else {
            if (pt[i].lon < lo0) lo0 = pt[i].lon;
            if (pt[i].lon > lo1) lo1 = pt[i].lon;
            if (pt[i].lat < la0) la0 = pt[i].lat;
            if (pt[i].lat > la1) la1 = pt[i].lat;
        }
        k++;
    }
    if (k == 0) return -1;
    if (lon0) *lon0 = lo0;
    if (lon1) *lon1 = lo1;
    if (lat0) *lat0 = la0;
    if (lat1) *lat1 = la1;
    return 0;
}

void csnhypodbp_map_span_min(double *lon0, double *lon1, double *lat0, double *lat1,
                             double min_span)
{
    double c;

    if (!lon0 || !lon1 || !lat0 || !lat1) return;
    if (!isfinite(min_span) || min_span <= 0.0) return;
    if (*lon1 - *lon0 < min_span) {
        c = (*lon0 + *lon1) / 2.0;
        *lon0 = c - min_span / 2.0;
        *lon1 = c + min_span / 2.0;
    }
    if (*lat1 - *lat0 < min_span) {
        c = (*lat0 + *lat1) / 2.0;
        *lat0 = c - min_span / 2.0;
        *lat1 = c + min_span / 2.0;
    }
    /* El contrato del mapa base es el mundo completo: no se sale de el. */
    if (*lon0 < -180.0) *lon0 = -180.0;
    if (*lon1 > 180.0) *lon1 = 180.0;
    if (*lat0 < -90.0) *lat0 = -90.0;
    if (*lat1 > 90.0) *lat1 = 90.0;
}

void csnhypodbp_mapfit(double lon0, double lon1, double lat0, double lat1,
                       double img_w, double img_h, int cw, int ch, double margin,
                       double *zoom, double *cx, double *cy)
{
    double xa, ya, xb, yb;
    double spanx, spany, zx, zy, z;

    /* Proyeccion equirectangular: x = W*(lon+180)/360, y = H*(90-lat)/180.
       Se toma el min/max por si el llamador invierte los extremos. */
    ewgui_geo_project(lat0, lon0, img_w, img_h, &xa, &ya);
    ewgui_geo_project(lat1, lon1, img_w, img_h, &xb, &yb);
    {
        double x0 = (xa < xb) ? xa : xb, x1 = (xa < xb) ? xb : xa;
        double y0 = (ya < yb) ? ya : yb, y1 = (ya < yb) ? yb : ya;
        xa = x0; xb = x1; ya = y0; yb = y1;
    }

    spanx = fabs(xb - xa);
    spany = fabs(yb - ya);
    if (spanx < SPAN_MIN) spanx = SPAN_MIN;
    if (spany < SPAN_MIN) spany = SPAN_MIN;

    zx = ((double)cw - 2.0 * margin) / spanx;
    zy = ((double)ch - 2.0 * margin) / spany;
    if (zx < 0) zx = SPAN_MIN;
    if (zy < 0) zy = SPAN_MIN;
    z = (zx < zy) ? zx : zy;
    if (!isfinite(z) || z <= 0.0) z = 1.0;

    if (zoom) *zoom = z;
    if (cx) *cx = (xa + xb) / 2.0;
    if (cy) *cy = (ya + yb) / 2.0;
}

void csnhypodbp_mapfit_at(double ref_lat, double ref_lon,
                          double lon0, double lon1, double lat0, double lat1,
                          double img_w, double img_h, int cw, int ch, double margin,
                          double zoom_factor, double *zoom, double *cx, double *cy)
{
    double z, rx, ry;
    double f = (isfinite(zoom_factor) && zoom_factor > 0.0) ? zoom_factor : 1.0;

    /* El span sale del bbox (el suelo MapMinSpanDeg ya lo aplico el llamante) y
       el factor lo acerca; el CENTRO es la referencia. */
    csnhypodbp_mapfit(lon0, lon1, lat0, lat1, img_w, img_h, cw, ch, margin,
                      &z, NULL, NULL);
    z *= f;
    if (!isfinite(z) || z <= 0.0) z = 1.0;

    if (isfinite(ref_lat) && isfinite(ref_lon) &&
        !(ref_lat == 0.0 && ref_lon == 0.0)) {
        ewgui_geo_project(ref_lat, ref_lon, img_w, img_h, &rx, &ry);
    } else {
        /* Sin referencia valida: el punto medio del bbox (como mapfit). */
        double xa, ya, xb, yb, x0, y0, x1, y1;

        ewgui_geo_project(lat0, lon0, img_w, img_h, &xa, &ya);
        ewgui_geo_project(lat1, lon1, img_w, img_h, &xb, &yb);
        x0 = (xa < xb) ? xa : xb;
        x1 = (xa < xb) ? xb : xa;
        y0 = (ya < yb) ? ya : yb;
        y1 = (ya < yb) ? yb : ya;
        rx = (x0 + x1) / 2.0;
        ry = (y0 + y1) / 2.0;
    }

    if (zoom) *zoom = z;
    if (cx) *cx = rx;
    if (cy) *cy = ry;
}

/* --- lienzo de proyeccion de la planta (sin MapImageFile) --- */

int csnhypodbp_map_canvas(int has_image, int pw, int ph, double *iw, double *ih)
{
    if (!iw || !ih) return -1;
    if (has_image && pw > 0 && ph > 0) {
        *iw = (double)pw;
        *ih = (double)ph;
        return 0;
    }
    *iw = CSNHYPODBP_MAP_VIRTUAL_W;
    *ih = CSNHYPODBP_MAP_VIRTUAL_H;
    return 1;
}

int csnhypodbp_map_screen(double lat, double lon, double iw, double ih,
                          int cw, int ch, double zoom, double cx, double cy,
                          double pan_x, double pan_y, double *sx, double *sy)
{
    double x, y;

    if (!sx || !sy) return -1;
    if (!isfinite(lat) || !isfinite(lon)) return -1;
    if (!isfinite(iw) || !isfinite(ih) || iw <= 0.0 || ih <= 0.0) return -1;
    if (!isfinite(zoom) || zoom <= 0.0) return -1;
    if (!isfinite(cx) || !isfinite(cy)) return -1;
    if (cw <= 0 || ch <= 0) return -1;
    if (!isfinite(pan_x)) pan_x = 0.0;
    if (!isfinite(pan_y)) pan_y = 0.0;

    ewgui_geo_project(lat, lon, iw, ih, &x, &y);
    *sx = (x - cx) * zoom + cw / 2.0 + pan_x;
    *sy = (y - cy) * zoom + ch / 2.0 + pan_y;
    return 0;
}

int csnhypodbp_map_view_bounds(double iw, double ih, int cw, int ch,
                               double zoom, double cx, double cy,
                               double pan_x, double pan_y,
                               double *lon0, double *lon1, double *lat0, double *lat1)
{
    double x0, x1, y0, y1, lo0, lo1, la0, la1;

    if (!lon0 || !lon1 || !lat0 || !lat1) return -1;
    if (!isfinite(iw) || !isfinite(ih) || iw <= 0.0 || ih <= 0.0) return -1;
    if (!isfinite(zoom) || zoom <= 0.0) return -1;
    if (!isfinite(cx) || !isfinite(cy)) return -1;
    if (cw <= 0 || ch <= 0) return -1;
    if (!isfinite(pan_x)) pan_x = 0.0;
    if (!isfinite(pan_y)) pan_y = 0.0;

    /* Inversa de la transformacion de csnhypodbp_map_screen(). */
    x0 = (0.0 - cw / 2.0 - pan_x) / zoom + cx;
    x1 = ((double)cw - cw / 2.0 - pan_x) / zoom + cx;
    y0 = (0.0 - ch / 2.0 - pan_y) / zoom + cy;
    y1 = ((double)ch - ch / 2.0 - pan_y) / zoom + cy;

    /* Inversa de ewgui_geo_project(): y mayor = latitud menor. */
    lo0 = x0 * 360.0 / iw - 180.0;
    lo1 = x1 * 360.0 / iw - 180.0;
    la0 = 90.0 - y1 * 180.0 / ih;
    la1 = 90.0 - y0 * 180.0 / ih;

    if (lo0 < -180.0) lo0 = -180.0;
    if (lo1 > 180.0) lo1 = 180.0;
    if (la0 < -90.0) la0 = -90.0;
    if (la1 > 90.0) la1 = 90.0;
    if (!(lo1 > lo0) || !(la1 > la0)) return -1;

    *lon0 = lo0;
    *lon1 = lo1;
    *lat0 = la0;
    *lat1 = la1;
    return 0;
}

int csnhypodbp_map_fit_bounds(const CsnhypodbpMapPt *pt, int n, double min_span,
                              double *lon0, double *lon1, double *lat0, double *lat1)
{
    double lo0, lo1, la0, la1;

    if (!lon0 || !lon1 || !lat0 || !lat1) return -1;
    if (csnhypodbp_map_bbox(pt, n, &lo0, &lo1, &la0, &la1) == 0) {
        csnhypodbp_map_span_min(&lo0, &lo1, &la0, &la1, min_span);
        *lon0 = lo0;
        *lon1 = lo1;
        *lat0 = la0;
        *lat1 = la1;
        return 0;
    }
    /* Sin soluciones con posicion el mapa se abre igual: al mundo completo. */
    *lon0 = -180.0;
    *lon1 = 180.0;
    *lat0 = -90.0;
    *lat1 = 90.0;
    return 1;
}

/* Serie de pasos de grilla, en grados. Se elige el primero que no de mas de ~8
   divisiones en el span visible, asi la grilla no desaparece a ningun zoom. */
static const double GRID_STEPS[] = {
    0.05, 0.1, 0.2, 0.25, 0.5, 1.0, 2.0, 5.0, 10.0, 15.0, 30.0, 45.0, 90.0
};

double csnhypodbp_map_grid_step(double span_deg)
{
    const int nsteps = (int)(sizeof(GRID_STEPS) / sizeof(GRID_STEPS[0]));
    double raw;
    int i;

    if (!isfinite(span_deg) || span_deg <= 0.0) return 10.0;
    raw = span_deg / 8.0;
    for (i = 0; i < nsteps; i++)
        if (GRID_STEPS[i] >= raw) return GRID_STEPS[i];
    return GRID_STEPS[nsteps - 1];
}

void csnhypodbp_map_axis_fmt(double step, double value, char *buf, size_t n)
{
    if (!buf || n == 0) return;
    if (!isfinite(step) || step <= 0.0) step = 10.0;
    if (!isfinite(value)) {
        snprintf(buf, n, "?");
        return;
    }
    if (value == 0.0) value = 0.0;              /* evita imprimir "-0" */
    if (step >= 1.0)
        snprintf(buf, n, "%.0f", value);
    else if (step >= 0.1)
        snprintf(buf, n, "%.1f", value);
    else
        snprintf(buf, n, "%.2f", value);
}

int csnhypodbp_map_label_pick(const double *pos, int n, double min_gap, int *keep)
{
    double last = 0.0;
    int i, have = 0;

    if (!pos || !keep) return -1;
    if (!isfinite(min_gap) || min_gap < 0.0) min_gap = 0.0;
    for (i = 0; i < n; i++) {
        if (!isfinite(pos[i])) {
            keep[i] = 0;
            continue;
        }
        if (have && fabs(pos[i] - last) < min_gap) {
            keep[i] = 0;
            continue;
        }
        keep[i] = 1;
        last = pos[i];
        have = 1;
    }
    return 0;
}

int csnhypodbp_map_split_parse(const char *s, double *frac)
{
    char *end = NULL;
    double a, b;

    if (!s) return 0;
    a = strtod(s, &end);
    if (end == s || !isfinite(a)) return 0;
    while (*end == ' ' || *end == '\t') end++;
    if (*end == '/') {
        char *e2 = NULL;

        b = strtod(end + 1, &e2);
        if (e2 == end + 1 || !isfinite(b) || b <= 0.0 || a <= 0.0) return 0;
        end = e2;
        a = a / (a + b);
    }
    while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n') end++;
    if (*end) return 0;                       /* basura al final: no se acepta */
    if (!(a > 0.0) || !(a < 1.0)) return 0;
    if (frac) *frac = a;
    return 1;
}

/* --- aciertos con el puntero --- */

int csnhypodbp_map_hit(const CsnhypodbpMapPt *pt, const double *sx, const double *sy,
                       int n, double x, double y, double radius)
{
    int k, i;

    if (!pt || !sx || !sy || n <= 0) return -1;
    if (!isfinite(x) || !isfinite(y) || !(radius > 0.0)) return -1;
    for (k = 2; k >= 0; k--) {              /* candidata, preferida, resto */
        int best = -1;
        double best_d = 0.0;
        for (i = 0; i < n; i++) {
            double dx, dy, d;
            if (pt[i].kind != k) continue;
            dx = sx[i] - x;
            dy = sy[i] - y;
            d = sqrt(dx * dx + dy * dy);
            if (d <= radius && (best < 0 || d < best_d)) {
                best = i;
                best_d = d;
            }
        }
        if (best >= 0) return best;
    }
    return -1;
}

int csnhypodbp_map_drag_exceeded(double dx, double dy, double threshold)
{
    if (!isfinite(dx)) dx = 0.0;
    if (!isfinite(dy)) dy = 0.0;
    if (!(threshold > 0.0)) threshold = 0.0;
    return (fabs(dx) > threshold || fabs(dy) > threshold) ? 1 : 0;
}

int csnhypodbp_map_find_id(const char (*ids)[CSNHYPODBP_MAP_ID_LEN], int n,
                           const char *id)
{
    int i;
    if (!ids || !id || !id[0] || n <= 0) return -1;
    for (i = 0; i < n; i++)
        if (strcmp(ids[i], id) == 0) return i;
    return -1;
}

/* --- marco del panel de perfil --- */
void csnhypodbp_profile_frame(const CsnhypodbpProfileLine *slab,
                              const CsnhypodbpProfileLine *topo,
                              double depth_min_km,
                              double *x0, double *x1, double *y0, double *y1)
{
    double bx0 = 0.0, bx1 = 0.0, bottom, top;
    int have_x = 0;

    bottom = (isfinite(depth_min_km) && depth_min_km > 0.0) ? depth_min_km : 200.0;
    top = 10.0;
    if (slab && slab->n > 0 && slab->lon_min <= slab->lon_max) {
        bx0 = slab->lon_min;
        bx1 = slab->lon_max;
        have_x = 1;
        if (isfinite(slab->val_min) && -slab->val_min > bottom) bottom = -slab->val_min;
        if (isfinite(slab->val_max) && slab->val_max > top) top = slab->val_max;
    }
    if (topo && topo->n > 0 && topo->lon_min <= topo->lon_max) {
        if (!have_x) {
            bx0 = topo->lon_min;
            bx1 = topo->lon_max;
            have_x = 1;
        } else {
            if (topo->lon_min < bx0) bx0 = topo->lon_min;
            if (topo->lon_max > bx1) bx1 = topo->lon_max;
        }
        if (isfinite(topo->val_max)) {
            double e = topo->val_max / 1000.0;      /* m -> km */
            if (e + 1.0 > top) top = e + 1.0;
        }
    }
    if (!have_x) {
        bx0 = -76.0;
        bx1 = -66.0;
    }
    if (bx1 - bx0 < 0.05) {
        double c = (bx0 + bx1) / 2.0;
        bx0 = c - 0.025;
        bx1 = c + 0.025;
    }
    {
        double dx = (bx1 - bx0) * 0.02;
        bx0 -= dx;
        bx1 += dx;
    }
    if (x0) *x0 = bx0;
    if (x1) *x1 = bx1;
    if (y0) *y0 = -bottom * 1.05 - 5.0;
    if (y1) *y1 = top + 2.0;
}

void csnhypodbp_frame_view(CsnhypodbpFrame *f, double zoom,
                           double pan_x, double pan_y)
{
    double cx, cy, sx, sy;

    if (!f || !csnhypodbp_frame_valid(f)) return;
    if (!isfinite(zoom) || zoom <= 0.0) return;
    if (!isfinite(pan_x)) pan_x = 0.0;
    if (!isfinite(pan_y)) pan_y = 0.0;
    cx = (f->x0 + f->x1) / 2.0;
    cy = (f->y0 + f->y1) / 2.0;
    sx = (f->x1 - f->x0) / zoom;
    sy = (f->y1 - f->y0) / zoom;
    f->x0 = cx - sx / 2.0 + pan_x;
    f->x1 = cx + sx / 2.0 + pan_x;
    f->y0 = cy - sy / 2.0 + pan_y;
    f->y1 = cy + sy / 2.0 + pan_y;
}

int csnhypodbp_frame_valid(const CsnhypodbpFrame *f)
{
    if (!f) return 0;
    if (!(f->x1 >= f->x0) || !(f->y1 >= f->y0)) return 0;
    if (!isfinite(f->x0) || !isfinite(f->x1) ||
        !isfinite(f->y0) || !isfinite(f->y1)) return 0;
    return 1;
}

void csnhypodbp_frame_reset(CsnhypodbpFrame *f, double px, double py,
                            double pw, double ph)
{
    if (!f) return;
    memset(f, 0, sizeof(*f));
    f->x0 = 1.0; f->x1 = -1.0;      /* vacio */
    f->y0 = 1.0; f->y1 = -1.0;
    f->px = px; f->py = py; f->pw = pw; f->ph = ph;
}

void csnhypodbp_frame_set(CsnhypodbpFrame *f, double x0, double x1,
                          double y0, double y1, double px, double py,
                          double pw, double ph)
{
    if (!f) return;
    f->x0 = x0; f->x1 = x1; f->y0 = y0; f->y1 = y1;
    f->px = px; f->py = py; f->pw = pw; f->ph = ph;
}

void csnhypodbp_frame_project(const CsnhypodbpFrame *f, double x, double y,
                              double *sx, double *sy)
{
    double fx, fy, spanx, spany;

    if (!f) return;
    if (csnhypodbp_frame_valid(f)) {
        spanx = f->x1 - f->x0;
        spany = f->y1 - f->y0;
        /* Bbox degenerado (un solo punto): al centro. */
        fx = (spanx > SPAN_MIN) ? (x - f->x0) / spanx : 0.5;
        fy = (spany > SPAN_MIN) ? (y - f->y0) / spany : 0.5;
    } else {
        fx = fy = 0.5;
    }
    if (sx) *sx = f->px + fx * f->pw;
    if (sy) *sy = f->py + f->ph - fy * f->ph;   /* y crece hacia arriba */
}

void csnhypodbp_frame_grow(CsnhypodbpFrame *f, double x, double y)
{
    if (!f || !isfinite(x) || !isfinite(y)) return;
    if (!csnhypodbp_frame_valid(f)) {
        f->x0 = f->x1 = x;
        f->y0 = f->y1 = y;
        return;
    }
    if (x < f->x0) f->x0 = x;
    if (x > f->x1) f->x1 = x;
    if (y < f->y0) f->y0 = y;
    if (y > f->y1) f->y1 = y;
}

void csnhypodbp_frame_pad(CsnhypodbpFrame *f, double fx, double fy)
{
    double dx, dy;

    if (!csnhypodbp_frame_valid(f)) return;
    dx = (f->x1 - f->x0) * fx;
    dy = (f->y1 - f->y0) * fy;
    f->x0 -= dx; f->x1 += dx;
    f->y0 -= dy; f->y1 += dy;
}

int csnhypodbp_marker_star_path(double cx, double cy, double r, int points,
                                double inner_ratio, double *xs, double *ys, int max)
{
    int i, need;

    if (!xs || !ys || points < 3 || max < 2 * points) return 0;
    if (!(r > 0.0) || !isfinite(r) || !isfinite(cx) || !isfinite(cy)) return 0;
    if (!(inner_ratio > 0.0) || inner_ratio >= 1.0) inner_ratio = 0.45;
    need = 2 * points;
    for (i = 0; i < need; i++) {
        /* Alterna exterior/interior; -PI/2 deja el primer vertice ARRIBA (en
           pantalla y crece hacia abajo). */
        double rad = (i % 2 == 0) ? r : r * inner_ratio;
        double a = -M_PI / 2.0 + (M_PI * (double)i) / (double)points;

        xs[i] = cx + rad * cos(a);
        ys[i] = cy + rad * sin(a);
    }
    return need;
}
