/*
 * Test headless de la tablita del preview (pestana Ondas): titulo por estado,
 * formatos de los 7 parametros y guion cuando falta el dato. Sin GTK.
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "csnhypodbp_preview.h"

static int fails = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); fails++; } \
    else printf("ok: %s\n", msg); \
} while (0)

int main(void)
{
    CsnhypodbpPreviewVals v;

    /* 1) cabeceras: 7, con la unidad pegada al nombre */
    CHECK(CSNHYPODBP_PREVIEW_NVALS == 7, "7 parametros en la tablita");
    CHECK(strcmp(csnhypodbp_preview_headers[0], "Lat") == 0 &&
          strcmp(csnhypodbp_preview_headers[2], "Depth[km]") == 0 &&
          strcmp(csnhypodbp_preview_headers[4], "RMS[s]") == 0,
          "cabeceras con unidad pegada");

    /* 2) candidata: titulo con el localizador y valores formateados */
    csnhypodbp_preview_from_candidate(&v, "csnloc", -33.1234, -71.5678, 34.6, 18,
                                      0.4234, "4.8-12", "");
    CHECK(v.candidate == 1, "candidata marcada");
    CHECK(strcmp(v.title, "New solution · csnloc") == 0, "titulo de la candidata");
    CHECK(strcmp(v.val[0], "-33.123") == 0 && strcmp(v.val[1], "-71.568") == 0,
          "lat/lon a 3 decimales");
    CHECK(strcmp(v.val[2], "35") == 0, "profundidad sin decimales");
    CHECK(strcmp(v.val[3], "18") == 0, "nph de la candidata");
    CHECK(strcmp(v.val[4], "0.42") == 0, "rms a 2 decimales");
    CHECK(strcmp(v.val[5], "4.8-12") == 0 && strcmp(v.val[6], "-") == 0,
          "magnitud ausente -> guion");

    /* 3) candidata sin localizador activo */
    csnhypodbp_preview_from_candidate(&v, "", 1.0, 2.0, 3.0, 1, 0.1, "", "");
    CHECK(strcmp(v.title, "New solution · -") == 0, "sin localizador -> guion");

    /* 4) seleccionada: titulo propio y textos de la tabla tal cual */
    csnhypodbp_preview_from_selected(&v, -30.5, -70.25, 12.0, "0.4", "22", "5.1-30", "6.2-8");
    CHECK(v.candidate == 0, "la seleccionada no es candidata");
    CHECK(strcmp(v.title, "Selected solution") == 0, "titulo de la seleccionada");
    CHECK(strcmp(v.val[0], "-30.500") == 0 && strcmp(v.val[2], "12") == 0,
          "lat/profundidad de la seleccionada");
    CHECK(strcmp(v.val[3], "22") == 0 && strcmp(v.val[4], "0.4") == 0,
          "nph/rms de la seleccionada tal cual");
    CHECK(strcmp(v.val[5], "5.1-30") == 0 && strcmp(v.val[6], "6.2-8") == 0,
          "magnitudes de la seleccionada");

    /* 5) sin seleccion: titulo propio y los 7 en guion */
    csnhypodbp_preview_clear(&v);
    {
        int i, all = 1;
        for (i = 0; i < CSNHYPODBP_PREVIEW_NVALS; i++)
            if (strcmp(v.val[i], "-") != 0) all = 0;
        CHECK(v.candidate == 0, "clear no marca candidata");
        CHECK(strcmp(v.title, "No solution selected") == 0, "titulo sin seleccion");
        CHECK(all, "los 7 valores en guion");
    }

    /* 6) NULL no revienta */
    csnhypodbp_preview_clear(NULL);
    csnhypodbp_preview_from_candidate(NULL, "x", 0, 0, 0, 0, 0, "", "");
    csnhypodbp_preview_from_selected(NULL, 0, 0, 0, "", "", "", "");
    CHECK(1, "NULL no revienta");

    if (fails > 0) { printf("\n%d FALLOS\n", fails); return 1; }
    printf("\nOK test_preview\n");
    return 0;
}
