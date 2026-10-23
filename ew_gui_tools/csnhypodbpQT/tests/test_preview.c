/*
 * test_preview.c - Nucleo puro del preview (fase 5).
 *
 * Verifica, sin GUI:
 *   - los 5 estados del cuadro (IDLE / RUNNING / CANDIDATA / ERROR) y que los
 *     valores viejos NO sobrevivan al estado de calculo;
 *   - que `from_candidate` formatee lat/lon/prof/nph/rms y deje "-" en las
 *     magnitudes que no vinieron;
 *   - que `from_selected` copie rms/nph/mags TAL CUAL (ya vienen formateados de
 *     la tabla de detalle);
 *   - la regla de magnitud preferida de csnmags y el parseo "valor-nst".
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "bpq_capi.h"

static int g_fail = 0;
static int g_checks = 0;

#define CK(cond) do {                                                          \
    g_checks++;                                                                \
    if (!(cond)) {                                                             \
        g_fail++;                                                              \
        fprintf(stderr, "FALLA %s:%d  %s\n", __FILE__, __LINE__, #cond);        \
    }                                                                          \
} while (0)

static int all_dash(const CsnhypodbpPreviewVals *v)
{
    int i;

    for (i = 0; i < CSNHYPODBP_PREVIEW_NVALS; i++)
        if (strcmp(v->val[i], "-") != 0) return 0;
    return 1;
}

int main(void)
{
    CsnhypodbpPreviewVals v;

    printf("== test_preview ==\n");

    /* --- 1. cabeceras y bloques -------------------------------------------- */
    CK(CSNHYPODBP_PREVIEW_NVALS == 10);
    CK(CSNHYPODBP_PREVIEW_NPARAMS == 5);
    CK(CSNHYPODBP_PREVIEW_MAGS == 5);
    {
        int i, nmag = 0;

        for (i = 0; i < CSNHYPODBP_PREVIEW_NVALS; i++) {
            CK(csnhypodbp_preview_headers[i] != NULL);
            CK(csnhypodbp_preview_headers[i][0] != '\0');
            if (csnhypodbp_preview_is_mag(i)) nmag++;
            CK(csnhypodbp_preview_is_mag(i) == (i >= CSNHYPODBP_PREVIEW_NPARAMS));
        }
        CK(nmag == CSNHYPODBP_PREVIEW_MAGS);
        CK(csnhypodbp_preview_is_mag(-1) == 0);
        CK(csnhypodbp_preview_is_mag(CSNHYPODBP_PREVIEW_NVALS) == 0);
        CK(csnhypodbp_preview_mag_col(CSNHYPODBP_PREVIEW_NPARAMS) == 0);
        CK(csnhypodbp_preview_mag_col(CSNHYPODBP_PREVIEW_NVALS - 1) ==
           CSNHYPODBP_PREVIEW_MAGS - 1);
        CK(csnhypodbp_preview_mag_col(0) == -1);
        CK(csnhypodbp_preview_mag_col(CSNHYPODBP_PREVIEW_NVALS) == -1);
    }

    /* --- 2. IDLE: guiones y sin candidata ---------------------------------- */
    memset(&v, 0xAA, sizeof(v));
    csnhypodbp_preview_clear(&v);
    CK(v.state == CSNHYPODBP_PREVIEW_IDLE);
    CK(v.candidate == 0);
    CK(v.title[0] != '\0');
    CK(all_dash(&v));
    CK(v.busy[0] == '\0');
    CK(v.err[0] == '\0');

    /* --- 3. RUNNING: sin cifras viejas, con el reloj ----------------------- */
    memset(&v, 0, sizeof(v));
    csnhypodbp_preview_set_busy(&v, "hyp2000_ring", 12);
    CK(v.state == CSNHYPODBP_PREVIEW_RUNNING);
    CK(v.candidate == 0);
    CK(all_dash(&v));
    CK(strstr(v.busy, "calculando") != NULL);
    CK(strstr(v.busy, "hyp2000_ring") != NULL);
    CK(strstr(v.busy, "12") != NULL);
    CK(strstr(v.title, "hyp2000_ring") != NULL);
    CK(strstr(v.otime, "--") != NULL);

    /* --- 4. ERROR: guiones + motivo visible -------------------------------- */
    memset(&v, 0, sizeof(v));
    csnhypodbp_preview_set_error(&v, "nlloc_ring", "timeout tras 600 s");
    CK(v.state == CSNHYPODBP_PREVIEW_ERROR);
    CK(v.candidate == 0);
    CK(all_dash(&v));
    CK(strstr(v.err, "timeout") != NULL);
    CK(strstr(v.busy, "calculando") == NULL);

    /* --- 5. CANDIDATA: numeros formateados y mags vacias en "-" ------------ */
    memset(&v, 0, sizeof(v));
    csnhypodbp_preview_from_candidate(&v, 1700000000.0, "csnloc",
                                      -22.12344, -66.89701, 165.24, 7, 0.086,
                                      "", "", "", "", "");
    CK(v.state == CSNHYPODBP_PREVIEW_CANDIDATE);
    CK(v.candidate == 1);
    CK(strstr(v.title, "csnloc") != NULL);
    CK(strncmp(v.val[0], "-22.12", 6) == 0);
    CK(strncmp(v.val[1], "-66.89", 6) == 0);
    CK(strncmp(v.val[2], "165", 3) == 0);
    CK(strcmp(v.val[3], "7") == 0);
    CK(strncmp(v.val[4], "0.08", 4) == 0 ||
       strncmp(v.val[4], "0.09", 4) == 0);
    {
        int k;

        for (k = 0; k < CSNHYPODBP_PREVIEW_MAGS; k++)
            CK(strcmp(v.val[CSNHYPODBP_PREVIEW_NPARAMS + k], "-") == 0);
    }
    CK(v.busy[0] == '\0');
    CK(v.err[0] == '\0');

    /* Con magnitudes: se copian tal cual. */
    memset(&v, 0, sizeof(v));
    csnhypodbp_preview_from_candidate(&v, 1700000000.0, "csnloc",
                                      -22.0, -66.0, 10.0, 4, 0.5,
                                      "4.5-12", "5.1-8", "", "-", "6.2");
    CK(strcmp(v.val[5], "4.5-12") == 0);
    CK(strcmp(v.val[6], "5.1-8") == 0);
    CK(strcmp(v.val[7], "-") == 0);
    CK(strcmp(v.val[8], "-") == 0);
    CK(strcmp(v.val[9], "6.2") == 0);

    /* --- 6. ELEGIDA: rms/nph/mags llegan ya formateados -------------------- */
    memset(&v, 0, sizeof(v));
    csnhypodbp_preview_from_selected(&v, 1700000000.0, -21.5, -68.7, 33.0,
                                     "0.2", "12", "3.1-6", "-", "-", "2.9-3", "");
    CK(v.state == CSNHYPODBP_PREVIEW_CANDIDATE);
    CK(v.candidate == 0);
    CK(strcmp(v.val[3], "12") == 0);
    CK(strcmp(v.val[4], "0.2") == 0);
    CK(strcmp(v.val[5], "3.1-6") == 0);
    CK(strcmp(v.val[6], "-") == 0);
    CK(strcmp(v.val[8], "2.9-3") == 0);
    CK(strcmp(v.val[9], "-") == 0);

    /* --- 7. mensaje de "sin solucion nueva" -------------------------------- */
    {
        char msg[128] = "";

        csnhypodbp_preview_nosol_msg(msg, sizeof(msg), "nlloc_ring", "ref.err");
        CK(strstr(msg, "nlloc_ring") != NULL);
        CK(strstr(msg, "ref.err") != NULL);
        csnhypodbp_preview_nosol_msg(msg, sizeof(msg), "nlloc_ring", NULL);
        CK(strstr(msg, "nlloc_ring") != NULL);
        CK(strlen(msg) > 0);
    }

    /* --- 8. "valor-nst" y magnitud preferida ------------------------------- */
    {
        double val = 0.0;
        int nst = 0;

        CK(csnhypodbp_mag_split("4.5-12", &val, &nst) == 1);
        CK(fabs(val - 4.5) < 1e-9);
        CK(nst == 12);
        CK(csnhypodbp_mag_split("-", &val, &nst) == 0);
        CK(csnhypodbp_mag_split("", &val, &nst) == 0);
    }
    /* Regla de csnmags: Mwp(>=5.5, nst>=3) > Ms > Mb > ML. */
    CK(csnhypodbp_mag_pref_default("3.0-5", "5.7-4", "4.0-4", "4.2-6") == 1);
    CK(csnhypodbp_mag_pref_default("3.0-5", "5.0-4", "4.0-4", "4.2-6") == 3);
    CK(csnhypodbp_mag_pref_default("3.0-5", "-", "4.0-4", "-") == 2);
    CK(csnhypodbp_mag_pref_default("3.0-5", "-", "-", "-") == 0);
    CK(csnhypodbp_mag_pref_default("-", "-", "-", "-") == -1);
    {
        const int has[CSNHYPODBP_PREVIEW_MAGS] = { 1, 0, 0, 0, 0 };

        CK(csnhypodbp_mag_pref_ok(has, 0) == 1);
        CK(csnhypodbp_mag_pref_ok(has, 1) == 0);
        CK(csnhypodbp_mag_pref_ok(has, 4) == 0);
    }

    /* --- 9. atajos de filtro (4.3): la tecla del activo lo apaga ----------- */
    {
        CsnhypodbpFilterCfg fc;

        memset(&fc, 0, sizeof(fc));
        fc.nf = 2;
        snprintf(fc.f[0].name, sizeof(fc.f[0].name), "HP");
        fc.f[0].key = 'r';
        fc.f[0].type = 1;
        fc.f[0].f1 = 1.0;
        fc.f[0].order = 2;
        snprintf(fc.f[1].name, sizeof(fc.f[1].name), "P_local");
        fc.f[1].key = '1';
        fc.f[1].type = 3;
        fc.f[1].f1 = 1.0;
        fc.f[1].f2 = 20.0;
        fc.f[1].order = 2;
        fc.menu_key = 'f';
        fc.raw_key = '0';
        fc.stalta_key = 't';

        /* Desde Raw, cada tecla enciende su preset. */
        CK(csnhypodbp_filtercfg_key_action(&fc, -1, '1') == 1);
        CK(csnhypodbp_filtercfg_key_action(&fc, -1, 'r') == 0);
        /* La tecla del ACTIVO lo apaga (vuelve a Raw). */
        CK(csnhypodbp_filtercfg_key_action(&fc, 1, '1') ==
           CSNHYPODBP_FILTER_KEY_RAW);
        CK(csnhypodbp_filtercfg_key_action(&fc, 0, 'r') ==
           CSNHYPODBP_FILTER_KEY_RAW);
        /* Teclas especiales. */
        CK(csnhypodbp_filtercfg_key_action(&fc, 0, '0') ==
           CSNHYPODBP_FILTER_KEY_RAW);
        CK(csnhypodbp_filtercfg_key_action(&fc, 0, 'f') ==
           CSNHYPODBP_FILTER_KEY_MENU);
        CK(csnhypodbp_filtercfg_key_action(&fc, 0, 't') ==
           CSNHYPODBP_FILTER_KEY_NONE);
        CK(csnhypodbp_filtercfg_key_action(&fc, 0, 'z') ==
           CSNHYPODBP_FILTER_KEY_NONE);
        /* Cambiar de preset no es un toggle: con '1' activo, 'r' enciende HP. */
        CK(csnhypodbp_filtercfg_key_action(&fc, 1, 'r') == 0);
        /* Etiqueta del preset, para la ventana Filters… */
        {
            char lbl[96] = "";

            csnhypodbp_filtercfg_format_preset(&fc.f[1], lbl, sizeof(lbl));
            CK(lbl[0] != '\0');
            CK(strstr(lbl, "1") != NULL);      /* frecuencia u orden visibles */
        }
    }

    printf("== preview checks=%d fallas=%d ==\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
