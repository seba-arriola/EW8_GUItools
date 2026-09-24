/*
 * CA2: carga del conjunto de grillas anidadas y precómputo de estaciones.
 *   - dos grillas con niveles distintos -> gs.n==2 con niveles correctos.
 *   - sin grillas configuradas -> error.
 */
#include <stdio.h>
#include <string.h>
#include "../csnloc.h"

static int fails = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); fails++; } \
                              else printf("ok  : %s\n", msg); } while (0)

static void write_file(const char *path, const char *content)
{
    FILE *f = fopen(path, "w");
    if (!f) { perror("fopen"); return; }
    fputs(content, f);
    fclose(f);
}

int main(void)
{
    StationList  st;
    CSLocParams  cfg;
    GridSet      gs;
    int          rc;

    if (Stations_Load("test/test_stations.txt", &st) != 0 &&
        Stations_Load("test_stations.txt", &st) != 0) {
        printf("FAIL: no se pudo cargar estaciones\n"); return 1;
    }

    write_file("/tmp/csnloc_gs_global.grid",
        "Name Global\nLatMin -60\nLatMax 10\nLonMin -100\nLonMax -30\n"
        "NodeKm 50\nDepthLayers 10,50,100,300\nStaMaxDistKm 1200\n"
        "NumStationsPerNode 20\nMaxNodes 2000000\n");
    write_file("/tmp/csnloc_gs_local.grid",
        "Name Centro\nLatMin -36\nLatMax -30\nLonMin -74\nLonMax -68\n"
        "NodeKm 10\nDepthLayers 5,10,20,40,80,160,300\nStaMaxDistKm 800\n"
        "NumStationsPerNode 20\nMaxNodes 200000\n");

    memset(&cfg, 0, sizeof(cfg));
    cfg.GridActivationMinPicks = 3;
    cfg.GridActivationMarginDeg = 1.0;
    snprintf(cfg.GridFiles[0], CSLOC_STR, "/tmp/csnloc_gs_global.grid");
    cfg.GridFileLevel[0] = GRID_LEVEL_GLOBAL;
    snprintf(cfg.GridFiles[1], CSLOC_STR, "/tmp/csnloc_gs_local.grid");
    cfg.GridFileLevel[1] = GRID_LEVEL_LOCAL;
    cfg.n_gridfiles = 2;

    rc = GridSet_Load(&gs, &cfg, &st);
    CHECK(rc == 0, "CA2: GridSet_Load ok");
    CHECK(gs.n == 2, "CA2: dos grillas cargadas");
    CHECK(gs.g[0].level == GRID_LEVEL_GLOBAL, "CA2: nivel global");
    CHECK(gs.g[1].level == GRID_LEVEL_LOCAL, "CA2: nivel local");
    CHECK(gs.g[0].n_nodes > 0 && gs.g[1].n_nodes > 0, "CA2: nodos construidos");
    CHECK(gs.g[1].nnz > 0, "CA2: precómputo con estaciones asociadas");
    CHECK(gs.g[1].sta_mask != NULL && gs.g[1].n_sta == st.n,
          "CA2: mascara de estaciones");
    GridSet_Free(&gs);

    memset(&cfg, 0, sizeof(cfg));
    cfg.n_gridfiles = 0;
    rc = GridSet_Load(&gs, &cfg, &st);
    CHECK(rc == -1, "CA2: sin grillas configuradas falla");

    if (fails) { printf("\n%d FALLOS\n", fails); return 1; }
    printf("\nOK test_gridset_load\n");
    return 0;
}
