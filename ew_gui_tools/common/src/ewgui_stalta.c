#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "ewgui/stalta.h"

static size_t window_samples(double fs, double secs, size_t min)
{
    double n = secs * fs;
    if (!(n > 0.0))
        return min;
    if (n < (double)min)
        return min;
    if (n > 5000000.0)
        return 5000000u;   /* tope duro: 40 MB de buffer, no un calloc de GB */
    return (size_t)(n + 0.5);
}

void ewgui_stalta_init(EwGuiStaLta *s, double fs, double sta_s, double lta_s)
{
    if (!s)
        return;
    memset(s, 0, sizeof(*s));
    if (!(fs > 0.0))
        fs = 100.0;
    s->fs = fs;
    s->nsta = window_samples(fs, sta_s, 1);
    s->nlta = window_samples(fs, lta_s, s->nsta);
    if (s->nlta < s->nsta)
        s->nlta = s->nsta;
    s->buf = calloc(s->nlta, sizeof(double));
    if (!s->buf) {
        s->nsta = s->nlta = 0;
        return;
    }
}

void ewgui_stalta_free(EwGuiStaLta *s)
{
    if (!s)
        return;
    free(s->buf);
    memset(s, 0, sizeof(*s));
}

static void stalta_reset(EwGuiStaLta *s)
{
    if (!s->buf)
        return;
    memset(s->buf, 0, s->nlta * sizeof(double));
    s->pos = 0;
    s->count = 0;
    s->sum_sta = 0.0;
    s->sum_lta = 0.0;
}

double ewgui_stalta_update(EwGuiStaLta *s, double x)
{
    if (!s || !s->buf || s->nsta == 0 || s->nlta == 0)
        return 0.0;

    double p = x * x;
    if (!(p >= 0.0) || p > 1e300)
        p = 0.0;

    size_t i = s->pos;

    /* Antes de sobrescribir: el que sale de la STA (si ya está llena) y el
     * que sale de la LTA (solo cuando el ring está completo). */
    if (s->count >= s->nsta)
        s->sum_sta -= s->buf[(i + s->nlta - s->nsta) % s->nlta];
    if (s->count >= s->nlta)
        s->sum_lta -= s->buf[i];

    s->buf[i] = p;
    s->sum_sta += p;
    s->sum_lta += p;

    if (s->count < s->nlta)
        s->count++;
    s->pos = (i + 1) % s->nlta;

    if (s->count < s->nlta || s->sum_lta <= 0.0)
        return 0.0;

    double msta = s->sum_sta / (double)s->nsta;
    double mlta = s->sum_lta / (double)s->nlta;
    if (!(mlta > 0.0))
        return 0.0;
    return msta / mlta;
}

void ewgui_stalta_run(const int32_t *x, size_t n, double fs,
                      double sta_s, double lta_s, double *cf)
{
    if (!x || !cf || n == 0)
        return;

    EwGuiStaLta s;
    ewgui_stalta_init(&s, fs, sta_s, lta_s);
    if (!s.buf) {
        memset(cf, 0, n * sizeof(double));
        return;
    }

    for (size_t i = 0; i < n; i++) {
        if (x[i] == INT_MAX) {
            stalta_reset(&s);
            cf[i] = 0.0;
            continue;
        }
        cf[i] = ewgui_stalta_update(&s, (double)x[i]);
    }
    ewgui_stalta_free(&s);
}

int ewgui_stalta_triggers(const double *cf, size_t n,
                          double on, double off, size_t *idx, int max)
{
    if (!cf || !idx || n == 0 || max <= 0 || !(on > 0.0))
        return 0;
    if (!(off >= 0.0) || off > on)
        off = on * 0.5;

    int nfound = 0;
    int armed = 1;

    for (size_t i = 0; i < n && nfound < max; i++) {
        if (armed) {
            if (cf[i] >= on) {
                idx[nfound++] = i;
                armed = 0;
            }
        } else if (cf[i] < off) {
            armed = 1;
        }
    }
    return nfound;
}
