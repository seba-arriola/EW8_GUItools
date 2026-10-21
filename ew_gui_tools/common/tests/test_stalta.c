#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ewgui/stalta.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int failures = 0;
#define CHECK(cond, name) do { \
    if (cond) { printf("  [PASS] %s\n", name); } \
    else      { printf("  [FAIL] %s\n", name); failures++; } \
} while (0)

int main(void)
{
    const size_t n = 6000;
    const double fs = 100.0;
    const double sta = 0.5;    /* 50 muestras */
    const double lta = 5.0;    /* 500 muestras */
    int32_t *x = malloc(n * sizeof(int32_t));
    double *cf = malloc(n * sizeof(double));

    CHECK(x != NULL && cf != NULL, "alloc");
    if (!x || !cf)
        return 1;

    /* 1. Senal estacionaria: la CF converge a 1 */
    for (size_t i = 0; i < n; i++)
        x[i] = (int32_t)(1000.0 * sin(2.0*M_PI*10.0*(double)i/fs));
    ewgui_stalta_run(x, n, fs, sta, lta, cf);

    double mn = 1e9, mx = -1e9;
    for (size_t i = 1500; i < n; i++) {
        if (cf[i] < mn) mn = cf[i];
        if (cf[i] > mx) mx = cf[i];
    }
    CHECK(fabs(mn - 1.0) < 0.05 && fabs(mx - 1.0) < 0.05,
          "senal estacionaria: CF ~ 1");

    /* 2. CF = 0 antes de llenar la LTA */
    {
        int zeros = 1;
        for (size_t i = 0; i + 1 < (size_t)(lta * fs); i++)
            if (cf[i] != 0.0) { zeros = 0; break; }
        CHECK(zeros, "CF = 0 antes de llenar la LTA");
    }

    /* 3. Escalon de amplitud: un solo disparo y cerca del onset */
    {
        const size_t k = 3000;
        for (size_t i = 0; i < n; i++) {
            double a = (i < k) ? 1.0 : 10.0;
            x[i] = (int32_t)(a * sin(2.0*M_PI*10.0*(double)i/fs));
        }
        ewgui_stalta_run(x, n, fs, sta, lta, cf);

        size_t idx[8];
        int nt = ewgui_stalta_triggers(cf, n, 3.0, 1.5, idx, 8);
        CHECK(nt == 1, "escalon: exactamente 1 disparo");
        CHECK(nt == 1 && idx[0] >= k, "escalon: el disparo no precede al onset");
        CHECK(nt == 1 && idx[0] <= k + (size_t)(sta * fs) + 2,
              "escalon: dispara dentro de una STA");

        double t_true = (double)k / fs;
        double t_est = (double)idx[0] / fs - sta / 2.0;
        CHECK(fabs(t_est - t_true) <= sta,
              "marca corregida dentro de +-STA (estimacion, no pick)");
    }

    /* 4. Ruido estacionario: sin disparos con on=3 */
    {
        for (size_t i = 0; i < n; i++)
            x[i] = (int32_t)(1000.0 * sin(2.0*M_PI*10.0*(double)i/fs));
        ewgui_stalta_run(x, n, fs, sta, lta, cf);
        size_t idx[8];
        CHECK(ewgui_stalta_triggers(cf, n, 3.0, 1.5, idx, 8) == 0,
              "ruido estacionario: 0 disparos");
    }

    /* 5. Gap: CF = 0 en el gap y el detector se rearma */
    {
        for (size_t i = 0; i < n; i++)
            x[i] = (int32_t)(1000.0 * sin(2.0*M_PI*10.0*(double)i/fs));
        x[2000] = INT_MAX; x[2001] = INT_MAX; x[2002] = INT_MAX;
        ewgui_stalta_run(x, n, fs, sta, lta, cf);
        CHECK(cf[2000] == 0.0 && cf[2001] == 0.0 && cf[2002] == 0.0,
              "gap: CF = 0");
        CHECK(cf[2003] == 0.0 && cf[2200] == 0.0,
              "gap: el detector se rearma (CF=0 hasta llenar la LTA)");
        CHECK(cf[2600] > 0.5, "gap: la CF vuelve tras rearmarse");
    }

    /* 6. off <= on: se corrige a on/2 (no se queda sin rearme) */
    {
        for (size_t i = 0; i < n; i++)
            x[i] = (int32_t)(1000.0 * sin(2.0*M_PI*10.0*(double)i/fs));
        x[3000] = 30000;   /* pico aislado */
        ewgui_stalta_run(x, n, fs, sta, lta, cf);
        size_t idx[8];
        int nt = ewgui_stalta_triggers(cf, n, 3.0, 9.0, idx, 8);
        CHECK(nt >= 1, "off > on: no bloquea el disparo");
    }

    free(x);
    free(cf);

    if (failures == 0) { printf("\nALL STALTA TESTS PASSED\n"); return 0; }
    printf("\n%d STALTA TEST(S) FAILED\n", failures);
    return 1;
}
