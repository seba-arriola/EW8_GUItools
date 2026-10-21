#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ewgui/wave.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define CAP 10000

static void fill_sine(int32_t *circ, int n, double amp, double period_samps)
{
    for (int i = 0; i < n; i++)
        circ[i] = (int32_t)(amp * sin(2.0 * M_PI * (double)i / period_samps));
}

int main(void)
{
    const double rate = 100.0;
    const int64_t last_abs = CAP - 1;
    const double t_end = 100.0;       /* abs_end = 10000 */
    const double window_secs = 10.0;
    const double pad_secs = 1.0;
    const int width = 100;

    int32_t circ[CAP];
    fill_sine(circ, CAP, 1000.0, 50.0);

    EwTraceCache c;
    ewgui_trace_cache_init(&c);

    EwFilterParams raw = {0, 0.0, 0.0, 0, 0};

    /* 1. Primera llamada: recalcula (1) */
    int r1 = ewgui_trace_envelope(circ, CAP, last_abs, rate, t_end,
                                  window_secs, pad_secs, width, &raw, &c);
    assert(r1 == 1);
    assert(c.env_valid == 1);
    assert(c.env_width == width);

    double min1[width], max1[width];
    memcpy(min1, c.env_min, sizeof(double) * width);
    memcpy(max1, c.env_max, sizeof(double) * width);

    /* La senal tiene amplitud ~1000: la envolvente debe reflejarlo */
    double gmax = 0;
    for (int i = 0; i < width; i++) if (max1[i] > gmax) gmax = max1[i];
    assert(gmax > 500.0 && gmax <= 1100.0);

    /* 2. Segunda llamada identica: cache-hit (0) y envolvente igual */
    int r2 = ewgui_trace_envelope(circ, CAP, last_abs, rate, t_end,
                                  window_secs, pad_secs, width, &raw, &c);
    assert(r2 == 0);
    assert(memcmp(min1, c.env_min, sizeof(double) * width) == 0);
    assert(memcmp(max1, c.env_max, sizeof(double) * width) == 0);

    /* 3. Cambio de filtro: recalcula (1) */
    EwFilterParams hp = {1, 5.0, 0.0, 2, 0};
    int r3 = ewgui_trace_envelope(circ, CAP, last_abs, rate, t_end,
                                  window_secs, pad_secs, width, &hp, &c);
    assert(r3 == 1);

    /* 4. Determinismo: dos cachés distintas dan la misma envolvente */
    EwTraceCache c2;
    ewgui_trace_cache_init(&c2);
    ewgui_trace_envelope(circ, CAP, last_abs, rate, t_end,
                         window_secs, pad_secs, width, &hp, &c2);
    assert(memcmp(c.env_min, c2.env_min, sizeof(double) * width) == 0);
    assert(memcmp(c.env_max, c2.env_max, sizeof(double) * width) == 0);
    assert(c.env_max_abs == c2.env_max_abs);

    /* 5. Rango inválido -> -1 */
    assert(ewgui_trace_envelope(circ, CAP, last_abs, rate, t_end,
                                100000.0, pad_secs, width, &raw, &c) == -1);

    ewgui_trace_cache_free(&c);
    ewgui_trace_cache_free(&c2);

    /* 6. EwGuiTrace: posee raw+filt; clear/finish/filter */
    {
        EwGuiTrace *t = ewgui_trace_new(200);
        assert(t != NULL);
        assert(ewgui_trace_capacity(t) == 200);
        assert(ewgui_trace_length(t) == 0);

        int32_t *raw = ewgui_trace_raw(t);
        for (int i = 0; i < 200; i++) raw[i] = 100;   /* DC */
        raw[150] = INT_MAX; raw[151] = INT_MAX;       /* gap corto */
        for (int i = 180; i < 200; i++) raw[i] = INT_MAX; /* cola futura */
        ewgui_trace_set_length(t, 200);
        ewgui_trace_set_rate(t, 100.0);
        ewgui_trace_set_oldest(t, 0.0);

        ewgui_trace_finish(t);
        /* cola recortada en 180 */
        assert(ewgui_trace_length(t) == 180);

        EwFilterParams hp = {1, 0.7, 0.0, 4, 0};
        ewgui_trace_filter(t, &hp);
        int32_t *filt = ewgui_trace_filtered(t);
        long mx = 0;
        for (int i = 100; i < 170; i++) if (labs((long)filt[i]) > mx) mx = labs((long)filt[i]);
        assert(mx <= 5);   /* HP elimina el DC */

        ewgui_trace_clear(t);
        assert(ewgui_trace_length(t) == 0);
        assert(ewgui_trace_raw(t)[0] == INT_MAX);
        ewgui_trace_free(t);
    }

    /* 7. set_span: oldest+rate+length de una vez. Regresion csnhypodbp: la
       primera descarga creaba la traza con oldest=0 y el draw no dibujaba. */
    {
        const double req_start = 1600000000.0;
        const long   len = 500;
        EwGuiTrace *t = ewgui_trace_new(1000);
        assert(t != NULL);
        for (long i = 0; i < len; i++) ewgui_trace_raw(t)[i] = 10;

        ewgui_trace_set_span(t, req_start, rate, len);
        assert(ewgui_trace_oldest(t) == req_start);
        assert(ewgui_trace_rate(t) == rate);
        assert(ewgui_trace_length(t) == len);

        /* clamp a la capacidad */
        ewgui_trace_set_span(t, req_start, rate, 5000);
        assert(ewgui_trace_length(t) == 1000);

        /* finish NO resetea oldest */
        ewgui_trace_set_span(t, req_start, rate, len);
        ewgui_trace_finish(t);
        assert(ewgui_trace_oldest(t) == req_start);

        /* invariante del draw: (t - oldest)*rate dentro de [0, length) */
        {
            double win_start = req_start + 2.0;
            long k0 = (long)((win_start - ewgui_trace_oldest(t)) * ewgui_trace_rate(t));
            assert(k0 >= 0 && k0 < ewgui_trace_length(t));
        }
        ewgui_trace_free(t);
    }

    /* 8. zerophase: no mueve el pico y NO toca el buffer crudo */
    {
        const long len = 4000;
        EwGuiTrace *t = ewgui_trace_new(len);
        assert(t != NULL);
        for (long i = 0; i < len; i++)
            ewgui_trace_raw(t)[i] = 0;
        /* una wavelet tipo Ricker (~1.5 Hz), aislada, en el centro */
        for (long i = 1850; i < 2150; i++) {
            double u = (double)(i - 2000) / 30.0;
            ewgui_trace_raw(t)[i] = (int32_t)(8000.0 * (1.0 - 2.0*u*u) * exp(-u*u));
        }
        ewgui_trace_set_span(t, 0.0, rate, len);

        int32_t *raw = ewgui_trace_raw(t);
        int32_t *snapshot = malloc((size_t)len * sizeof(int32_t));
        memcpy(snapshot, raw, (size_t)len * sizeof(int32_t));

        EwFilterParams bp = {3, 1.0, 20.0, 2, 1};
        ewgui_trace_filter(t, &bp);

        assert(memcmp(snapshot, raw, (size_t)len * sizeof(int32_t)) == 0);

        int32_t *filt = ewgui_trace_filtered(t);
        long peak = 0;
        for (long i = 100; i < len - 100; i++)
            if (labs((long)filt[i]) > labs((long)filt[peak])) peak = i;
        assert(labs(peak - 2000) <= 1);

        /* el mismo filtro en causal sí se retrasa */
        EwFilterParams bpc = {3, 1.0, 20.0, 2, 0};
        ewgui_trace_filter(t, &bpc);
        long peakc = 0;
        for (long i = 100; i < len - 100; i++)
            if (labs((long)filt[i]) > labs((long)filt[peakc])) peakc = i;
        assert(peakc - 2000 >= 5);

        free(snapshot);
        ewgui_trace_free(t);
    }

    printf("ALL WAVE TESTS PASSED\n");
    return 0;
}
