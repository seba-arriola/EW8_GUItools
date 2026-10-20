/*
 * Test headless de la geometria de los paneles de mapa (ver
 * csnhypodbp_mapview.h): bbox de puntos, auto-encuadre del mapa de planta y
 * marco del panel de perfil (x = lon, y = -profundidad).
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ewgui/geo.h"

#include "csnhypodbp_mapview.h"

static int fails = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); fails++; } \
    else printf("ok: %s\n", msg); \
} while (0)

#define IMG_W 10800.0
#define IMG_H 5400.0

static void test_bbox(void)
{
    CsnhypodbpMapPt pts[4];
    double lon0 = 0, lon1 = 0, lat0 = 0, lat1 = 0;

    memset(pts, 0, sizeof(pts));
    pts[0].lat = -20.0; pts[0].lon = -70.0; pts[0].kind = 1;
    pts[1].lat = -22.0; pts[1].lon = -71.5; pts[1].kind = 0;
    pts[2].lat = 0.0;   pts[2].lon = 0.0;   pts[2].kind = 0;   /* sin posicion */
    pts[3].lat = NAN;   pts[3].lon = -68.0; pts[3].kind = 0;   /* no finito */

    CHECK(csnhypodbp_map_bbox(pts, 4, &lon0, &lon1, &lat0, &lat1) == 0,
          "bbox de 2 puntos validos");
    CHECK(fabs(lon0 + 71.5) < 1e-12 && fabs(lon1 + 70.0) < 1e-12,
          "bbox: lon0/lon1");
    CHECK(fabs(lat0 + 22.0) < 1e-12 && fabs(lat1 + 20.0) < 1e-12,
          "bbox: lat0/lat1");

    CHECK(csnhypodbp_map_bbox(&pts[2], 1, &lon0, &lon1, &lat0, &lat1) == -1,
          "solo (0,0) -> -1");
    CHECK(csnhypodbp_map_bbox(&pts[3], 1, &lon0, &lon1, &lat0, &lat1) == -1,
          "solo NaN -> -1");
    CHECK(csnhypodbp_map_bbox(NULL, 3, &lon0, &lon1, &lat0, &lat1) == -1,
          "NULL -> -1");
    CHECK(csnhypodbp_map_bbox(pts, 0, &lon0, &lon1, &lat0, &lat1) == -1,
          "n = 0 -> -1");

    /* Un solo punto valido: bbox degenerado pero valido. */
    CHECK(csnhypodbp_map_bbox(&pts[0], 1, &lon0, &lon1, &lat0, &lat1) == 0 &&
          fabs(lon0 - lon1) < 1e-12 && fabs(lat0 - lat1) < 1e-12,
          "un solo punto: bbox de lado cero");
}

/* Vertice (lat,lon) -> pixel de pantalla con la transformacion de csnrv. */
static void corner_px(double lat, double lon, double z, double cx, double cy,
                      double *sx, double *sy)
{
    double x, y;
    ewgui_geo_project(lat, lon, IMG_W, IMG_H, &x, &y);
    *sx = (x - cx) * z + 200.0;
    *sy = (y - cy) * z + 150.0;
}

static void test_mapfit(void)
{
    const double cw = 400.0, ch = 300.0, margin = 8.0;
    const double util_x = cw - 2.0 * margin, util_y = ch - 2.0 * margin;
    double z, cx, cy, sx, sy;
    double xa, ya, xb, yb, spanx, spany;
    int i;
    const double lats[4] = { -17.0, -40.0, -17.0, -40.0 };
    const double lons[4] = { -76.0, -66.0, -66.0, -76.0 };

    /* Chile: lon -76..-66, lat -40..-17. */
    csnhypodbp_mapfit(-76.0, -66.0, -40.0, -17.0, IMG_W, IMG_H, (int)cw, (int)ch,
                      margin, &z, &cx, &cy);
    CHECK(isfinite(z) && z > 0.0, "mapfit: zoom finito y positivo");

    for (i = 0; i < 4; i++) {
        corner_px(lats[i], lons[i], z, cx, cy, &sx, &sy);
        if (!(sx >= margin - 1e-6 && sx <= cw - margin + 1e-6 &&
              sy >= margin - 1e-6 && sy <= ch - margin + 1e-6)) {
            printf("FAIL: vertice %d fuera del canvas (%.3f, %.3f)\n", i, sx, sy);
            fails++;
        }
    }
    CHECK(1, "mapfit: los 4 vertices del bbox entran en el canvas");

    ewgui_geo_project(-17.0, -76.0, IMG_W, IMG_H, &xa, &ya);
    ewgui_geo_project(-40.0, -66.0, IMG_W, IMG_H, &xb, &yb);
    spanx = fabs(xb - xa) * z;
    spany = fabs(yb - ya) * z;
    CHECK(spanx <= util_x + 1e-6 && spany <= util_y + 1e-6,
          "mapfit: el bbox no se sale del area util");
    CHECK(spanx >= util_x - 1e-6 || spany >= util_y - 1e-6,
          "mapfit: la dimension mas ajustada llena el area util");
    CHECK((spanx / util_x > spany / util_y ? spanx / util_x : spany / util_y) >= 0.8,
          "mapfit: ocupa al menos el 80% del area util");

    /* Extremos en orden invertido: mismo resultado. */
    {
        double z2, cx2, cy2;
        csnhypodbp_mapfit(-66.0, -76.0, -17.0, -40.0, IMG_W, IMG_H, (int)cw, (int)ch,
                          margin, &z2, &cx2, &cy2);
        CHECK(fabs(z2 - z) < 1e-9 && fabs(cx2 - cx) < 1e-9 && fabs(cy2 - cy) < 1e-9,
              "mapfit: extremos invertidos dan lo mismo");
    }

    /* Bbox degenerado (un solo punto): sin inf ni NaN. */
    csnhypodbp_mapfit(-70.0, -70.0, -30.0, -30.0, IMG_W, IMG_H, (int)cw, (int)ch,
                      margin, &z, &cx, &cy);
    CHECK(isfinite(z) && z > 0.0 && isfinite(cx) && isfinite(cy),
          "mapfit: bbox degenerado sin inf/NaN");

    /* Margen mayor que el canvas: no rompe. */
    csnhypodbp_mapfit(-76.0, -66.0, -40.0, -17.0, IMG_W, IMG_H, 10, 10, 20.0,
                      &z, &cx, &cy);
    CHECK(isfinite(z) && z > 0.0, "mapfit: margen mayor que el canvas no rompe");
}

