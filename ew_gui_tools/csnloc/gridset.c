/******************************************************************************
 * gridset.c                                                                  *
 *                                                                            *
 * Conjunto de grillas anidadas y regla de activacion.                        *
 *                                                                            *
 * Carga: por cada clave GlobalGrid/RegionalGrid/LocalGrid del .d se abre un   *
 * archivo .grid, se construyen sus ejes y se precomputan las estaciones       *
 * asociadas a cada nodo.                                                     *
 *                                                                            *
 * Activacion (por ventana): las grillas gruesas (global/regional) se buscan   *
 * siempre; una grilla local se activa si (a) hay al menos                     *
 * GridActivationMinPicks picks de estaciones asociadas a ella, o (b) alguna   *
 * nucleacion de un nivel mas grueso cae dentro de su bbox + margen.           *
 ******************************************************************************/
#include "csnloc.h"

#ifndef RAD
#define RAD 0.017453292519943
#endif

/* Geocentrica -> geografica (inversa de la usada en grid.c). */
static double geoc_to_geo(double lat_geoc)
{
    double g = lat_geoc * RAD;
    return atan(tan(g) / 0.993277) / RAD;
}

int GridSet_Load(GridSet *gs, const CSLocParams *cfg, const StationList *st)
{
    int i;

    if (!gs || !cfg) return -1;
    memset(gs, 0, sizeof(*gs));
    if (cfg->n_gridfiles <= 0) return -1;

    for (i = 0; i < cfg->n_gridfiles && gs->n < CSLOC_MAX_GRIDS; i++) {
        Grid *g = &gs->g[gs->n];
        int   rc = Grid_LoadFile(cfg->GridFiles[i], cfg->GridFileLevel[i], g);
        if (rc != 0) return rc;
        rc = Grid_PrecomputeStations(g, st);
        if (rc != 0) return rc;
        gs->n++;
    }

    return (gs->n > 0) ? 0 : -1;
}

void GridSet_Free(GridSet *gs)
{
    int i;
    if (!gs) return;
    for (i = 0; i < gs->n; i++) Grid_Free(&gs->g[i]);
    gs->n = 0;
}

int Grid_IsActive(const Grid *g, const Pick *picks, const int *pick_sidx,
                  int npick, const StationList *st, const CSLocParams *cfg,
                  const HypoCandidate *nuc, int nnuc)
{
    int p, n_assoc = 0;

    if (!g) return 0;
    (void)picks; (void)st;

    /* Las grillas gruesas se buscan siempre. */
    if (g->level <= GRID_LEVEL_REGIONAL) return 1;

    /* (a) cobertura de estaciones asociadas a la grilla. */
    if (g->sta_mask) {
        for (p = 0; p < npick; p++) {
            int s = pick_sidx ? pick_sidx[p] : -1;
            if (s >= 0 && s < g->n_sta && g->sta_mask[s]) n_assoc++;
        }
    }
    if (getenv("CSNLOC_DEBUG"))
        fprintf(stderr, "[dbg] grid '%s' level=%d n_assoc=%d\n",
                g->name, g->level, n_assoc);
    if (n_assoc >= cfg->GridActivationMinPicks) return 1;

    /* (b) cercania a una nucleacion de un nivel mas grueso. */
    if (nuc && nnuc > 0) {
        int i;
        for (i = 0; i < nnuc; i++) {
            double lat_geo = geoc_to_geo(nuc[i].lat);
            if (Grid_ContainsLL(g, lat_geo, nuc[i].lon,
                                cfg->GridActivationMarginDeg))
                return 1;
        }
    }

    return 0;
}
