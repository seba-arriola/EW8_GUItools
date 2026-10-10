/******************************************************************************
 * csnhypodbp_tt.c                                                            *
 *                                                                            *
 * Implementacion de la envoltura del motor IASP91 de csnloc. Ver .h.          *
 ******************************************************************************/
#include <stdio.h>
#include <unistd.h>

#include "csnloc.h"

#include "csnhypodbp_tt.h"

static TTModel g_tt;
static int     g_ready = 0;

int csnhypodbp_tt_init(const char *model)
{
    char hed[128], tbl[128];

    g_ready = 0;
    if (!model || !model[0])
        model = "iasp91";

    /* tabin() aborta con exit(1) si no encuentra <model>.hed/.tbl en el cwd,
     * asi que comprobamos antes: sin tablas se degrada (teorica = otime). */
    snprintf(hed, sizeof(hed), "%s.hed", model);
    snprintf(tbl, sizeof(tbl), "%s.tbl", model);
    if (access(hed, R_OK) != 0 || access(tbl, R_OK) != 0)
        return -1;

    if (TTModel_Init(&g_tt, ".", model, 0.0, 180.0, 0.5) != 0)
        return -1;
    g_ready = 1;
    return 0;
}

int csnhypodbp_tt_ready(void)
{
    return g_ready;
}

double csnhypodbp_tt_p(double delta_deg, double depth_km)
{
    double t = 0.0, dtdd = 0.0;
    if (!g_ready)
        return -1.0;
    if (TTModel_Predict(&g_tt, delta_deg, depth_km, CSLOC_PHASE_P, &t, &dtdd) != 0)
        return -1.0;
    return t;
}