static void test_split_parse(void)
{
    double f = -1.0;

    CHECK(csnhypodbp_map_split_parse("7/3", &f) == 1 && fabs(f - 0.7) < 1e-9,
          "split: 7/3 -> 0.7");
    CHECK(csnhypodbp_map_split_parse("0.65", &f) == 1 && fabs(f - 0.65) < 1e-9,
          "split: 0.65 -> 0.65");
    CHECK(csnhypodbp_map_split_parse(" 8 / 2 ", &f) == 1 && fabs(f - 0.8) < 1e-9,
          "split: admite espacios alrededor de /");
    CHECK(csnhypodbp_map_split_parse("7/0", &f) == 0, "split: denominador 0 -> 0");
    CHECK(csnhypodbp_map_split_parse("0", &f) == 0, "split: 0 esta fuera de (0,1)");
    CHECK(csnhypodbp_map_split_parse("1", &f) == 0, "split: 1 esta fuera de (0,1)");
    CHECK(csnhypodbp_map_split_parse("x", &f) == 0, "split: texto -> 0");
    CHECK(csnhypodbp_map_split_parse("7/3x", &f) == 0, "split: basura al final -> 0");
    CHECK(csnhypodbp_map_split_parse("", &f) == 0, "split: vacio -> 0");
    CHECK(csnhypodbp_map_split_parse(NULL, &f) == 0, "split: NULL -> 0");
}

static void test_mapfit_at(void)
{
    const double cw = 400.0, ch = 300.0, margin = 8.0;
    double z1, cx1, cy1, z2, cx2, cy2, sx, sy;

    /* Referencia: un punto DENTRO del bbox (no el centro geometrico). */
    csnhypodbp_mapfit_at(-33.0, -71.5, -76.0, -66.0, -40.0, -17.0, IMG_W, IMG_H,
                         (int)cw, (int)ch, margin, 1.0, &z1, &cx1, &cy1);
    csnhypodbp_mapfit(-76.0, -66.0, -40.0, -17.0, IMG_W, IMG_H, (int)cw, (int)ch,
                      margin, &z2, &cx2, &cy2);
    CHECK(fabs(z1 - z2) < 1e-9, "mapfit_at: zoom 1.0 = auto-encuadre del bbox");
    corner_px(-33.0, -71.5, z1, cx1, cy1, &sx, &sy);
    CHECK(fabs(sx - cw / 2.0) < 1e-6 && fabs(sy - ch / 2.0) < 1e-6,
          "mapfit_at: la referencia proyecta al centro del canvas");
    CHECK(fabs(cx1 - cx2) > 1e-9 || fabs(cy1 - cy2) > 1e-9,
          "mapfit_at: el centro NO es el punto medio del bbox");

    /* MapZoom: factor sobre el auto-encuadre, sin mover la referencia. */
    csnhypodbp_mapfit_at(-33.0, -71.5, -76.0, -66.0, -40.0, -17.0, IMG_W, IMG_H,
                         (int)cw, (int)ch, margin, 2.0, &z1, &cx1, &cy1);
    CHECK(fabs(z1 - 2.0 * z2) < 1e-9, "mapfit_at: factor 2.0 duplica el zoom");
    corner_px(-33.0, -71.5, z1, cx1, cy1, &sx, &sy);
    CHECK(fabs(sx - cw / 2.0) < 1e-6 && fabs(sy - ch / 2.0) < 1e-6,
          "mapfit_at: con zoom 2.0 la referencia sigue en el centro");

    /* Sin referencia valida -> punto medio del bbox (como mapfit). */
    csnhypodbp_mapfit_at(0.0, 0.0, -76.0, -66.0, -40.0, -17.0, IMG_W, IMG_H,
                         (int)cw, (int)ch, margin, 1.0, &z1, &cx1, &cy1);
    CHECK(fabs(z1 - z2) < 1e-9 && fabs(cx1 - cx2) < 1e-9 && fabs(cy1 - cy2) < 1e-9,
          "mapfit_at: sin referencia vuelve al bbox");
    csnhypodbp_mapfit_at(NAN, NAN, -76.0, -66.0, -40.0, -17.0, IMG_W, IMG_H,
                         (int)cw, (int)ch, margin, 1.0, &z1, &cx1, &cy1);
    CHECK(fabs(cx1 - cx2) < 1e-9 && fabs(cy1 - cy2) < 1e-9,
          "mapfit_at: referencia NaN vuelve al bbox");

    /* Factor invalido -> 1.0. */
    csnhypodbp_mapfit_at(-33.0, -71.5, -76.0, -66.0, -40.0, -17.0, IMG_W, IMG_H,
                         (int)cw, (int)ch, margin, 0.0, &z1, &cx1, &cy1);
    CHECK(fabs(z1 - z2) < 1e-9, "mapfit_at: factor 0 -> automatico");
    csnhypodbp_mapfit_at(-33.0, -71.5, -76.0, -66.0, -40.0, -17.0, IMG_W, IMG_H,
                         (int)cw, (int)ch, margin, -3.0, &z1, &cx1, &cy1);
    CHECK(fabs(z1 - z2) < 1e-9, "mapfit_at: factor negativo -> automatico");
}

