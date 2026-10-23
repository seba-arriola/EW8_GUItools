/*
 * test_draw_map.c - Paneles de planta y perfil con contexto real (AC13).
 *
 * Verifica, sin Qt y sin GTK:
 *   - el mapa base se pinta (fondo sintetico) y cada tipo de marcador aparece con
 *     su color: solucion (gris), preferida (rojo), candidata (naranja), elegida
 *     (verde);
 *   - el auto-encuadre (bpq_map_fit) deja todos los puntos dentro del lienzo;
 *   - el perfil carga slab/topo de archivos y dibuja las polilineas y las
 *     soluciones dentro del marco;
 *   - los argumentos invalidos no petan.
 *
 * Uso: test_draw_map     (crea sus propios archivos de perfil en /tmp)
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bpq_capi.h"

#include "ewgui/geo.h"

static int g_fail = 0;
static int g_checks = 0;

#define CK(cond) do {                                                       \
    g_checks++;                                                             \
    if (!(cond)) {                                                          \
        g_fail++;                                                           \
        fprintf(stderr, "FALLA %s:%d  %s\n", __FILE__, __LINE__, #cond);     \
    }                                                                       \
} while (0)

static const int W = 400, H = 300;

/* Cuenta pixeles que cumplen r>g && r>b (rojo/naranja) o g dominante (verde). */
typedef enum { PX_RED, PX_GREEN, PX_DARK, PX_ORANGE } PxKind;

static int count_px(const unsigned char *b, int stride, PxKind k)
{
    int x, y, n = 0;
    for (y = 0; y < H; y++) {
        for (x = 0; x < W; x++) {
            const unsigned char *p = b + (size_t)y * stride + (size_t)x * 4;
            int bl = p[0], gr = p[1], rd = p[2];
            switch (k) {
            case PX_RED:    if (rd > 150 && gr < 90 && bl < 90) n++; break;
            case PX_ORANGE: if (rd > 200 && gr > 100 && gr < 180 && bl < 80) n++; break;
            case PX_GREEN:  if (gr > 130 && rd < 90 && bl < 90) n++; break;
            case PX_DARK:   if (rd < 90 && gr < 90 && bl < 90) n++; break;
            }
        }
    }
    return n;
}

/* Imagen de fondo sintetica: gradiente opaco (para distinguir del blanco). */
static void make_bg(unsigned char *bg, int w, int h)
{
    int x, y;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            unsigned char *p = bg + (size_t)y * w * 4 + (size_t)x * 4;
            p[0] = (unsigned char)(120 + (x * 60) / w);   /* B */
            p[1] = (unsigned char)(150 + (y * 60) / h);   /* G */
            p[2] = (unsigned char)(180);                  /* R */
            p[3] = 255;
        }
}

static void paint_map(const BpqConfig *cfg, const BpqMap *map, BpqMapCtx *c,
                      unsigned char *buf, int stride)
{
    double zoom, cx, cy;

    CK(bpq_map_fit(cfg, c, W, H, 12.0, &zoom, &cx, &cy) == 0);
    CK(zoom > 0.0);
    c->zoom = zoom;
    c->cx = cx;
    c->cy = cy;

    memset(buf, 0, (size_t)stride * H);
    CK(bpq_scene_paint(BPQ_SCENE_MAP_PLAN, c, buf, W, H, stride) == 0);
}

/* Posicion en pantalla segun el contrato de la vista (map_screen de mapview):
 *   s = (p - c) * zoom + lienzo/2 + pan
 * Se recalcula aqui a proposito, para no validar el helper con sigo mismo. */
static void expect_screen(const BpqMapCtx *c, const CsnhypodbpMapPt *p,
                          double iw, double ih, int w, int h,
                          double *sx, double *sy)
{
    double x = 0.0, y = 0.0;

    ewgui_geo_project(p->lat, p->lon, iw, ih, &x, &y);
    *sx = (x - c->cx) * c->zoom + w / 2.0 + c->pan_x;
    *sy = (y - c->cy) * c->zoom + h / 2.0 + c->pan_y;
}

/* Escribe un .tmp de perfil de `n` puntos con 7 columnas (lon en 3, valor en 7). */
static int write_profile_file(const char *path, const double *lon,
                             const double *val, int n, double step)
{
    FILE *f = fopen(path, "w");
    int i;
    if (!f) return -1;
    for (i = 0; i < n; i++) {
        /* Columnas 1-based 3 = lon y 7 = valor. */
        fprintf(f, "0 0 %.4f 0 0 0 %.4f\n", lon[i], val[i]);
        (void)step;
    }
    fclose(f);
    return 0;
}

