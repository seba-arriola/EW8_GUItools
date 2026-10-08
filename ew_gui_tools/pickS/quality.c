/******************************************************************************
 * quality.c — métricas de calidad -> weight 0-4 (0 = mejor).                 *
 *                                                                            *
 * Cada métrica se puntúa 0-4 y el peso final es el peor de ellos (criterio   *
 * conservador). Umbrales configurables desde pickS.d.                        *
 ******************************************************************************/

#include "pickS.h"

/* Métrica donde "más es mejor": w0 en x>=x_w0, w4 en x<=x_w4. */
static double grade_high_better(double x, double x_w0, double x_w4)
{
    if (x >= x_w0) return 0.0;
    if (x <= x_w4) return 4.0;
    if (fabs(x_w0 - x_w4) < 1e-12) return 0.0;
    return 4.0 * (x_w0 - x) / (x_w0 - x_w4);
}

/* Incidencia: 0 dentro de [min,max]; degrada con la desviación. */
static double grade_incidence(double inc, double inc_min, double inc_max)
{
    double dev;
    if (inc >= inc_min && inc <= inc_max) return 0.0;
    dev = (inc < inc_min) ? (inc_min - inc) : (inc - inc_max);
    {
        double g = 1.0 + dev / 15.0;
        return (g > 4.0) ? 4.0 : g;
    }
}

double PickS_Quality_Weight(const PickS_Params *p, const PickS_Metrics *m,
                            int *weight_out)
{
    double g_snr, g_sta, g_rect, g_inc, w;

    g_snr  = grade_high_better(m->snr,           p->Q_Snr_W0,    p->Q_Snr_W4);
    g_sta  = grade_high_better(m->stalta_peak,   p->Q_StaLta_W0, p->Q_StaLta_W4);
    g_rect = grade_high_better(m->rectilinearity, p->Q_Rect_W0,  p->Q_Rect_W4);
    g_inc  = grade_incidence(m->incidence_deg, p->Q_IncidMinDeg, p->Q_IncidMaxDeg);

    w = g_snr;
    if (g_sta  > w) w = g_sta;
    if (g_rect > w) w = g_rect;
    if (g_inc  > w) w = g_inc;

    /* Métricas opcionales (deshabilitadas si W0 <= 0): no alteran el
     * comportamiento por defecto, pero permiten calibrarlas. */
    if (p->Q_Plan_W0 > 0.0) {
        double g_plan = grade_high_better(m->planarity, p->Q_Plan_W0, p->Q_Plan_W4);
        if (g_plan > w) w = g_plan;
    }
    if (p->Q_Hv_W0 > 0.0) {
        double g_hv = grade_high_better(m->hv_ratio, p->Q_Hv_W0, p->Q_Hv_W4);
        if (g_hv > w) w = g_hv;
    }

    if (w < 0.0) w = 0.0;
    if (w > 4.0) w = 4.0;

    if (weight_out) *weight_out = (int)(w + 0.5);
    return w;
}
