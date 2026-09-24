/******************************************************************************
 * grid.c                                                                     *
 *                                                                            *
 * Grillas de busqueda 3D (lat, lon, depth) para la back-projection.           *
 *                                                                            *
 * Cada grilla se declara en un archivo de texto clave-valor (estilo .d):      *
 *                                                                            *
 *   Name                Tarapaca                                             *
 *   LatMin              -21.653                                             *
 *   LatMax              -16.347                                             *
 *   LonMin              -71.593                                             *
 *   LonMax              -67.408                                             *
 *   NodeKm              10.0                                                *
 *   DepthLayers         5,10,20,...,300      (o DepthMin/Max/Step)          *
 *   StaMaxDistKm        1200.0                                              *
 *   NumStationsPerNode  30                                                  *
 *   MinPhases           4                                                   *
 *   BackProjThreshold   0.0                                                 *
 *   MaxNodes            200000                                              *
 *                                                                            *
 * La resolucion horizontal se da en km y se convierte a grados; la longitud  *
 * se corrige por cos(lat). El precómputo asocia a cada nodo las estaciones    *
 * cercanas (radio StaMaxDistKm, tope NumStationsPerNode) en una estructura    *
 * CSR, para que la back-projection no recorra todas las estaciones por nodo.  *
 ******************************************************************************/
#include "csnloc.h"

#ifndef RAD
#define RAD 0.017453292519943
#endif

#define KM_PER_DEG 111.195

static double geocentric_lat_deg(double lat_deg)
{
    double l = lat_deg * RAD;
    double g = atan(0.993277 * tan(l));
    return g / RAD;
}

/* ------------------------------------------------------------------------- */
/* Parser de la lista de profundidades "5,10,20,...". Normaliza: descarta     */
/* negativos, ordena ascendente y elimina duplicados.                         */
/* ------------------------------------------------------------------------- */
static void parse_depth_layers(const char *s, Grid *g)
{
    char  buf[CSLOC_STR * 4];
    char *p, *tok;
    int   n = 0;
    double v[CSLOC_MAX_GRID_DEPTHS];

    if (!s || !g) return;
    snprintf(buf, sizeof(buf), "%s", s);

    tok = buf;
    while (tok && *tok && n < CSLOC_MAX_GRID_DEPTHS) {
        p = strchr(tok, ',');
        if (p) *p = '\0';
        {
            double d = atof(tok);
            if (d >= 0.0) v[n++] = d;
        }
        tok = p ? p + 1 : NULL;
    }

    /* orden ascendente (insercion) */
    {
        int i, j;
        for (i = 1; i < n; i++) {
            double key = v[i];
            j = i - 1;
            while (j >= 0 && v[j] > key) { v[j + 1] = v[j]; j--; }
            v[j + 1] = key;
        }
    }
    /* dedupe */
    {
        int i, m = 0;
        for (i = 0; i < n; i++) {
            if (m == 0 || v[i] != v[m - 1]) v[m++] = v[i];
        }
        n = m;
    }
    {
        int i;
        for (i = 0; i < n; i++) g->depth_layers[i] = v[i];
    }
    g->n_depth_layers = n;
}

/* ------------------------------------------------------------------------- */
/* Construye los ejes lat/lon/depth a partir de la especificacion.            */
/* Devuelve 0, -1 (spec invalida) o -2 (excede MaxNodes).                     */
/* ------------------------------------------------------------------------- */
int Grid_BuildAxes(Grid *g)
{
    double lat_step, lon_step, clat;
    int    i;

    if (!g) return -1;
    if (!(g->lat_max > g->lat_min) || !(g->lon_max > g->lon_min)) return -1;
    if (!(g->node_km > 0.0)) return -1;

    /* --- resolucion horizontal --- */
    lat_step = g->node_km / KM_PER_DEG;
    clat = fabs(cos(geocentric_lat_deg((g->lat_min + g->lat_max) * 0.5) * RAD));
    if (clat < 0.1) clat = 0.1;              /* cerca de los polos         */
    lon_step = g->node_km / (KM_PER_DEG * clat);

    g->ny = (int)floor((g->lat_max - g->lat_min) / lat_step) + 1;
    g->nx = (int)floor((g->lon_max - g->lon_min) / lon_step) + 1;
    if (g->ny < 1) g->ny = 1;
    if (g->nx < 1) g->nx = 1;

    /* --- profundidades --- */
    if (g->n_depth_layers > 0) {
        g->nz = g->n_depth_layers;
    } else {
        if (!(g->depth_step > 0.0)) g->depth_step = 25.0;
        if (g->depth_max < g->depth_min) g->depth_max = g->depth_min;
        g->nz = (int)floor((g->depth_max - g->depth_min) / g->depth_step) + 1;
        if (g->nz < 1) g->nz = 1;
    }
    if (g->nz > CSLOC_MAX_GRID_DEPTHS) g->nz = CSLOC_MAX_GRID_DEPTHS;

    /* Limite de recursos. */
    if (g->max_nodes > 0 &&
        (long)g->ny * g->nx * g->nz > (long)g->max_nodes) {
        return -2;
    }

    g->n_nodes = g->ny * g->nx * g->nz;

    g->lat   = (double *)malloc(sizeof(double) * g->ny);
    g->lon   = (double *)malloc(sizeof(double) * g->nx);
    g->depth = (double *)malloc(sizeof(double) * g->nz);
    if (!g->lat || !g->lon || !g->depth) { Grid_Free(g); return -1; }

    for (i = 0; i < g->ny; i++) g->lat[i] = geocentric_lat_deg(g->lat_min + i * lat_step);
    for (i = 0; i < g->nx; i++) g->lon[i] = g->lon_min + i * lon_step;
    if (g->n_depth_layers > 0) {
        for (i = 0; i < g->nz; i++) g->depth[i] = g->depth_layers[i];
    } else {
        for (i = 0; i < g->nz; i++) g->depth[i] = g->depth_min + i * g->depth_step;
        if (g->depth[g->nz - 1] > g->depth_max) g->depth[g->nz - 1] = g->depth_max;
    }

    return 0;
}

