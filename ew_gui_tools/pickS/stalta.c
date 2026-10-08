/******************************************************************************
 * stalta.c — relación STA/LTA recursiva sobre una serie (envolvente).        *
 ******************************************************************************/

#include "pickS.h"

void PickS_StaLta_Init(PickS_StaLta *s, double fs, double stasec, double ltasec)
{
    memset(s, 0, sizeof(*s));
    s->fs     = fs;
    s->stasec = stasec;
    s->ltasec = ltasec;
    s->nsta   = (int)(stasec * fs);
    s->nlta   = (int)(ltasec * fs);
    if (s->nsta < 1) s->nsta = 1;
    if (s->nlta < 1) s->nlta = 1;
    if (s->nlta < s->nsta) s->nlta = s->nsta;

    s->bsta = (double *)calloc((size_t)s->nsta, sizeof(double));
    s->blta = (double *)calloc((size_t)s->nlta, sizeof(double));
}

void PickS_StaLta_Free(PickS_StaLta *s)
{
    free(s->bsta); s->bsta = NULL;
    free(s->blta); s->blta = NULL;
}

/* x debe ser una amplitud no negativa (envolvente). Se usa x^2 (potencia). */
double PickS_StaLta_Update(PickS_StaLta *s, double x)
{
    double p = x * x;

    if (s->bsta) {
        double old = s->bsta[s->ista];
        s->ssta += p - old;
        s->bsta[s->ista] = p;
        s->ista = (s->ista + 1) % s->nsta;
        if (s->csta < s->nsta) s->csta++;
    }
    if (s->blta) {
        double old = s->blta[s->ilta];
        s->slta += p - old;
        s->blta[s->ilta] = p;
        s->ilta = (s->ilta + 1) % s->nlta;
        if (s->clta < s->nlta) s->clta++;
    }

    {
        double sa = s->ssta / (s->csta > 0 ? (double)s->csta : 1.0);
        double la = s->slta / (s->clta > 0 ? (double)s->clta : 1.0);
        s->ratio = sa / (la + 1e-12);
    }
    return s->ratio;
}
