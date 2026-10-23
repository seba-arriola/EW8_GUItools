/* C6: mapa (proyeccion, zoom anclado, escena, capas y export). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "sev_capi.h"

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

/* Lee ancho/alto del IHDR de un PNG (sin libpng). 0 si no es PNG. */
static int png_size(const char *path, unsigned *w, unsigned *h)
{
    unsigned char b[24];
    FILE *f = fopen(path, "rb");
    size_t n;

    if (!f)
        return -1;
    n = fread(b, 1, sizeof b, f);
    fclose(f);
    if (n != sizeof b || memcmp(b, "\x89PNG\r\n\x1a\n", 8) != 0 ||
        memcmp(b + 12, "IHDR", 4) != 0)
        return -1;
    *w = ((unsigned)b[16] << 24) | ((unsigned)b[17] << 16) |
         ((unsigned)b[18] << 8) | (unsigned)b[19];
    *h = ((unsigned)b[20] << 24) | ((unsigned)b[21] << 16) |
         ((unsigned)b[22] << 8) | (unsigned)b[23];
    return 0;
}

static unsigned char *new_buf(int w, int h)
{
    unsigned char *p = malloc((size_t)w * (size_t)h * 4);
    if (p)
        memset(p, 0, (size_t)w * (size_t)h * 4);
    return p;
}

#define PX(buf, w, x, y, c) ((buf)[((size_t)(y) * (size_t)(w) + (size_t)(x)) * 4 + (c)])

