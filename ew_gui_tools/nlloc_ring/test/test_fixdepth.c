/*
 * Profundidad fija en NLLoc (emulada): LOCGRID con 2 nodos z centrados en el
 * valor pedido y LOCSEARCH OCT con init_num_cells_z=1. Logica pura.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../nlloc_depth.h"

static int fails = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); fails++; } \
    else printf("ok: %s\n", msg); \
} while (0)

int main(void)
{
    char out[1024];

    /* LOCGRID: conserva nx/ny/x0/y0/dx/dy y el sufijo; fuerza nz=2, z0/dz. */
    CHECK(nlloc_locgrid_fixdepth(
              "LOCGRID 442 538 221 -367 -404 -9.25 1.5 1.5 1.5 PROB_DENSITY SAVE\n",
              out, sizeof(out), 33.0, 0.1) == 0, "LOCGRID: reescribe");
    CHECK(strcmp(out, "LOCGRID 442 538 2 -367.0000 -404.0000 32.9500 1.5000 1.5000 0.1000 PROB_DENSITY SAVE\n") == 0,
          "LOCGRID: nz=2, z0=32.95, dz=0.1, resto intacto");
    CHECK(nlloc_locgrid_fixdepth(
              "LOCGRID 81 81 31 -40 -40 0 1 1 5 PROB_DENSITY SAVE", out, sizeof(out),
              10.0, 0.2) == 0 &&
          strstr(out, " 81 81 2 -40.0000 -40.0000 9.9000 1.0000 1.0000 0.2000") != NULL,
          "LOCGRID: otro espaciado");
    CHECK(nlloc_locgrid_fixdepth("TRANS SIMPLE 0 0 0", out, sizeof(out), 10, 0.1) == -1,
          "LOCGRID: una linea que no lo es -> -1");
    CHECK(nlloc_locgrid_fixdepth("LOCGRID 10 10 2 0 0 10 1 1 0", out, sizeof(out),
                                 10, 0.0) == -1, "LOCGRID: spacing 0 -> -1");

    /* LOCSEARCH OCT: init_num_cells_z (4.o campo) pasa a 1. */
    CHECK(nlloc_locsearch_fixz("LOCSEARCH OCT 96 48 6 0.05 50000 10000 4 0\n",
                               out, sizeof(out)) == 0 &&
          strcmp(out, "LOCSEARCH OCT 96 48 1 0.05 50000 10000 4 0\n") == 0,
          "LOCSEARCH OCT: init_z=1 y resto intacto");
    CHECK(nlloc_locsearch_fixz("LOCSEARCH OCT 10 10 4 0.5 20000 1000 0 1\n",
                               out, sizeof(out)) == 0 &&
          strstr(out, "OCT 10 10 1 0.5") != NULL, "LOCSEARCH OCT: segundo caso");
    CHECK(nlloc_locsearch_fixz("LOCMETH EDT_OT_WT 1\n", out, sizeof(out)) == 0 &&
          strcmp(out, "LOCMETH EDT_OT_WT 1\n") == 0, "otra linea: se copia igual");
    CHECK(nlloc_locsearch_fixz(NULL, out, sizeof(out)) == -1, "LOCSEARCH NULL -> -1");

    if (fails > 0) { printf("\n%d FALLOS\n", fails); return 1; }
    printf("\nOK test_fixdepth\n");
    return 0;
}
