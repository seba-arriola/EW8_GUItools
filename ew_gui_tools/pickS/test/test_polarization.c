/* test_polarization.c — una onda rectilínea horizontal da alta rectilinealidad. */
#include "pickS.h"

int main(void)
{
    int    n = 200, i;
    double fs = 100.0;
    double e[200], nn[200], z[200];
    PickS_Pol pol;

    for (i = 0; i < n; i++) {
        double s = sin(2.0 * 3.14159265358979 * 3.0 * (double)i / fs);
        e[i]  = 2.0 * s;
        nn[i] = 1.0 * s;
        z[i]  = 0.1 * s;
    }
    if (PickS_Pol_Compute(e, nn, z, n, &pol) != 0) {
        printf("FAIL: Pol_Compute error\n"); return 1;
    }
    printf("rect=%.3f plan=%.3f inc=%.1f az=%.1f hv=%.2f\n",
           pol.rectilinearity, pol.planarity, pol.incidence_deg,
           pol.azimuth_deg, pol.hv_ratio);

    if (!(pol.rectilinearity > 0.8)) {
        printf("FAIL: rectilinealidad baja\n"); return 1;
    }
    if (!(pol.incidence_deg > 70.0 && pol.incidence_deg < 110.0)) {
        printf("FAIL: incidencia no horizontal\n"); return 1;
    }
    printf("test_polarization OK\n");
    return 0;
}
