#include "csnmags.h"

#include <string.h>
#include <math.h>

/*
 * mag_response.c - Respuesta instrumental autocontenida.
 *
 *   - Lector SAC PZ (CONSTANT / ZEROS n / POLES n).
 *   - Evaluación H(s=j2*pi*f) = constant * prod(s-z)/prod(s-p).
 *   - Deconvolución en frecuencia con FFT radix-2 propia y water-level.
 *
 * No depende del motor de transferencia de EarthWorm (transfer.o no está
 * precompilado en este árbol); es autocontenido y testeable.
 */

static void build_name(const char *pattern, const MagStation *st, char *out, size_t osz)
{
    size_t oi = 0;
    const char *p;
    for (p = pattern; *p && oi + 1 < osz; p++) {
        if (*p == '%' && p[1]) {
            const char *rep = NULL;
            switch (p[1]) {
                case 'S': rep = st->sta; break;
                case 'C': rep = st->chan; break;
                case 'N': rep = st->net; break;
                case 'L': rep = st->loc; break;
                default: break;
            }
            if (rep) { for (; *rep && oi + 1 < osz; rep++) out[oi++] = *rep; p++; continue; }
        }
        out[oi++] = *p;
    }
    out[oi] = '\0';
}

int mag_resp_load(const MagConfig *cfg, const MagStation *st, MagResponse *out)
{
    char path[MAG_STR * 2], full[MAG_STR * 3];
    memset(out, 0, sizeof(*out));
    build_name(cfg->resp_pattern, st, path, sizeof(path));
    snprintf(full, sizeof(full), "%s/%s", cfg->resp_dir, path);
    if (mag_pz_read(full, &out->pz) == 0) {
        if (cfg->resp_in_meters) out->pz.constant *= 1.0e-9;  /* m -> nm */
        out->has_pz = 1;
        out->loaded = 1;
    }
    return out->loaded;
}

void mag_resp_free(MagResponse *r) { if (r) memset(r, 0, sizeof(*r)); }

/* ------------------------------ SAC PZ ------------------------------ */

int mag_pz_read(const char *path, MagPZ *out)
{
    FILE *f;
    char  line[256];
    int   expect = 0;   /* 0 = nada, 1 = ceros, 2 = polos */
    int   iz = 0, ip = 0;

    memset(out, 0, sizeof(*out));
    out->constant = 1.0;

    f = fopen(path, "r");
    if (!f) return -1;

    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '*' || *p == '#' || *p == '\n' || *p == '\0') continue;

        if (strncmp(p, "CONSTANT", 8) == 0) { sscanf(p + 8, "%lf", &out->constant); expect = 0; }
        else if (strncmp(p, "ZEROS", 5) == 0) {
            sscanf(p + 5, "%d", &out->nz); iz = 0; expect = 1;
            if (out->nz > 32) { fclose(f); return -1; }
        }
        else if (strncmp(p, "POLES", 5) == 0) {
            sscanf(p + 5, "%d", &out->np); ip = 0; expect = 2;
            if (out->np > 32) { fclose(f); return -1; }
        }
        else {
            double a, b = 0.0;
            if (sscanf(p, "%lf %lf", &a, &b) < 1) continue;
            if (expect == 1 && iz < out->nz) { out->zeros[iz].re = a; out->zeros[iz].im = b; iz++; }
            else if (expect == 2 && ip < out->np) { out->poles[ip].re = a; out->poles[ip].im = b; ip++; }
        }
    }
    fclose(f);
    return 0;
}

int mag_pz_eval(const MagPZ *pz, double f_hz, MagCx *out)
{
    MagCx s, num = { 1.0, 0.0 }, den = { 1.0, 0.0 };
    int i;
    s.re = 0.0; s.im = 2.0 * M_PI * f_hz;
    for (i = 0; i < pz->nz; i++) {
        MagCx d = { s.re - pz->zeros[i].re, s.im - pz->zeros[i].im };
        MagCx t = { num.re * d.re - num.im * d.im, num.re * d.im + num.im * d.re };
        num = t;
    }
    for (i = 0; i < pz->np; i++) {
        MagCx d = { s.re - pz->poles[i].re, s.im - pz->poles[i].im };
        MagCx t = { den.re * d.re - den.im * d.im, den.re * d.im + den.im * d.re };
        den = t;
    }
    {
        double mag2 = den.re * den.re + den.im * den.im;
        if (mag2 == 0.0) { out->re = out->im = 0.0; return -1; }
        out->re = pz->constant * (num.re * den.re + num.im * den.im) / mag2;
        out->im = pz->constant * (num.im * den.re - num.re * den.im) / mag2;
    }
    return 0;
}

