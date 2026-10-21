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

    /* 10. fc en/por encima de Nyquist: no-op (evita IIR inestable -> NaN/basura) */
    {
        int32_t *d = malloc((size_t)N * sizeof(int32_t));
        int32_t *ref = malloc((size_t)N * sizeof(int32_t));
        for (long i = 0; i < N; i++) {
            d[i] = (int32_t)(1000.0 * sin(2.0*M_PI*5.0*(double)i/fs));
            ref[i] = d[i];
        }
        ewgui_filter_iir(d, (size_t)N, fs, EW_FILTER_HP, 50.0, 4);  /* == Nyquist */
        ewgui_filter_iir(d, (size_t)N, fs, EW_FILTER_LP, 80.0, 4);  /* > Nyquist */
        int same = 1;
        for (long i = 0; i < N; i++) if (d[i] != ref[i]) { same = 0; break; }
        CHECK(same, "fc >= Nyquist: no-op (sin inestabilidad)");
        free(d); free(ref);
    }

    /* 11. demean satura en vez de envolver (cast indefinido a int32) */
    {
        int32_t raw[200], filt[200];
        raw[0] = 2147483000;
        for (int i = 1; i < 200; i++) raw[i] = -2147483000;
        ewgui_demean(raw, 200, filt);
        CHECK(filt[0] > 0 && filt[0] <= INT_MAX, "demean: satura sin wrap a negativo");
    }

    /* 12. fase cero: un impulso no se mueve (el causal se mide con wavelet) */
    {
        int32_t *zp = malloc((size_t)N * sizeof(int32_t));
        for (long i = 0; i < N; i++) zp[i] = 0;
        zp[4000] = 10000;
        ewgui_filter_iir_zp(zp, (size_t)N, fs, EW_FILTER_HP, 1.0, 2);
        long ip = 0;
        for (long i = 0; i < N; i++)
            if (labs((long)zp[i]) > labs((long)zp[ip])) ip = i;
        CHECK(labs(ip - 4000) <= 1, "zp: impulso no se desplaza (+-1 muestra)");
        free(zp);
    }

    /* 13. fase cero: respuesta en amplitud |H|^2. En fc => 0.5 (-6 dB);
     *      en 2fc, orden 2 => 1/(1+2^4) = -24.6 dB (orden 4 efectivo). */
    {
        int32_t *d = malloc((size_t)N * sizeof(int32_t));
        for (long i = 0; i < N; i++)
            d[i] = (int32_t)(1000.0 * sin(2.0*M_PI*1.0*(double)i/fs));
        ewgui_filter_iir_zp(d, (size_t)N, fs, EW_FILTER_LP, 1.0, 2);
        double amp = 0;
        for (long i = N/4; i < 3*N/4; i++) amp = fmax(amp, fabs((double)d[i]));
        CHECK(fabs(amp / 1000.0 - 0.5) < 0.02, "zp orden 2: |H|^2 = 0.5 en fc (-6 dB)");

        for (long i = 0; i < N; i++)
            d[i] = (int32_t)(1000.0 * sin(2.0*M_PI*2.0*(double)i/fs));
        ewgui_filter_iir_zp(d, (size_t)N, fs, EW_FILTER_LP, 1.0, 2);
        amp = 0;
        for (long i = N/4; i < 3*N/4; i++) amp = fmax(amp, fabs((double)d[i]));
        double db = 20.0 * log10(amp / 1000.0 + 1e-12);
        CHECK(fabs(db - (-24.6)) < 2.0, "zp orden 2: ~-24 dB en 2fc (orden 4 efectivo)");
        free(d);
    }

    /* 14. fase cero: gaps preservados y sin contaminacion entre tramos */
    {
        int32_t *d = malloc((size_t)N * sizeof(int32_t));
        for (long i = 0; i < N; i++)
            d[i] = (int32_t)(500.0 * sin(2.0*M_PI*2.0*(double)i/fs));
        d[4000] = INT_MAX; d[4001] = INT_MAX; d[4002] = INT_MAX;
        ewgui_filter_iir_zp(d, (size_t)N, fs, EW_FILTER_LP, 10.0, 2);
        CHECK(d[4000] == INT_MAX && d[4001] == INT_MAX && d[4002] == INT_MAX,
              "zp: gaps INT_MAX preservados");
        CHECK(d[3999] != INT_MAX && d[4003] != INT_MAX && labs((long)d[3999]) < 1500,
              "zp: vecinos del gap finitos y sin explotar");
        free(d);
    }

    /* 15. fc irreal (0.001 Hz): HP no-op, el resultado equivale a solo LP */
    {
        int32_t *a = malloc((size_t)N * sizeof(int32_t));
        int32_t *b = malloc((size_t)N * sizeof(int32_t));
        for (long i = 0; i < N; i++) {
            a[i] = (int32_t)(1000.0 * sin(2.0*M_PI*3.0*(double)i/fs) + 500.0);
            b[i] = a[i];
        }
        ewgui_filter_bandpass_zp(a, (size_t)N, fs, 0.001, 12.0, 4);
        ewgui_filter_iir_zp(b, (size_t)N, fs, EW_FILTER_LP, 12.0, 4);
        long maxdiff = 0;
        int finite = 1;
        for (long i = 0; i < N; i++) {
            if (a[i] == INT_MAX) continue;
            long df = labs((long)a[i] - (long)b[i]);
            if (df > maxdiff) maxdiff = df;
            if (a[i] == INT32_MAX || a[i] == INT32_MIN) finite = 0;
        }
        CHECK(maxdiff <= 2, "zp: HP por debajo de 1/(2T) es no-op");
        CHECK(finite, "zp: fc=0.001 no satura");
        free(a); free(b);
    }

    /* 16. tramo demasiado corto: no se toca (solo demeaning en el llamador) */
    {
        int32_t d[15];
        for (int i = 0; i < 15; i++) d[i] = i * 10;
        int32_t ref[15];
        for (int i = 0; i < 15; i++) ref[i] = d[i];
        ewgui_filter_iir_zp(d, 15, fs, EW_FILTER_HP, 1.0, 2);
        int same = 1;
        for (int i = 0; i < 15; i++) if (d[i] != ref[i]) { same = 0; break; }
        CHECK(same, "zp: tramo corto intacto");
    }

    if (failures == 0) { printf("\nALL DSP TESTS PASSED\n"); return 0; }
    printf("\n%d DSP TEST(S) FAILED\n", failures);
    return 1;
}