/* ------------------------------------------------------------------------- */
/* Carga una grilla desde un archivo .grid. `level` es GRID_LEVEL_*.          */
/* Devuelve 0, -1 (parseo/spec) o -2 (excede MaxNodes).                       */
/* ------------------------------------------------------------------------- */
int Grid_LoadFile(const char *path, int level, Grid *g)
{
    FILE *fp;
    char  line[CSLOC_STR * 4];

    if (!g || !path) return -1;
    memset(g, 0, sizeof(*g));
    g->level                 = level;
    g->max_nodes             = 0;        /* 0 = sin tope                  */
    g->sta_max_dist_km       = 1200.0;
    g->num_stations_per_node = 30;
    g->min_phases            = 0;        /* hereda cfg                    */
    g->backproj_threshold    = -1.0;     /* hereda cfg                    */
    snprintf(g->path, sizeof(g->path), "%s", path);

    fp = fopen(path, "r");
    if (!fp) {
        fprintf(stderr, "csnloc: no se pudo abrir grilla <%s>\n", path);
        return -1;
    }

    while (fgets(line, sizeof(line), fp)) {
        char  key[64], val[CSLOC_STR * 4];
        char *hash;

        hash = strchr(line, '#');
        if (hash) *hash = '\0';
        val[0] = '\0';
        if (sscanf(line, "%63s %[^\n]", key, val) < 1) continue;

        if      (!strcmp(key, "Name"))       snprintf(g->name, sizeof(g->name), "%s", val);
        else if (!strcmp(key, "LatMin"))     g->lat_min = atof(val);
        else if (!strcmp(key, "LatMax"))     g->lat_max = atof(val);
        else if (!strcmp(key, "LonMin"))     g->lon_min = atof(val);
        else if (!strcmp(key, "LonMax"))     g->lon_max = atof(val);
        else if (!strcmp(key, "NodeKm"))     g->node_km = atof(val);
        else if (!strcmp(key, "DepthLayers"))parse_depth_layers(val, g);
        else if (!strcmp(key, "DepthMin"))   g->depth_min = atof(val);
        else if (!strcmp(key, "DepthMax"))   g->depth_max = atof(val);
        else if (!strcmp(key, "DepthStep"))  g->depth_step = atof(val);
        else if (!strcmp(key, "MaxNodes"))   g->max_nodes = atoi(val);
        else if (!strcmp(key, "StaMaxDistKm")) g->sta_max_dist_km = atof(val);
        else if (!strcmp(key, "NumStationsPerNode")) g->num_stations_per_node = atoi(val);
        else if (!strcmp(key, "MinPhases"))  g->min_phases = atoi(val);
        else if (!strcmp(key, "BackProjThreshold")) g->backproj_threshold = atof(val);
        /* clave desconocida: se ignora */
    }
    fclose(fp);

    if (g->name[0] == '\0')
        snprintf(g->name, sizeof(g->name), "%s", path);

    return Grid_BuildAxes(g);
}

/* ------------------------------------------------------------------------- */
/* true si (lat, lon) cae dentro de la bbox ampliada por `margin_deg`.        */
/* ------------------------------------------------------------------------- */
int Grid_ContainsLL(const Grid *g, double lat, double lon, double margin_deg)
{
    if (!g) return 0;
    return (lat >= g->lat_min - margin_deg &&
            lat <= g->lat_max + margin_deg &&
            lon >= g->lon_min - margin_deg &&
            lon <= g->lon_max + margin_deg);
}

