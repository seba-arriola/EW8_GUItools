/*
 * Capa vectorial de líneas: parser de ".lines" (texto plano) y dibujo cairo.
 * Sin GTK. Ver include/ewgui/overlay.h y tools/OVERLAYS.md.
 */
#include "ewgui/overlay.h"
#include "ewgui/config.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OV_DEFAULT_WIDTH 1.5
#define OV_LINE_MAX      1024

static void ov_warn(const char *path, int lineno, const char *fmt, ...)
{
    va_list ap;
    fprintf(stderr, "overlay: %s:%d: ", path ? path : "?", lineno);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

EwOverlay *ewgui_overlay_new(void)
{
    return calloc(1, sizeof(EwOverlay));
}

void ewgui_overlay_free(EwOverlay *ov)
{
    size_t i, j;
    if (!ov)
        return;
    for (i = 0; i < ov->nlayers; i++) {
        EwOverlayLayer *L = &ov->layers[i];
        for (j = 0; j < L->nlines; j++)
            free(L->lines[j].pts);
        free(L->lines);
    }
    free(ov->layers);
    free(ov);
}

int ewgui_overlay_layer_index(const EwOverlay *ov, const char *name)
{
    size_t i;
    if (!ov || !name)
        return -1;
    for (i = 0; i < ov->nlayers; i++)
        if (strcmp(ov->layers[i].name, name) == 0)
            return (int)i;
    return -1;
}

int ewgui_overlay_set_color(EwOverlay *ov, const char *layer, const double rgb[3])
{
    size_t i;
    int n = 0;
    if (!ov || !layer || !rgb)
        return 0;
    for (i = 0; i < ov->nlayers; i++) {
        if (strcmp(ov->layers[i].name, layer) == 0) {
            memcpy(ov->layers[i].color, rgb, sizeof ov->layers[i].color);
            n++;
        }
    }
    return n;
}

void ewgui_overlay_set_global_width(EwOverlay *ov, double width)
{
    size_t i;
    if (!ov || width <= 0)
        return;
    for (i = 0; i < ov->nlayers; i++)
        ov->layers[i].width = width;
}

int ewgui_overlay_set_visible(EwOverlay *ov, const char *layer, int visible)
{
    size_t i;
    int n = 0;
    if (!ov || !layer)
        return 0;
    for (i = 0; i < ov->nlayers; i++) {
        if (strcmp(ov->layers[i].name, layer) == 0) {
            ov->layers[i].visible = visible ? 1 : 0;
            n++;
        }
    }
    return n;
}

/* --- helpers de parseo --------------------------------------------------- */

static EwOverlayLayer *ov_add_layer(EwOverlay *ov, const char *name)
{
    EwOverlayLayer *tmp = realloc(ov->layers, (ov->nlayers + 1) * sizeof(*tmp));
    EwOverlayLayer *L;
    if (!tmp)
        return NULL;
    ov->layers = tmp;
    L = &ov->layers[ov->nlayers++];
    memset(L, 0, sizeof(*L));
    snprintf(L->name, sizeof L->name, "%s", name ? name : "");
    L->width = OV_DEFAULT_WIDTH;
    L->visible = 1;
    return L;
}

/* Vuelca el buffer temporal como una polilínea nueva de `L`. */
static int ov_flush(EwOverlayLayer *L, EwOverlayPoint *buf, size_t n)
{
    EwOverlayPolyline *tmp, *P;
    if (!L || n == 0)
        return 0;
    tmp = realloc(L->lines, (L->nlines + 1) * sizeof(*tmp));
    if (!tmp)
        return -1;
    L->lines = tmp;
    P = &L->lines[L->nlines];
    P->pts = malloc(n * sizeof(*P->pts));
    if (!P->pts)
        return -1;
    memcpy(P->pts, buf, n * sizeof(*P->pts));
    P->npts = n;
    L->nlines++;
    return 0;
}

static int ov_push(EwOverlayPoint **buf, size_t *n, size_t *cap,
                   double lon, double lat)
{
    if (*n == *cap) {
        size_t nc = *cap ? *cap * 2 : 64;
        EwOverlayPoint *t = realloc(*buf, nc * sizeof(*t));
        if (!t)
            return -1;
        *buf = t;
        *cap = nc;
    }
    (*buf)[*n].lon = lon;
    (*buf)[*n].lat = lat;
    (*n)++;
    return 0;
}

/* Procesa los pares key=value de una línea "#layer". */
static void ov_parse_attrs(const char *path, int lineno, char *rest,
                           char *name, size_t namesz,
                           double color[3], int *has_color,
                           double *width, int *has_width)
{
    char *tok = strtok(rest, " \t");
    while (tok) {
        char *eq = strchr(tok, '=');
        if (eq) {
            const char *k = tok;
            const char *v;
            *eq = '\0';
            v = eq + 1;
            if (strcmp(k, "name") == 0) {
                snprintf(name, namesz, "%s", v);
            } else if (strcmp(k, "color") == 0) {
                double rgb[3];
                if (ewgui_parse_hex_color(v, rgb) == 0) {
                    memcpy(color, rgb, sizeof(double) * 3);
                    *has_color = 1;
                } else {
                    ov_warn(path, lineno, "color invalido '%s'", v);
                }
            } else if (strcmp(k, "width") == 0) {
                char *end = NULL;
                double w = strtod(v, &end);
                if (end && *end == '\0' && w > 0.0) {
                    *width = w;
                    *has_width = 1;
                } else {
                    ov_warn(path, lineno, "width invalido '%s'", v);
                }
            } else {
                ov_warn(path, lineno, "atributo desconocido '%s'", k);
            }
        } else {
            ov_warn(path, lineno, "atributo sin '=' ('%s')", tok);
        }
        tok = strtok(NULL, " \t");
    }
}

int ewgui_overlay_append(EwOverlay *ov, const char *path)
{
    FILE *f;
    char line[OV_LINE_MAX];
    int lineno = 0;
    EwOverlayLayer *cur = NULL;
    EwOverlayPoint *buf = NULL;
    size_t nbuf = 0, capbuf = 0;
    unsigned anon = 0;
    int warned_range = 0;

    if (!ov || !path)
        return -1;
    f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "overlay: no se pudo abrir %s\n", path);
        return -1;
    }

    while (fgets(line, sizeof line, f)) {
        size_t n;
        char *s;
        lineno++;
        n = strlen(line);
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
            line[--n] = '\0';
        s = line;
        while (*s == ' ' || *s == '\t')
            s++;

        if (*s == '\0')
            continue;                              /* vacía                   */
        if (*s == '>') {                           /* fin de polilínea        */
            if (ov_flush(cur, buf, nbuf) != 0)
                break;
            nbuf = 0;
            continue;
        }
        if (*s == '#') {
            char *p = s + 1;
            while (*p == ' ' || *p == '\t')
                p++;
            if (strncmp(p, "layer", 5) == 0 &&
                (p[5] == '\0' || isspace((unsigned char)p[5]))) {
                char name[EWGUI_OVERLAY_NAME_MAX] = "";
                double color[3] = {0.0, 0.0, 0.0};
                double width = OV_DEFAULT_WIDTH;
                int has_color = 0, has_width = 0;

                if (ov_flush(cur, buf, nbuf) != 0)
                    break;
                nbuf = 0;

                ov_parse_attrs(path, lineno, p + 5, name, sizeof name,
                               color, &has_color, &width, &has_width);
                if (name[0] == '\0') {
                    snprintf(name, sizeof name, "layer%u", ++anon);
                    ov_warn(path, lineno, "#layer sin name; uso '%s'", name);
                }
                cur = ov_add_layer(ov, name);
                if (!cur)
                    break;
                if (has_color)
                    memcpy(cur->color, color, sizeof color);
                if (has_width)
                    cur->width = width;
            }
            continue;                              /* comentario              */
        }

        /* coordenada lon lat */
        if (!cur) {
            ov_warn(path, lineno, "coordenada antes de #layer; ignorada");
            continue;
        }
        {
            char *end1 = NULL, *end2 = NULL;
            double lon = strtod(s, &end1);
            double lat;
            if (end1 == s) {
                ov_warn(path, lineno, "coordenada invalida; ignorada");
                continue;
            }
            lat = strtod(end1, &end2);
            if (end2 == end1) {
                ov_warn(path, lineno, "coordenada invalida; ignorada");
                continue;
            }
            if (!warned_range &&
                (lon < -180.0 || lon > 180.0 || lat < -90.0 || lat > 90.0)) {
                ov_warn(path, lineno, "coordenada fuera de rango (%g,%g)", lon, lat);
                warned_range = 1;
            }
            if (ov_push(&buf, &nbuf, &capbuf, lon, lat) != 0)
                break;
        }
    }
    ov_flush(cur, buf, nbuf);
    free(buf);
    fclose(f);
    return 0;
}

void ewgui_overlay_draw(cairo_t *cr, const EwOverlay *ov,
                        EwOverlayMapFn map, void *ud, double width_scale)
{
    size_t li, pi, k;
    double ws;
    if (!cr || !ov || !map)
        return;
    ws = (width_scale > 0.0) ? width_scale : 1.0;
    cairo_save(cr);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    for (li = 0; li < ov->nlayers; li++) {
        const EwOverlayLayer *L = &ov->layers[li];
        if (!L->visible)
            continue;
        cairo_set_source_rgb(cr, L->color[0], L->color[1], L->color[2]);
        cairo_set_line_width(cr, (L->width > 0 ? L->width : 1.0) * ws);
        for (pi = 0; pi < L->nlines; pi++) {
            const EwOverlayPolyline *P = &L->lines[pi];
            if (P->npts == 0)
                continue;
            for (k = 0; k < P->npts; k++) {
                double sx = 0, sy = 0;
                map(P->pts[k].lat, P->pts[k].lon, ud, &sx, &sy);
                if (k == 0)
                    cairo_move_to(cr, sx, sy);
                else
                    cairo_line_to(cr, sx, sy);
            }
            cairo_stroke(cr);
        }
    }
    cairo_restore(cr);
}
