/* Normalizacion vertical (paridad con csntvp_gui.c:378-393).
 *
 * Cada pista se escala con SU `env_max_abs` de la ventana visible:
 *
 *   alto_px = env_value * (alto_pista * 0.425 / env_max_abs) * zoom
 *
 * El `scale` del StaFile (columna 5) es de paridad de formato y NO entra en el
 * dibujo: el GTK lo guarda en `dScreenScale` (csntvp_data.c:209) y no lo lee
 * nunca. Multiplicarlo despues de normalizar encoge la traza por ese factor
 * (0.005 en las 141 filas de stations_to_view.sta => 200x => linea plana).
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "tvq_test.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define W 400
#define H 480

static void fill_wave(int32_t *s, int n, double amp, double dc, double rate)
{
    for (int k = 0; k < n; k++)
        s[k] = (int32_t)(dc + amp * sin(2.0 * M_PI * 2.0 * (double)k / rate));
}

/* Filas de la pista `row` con al menos un pixel de la onda (negra).
 * cairo ARGB32 little-endian: B,G,R,A. Se excluye el panel izquierdo para no
 * contar etiquetas ni el borde. */
static int rows_painted(const unsigned char *buf, int w, int h, int th, int row)
{
    int n = 0;
    int y0 = row * th;
    int y1 = y0 + th;

    if (y1 > h)
        y1 = h;
    for (int y = y0; y < y1; y++) {
        for (int x = TVQ_PANEL_WIDTH + 1; x < w; x++) {
            const unsigned char *p =
                buf + ((size_t)y * (size_t)w + (size_t)x) * 4;

            if (p[0] < 60 && p[1] < 60 && p[2] < 60) {
                n++;
                break;
            }
        }
    }
    return n;
}

/* Un TRACE2 completo (i4) con un seno de 2 Hz sobre DC 5000. */
static int ingest_wave(TvqCore *c, const char *sta, double amp, double t0,
                       double rate, int nsamp)
{
    size_t cap = sizeof(TRACE2_HEADER) + (size_t)nsamp * sizeof(int32_t) + 64;
    char *buf = malloc(cap);
    int32_t *samples = malloc((size_t)nsamp * sizeof(int32_t));
    int rc = -1;

    if (!buf || !samples)
        goto out;

    fill_wave(samples, nsamp, amp, 5000.0, rate);
    {
        const int len = tvq_t_trace(buf, cap, sta, "HHZ", "XX", "--", "i4",
                                    rate, t0, nsamp, samples);

        if (len > 0)
            rc = tvq_core_ingest_trace(c, buf, len);
    }
out:
    free(samples);
    free(buf);
    return rc;
}

static int absdiff(int a, int b)
{
    return a > b ? a - b : b - a;
}

