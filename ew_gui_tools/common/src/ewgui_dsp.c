#include <limits.h>
#include <math.h>
#include <stdlib.h>

#include "ewgui/dsp.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Portado de csnhypodbp_dsp.c (a su vez de EW7 dataprocessing.c) y de
 * csntvp.c. Misma matemática; solo se unifica el tipo de muestra a int32_t. */

#define EW_MAX_SECTIONS 4   /* orden maximo 8 = 4 biquads */

typedef struct {
    double b0, b1, b2, a1, a2;
} EwBiquad;

/* Satura al rango de int32_t. Los no finitos (NaN/Inf) van a 0 para no
 * envenenar el estado del IIR ni dibujar picos basura por cast indefinido. */
static int32_t clamp_i32(double v)
{
    if (!isfinite(v))
        return 0;
    if (v >= 2147483647.0)
        return INT32_MAX;
    if (v <= -2147483648.0)
        return INT32_MIN;
    return (int32_t)v;
}

/* Q de las secciones de un Butterworth de orden par: Q_k = 1/(2 cos(theta_k)),
 * theta_k = pi(2k+1)/(2N). Para N=2 da 0.7071; para N=4, 0.5412 y 1.3066. */
static int butter_sections(int order, double *Q)
{
    if (order < 2)
        order = 2;
    if (order & 1)
        order++;
    if (order > 8)
        order = 8;

    int ns = order / 2;
    for (int k = 0; k < ns; k++) {
        double th = M_PI * (2.0 * (double)k + 1.0) / (2.0 * (double)order);
        Q[k] = 1.0 / (2.0 * cos(th));
    }
    return ns;
}

static void butter_design(EwFilterType type, double w0, double Q, EwBiquad *q)
{
    double cosW = cos(w0);
    double sinW = sin(w0);
    double alpha = sinW / (2.0 * Q);
    double a0 = 1.0 + alpha;

    if (type == EW_FILTER_HP) {
        q->b0 = ((1.0 + cosW) / 2.0) / a0;
        q->b1 = -(1.0 + cosW) / a0;
        q->b2 = ((1.0 + cosW) / 2.0) / a0;
    } else {
        q->b0 = ((1.0 - cosW) / 2.0) / a0;
        q->b1 = (1.0 - cosW) / a0;
        q->b2 = ((1.0 - cosW) / 2.0) / a0;
    }
    q->a1 = (-2.0 * cosW) / a0;
    q->a2 = (1.0 - alpha) / a0;
}

/* Una seccion causal sobre int32 (estado reiniciado en cada gap). */
static void biquad_causal(int32_t *data, size_t n, const EwBiquad *q, int is_lp)
{
    double x1 = (double)data[0], x2 = (double)data[0];
    double y1 = 0.0, y2 = 0.0;

    if (is_lp) {
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
        double y0 = q->b0*x0 + q->b1*x1 + q->b2*x2 - q->a1*y1 - q->a2*y2;
        if (!isfinite(y0)) {
            x1 = x2 = 0.0;
            y1 = y2 = 0.0;
            data[i] = 0;
            continue;
        }
        x2 = x1; x1 = x0;
        y2 = y1; y1 = y0;
        data[i] = clamp_i32(y0);
    }
}

/* Una seccion sobre doubles (para la pasada forward/backward). */
static void biquad_double(double *x, size_t n, const EwBiquad *q)
{
    double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;

    for (size_t i = 0; i < n; i++) {
        double x0 = x[i];
        double y0 = q->b0*x0 + q->b1*x1 + q->b2*x2 - q->a1*y1 - q->a2*y2;
        if (!isfinite(y0))
            y0 = 0.0;
        x2 = x1; x1 = x0;
        y2 = y1; y1 = y0;
        x[i] = y0;
    }
}

static void reverse_double(double *x, size_t n)
{
    for (size_t i = 0, j = n - 1; i < j; i++, j--) {
        double t = x[i];
        x[i] = x[j];
        x[j] = t;
    }
}

void ewgui_filter_iir(int32_t *data, size_t n, double fs,
                      EwFilterType type, double fc, int order)
{
    if (!data || fs <= 0.0 || n == 0 || fc <= 0.0 || fc >= fs * 0.5)
        return;   /* sin frecuencia de Nyquist válida: no filtrar (evita inestabilidad) */

    /* Menos de medio ciclo en toda la traza: el biquad no tiene resolución
     * para esa fc (coeficientes al borde de la cancelación) y su efecto es
     * indistinguible de no filtrar. */
    if (fc * (double)n / fs < 0.5)
        return;

    double Q[EW_MAX_SECTIONS];
    int ns = butter_sections(order, Q);
    double w0 = 2.0 * M_PI * fc / fs;

    for (int b = 0; b < ns; b++) {
        EwBiquad q;
        butter_design(type, w0, Q[b], &q);
        biquad_causal(data, n, &q, type != EW_FILTER_HP);
    }
}

