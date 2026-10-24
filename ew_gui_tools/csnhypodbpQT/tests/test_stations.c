/*
 * test_stations.c - Catalogo de estaciones + tabla de picadas (fase 3).
 *
 * Verifica, sin GUI ni EarthWorm:
 *   - la lectura del catalogo (StaFile con 8 campos) y su recuento;
 *   - la distancia haversine en grados y el orden por distancia de las filas;
 *   - que la picada MANUAL gane a la automatica de la misma fase (M > A > Teo),
 *     con el residual de la elegida;
 *   - los bordes: sin solucion, sin StaFile, archivo inexistente y tope `max`.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bpq_capi.h"
#include "csnhypodbp_tt.h"

static int g_fail = 0;
static int g_checks = 0;

#define CK(cond) do {                                                          \
    g_checks++;                                                                \
    if (!(cond)) {                                                             \
        g_fail++;                                                              \
        fprintf(stderr, "FALLA %s:%d  %s\n", __FILE__, __LINE__, #cond);        \
    }                                                                          \
} while (0)

/* S3 va ANTES que S2 a proposito: el orden final debe ser por distancia. */
static int write_cat(const char *path)
{
    FILE *f = fopen(path, "w");

    if (!f) return -1;
    fprintf(f, "S1 C HHZ -- -30.00000 -70.00000 1000.0 1.0e+09\n");
    fprintf(f, "S3 C HHZ -- -28.00000 -70.00000 1000.0 1.0e+09\n");
    fprintf(f, "S2 C HHZ -- -31.00000 -70.00000 1000.0 1.0e+09\n");
    fclose(f);
    return 0;
}

static void set_pick(CsnhypodbpPick *p, const char *sta, char phase, int manual,
                     double t, double resid)
{
    memset(p, 0, sizeof(*p));
    snprintf(p->sta, sizeof(p->sta), "%s", sta);
    snprintf(p->chan, sizeof(p->chan), "%s", "HHZ");
    snprintf(p->net, sizeof(p->net), "%s", "C");
    snprintf(p->loc, sizeof(p->loc), "%s", "--");
    p->phase = phase;
    p->manual = manual;
    p->t_pick = t;
    p->residual = resid;
    p->weight = 0.0;
}

