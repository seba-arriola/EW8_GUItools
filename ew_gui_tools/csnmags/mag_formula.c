#include "csnmags.h"

#include <math.h>
#include <string.h>

/*
 * mag_formula.c - Fórmulas puras (sin EarthWorm) y utilidades geométricas.
 * Todas testeables headless.
 *
 * Unidades:
 *   amp_nm        amplitud de desplazamiento en nanómetros
 *   period_s      periodo en segundos
 *   dist_km       distancia (hipo o epi según Ml_DistType)
 *   delta_deg     distancia epicentral en grados
 *   int_disp_nm_s integral de desplazamiento vertical en nm*s
 */

/* ------------------------------ Tablas 1D ------------------------------ */

int tab1d_load(const char *path, Tab1D *t)
{
    FILE *f;
    char  line[256];
    t->n = 0;
    f = fopen(path, "r");
    if (!f) return -1;
    while (fgets(line, sizeof(line), f)) {
        double x, y;
        if (line[0] == '#' || line[0] == '\n') continue;
        if (sscanf(line, "%lf %lf", &x, &y) != 2) continue;
        if (t->n >= 256) break;
        t->x[t->n] = x; t->y[t->n] = y; t->n++;
    }
    fclose(f);
    return t->n;
}

double tab1d_interp(const Tab1D *t, double x)
{
    int i;
    if (!t || t->n == 0) return 0.0;
    if (t->n == 1 || x <= t->x[0]) return t->y[0];
    if (x >= t->x[t->n - 1]) return t->y[t->n - 1];
    for (i = 1; i < t->n; i++) {
        if (x <= t->x[i]) {
            double dx = t->x[i] - t->x[i - 1];
            if (dx <= 0.0) return t->y[i];
            return t->y[i - 1] + (t->y[i] - t->y[i - 1]) * (x - t->x[i - 1]) / dx;
        }
    }
    return t->y[t->n - 1];
}

/* ------------------------------ ML ------------------------------ */

double mag_ml(double amp_nm, double dist_km, const Tab1D *loga0,
              double c1, double c2, double c3)
{
    double corr;
    if (amp_nm <= 0.0) return 0.0;
    if (dist_km <= 0.0) dist_km = 1.0;
    if (loga0 && loga0->n > 0) corr = tab1d_interp(loga0, dist_km);
    else                       corr = c1 * log10(dist_km) + c2 * dist_km + c3;
    return log10(amp_nm) + corr;
}

/* ------------------------------ Mwp ------------------------------ */

double mag_mwp(double int_disp_nm_s, double dist_km, double delta_deg,
               double rho, double alpha, double fp)
{
    double r_m, disp_m, m0;
    (void)delta_deg;
    if (int_disp_nm_s <= 0.0 || dist_km <= 0.0 || fp <= 0.0) return 0.0;
    r_m    = dist_km * 1000.0;
    disp_m = fabs(int_disp_nm_s) * 1.0e-9;         /* nm*s -> m*s */
    m0     = 4.0 * M_PI * rho * alpha * alpha * alpha * r_m * disp_m / fp;
    if (m0 <= 0.0) return 0.0;
    return (log10(m0) - 9.1) / 1.5;
}

/* ------------------------------ Mb ------------------------------ */

double mag_mb(double amp_nm, double period_s, double delta_deg, const Tab1D *qtab)
{
    double q;
    if (amp_nm <= 0.0 || period_s <= 0.0) return 0.0;
    if (qtab && qtab->n > 0) q = tab1d_interp(qtab, delta_deg);
    else                     return 0.0;   /* sin tabla Q no se calcula */
    return log10(amp_nm / period_s) + q - 3.0;
}

/* ------------------------------ Ms ------------------------------ */

double mag_ms(double amp_nm, double period_s, double delta_deg, int variant,
              double k1, double k2)
{
    (void)variant;
    if (amp_nm <= 0.0 || period_s <= 0.0 || delta_deg <= 0.0) return 0.0;
    return log10(amp_nm / period_s) + k1 * log10(delta_deg) + k2;
}

/* ------------------------------ Agregación de red ------------------------------ */

