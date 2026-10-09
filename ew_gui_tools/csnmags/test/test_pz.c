#include "csnmags.h"
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <assert.h>

/* test_pz.c - lector SAC PZ, evaluación, FFT y deconvolución. */

static void write_pz(const char *path, double constant, int nz, int np,
                     double zr, double zi, double pr, double pi)
{
    FILE *f = fopen(path, "w");
    fprintf(f, "CONSTANT %g\n", constant);
    fprintf(f, "ZEROS %d\n", nz);
    if (nz > 0) fprintf(f, "%g %g\n", zr, zi);
    fprintf(f, "POLES %d\n", np);
    if (np > 0) fprintf(f, "%g %g\n", pr, pi);
    fclose(f);
}

int main(void)
{
    const char *pf = "test/tmp_pz.pz";
    MagPZ pz, flat;

    /* 1) eval: H = 2 * s/(s+1) con s=j*2*pi*f */
    write_pz(pf, 2.0, 1, 1, 0.0, 0.0, -1.0, 0.0);
    assert(mag_pz_read(pf, &pz) == 0);
    assert(pz.nz == 1 && pz.np == 1);
    {
        double f = 1.0;
        MagCx h;
        double w = 2.0 * M_PI * f;
        double want = 2.0 * w / sqrt(w * w + 1.0);
        mag_pz_eval(&pz, f, &h);
        assert(fabs(sqrt(h.re * h.re + h.im * h.im) - want) < 1e-9);
    }
    remove(pf);

    /* 2) FFT round-trip */
    {
        long n = 8; double re[8] = {1, 2, 3, 4, 5, 6, 7, 8}, im[8] = {0};
        double re0[8]; long i;
        memcpy(re0, re, sizeof(re0));
        mag_fft(re, im, n, 0);
        mag_fft(re, im, n, 1);
        for (i = 0; i < n; i++) assert(fabs(re[i] - re0[i]) < 1e-9);
    }

    /* 3) Deconvolución con orig plana=2 y target plana=1 -> x/2 */
    {
        double x[256]; long i;
        MagPZ orig, tgt;
        for (i = 0; i < 256; i++) x[i] = 10.0 * sin(2.0 * M_PI * 3.0 * i / 64.0);
        mag_resp_make_disp(&orig, 2.0);
        mag_resp_make_disp(&tgt, 1.0);
        assert(mag_resp_convert(&orig, &tgt, x, 256, 0.01) == 0);
        /* intenta recuperar la sinusoide dividida por 2 en el centro */
        {
            double ref = 10.0 * sin(2.0 * M_PI * 3.0 * 128 / 64.0) / 2.0;
            assert(fabs(x[128] - ref) < 1.0);
        }
    }

    /* 4) target plana ganancia 1: identidad */
    {
        double x[256]; long i;
        for (i = 0; i < 256; i++) x[i] = 3.0 * cos(2.0 * M_PI * 2.0 * i / 64.0);
        mag_resp_make_disp(&flat, 1.0);
        assert(mag_resp_convert(&flat, &flat, x, 256, 0.01) == 0);
        assert(fabs(x[128] - 3.0 * cos(2.0 * M_PI * 2.0 * 128 / 64.0)) < 1.0);
    }

    printf("test_pz OK\n");
    return 0;
}