static void test_span_min(void)
{
    double lon0, lon1, lat0, lat1;

    /* bbox apretado -> se expande simetricamente hasta el minimo. */
    lon0 = -70.2; lon1 = -70.0; lat0 = -30.1; lat1 = -29.9;
    csnhypodbp_map_span_min(&lon0, &lon1, &lat0, &lat1, 5.0);
    CHECK(fabs(lon0 + 72.6) < 1e-9 && fabs(lon1 + 67.6) < 1e-9 &&
          fabs(lat0 + 32.5) < 1e-9 && fabs(lat1 + 27.5) < 1e-9,
          "span_min: bbox apretado se expande a 5 grados centrado");

    /* bbox ya mas grande: no se toca. */
    lon0 = -76.0; lon1 = -66.0; lat0 = -40.0; lat1 = -17.0;
    csnhypodbp_map_span_min(&lon0, &lon1, &lat0, &lat1, 5.0);
    CHECK(lon0 == -76.0 && lon1 == -66.0 && lat0 == -40.0 && lat1 == -17.0,
          "span_min: bbox mas grande queda igual");

    /* Solo un lado corto: el otro no se toca. */
    lon0 = -70.2; lon1 = -70.0; lat0 = -40.0; lat1 = -17.0;
    csnhypodbp_map_span_min(&lon0, &lon1, &lat0, &lat1, 5.0);
    CHECK(fabs(lon0 + 72.6) < 1e-9 && fabs(lon1 + 67.6) < 1e-9 &&
          lat0 == -40.0 && lat1 == -17.0,
          "span_min: solo el lado corto se expande");

    /* No se sale del mundo (contrato del mapa base). */
    lon0 = 179.0; lon1 = 179.5; lat0 = -89.0; lat1 = -88.5;
    csnhypodbp_map_span_min(&lon0, &lon1, &lat0, &lat1, 5.0);
    CHECK(lon1 <= 180.0 && lat0 >= -90.0 && lat1 <= 90.0 && lon0 <= lon1,
          "span_min: recortado al mundo");

    /* min_span invalido: no hace nada. */
    lon0 = -70.2; lon1 = -70.0; lat0 = -30.1; lat1 = -29.9;
    csnhypodbp_map_span_min(&lon0, &lon1, &lat0, &lat1, 0.0);
    csnhypodbp_map_span_min(&lon0, &lon1, &lat0, &lat1, -3.0);
    csnhypodbp_map_span_min(&lon0, &lon1, &lat0, &lat1, NAN);
    CHECK(lon0 == -70.2 && lon1 == -70.0 && lat0 == -30.1 && lat1 == -29.9,
          "span_min: <= 0 o NaN no cambia nada");
    csnhypodbp_map_span_min(NULL, NULL, NULL, NULL, 5.0);
    CHECK(1, "span_min: punteros NULL no revientan");
}

static void test_profile_frame(void)
{
    CsnhypodbpProfileLine slab, topo;
    CsnhypodbpFrame f;
    double x0, x1, y0, y1;

    memset(&slab, 0, sizeof(slab));
    memset(&topo, 0, sizeof(topo));

    /* Sin datos: rango de respaldo. */
    csnhypodbp_profile_frame(&slab, &topo, 200.0, &x0, &x1, &y0, &y1);
    CHECK(fabs(x0 + 76.2) < 1e-9 && fabs(x1 + 65.8) < 1e-9 &&
          y0 < -200.0 && y1 >= 10.0,
          "profile_frame: sin datos usa el rango de respaldo");

    /* Slab de -20 a -600 km y topo hasta +5 km: todo entra en el marco. */
    slab.n = 3; slab.lon_min = -72.0; slab.lon_max = -68.0;
    slab.val_min = -600.0; slab.val_max = -20.0;
    topo.n = 4; topo.lon_min = -72.5; topo.lon_max = -67.5;
    topo.val_min = -4000.0; topo.val_max = 5000.0;
    csnhypodbp_profile_frame(&slab, &topo, 200.0, &x0, &x1, &y0, &y1);
    CHECK(x0 < -72.5 && x1 > -67.5, "profile_frame: x cubre slab y topo con margen");
    CHECK(y0 <= -600.0 && y1 >= 5.0,
          "profile_frame: y cubre el slab mas profundo y la topografia");

    /* Contencion real: los cuatro extremos caen dentro del area de dibujo. */
    csnhypodbp_frame_set(&f, x0, x1, y0, y1, 10.0, 10.0, 300.0, 200.0);
    {
        double sx, sy;
        int bad = 0;
        csnhypodbp_frame_project(&f, slab.lon_min, slab.val_min, &sx, &sy);
        if (sx < 10.0 || sx > 310.0 || sy < 10.0 || sy > 210.0) bad++;
        csnhypodbp_frame_project(&f, slab.lon_max, slab.val_max, &sx, &sy);
        if (sx < 10.0 || sx > 310.0 || sy < 10.0 || sy > 210.0) bad++;
        csnhypodbp_frame_project(&f, topo.lon_min, topo.val_max / 1000.0, &sx, &sy);
        if (sx < 10.0 || sx > 310.0 || sy < 10.0 || sy > 210.0) bad++;
        csnhypodbp_frame_project(&f, topo.lon_max, topo.val_min / 1000.0, &sx, &sy);
        if (sx < 10.0 || sx > 310.0 || sy < 10.0 || sy > 210.0) bad++;
        CHECK(bad == 0, "profile_frame: los extremos caen dentro del area");
    }

    /* ProfileDepthMinKm manda cuando es mas profundo que el slab. */
    slab.val_min = -50.0;
    csnhypodbp_profile_frame(&slab, &topo, 300.0, &x0, &x1, &y0, &y1);
    CHECK(y0 <= -300.0, "profile_frame: ProfileDepthMinKm manda si es mas profundo");

    /* Solo slab (sin topografia) tambien da un marco valido. */
    csnhypodbp_profile_frame(&slab, NULL, 200.0, &x0, &x1, &y0, &y1);
    CHECK(x1 > x0 && y1 > y0, "profile_frame: solo slab");
    csnhypodbp_profile_frame(NULL, NULL, 0.0, &x0, &x1, &y0, &y1);
    CHECK(x1 > x0 && y1 > y0, "profile_frame: sin argumentos utiles no rompe");
}