int main(void)
{
    /* AAA/BBB: MISMA onda (1200), escalas 1.0 y 0.005  => N1.
     * CCC (120) y DDD (12000), escala 1.0            => N2. */
    TvqConfig *cfg = tvq_t_cfg("norm.sta",
                               "AAA HHZ XX -- 1.0\n"
                               "BBB HHZ XX -- 0.005\n"
                               "CCC HHZ XX -- 1.0\n"
                               "DDD HHZ XX -- 1.0\n");
    char err[512];
    TvqCore *c;
    unsigned char *buf;
    int rows4[4];
    const double rate = 100.0;
    const int nsamp = 7000;              /* 70 s: cubre la ventana de 1 min */
    const double now = tvq_now_epoch();
    const double t0 = now - 2.0 - (double)nsamp / rate;

    cfg->stations_per_screen = 4;
    cfg->time_window_min = 1;

    c = tvq_core_new(cfg, err, sizeof err);
    T_CHECK(c && tvq_core_load_stations(c, err, sizeof err) == 0,
            "core con 4 estaciones");

    T_CHECK(ingest_wave(c, "AAA", 1200.0, t0, rate, nsamp) == 1, "AAA 1200");
    T_CHECK(ingest_wave(c, "BBB", 1200.0, t0, rate, nsamp) == 1, "BBB 1200");
    T_CHECK(ingest_wave(c, "CCC", 120.0, t0, rate, nsamp) == 1, "CCC 120");
    T_CHECK(ingest_wave(c, "DDD", 12000.0, t0, rate, nsamp) == 1, "DDD 12000");

    (void)tvq_core_tick(c, now);          /* fija smooth_t = ahora - 2 s */

    buf = malloc((size_t)W * (size_t)H * 4);
    T_CHECK(buf != NULL, "buffer BGRA");
    memset(buf, 0, (size_t)W * (size_t)H * 4);
    T_CHECK(tvq_paint_traces(c, buf, W, H, W * 4, 1.0, 0) == 1, "pinta 4 pistas");

    {
        const int th = tvq_core_track_height(c, H);

        T_CHECK(th == 120, "alto de pista 120");
        for (int i = 0; i < 4; i++)
            rows4[i] = rows_painted(buf, W, H, th, i);

        /* N1: el scale del StaFile no cambia la altura dibujada. */
        T_CHECK(absdiff(rows4[0], rows4[1]) <= 2,
                "N1: scale 0.005 pinta igual que scale 1.0");

        /* N2: la normalizacion es POR PISTA (120 y 12000 counts => misma altura). */
        T_CHECK(absdiff(rows4[0], rows4[2]) <= 2,
                "N2: amplitud 120 normaliza igual que 1200");
        T_CHECK(absdiff(rows4[0], rows4[3]) <= 2,
                "N2: amplitud 12000 normaliza igual que 1200");

        /* Cobertura: la onda ocupa ~85 % del alto de pista (0.425 arriba y abajo). */
        T_CHECK(rows4[0] >= th * 70 / 100 && rows4[0] <= th * 95 / 100,
                "N2: la traza llena la pista (70-95 %)");
    }

    /* N3: el zoom vertical multiplica esa base (y el clip la acota). */
    free(buf);
    {
        const int H1 = 400;
        const int th1 = H1;              /* 1 estacion por pantalla */
        int rows1, rows_half, rows_double;

        tvq_core_set_stations_per_screen(c, 1);
        T_CHECK(tvq_core_track_height(c, H1) == th1, "1 por pantalla -> pista 400");

        buf = malloc((size_t)W * (size_t)H1 * 4);
        T_CHECK(buf != NULL, "buffer BGRA grande");

        memset(buf, 0, (size_t)W * (size_t)H1 * 4);
        (void)tvq_paint_traces(c, buf, W, H1, W * 4, 1.0, 0);
        rows1 = rows_painted(buf, W, H1, th1, 0);

        tvq_core_adjust_scale(c, 0.5);
        memset(buf, 0, (size_t)W * (size_t)H1 * 4);
        (void)tvq_paint_traces(c, buf, W, H1, W * 4, 1.0, 0);
        rows_half = rows_painted(buf, W, H1, th1, 0);

        tvq_core_adjust_scale(c, 4.0);   /* 0.5 * 4 = 2.0 */
        memset(buf, 0, (size_t)W * (size_t)H1 * 4);
        (void)tvq_paint_traces(c, buf, W, H1, W * 4, 1.0, 0);
        rows_double = rows_painted(buf, W, H1, th1, 0);

        T_CHECK(absdiff(rows_half, rows1 / 2) <= 3,
                "N3: zoom 0.5 reduce la altura a la mitad");
        T_CHECK(rows_double == th1,
                "N3: zoom 2 satura la pista (clip, no desborda)");

        free(buf);
    }

    tvq_core_free(c);
    tvq_config_free(cfg);

    printf(tvq_t_fail ? "FALLAS: test_norm\n" : "OK: test_norm\n");
    return tvq_t_fail ? 1 : 0;
}
