#include "csnmags.h"
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <assert.h>

/* test_formula.c - fórmulas puras y utilidades. */

static int approx(double a, double b, double tol) { return fabs(a - b) <= tol; }

int main(void)
{
    Tab1D q; int i;
    /* Tabla Q(Delta) = 0 para todo Delta (mb = log10(A/T) - 3) */
    q.n = 3; q.x[0] = 0; q.y[0] = 0; q.x[1] = 100; q.y[1] = 0; q.x[2] = 180; q.y[2] = 0;

    /* ML: A=1000 nm, dist=22.3 km -> log10(1000)+1.11*log10(22.3)+... */
    {
        double r = 22.3;
        double want = 3.0 + 1.11 * log10(r) + 0.00189 * r - 2.09;
        double got = mag_ml(1000.0, r, NULL, 1.11, 0.00189, -2.09);
        assert(approx(got, want, 1e-9));
        printf("ML ok: %.3f\n", got);
    }
    /* Mwp: con un M0 conocido */
    {
        double want_m0 = 1.0e19;
        double rho = 2700.0, alpha = 6000.0, fp = 0.6, r_km = 100.0;
        /* int_disp = M0*fp/(4*pi*rho*alpha^3*R) en m*s */
        double r_m = r_km * 1000.0;
        double disp_m = want_m0 * fp / (4.0 * M_PI * rho * alpha * alpha * alpha * r_m);
        double got = mag_mwp(disp_m * 1e9, r_km, 0.9, rho, alpha, fp);
        double want = (log10(want_m0) - 9.1) / 1.5;
        assert(approx(got, want, 1e-6));
        printf("Mwp ok: %.3f\n", got);
    }
    /* Mb: A=100 nm, T=1 s, Q=0 -> 2-3 = -1 */
    {
        double got = mag_mb(100.0, 1.0, 30.0, &q);
        assert(approx(got, log10(100.0) - 3.0, 1e-9));
        printf("Mb ok: %.3f\n", got);
    }
    /* Ms: A=1000 nm, T=20 s, Delta=60 -> log10(50)+1.66*log10(60)+0.3 */
    {
        double got = mag_ms(1000.0, 20.0, 60.0, 0, 1.66, 0.3);
        double want = log10(50.0) + 1.66 * log10(60.0) + 0.3;
        assert(approx(got, want, 1e-9));
        printf("Ms ok: %.3f\n", got);
    }
    /* Agregación: media limpia y mediana robusta a un outlier */
    {
        double v[5] = {4.5, 4.6, 4.4, 4.5, 4.5};
        double sig; int used;
        double m = mag_net(v, 5, 0, 2.0, &sig, &used);
        assert(used == 5);
        assert(m > 4.4 && m < 4.6);
        printf("net(mean) ok: %.3f (n=%d, sigma=%.3f)\n", m, used, sig);
    }
    {
        double v[5] = {4.5, 4.6, 4.4, 4.5, 9.0};
        double sig; int used;
        double m = mag_net(v, 5, 1, 2.0, &sig, &used);
        assert(used == 5);
        assert(m > 4.4 && m < 4.7);
        printf("net(median) ok: %.3f (n=%d)\n", m, used);
    }
    /* Geometría y tiempo */
    {
        double d = mag_great_circle_deg(0, 0, 0, 1);
        assert(approx(d, 1.0, 1e-6));
        assert(approx(mag_epi_km(1.0), 111.1949, 1e-3));
        assert(approx(mag_ymdhms_epoch(1970, 1, 1, 0, 0, 0.0), 0.0, 1e-6));
        assert(approx(mag_ymdhms_epoch(2020, 1, 1, 0, 0, 0.0), 1577836800.0, 1e-3));
    }
    (void)i;
    printf("test_formula OK\n");
    return 0;
}
