/* Guia vertical del puntero para el picado (paridad csnhypodbpQT).
 *
 * El test aisla la guia comparando DOS pintados identicos (mismas trazas,
 * mismas picadas, mismo scroll) que solo difieren en `tvq_core_set_guide`:
 * asi el conteo no depende de tolerancias de color ni del antialias.
 */
#include <string.h>

#include "tvq_test.h"

#define W 400
#define H 120

static void paint(TvqCore *c, unsigned char *buf, int w, int h, int scroll)
{
    memset(buf, 0, (size_t)w * (size_t)h * 4);
    (void)tvq_paint_traces(c, buf, w, h, w * 4, 1.0, scroll);
}

static void paint_dpr(TvqCore *c, unsigned char *buf, int w, int h, double dpr)
{
    memset(buf, 0, (size_t)w * (size_t)h * 4);
    (void)tvq_paint_traces(c, buf, w, h, w * 4, dpr, 0);
}

/* Pixeles distintos entre dos buffers, recortado a la banda [x0,x1) x [y0,y1). */
static int diff_pixels(const unsigned char *a, const unsigned char *b, int w,
                       int x0, int x1, int y0, int y1)
{
    int n = 0;

    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (x1 > w)
        x1 = w;
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            const size_t off = ((size_t)y * (size_t)w + (size_t)x) * 4;
            const unsigned char *pa = a + off;
            const unsigned char *pb = b + off;

            if (pa[0] != pb[0] || pa[1] != pb[1] || pa[2] != pb[2])
                n++;
        }
    }
    return n;
}