void ewgui_filter_bandpass(int32_t *data, size_t n, double fs,
                           double f1, double f2, int order)
{
    ewgui_filter_iir(data, n, fs, EW_FILTER_HP, f1, order);
    ewgui_filter_iir(data, n, fs, EW_FILTER_LP, f2, order);
}

void ewgui_filter_iir_zp(int32_t *data, size_t n, double fs,
                         EwFilterType type, double fc, int order)
{
    if (!data || fs <= 0.0 || n == 0 || fc <= 0.0 || fc >= fs * 0.5)
        return;
    if (fc * (double)n / fs < 0.5)
        return;

    double Q[EW_MAX_SECTIONS];
    int ns = butter_sections(order, Q);
    double w0 = 2.0 * M_PI * fc / fs;

    size_t pad = (size_t)(3 * (order + 1));
    if (pad < 9)
        pad = 9;
    if (n < 2 * pad + 2)
        return;   /* tramo demasiado corto: se queda con el demean */

    size_t i = 0;
    while (i < n) {
        if (data[i] == INT_MAX) {
            i++;
            continue;
        }
        size_t s = i;
        while (i < n && data[i] != INT_MAX)
            i++;
        size_t e = i;
        size_t m = e - s;
        if (m < 2 * pad + 2)
            continue;

        size_t total = m + 2 * pad;
        double *buf = malloc(total * sizeof(double));
        if (!buf)
            continue;

        for (size_t j = 0; j < m; j++)
            buf[pad + j] = (double)data[s + j];
        /* Padding por reflexión impar alrededor de los extremos. */
        for (size_t k = 1; k <= pad; k++) {
            buf[pad - k]         = 2.0 * (double)data[s]     - (double)data[s + k];
            buf[pad + m - 1 + k] = 2.0 * (double)data[e - 1] - (double)data[e - 1 - k];
        }

        for (int b = 0; b < ns; b++) {
            EwBiquad q;
            butter_design(type, w0, Q[b], &q);
            biquad_double(buf, total, &q);
            reverse_double(buf, total);
            biquad_double(buf, total, &q);
            reverse_double(buf, total);
        }

        for (size_t j = 0; j < m; j++)
            data[s + j] = clamp_i32(buf[pad + j]);
        free(buf);
    }
}

void ewgui_filter_bandpass_zp(int32_t *data, size_t n, double fs,
                              double f1, double f2, int order)
{
    ewgui_filter_iir_zp(data, n, fs, EW_FILTER_HP, f1, order);
    ewgui_filter_iir_zp(data, n, fs, EW_FILTER_LP, f2, order);
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
            out[k] = clamp_i32((double)in[k] - mean);
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

/* --- Procesado de la traza de VISTA de magnitudes ------------------------ */

void ewgui_filter_wood_anderson(int32_t *data, size_t n, double fs,
                                double period, double damp, double gain)
{
    double K, w0, a, b, c, d, b0, b1, b2, a1, a2;
    double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;

    if (!data || n == 0 || fs <= 0.0 || period <= 0.0)
        return;
    if (!(damp > 0.0)) damp = 0.70710678;
    if (!(gain > 0.0)) gain = 1.0;
    /* Transformada bilineal (s -> K(1-z^-1)/(1+z^-1)) de
       H(s) = gain * s^2 / (s^2 + 2*damp*w0*s + w0^2). */
    K = 2.0 * fs;
    w0 = 2.0 * M_PI / period;
    a = K * K;
    b = 2.0 * damp * w0 * K;
    c = w0 * w0;
    d = a + b + c;
    if (d == 0.0)
        return;
    b0 = gain * a / d;
    b1 = -2.0 * b0;
    b2 = b0;
    a1 = (-2.0 * a + 2.0 * c) / d;
    a2 = (a - b + c) / d;
    for (size_t i = 0; i < n; i++) {
        double x0, y0;
        if (data[i] == INT_MAX) {           /* gap: no envenenar el estado */
            x1 = x2 = y1 = y2 = 0.0;
            continue;
        }
        x0 = (double)data[i];
        y0 = b0 * x0 + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x0;
        y2 = y1; y1 = y0;
        data[i] = clamp_i32(y0);
    }
}

void ewgui_integrate(const int32_t *in, size_t n, double dt, double *out)
{
    double acc = 0.0;

    if (!in || !out || n == 0)
        return;
    if (!(dt > 0.0)) dt = 1.0;
    out[0] = 0.0;
    for (size_t i = 1; i < n; i++) {
        if (in[i] == INT_MAX || in[i - 1] == INT_MAX) {
            out[i] = NAN;
            continue;
        }
        acc += 0.5 * ((double)in[i] + (double)in[i - 1]) * dt;
        out[i] = acc;
    }
}

size_t ewgui_decimate(const int32_t *in, size_t n, int factor, int32_t *out)
{
    size_t i, k = 0;

    if (!in || !out || n == 0)
        return 0;
    if (factor < 1) factor = 1;
    for (i = 0; i < n; i += (size_t)factor)
        out[k++] = in[i];
    return k;
}
