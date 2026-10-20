/*
 * Test headless de la tablita del preview (pestana Ondas): titulo por estado,
 * formatos de los 10 parametros y guion cuando falta el dato. Sin GTK.
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

    /* Magnitud preferida: la regla de csnmags y radios sensibles solo con valor. */
    CHECK(csnhypodbp_mag_pref_default("4.5-12", "6.1-5", "5.0-9", "4.2-4") == 1,
          "pref: Mwp>=5.5 con nst>=3 gana");
    CHECK(csnhypodbp_mag_pref_default("4.5-12", "6.1-2", "5.0-9", "4.2-4") == 3,
          "pref: Mwp con pocas estaciones -> Ms");
    CHECK(csnhypodbp_mag_pref_default("4.5-12", "", "5.0-9", "") == 2,
          "pref: sin Ms -> Mb");
    CHECK(csnhypodbp_mag_pref_default("4.5-12", "", "", "") == 0,
          "pref: solo ML");
    CHECK(csnhypodbp_mag_pref_default("", "", "", "") == -1,
          "pref: sin magnitudes -> -1");
    {
        const int has[CSNHYPODBP_PREVIEW_MAGS] = { 1, 0, 1, 0, 0 };

        CHECK(csnhypodbp_mag_pref_ok(has, 0) == 1, "pref_ok: ML calculada");
        CHECK(csnhypodbp_mag_pref_ok(has, 1) == 0, "pref_ok: Mwp no calculada -> 0");
        CHECK(csnhypodbp_mag_pref_ok(has, 3) == 0, "pref_ok: Ms no calculada -> 0");
        CHECK(csnhypodbp_mag_pref_ok(has, 9) == 0, "pref_ok: kind invalido -> 0");
        CHECK(csnhypodbp_mag_pref_ok(has, CSNHYPODBP_PREVIEW_MWW_KIND) == 0,
              "pref_ok: Mww sin inversion -> 0");
        CHECK(csnhypodbp_mag_pref_ok(NULL, 0) == 0, "pref_ok: NULL -> 0");
    }
    {
        double val = 0.0;
        int nst = 0;

        CHECK(csnhypodbp_mag_split("4.5-12", &val, &nst) == 1 &&
              fabs(val - 4.5) < 1e-9 && nst == 12, "split: valor y nst");
        CHECK(csnhypodbp_mag_split("-", &val, &nst) == 0, "split: guion -> 0");
        CHECK(csnhypodbp_mag_split("", &val, &nst) == 0, "split: vacio -> 0");
        CHECK(csnhypodbp_mag_split("4.50", &val, &nst) == 1 && nst == 0,
              "split: sin nst");
        CHECK(csnhypodbp_mag_split(NULL, &val, &nst) == 0, "split: NULL -> 0");
    }

    /* 1) cabeceras: 10, con la unidad pegada al nombre y las 5 magnitudes */
    CHECK(CSNHYPODBP_PREVIEW_NVALS == 10, "10 parametros en la tablita");
    CHECK(CSNHYPODBP_PREVIEW_NPARAMS == 5 && CSNHYPODBP_PREVIEW_MAGS == 5,
          "5 parametros + 5 magnitudes");
    CHECK(!csnhypodbp_preview_is_mag(0) && !csnhypodbp_preview_is_mag(4) &&
          csnhypodbp_preview_is_mag(5) && csnhypodbp_preview_is_mag(9) &&
          !csnhypodbp_preview_is_mag(-1) && !csnhypodbp_preview_is_mag(10),
          "is_mag: 0..4 parametros, 5..9 magnitudes");
    CHECK(csnhypodbp_preview_mag_col(5) == 0 &&
          csnhypodbp_preview_mag_col(9) == 4 &&
          csnhypodbp_preview_mag_col(0) == -1 &&
          csnhypodbp_preview_mag_col(10) == -1,
          "mag_col: 0..4 dentro del bloque, -1 fuera");
    CHECK(strcmp(csnhypodbp_preview_headers[0], "Lat") == 0 &&
          strcmp(csnhypodbp_preview_headers[2], "Depth[km]") == 0 &&
          strcmp(csnhypodbp_preview_headers[4], "RMS[s]") == 0,
          "cabeceras con unidad pegada");
    CHECK(strcmp(csnhypodbp_preview_headers[5], "ML") == 0 &&
          strcmp(csnhypodbp_preview_headers[6], "Mwp") == 0 &&
          strcmp(csnhypodbp_preview_headers[7], "Mb") == 0 &&
          strcmp(csnhypodbp_preview_headers[8], "Ms") == 0 &&
          strcmp(csnhypodbp_preview_headers[9], "Mww") == 0,
          "las cuatro del ARC + Mww en la cabecera");

    /* 2) candidata: titulo con el localizador y valores formateados */
    csnhypodbp_preview_from_candidate(&v, 1600000267.0, "csnloc",
                                      -33.1234, -71.5678, 34.6, 18,
                                      0.4234, "4.8-12", "", "4.10", "", "7.42");
    CHECK(v.candidate == 1, "candidata marcada");
    CHECK(strcmp(v.title, "New solution · csnloc") == 0, "titulo de la candidata");
    CHECK(strcmp(v.otime, "Origin time: 2020-09-13 12:31:07 UTC") == 0,
          "tiempo de origen de la candidata");
    CHECK(strcmp(v.val[0], "-33.123") == 0 && strcmp(v.val[1], "-71.568") == 0,
          "lat/lon a 3 decimales");
    CHECK(strcmp(v.val[2], "35") == 0, "profundidad sin decimales");
    CHECK(strcmp(v.val[3], "18") == 0, "nph de la candidata");
    CHECK(strcmp(v.val[4], "0.42") == 0, "rms a 2 decimales");
    CHECK(strcmp(v.val[5], "4.8-12") == 0 && strcmp(v.val[6], "-") == 0,
          "magnitud ausente -> guion");
    CHECK(strcmp(v.val[7], "4.10") == 0 && strcmp(v.val[8], "-") == 0,
          "Mb/Ms de la candidata");
    CHECK(strcmp(v.val[9], "7.42") == 0, "Mww de la candidata (de la inversion)");

    /* 2b) indicador general: sin resultado todavia, guiones + texto de calculo */
    csnhypodbp_preview_set_busy(&v, "nlloc_ring", 12);
    CHECK(v.state == CSNHYPODBP_PREVIEW_RUNNING, "busy: estado RUNNING");
    CHECK(strcmp(v.busy, "calculando nlloc_ring… 12 s") == 0,
          "busy: localizador y segundos");
    CHECK(strcmp(v.title, "New solution · nlloc_ring") == 0,
          "busy: titulo con el localizador");
    CHECK(strcmp(v.otime, "Origin time: --") == 0, "busy: sin tiempo de origen");
    CHECK(v.candidate == 0 && v.err[0] == '\0', "busy: sin candidata ni error");
    {
        int i, all = 1;
        for (i = 0; i < CSNHYPODBP_PREVIEW_NVALS; i++)
            if (strcmp(v.val[i], "-") != 0) all = 0;
        CHECK(all, "busy: los 10 valores en guion");
    }
    csnhypodbp_preview_set_busy(&v, "", 0);
    CHECK(strcmp(v.busy, "calculando csnloc… 0 s") == 0,
          "busy: localizador vacio -> csnloc");
    csnhypodbp_preview_set_busy(&v, "x", -5);
    CHECK(strcmp(v.busy, "calculando x… 0 s") == 0, "busy: segundos negativos -> 0");

    /* 2c) fallo del locate: guiones + mensaje, sin candidata */
    csnhypodbp_preview_set_error(&v, "nlloc_ring", "timeout tras 600 s");
    CHECK(v.state == CSNHYPODBP_PREVIEW_ERROR, "error: estado ERROR");
    CHECK(strcmp(v.err, "timeout tras 600 s") == 0 && v.busy[0] == '\0',
          "error: motivo visible y sin texto busy");
    CHECK(strcmp(v.title, "New solution · nlloc_ring") == 0,
          "error: titulo con el localizador");
    CHECK(strcmp(v.otime, "Origin time: --") == 0, "error: sin tiempo de origen");
    CHECK(v.candidate == 0, "error: no queda candidata");
    {
        int i, all = 1;
        for (i = 0; i < CSNHYPODBP_PREVIEW_NVALS; i++)
            if (strcmp(v.val[i], "-") != 0) all = 0;
        CHECK(all, "error: los 10 valores en guion");
    }
    csnhypodbp_preview_set_error(&v, "x", "");
    CHECK(strcmp(v.err, "error") == 0, "error: motivo vacio -> error");

    /* 2d) mensaje de "sin solucion nueva" (acotado y NUL-terminado) */
    {
        char msg[96];
        char out[64];
        char big[600];

        csnhypodbp_preview_nosol_msg(msg, sizeof(msg), "nlloc_ring",
                                     "timeout tras 600 s");
        CHECK(strcmp(msg, "sin solución nueva con nlloc_ring: timeout tras 600 s") == 0,
              "nosol_msg: localizador y detalle");
        csnhypodbp_preview_nosol_msg(msg, sizeof(msg), "hyp2000_ring", "");
        CHECK(strcmp(msg, "sin solución nueva con hyp2000_ring") == 0,
              "nosol_msg: sin detalle no deja ': '");
        csnhypodbp_preview_nosol_msg(msg, sizeof(msg), "", NULL);
        CHECK(strcmp(msg, "sin solución nueva con csnloc") == 0,
              "nosol_msg: localizador vacio -> csnloc");
        memset(big, 'x', sizeof(big) - 1);
        big[sizeof(big) - 1] = '\0';
        csnhypodbp_preview_nosol_msg(out, sizeof(out), "nlloc_ring", big);
        CHECK(strlen(out) == sizeof(out) - 1,
              "nosol_msg: trunca al buffer y termina en NUL");
        csnhypodbp_preview_nosol_msg(NULL, 10, "x", "y");
        csnhypodbp_preview_nosol_msg(msg, 0, "x", "y");
        CHECK(1, "nosol_msg: NULL/0 no revienta");
    }

    /* 3) candidata sin localizador activo */
    csnhypodbp_preview_from_candidate(&v, 0.0, "", 1.0, 2.0, 3.0, 1, 0.1, "", "", "",
                                      "", "");
    CHECK(strcmp(v.title, "New solution · -") == 0, "sin localizador -> guion");
    CHECK(strcmp(v.val[9], "-") == 0, "sin Mww -> guion");
    CHECK(strcmp(v.otime, "Origin time: --") == 0, "sin otime -> guion");
    csnhypodbp_preview_from_candidate(&v, NAN, "", 1.0, 2.0, 3.0, 1, 0.1, "", "", "",
                                      "", "");
    CHECK(strcmp(v.otime, "Origin time: --") == 0, "otime NAN -> guion");

    /* 4) seleccionada: titulo propio y textos de la tabla tal cual */
    csnhypodbp_preview_from_selected(&v, 1600000267.0, -30.5, -70.25, 12.0, "0.4",
                                     "22", "5.1-30", "6.2-8", "4.9-11", "-", "7.10");
    CHECK(v.candidate == 0, "la seleccionada no es candidata");
    CHECK(strcmp(v.title, "Selected solution") == 0, "titulo de la seleccionada");
    CHECK(strcmp(v.otime, "Origin time: 2020-09-13 12:31:07 UTC") == 0,
          "tiempo de origen de la seleccionada");
    CHECK(strcmp(v.val[0], "-30.500") == 0 && strcmp(v.val[2], "12") == 0,
          "lat/profundidad de la seleccionada");
    CHECK(strcmp(v.val[3], "22") == 0 && strcmp(v.val[4], "0.4") == 0,
          "nph/rms de la seleccionada tal cual");
    CHECK(strcmp(v.val[5], "5.1-30") == 0 && strcmp(v.val[6], "6.2-8") == 0,
          "magnitudes de la seleccionada");
    CHECK(strcmp(v.val[7], "4.9-11") == 0 && strcmp(v.val[8], "-") == 0,
          "Mb/Ms de la seleccionada");
    CHECK(strcmp(v.val[9], "7.10") == 0, "Mww de la seleccionada");

    /* 5) sin seleccion: titulo propio y los 10 en guion */
    csnhypodbp_preview_clear(&v);
    {
        int i, all = 1;
        for (i = 0; i < CSNHYPODBP_PREVIEW_NVALS; i++)
            if (strcmp(v.val[i], "-") != 0) all = 0;
        CHECK(v.candidate == 0, "clear no marca candidata");
        CHECK(strcmp(v.title, "No solution selected") == 0, "titulo sin seleccion");
        CHECK(strcmp(v.otime, "Origin time: --") == 0, "clear sin tiempo de origen");
        CHECK(all, "los 10 valores en guion");
    }

    /* 6) NULL no revienta */
    csnhypodbp_preview_clear(NULL);
    csnhypodbp_preview_from_candidate(NULL, 0, "x", 0, 0, 0, 0, 0, "", "", "", "", "");
    csnhypodbp_preview_from_selected(NULL, 0, 0, 0, 0, "", "", "", "", "", "", "");
    CHECK(1, "NULL no revienta");

    if (fails > 0) { printf("\n%d FALLOS\n", fails); return 1; }
    printf("\nOK test_preview\n");
    return 0;
}
