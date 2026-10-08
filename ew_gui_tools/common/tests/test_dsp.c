/* Vectores portados de csntvp/test_filter.c y csnhypodbp/test_filter.c. */
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "ewgui/dsp.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int failures = 0;
#define CHECK(cond, name) do { \
    if (cond) { printf("  [PASS] %s\n", name); } \
    else      { printf("  [FAIL] %s\n", name); failures++; } \
} while (0)

int main(void)
{
    const long N = 8000;
    const double fs = 100.0;

    /* 1. HP elimina DC (cola estacionaria ~ 0) */
    {
        int32_t *d = malloc((size_t)N * sizeof(int32_t));
        for (long i = 0; i < N; i++) d[i] = 1000;
        ewgui_filter_iir(d, (size_t)N, fs, EW_FILTER_HP, 0.7, 4);
        double tail = 0;
        for (long i = N - 200; i < N; i++) tail += fabs((double)d[i]);
        tail /= 200.0;
        CHECK(tail < 1.0, "HP elimina DC");
        free(d);
    }

    /* 2. LP conserva DC y atenúa 50 Hz */
    {
        int32_t *d = malloc((size_t)N * sizeof(int32_t));
        for (long i = 0; i < N; i++)
            d[i] = (int32_t)(1000.0 + 500.0 * sin(2.0*M_PI*50.0*(double)i/fs));
        ewgui_filter_iir(d, (size_t)N, fs, EW_FILTER_LP, 2.0, 4);
        double mean = 0;
        for (long i = N - 2000; i < N; i++) mean += (double)d[i];
        mean /= 2000.0;
        double amp = 0;
        for (long i = N - 2000; i < N; i++) amp = fmax(amp, fabs((double)d[i] - mean));
        CHECK(fabs(mean - 1000.0) < 5.0, "LP conserva DC");
        CHECK(amp < 10.0, "LP atenúa 50 Hz");
        free(d);
    }

    /* 3. Orden 4 atenúa más que orden 2 por debajo de la esquina */
    {
        int32_t *d2 = malloc((size_t)N * sizeof(int32_t));
        int32_t *d4 = malloc((size_t)N * sizeof(int32_t));
        for (long i = 0; i < N; i++) {
            d2[i] = (int32_t)(1000.0 * sin(2.0*M_PI*1.0*(double)i/fs));
            d4[i] = d2[i];
        }
        ewgui_filter_iir(d2, (size_t)N, fs, EW_FILTER_HP, 5.0, 2);
        ewgui_filter_iir(d4, (size_t)N, fs, EW_FILTER_HP, 5.0, 4);
        double a2 = 0, a4 = 0;
        for (long i = N - 2000; i < N; i++) {
            a2 = fmax(a2, fabs((double)d2[i]));
            a4 = fmax(a4, fabs((double)d4[i]));
        }
        CHECK(a4 < a2, "orden 4 más selectivo que orden 2");
        free(d2); free(d4);
    }

    /* 4. Gaps INT_MAX preservados y vecinos filtrados */
    {
        int32_t *d = malloc((size_t)N * sizeof(int32_t));
        for (long i = 0; i < N; i++)
            d[i] = (int32_t)(500.0 * sin(2.0*M_PI*2.0*(double)i/fs));
        d[4000] = INT_MAX; d[4001] = INT_MAX; d[4002] = INT_MAX;
        ewgui_filter_iir(d, (size_t)N, fs, EW_FILTER_LP, 10.0, 4);
        CHECK(d[4000] == INT_MAX && d[4001] == INT_MAX && d[4002] == INT_MAX,
              "gaps INT_MAX preservados");
        CHECK(d[3999] != INT_MAX && d[4003] != INT_MAX,
              "vecinos del gap filtrados (finitos)");
        free(d);
    }

    /* 5. Band-pass (HP+LP) atenúa 30 Hz */
    {
        int32_t *d = malloc((size_t)N * sizeof(int32_t));
        for (long i = 0; i < N; i++)
            d[i] = (int32_t)(1000.0 * sin(2.0*M_PI*30.0*(double)i/fs));
        ewgui_filter_bandpass(d, (size_t)N, fs, 0.7, 2.0, 4);
        double amp = 0;
        for (long i = N - 2000; i < N; i++) amp = fmax(amp, fabs((double)d[i]));
        CHECK(amp < 200.0, "bandpass atenúa 30 Hz");
        free(d);
    }

    /* 6. interpolate: gap largo se conserva */
    {
        int32_t buf[120];
        for (int i = 0; i < 30; i++) buf[i] = 10;
        for (int i = 30; i < 110; i++) buf[i] = INT_MAX;
        for (int i = 110; i < 120; i++) buf[i] = 20;
        ewgui_interpolate_short_gaps(buf, 120, 100.0);
        CHECK(buf[40] == INT_MAX && buf[109] == INT_MAX, "interp: gap largo se conserva");
    }

    /* 7. interpolate: gap corto se interpola */
    {
        int32_t buf[60];
        for (int i = 0; i < 30; i++) buf[i] = 0;
        buf[30] = INT_MAX; buf[31] = INT_MAX; buf[32] = INT_MAX;
        for (int i = 33; i < 60; i++) buf[i] = 30;
        ewgui_interpolate_short_gaps(buf, 60, 100.0);
        CHECK(buf[30] != INT_MAX && buf[31] != INT_MAX && buf[32] != INT_MAX,
              "interp: gap corto se interpola");
        CHECK(buf[30] > 0 && buf[32] < 30, "interp: entre vecinos");
    }

    /* 8. demean preserva el gap y centra */
    {
        int32_t raw[50], filt[50];
        for (int i = 0; i < 50; i++) raw[i] = 100 + (i % 5);
        raw[25] = INT_MAX;
        ewgui_demean(raw, 50, filt);
        CHECK(filt[25] == INT_MAX, "demean: gap conservado");
        double mean = 0; int nv = 0;
        for (int i = 0; i < 50; i++) if (filt[i] != INT_MAX) { mean += filt[i]; nv++; }
        if (nv) mean /= nv;
        CHECK(fabs(mean) < 1.0, "demean: media ~0");
    }

    /* 9. find_data_end recorta la cola */
    {
        int32_t buf[40];
        for (int i = 0; i < 40; i++) buf[i] = (i < 30) ? 5 : INT_MAX;
        CHECK(ewgui_find_data_end(buf, 40) == 30, "find_data_end: recorta cola");
    }

    if (failures == 0) { printf("\nALL DSP TESTS PASSED\n"); return 0; }
    printf("\n%d DSP TEST(S) FAILED\n", failures);
    return 1;
}
