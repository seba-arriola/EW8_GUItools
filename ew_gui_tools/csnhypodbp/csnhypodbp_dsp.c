/******************************************************************
 * csnhypodbp_dsp.c                                               *
 *                                                                *
 * Procesamiento digital de senal (DSP) para csnhypodbp (EW8).    *
 * Portado de EW7 new_hypo_display/libsrc/dataprocessing.c,       *
 * adaptado de `long*` a `int32_t*` (los buffers de csnhypodbp    *
 * usan int32_t) y al sentinel de gap INT_MAX de este modulo.     *
 *                                                                *
 * Funciones:                                                     *
 *  - aplicar_filtro_iir_int32: Butterworth IIR orden 2/4 (HP/LP),*
 *    resetea el estado en cada gap (INT_MAX) para no generar     *
 *    transitorios/artefactos entre segmentos discontinuos.       *
 *  - interpolate_short_gaps: interpola gaps cortos (< ~0.5 s)    *
 *    entre muestras validas; deja intactos los gaps largos       *
 *    (INT_MAX) como espacio vacio.                               *
 *  - demean_trace_station: quita el offset DC preservando gaps.  *
 *  - find_data_end_station: recorta la cola de INT_MAX (datos    *
 *    futuros no registrados).                                    *
 ******************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <limits.h>
#include "csnhypodbp.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* --------------------------------------------------------------------
 * Butterworth IIR dinámico (orden 2 y 4).
 *   type : 1 = High-Pass, 2 = Low-Pass
 *   order: 2 o 4
 * El estado se inicializa con la primera muestra para evitar el pico
 * de la respuesta al impulso; en cada gap (INT_MAX) se resetea el
 * estado y el gap queda intacto en la salida.
 * -------------------------------------------------------------------- */
void aplicar_filtro_iir_int32(int32_t *data, long size, double fs, int type, double fc, int order) {
    if (fs <= 0.0 || size == 0 || fc <= 0.0) return;

    double w0 = 2.0 * M_PI * fc / fs;
    double cosW = cos(w0);
    double sinW = sin(w0);

    int n_biquads = (order >= 4) ? 2 : 1;

    /* Factores Q de Butterworth */
    double Q[2] = {0.70710678, 0.0};
    if (n_biquads == 2) {
        Q[0] = 0.54119610;
        Q[1] = 1.30656296;
    }

    for (int b = 0; b < n_biquads; b++) {
        double alpha = sinW / (2.0 * Q[b]);
        double a0 = 1.0 + alpha;
        double b0_f, b1_f, b2_f, a1_f, a2_f;

        if (type == 1) { /* High-Pass */
            b0_f = ((1.0 + cosW) / 2.0) / a0;
            b1_f = -(1.0 + cosW) / a0;
            b2_f = ((1.0 + cosW) / 2.0) / a0;
        } else { /* Low-Pass */
            b0_f = ((1.0 - cosW) / 2.0) / a0;
            b1_f = (1.0 - cosW) / a0;
            b2_f = ((1.0 - cosW) / 2.0) / a0;
        }

        a1_f = (-2.0 * cosW) / a0;
        a2_f = (1.0 - alpha) / a0;

        /* Estado inicial con la primera muestra para evitar transitorio */
        double x1 = (double)data[0], x2 = (double)data[0];
        double y1 = 0.0, y2 = 0.0;
        if (type != 1) { y1 = (double)data[0]; y2 = (double)data[0]; } /* LPF: DC pasa */

        for (long i = 0; i < size; i++) {
            /* Gap (INT_MAX): resetea el estado para que cada segmento
               continuo se procese por separado y el gap permanezca. */
            if (data[i] == INT_MAX) {
                x1 = x2 = 0.0; y1 = y2 = 0.0;
                continue;
            }
            double x0 = (double)data[i];
            double y0 = b0_f*x0 + b1_f*x1 + b2_f*x2 - a1_f*y1 - a2_f*y2;
            x2 = x1; x1 = x0;
            y2 = y1; y1 = y0;
            data[i] = (int32_t)y0;
        }
    }
}

/* --------------------------------------------------------------------
 * interpolate_short_gaps: en EW8 los huecos se marcan con INT_MAX
 * (no con 0 como el writer de disco de EW7). Se interpola linealmente
 * cada run corto de INT_MAX (< ~0.5 s) flanqueado por muestras validas;
 * los runs largos (gaps reales) y los que tocan el borde del buffer
 * quedan como INT_MAX (espacio vacio, sin fabricar datos).
 * -------------------------------------------------------------------- */
void interpolate_short_gaps(DEV_STATION *pSta) {
    long n = pSta->lRawCircCtr;
    if (n <= 0 || pSta->dSampRate <= 0.0) return;

    long thr = (long)(0.5 * pSta->dSampRate);
    if (thr < 10) thr = 10;

    long i = 0;
    while (i < n) {
        if (pSta->plRawCircBuff[i] != INT_MAX) { i++; continue; }
        long s = i;
        while (i < n && pSta->plRawCircBuff[i] == INT_MAX) i++;
        long e = i;
        long len = e - s;
        if (len >= thr) continue; /* gap real: se mantiene como vacio */

        int has_left  = (s > 0 && pSta->plRawCircBuff[s-1] != INT_MAX);
        int has_right = (e < n && pSta->plRawCircBuff[e]   != INT_MAX);
        if (!has_left || !has_right) continue; /* borde de buffer: no fabricar */

        double left  = (double)pSta->plRawCircBuff[s-1];
        double right = (double)pSta->plRawCircBuff[e];
        for (long k = s; k < e; k++) {
            double frac = (double)(k - s + 1) / (double)(len + 1);
            pSta->plRawCircBuff[k] = (int32_t)(left + frac * (right - left));
        }
    }
}

/* --------------------------------------------------------------------
 * demean_trace_station: calcula y elimina el offset DC. Los gaps
 * (INT_MAX) se copian intactos a plFiltCircBuff para no contaminar
 * la media ni el render.
 * -------------------------------------------------------------------- */
void demean_trace_station(DEV_STATION *pSta) {
    if (pSta->lRawCircCtr <= 0) return;

    double mean = 0.0;
    long nvalid = 0;
    for (long k = 0; k < pSta->lRawCircCtr; k++) {
        if (pSta->plRawCircBuff[k] == INT_MAX) continue;
        mean += (double)pSta->plRawCircBuff[k];
        nvalid++;
    }
    if (nvalid > 0) mean /= (double)nvalid;

    for (long k = 0; k < pSta->lRawCircCtr; k++) {
        if (pSta->plRawCircBuff[k] == INT_MAX) {
            pSta->plFiltCircBuff[k] = INT_MAX;
        } else {
            pSta->plFiltCircBuff[k] = (int32_t)((double)pSta->plRawCircBuff[k] - mean);
        }
    }
}

/* --------------------------------------------------------------------
 * find_data_end_station: recorta la cola de INT_MAX (datos futuros
 * no registrados) del buffer, ajustando lRawCircCtr.
 * -------------------------------------------------------------------- */
void find_data_end_station(DEV_STATION *pSta) {
    long lLastNonZero = -1;
    if (pSta->lRawCircCtr > 0) {
        for (long i = pSta->lRawCircCtr - 1; i >= 0; i--) {
            if (pSta->plRawCircBuff[i] != INT_MAX) {
                lLastNonZero = i;
                break;
            }
        }
        if (lLastNonZero == -1) {
            pSta->lRawCircCtr = 0;
        } else {
            pSta->lRawCircCtr = lLastNonZero + 1;
        }
    }
}