/* cairo ARGB32 little-endian: B,G,R,A. */
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
        "guide.sta", "AAA HHZ XX -- 1.0\nBBB HHN XX -- 1.0\nCCC HHE XX -- 1.0\n");
    char err[512];
    TvqCore *c;
    unsigned char *base;
    unsigned char *shot;

    cfg->stations_per_screen = 3;
    cfg->time_window_min = 1;

    c = tvq_core_new(cfg, err, sizeof err);
    T_CHECK(c && tvq_core_load_stations(c, err, sizeof err) == 0,
            "core con 3 estaciones");
    T_CHECK(tvq_core_seed_fixture(c, tvq_now_epoch() - 2.0, 20) == 3,
            "siembra las 3 estaciones");

    base = (unsigned char *)malloc((size_t)W * H * 4);
    shot = (unsigned char *)malloc((size_t)W * H * 4);
    T_CHECK(base && shot, "buffers BGRA");
    if (!base || !shot)
        return 1;

    /* --- G4: contrato del estado ---------------------------------------- */
    {
        double x = -1.0;

        T_CHECK(tvq_core_guide(c, &x) == 0, "G4: sin guia por defecto");
        tvq_core_set_guide(NULL, 100.0, 1);      /* no debe reventar */
        T_CHECK(tvq_core_guide(NULL, &x) == 0, "G4: getter con core nulo");
        tvq_core_set_guide(c, 250.0, 1);
        T_CHECK(tvq_core_guide(c, &x) == 1 && x > 249.0 && x < 251.0,
                "G4: set/get de la guia");
        tvq_core_set_guide(c, 250.0, 0);
        T_CHECK(tvq_core_guide(c, &x) == 0, "G4: valid=0 la oculta");
    }

    /* --- G1: sin fase armada no se pinta -------------------------------- */
    paint(c, base, W, H, 0);
    tvq_core_set_guide(c, 250.0, 1);
    paint(c, shot, W, H, 0);
    T_CHECK(diff_pixels(base, shot, W, 0, W, 0, H) == 0,
            "G1: sin fase armada la guia no se dibuja");

    /* --- G2: con P armada, columna discontinua -------------------------- */
    tvq_core_set_pick_phase(c, 'P');
    tvq_core_set_guide(c, 250.0, 0);   /* baseline sin guia */
    paint(c, base, W, H, 0);
    tvq_core_set_guide(c, 250.0, 1);
    paint(c, shot, W, H, 0);
    {
        const int band = diff_pixels(base, shot, W, 248, 253, 0, H);
        const int panel = diff_pixels(base, shot, W, 0, TVQ_PANEL_WIDTH, 0, H);
        const int far = diff_pixels(base, shot, W, 260, W, 0, H);

        T_CHECK(band > H / 4, "G2: la guia se dibuja en x=250");
        T_CHECK(band < H, "G2: la guia es discontinua (dash)");
        T_CHECK(panel == 0, "G2: no invade el panel izquierdo");
        T_CHECK(far == 0, "G2: no se dibuja fuera de su x");
    }

    /* --- G3: fuera del area de trazas no se pinta ----------------------- */
    tvq_core_set_guide(c, 250.0, 0);   /* baseline sin guia */
    paint(c, base, W, H, 0);
    tvq_core_set_guide(c, (double)TVQ_PANEL_WIDTH, 1);
    paint(c, shot, W, H, 0);
    T_CHECK(diff_pixels(base, shot, W, 0, W, 0, H) == 0,
            "G3: x en el borde del panel no pinta");
    tvq_core_set_guide(c, (double)TVQ_PANEL_WIDTH + 0.5, 1);
    paint(c, shot, W, H, 0);
    T_CHECK(diff_pixels(base, shot, W, 0, W, 0, H) == 0,
            "G3: x.5 en el borde no pinta (hit lo rechaza)");
    tvq_core_set_guide(c, (double)W + 10.0, 1);
    paint(c, shot, W, H, 0);
    T_CHECK(diff_pixels(base, shot, W, 0, W, 0, H) == 0,
            "G3: x fuera del lienzo no pinta");

    /* --- G5: la guia no depende del scroll vertical --------------------- */
    tvq_core_set_guide(c, 250.0, 0);
    paint(c, base, W, H, H / 3);
    tvq_core_set_guide(c, 250.0, 1);
    paint(c, shot, W, H, H / 3);
    T_CHECK(diff_pixels(base, shot, W, 248, 253, 0, H) > 20,
            "G5: con scroll sigue en la misma x");
    T_CHECK(diff_pixels(base, shot, W, 0, TVQ_PANEL_WIDTH, 0, H) == 0,
            "G5: con scroll sigue fuera del panel");

    /* --- G6: DPR 2 escala la guia a pixeles fisicos --------------------- */
    {
        unsigned char *b2 = (unsigned char *)malloc((size_t)W * H * 4);
        unsigned char *s2 = (unsigned char *)malloc((size_t)W * H * 4);

        if (b2 && s2) {
            /* Con DPR 2 el modulo dibuja en W/2 x H/2 logicos: guia en 150
             * logico => 300 fisico. */
            tvq_core_set_guide(c, 150.0, 0);
            paint_dpr(c, b2, W, H, 2.0);
            tvq_core_set_guide(c, 150.0, 1);
            paint_dpr(c, s2, W, H, 2.0);
            T_CHECK(diff_pixels(b2, s2, W, 298, 303, 0, H) > 10,
                    "G6: con DPR 2 la guia cae en 2*x fisico");
            T_CHECK(diff_pixels(b2, s2, W, 0, 2 * TVQ_PANEL_WIDTH, 0, H) == 0,
                    "G6: con DPR 2 no invade el panel");
            free(b2);
            free(s2);
        }
    }

    /* --- G7: la picada sigue visible bajo la guia ----------------------- */
    {
        double tl = 0.0, tr = 0.0;
        double t;
        int placed;

        tvq_core_set_hold(c, 1);
        tvq_core_view_range(c, &tl, &tr);
        t = tl + ((250.0 - TVQ_PANEL_WIDTH) / (double)(W - TVQ_PANEL_WIDTH)) *
                     (tr - tl);
        placed = tvq_core_place_pick(c, 0, t, 'P');
        T_CHECK(placed == 1, "G7: picada de prueba colocada");

        tvq_core_set_guide(c, 250.0, 0);
        paint(c, base, W, H, 0);
        tvq_core_set_guide(c, 250.0, 1);
        paint(c, shot, W, H, 0);
        /* `P` es rojo por defecto (tvq_config.c:53): la marca de 2 px sigue
         * viendose aunque la guia de 1 px pase por encima. */
        T_CHECK(count_near(base, W, H, 255, 0, 0) > 0, "G7: picada roja visible");
        T_CHECK(count_near(shot, W, H, 255, 0, 0) > 0,
                "G7: la guia no borra la picada");
    }

    free(base);
    free(shot);
    tvq_core_free(c);
    tvq_config_free(cfg);

    printf(tvq_t_fail ? "FALLAS: test_guide\n" : "OK: test_guide\n");
    return tvq_t_fail ? 1 : 0;
}
