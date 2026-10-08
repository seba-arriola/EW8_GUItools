/******************************************************************
 * test_filter.c                                                  *
 *                                                                *
 * Test del DSP de csnhypodbp. Tras la Fase 3 el DSP vive en      *
 * libewgui (ewgui_dsp/ewgui_wave); este harness valida la        *
 * integración vía EwGuiTrace (filtro, gaps, demean, recorte).    *
 ******************************************************************/

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "ewgui/wave.h"

static int g_failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("  FAIL: %s\n", msg); g_failures++; } \
    else         { printf("  PASS: %s\n", msg); } \
} while (0)

int main(void) {
    printf("== csnhypodbp DSP tests (libewgui) ==\n");

    /* 1. HP: preserva gaps y elimina el DC */
    {
        EwGuiTrace *t = ewgui_trace_new(200);
        int32_t *raw = ewgui_trace_raw(t);
        for (int i = 0; i < 200; i++) raw[i] = 100;
        raw[100] = INT_MAX; raw[101] = INT_MAX;
        ewgui_trace_set_length(t, 200);
        ewgui_trace_set_rate(t, 100.0);
        EwFilterParams hp = {1, 0.7, 0.0, 4};
        ewgui_trace_filter(t, &hp);
        int32_t *f = ewgui_trace_filtered(t);
        CHECK(f[100] == INT_MAX && f[101] == INT_MAX, "HP: el gap se mantiene INT_MAX");
        long mx = 0;
        for (int i = 0; i < 200; i++)
            if (f[i] != INT_MAX && labs((long)f[i]) > mx) mx = labs((long)f[i]);
        CHECK(mx <= 5, "HP: elimina el DC");
        ewgui_trace_free(t);
    }

    /* 2. LP: atenúa la alta frecuencia */
    {
        EwGuiTrace *t = ewgui_trace_new(100);
        int32_t *raw = ewgui_trace_raw(t);
        for (int i = 0; i < 100; i++) raw[i] = (int32_t)(1000.0 * sin(2*M_PI*30.0*i/100.0));
        ewgui_trace_set_length(t, 100);
        ewgui_trace_set_rate(t, 100.0);
        EwFilterParams lp = {2, 2.0, 0.0, 4};
        ewgui_trace_filter(t, &lp);
        int32_t *f = ewgui_trace_filtered(t);
        long mx = 0;
        for (int i = 50; i < 100; i++) if (labs((long)f[i]) > mx) mx = labs((long)f[i]);
        CHECK(mx < 200, "LP: atenúa la alta frecuencia");
        ewgui_trace_free(t);
    }

    /* 3. finish: interpola gap corto y recorta la cola de INT_MAX */
    {
        EwGuiTrace *t = ewgui_trace_new(60);
        int32_t *raw = ewgui_trace_raw(t);
        for (int i = 0; i < 60; i++) raw[i] = (i < 30) ? 0 : ((i < 50) ? 30 : INT_MAX);
        raw[10] = INT_MAX; raw[11] = INT_MAX;
        ewgui_trace_set_length(t, 60);
        ewgui_trace_set_rate(t, 100.0);
        ewgui_trace_finish(t);
        raw = ewgui_trace_raw(t);
        CHECK(raw[10] != INT_MAX && raw[11] != INT_MAX, "finish: gap corto interpolado");
        CHECK(ewgui_trace_length(t) == 50, "finish: recorta la cola de datos futuros");
        ewgui_trace_free(t);
    }

    if (g_failures == 0) { printf("\nTODOS LOS TESTS PASARON\n"); return 0; }
    printf("\n%d TEST(S) FALLARON\n", g_failures);
    return 1;
}
