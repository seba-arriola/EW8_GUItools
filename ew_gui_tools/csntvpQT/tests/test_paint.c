/* Render: BGRA no vacio, panel dibujado, eje con texto y waveformsColor
 * mandando sobre las tres pistas (Z, N y E sin color propio). */
#include <string.h>

#include "tvq_test.h"

#define W 400
#define H 120

static void paint(TvqCore *c, unsigned char *buf, int w, int h)
{
    memset(buf, 0, (size_t)w * (size_t)h * 4);
    (void)tvq_paint_traces(c, buf, w, h, w * 4, 1.0, 0);
}

/* cairo ARGB32 en little-endian: B,G,R,A. */
static int count_near(const unsigned char *buf, int w, int h, int r, int g, int b)
{
    int n = 0;

    for (int i = 0; i < w * h; i++) {
        const unsigned char *p = buf + (size_t)i * 4;

        if (p[3] < 200)
            continue;
        if (abs((int)p[2] - r) <= 40 && abs((int)p[1] - g) <= 40 &&
            abs((int)p[0] - b) <= 40)
            n++;
    }
    return n;
}

/* Pixeles del color pedido dentro de un rectangulo (area de trazas). */
static int count_near_rect(const unsigned char *buf, int w, int x0, int x1,
                           int y0, int y1, int r, int g, int b)
{
    int n = 0;

    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            const unsigned char *p =
                buf + ((size_t)y * (size_t)w + (size_t)x) * 4;

            if (p[3] < 200)
                continue;
            if (abs((int)p[2] - r) <= 40 && abs((int)p[1] - g) <= 40 &&
                abs((int)p[0] - b) <= 40)
                n++;
        }
    }
    return n;
}

int main(void)
{
    TvqConfig *cfg = tvq_t_cfg(
        "three.sta", "AAA HHZ XX -- 1.0\nBBB HHN XX -- 1.0\nCCC HHE XX -- 1.0\n");
    char err[512];
    TvqCore *c;
    unsigned char *buf;
    int nonwhite;

    cfg->stations_per_screen = 3;
    cfg->time_window_min = 1;

    c = tvq_core_new(cfg, err, sizeof err);
    T_CHECK(c && tvq_core_load_stations(c, err, sizeof err) == 0,
            "core con 3 estaciones");
    T_CHECK(tvq_core_seed_fixture(c, tvq_now_epoch() - 2.0, 20) == 3,
            "siembra las 3 estaciones");

    buf = (unsigned char *)malloc((size_t)W * H * 4);
    T_CHECK(buf != NULL, "buffer BGRA");

    /* --- guardas --------------------------------------------------------- */
    T_CHECK(tvq_paint_traces(c, NULL, W, H, W * 4, 1.0, 0) == 0,
            "pintar con buffer nulo devuelve 0");
    T_CHECK(tvq_paint_traces(c, buf, 0, H, W * 4, 1.0, 0) == 0,
            "pintar con ancho 0 devuelve 0");

    /* --- panel y trazas --------------------------------------------------- */
    paint(c, buf, W, H);
    T_CHECK(buf[3] == 255, "el fondo queda opaco");
    T_CHECK(count_near(buf, W, H, 245, 245, 245) > 100,
            "panel izquierdo dibujado (gris 0.96)");
    T_CHECK(count_near(buf, W, H, 255, 255, 255) > 100,
            "fondo de trazas dibujado (blanco)");

    /* --- waveformsColor manda sobre las 3 pistas ------------------------- */
    {
        const int th = H / 3;
        int z, n, e;
        double red[3] = { 1.0, 0.0, 0.0 };
        double blue[3] = { 0.0, 0.0, 1.0 };

        tvq_core_set_colour(c, TVQ_COLOUR_WAVE, red);
        paint(c, buf, W, H);
        z = count_near_rect(buf, W, TVQ_PANEL_WIDTH, W, 0, th, 255, 0, 0);
        n = count_near_rect(buf, W, TVQ_PANEL_WIDTH, W, th, 2 * th, 255, 0, 0);
        e = count_near_rect(buf, W, TVQ_PANEL_WIDTH, W, 2 * th, H, 255, 0, 0);
        T_CHECK(z > 0 && n > 0 && e > 0,
                "las 3 pistas (Z, N y E) usan waveformsColor");
        T_CHECK(count_near(buf, W, H, 0, 255, 0) == 0 &&
                    count_near(buf, W, H, 0, 0, 255) == 0,
                "el canal no cambia el color de la pista");

        tvq_core_set_colour(c, TVQ_COLOUR_WAVE, blue);
        paint(c, buf, W, H);
        z = count_near_rect(buf, W, TVQ_PANEL_WIDTH, W, 0, th, 0, 0, 255);
        n = count_near_rect(buf, W, TVQ_PANEL_WIDTH, W, th, 2 * th, 0, 0, 255);
        e = count_near_rect(buf, W, TVQ_PANEL_WIDTH, W, 2 * th, H, 0, 0, 255);
        T_CHECK(z > 0 && n > 0 && e > 0,
                "cambiar waveformsColor recolorea las 3 pistas");
    }

    /* --- scroll: pintar otra franja no revienta -------------------------- */
    {
        unsigned char *buf2 = (unsigned char *)malloc((size_t)W * H * 4);

        if (buf2) {
            memset(buf2, 0, (size_t)W * H * 4);
            T_CHECK(tvq_paint_traces(c, buf2, W, H, W * 4, 1.0,
                                     H / 3) == 1,
                    "pintar con scroll_y");
            free(buf2);
        }
    }

    /* --- eje ------------------------------------------------------------- */
    {
        unsigned char *axis = (unsigned char *)malloc((size_t)W * 40 * 4);

        if (axis) {
            memset(axis, 0, (size_t)W * 40 * 4);
            T_CHECK(tvq_paint_axis(c, axis, W, 40, W * 4, 1.0) == 1,
                    "eje pintado");
            nonwhite = 0;
            for (int i = 0; i < W * 40; i++) {
                const unsigned char *p = axis + (size_t)i * 4;

                if (p[0] < 240 || p[1] < 240 || p[2] < 240)
                    nonwhite++;
            }
            T_CHECK(nonwhite > 50, "el eje tiene texto y linea");
            free(axis);
        }
    }

    /* --- escala (devicePixelRatio) --------------------------------------- */
    {
        unsigned char *hi = (unsigned char *)malloc((size_t)W * H * 4);

        if (hi) {
            memset(hi, 0, (size_t)W * H * 4);
            /* pw/ph fisicos con DPR 2 => el modulo dibuja en W/2 x H/2 logicos */
            T_CHECK(tvq_paint_traces(c, hi, W, H, W * 4, 2.0, 0) == 1,
                    "pintar con DPR 2");
            nonwhite = count_near(hi, W, H, 245, 245, 245);
            T_CHECK(nonwhite > 100, "con DPR 2 el panel se dibuja escalado");
            free(hi);
        }
    }

    free(buf);
    tvq_core_free(c);
    tvq_config_free(cfg);

    printf(tvq_t_fail ? "FALLAS: test_paint\n" : "OK: test_paint\n");
    return tvq_t_fail ? 1 : 0;
}
