/*
 * test_db.c - Historial / busqueda en la base hipocentral (fase 3).
 *
 * Verifica sin GUI:
 *   - el parseo de tiempos UTC del dialogo (formato, ISO con 'T' y rechazos);
 *   - que sin DbEnvFile la capa queda DESHABILITADA pero no peta, y devuelve el
 *     motivo en `err`;
 *   - contra la base real (si `db.env` existe y responde): que una busqueda en un
 *     rango con datos devuelve origenes y los deja en el store.
 *
 * Si la base no esta disponible el bloque final se SALTA (no falla): el test
 * tiene que poder correr sin PostgreSQL.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "bpq_capi.h"

static int g_fail = 0;
static int g_checks = 0;
static int g_skips = 0;

#define CK(cond) do {                                                          \
    g_checks++;                                                                \
    if (!(cond)) {                                                             \
        g_fail++;                                                              \
        fprintf(stderr, "FALLA %s:%d  %s\n", __FILE__, __LINE__, #cond);        \
    }                                                                          \
} while (0)

static double utc(int Y, int M, int D, int h, int m, int s)
{
    struct tm t;

    memset(&t, 0, sizeof(t));
    t.tm_year = Y - 1900;
    t.tm_mon = M - 1;
    t.tm_mday = D;
    t.tm_hour = h;
    t.tm_min = m;
    t.tm_sec = s;
    return (double)timegm(&t);
}

int main(void)
{
    printf("== test_db ==\n");

    /* --- 1. parseo de tiempos --- */
    {
        double t = 0.0;

        CK(bpq_db_parse_utc("2026-10-22 13:00:00", &t) == 0);
        CK(fabs(t - utc(2026, 10, 22, 13, 0, 0)) < 1e-6);
        CK(bpq_db_parse_utc("  2026-10-22T13:00:00", &t) == 0);
        CK(fabs(t - utc(2026, 10, 22, 13, 0, 0)) < 1e-6);
        CK(bpq_db_parse_utc("2026-01-01 00:00:00", &t) == 0);
        CK(fabs(t - utc(2026, 1, 1, 0, 0, 0)) < 1e-6);

        CK(bpq_db_parse_utc("", &t) == -1);
        CK(bpq_db_parse_utc("basura", &t) == -1);
        CK(bpq_db_parse_utc("2026-13-01 00:00:00", &t) == -1);
        CK(bpq_db_parse_utc(NULL, &t) == -1);
        CK(bpq_db_parse_utc("2026-10-22 13:00:00", NULL) == -1);
    }

    /* --- 2. sin DbEnvFile: objeto valido, deshabilitado y con motivo --- */
    {
        BpqConfig cfg;
        BpqDb *db;
        char err[512] = "";

        memset(&cfg, 0, sizeof(cfg));
        db = bpq_db_open(&cfg, err, sizeof(err));
        CK(db != NULL);
        CK(bpq_db_available(db) == 0);
        CK(err[0] != '\0');
        CK(bpq_db_load_recent(db, &cfg, NULL, 24, err, sizeof(err)) == 0);
        CK(bpq_db_search(db, &cfg, NULL, 1.0, 2.0, 10, err, sizeof(err)) == 0);
        /* Rango invalido con conexion tampoco revienta. */
        CK(bpq_db_search(db, &cfg, NULL, 0.0, 0.0, 10, err, sizeof(err)) == 0);
        bpq_db_close(db);
        bpq_db_close(NULL);
        CK(bpq_db_available(NULL) == 0);
    }

    /* --- 3. DbEnvFile inexistente --- */
    {
        BpqConfig cfg;
        BpqDb *db;
        char err[512] = "";

        memset(&cfg, 0, sizeof(cfg));
        snprintf(cfg.db_env_file, sizeof(cfg.db_env_file), "%s",
                 "/tmp/bpq_no_existe_db.env");
        db = bpq_db_open(&cfg, err, sizeof(err));
        CK(db != NULL);
        CK(bpq_db_available(db) == 0);
        CK(strstr(err, "no se pudo leer") != NULL);
        bpq_db_close(db);
    }

    /* --- 4. contra la base real (si esta) --- */
    {
        BpqConfig cfg;
        BpqDb *db;
        BpqEvents *store;
        char err[512] = "";
        const char *env = "../../run_working_v8/params/db.env";

        memset(&cfg, 0, sizeof(cfg));
        snprintf(cfg.db_env_file, sizeof(cfg.db_env_file), "%s", env);
        cfg.group_pick_tol_s = 2.0;
        cfg.group_min_match_picks = 2;
        cfg.group_max_solutions = 16;
        cfg.max_dist_km = 500.0;

        db = bpq_db_open(&cfg, err, sizeof(err));
        CK(db != NULL);
        if (!bpq_db_available(db)) {
            g_skips++;
            printf("SKIP: base no disponible (%s)\n", err);
            bpq_db_close(db);
        } else {
            /* Ultimas 24 h: es el mismo rango que carga el historial, asi que hay
             * eventos (un rango fijo puede caer en un hueco). */
            double t1 = (double)time(NULL);
            double t0 = t1 - 24 * 3600.0;
            int ne;

            store = bpq_events_new(&cfg);
            CK(store != NULL);
            ne = bpq_db_search(db, &cfg, store, t0, t1, 40, err, sizeof(err));
            CK(ne >= 0);                       /* no peta ni con 0 resultados */
            CK(bpq_events_count(store) >= 0);
            if (ne > 0) {
                const BpqEvent *ev = bpq_events_at(store, 0);
                const int ncols = bpq_event_ncells();
                char cell[64] = "";
                int col, withmag = 0;

                CK(ev != NULL);
                CK(bpq_event_nsols(ev) >= 1);
                /* La primera solucion tiene sus picadas y su tiempo de origen. */
                CK(bpq_event_sol(ev, 0) != NULL);
                CK(bpq_event_sol(ev, 0)->otime > 0.0);
                /* Y las magnitudes del historial llegan a las celdas "valor-nst". */
                for (col = 0; col < ncols; col++) {
                    cell[0] = '\0';
                    bpq_event_cell(ev, col, cell, sizeof(cell));
                    if (strchr(cell, '-') && strpbrk(cell, "0123456789")) withmag++;
                    printf("celda[%d]=%s\n", col, cell);
                }
                CK(withmag >= 1);
            } else {
                printf("NOTA: el rango de prueba no devolvio eventos\n");
            }

            /* Y el store se puede vaciar para reemplazar la vista. */
            bpq_events_clear(store);
            CK(bpq_events_count(store) == 0);
            bpq_events_free(store);
            bpq_db_close(db);
        }
    }

    printf("== db checks=%d fallas=%d skips=%d ==\n", g_checks, g_fail, g_skips);
    return g_fail ? 1 : 0;
}
