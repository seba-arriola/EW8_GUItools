/******************************************************************************
 * pickml_post.c — post-proceso en C de la salida del grafo.                  *
 *                                                                            *
 * El grafo ONNX devuelve una curva de probabilidad P/S/detección por muestra. *
 * Lo que NO está en el grafo y se hace aquí: umbral, selección del pico,      *
 * ventana de validez (MinPosSec/MaxPosSec) y deduplicación entre ventanas.    *
 *                                                                            *
 * Política de dedup: la PRIMERA emisión gana. Una vez publicado un pick no se *
 * puede retirar del anillo, así que "mejorar" el pico en una ventana posterior *
 * produciría dos picks contradictorios; se descarta el segundo.              *
 ******************************************************************************/

#include "pickml.h"

int pm_post_peaks(const float *curve, long n, double t0, double rate,
                  double thr, double minpos, double maxpos, char phase,
                  PmPick *out, int cap)
{
    long i, i0, i1, best;
    int  k = 0;

    if (!curve || n <= 0 || rate <= 0.0 || !out || cap <= 0) return 0;

    i0 = (long)ceil(minpos * rate);
    i1 = (maxpos > 0.0) ? (long)floor(maxpos * rate) : n - 1;
    if (i0 < 0) i0 = 0;
    if (i1 >= n) i1 = n - 1;
    if (i0 > i1) return 0;

    i = i0;
    while (i <= i1 && k < cap) {
        if (curve[i] < (float)thr) { i++; continue; }

        /* Región contigua por encima del umbral: se emite su máximo. */
        best = i;
        while (i <= i1 && curve[i] >= (float)thr) {
            if (curve[i] > curve[best]) best = i;
            i++;
        }
        out[k].t     = t0 + (double)best / rate;
        out[k].phase = phase;
        out[k].prob  = curve[best];
        k++;
    }
    return k;
}

int pm_prob_weight(double prob, const double wprob[4], int *weight)
{
    int i;
    if (!wprob) return 0;
    /* La curva es float32: se compara en esa precisión para que un 0.90f no
     * quede por debajo de un umbral 0.90 escrito en decimal. */
    for (i = 0; i < 4; i++) {
        if (prob >= (double)(float)wprob[i]) {
            if (weight) *weight = i;
            return 1;
        }
    }
    return 0;
}

int pm_dedup_accept(PmDedup *d, double t, char phase, double dedup_sec)
{
    int i;

    if (!d) return 1;
    if (dedup_sec <= 0.0) return 1;

    for (i = 0; i < d->n && i < PM_DEDUP_SLOTS; i++) {
        if (d->phase[i] != phase) continue;
        if (fabs(d->t[i] - t) <= dedup_sec) return 0;
    }

    if (d->n < PM_DEDUP_SLOTS) {
        i = d->n++;
    } else {
        /* Anillo lleno: se pisa el más viejo (solo importan los recientes). */
        i = 0;
        for (int j = 1; j < PM_DEDUP_SLOTS; j++)
            if (d->t[j] < d->t[i]) i = j;
    }
    d->t[i]     = t;
    d->phase[i] = phase;
    return 1;
}

/* Un mismo arribo suele producir un pico S inmediatamente después del P (y el
 * arribo siguiente aparece etiquetado como P). Un S a menos de `minsp` de un P
 * de la misma estación y ventana es, en la práctica, ese mismo arribo mal
 * etiquetado: se descarta. Coherente con pickS (GuideMinDtSec = 2.0 s).
 * Medido en el sismo de CX.PB15: S-P espurias de 0.32-0.65 s en todas las
 * estaciones, mientras el arribo siguiente llegaba ~16 s después. */
int pm_gate_s_vs_p(PmPick *sp, int ns, const PmPick *pp, int np, double minsp)
{
    int i, k, keep = 0;

    if (!sp || ns <= 0) return 0;
    if (!pp || np <= 0 || minsp <= 0.0) return ns;

    for (i = 0; i < ns; i++) {
        int drop = 0;
        for (k = 0; k < np; k++) {
            double dt = sp[i].t - pp[k].t;
            if (dt >= 0.0 && dt < minsp) { drop = 1; break; }
        }
        if (!drop) {
            if (keep != i) sp[keep] = sp[i];
            keep++;
        }
    }
    return keep;
}
