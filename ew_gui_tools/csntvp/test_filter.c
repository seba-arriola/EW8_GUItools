/* test_filter.c - unit test for the ported aplicar_filtro_iir (Butterworth IIR).
   Standalone harness (not linked into csntvp). Build: gcc -O2 -o test_filter test_filter.c -lm */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <limits.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Verbatim copy of the filter ported into csntvp.c */
static void aplicar_filtro_iir(long *data, long size, double fs, int type, double fc, int order) {
    if (fs <= 0.0 || size == 0 || fc <= 0.0) return;
    double w0 = 2.0 * M_PI * fc / fs;
    double cosW = cos(w0), sinW = sin(w0);
    int n_biquads = (order >= 4) ? 2 : 1;
    double Q[2] = {0.70710678, 0.0};
    if (n_biquads == 2) { Q[0] = 0.54119610; Q[1] = 1.30656296; }
    for (int b = 0; b < n_biquads; b++) {
        double alpha = sinW / (2.0 * Q[b]);
        double a0 = 1.0 + alpha;
        double b0_f, b1_f, b2_f, a1_f, a2_f;
        if (type == 1) { b0_f = ((1.0 + cosW) / 2.0) / a0; b1_f = -(1.0 + cosW) / a0; b2_f = ((1.0 + cosW) / 2.0) / a0; }
        else { b0_f = ((1.0 - cosW) / 2.0) / a0; b1_f = (1.0 - cosW) / a0; b2_f = ((1.0 - cosW) / 2.0) / a0; }
        a1_f = (-2.0 * cosW) / a0; a2_f = (1.0 - alpha) / a0;
        double x1 = (double)data[0], x2 = (double)data[0];
        double y1 = 0.0, y2 = 0.0;
        if (type == 1) { y1 = 0.0; y2 = 0.0; }
        else { y1 = (double)data[0]; y2 = (double)data[0]; }
        for (long i = 0; i < size; i++) {
            if (data[i] == INT_MAX) { x1 = x2 = 0.0; y1 = y2 = 0.0; continue; }
            double x0 = (double)data[i];
            double y0 = b0_f*x0 + b1_f*x1 + b2_f*x2 - a1_f*y1 - a2_f*y2;
            x2 = x1; x1 = x0; y2 = y1; y1 = y0;
            data[i] = (long)y0;
        }
    }
}

static int failures = 0;
#define CHECK(cond, name) do { if (cond) { printf("  [PASS] %s\n", name); } else { printf("  [FAIL] %s\n", name); failures++; } } while (0)

int main(void) {
    const long N = 8000;
    const double fs = 100.0;

    /* --- 1. High-pass removes DC --- */
    {
        long *d = malloc(N * sizeof(long));
        for (long i = 0; i < N; i++) d[i] = 1000; /* pure DC */
        aplicar_filtro_iir(d, N, fs, 1, 0.7, 4);
        double tail = 0; for (long i = N - 200; i < N; i++) tail += fabs((double)d[i]);
        tail /= 200.0;
        CHECK(tail < 1.0, "HP removes DC (steady-state tail ~ 0)");
        free(d);
    }

    /* --- 2. Low-pass passes DC and attenuates high frequency --- */
    {
        long *d = malloc(N * sizeof(long));
        for (long i = 0; i < N; i++) d[i] = (long)(1000.0 + 500.0 * sin(2.0*M_PI*50.0*(double)i/fs)); /* DC + 50Hz (Nyquist) */
        aplicar_filtro_iir(d, N, fs, 2, 2.0, 4);
        double mean = 0; for (long i = N - 2000; i < N; i++) mean += (double)d[i]; mean /= 2000.0;
        double amp = 0; for (long i = N - 2000; i < N; i++) amp = fmax(amp, fabs((double)d[i] - mean));
        CHECK(fabs(mean - 1000.0) < 5.0, "LP preserves DC (~1000)");
        CHECK(amp < 10.0, "LP attenuates 50Hz component");
        free(d);
    }

    /* --- 3. Order 4 has steeper rolloff than order 2 --- */
    {
        /* 1Hz signal, HP at 5Hz: both orders should attenuate, order 4 more */
        long *d2 = malloc(N * sizeof(long));
        long *d4 = malloc(N * sizeof(long));
        for (long i = 0; i < N; i++) { d2[i] = (long)(1000.0*sin(2.0*M_PI*1.0*(double)i/fs)); d4[i] = d2[i]; }
        aplicar_filtro_iir(d2, N, fs, 1, 5.0, 2);
        aplicar_filtro_iir(d4, N, fs, 1, 5.0, 4);
        double a2 = 0, a4 = 0;
        for (long i = N - 2000; i < N; i++) { a2 = fmax(a2, fabs((double)d2[i])); a4 = fmax(a4, fabs((double)d4[i])); }
        CHECK(a4 < a2, "order 4 attenuates 1Hz (below 5Hz HP corner) more than order 2");
        free(d2); free(d4);
    }

    /* --- 4. INT_MAX gaps are preserved and reset the state --- */
    {
        long *d = malloc(N * sizeof(long));
        for (long i = 0; i < N; i++) d[i] = (long)(500.0*sin(2.0*M_PI*2.0*(double)i/fs));
        d[4000] = INT_MAX; d[4001] = INT_MAX; d[4002] = INT_MAX;
        aplicar_filtro_iir(d, N, fs, 2, 10.0, 4);
        int gap_ok = (d[4000] == INT_MAX && d[4001] == INT_MAX && d[4002] == INT_MAX);
        CHECK(gap_ok, "INT_MAX gaps preserved untouched");
        int neighbor_finite = (d[3999] != INT_MAX && d[4003] != INT_MAX);
        CHECK(neighbor_finite, "samples around the gap are still filtered (finite)");
        free(d);
    }

    printf("\n%s: %d failure(s)\n", failures ? "FAILED" : "ALL TESTS PASSED", failures);
    return failures ? 1 : 0;
}
