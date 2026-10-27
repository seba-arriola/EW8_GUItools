/* Filtros: afectan SOLO a la vista (envolvente), nunca al crudo; y el tipo,
 * f1/f2 y orden se acotan como en el GTK. */
#include <string.h>

#include "tvq_test.h"

static void paint(TvqCore *c, unsigned char *buf, int w, int h)
{
    memset(buf, 0, (size_t)w * (size_t)h * 4);
    (void)tvq_paint_traces(c, buf, w, h, w * 4, 1.0, 0);
}

int main(void)
{
    TvqConfig *cfg = tvq_t_cfg("one.sta", "AAA HHZ XX -- 1.0\n");
    char err[512];
    TvqCore *c;
    const int W = 400, H = 120;
    unsigned char *a, *b;
    int v0 = 0, v1 = 0;
    int type = 0, order = 0;
    double f1 = 0.0, f2 = 0.0;

    cfg->time_window_min = 1;
    c = tvq_core_new(cfg, err, sizeof err);
    T_CHECK(c && tvq_core_load_stations(c, err, sizeof err) == 0,
            "core con 1 estacion");
    T_CHECK(tvq_core_seed_fixture(c, tvq_now_epoch() - 2.0, 30) == 1,
            "siembra 30 s de ondas");

    a = (unsigned char *)malloc((size_t)W * H * 4);
    b = (unsigned char *)malloc((size_t)W * H * 4);
    T_CHECK(a && b, "buffers de pintado");

    /* Un indice absoluto con dato, FIJO: si se recalculara con el reloj, dos
     * llamadas caerian en muestras distintas y el test no probaria nada. */
    {
        const double t_end = tvq_now_epoch() - 2.0;
        long long idx = (long long)((t_end - 10.0) * 100.0);
        int probe = 0;

        for (int d = 0; d < 200; d++) {
            if (tvq_core_sample(c, 0, idx + d, &probe) == 1) {
                idx += d;
                break;
            }
        }
        T_CHECK(tvq_core_sample(c, 0, idx, &v0) == 1, "muestra cruda disponible");
        /* se reutiliza `idx` abajo */
        {
            /* crudo antes del filtro */
            paint(c, a, W, H);
            T_CHECK(tvq_core_sample(c, 0, idx, &v1) == 1 &&
                        v0 == v1,
                    "pintar no toca el crudo");

            /* --- High-pass 5 Hz: la envolvente cambia, el crudo no ------ */
            tvq_core_set_filter(c, 1, 5.0, 0.0, 4);
            paint(c, b, W, H);
            T_CHECK(memcmp(a, b, (size_t)W * H * 4) != 0,
                    "el filtro cambia lo dibujado");
            T_CHECK(tvq_core_sample(c, 0, idx, &v1) == 1 && v0 == v1,
                    "el filtro NO toca el crudo");
        }
    }

    /* --- get/set y clamps ------------------------------------------------ */
    tvq_core_get_filter(c, &type, &f1, &f2, &order);
    T_CHECK(type == 1 && order == 4, "get_filter devuelve lo aplicado");
    T_CHECK(f1 > 4.9 && f1 < 5.1, "get_filter devuelve f1");

    tvq_core_set_filter(c, 99, -1.0, -2.0, 3);
    tvq_core_get_filter(c, &type, &f1, &f2, &order);
    T_CHECK(type == 0, "tipo fuera de rango -> Raw");
    T_CHECK(order == 4, "orden invalido -> 4");
    T_CHECK(f1 == 0.0 && f2 == 0.0, "frecuencias negativas -> 0");

    /* --- Band-pass y Low-pass no revientan ------------------------------ */
    tvq_core_set_filter(c, 3, 0.5, 4.0, 2);
    paint(c, b, W, H);
    tvq_core_set_filter(c, 2, 1.0, 0.0, 2);
    paint(c, b, W, H);
    T_CHECK(1, "band-pass y low-pass pintan sin fallar");

    free(a);
    free(b);
    tvq_core_free(c);
    tvq_config_free(cfg);

    printf(tvq_t_fail ? "FALLAS: test_env\n" : "OK: test_env\n");
    return tvq_t_fail ? 1 : 0;
}