static void test_frame_view(void)
{
    CsnhypodbpFrame f;

    csnhypodbp_frame_set(&f, -72.0, -68.0, -200.0, 0.0, 0.0, 0.0, 100.0, 100.0);
    csnhypodbp_frame_view(&f, 2.0, 0.0, 0.0);
    CHECK(fabs(f.x0 + 71.0) < 1e-9 && fabs(f.x1 + 69.0) < 1e-9 &&
          fabs(f.y0 + 150.0) < 1e-9 && fabs(f.y1 + 50.0) < 1e-9,
          "frame_view: zoom 2 reduce el span a la mitad y mantiene el centro");

    csnhypodbp_frame_set(&f, -72.0, -68.0, -200.0, 0.0, 0.0, 0.0, 100.0, 100.0);
    csnhypodbp_frame_view(&f, 1.0, 1.0, -50.0);
    CHECK(fabs(f.x0 + 71.0) < 1e-9 && fabs(f.x1 + 67.0) < 1e-9 &&
          fabs(f.y0 + 250.0) < 1e-9 && fabs(f.y1 + 50.0) < 1e-9,
          "frame_view: el paneo desplaza en unidades de datos");

    csnhypodbp_frame_view(&f, 0.0, 0.0, 0.0);
    CHECK(fabs(f.x1 + 67.0) < 1e-9, "frame_view: zoom <= 0 se ignora");
    csnhypodbp_frame_view(NULL, 2.0, 0.0, 0.0);
    CHECK(1, "frame_view: NULL no revienta");
}

static void test_hit(void)
{
    CsnhypodbpMapPt pts[3];
    double sx[3] = { 10.0, 20.0, 30.0 }, sy[3] = { 10.0, 10.0, 10.0 };

    memset(pts, 0, sizeof(pts));
    pts[0].kind = 0; pts[1].kind = 1; pts[2].kind = 2;

    CHECK(csnhypodbp_map_hit(pts, sx, sy, 3, 10.0, 10.0, 7.0) == 0,
          "hit: acierta el marcador bajo el puntero");
    CHECK(csnhypodbp_map_hit(pts, sx, sy, 3, 12.0, 12.0, 7.0) == 0,
          "hit: dentro del radio");
    CHECK(csnhypodbp_map_hit(pts, sx, sy, 3, 50.0, 50.0, 7.0) == -1,
          "hit: lejos -> -1");
    CHECK(csnhypodbp_map_hit(pts, sx, sy, 3, 30.0, 10.0, 7.0) == 2,
          "hit: acierta la candidata");
    CHECK(csnhypodbp_map_hit(pts, sx, sy, 3, 14.0, 10.0, 7.0) == 1,
          "hit: elige el mas cercano dentro del radio");

    /* Los tres superpuestos: gana el que se dibuja encima. */
    sx[0] = sx[1] = sx[2] = 25.0;
    CHECK(csnhypodbp_map_hit(pts, sx, sy, 3, 25.0, 10.0, 7.0) == 2,
          "hit: superpuestos gana la candidata");
    pts[2].kind = 0;
    CHECK(csnhypodbp_map_hit(pts, sx, sy, 3, 25.0, 10.0, 7.0) == 1,
          "hit: sin candidata gana la preferida");

    CHECK(csnhypodbp_map_hit(pts, sx, sy, 0, 0.0, 0.0, 7.0) == -1, "hit: n = 0 -> -1");
    CHECK(csnhypodbp_map_hit(pts, sx, sy, 3, 25.0, 10.0, 0.0) == -1,
          "hit: radio 0 -> -1");
    CHECK(csnhypodbp_map_hit(NULL, NULL, NULL, 3, 0.0, 0.0, 7.0) == -1,
          "hit: punteros NULL -> -1");

    CHECK(csnhypodbp_map_drag_exceeded(1.0, 1.0, 3.0) == 0, "drag: bajo el umbral");
    CHECK(csnhypodbp_map_drag_exceeded(4.0, 0.0, 3.0) == 1, "drag: supera el umbral");
    CHECK(csnhypodbp_map_drag_exceeded(0.0, -5.0, 3.0) == 1, "drag: tambien en vertical");
    CHECK(csnhypodbp_map_drag_exceeded(NAN, 0.0, 3.0) == 0, "drag: NaN no cuenta");
}

static void test_find_id(void)
{
    static const char ids[3][CSNHYPODBP_MAP_ID_LEN] = { "1/2", "3/4", "5/6" };
    static const char dup[2][CSNHYPODBP_MAP_ID_LEN] = { "1/2", "1/2" };
    static const char hueco[2][CSNHYPODBP_MAP_ID_LEN] = { "", "1/2" };

    CHECK(csnhypodbp_map_find_id(ids, 3, "3/4") == 1, "find_id: encuentra el id");
    CHECK(csnhypodbp_map_find_id(ids, 3, "5/6") == 2, "find_id: tambien el ultimo");
    CHECK(csnhypodbp_map_find_id(ids, 3, "9/9") == -1, "find_id: id ausente -> -1");
    CHECK(csnhypodbp_map_find_id(dup, 2, "1/2") == 0, "find_id: duplicado gana el primero");
    CHECK(csnhypodbp_map_find_id(hueco, 2, "1/2") == 1, "find_id: salta la entrada vacia");
    CHECK(csnhypodbp_map_find_id(hueco, 2, "") == -1, "find_id: id vacio -> -1");
    CHECK(csnhypodbp_map_find_id(ids, 3, NULL) == -1, "find_id: id NULL -> -1");
    CHECK(csnhypodbp_map_find_id(ids, 0, "1/2") == -1, "find_id: n = 0 -> -1");
    CHECK(csnhypodbp_map_find_id(NULL, 3, "1/2") == -1, "find_id: ids NULL -> -1");
}

