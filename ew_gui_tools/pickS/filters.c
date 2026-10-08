/******************************************************************************
 * filters.c — Butterworth pasa-banda (RBJ) en cascada                       *
 *                                                                            *
 * Se diseñan nsect secciones de 2º orden (bandpass de ganancia constante) y  *
 * se cascadan. Los coeficientes se calculan con la transformada bilineal     *
 * (Cookbook RBJ).                                                            *
 ******************************************************************************/

#include "pickS.h"

#define PICKS_PI 3.14159265358979323846

void PickS_FilterReset(PickS_BiQuad *bq)
{
    int s;
    for (s = 0; s < PICKS_FILT_MAXSECT; s++)
        bq->x1[s] = bq->x2[s] = bq->y1[s] = bq->y2[s] = 0.0;
}

/* Diseña UNA sección de 2º orden pasa-banda con centro f0 y ancho bw_oct. */
static void design_section(double *b0, double *b1, double *b2,
                           double *a1, double *a2,
                           double fs, double f0, double bw_oct)
{
    double w0 = 2.0 * PICKS_PI * f0 / fs;
    double sw = sin(w0);
    double alpha;
    double a0;

    if (sw < 1e-12) sw = 1e-12;
    alpha = sw * sinh(log(2.0) / 2.0 * bw_oct * w0 / sw);
    if (alpha < 1e-12) alpha = 1e-12;

    a0  = 1.0 + alpha;
    *b0 =  alpha / a0;
    *b1 =  0.0;
    *b2 = -alpha / a0;
    *a1 = (-2.0 * cos(w0)) / a0;
    *a2 = (1.0 - alpha) / a0;
}

void PickS_FilterInit(PickS_BiQuad *bq, double fs, double f1, double f2, int order)
{
    double f0, bw_oct;
    int    nsect, i;

    if (f1 < 1e-3) f1 = 1e-3;
    if (f2 > 0.45 * fs) f2 = 0.45 * fs;
    if (f2 <= f1) f2 = f1 * 2.0;

    f0     = sqrt(f1 * f2);
    bw_oct = log(f2 / f1) / log(2.0);
    if (bw_oct < 0.01) bw_oct = 0.01;

    nsect = order / 2;
    if (nsect < 1) nsect = 1;
    if (nsect > PICKS_FILT_MAXSECT) nsect = PICKS_FILT_MAXSECT;

    bq->nsect = nsect;
    for (i = 0; i < nsect; i++) {
        design_section(&bq->b0[i], &bq->b1[i], &bq->b2[i],
                       &bq->a1[i], &bq->a2[i], fs, f0, bw_oct);
    }
    PickS_FilterReset(bq);
}

double PickS_FilterApply(PickS_BiQuad *bq, double x)
{
    int s;
    double y;

    for (s = 0; s < bq->nsect; s++) {
        y = bq->b0[s] * x + bq->b1[s] * bq->x1[s] + bq->b2[s] * bq->x2[s]
          - bq->a1[s] * bq->y1[s] - bq->a2[s] * bq->y2[s];
        bq->x2[s] = bq->x1[s];
        bq->x1[s] = x;
        bq->y2[s] = bq->y1[s];
        bq->y1[s] = y;
        x = y;
    }
    return x;
}