/* ------------------------------------------------------------------------- */
/* Precómputo estacion<->nodo: para cada nodo, las estaciones a distancia     */
/* <= StaMaxDistKm (tope NumStationsPerNode, las mas cercanas), en CSR        */
/* node_sta_off/node_sta_idx ordenado por distancia ascendente.               */
/* ------------------------------------------------------------------------- */
int Grid_PrecomputeStations(Grid *g, const StationList *st)
{
    int    *off, *idx = NULL, nnz = 0, cap = 0;
    unsigned char *mask;
    double *td;
    int    *ti;
    int     cap_n, i, s;

    if (!g || !st || g->n_nodes <= 0) return -1;

    off  = (int *)malloc(sizeof(int) * ((size_t)g->n_nodes + 1));
    mask = (unsigned char *)calloc((size_t)(st->n > 0 ? st->n : 1), 1);
    if (!off || !mask) { free(off); free(mask); return -1; }

    cap_n = g->num_stations_per_node;
    if (cap_n <= 0 || cap_n > st->n) cap_n = st->n;
    if (cap_n < 1) cap_n = 1;

    td = (double *)malloc(sizeof(double) * (size_t)cap_n);
    ti = (int *)malloc(sizeof(int) * (size_t)cap_n);
    if (!td || !ti) { free(td); free(ti); free(off); free(mask); return -1; }

    /* Atajo denso: sin radio y sin tope -> todas las estaciones en cada nodo,
       sin calcular ninguna distancia. */
    {
        int dense = (g->sta_max_dist_km <= 0.0 && cap_n >= st->n);

    off[0] = 0;
    for (i = 0; i < g->n_nodes; i++) {
        int    iy = (i / g->nx) % g->ny;
        int    ix = i % g->nx;
        int    nsel = 0;
        double maxd = 1e18;

        if (dense) {
            for (s = 0; s < st->n; s++) { td[s] = 0.0; ti[s] = s; }
            nsel = st->n;
        } else {
            /* Seleccion por distancia APROXIMADA (equirectangular, sin trig);
               la distancia exacta se calcula en runtime al apilar. */
            double clat_node = cos(g->lat[iy] * RAD);
            double rdeg = (g->sta_max_dist_km > 0.0)
                          ? g->sta_max_dist_km / KM_PER_DEG : 0.0;
            for (s = 0; s < st->n; s++) {
                double dlat = (st->st[s].lat - g->lat[iy]) * KM_PER_DEG;
                double dlon = st->st[s].lon - g->lon[ix];
                double dlon_km, d;

                while (dlon > 180.0) dlon -= 360.0;
                while (dlon < -180.0) dlon += 360.0;

                if (rdeg > 0.0 &&
                    (fabs(st->st[s].lat - g->lat[iy]) > rdeg ||
                     fabs(dlon) > rdeg))
                    continue;

                dlon_km = dlon * KM_PER_DEG * clat_node;
                d = sqrt(dlat * dlat + dlon_km * dlon_km);
                if (g->sta_max_dist_km > 0.0 && d > g->sta_max_dist_km)
                    continue;

                if (nsel < cap_n) {
                    int k = nsel;
                    while (k > 0 && td[k - 1] > d) {
                        td[k] = td[k - 1]; ti[k] = ti[k - 1]; k--;
                    }
                    td[k] = d; ti[k] = s; nsel++;
                    if (nsel == cap_n) maxd = td[cap_n - 1];
                } else if (d < maxd) {
                    int k = cap_n - 1;
                    while (k > 0 && td[k - 1] > d) {
                        td[k] = td[k - 1]; ti[k] = ti[k - 1]; k--;
                    }
                    td[k] = d; ti[k] = s;
                    maxd = td[cap_n - 1];
                }
            }
        }

        if (nnz + nsel > cap) {
            int ncap = cap ? cap * 2 : 4096;
            int *tmp;
            while (ncap < nnz + nsel) ncap *= 2;
            tmp = (int *)realloc(idx, sizeof(int) * (size_t)ncap);
            if (!tmp) { free(td); free(ti); free(idx); free(off); free(mask); return -1; }
            idx = tmp; cap = ncap;
        }
        {
            int k;
            for (k = 0; k < nsel; k++) {
                idx[nnz++] = ti[k];
                if (ti[k] >= 0 && ti[k] < st->n) mask[ti[k]] = 1;
            }
        }
        off[i + 1] = nnz;
    }
    }  /* fin bloque dense */

    free(td);
    free(ti);

    g->node_sta_off = off;
    g->node_sta_idx = idx;
    g->nnz          = nnz;
    g->sta_mask     = mask;
    g->n_sta        = st->n;
    return 0;
}

void Grid_Free(Grid *g)
{
    if (!g) return;
    free(g->lat);   g->lat = NULL;
    free(g->lon);   g->lon = NULL;
    free(g->depth); g->depth = NULL;
    free(g->node_sta_off); g->node_sta_off = NULL;
    free(g->node_sta_idx); g->node_sta_idx = NULL;
    free(g->sta_mask);     g->sta_mask = NULL;
    g->nnz = 0;
    g->nx = g->ny = g->nz = g->n_nodes = 0;
}

void Grid_IndexToLLD(const Grid *g, int iy, int ix, int iz,
                     double *lat, double *lon, double *depth)
{
    if (!g) return;
    if (lat)   *lat   = g->lat[iy];
    if (lon)   *lon   = g->lon[ix];
    if (depth) *depth = g->depth[iz];
}
