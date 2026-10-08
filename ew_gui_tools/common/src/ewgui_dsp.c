#include <limits.h>
#include <math.h>

#include "ewgui/dsp.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Portado de csnhypodbp_dsp.c (a su vez de EW7 dataprocessing.c) y de
 * csntvp.c. Misma matemática; solo se unifica el tipo de muestra a int32_t. */

void ewgui_filter_iir(int32_t *data, size_t n, double fs,
                      EwFilterType type, double fc, int order)
{
    if (fs <= 0.0 || n == 0 || fc <= 0.0)
        return;

    double w0 = 2.0 * M_PI * fc / fs;
    double cosW = cos(w0);
    double sinW = sin(w0);

    int n_biquads = (order >= 4) ? 2 : 1;

    double Q[2] = {0.70710678, 0.0};
    if (n_biquads == 2) {
        Q[0] = 0.54119610;
        Q[1] = 1.30656296;
    }

    for (int b = 0; b < n_biquads; b++) {
        double alpha = sinW / (2.0 * Q[b]);
        double a0 = 1.0 + alpha;
        double b0_f, b1_f, b2_f, a1_f, a2_f;

        if (type == EW_FILTER_HP) {
            b0_f = ((1.0 + cosW) / 2.0) / a0;
            b1_f = -(1.0 + cosW) / a0;
            b2_f = ((1.0 + cosW) / 2.0) / a0;
        } else {
            b0_f = ((1.0 - cosW) / 2.0) / a0;
            b1_f = (1.0 - cosW) / a0;
            b2_f = ((1.0 - cosW) / 2.0) / a0;
        }

        a1_f = (-2.0 * cosW) / a0;
        a2_f = (1.0 - alpha) / a0;

        double x1 = (double)data[0], x2 = (double)data[0];
        double y1 = 0.0, y2 = 0.0;
        if (type != EW_FILTER_HP) {
            y1 = (double)data[0];
            y2 = (double)data[0];
        }

        for (size_t i = 0; i < n; i++) {
            if (data[i] == INT_MAX) {
                x1 = x2 = 0.0;
                y1 = y2 = 0.0;
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

void ewgui_filter_bandpass(int32_t *data, size_t n, double fs,
                           double f1, double f2, int order)
{
    ewgui_filter_iir(data, n, fs, EW_FILTER_HP, f1, order);
    ewgui_filter_iir(data, n, fs, EW_FILTER_LP, f2, order);
}

void ewgui_interpolate_short_gaps(int32_t *data, size_t n, double fs)
{
    if (n == 0 || fs <= 0.0)
        return;

    long thr = (long)(0.5 * fs);
    if (thr < 10)
        thr = 10;

    size_t i = 0;
    while (i < n) {
        if (data[i] != INT_MAX) { i++; continue; }
        size_t s = i;
        while (i < n && data[i] == INT_MAX) i++;
        size_t e = i;
        size_t len = e - s;
        if ((long)len >= thr)
            continue;

        int has_left  = (s > 0 && data[s-1] != INT_MAX);
        int has_right = (e < n && data[e]   != INT_MAX);
        if (!has_left || !has_right)
            continue;

        double left  = (double)data[s-1];
        double right = (double)data[e];
        for (size_t k = s; k < e; k++) {
            double frac = (double)(k - s + 1) / (double)(len + 1);
            data[k] = (int32_t)(left + frac * (right - left));
        }
    }
}

void ewgui_demean(const int32_t *in, size_t n, int32_t *out)
{
    if (n == 0)
        return;

    double mean = 0.0;
    size_t nvalid = 0;
    for (size_t k = 0; k < n; k++) {
        if (in[k] == INT_MAX)
            continue;
        mean += (double)in[k];
        nvalid++;
    }
    if (nvalid > 0)
        mean /= (double)nvalid;

    for (size_t k = 0; k < n; k++) {
        if (in[k] == INT_MAX)
            out[k] = INT_MAX;
        else
            out[k] = (int32_t)((double)in[k] - mean);
    }
}

size_t ewgui_find_data_end(const int32_t *data, size_t n)
{
    if (n == 0)
        return 0;
    for (size_t i = n; i-- > 0; ) {
        if (data[i] != INT_MAX)
            return i + 1;
    }
    return 0;
}
