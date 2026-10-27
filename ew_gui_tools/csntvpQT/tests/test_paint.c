/* Render: BGRA no vacio, panel dibujado, eje con texto y un color por
 * componente (Z/N/E). */
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
    tvq_t_colour(cfg->colour[TVQ_COLOUR_Z], 1.0, 0.0, 0.0);   /* rojo  */
    tvq_t_colour(cfg->colour[TVQ_COLOUR_N], 0.0, 1.0, 0.0);   /* verde */
    tvq_t_colour(cfg->colour[TVQ_COLOUR_E], 0.0, 0.0, 1.0);   /* azul  */

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

    /* --- un color por componente ----------------------------------------- */
    {
        const int red = count_near(buf, W, H, 255, 0, 0);
        const int green = count_near(buf, W, H, 0, 255, 0);
        const int blue = count_near(buf, W, H, 0, 0, 255);

        T_CHECK(red > 0, "componente Z en su color");
        T_CHECK(green > 0, "componente N en su color");
        T_CHECK(blue > 0, "componente E en su color");
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
