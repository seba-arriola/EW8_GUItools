/*
 * Tests del procesado de la traza de VISTA de magnitudes (ver ewgui/dsp.h):
 * simulacion Wood-Anderson (ML), integracion (Mwp) y decimacion (Ms/Mwp).
 */
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "ewgui/dsp.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int failures = 0;
#define CHECK(cond, name) do { \
    if (cond) { printf("  [PASS] %s\n", name); } \
    else      { printf("  [FAIL] %s\n", name); failures++; } \
} while (0)

static void test_wood_anderson(void)
{
    const long N = 4000;
    const double fs = 100.0, period = 0.8, damp = 0.7, gain = 2080.0;
    int32_t *d = malloc((size_t)N * sizeof(*d));
    double amp = 100.0, pk = 0.0, want;

    if (!d) { CHECK(0, "malloc"); return; }

    /* Seno en la frecuencia natural (1/period): en regimen la ganancia vale
       |H| = gain/(2*damp) (2 ceros en el origen). */
    for (long i = 0; i < N; i++)
        d[i] = (int32_t)(amp * sin(2.0 * M_PI * (double)i / (period * fs)));
    ewgui_filter_wood_anderson(d, (size_t)N, fs, period, damp, gain);
    for (long i = N / 2; i < N; i++) {
        double v = fabs((double)d[i]);
        if (v > pk) pk = v;
    }
    want = amp * gain / (2.0 * damp);
    CHECK(fabs(pk - want) / want < 0.15,
          "WA: el pico en la resonancia es ~ gain/(2*damp)");

    /* La continua se bloquea (2 ceros en el origen). */
    for (long i = 0; i < N; i++) d[i] = 1000;
    ewgui_filter_wood_anderson(d, (size_t)N, fs, period, damp, gain);
    CHECK(fabs((double)d[N - 1]) < 2.0, "WA: la continua se bloquea");

    /* El gap se conserva y reinicia el estado. */
    for (long i = 0; i < 20; i++) d[i] = 500;
    d[5] = INT32_MAX;
    ewgui_filter_wood_anderson(d, 20, fs, period, damp, gain);
    CHECK(d[5] == INT32_MAX, "WA: el gap se conserva");

    /* Sin frecuencia valida no toca nada. */
    for (long i = 0; i < 20; i++) d[i] = 500;
    ewgui_filter_wood_anderson(d, 20, 0.0, period, damp, gain);
    CHECK(d[19] == 500, "WA: fs invalida deja la traza igual");
    ewgui_filter_wood_anderson(NULL, 20, fs, period, damp, gain);
    ewgui_filter_wood_anderson(d, 0, fs, period, damp, gain);
    CHECK(1, "WA: NULL y n=0 no revientan");

    free(d);
}

static void test_integrate(void)
{
    int32_t in[6] = { 100, 100, 100, 100, 100, 100 };
    double out[6];

    /* Velocidad constante -> rampa lineal: v*(n-1)*dt. */
    ewgui_integrate(in, 6, 0.5, out);
    CHECK(fabs(out[0]) < 1e-12, "integrar: arranca en 0");
    CHECK(fabs(out[5] - 100.0 * 5.0 * 0.5) < 1e-9, "integrar: rampa lineal");

    /* dt <= 0 se trata como 1. */
    ewgui_integrate(in, 6, 0.0, out);
    CHECK(fabs(out[5] - 500.0) < 1e-9, "integrar: dt invalido -> 1");

    /* Gap: NaN en esa muestra y en la del par que lo toca; despues sigue. */
    in[2] = INT32_MAX;
    ewgui_integrate(in, 6, 1.0, out);
    CHECK(isnan(out[2]), "integrar: el gap deja NaN");
    CHECK(isnan(out[3]), "integrar: el par que toca el gap tambien");
    CHECK(!isnan(out[4]) && fabs(out[4] - 200.0) < 1e-9,
          "integrar: tras el gap el acumulado sigue");

    ewgui_integrate(NULL, 6, 1.0, out);
    ewgui_integrate(in, 6, 1.0, NULL);
    ewgui_integrate(in, 0, 1.0, out);
    CHECK(1, "integrar: NULL y n=0 no revientan");
}

static void test_decimate(void)
{
    int32_t in[100], out[100];
    size_t n;

    for (int i = 0; i < 100; i++) in[i] = i;
    n = ewgui_decimate(in, 100, 10, out);
    CHECK(n == 10, "diezmar: 100/10 = 10 muestras");
    CHECK(out[0] == 0 && out[3] == 30 && out[9] == 90,
          "diezmar: toma 1 de cada 10");
    n = ewgui_decimate(in, 100, 1, out);
    CHECK(n == 100 && out[99] == 99, "diezmar: factor 1 copia");
    n = ewgui_decimate(in, 100, 0, out);
    CHECK(n == 100, "diezmar: factor 0 se trata como 1");
    n = ewgui_decimate(in, 100, -3, out);
    CHECK(n == 100, "diezmar: factor negativo se trata como 1");

    in[7] = INT32_MAX;
    n = ewgui_decimate(in, 100, 1, out);
    CHECK(n == 100 && out[7] == INT32_MAX, "diezmar: conserva los gaps");

    CHECK(ewgui_decimate(in, 0, 4, out) == 0, "diezmar: n=0 -> 0");
    CHECK(ewgui_decimate(NULL, 100, 4, out) == 0, "diezmar: entrada NULL -> 0");
    CHECK(ewgui_decimate(in, 100, 4, NULL) == 0, "diezmar: salida NULL -> 0");
}

int main(void)
{
    test_wood_anderson();
    test_integrate();
    test_decimate();
    if (failures) { printf("%d FALLOS\n", failures); return 1; }
    printf("OK test_dsp_mag\n");
    return 0;
}