int main(void)
{
    BpqConfig cfg;
    BpqMap *map = NULL;
    unsigned char *buf;
    int stride = 4 * W;
    static unsigned char bg[360 * 180 * 4];
    CsnhypodbpMapPt pts[3];
    BpqMapCtx c;
    int nred, norange, ngreen, ndark;

    printf("== test_draw_map ==\n");
    memset(&cfg, 0, sizeof(cfg));
    cfg.map_min_span_deg = 5.0;
    cfg.map_zoom_factor = 1.0;

    /* Sin overlays: el mapa dibuja solo fondo + grilla + perfiles + marcadores. */
    map = bpq_map_new(&cfg);
    CK(map != NULL);
    CK(bpq_map_nlayers(map) == 0);

    buf = (unsigned char *)malloc((size_t)stride * H);
    CK(buf != NULL);
    if (!buf) return 1;
    make_bg(bg, 360, 180);

    pts[0].lat = -20.10; pts[0].lon = -70.10; pts[0].depth = 30.0; pts[0].mag = 2.5; pts[0].kind = 0;
    pts[1].lat = -20.20; pts[1].lon = -70.20; pts[1].depth = 35.0; pts[1].mag = 3.1; pts[1].kind = 1;
    pts[2].lat = -20.30; pts[2].lon = -70.30; pts[2].depth = 40.0; pts[2].mag = 2.9; pts[2].kind = 2;

    memset(&c, 0, sizeof(c));
    c.bg = bg; c.bg_w = 360; c.bg_h = 180; c.bg_stride = 4 * 360;
    c.pts = pts; c.npts = 3;
    c.preferred = 1;
    c.selected = -1;
    c.active_profile = -1;
    c.show_grid = 1;
    c.map = map;

    /* --- 1. solo la solucion comun (kind 0): debe haber pixeles oscuros --- */
    {
        CsnhypodbpMapPt one = pts[0];
        c.pts = &one; c.npts = 1; c.preferred = -1;
        paint_map(&cfg, map, &c, buf, stride);
        ndark = count_px(buf, stride, PX_DARK);
        CK(ndark > 10);            /* el marcador gris + el marco de la grilla */
        CK(count_px(buf, stride, PX_RED) == 0);
        CK(count_px(buf, stride, PX_GREEN) == 0);
    }

    /* --- 2. preferida (rojo) --- */
    {
        CsnhypodbpMapPt one = pts[1];
        c.pts = &one; c.npts = 1; c.preferred = 0;
        paint_map(&cfg, map, &c, buf, stride);
        nred = count_px(buf, stride, PX_RED);
        CK(nred > 20);
        CK(count_px(buf, stride, PX_GREEN) == 0);
    }

    /* --- 3. candidata (naranja) --- */
    {
        CsnhypodbpMapPt one = pts[2];
        c.pts = &one; c.npts = 1; c.preferred = -1;
        paint_map(&cfg, map, &c, buf, stride);
        norange = count_px(buf, stride, PX_ORANGE);
        CK(norange > 20);
    }

    /* --- 4. elegida en Origins: verde y sustituye al rojo de la preferida --- */
    {
        CsnhypodbpMapPt one = pts[1];
        c.pts = &one; c.npts = 1; c.preferred = 0; c.selected = 0;
        paint_map(&cfg, map, &c, buf, stride);
        ngreen = count_px(buf, stride, PX_GREEN);
        CK(ngreen > 20);
        CK(count_px(buf, stride, PX_RED) == 0);
        /* el mapa base se pinto (no es todo blanco) */
        CK(buf[0] != 0 || buf[1] != 0 || buf[2] != 0);
    }

    /* --- 5. los 3 puntos a la vez quedan dentro del lienzo --- */
    {
        double zoom, cx, cy;
        double iw = 360.0, ih = 180.0;
        int i;

        c.pts = pts; c.npts = 3; c.preferred = 1; c.selected = 2;
        paint_map(&cfg, map, &c, buf, stride);
        zoom = c.zoom; cx = c.cx; cy = c.cy;
        CK(bpq_map_fit(&cfg, &c, W, H, 12.0, &zoom, &cx, &cy) == 0);
        for (i = 0; i < 3; i++) {
            double x, y, sx, sy;
            ewgui_geo_project(pts[i].lat, pts[i].lon, iw, ih, &x, &y);
            sx = (x - cx) * zoom + W / 2.0;
            sy = (y - cy) * zoom + H / 2.0;
            CK(sx >= 0.0 && sx <= W);
            CK(sy >= 0.0 && sy <= H);
        }
        /* Con la elegida en verde, la preferida roja ya no se ve. */
        CK(count_px(buf, stride, PX_GREEN) > 20);
    }

    /* --- 6. sin puntos: el panel NO queda vacio (marco, grilla y etiquetas) --- */
    c.pts = NULL; c.npts = 0;
    CK(bpq_map_fit(&cfg, &c, W, H, 12.0, NULL, NULL, NULL) == -1);
    CK(bpq_scene_paint(BPQ_SCENE_MAP_PLAN, &c, buf, W, H, stride) == 0);
    /* Regresion: al arrancar sin soluciones el panel se veia liso (sin ejes). */
    CK(count_px(buf, stride, PX_DARK) > 200);
    CK(bpq_map_fit(NULL, &c, W, H, 12.0, NULL, NULL, NULL) == -1);

    /* Encuadre del mundo: deja el mapa y la grilla dentro del lienzo. */
    {
        double wz = 0.0, wcx = 0.0, wcy = 0.0;
        bpq_map_fit_world(&c, W, H, 8.0, &wz, &wcx, &wcy);
        CK(wz > 0.0);
        CK(360.0 * wz <= W + 0.001);
        CK(180.0 * wz <= H + 0.001);
        CK(wcx == 180.0);
        CK(wcy == 90.0);
    }
    /* 2x (HiDPI): el marco sigue dibujandose y no se sale del lienzo. */
    c.dpr = 2.0;
    c.show_grid = 1;
    CK(bpq_scene_paint(BPQ_SCENE_MAP_PLAN, &c, buf, W, H, stride) == 0);
    CK(count_px(buf, stride, PX_DARK) > 200);
    c.dpr = 0.0;

    /* --- 7. interaccion del mapa: paneo, zoom anclado y clic --- */
    {
        const double iw = 360.0, ih = 180.0;
        double sx, sy, sx2, sy2, z0;
        int idx;

        c.pts = pts; c.npts = 3; c.preferred = 1; c.selected = -1;
        c.pan_x = c.pan_y = 0.0;
        paint_map(&cfg, map, &c, buf, stride);        /* fija zoom/cx/cy */
        z0 = c.zoom;
        CK(z0 > 0.0);

        /* Paneo: el contenido se mueve exactamente lo arrastrado. */
        expect_screen(&c, &pts[0], iw, ih, W, H, &sx, &sy);
        bpq_map_pan(&c, 30.0, -20.0);
        expect_screen(&c, &pts[0], iw, ih, W, H, &sx2, &sy2);
        CK(fabs((sx2 - sx) - 30.0) < 1e-6);
        CK(fabs((sy2 - sy) + 20.0) < 1e-6);

        /* Zoom x2 con el cursor ENCIMA del marcador: no se mueve. */
        expect_screen(&c, &pts[1], iw, ih, W, H, &sx, &sy);
        bpq_map_zoom_at(&c, W, H, sx, sy, 2.0);
        CK(fabs(c.zoom - 2.0 * z0) < 1e-9);
        expect_screen(&c, &pts[1], iw, ih, W, H, &sx2, &sy2);
        CK(fabs(sx2 - sx) < 0.5);
        CK(fabs(sy2 - sy) < 0.5);

        /* El zoom se acota (no se puede perder el mapa). */
        bpq_map_zoom_at(&c, W, H, W / 2.0, H / 2.0, 1e9);
        CK(c.zoom <= 1e4 + 1e-6);
        bpq_map_zoom_at(&c, W, H, W / 2.0, H / 2.0, 1e-12);
        CK(c.zoom >= 1e-4 - 1e-12);

        /* Clic sobre un marcador (hit test por cercania). */
        c.zoom = z0; c.pan_x = c.pan_y = 0.0;
        expect_screen(&c, &pts[1], iw, ih, W, H, &sx, &sy);
        idx = bpq_map_hit_test(&c, W, H, sx, sy, 10.0);
        CK(idx == 1);
        CK(bpq_map_hit_test(&c, W, H, sx + 40.0, sy + 40.0, 5.0) == -1);
        CK(bpq_map_hit_test(&c, W, H, sx + 3.0, sy + 3.0, 8.0) == 1);
        c.npts = 0;
        CK(bpq_map_hit_test(&c, W, H, sx, sy, 10.0) == -1);
        c.npts = 3;
        CK(bpq_map_hit_test(NULL, W, H, sx, sy, 10.0) == -1);
    }

    bpq_map_free(map);

    /* --- 7. perfil: slab + topo + soluciones --- */
    {
        const char *slab_path = "/tmp/bpq_test_slab.tmp";
        const char *topo_path = "/tmp/bpq_test_topo.tmp";
        double lon[5], slab_v[5], topo_v[5];
        BpqProfileData *pd;
        BpqProfileCtx pc;
        CsnhypodbpMapPt sp[2];
        char err[256] = "";
        int i;

        for (i = 0; i < 5; i++) {
            lon[i] = -71.0 + 0.5 * i;
            slab_v[i] = -100.0 - 20.0 * i;   /* profundidad negativa (km) */
            topo_v[i] = 500.0 + 100.0 * i;   /* elevacion en metros */
        }
        CK(write_profile_file(slab_path, lon, slab_v, 5, 0.5) == 0);
        CK(write_profile_file(topo_path, lon, topo_v, 5, 0.5) == 0);

        memset(&cfg, 0, sizeof(cfg));
        cfg.profile_lon_col = 3;
        cfg.profile_val_col = 7;
        cfg.profile_depth_min_km = 200.0;
        cfg.profiles.n = 1;
        snprintf(cfg.profiles.p[0].name, sizeof(cfg.profiles.p[0].name), "%s", "PT");
        snprintf(cfg.profiles.p[0].slab_file, sizeof(cfg.profiles.p[0].slab_file), "%s", slab_path);
        snprintf(cfg.profiles.p[0].topo_file, sizeof(cfg.profiles.p[0].topo_file), "%s", topo_path);
        cfg.profiles.p[0].lon1 = -72.0; cfg.profiles.p[0].lat1 = -20.0;
        cfg.profiles.p[0].lon2 = -66.0; cfg.profiles.p[0].lat2 = -20.0;
        cfg.profiles.p[0].a = 0.0; cfg.profiles.p[0].b = -20.0;

        CK(bpq_profile_count(&cfg) == 1);
        CK(bpq_profile_nearest(&cfg, -20.0, -70.0) == 0);
        CK(bpq_profile_nearest(&cfg, -20.0, -10.0) == 0);   /* solo hay uno */

        pd = bpq_profile_load(&cfg, 0, err, sizeof(err));
        CK(pd != NULL);
        CK(strcmp(bpq_profile_name(pd), "PT") == 0);
        CK(bpq_profile_line_n(pd) == 5);
        CK(bpq_profile_load(&cfg, 9, err, sizeof(err)) == NULL);
        CK(bpq_profile_load(NULL, 0, err, sizeof(err)) == NULL);

        memset(&pc, 0, sizeof(pc));
        sp[0].lat = -20.0; sp[0].lon = -70.0; sp[0].depth = 120.0; sp[0].kind = 1;
        /* Dentro del rango de longitudes del slab (-71..-69): fuera del marco el
         * marcador se recorta a proposito (el panel se encuadra a sus datos). */
        sp[1].lat = -20.0; sp[1].lon = -69.5; sp[1].depth = 60.0;  sp[1].kind = 0;
        pc.pts = sp; pc.npts = 2; pc.preferred = 0; pc.selected = 1;
        pc.depth_min_km = 200.0; pc.zoom = 1.0; pc.prof = pd;

        memset(buf, 0, (size_t)stride * H);
        CK(bpq_scene_paint(BPQ_SCENE_MAP_PROF, &pc, buf, W, H, stride) == 0);
        /* Fondo blanco (a diferencia del mapa de planta) y trazos negros. */
        CK(buf[3] == 255);
        CK(count_px(buf, stride, PX_DARK) > 50);
        CK(count_px(buf, stride, PX_GREEN) > 10);   /* la elegida, en verde */

        /* Sin perfil cargado el panel sigue dibujando: marco por defecto (sur de
         * Chile) con ejes, no una caja degenerada. */
        pc.prof = NULL;
        CK(bpq_scene_paint(BPQ_SCENE_MAP_PROF, &pc, buf, W, H, stride) == 0);
        CK(count_px(buf, stride, PX_DARK) > 100);

        /* Interaccion: el paneo se convierte a unidades del marco (el contenido
         * sigue al cursor en ambos ejes) y el zoom deja quieto el dato de abajo. */
        {
            double px0, py0, px1, py1;

            pc.prof = pd;
            pc.zoom = 1.0;
            pc.pan_x = pc.pan_y = 0.0;
            CK(bpq_profile_project(&pc, W, H, sp[1].lon, sp[1].depth,
                                   &px0, &py0) == 0);
            bpq_profile_pan(&pc, W, H, 12.0, -8.0);
            CK(bpq_profile_project(&pc, W, H, sp[1].lon, sp[1].depth,
                                   &px1, &py1) == 0);
            CK(fabs((px1 - px0) - 12.0) < 0.01);
            CK(fabs((py1 - py0) + 8.0) < 0.01);

            bpq_profile_zoom_at(&pc, W, H, px1, py1, 2.0);
            CK(fabs(pc.zoom - 2.0) < 1e-9);
            CK(bpq_profile_project(&pc, W, H, sp[1].lon, sp[1].depth,
                                   &px1, &py1) == 0);
            CK(fabs(px1 - px0 - 12.0) < 0.05);
            CK(fabs(py1 - py0 + 8.0) < 0.05);
        }

        bpq_profile_free(pd);
        remove(slab_path);
        remove(topo_path);
    }

    free(buf);
    printf("== draw_map checks=%d fallas=%d ==\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
