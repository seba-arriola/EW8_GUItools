/*
 * test_mww.c - Panel Mww (fase 6.3): semilla, tabla de parametros (rango y
 * parseo de cada celda), invariante "un fallo nunca es mudo", playera y guardado
 * del tensor sin origin.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cairo.h>

#include "bpq_capi.h"
#include "bpq_internal.h"

static int g_checks, g_fail;

#define CK(c) do {                                                    \
        g_checks++;                                                    \
        if (!(c)) {                                                    \
            g_fail++;                                                  \
            fprintf(stderr, "FALLA %s:%d: %s\n", __FILE__, __LINE__, #c); \
        }                                                              \
    } while (0)

static int find_field(const BpqMwwParamRow *rows, int n, int field)
{
    for (int i = 0; i < n; i++)
        if (rows[i].field == field) return i;
    return -1;
}

int main(void)
{
    BpqConfig cfg;
    BpqMww *m;
    BpqMwwParamRow rows[32];
    CsnhypodbpSolution sol;
    char err[512] = "";
    int n, i, mag_idx, t0_idx;

    /* --- 1. sin configuracion no hay panel -------------------------------- */
    CK(bpq_mww_new(NULL, err, sizeof(err)) == NULL);
    CK(err[0] != '\0');

    memset(&cfg, 0, sizeof(cfg));
    snprintf(cfg.mww_config_file, sizeof(cfg.mww_config_file), "%s", "csnwphase.d");
    snprintf(cfg.mww_workdir, sizeof(cfg.mww_workdir), "%s", "/tmp/bpq_mww_test");

    m = bpq_mww_new(&cfg, err, sizeof(err));
    if (!m) {
        /* Sin csnwphase.d a mano (cwd/EW_PARAMS): el test no falla, se salta. */
        printf("== mww checks=%d fallas=%d skips=1 (%s) ==\n", g_checks + 1,
               g_fail, err);
        return g_fail ? 1 : 0;
    }
    CK(bpq_mww_seeded(m) == 0);
    CK(bpq_mww_rows(m, rows, 32) == 0);          /* sin semilla no hay tabla */
    CK(bpq_mww_run(m, 1, err, sizeof(err)) == NULL);
    CK(err[0] != '\0');

    /* --- 2. semilla: origen + hipocentro + magnitud ----------------------- */
    memset(&sol, 0, sizeof(sol));
    sol.otime = 1700000000.0;
    sol.lat = -24.35;
    sol.lon = -67.73;
    sol.depth = 120.0;
    sol.qid = 1234;
    sol.mod = 0;
    sol.version = 2;
    sol.nph = 12;
    CK(bpq_mww_seed(m, NULL, 6.5, err, sizeof(err)) == 0);
    CK(err[0] != '\0');
    CK(bpq_mww_seed(m, &sol, NAN, err, sizeof(err)) == 0);   /* sin magnitud */
    CK(err[0] != '\0');
    CK(bpq_mww_seed(m, &sol, 6.5, err, sizeof(err)) == 1);
    CK(bpq_mww_seeded(m) == 1);
    CK(strstr(bpq_mww_rundir(m), "E1234_2_") != NULL);
    CK(strncmp(bpq_mww_rundir(m), "/tmp/bpq_mww_test/", 18) == 0);

    /* --- 3. tabla de parametros (misma fuente que i_master y el run.sh) --- */
    n = bpq_mww_rows(m, rows, 32);
    CK(n > 0);
    {
        int found_mag = -1, found_t0 = -1, found_dmin = -1, found_dmax = -1;
        int editable = 0;

        for (i = 0; i < n; i++) {
            CK(rows[i].name[0] != '\0');
            CK(rows[i].value[0] != '\0');
            if (rows[i].field != 0) editable++;
            if (strstr(rows[i].name, "mag") || strstr(rows[i].name, "Mag") ||
                strstr(rows[i].name, "MAG"))
                found_mag = i;
            if (strstr(rows[i].name, "t0") || strstr(rows[i].name, "origen"))
                found_t0 = i;
            if (strstr(rows[i].name, "Dmin") || strstr(rows[i].name, "dmin"))
                found_dmin = i;
            if (strstr(rows[i].name, "Dmax") || strstr(rows[i].name, "dmax"))
                found_dmax = i;
        }
        CK(found_mag >= 0);
        CK(found_t0 >= 0);
        CK(editable >= 8);               /* lat/lon/prof/mag/ventanas/cribado */
        if (found_mag >= 0)
            CK(strstr(rows[found_mag].value, "6.5") != NULL);
        if (found_t0 >= 0)
            CK(strstr(rows[found_t0].value, "2023") != NULL);
        (void)found_dmin;
        (void)found_dmax;
    }
    t0_idx = -1;
    for (i = 0; i < n; i++)
        if (strstr(rows[i].name, "t0") || strstr(rows[i].name, "origen"))
            t0_idx = i;
    CK(t0_idx >= 0);

    /* --- 4. celdas: rango y parseo (nunca un prefijo a medias) ------------ */
    {
        double lo = 0.0, hi = 0.0;

        CK(bpq_mww_field_limits(5, &lo, &hi) == 0);   /* MAGNITUDE */
        CK(hi > lo);
        CK(bpq_mww_edit(m, 5, "7.2") == 1);
        n = bpq_mww_rows(m, rows, 32);
        for (i = 0; i < n; i++)
            if (rows[i].field == 5) CK(strstr(rows[i].value, "7.2") != NULL);
        /* Texto invalido: se descarta el campo (ni un prefijo a medias: vuelve
         * al valor de la semilla, no a la edicion anterior). */
        CK(bpq_mww_edit(m, 5, "9") == 1);
        CK(bpq_mww_edit(m, 5, "9abc") == 0);
        n = bpq_mww_rows(m, rows, 32);
        for (i = 0; i < n; i++)
            if (rows[i].field == 5) CK(strstr(rows[i].value, "6.5") != NULL);
        /* Un campo que no es editable no se toca. */
        CK(bpq_mww_edit(m, 0, "1") == 0);
        /* El t0 tambien se edita por texto (campo 1 = ORIGIN_TIME). */
        CK(bpq_mww_edit(m, 1, "no-es-fecha") == 0);
        CK(bpq_mww_edit(m, 1, "2023-11-14 22:13:20") == 1);
        n = bpq_mww_rows(m, rows, 32);
        for (i = 0; i < n; i++)
            if (rows[i].field == 1) CK(strstr(rows[i].value, "2023") != NULL);
        (void)t0_idx;
    }

    /* --- 5. sin corrida no hay resultado, y el texto lo dice -------------- */
    CK(bpq_mww_result(m) == NULL);
    CK(bpq_mww_tensor_ready(m) == 0);
    CK(strstr(bpq_mww_summary(m), "sin resultado") != NULL);
    CK(bpq_mww_save(NULL, NULL, 0, NULL, m, err, sizeof(err)) == 0);
    CK(err[0] != '\0');
    CK(bpq_mww_save(NULL, &sol, 0, NULL, m, err, sizeof(err)) == 0);
    CK(err[0] != '\0');
    /* Sin resultado de la inversion no hay nada que colgar de la BD. */
    CK(bpq_mww_save(NULL, &sol, 162, "csnloc", m, err, sizeof(err)) == 0);
    CK(err[0] != '\0');
    /* Modulo del localizador: es el `mod` del origin_id (la solucion de `locate`
     * trae mod=0). */

    /* --- 6. playera: sin mecanismo dibuja el aviso, no basura ------------- */
    {
        unsigned char *buf = (unsigned char *)calloc(160 * 160, 4);
        cairo_surface_t *sf;
        cairo_t *cr;
        int dark = 0;

        CK(buf != NULL);
        if (buf) {
            sf = cairo_image_surface_create_for_data(buf, CAIRO_FORMAT_ARGB32,
                                                     160, 160, 160 * 4);
            cr = cairo_create(sf);
            bpq_mww_ball_draw(cr, 160, 160, m);
            cairo_destroy(cr);
            cairo_surface_destroy(sf);
            for (int k = 0; k < 160 * 160; k++)
                if (buf[k * 4] < 200 || buf[k * 4 + 1] < 200 ||
                    buf[k * 4 + 2] < 200)
                    dark++;
            CK(dark > 20);               /* el texto de aviso se dibujo */
            free(buf);
        }
        CK(bpq_mww_out_raw(m) == NULL);
    }

    bpq_mww_free(m);
    printf("== mww checks=%d fallas=%d ==\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