/* ------------------------------ FFT radix-2 ------------------------------ */

static long next_pow2(long n) { long p = 1; while (p < n) p <<= 1; return p; }

void mag_fft(double *re, double *im, long n, int inverse)
{
    long i, j, k, m, step;
    for (i = 1, j = 0; i < n; i++) {
        long bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            double t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }
    for (step = 2; step <= n; step <<= 1) {
        double ang = (inverse ? 2.0 : -2.0) * M_PI / (double)step;
        double wr = cos(ang), wi = sin(ang);
        for (m = 0; m < n; m += step) {
            double cr = 1.0, ci = 0.0;
            for (k = 0; k < step / 2; k++) {
                long a = m + k, b = m + k + step / 2;
                double tr = re[b] * cr - im[b] * ci;
                double ti = re[b] * ci + im[b] * cr;
                re[b] = re[a] - tr; im[b] = im[a] - ti;
                re[a] += tr;        im[a] += ti;
                {
                    double nr = cr * wr - ci * wi;
                    ci = cr * wi + ci * wr; cr = nr;
                }
            }
        }
    }
    if (inverse) for (i = 0; i < n; i++) { re[i] /= (double)n; im[i] /= (double)n; }
}

/* ------------------------------ deconvolución ------------------------------ */

int mag_resp_convert(const MagPZ *orig, const MagPZ *target, double *x, long n, double dt)
{
    long nfft, i;
    double *re, *im, wmax = 0.0, df, wl;

    if (!orig || !target || !x || n < 4 || dt <= 0.0) return -1;

    nfft = next_pow2(n);
    re = (double *)calloc((size_t)nfft, sizeof(double));
    im = (double *)calloc((size_t)nfft, sizeof(double));
    if (!re || !im) { free(re); free(im); return -1; }

    for (i = 0; i < n; i++) re[i] = x[i];
    mag_fft(re, im, nfft, 0);

    df = 1.0 / ((double)nfft * dt);
    for (i = 0; i < nfft; i++) {
        double f = (i <= nfft / 2) ? i * df : (double)(i - nfft) * df;
        MagCx ho;
        mag_pz_eval(orig, f, &ho);
        {
            double mag = sqrt(ho.re * ho.re + ho.im * ho.im);
            if (mag > wmax) wmax = mag;
        }
    }
    wl = wmax * 1.0e-3;
    if (wl <= 0.0) wl = 1.0e-12;

    for (i = 0; i < nfft; i++) {
        double f = (i <= nfft / 2) ? i * df : (double)(i - nfft) * df;
        MagCx ho, ht;
        double den, xr, xi, nr, ni;
        mag_pz_eval(orig, f, &ho);
        mag_pz_eval(target, f, &ht);
        den = ho.re * ho.re + ho.im * ho.im;
        if (den < wl * wl) den = wl * wl;
        xr = (ht.re * ho.re + ht.im * ho.im) / den;
        xi = (ht.im * ho.re - ht.re * ho.im) / den;
        nr = re[i] * xr - im[i] * xi;
        ni = re[i] * xi + im[i] * xr;
        re[i] = nr; im[i] = ni;
    }
    mag_fft(re, im, nfft, 1);
    for (i = 0; i < n; i++) x[i] = re[i];

    free(re); free(im);
    return 0;
}

int mag_resp_make_wa(MagPZ *wa, double period, double damp, double gain)
{
    double omega = 2.0 * M_PI / period;
    double mu = sqrt(1.0 - damp * damp);
    memset(wa, 0, sizeof(*wa));
    wa->constant = gain;
    wa->nz = 2; wa->np = 2;
    wa->zeros[0].re = 0; wa->zeros[0].im = 0;
    wa->zeros[1].re = 0; wa->zeros[1].im = 0;
    wa->poles[0].re = -omega * damp; wa->poles[0].im =  omega * mu;
    wa->poles[1].re = -omega * damp; wa->poles[1].im = -omega * mu;
    return 0;
}

int mag_resp_make_disp(MagPZ *disp, double gain)
{
    memset(disp, 0, sizeof(*disp));
    disp->constant = gain;
    return 0;
}
