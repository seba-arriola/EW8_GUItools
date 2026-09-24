/*
 * CA1/CA3: parser de archivos de grilla.
 *   - DepthLayers no homogeneo (con desorden y duplicados) -> normaliza.
 *   - DepthMin/Max/Step homogeneo.
 *   - bbox invertida y archivo inexistente -> error.
 *   - MaxNodes excedido -> -2.
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
    Grid g;
    int  rc;
    const char *pa = "/tmp/csnloc_gridparse_a.grid";
    const char *pb = "/tmp/csnloc_gridparse_b.grid";
    const char *pc = "/tmp/csnloc_gridparse_c.grid";
    const char *pd = "/tmp/csnloc_gridparse_d.grid";

    write_file(pa,
        "Name Tarapaca\n"
        "LatMin -21.653\nLatMax -16.347\nLonMin -71.593\nLonMax -67.408\n"
        "NodeKm 10.0\n"
        "DepthLayers 5,10,20,40,80,160,300\n"
        "MaxNodes 200000\n");
    rc = Grid_LoadFile(pa, GRID_LEVEL_LOCAL, &g);
    CHECK(rc == 0, "CA1: carga grilla con DepthLayers");
    CHECK(g.n_depth_layers == 7 && g.nz == 7, "CA1: 7 capas de profundidad");
    CHECK(g.depth[0] == 5.0 && g.depth[6] == 300.0, "CA1: profundidades extremas");
    CHECK(g.level == GRID_LEVEL_LOCAL, "CA1: nivel local");
    CHECK(g.nx >= 44 && g.nx <= 46, "CA1: ~45 columnas (Glass3 60x45)");
    CHECK(g.ny >= 59 && g.ny <= 61, "CA1: ~60 filas");
    CHECK(strcmp(g.name, "Tarapaca") == 0, "CA1: nombre");
    Grid_Free(&g);

    write_file(pb,
        "Name Hom\nLatMin -30\nLatMax -20\nLonMin -75\nLonMax -65\n"
        "NodeKm 50\nDepthMin 0\nDepthMax 200\nDepthStep 25\n");
    rc = Grid_LoadFile(pb, GRID_LEVEL_REGIONAL, &g);
    CHECK(rc == 0, "carga grilla homogenea");
    CHECK(g.nz == 9, "nz==9 (0..200 paso 25)");
    Grid_Free(&g);

    write_file(pc,
        "Name Norm\nLatMin -30\nLatMax -20\nLonMin -75\nLonMax -65\n"
        "NodeKm 50\nDepthLayers 100,10,50,10,5\n");
    rc = Grid_LoadFile(pc, GRID_LEVEL_LOCAL, &g);
    CHECK(rc == 0, "carga DepthLayers desordenadas");
    CHECK(g.n_depth_layers == 4, "dedupe a 4 capas");
    CHECK(g.depth[0] == 5.0 && g.depth[3] == 100.0, "ordenadas 5..100");
    Grid_Free(&g);

    write_file(pd,
        "Name Bad\nLatMin -20\nLatMax -30\nLonMin -75\nLonMax -65\nNodeKm 50\n");
    rc = Grid_LoadFile(pd, GRID_LEVEL_LOCAL, &g);
    CHECK(rc == -1, "bbox invertida falla");

    rc = Grid_LoadFile("/tmp/no_such_grid_xyz.grid", GRID_LEVEL_LOCAL, &g);
    CHECK(rc == -1, "archivo inexistente falla");

    write_file(pa,
        "Name Big\nLatMin -40\nLatMax -10\nLonMin -80\nLonMax -60\n"
        "NodeKm 1\nMaxNodes 1000\n");
    rc = Grid_LoadFile(pa, GRID_LEVEL_LOCAL, &g);
    CHECK(rc == -2, "CA3: MaxNodes excedido devuelve -2");
    Grid_Free(&g);

    if (fails) { printf("\n%d FALLOS\n", fails); return 1; }
    printf("\nOK test_grid_parse\n");
    return 0;
}
