/******************************************************************
 * test_filter.c                                                  *
 *                                                                *
 * Test unitario del DSP de csnhypodbp (filtro IIR + manejo de    *
 * gaps). No requiere GTK ni EarthWorm enlazados (solo libm).     *
 ******************************************************************/

#include "csnhypodbp.h"
#include <stdio.h>
#include <math.h>

static int g_failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("  FAIL: %s\n", msg); g_failures++; } \
    else         { printf("  PASS: %s\n", msg); } \
} while (0)

int main(void) {
    printf("== csnhypodbp DSP tests ==\n");

    /* 1. HP: preserva el gap y no genera NaN/Inf */
    {
        int32_t data[200];
        for (int i = 0; i < 200; i++) data[i] = 100;
        data[100] = INT_MAX; data[101] = INT_MAX;
        aplicar_filtro_iir_int32(data, 200, 100.0, 1, 0.7, 4);

        CHECK(data[100] == INT_MAX && data[101] == INT_MAX, "HP: el gap se mantiene INT_MAX");

        int bad = 0;
        for (int i = 0; i < 200; i++)
            if (data[i] != INT_MAX && (isnan((double)data[i]) || isinf((double)data[i]))) bad++;
        CHECK(bad == 0, "HP: sin NaN/Inf en la salida");
    }

    /* 1b. HP elimina el DC en una senal continua (sin gaps) */
    {
        int32_t data[100];
        for (int i = 0; i < 100; i++) data[i] = 100;
        aplicar_filtro_iir_int32(data, 100, 100.0, 1, 0.7, 4);
        long max_out = 0;
        for (int i = 0; i < 100; i++) if (labs((long)data[i]) > max_out) max_out = labs((long)data[i]);
        CHECK(max_out <= 5, "HP: elimina el DC (senal continua)");
    }

    /* 2. LP: preserva el gap y no explota */
    {
        int32_t data[100];
        for (int i = 0; i < 100; i++) data[i] = 50;
        data[50] = INT_MAX;
        aplicar_filtro_iir_int32(data, 100, 100.0, 2, 2.0, 2);
        CHECK(data[50] == INT_MAX, "LP: el gap se mantiene INT_MAX");
        int bad = 0;
        for (int i = 0; i < 100; i++)
            if (data[i] != INT_MAX && (isnan((double)data[i]) || isinf((double)data[i]))) bad++;
        CHECK(bad == 0, "LP: sin NaN/Inf");
    }

    /* 3. LP atenua una frecuencia alta (30 Hz con fc=2 Hz) */
    {
        int32_t data[100];
        for (int i = 0; i < 100; i++) data[i] = (int32_t)(1000.0 * sin(2 * M_PI * 30.0 * i / 100.0));
        aplicar_filtro_iir_int32(data, 100, 100.0, 2, 2.0, 4);
        long max_out = 0;
        for (int i = 50; i < 100; i++) if (labs((long)data[i]) > max_out) max_out = labs((long)data[i]);
        CHECK(max_out < 200, "LP: atenua la alta frecuencia");
    }

    /* 4. interpolate_short_gaps: gap largo se conserva */
    {
        DEV_STATION st; memset(&st, 0, sizeof(st));
        int32_t buf[120];
        for (int i = 0; i < 30; i++) buf[i] = 10;
        for (int i = 30; i < 110; i++) buf[i] = INT_MAX; /* 80 muestras >= thr(50) */
        for (int i = 110; i < 120; i++) buf[i] = 20;
        st.plRawCircBuff = buf; st.lRawCircCtr = 120; st.dSampRate = 100.0;
        interpolate_short_gaps(&st);
        CHECK(buf[40] == INT_MAX && buf[109] == INT_MAX, "interp: gap largo se mantiene INT_MAX");
    }

    /* 5. interpolate_short_gaps: gap corto se interpola entre vecinos */
    {
        DEV_STATION st; memset(&st, 0, sizeof(st));
        int32_t buf[60];
        for (int i = 0; i < 30; i++) buf[i] = 0;
        buf[30] = INT_MAX; buf[31] = INT_MAX; buf[32] = INT_MAX; /* 3 muestras < thr */
        for (int i = 33; i < 60; i++) buf[i] = 30;
        st.plRawCircBuff = buf; st.lRawCircCtr = 60; st.dSampRate = 100.0;
        interpolate_short_gaps(&st);
        CHECK(buf[30] != INT_MAX && buf[31] != INT_MAX && buf[32] != INT_MAX, "interp: gap corto se interpola");
        CHECK(buf[30] > 0 && buf[32] < 30, "interp: valores interpolados entre vecinos");
    }

    /* 6. demean: preserva el gap y centra la senal */
    {
        DEV_STATION st; memset(&st, 0, sizeof(st));
        int32_t raw[50], filt[50];
        for (int i = 0; i < 50; i++) raw[i] = 100 + (i % 5);
        raw[25] = INT_MAX;
        st.plRawCircBuff = raw; st.plFiltCircBuff = filt; st.lRawCircCtr = 50;
        demean_trace_station(&st);
        CHECK(filt[25] == INT_MAX, "demean: el gap se conserva");
        double mean = 0.0; int nv = 0;
        for (int i = 0; i < 50; i++) if (filt[i] != INT_MAX) { mean += filt[i]; nv++; }
        if (nv) mean /= nv;
        CHECK(fabs(mean) < 1.0, "demean: media ~0");
    }

    /* 7. find_data_end: recorta la cola de INT_MAX */
    {
        DEV_STATION st; memset(&st, 0, sizeof(st));
        int32_t buf[40];
        for (int i = 0; i < 40; i++) buf[i] = (i < 30) ? 5 : INT_MAX;
        st.plRawCircBuff = buf; st.lRawCircCtr = 40;
        find_data_end_station(&st);
        CHECK(st.lRawCircCtr == 30, "find_data_end: recorta la cola de datos futuros");
    }

    if (g_failures == 0) { printf("\nTODOS LOS TESTS PASARON\n"); return 0; }
    printf("\n%d TEST(S) FALLARON\n", g_failures);
    return 1;
}