static double median_of(double *a, int n)
{
    /* ordena una copia pequeña (n <= MAG_MAX_STA) */
    double tmp[MAG_MAX_STA];
    int i, j;
    if (n <= 0) return 0.0;
    if (n > MAG_MAX_STA) n = MAG_MAX_STA;
    memcpy(tmp, a, (size_t)n * sizeof(double));
    for (i = 1; i < n; i++) {
        double v = tmp[i];
        for (j = i - 1; j >= 0 && tmp[j] > v; j--) tmp[j + 1] = tmp[j];
        tmp[j + 1] = v;
    }
    return (n % 2) ? tmp[n / 2] : 0.5 * (tmp[n / 2 - 1] + tmp[n / 2]);
}

double mag_net(const double *per_sta, int n, int use_median, double trunc_k,
               double *sigma_out, int *n_used_out)
{
    double sum = 0.0, mean, sd = 0.0, sum2 = 0.0;
    int    i, used = 0;

    if (sigma_out)  *sigma_out = 0.0;
    if (n_used_out) *n_used_out = 0;
    if (n <= 0) return 0.0;

    if (use_median) {
        mean = median_of((double *)per_sta, n);
        for (i = 0; i < n; i++) sd += (per_sta[i] - mean) * (per_sta[i] - mean);
        if (sigma_out) *sigma_out = (n > 1) ? sqrt(sd / n) : 0.0;
        if (n_used_out) *n_used_out = n;
        return mean;
    }

    for (i = 0; i < n; i++) sum += per_sta[i];
    mean = sum / n;
    if (n > 2) {
        for (i = 0; i < n; i++) sd += (per_sta[i] - mean) * (per_sta[i] - mean);
        sd = sqrt(sd / n);
        sum2 = 0.0;
        for (i = 0; i < n; i++) {
            if (trunc_k <= 0.0 || fabs(per_sta[i] - mean) <= trunc_k * sd) {
                sum2 += per_sta[i]; used++;
            }
        }
        if (used >= 2) {
            if (sigma_out) *sigma_out = sd;
            if (n_used_out) *n_used_out = used;
            return sum2 / used;
        }
    }
    if (sigma_out) *sigma_out = 0.0;
    if (n_used_out) *n_used_out = n;
    return mean;
}

/* ------------------------------ Geometría ------------------------------ */

double mag_great_circle_deg(double lat1, double lon1, double lat2, double lon2)
{
    double p1 = lat1 * M_PI / 180.0, p2 = lat2 * M_PI / 180.0;
    double dl = (lon2 - lon1) * M_PI / 180.0;
    double c = sin(p1) * sin(p2) + cos(p1) * cos(p2) * cos(dl);
    if (c > 1.0) c = 1.0;
    if (c < -1.0) c = -1.0;
    return acos(c) * 180.0 / M_PI;
}

double mag_epi_km(double delta_deg) { return delta_deg * 111.1949; }

double mag_hypo_km(double epi_km, double depth_km)
{
    return sqrt(epi_km * epi_km + depth_km * depth_km);
}

/* ------------------------------ Tiempo ------------------------------ */

double mag_ymdhms_epoch(int y, int m, int d, int hh, int mi, double ss)
{
    long long yy = y;
    unsigned long long era, yoe, doy, doe, days, secs;
    if (m <= 2) yy -= 1;
    era = (yy >= 0 ? (unsigned long long)yy : (unsigned long long)(yy - 399)) / 400;
    yoe = (unsigned long long)(yy - (long long)(era * 400));
    doy = (153ULL * (unsigned long long)(m + (m > 2 ? -3 : 9)) + 2ULL) / 5ULL
          + (unsigned long long)(d - 1);
    doe = yoe * 365ULL + yoe / 4ULL - yoe / 100ULL + doy;
    days = (long long)(era * 146097ULL + doe) - 719468LL;
    secs = (unsigned long long)((long long)(hh * 3600 + mi * 60) + (long long)ss);
    return (double)((long long)days * 86400LL + (long long)secs);
}

const char *mag_magtype_name(int imagtype)
{
    switch (imagtype) {
        case 1:  return "ML";
        case 3:  return "Mb";
        case 4:  return "Ms";
        case 5:  return "Mwp";
        default: return "?";
    }
}