static void test_frame(void)
{
    CsnhypodbpFrame f;
    double sx, sy;

    csnhypodbp_frame_set(&f, -72.0, -66.0, -200.0, 10.0, 10.0, 10.0, 300.0, 200.0);
    CHECK(csnhypodbp_frame_valid(&f), "marco con bbox explicito es valido");

    csnhypodbp_frame_project(&f, -72.0, 10.0, &sx, &sy);
    CHECK(fabs(sx - 10.0) < 1e-9 && fabs(sy - 10.0) < 1e-9,
          "esquina NO: lon minimo arriba-izquierda");
    csnhypodbp_frame_project(&f, -66.0, -200.0, &sx, &sy);
    CHECK(fabs(sx - 310.0) < 1e-9 && fabs(sy - 210.0) < 1e-9,
          "esquina SE: lon maximo abajo-derecha");
    csnhypodbp_frame_project(&f, -69.0, -95.0, &sx, &sy);
    CHECK(fabs(sx - 160.0) < 1e-9 && fabs(sy - 110.0) < 1e-9,
          "centro exacto del marco");

    /* x crece a la derecha; mas profundo (y = -depth menor) queda mas abajo. */
    {
        double s1x, s1y, s2x, s2y, s3x, s3y;
        csnhypodbp_frame_project(&f, -66.0, 0.0, &s1x, &s1y);
        csnhypodbp_frame_project(&f, -69.0, 0.0, &s2x, &s2y);
        csnhypodbp_frame_project(&f, -69.0, -200.0, &s3x, &s3y);
        CHECK(s1x > s2x, "x crece hacia la derecha (este)");
        CHECK(s2y < s3y, "mas profundidad -> mas abajo");
        CHECK(s2y > 10.0 && s3y <= 210.0 + 1e-9, "superficie y fondo dentro del marco");
    }

    /* Marco vacio: proyecta al centro del area y no es valido. */
    csnhypodbp_frame_reset(&f, 0.0, 0.0, 100.0, 50.0);
    CHECK(!csnhypodbp_frame_valid(&f), "marco vacio no es valido");
    csnhypodbp_frame_project(&f, 1.0, 2.0, &sx, &sy);
    CHECK(fabs(sx - 50.0) < 1e-9 && fabs(sy - 25.0) < 1e-9,
          "marco vacio: proyecta al centro");

    /* grow: inicializa y luego extiende. */
    csnhypodbp_frame_reset(&f, 0.0, 0.0, 100.0, 100.0);
    csnhypodbp_frame_grow(&f, -70.0, -10.0);
    CHECK(csnhypodbp_frame_valid(&f), "grow inicializa el marco");
    csnhypodbp_frame_grow(&f, -68.0, -50.0);
    CHECK(fabs(f.x0 + 70.0) < 1e-12 && fabs(f.x1 + 68.0) < 1e-12 &&
          fabs(f.y0 + 50.0) < 1e-12 && fabs(f.y1 + 10.0) < 1e-12,
          "grow extiende a los 4 lados");
    csnhypodbp_frame_grow(&f, -75.0, 5.0);
    CHECK(fabs(f.x0 + 75.0) < 1e-12 && fabs(f.y1 - 5.0) < 1e-12,
          "grow tambien hacia el oeste y arriba");
    csnhypodbp_frame_grow(&f, NAN, 0.0);
    CHECK(fabs(f.x0 + 75.0) < 1e-12, "grow ignora NaN");

    /* pad: margen relativo. */
    csnhypodbp_frame_set(&f, -70.0, -68.0, -50.0, -10.0, 0.0, 0.0, 100.0, 100.0);
    csnhypodbp_frame_pad(&f, 0.1, 0.1);
    CHECK(fabs(f.x0 + 70.2) < 1e-9 && fabs(f.x1 + 67.8) < 1e-9 &&
          fabs(f.y0 + 54.0) < 1e-9 && fabs(f.y1 + 6.0) < 1e-9,
          "pad agrega 10% del span a cada lado");
    csnhypodbp_frame_reset(&f, 0.0, 0.0, 100.0, 100.0);
    csnhypodbp_frame_pad(&f, 0.1, 0.1);
    CHECK(!csnhypodbp_frame_valid(&f), "pad sobre marco vacio no lo inventa");

    /* Sin punteros: no revienta. */
    csnhypodbp_frame_project(NULL, 0.0, 0.0, NULL, NULL);
    csnhypodbp_frame_grow(NULL, 0.0, 0.0);
    csnhypodbp_frame_pad(NULL, 0.1, 0.1);
    CHECK(csnhypodbp_frame_valid(NULL) == 0, "valid(NULL) = 0");
}

