/******************************************************************************
 * live_check.c                                                               *
 *                                                                            *
 * Validacion de la cabecera del ARC de salida del refinador y politica de    *
 * N_retry. Sin dependencias del motor ni de la asociacion: lo usan           *
 * hyp2000_ring (vivo) y nlloc_ring (offline/manual).                         *
 ******************************************************************************/
#include "live_assoc.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int live_check_arc(const char *arc, int len, const LiveLocCheckCfg *cfg,
                   LiveLocCheck *out)
{
    char b[8];

    if (!out) return -1;
    memset(out, 0, sizeof(*out));
    out->gap = 0.0;

    if (!arc || len < 52 || arc[0] == '$') {
        snprintf(out->why, sizeof(out->why), "ARC ilegible");
        return -1;
    }

    memcpy(b, arc + 39, 3); b[3] = '\0';
    out->nph = atoi(b);
    memcpy(b, arc + 42, 3); b[3] = '\0';
    out->gap = atof(b);
    memcpy(b, arc + 48, 4); b[4] = '\0';
    out->rms = atof(b) / 100.0;

    /* nph<=0 nunca es una solucion, aunque los filtros esten en 0. */
    if (out->nph <= 0) {
        snprintf(out->why, sizeof(out->why), "nph=%d", out->nph);
        return 0;
    }
    if (cfg && cfg->min_phases > 0 && out->nph < cfg->min_phases) {
        snprintf(out->why, sizeof(out->why), "nph=%d < %d",
                 out->nph, cfg->min_phases);
        return 0;
    }
    /* 99.99 es el centinela de fallo de HYPOINVERSE/NLLoc: no es un RMS
       real, asi que se rechaza incluso con MaxRMS=0. */
    if (!isfinite(out->rms) || out->rms >= 99.0) {
        snprintf(out->why, sizeof(out->why), "rms=%.2f (centinela)", out->rms);
        return 0;
    }
    if (cfg && cfg->max_rms > 0.0 && out->rms > cfg->max_rms) {
        snprintf(out->why, sizeof(out->why), "rms=%.2f > %.2f",
                 out->rms, cfg->max_rms);
        return 0;
    }
    if (cfg && cfg->max_gap_deg > 0.0 && out->gap > cfg->max_gap_deg) {
        snprintf(out->why, sizeof(out->why), "gap=%.0f > %.0f",
                 out->gap, cfg->max_gap_deg);
        return 0;
    }

    out->ok = 1;
    snprintf(out->why, sizeof(out->why), "ok");
    return 0;
}

int live_retry_exhausted(int no_new, int n_retry)
{
    if (n_retry <= 0) return no_new >= 1 ? 1 : 0;
    return no_new >= n_retry ? 1 : 0;
}
