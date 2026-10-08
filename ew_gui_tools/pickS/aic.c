/******************************************************************************
 * aic.c — refinamiento del onset por Akaike Information Criterion (AIC).     *
 *                                                                            *
 * AIC(k) = k*log(var(v[0..k])) + (N-k-1)*log(var(v[k+1..N-1]))               *
 * El onset es el k que minimiza AIC en una ventana alrededor del disparo.    *
 * Referencia: Maeda (1985); Sleeman & van Eck (1999).                        *
 ******************************************************************************/

#include "pickS.h"

int PickS_Aic_Onset(const double *v, int n, int tguess, int halfwin,
                    double *onset_idx)
{
    double *P, *Q;
    int     k, kmin, kmax, kbest = tguess;
    double  aic, aicmin = 1e300;

    if (n < 4) return -1;
    if (tguess < 0) tguess = 0;
    if (tguess > n - 1) tguess = n - 1;
    if (halfwin < 1) halfwin = n / 4;

    P = (double *)calloc((size_t)n + 1, sizeof(double));
    Q = (double *)calloc((size_t)n + 1, sizeof(double));
    if (!P || !Q) { free(P); free(Q); return -1; }

    for (k = 0; k < n; k++) {
        P[k + 1] = P[k] + v[k];
        Q[k + 1] = Q[k] + v[k] * v[k];
    }

    kmin = tguess - halfwin;
    kmax = tguess + halfwin;
    if (kmin < 1) kmin = 1;
    if (kmax > n - 2) kmax = n - 2;

    for (k = kmin; k <= kmax; k++) {
        int    n1 = k + 1;
        int    n2 = n - k - 1;
        double m1 = P[n1] / n1;
        double m2 = (P[n] - P[n1]) / n2;
        double v1 = Q[n1] / n1 - m1 * m1;
        double v2 = (Q[n] - Q[n1]) / n2 - m2 * m2;

        if (v1 < 1e-12) v1 = 1e-12;
        if (v2 < 1e-12) v2 = 1e-12;
        aic = n1 * log(v1) + n2 * log(v2);
        if (aic < aicmin) { aicmin = aic; kbest = k; }
    }

    free(P); free(Q);
    if (onset_idx) *onset_idx = (double)kbest;
    return 0;
}