static void test_star(void)
{
    double xs[10], ys[10];
    double d;
    int n, i, bad = 0;

    n = csnhypodbp_marker_star_path(100.0, 50.0, 10.0, 5, 0.45, xs, ys, 10);
    CHECK(n == 10, "estrella: 5 puntas -> 10 vertices");
    CHECK(fabs(xs[0] - 100.0) < 1e-9 && fabs(ys[0] - 40.0) < 1e-9,
          "estrella: el primer vertice apunta arriba");
    for (i = 0; i < n; i++) {
        d = hypot(xs[i] - 100.0, ys[i] - 50.0);
        if (i % 2 == 0 ? fabs(d - 10.0) > 1e-9 : fabs(d - 4.5) > 1e-9) {
            printf("FAIL: vertice %d con radio %.6f\n", i, d);
            bad++;
        }
    }
    CHECK(bad == 0, "estrella: alterna radio exterior / interior");

    /* Mas de 2*points no cabe: no escribe nada. */
    CHECK(csnhypodbp_marker_star_path(0, 0, 5, 5, 0.45, xs, ys, 9) == 0,
          "estrella: sin sitio para 10 vertices -> 0");
    CHECK(csnhypodbp_marker_star_path(0, 0, 0, 5, 0.45, xs, ys, 10) == 0,
          "estrella: r <= 0 -> 0");
    CHECK(csnhypodbp_marker_star_path(0, 0, 5, 2, 0.45, xs, ys, 10) == 0,
          "estrella: menos de 3 puntas -> 0");
    CHECK(csnhypodbp_marker_star_path(0, 0, 5, 5, 0.45, NULL, ys, 10) == 0,
          "estrella: punteros NULL -> 0");

    /* inner_ratio invalido -> 0.45. */
    n = csnhypodbp_marker_star_path(0.0, 0.0, 10.0, 5, 0.0, xs, ys, 10);
    CHECK(n == 10 && fabs(hypot(xs[1], ys[1]) - 4.5) < 1e-9,
          "estrella: inner_ratio invalido usa 0.45");
}

static void test_canvas(void)
{
    double iw = 0.0, ih = 0.0;

    CHECK(csnhypodbp_map_canvas(0, 0, 0, &iw, &ih) == 1 &&
          iw == CSNHYPODBP_MAP_VIRTUAL_W && ih == CSNHYPODBP_MAP_VIRTUAL_H,
          "canvas: sin imagen -> lienzo virtual");
    CHECK(fabs(iw / ih - 2.0) < 1e-12, "canvas: el lienzo virtual es 2:1");

    CHECK(csnhypodbp_map_canvas(1, 10800, 5400, &iw, &ih) == 0 &&
          iw == 10800.0 && ih == 5400.0,
          "canvas: con imagen usa los pixeles del pixbuf");
    CHECK(csnhypodbp_map_canvas(0, 800, 400, &iw, &ih) == 1 &&
          iw == CSNHYPODBP_MAP_VIRTUAL_W,
          "canvas: sin imagen ignora el tamano declarado");
    CHECK(csnhypodbp_map_canvas(1, 0, 5400, &iw, &ih) == 1 &&
          iw == CSNHYPODBP_MAP_VIRTUAL_W,
          "canvas: imagen sin ancho cae al lienzo virtual");
    CHECK(csnhypodbp_map_canvas(1, 100, -1, &iw, &ih) == 1,
          "canvas: alto invalido cae al lienzo virtual");
    CHECK(csnhypodbp_map_canvas(1, 100, 50, NULL, &ih) == -1 &&
          csnhypodbp_map_canvas(1, 100, 50, &iw, NULL) == -1,
          "canvas: punteros NULL -> -1");
}

static void test_screen(void)
{
    const int cw = 900, ch = 600;
    double iw, ih, z, cx, cy, sx = 0.0, sy = 0.0;

    csnhypodbp_map_canvas(0, 0, 0, &iw, &ih);
    csnhypodbp_mapfit_at(-33.0, -71.5, -76.0, -66.0, -40.0, -17.0, iw, ih,
                         cw, ch, 12.0, 1.0, &z, &cx, &cy);

    CHECK(csnhypodbp_map_screen(-33.0, -71.5, iw, ih, cw, ch, z, cx, cy,
                                0.0, 0.0, &sx, &sy) == 0,
          "screen: devuelve 0");
    CHECK(fabs(sx - cw / 2.0) < 1e-6 && fabs(sy - ch / 2.0) < 1e-6,
          "screen: la referencia del encuadre cae en el centro");

    CHECK(csnhypodbp_map_screen(-33.0, -71.5, iw, ih, cw, ch, z, cx, cy,
                                40.0, -10.0, &sx, &sy) == 0 &&
          fabs(sx - (cw / 2.0 + 40.0)) < 1e-6 &&
          fabs(sy - (ch / 2.0 - 10.0)) < 1e-6,
          "screen: el paneo desplaza en pixeles");

    CHECK(csnhypodbp_map_screen(NAN, -71.5, iw, ih, cw, ch, z, cx, cy,
                                0.0, 0.0, &sx, &sy) == -1,
          "screen: lat NaN -> -1");
    CHECK(csnhypodbp_map_screen(-33.0, -71.5, iw, ih, cw, ch, 0.0, cx, cy,
                                0.0, 0.0, &sx, &sy) == -1,
          "screen: zoom 0 -> -1");
    CHECK(csnhypodbp_map_screen(-33.0, -71.5, 0.0, ih, cw, ch, z, cx, cy,
                                0.0, 0.0, &sx, &sy) == -1,
          "screen: lienzo vacio -> -1");
    CHECK(csnhypodbp_map_screen(-33.0, -71.5, iw, ih, cw, ch, z, cx, cy,
                                0.0, 0.0, NULL, &sy) == -1,
          "screen: punteros NULL -> -1");
}

