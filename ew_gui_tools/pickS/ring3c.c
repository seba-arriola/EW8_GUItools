/******************************************************************************
 * ring3c.c — buffer 3C sincronizado por estación con filtrado y STA/LTA.      *
 *                                                                            *
 * Las tres componentes se almacenan en arrays lineales indexados por índice  *
 * absoluto de muestra (idx = round((t - t0)*fs)). Al desbordar capacidad se  *
 * desplaza el contenido con memmove y se avanza t0. Se asume que los         *
 * paquetes de una misma componente llegan en orden creciente de tiempo.      *
 ******************************************************************************/

#include "pickS.h"

static void ring_reset(PickS_Ring3C *r)
{
    int c;
    for (c = 0; c < PICKS_MAX_COMP; c++) {
        r->lo[c] = LONG_MAX;
        r->hi[c] = LONG_MIN;
        r->filt_hi[c] = 0;
    }
    r->ntotal = 0;
    r->proc_idx = 0;
}

static void ring_shift(PickS_Ring3C *r, long shift)
{
    int c;
    if (shift <= 0) return;

    if (shift >= r->ntotal) {
        double adv = (r->fs > 0.0) ? (double)r->ntotal / r->fs : 0.0;
        r->t0 += adv;
        ring_reset(r);
        return;
    }

    for (c = 0; c < PICKS_MAX_COMP; c++) {
        memmove(r->buf[c], r->buf[c] + shift,
                (size_t)(r->ntotal - shift) * sizeof(double));
        if (r->lo[c] != LONG_MAX) r->lo[c] -= shift;
        if (r->hi[c] != LONG_MIN) r->hi[c] -= shift;
        r->filt_hi[c] -= shift;
        if (r->filt_hi[c] < 0) r->filt_hi[c] = 0;
    }
    r->ntotal -= shift;
    r->proc_idx -= shift;
    if (r->proc_idx < 0) r->proc_idx = 0;
    r->t0 += (double)shift / r->fs;
}

int PickS_Ring3C_Init(PickS_Ring3C *r, const PickS_Station *st, double fs,
                      double bufsec, double f1, double f2, int order)
{
    int c;

    memset(r, 0, sizeof(*r));
    r->active = 1;
    r->fs = fs;
    snprintf(r->sta, PICKS_SCNL, "%s", st->sta);
    snprintf(r->net, PICKS_SCNL, "%s", st->net);
    snprintf(r->loc, PICKS_SCNL, "%s", st->loc);

    r->cap = (long)(bufsec * fs) + 4;
    if (r->cap < 32) r->cap = 32;

    for (c = 0; c < PICKS_MAX_COMP; c++) {
        r->buf[c] = (double *)calloc((size_t)r->cap, sizeof(double));
        if (!r->buf[c]) {
            PickS_Ring3C_Free(r);
            return -1;
        }
        PickS_FilterInit(&r->filt[c], fs, f1, f2, order);
    }
    ring_reset(r);
    r->inited = 1;
    return 0;
}

void PickS_Ring3C_Free(PickS_Ring3C *r)
{
    int c;
    for (c = 0; c < PICKS_MAX_COMP; c++) {
        free(r->buf[c]);
        r->buf[c] = NULL;
    }
    PickS_StaLta_Free(&r->st);
    r->inited = 0;
    r->active = 0;
}

int PickS_Ring3C_Add(PickS_Ring3C *r, int comp, double starttime, double fs,
                     const int32_t *data, int nsamp)
{
    long idx0, keep;
    int  s, k;

    if (!r->active || !r->inited) return -1;
    if (comp < 0 || comp >= PICKS_MAX_COMP) return -1;
    if (nsamp <= 0) return 0;
    if (fabs(fs - r->fs) > 1e-6) return -1;

    if (!r->started) {
        r->t0 = starttime;
        r->started = 1;
    }

    idx0 = lround((starttime - r->t0) * r->fs);
    if (idx0 < 0) {
        long need = -idx0 + 2;
        ring_shift(r, need);
        idx0 = lround((starttime - r->t0) * r->fs);
        if (idx0 < 0) return -1;
    }

    /* Recorte si solapa con datos ya presentes de esta componente. */
    s = 0;
    if (r->hi[comp] != LONG_MIN && idx0 < r->hi[comp]) {
        long skip = r->hi[comp] - idx0;
        if (skip >= nsamp) return 0;
        s = (int)skip;
        idx0 += skip;
    }

    keep = (long)(nsamp - s);
    if (idx0 + keep > r->cap) {
        long need = idx0 + keep - r->cap;
        ring_shift(r, need);
        idx0 -= need;
        if (idx0 < 0) return -1;
    }

    for (k = s; k < nsamp; k++) {
        long   idx = idx0 + (k - s);
        double v = (double)data[k];
        if (idx >= r->filt_hi[comp]) {
            v = PickS_FilterApply(&r->filt[comp], v);
            r->filt_hi[comp] = idx + 1;
        }
        r->buf[comp][idx] = v;
    }

    if (r->lo[comp] == LONG_MAX || idx0 < r->lo[comp]) r->lo[comp] = idx0;
    if (r->hi[comp] == LONG_MIN || idx0 + keep > r->hi[comp])
        r->hi[comp] = idx0 + keep;
    if (idx0 + keep > r->ntotal) r->ntotal = idx0 + keep;
    r->last_add_t = starttime + (double)(nsamp - 1) / r->fs;
    return 0;
}

int PickS_Ring3C_Window(const PickS_Ring3C *r, long idx, int len,
                        double *e, double *n, double *z)
{
    int c, i;

    if (!r->inited || len <= 0) return -1;
    if (idx < 0 || idx + len > r->ntotal) return -1;
    for (c = 0; c < PICKS_MAX_COMP; c++) {
        if (r->lo[c] == LONG_MAX) return -2;
        if (r->lo[c] > idx || r->hi[c] < idx + len) return -2;
    }
    for (i = 0; i < len; i++) {
        e[i] = r->buf[PICKS_COMP_E][idx + i];
        n[i] = r->buf[PICKS_COMP_N][idx + i];
        z[i] = r->buf[PICKS_COMP_Z][idx + i];
    }
    return 0;
}
