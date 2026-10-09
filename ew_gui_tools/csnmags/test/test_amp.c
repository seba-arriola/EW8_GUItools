#include "csnmags.h"
#include <stdio.h>
#include <math.h>
#include <assert.h>

/* test_amp.c - medidas de amplitud/periodo y bandpass. */

int main(void)
{
    double x[1000];
    long i;
    for (i = 0; i < 1000; i++) x[i] = sin(2.0 * M_PI * 1.0 * i / 100.0);   /* 1 Hz, fs=100 */

    assert(fabs(mag_amp_zero2peak(x, 1000, 0, 1000) - 1.0) < 1e-6);
    {
        double p = mag_amp_period(x, 1000, 5, 995, 0.01);
        assert(fabs(p - 1.0) < 0.05);
    }
    {
        double a = mag_amp_peak2peak(x, 1000, 0, 1000, 0.01, 0.8);
        assert(fabs(a - 1.0) < 1e-6);
    }
    {
        double dc[1000];
        for (i = 0; i < 1000; i++) dc[i] = 5.0 + 0.5 * sin(2.0 * M_PI * 2.0 * i / 100.0);
        mag_bandpass(dc, 1000, 0.01, 0.5, 5.0, 4);
        /* tras un HP el DC decae: la media final debe ser pequeña */
        {
            double m = 0; long j;
            for (j = 500; j < 1000; j++) m += dc[j];
            m /= 500.0;
            assert(fabs(m) < 0.5);
        }
    }
    printf("test_amp OK\n");
    return 0;
}