static void test_view_bounds(void)
{
    const int cw = 900, ch = 600;
    double iw, ih, z, cx, cy;
    double lon0, lon1, lat0, lat1, span_lat, span_lon;

    csnhypodbp_map_canvas(0, 0, 0, &iw, &ih);
    csnhypodbp_mapfit_at(-33.0, -71.5, -76.0, -66.0, -40.0, -17.0, iw, ih,
                         cw, ch, 12.0, 1.0, &z, &cx, &cy);

    CHECK(csnhypodbp_map_view_bounds(iw, ih, cw, ch, z, cx, cy, 0.0, 0.0,
                                     &lon0, &lon1, &lat0, &lat1) == 0,
          "view_bounds: devuelve 0");
    CHECK(lon0 < -71.5 && lon1 > -71.5 && lat0 < -33.0 && lat1 > -33.0,
          "view_bounds: el rango visible contiene la referencia");

    span_lat = lat1 - lat0;
    span_lon = lon1 - lon0;
    CHECK(fabs(span_lat - (double)ch * 180.0 / (z * ih)) < 1e-6,
          "view_bounds: el span vertical sale del zoom");
    CHECK(fabs(span_lon - (double)cw * 360.0 / (z * iw)) < 1e-6,
          "view_bounds: el span horizontal sale del zoom");

    /* Zoom x2: la mitad de span, sin mover el centro. */
    {
        double lo0, lo1, la0, la1;
        csnhypodbp_map_view_bounds(iw, ih, cw, ch, 2.0 * z, cx, cy, 0.0, 0.0,
                                   &lo0, &lo1, &la0, &la1);
        CHECK(fabs((la1 - la0) - span_lat / 2.0) < 1e-6 &&
              fabs((lo1 - lo0) - span_lon / 2.0) < 1e-6,
              "view_bounds: zoom x2 -> mitad de span");
    }

    /* Paneo a la derecha: el contenido se corre y el rango se mueve al oeste. */
    {
        double lo0, lo1, la0, la1;
        csnhypodbp_map_view_bounds(iw, ih, cw, ch, z, cx, cy, 100.0, 0.0,
                                   &lo0, &lo1, &la0, &la1);
        CHECK(lo0 < lon0 && lo1 < lon1,
              "view_bounds: paneo a la derecha mueve el rango al oeste");
        CHECK(fabs((lo1 - lo0) - span_lon) < 1e-6,
              "view_bounds: el paneo no cambia el span");
    }

    /* Mundo entero: el rango se recorta a ±180 / ±90. */
    {
        double lo0, lo1, la0, la1;
        double wz, wcx, wcy;
        csnhypodbp_mapfit(-180.0, 180.0, -90.0, 90.0, iw, ih, cw, ch, 12.0,
                          &wz, &wcx, &wcy);
        csnhypodbp_map_view_bounds(iw, ih, cw, ch, wz, wcx, wcy, 0.0, 0.0,
                                   &lo0, &lo1, &la0, &la1);
        CHECK(lo0 >= -180.0 && lo1 <= 180.0 && la0 >= -90.0 && la1 <= 90.0,
              "view_bounds: recortado al mundo");
        CHECK(lo1 - lo0 > 359.0 && la1 - la0 > 179.0,
              "view_bounds: con el mundo encuadrado se ve el mundo");
    }

    CHECK(csnhypodbp_map_view_bounds(iw, ih, cw, ch, 0.0, cx, cy, 0.0, 0.0,
                                     &lon0, &lon1, &lat0, &lat1) == -1,
          "view_bounds: zoom 0 -> -1");
    CHECK(csnhypodbp_map_view_bounds(iw, ih, 0, ch, z, cx, cy, 0.0, 0.0,
                                     &lon0, &lon1, &lat0, &lat1) == -1,
          "view_bounds: canvas vacio -> -1");
    CHECK(csnhypodbp_map_view_bounds(iw, ih, cw, ch, z, cx, cy, 0.0, 0.0,
                                     NULL, &lon1, &lat0, &lat1) == -1,
          "view_bounds: punteros NULL -> -1");
}

static void test_fit_bounds(void)
{
    CsnhypodbpMapPt pts[2];
    double lon0 = 0, lon1 = 0, lat0 = 0, lat1 = 0;

    memset(pts, 0, sizeof(pts));
    CHECK(csnhypodbp_map_fit_bounds(pts, 0, 5.0, &lon0, &lon1, &lat0, &lat1) == 1,
          "fit_bounds: sin puntos usa el mundo");
    CHECK(lon0 == -180.0 && lon1 == 180.0 && lat0 == -90.0 && lat1 == 90.0,
          "fit_bounds: el respaldo es el mundo completo");
    CHECK(csnhypodbp_map_fit_bounds(NULL, 3, 5.0, &lon0, &lon1, &lat0, &lat1) == 1,
          "fit_bounds: puntero NULL tambien cae al mundo");

    pts[0].lat = -20.0; pts[0].lon = -70.0;
    pts[1].lat = -22.0; pts[1].lon = -71.5;
    CHECK(csnhypodbp_map_fit_bounds(pts, 2, 5.0, &lon0, &lon1, &lat0, &lat1) == 0,
          "fit_bounds: con puntos usa su bbox");
    CHECK(fabs(lon0 + 73.25) < 1e-9 && fabs(lon1 + 68.25) < 1e-9 &&
          fabs(lat0 + 23.5) < 1e-9 && fabs(lat1 + 18.5) < 1e-9,
          "fit_bounds: aplica MapMinSpanDeg");

    /* Un bbox ya grande no se toca. */
    pts[0].lat = -40.0; pts[0].lon = -76.0;
    pts[1].lat = -17.0; pts[1].lon = -66.0;
    CHECK(csnhypodbp_map_fit_bounds(pts, 2, 5.0, &lon0, &lon1, &lat0, &lat1) == 0 &&
          lon0 == -76.0 && lon1 == -66.0 && lat0 == -40.0 && lat1 == -17.0,
          "fit_bounds: bbox mayor que el suelo queda igual");

    CHECK(csnhypodbp_map_fit_bounds(pts, 2, 5.0, NULL, &lon1, &lat0, &lat1) == -1,
          "fit_bounds: punteros NULL -> -1");
}

