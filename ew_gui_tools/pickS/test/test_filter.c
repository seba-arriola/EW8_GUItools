/* test_filter.c — estabilidad y atenuación fuera de banda del pasa-banda. */
#include "pickS.h"

static double rms(const double *v, int a, int b)
{
    double s = 0.0; int i;
    for (i = a; i < b; i++) s += v[i] * v[i];
    return sqrt(s / (b - a));
}

int main(void)
{
    double fs = 100.0, f1 = 1.0, f2 = 10.0;
    int    n = 2000, i;
    PickS_BiQuad bq;
    double in[2000], out[2000];
    double ri, ro, ratio_inband, ratio_outband;

    /* In-band (5 Hz) */
    for (i = 0; i < n; i++) in[i] = sin(2.0 * 3.14159265358979 * 5.0 * i / fs);
    PickS_FilterInit(&bq, fs, f1, f2, 4);
    for (i = 0; i < n; i++) out[i] = PickS_FilterApply(&bq, in[i]);
    ri = rms(in, n / 2, n); ro = rms(out, n / 2, n);
    ratio_inband = ro / (ri + 1e-12);

    /* Out-of-band (40 Hz) */
    for (i = 0; i < n; i++) in[i] = sin(2.0 * 3.14159265358979 * 40.0 * i / fs);
    PickS_FilterInit(&bq, fs, f1, f2, 4);
    for (i = 0; i < n; i++) out[i] = PickS_FilterApply(&bq, in[i]);
    ri = rms(in, n / 2, n); ro = rms(out, n / 2, n);
    ratio_outband = ro / (ri + 1e-12);

    printf("in-band ratio=%.3f out-band ratio=%.5f\n", ratio_inband, ratio_outband);

    if (!(ratio_inband > 0.3 && ratio_inband < 2.0)) {
        printf("FAIL: in-band no conservado\n"); return 1;
    }
    if (!(ratio_outband < 0.4)) {
        printf("FAIL: fuera de banda no atenuado\n"); return 1;
    }
    printf("test_filter OK\n");
    return 0;
}
