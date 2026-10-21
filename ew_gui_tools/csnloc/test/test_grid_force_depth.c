/*
 * Profundidad fija en csnloc: Grid_ForceDepth deja el eje z con UN nodo y
 * recalcula n_nodes; el precomputo estacion<->nodo sigue funcionando.
 */
#include <math.h>
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
    StationList st;
    Grid        g;
    const char *path = "/tmp/csnloc_force_depth.grid";
    int         rc;

    if (Stations_Load("test/test_stations.txt", &st) != 0 &&
        Stations_Load("test_stations.txt", &st) != 0) {
        printf("FAIL: no se pudo cargar estaciones\n");
        return 1;
    }

    write_file(path,
        "Name Fija\nLatMin -36\nLatMax -30\nLonMin -74\nLonMax -68\n"
        "NodeKm 10\nDepthLayers 5,10,20,40,80,160,300\nStaMaxDistKm 800\n"
        "NumStationsPerNode 20\nMaxNodes 2000000\n");

    memset(&g, 0, sizeof(g));
    rc = Grid_LoadFile(path, GRID_LEVEL_LOCAL, &g);
    CHECK(rc == 0, "grid: carga ok");
    CHECK(g.nz == 7 && g.n_nodes == g.nx * g.ny * 7, "grid: 7 capas de profundidad");

    rc = Grid_ForceDepth(&g, 33.0);
    CHECK(rc == 0, "force: ok");
    CHECK(g.nz == 1, "force: un solo nodo z");
    CHECK(g.n_nodes == g.nx * g.ny, "force: n_nodes = nx*ny");
    CHECK(fabs(g.depth[0] - 33.0) < 1e-9, "force: depth[0] == 33");
    CHECK(g.n_depth_layers == 1 && fabs(g.depth_layers[0] - 33.0) < 1e-9,
          "force: la especificacion tambien queda fija");
    CHECK(fabs(g.depth_min - 33.0) < 1e-9 && fabs(g.depth_max - 33.0) < 1e-9,
          "force: depth_min/max == 33");

    rc = Grid_PrecomputeStations(&g, &st);
    CHECK(rc == 0, "force: el precomputo CSR sigue funcionando con nz=1");
    CHECK(g.node_sta_off != NULL, "force: CSR reconstruido");

    Grid_Free(&g);
    CHECK(g.nz == 0 && g.depth == NULL && g.lat == NULL,
          "force: Grid_Free deja la grilla vacia");

    CHECK(Grid_ForceDepth(NULL, 10.0) == -1, "force: NULL -> -1");
    remove(path);

    if (fails) { printf("\n%d FALLOS\n", fails); return 1; }
    printf("\nOK test_grid_force_depth\n");
    return 0;
}
