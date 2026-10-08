/*
 * Bench headless de la envolvente (criterio C6): N trazas x M sps.
 * Objetivo: < 16 ms. No forma parte de `make check` (se corre con `make bench`).
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "ewgui/wave.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define NSTA   64
#define RATE   250.0
#define SECS   30.0
#define WIDTH  1200
#define ITERS  10

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

int main(void)
{
    long cap = (long)(RATE * SECS);
    int32_t **buf = malloc(NSTA * sizeof(int32_t *));
    EwTraceCache *caches = calloc(NSTA, sizeof(EwTraceCache));
    EwFilterParams fp = {0, 0.0, 0.0, 0};

    for (int i = 0; i < NSTA; i++) {
        buf[i] = malloc((size_t)cap * sizeof(int32_t));
        for (long k = 0; k < cap; k++)
            buf[i][k] = (int32_t)(1000.0 * sin(2.0 * M_PI * (double)k / 50.0));
        ewgui_trace_cache_init(&caches[i]);
    }

    double t0 = now_ms();
    for (int it = 0; it < ITERS; it++) {
        for (int i = 0; i < NSTA; i++) {
            ewgui_trace_envelope(buf[i], cap, (int64_t)cap - 1, RATE,
                                 SECS, SECS - 2.0, 1.0, WIDTH, &fp, &caches[i]);
            caches[i].proc_valid = 0;   /* fuerza recálculo del procesado */
        }
    }
    double dt = (now_ms() - t0) / ITERS;

    printf("bench_wave: %d trazas x %ld sps -> envolvente en %.2f ms (objetivo < 16)\n",
           NSTA, cap, dt);

    for (int i = 0; i < NSTA; i++) {
        ewgui_trace_cache_free(&caches[i]);
        free(buf[i]);
    }
    free(buf);
    free(caches);

    return dt < 16.0 ? 0 : 1;
}