int main(void)
{
    char dir[256], ov[600], png[600], svg[600], pdf[600], cmd[400];
    SevConfig cfg;
    SevMap *m;
    SevMapView v;
    SevSnapshot s;
    unsigned char bg[200 * 100 * 4];
    unsigned char *buf, *buf2;
    const int W = 800, H = 600;
    double ax, ay, bx, by, x, y;
    int i;

    snprintf(dir, sizeof dir, "/tmp/sev_map_XXXXXX");
    if (!mkdtemp(dir)) {
        printf("FAIL: mkdtemp\n");
        return 1;
    }

    /* Una capa roja que cruza el mundo a lat 0. */
    snprintf(ov, sizeof ov, "%s/ov.lines", dir);
    {
        FILE *f = fopen(ov, "wb");
        fprintf(f, "#layer name=ov color=FF0000 width=2\n");
        fprintf(f, "-180 0\n180 0\n");
        fclose(f);
    }

    memset(&cfg, 0, sizeof cfg);
    cfg.station_size = 10.0;
    snprintf(cfg.overlay_files[0], SEV_MAX_STR, "%s", ov);
    cfg.n_overlay_files = 1;

    m = sev_map_new(&cfg);
    CHECK(m != NULL, "mapa nuevo");
    if (!m)
        return 1;

    /* Fondo 200x100 (2:1), azul oscuro. */
    for (i = 0; i < 200 * 100; i++) {
        bg[i * 4 + 0] = 50;   /* B */
        bg[i * 4 + 1] = 40;   /* G */
        bg[i * 4 + 2] = 30;   /* R */
        bg[i * 4 + 3] = 255;  /* A */
    }
    sev_map_set_background(m, bg, 200, 100, 200 * 4);
    CHECK(sev_map_has_background(m) == 1, "fondo aceptado");

    sev_map_view_init(&v);
    CHECK(v.zoom == 2.0 && v.pan_x == 0.0 && v.pan_y == 0.0,
          "vista inicial zoom 2 / pan 0");

    /* El centro geografico es (-30,-70). */
    sev_map_project(m, &v, W, H, -30.0, -70.0, &x, &y);
    CHECK(fabs(x - W / 2.0) < 0.001 && fabs(y - H / 2.0) < 0.001,
          "project(-30,-70) en el centro");

    /* Zoom anclado: el punto bajo el cursor no se mueve. */
    sev_map_project(m, &v, W, H, -30.0, -60.0, &ax, &ay);
    sev_map_zoom_at(&v, W, H, ax, ay, 1.2);
    CHECK(v.zoom > 2.39 && v.zoom < 2.41, "zoom_at aplica 1.2");
    sev_map_project(m, &v, W, H, -30.0, -60.0, &bx, &by);
    CHECK(fabs(bx - ax) < 0.5 && fabs(by - ay) < 0.5,
          "zoom_at deja el punto quieto");

    /* Topes: el factor aplicado es 1 y el pan no deriva. */
    v.zoom = 100.0;
    v.pan_x = 11.0;
    v.pan_y = -7.0;
    sev_map_zoom_at(&v, W, H, 300.0, 200.0, 1.2);
    CHECK(v.zoom == 100.0 && v.pan_x == 11.0 && v.pan_y == -7.0,
          "tope alto sin deriva");
    v.zoom = 0.2;
    sev_map_zoom_at(&v, W, H, 300.0, 200.0, 1.0 / 1.2);
    CHECK(v.zoom == 0.2 && v.pan_x == 11.0 && v.pan_y == -7.0,
          "tope bajo sin deriva");

    /* Factor invalido: no-op. */
    sev_map_zoom_at(&v, W, H, 300.0, 200.0, 0.0);
    sev_map_zoom_at(&v, W, H, 300.0, 200.0, -2.0);
    sev_map_zoom_at(&v, W, H, 300.0, 200.0, NAN);
    CHECK(v.zoom == 0.2 && v.pan_x == 11.0 && v.pan_y == -7.0,
          "factor invalido es no-op");

    /* Canvas sin tamano: aplica zoom, no toca pan. */
    v.zoom = 2.0;
    v.pan_x = 0.0;
    v.pan_y = 0.0;
    sev_map_zoom_at(&v, 0, 0, 300.0, 200.0, 1.2);
    CHECK(v.zoom > 2.39 && v.zoom < 2.41 && v.pan_x == 0.0 && v.pan_y == 0.0,
          "sin canvas no reancla");

    /* max_events: default 10, clamp 1..1000. */
    CHECK(sev_map_max_events(m) == 10, "max_events default 10");
    sev_map_set_max_events(m, 0);
    CHECK(sev_map_max_events(m) == 1, "max_events clamp bajo");
    sev_map_set_max_events(m, 5000);
    CHECK(sev_map_max_events(m) == 1000, "max_events clamp alto");
    sev_map_set_max_events(m, 7);
    CHECK(sev_map_max_events(m) == 7, "max_events 7");
    sev_map_set_max_events(m, 10);

    /* Escena: estacion en el centro (elev min = verde claro) y sismo lejos. */
    memset(&s, 0, sizeof s);
    snprintf(s.sta[0].sta, sizeof s.sta[0].sta, "AAA");
    s.sta[0].lat = -30.0;
    s.sta[0].lon = -70.0;
    s.sta[0].elev_m = 0.0;
    s.nsta = 1;
    s.elev_min = 0.0;
    s.elev_max = 1000.0;
    s.ev[0].lat = -20.0;
    s.ev[0].lon = -60.0;
    s.ev[0].mag = 5.0;
    s.ev[0].qid = 1;
    s.nev = 1;

    sev_map_view_init(&v);
    buf = new_buf(W, H);
    CHECK(buf != NULL, "buffer de pintado");
    sev_map_paint_bgra(m, &v, &s, buf, W, H, W * 4);
    CHECK(PX(buf, W, 400, 302, 1) > 200, "triangulo verde (G)");
    CHECK(PX(buf, W, 400, 302, 2) < 200, "triangulo verde (R)");
    CHECK(PX(buf, W, 411, 289, 2) > 200, "sismo mas nuevo rojo (R)");
    CHECK(PX(buf, W, 15, 240, 2) > 200, "leyenda clara");
    CHECK(PX(buf, W, 0, 0, 0) >= 60 && PX(buf, W, 0, 0, 0) <= 90,
          "marco gris");

    /* Capas: informacion, toggle y reload. */
    {
        SevLayerInfo li;
        CHECK(sev_map_nlayers(m) == 1, "una capa");
        CHECK(sev_map_layer_info(m, 0, &li) == 0, "layer_info");
        CHECK(strcmp(li.name, "ov") == 0, "nombre de la capa");
        CHECK(li.visible == 1, "capa visible");
        CHECK(li.rgb[0] > 0.99 && li.rgb[1] < 0.01 && li.rgb[2] < 0.01,
              "color rojo");
        CHECK(li.width == 2.0, "ancho de la capa");
        CHECK(li.polylines == 1 && li.vertices == 2, "polilineas/vertices");

        buf2 = new_buf(W, H);
        sev_map_paint_bgra(m, &v, &s, buf2, W, H, W * 4);
        CHECK(memcmp(buf, buf2, (size_t)W * H * 4) == 0,
              "dos pintados identicos");

        CHECK(sev_map_set_visible(m, 0, 0) == 0, "ocultar la capa");
        sev_map_layer_info(m, 0, &li);
        CHECK(li.visible == 0, "capa oculta");
        sev_map_paint_bgra(m, &v, &s, buf2, W, H, W * 4);
        CHECK(memcmp(buf, buf2, (size_t)W * H * 4) != 0,
              "ocultar la capa cambia la imagen");
        free(buf2);

        sev_map_reload_overlays(m);
        sev_map_layer_info(m, 0, &li);
        CHECK(li.visible == 1, "reload restaura la visibilidad del .d");
    }

    /* Export PNG/SVG/PDF. */
    snprintf(png, sizeof png, "%s/out.png", dir);
    snprintf(svg, sizeof svg, "%s/out.svg", dir);
    snprintf(pdf, sizeof pdf, "%s/out.pdf", dir);
    CHECK(sev_map_export(m, &v, &s, png, SEV_FMT_PNG, 320, 200) == 0,
          "export PNG");
    CHECK(sev_map_export(m, &v, &s, svg, SEV_FMT_SVG, 320, 200) == 0,
          "export SVG");
    CHECK(sev_map_export(m, &v, &s, pdf, SEV_FMT_PDF, 320, 200) == 0,
          "export PDF");
    CHECK(file_size(png) > 0 && file_size(svg) > 0 && file_size(pdf) > 0,
          "los tres exports tienen contenido");
    {
        unsigned pw = 0, ph = 0;
        CHECK(png_size(png, &pw, &ph) == 0 && pw == 320 && ph == 200,
              "PNG con las dimensiones pedidas");
    }

    /* Sin fondo: fondo de reemplazo y marco. */
    {
        SevMap *empty = sev_map_new(&cfg);
        unsigned char *eb = new_buf(400, 300);
        sev_map_view_init(&v);
        sev_map_paint_bgra(empty, &v, &s, eb, 400, 300, 400 * 4);
        CHECK(PX(eb, 400, 5, 5, 2) > 200 && PX(eb, 400, 5, 5, 1) > 220 &&
                  PX(eb, 400, 5, 5, 0) > 240,
              "fondo de reemplazo");
        free(eb);
        sev_map_free(empty);
    }

    free(buf);
    sev_map_free(m);

    snprintf(cmd, sizeof cmd, "rm -rf '%s'", dir);
    if (system(cmd) != 0)
        printf("nota: no se pudo limpiar %s\n", dir);

    if (g_fail == 0)
        printf("OK: mapa (proyeccion, zoom anclado, escena, capas y export)\n");
    return g_fail;
}
