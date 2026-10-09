#include "csnmags.h"

#include <math.h>

/*
 * mag_amp.c - Medidas de amplitud y periodo sobre series temporales double.
 * Filtro bandpass RBJ (Butterworth Q=1/sqrt(2)) implementado en dobles para no
 * depender de la capa dsp (que trabaja en int32).
 */

double mag_amp_zero2peak(const double *x, long n, long i0, long i1)
{
    double a = 0.0;
    long   i;
    if (!x) return 0.0;
    if (i0 < 0) i0 = 0;
    if (i1 > n) i1 = n;
    for (i = i0; i < i1; i++) {
        double v = fabs(x[i]);
        if (v > a) a = v;
    }
    return a;
}

double mag_amp_peak2peak(const double *x, long n, long i0, long i1, double dt,
                         double window_s)
{
    long w, i;
    double best = 0.0;
    if (!x || dt <= 0.0) return 0.0;
    if (i0 < 0) i0 = 0;
    if (i1 > n) i1 = n;
    w = (long)(window_s / dt + 0.5);
    if (w < 2) w = 2;
    if (i1 - i0 < w) {
        double mn = 0.0, mx = 0.0;
        for (i = i0; i < i1; i++) { if (x[i] < mn) mn = x[i]; if (x[i] > mx) mx = x[i]; }
        return 0.5 * (mx - mn);
    }
    {
        double mn = 0.0, mx = 0.0;
        /* inicializa la primera ventana */
        for (i = i0; i < i0 + w; i++) { if (x[i] < mn) mn = x[i]; if (x[i] > mx) mx = x[i]; }
        best = 0.5 * (mx - mn);
        for (i = i0 + w; i < i1; i++) {
            /* recalcula min/max de la ventana [i-w+1, i] de forma simple */
            long j; mn = x[i - w + 1]; mx = x[i - w + 1];
            for (j = i - w + 1; j <= i; j++) { if (x[j] < mn) mn = x[j]; if (x[j] > mx) mx = x[j]; }
            if (0.5 * (mx - mn) > best) best = 0.5 * (mx - mn);
        }
    }
    return best;
}

double mag_amp_period(const double *x, long n, long i0, long i1, double dt)
{
    long i, ipk = -1;
    double amax = 0.0, mean = 0.0, zc_before = 0.0, zc_after = 0.0;
    if (!x || dt <= 0.0) return 0.0;
    if (i0 < 0) i0 = 0;
    if (i1 > n) i1 = n;
    if (i1 - i0 < 3) return 0.0;

    for (i = i0; i < i1; i++) mean += x[i];
    mean /= (double)(i1 - i0);

    for (i = i0; i < i1; i++) {
        double v = fabs(x[i] - mean);
        if (v > amax) { amax = v; ipk = i; }
    }
    if (ipk < 0 || amax <= 0.0) return 0.0;

    /* cero anterior */
    for (i = ipk; i > i0; i--) {
        if ((x[i] - mean) * (x[i - 1] - mean) <= 0.0) { zc_before = (double)i; break; }
    }
    /* cero posterior */
    for (i = ipk; i < i1 - 1; i++) {
        if ((x[i] - mean) * (x[i + 1] - mean) <= 0.0) { zc_after = (double)i; break; }
    }
    if (zc_after <= zc_before) return 0.0;
    return 2.0 * (zc_after - zc_before) * dt;
}

double mag_rms(const double *x, long n)
{
    double s = 0.0;
    long i;
    if (!x || n <= 0) return 0.0;
    for (i = 0; i < n; i++) s += x[i] * x[i];
    return sqrt(s / (double)n);
}

/* --------------------------- biquad --------------------------- */

typedef struct { double b0, b1, b2, a1, a2; } Biquad;

static Biquad make_lp(double fc, double fs)
{
    Biquad q; double w0 = 2.0 * M_PI * fc / fs, c = cos(w0), s = sin(w0);
    double alpha = s / (2.0 * 0.7071067811865476), a0 = 1.0 + alpha;
    q.b0 = ((1.0 - c) / 2.0) / a0;
    q.b1 = (1.0 - c) / a0;
    q.b2 = ((1.0 - c) / 2.0) / a0;
    q.a1 = (-2.0 * c) / a0;
    q.a2 = (1.0 - alpha) / a0;
    return q;
}

static Biquad make_hp(double fc, double fs)
{
    Biquad q; double w0 = 2.0 * M_PI * fc / fs, c = cos(w0), s = sin(w0);
    double alpha = s / (2.0 * 0.7071067811865476), a0 = 1.0 + alpha;
    q.b0 = ((1.0 + c) / 2.0) / a0;
    q.b1 = (-(1.0 + c)) / a0;
    q.b2 = ((1.0 + c) / 2.0) / a0;
    q.a1 = (-2.0 * c) / a0;
    q.a2 = (1.0 - alpha) / a0;
    return q;
}

static void apply_biquad(const Biquad *q, double *x, long n)
{
    double z1 = 0.0, z2 = 0.0;
    long i;
    for (i = 0; i < n; i++) {
        double in = x[i];
        double out = q->b0 * in + z1;
        z1 = q->b1 * in - q->a1 * out + z2;
        z2 = q->b2 * in - q->a2 * out;
        x[i] = out;
    }
}

void mag_bandpass(double *x, long n, double dt, double f_lo, double f_hi, int order)
{
    double fs = 1.0 / dt;
    int    reps = (order >= 4) ? 2 : 1;
    int    i;
    if (!x || n <= 0 || dt <= 0.0) return;
    if (f_lo > 0.0 && f_lo < fs / 2.0) {
        Biquad hp = make_hp(f_lo, fs);
        for (i = 0; i < reps; i++) apply_biquad(&hp, x, n);
    }
    if (f_hi > 0.0 && f_hi < fs / 2.0) {
        Biquad lp = make_lp(f_hi, fs);
        for (i = 0; i < reps; i++) apply_biquad(&lp, x, n);
    }
}