int main(void)
{
    const char *path = "/tmp/bpq_test_estaciones.txt";
    BpqConfig cfg;
    BpqStations *cat;
    CsnhypodbpStaRow rows[16];
    CsnhypodbpSolution sol;
    char err[256] = "";
    int n;

    printf("== test_stations ==\n");
    CK(write_cat(path) == 0);

    memset(&cfg, 0, sizeof(cfg));
    snprintf(cfg.sta_file, sizeof(cfg.sta_file), "%s", path);
    /* P teorica: el test corre con cwd=params (tablas iasp91.*). */
    snprintf(cfg.tau_model, sizeof(cfg.tau_model), "%s", "iasp91");
    CK(bpq_init_tt(&cfg, err, sizeof(err)) == 1);
    CK(csnhypodbp_tt_ready() == 1);
    {
        BpqConfig bad;
        char terr[128] = "";

        memset(&bad, 0, sizeof(bad));
        snprintf(bad.tau_model, sizeof(bad.tau_model), "%s", "no_existe_iasp91");
        CK(bpq_init_tt(&bad, terr, sizeof(terr)) == 0);
        CK(terr[0] != '\0');
        CK(bpq_init_tt(&cfg, err, sizeof(err)) == 1);
    }
    cat = bpq_stations_open(&cfg, err, sizeof(err));
    CK(cat != NULL);
    CK(err[0] == '\0');
    CK(bpq_stations_count(cat) == 3);

    /* Solucion en (-30,-70): S1 al epicentro, S2 a 1 grado, S3 a 2 grados.
     * Dos picadas P para S2 (auto y manual) para probar la precedencia. */
    memset(&sol, 0, sizeof(sol));
    sol.lat = -30.0;
    sol.lon = -70.0;
    sol.depth = 33.0;
    sol.otime = 1000000.0;
    sol.npicks = 3;
    set_pick(&sol.picks[0], "S2", 'P', 0, 1000012.0, NAN);   /* automatica: sin residual */
    set_pick(&sol.picks[1], "S2", 'P', 1, 1000012.5, -0.4);  /* manual: gana y aporta resid */
    set_pick(&sol.picks[2], "S3", 'S', 0, 1000020.0, 1.2);   /* automatica */

    n = bpq_stations_build(cat, &sol, rows, 16);
    CK(n == 3);

    /* Orden por distancia: 0, 1 grado y 2 grados (1 deg = 111.19 km). */
    CK(strcmp(rows[0].sta, "S1") == 0);
    CK(strcmp(rows[1].sta, "S2") == 0);
    CK(strcmp(rows[2].sta, "S3") == 0);
    CK(fabs(rows[0].dist_km) < 0.01);
    CK(fabs(rows[1].dist_km - 111.19) < 0.5);
    CK(fabs(rows[2].dist_km - 222.38) < 0.5);
    CK(fabs(rows[1].dist_deg - 1.0) < 0.001);

    /* M > A: la manual de S2 desplaza a la automatica. El residual sale de la
     * primera picada no-NaN de esa estacion y fase (csnhypodbp_statable.c:84). */
    CK(rows[1].p.present == 1);
    CK(rows[1].p.origin == 'M');
    CK(fabs(rows[1].p.resid + 0.4) < 0.01);
    CK(rows[1].s.present == 0);

    /* S3 solo tiene S automatica: la P teorica entra como fallback M>A>Teo.
     * S1 no tiene ninguna picada, pero tambien recibe la teorica (si el
     * modelo devuelve un tiempo positivo). */
    CK(rows[2].s.present == 1);
    CK(rows[2].s.origin == 'A');
    CK(rows[2].p.present == 1);
    CK(rows[2].p.origin == 'T');
    {
        const double tp = csnhypodbp_tt_p(rows[2].dist_deg, sol.depth);

        CK(tp > 0.0);
        CK(fabs(rows[2].p.t - (sol.otime + tp)) < 1e-6);
        CK(strcmp(rows[2].p.chan, "HHZ") == 0);
    }
    CK(rows[0].p.origin != 'M' && rows[0].p.origin != 'A');
    if (rows[0].p.present) {
        CK(rows[0].p.origin == 'T');
        CK(rows[0].p.t > 0.0);
    }
    CK(rows[0].s.present == 0);

    /* Sin solucion se listan las estaciones igual (todo sin picada). */
    n = bpq_stations_build(cat, NULL, rows, 16);
    CK(n == 3);
    CK(rows[0].p.present == 0 && rows[0].s.present == 0);

    /* Tope de filas y argumentos invalidos. */
    CK(bpq_stations_build(cat, &sol, rows, 2) == 2);
    CK(bpq_stations_build(cat, &sol, rows, 0) == 0);
    CK(bpq_stations_build(NULL, &sol, rows, 16) == 0);
    CK(bpq_stations_count(NULL) == 0);
    bpq_stations_close(NULL);

    /* Catalogo vacio / inexistente: objeto valido, 0 estaciones y motivo. */
    {
        BpqConfig c2;
        BpqStations *k;

        memset(&c2, 0, sizeof(c2));
        k = bpq_stations_open(&c2, err, sizeof(err));
        CK(k != NULL);
        CK(bpq_stations_count(k) == 0);
        CK(err[0] != '\0');
        CK(bpq_stations_build(k, &sol, rows, 16) == 0);
        bpq_stations_close(k);

        memset(&c2, 0, sizeof(c2));
        snprintf(c2.sta_file, sizeof(c2.sta_file), "%s", "/tmp/bpq_no_existe.txt");
        k = bpq_stations_open(&c2, err, sizeof(err));
        CK(k != NULL);
        CK(bpq_stations_count(k) == 0);
        CK(strstr(err, "no se pudo abrir") != NULL);
        bpq_stations_close(k);
    }

    bpq_stations_close(cat);
    remove(path);
    printf("== stations checks=%d fallas=%d ==\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