static void test_grid_step(void)
{
    CHECK(fabs(csnhypodbp_map_grid_step(5.0) - 1.0) < 1e-12, "grid_step: 5 -> 1");
    CHECK(fabs(csnhypodbp_map_grid_step(23.0) - 5.0) < 1e-12, "grid_step: 23 -> 5");
    CHECK(fabs(csnhypodbp_map_grid_step(40.0) - 5.0) < 1e-12, "grid_step: 40 -> 5");
    CHECK(fabs(csnhypodbp_map_grid_step(360.0) - 45.0) < 1e-12, "grid_step: 360 -> 45");
    CHECK(fabs(csnhypodbp_map_grid_step(2.0) - 0.25) < 1e-12, "grid_step: 2 -> 0.25");
    CHECK(fabs(csnhypodbp_map_grid_step(0.3) - 0.05) < 1e-12, "grid_step: 0.3 -> 0.05");
    CHECK(fabs(csnhypodbp_map_grid_step(NAN) - 10.0) < 1e-12, "grid_step: NaN -> 10");
    CHECK(fabs(csnhypodbp_map_grid_step(0.0) - 10.0) < 1e-12, "grid_step: 0 -> 10");
    CHECK(fabs(csnhypodbp_map_grid_step(-5.0) - 10.0) < 1e-12, "grid_step: negativo -> 10");

    /* El paso nunca deja menos de ~4 divisiones en el span. */
    {
        const double spans[6] = { 0.4, 1.0, 4.0, 9.0, 90.0, 360.0 };
        int i, bad = 0;
        for (i = 0; i < 6; i++) {
            double st = csnhypodbp_map_grid_step(spans[i]);
            if (!(st > 0.0) || spans[i] / st < 3.0) bad++;
        }
        CHECK(bad == 0, "grid_step: al menos ~4 divisiones en cada span");
    }
}

static void test_axis_fmt(void)
{
    char buf[32];

    csnhypodbp_map_axis_fmt(1.0, -70.0, buf, sizeof(buf));
    CHECK(strcmp(buf, "-70") == 0, "axis_fmt: paso >= 1 sin decimales");
    csnhypodbp_map_axis_fmt(0.5, -70.5, buf, sizeof(buf));
    CHECK(strcmp(buf, "-70.5") == 0, "axis_fmt: paso 0.5 con un decimal");
    csnhypodbp_map_axis_fmt(0.05, -70.05, buf, sizeof(buf));
    CHECK(strcmp(buf, "-70.05") == 0, "axis_fmt: paso chico con dos decimales");
    csnhypodbp_map_axis_fmt(1.0, 0.0, buf, sizeof(buf));
    CHECK(strcmp(buf, "0") == 0, "axis_fmt: cero sin signo");
    csnhypodbp_map_axis_fmt(1.0, 15.0, buf, sizeof(buf));
    CHECK(strcmp(buf, "15") == 0, "axis_fmt: positivo");
    csnhypodbp_map_axis_fmt(0.0, 12.0, buf, sizeof(buf));
    CHECK(strcmp(buf, "12") == 0, "axis_fmt: paso invalido usa el decimal grueso");
    csnhypodbp_map_axis_fmt(1.0, NAN, buf, sizeof(buf));
    CHECK(strcmp(buf, "?") == 0, "axis_fmt: valor no finito -> ?");
    csnhypodbp_map_axis_fmt(1.0, 5.0, NULL, 0);
    CHECK(1, "axis_fmt: buffer NULL no revienta");
}

static void test_label_pick(void)
{
    const double pos[5] = { 0.0, 20.0, 100.0, 120.0, 200.0 };
    int keep[5];
    int i;

    CHECK(csnhypodbp_map_label_pick(pos, 5, 34.0, keep) == 0,
          "label_pick: devuelve 0");
    CHECK(keep[0] == 1 && keep[1] == 0 && keep[2] == 1 && keep[3] == 0 && keep[4] == 1,
          "label_pick: respeta la separacion minima");

    for (i = 0; i < 5; i++) keep[i] = -1;
    CHECK(csnhypodbp_map_label_pick(pos, 5, 0.0, keep) == 0 &&
          keep[0] == 1 && keep[1] == 1 && keep[2] == 1 && keep[3] == 1 && keep[4] == 1,
          "label_pick: sin separacion se etiqueta todo");

    CHECK(csnhypodbp_map_label_pick(pos, 0, 34.0, keep) == 0,
          "label_pick: n = 0 no es error");

    {
        const double malos[3] = { NAN, 10.0, 12.0 };
        CHECK(csnhypodbp_map_label_pick(malos, 3, 34.0, keep) == 0 &&
              keep[0] == 0 && keep[1] == 1 && keep[2] == 0,
              "label_pick: una posicion no finita se omite sin arrastrar el hueco");
    }

    CHECK(csnhypodbp_map_label_pick(NULL, 5, 34.0, keep) == -1 &&
          csnhypodbp_map_label_pick(pos, 5, 34.0, NULL) == -1,
          "label_pick: punteros NULL -> -1");
}

int main(void)
{
    test_bbox();
    test_mapfit();
    test_split_parse();
    test_mapfit_at();
    test_span_min();
    test_canvas();
    test_screen();
    test_view_bounds();
    test_fit_bounds();
    test_grid_step();
    test_axis_fmt();
    test_label_pick();
    test_profile_frame();
    test_frame_view();
    test_hit();
    test_find_id();
    test_frame();
    test_star();

    if (fails > 0) { printf("\n%d FALLOS\n", fails); return 1; }
    printf("\nOK test_mapview\n");
    return 0;
}
