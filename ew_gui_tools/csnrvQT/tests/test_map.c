/* C6: mapa (proyeccion, zoom/pan/reset, capas y export PNG/SVG/PDF). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "crv_capi.h"

static int g_fail = 0;

#define CHECK(cond, what)                                                       \
    do {                                                                        \
        if (!(cond)) {                                                          \
            printf("FAIL: %s (%s:%d)\n", what, __FILE__, __LINE__);             \
            g_fail = 1;                                                         \
        }                                                                       \
    } while (0)

static long file_size(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0)
        return -1;
    return (long)st.st_size;
}

int main(void)
{
    CrvConfig cfg;
    CrvConfig cfg_ov;
    CrvMap *m;
    CrvMapView v;
    CrvSnapshot s;
    unsigned char bg[100 * 50 * 4];
    unsigned char *pix;
    char lines[CRV_MAX_STR];
    char out[700];
    double x, y;
    FILE *f;
    int i;

    memset(&cfg, 0, sizeof cfg);
    cfg.initial_zoom = 8.0;

    memset(bg, 0x20, sizeof bg);
    m = crv_map_new(&cfg);
    CHECK(m != NULL, "map_new");
    if (!m)
        return 1;
    CHECK(crv_map_has_background(m) == 0, "sin fondo al nacer");
    crv_map_set_background(m, bg, 100, 50, 100 * 4);
    CHECK(crv_map_has_background(m) == 1, "fondo aceptado");

    memset(&s, 0, sizeof s);
    s.has_data = 1;
    s.has_depth = 1;
    s.lat = 0.0;
    s.lon = 0.0;

    crv_map_view_init(&v, 2.0);
    CHECK(v.zoom == 2.0 && v.initial_zoom == 2.0, "view_init");

    /* El epicentro cae en el centro del canvas. */
    crv_map_project(m, &v, &s, 400, 400, 0.0, 0.0, &x, &y);
    CHECK(x > 199.9 && x < 200.1 && y > 199.9 && y < 200.1, "epicentro al centro");

    /* lon 180 sobre un fondo de 100 px -> (100-50)*2 + 200. */
    crv_map_project(m, &v, &s, 400, 400, 0.0, 180.0, &x, &y);
    CHECK(x > 299.9 && x < 300.1, "lon 180 con zoom 2");

    /* Zoom con clamp y reset. */
    crv_map_zoom_factor(&v, 1.2);
    CHECK(v.zoom > 2.39 && v.zoom < 2.41, "zoom 1.2");
    for (i = 0; i < 100; i++)
        crv_map_zoom_factor(&v, 1.2);
    CHECK(v.zoom == 50.0, "clamp alto");
    for (i = 0; i < 200; i++)
        crv_map_zoom_factor(&v, 1.0 / 1.2);
    CHECK(v.zoom == 0.2, "clamp bajo");

    crv_map_pan(&v, 10.0, -5.0);
    CHECK(v.pan_x == 10.0 && v.pan_y == -5.0, "pan");
    crv_map_reset(&v);
    CHECK(v.zoom == 2.0 && v.pan_x == 0.0 && v.pan_y == 0.0, "reset");

    /* --- zoom anclado al cursor (crv_map_zoom_at) ------------------------ */
    crv_map_reset(&v);          /* zoom 2, pan 0 */
    {
        double ax, ay, bx, by;

        /* Punto del mundo que cae bajo el cursor ANTES de la rueda. */
        crv_map_project(m, &v, &s, 400, 400, 0.0, 180.0, &ax, &ay);
        crv_map_zoom_at(&v, 400, 400, ax, ay, 1.2);
        CHECK(v.zoom > 2.39 && v.zoom < 2.41, "zoom_at aplica el factor");
        crv_map_project(m, &v, &s, 400, 400, 0.0, 180.0, &bx, &by);
        CHECK(fabs(bx - ax) < 0.5 && fabs(by - ay) < 0.5,
              "zoom_at deja el punto quieto bajo el cursor");
    }

    /* Contra el tope: el factor aplicado es 1 y el pan NO deriva. */
    v.zoom = 50.0;
    v.pan_x = 11.0;
    v.pan_y = -7.0;
    crv_map_zoom_at(&v, 400, 400, 300.0, 200.0, 1.2);
    CHECK(v.zoom == 50.0 && v.pan_x == 11.0 && v.pan_y == -7.0,
          "zoom_at no deriva pan en el tope alto");
    v.zoom = 0.2;
    crv_map_zoom_at(&v, 400, 400, 300.0, 200.0, 1.0 / 1.2);
    CHECK(v.zoom == 0.2 && v.pan_x == 11.0 && v.pan_y == -7.0,
          "zoom_at no deriva pan en el tope bajo");

    /* Factor invalido o no finito: no-op. */
    crv_map_zoom_at(&v, 400, 400, 300.0, 200.0, 0.0);
    crv_map_zoom_at(&v, 400, 400, 300.0, 200.0, -2.0);
    crv_map_zoom_at(&v, 400, 400, 300.0, 200.0, NAN);
    CHECK(v.zoom == 0.2 && v.pan_x == 11.0 && v.pan_y == -7.0,
          "zoom_at ignora factor invalido");

    /* Canvas sin tamano: aplica el zoom, sin tocar el pan. */
    v.zoom = 2.0;
    v.pan_x = 0.0;
    v.pan_y = 0.0;
    crv_map_zoom_at(&v, 0, 0, 300.0, 200.0, 1.2);
    CHECK(v.zoom > 2.39 && v.zoom < 2.41 && v.pan_x == 0.0 && v.pan_y == 0.0,
          "zoom_at sin canvas no toca el pan");

    crv_map_reset(&v);          /* estado limpio para el resto del test */

    /* Pintar: el pixel central es el epicentro rojo. */
    pix = malloc(400 * 400 * 4);
    CHECK(pix != NULL, "buffer de pintura");
    if (pix) {
        memset(pix, 0, 400 * 400 * 4);
        crv_map_paint_bgra(m, &v, &s, pix, 400, 400, 400 * 4);
        {
            unsigned char *p = pix + ((size_t)200 * 400 + 200) * 4;
            CHECK(p[2] > 200 && p[1] < 80 && p[0] < 80,
                  "centro rojo (BGRA)");
        }
        free(pix);
    }

    /* Capas: un .lines con una capa de 1 polilinea / 2 vertices. */
    snprintf(lines, sizeof lines, "/tmp/crv_map_XXXXXX");
    f = NULL;
    {
        int fd = mkstemp(lines);
        CHECK(fd >= 0, "mkstemp");
        if (fd >= 0) {
            f = fdopen(fd, "wb");
            CHECK(f != NULL, "fdopen");
            if (f) {
                fprintf(f, "#layer name=a color=FF0000 width=2\n");
                fprintf(f, "0 0\n1 1\n");
                fclose(f);
            }
        }
    }
    memset(&cfg_ov, 0, sizeof cfg_ov);
    cfg_ov.initial_zoom = 8.0;
    snprintf(cfg_ov.overlay_files[0], CRV_MAX_STR, "%s", lines);
    cfg_ov.n_overlay_files = 1;
    {
        CrvMap *mo = crv_map_new(&cfg_ov);
        CrvLayerInfo li;
        CHECK(mo != NULL, "map_new overlays");
        if (mo) {
            CHECK(crv_map_nlayers(mo) == 1, "1 capa");
            CHECK(crv_map_layer_info(mo, 0, &li) == 0, "layer_info");
            CHECK(strcmp(li.name, "a") == 0, "nombre de capa");
            CHECK(li.visible == 1, "visible");
            CHECK(li.rgb == 0xFF0000, "color RGB");
            CHECK(li.polylines == 1 && li.vertices == 2, "polilineas/vertices");
            CHECK(crv_map_set_visible(mo, 0, 0) == 0, "ocultar capa");
            CHECK(crv_map_layer_info(mo, 0, &li) == 0 && li.visible == 0,
                  "capa oculta");
            crv_map_free(mo);
        }
    }
    unlink(lines);

    /* Export PNG/SVG/PDF. */
    {
        char base[600];
        int fd;

        snprintf(base, sizeof base, "/tmp/crv_map_XXXXXX");
        fd = mkstemp(base);
        if (fd >= 0)
            close(fd);
        snprintf(out, sizeof out, "%s.png", base);
        unlink(base);   /* el mkstemp vacio; el PNG va a <base>.png */
    }
    CHECK(crv_map_export(m, &v, &s, out, CRV_FMT_PNG, 400, 400) == 0,
          "export PNG");
    CHECK(file_size(out) > 100, "PNG no vacio");
    unlink(out);

    {
        char svg[600], pdf[600];
        snprintf(svg, sizeof svg, "/tmp/crv_export_%d.svg", (int)getpid());
        snprintf(pdf, sizeof pdf, "/tmp/crv_export_%d.pdf", (int)getpid());
        CHECK(crv_map_export(m, &v, &s, svg, CRV_FMT_SVG, 400, 400) == 0,
              "export SVG");
        CHECK(file_size(svg) > 100, "SVG no vacio");
        CHECK(crv_map_export(m, &v, &s, pdf, CRV_FMT_PDF, 400, 400) == 0,
              "export PDF");
        CHECK(file_size(pdf) > 100, "PDF no vacio");
        unlink(svg);
        unlink(pdf);
    }

    crv_map_free(m);
    if (g_fail == 0)
        printf("OK: mapa (proyeccion, zoom/pan, capas y PNG/SVG/PDF)\n");
    return g_fail;
}
