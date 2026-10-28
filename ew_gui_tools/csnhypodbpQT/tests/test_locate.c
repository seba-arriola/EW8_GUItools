/*
 * test_locate.c - Picadas efectivas que se mandan al localizador (fase 4).
 *
 * Verifica, sin GUI ni EarthWorm:
 *   - el ranking M > A > Teo de la tabla (una P y una S por estacion);
 *   - que los checks A/M/Teo y las casillas por fila (`pick_on`) filtren;
 *   - la puerta MaxDistKm (que es del `locate`, no de la tabla);
 *   - el orden por distancia de las filas y el sembrado de `base` (qid/lat/lon);
 *   - que cada picada salga con su COMP3/NET/LOC del catalogo aunque la solucion
 *     los traiga vacios (sin canal, HYPOINVERSE rechaza TODAS las fases).
 *
 * La cadena real (csnloc + refinador) NO se corre aqui: eso lo cubre el hook
 * headless `BPQ_MODE=--locate`.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bpq_capi.h"
#include "csnhypodbp_locate.h"

static int g_fail = 0;
static int g_checks = 0;

#define CK(cond) do {                                                          \
    g_checks++;                                                                \
    if (!(cond)) {                                                             \
        g_fail++;                                                              \
        fprintf(stderr, "FALLA %s:%d  %s\n", __FILE__, __LINE__, #cond);        \
    }                                                                          \
} while (0)

/* A al epicentro y B/C/D a 1, 2 y 5 grados (1 deg = 111.19 km). El orden del
 * archivo NO es el de distancia a proposito: el indice del catalogo (`idx`) y el
 * de la fila (ordenada por distancia) tienen que distinguirse. */
static int write_cat(const char *path)
{
    FILE *f = fopen(path, "w");

    if (!f) return -1;
    fprintf(f, "D C HHZ -- 5.00000 0.00000 1000.0 1.0e+09\n");
    fprintf(f, "B C HHZ -- 1.00000 0.00000 1000.0 1.0e+09\n");
    fprintf(f, "A C HHZ -- 0.00000 0.00000 1000.0 1.0e+09\n");
    fprintf(f, "C C HHZ -- 2.00000 0.00000 1000.0 1.0e+09\n");
    fclose(f);
    return 0;
}

static void set_pick(CsnhypodbpPick *p, const char *sta, char phase, int manual,
                     double t, const char *chan)
{
    memset(p, 0, sizeof(*p));
    snprintf(p->sta, sizeof(p->sta), "%s", sta);
    if (chan && chan[0]) {
        snprintf(p->chan, sizeof(p->chan), "%s", chan);
        snprintf(p->net, sizeof(p->net), "%s", "C");
        snprintf(p->loc, sizeof(p->loc), "%s", "--");
    }
    p->phase = phase;
    p->manual = manual;
    p->t_pick = t;
    p->residual = NAN;
    p->weight = 0.0;
}

/* Cuenta picadas de una fase y origen. */
static int count(const CsnhypodbpSolution *o, char phase, char origin)
{
    int i, n = 0;

    for (i = 0; i < o->npicks; i++) {
        if (o->picks[i].phase != phase) continue;
        if (origin && o->picks[i].manual != (origin == 'M' ? 1 : 0)) continue;
        n++;
    }
    return n;
}

int main(void)
{
    const char *path = "/tmp/bpq_test_locate_sta.txt";
    BpqConfig cfg;
    BpqStations *cat;
    CsnhypodbpStaRow rows[8];
    CsnhypodbpSolution sol, out;
    char err[256] = "";
    int n;

    printf("== test_locate ==\n");
    CK(write_cat(path) == 0);

    memset(&cfg, 0, sizeof(cfg));
    snprintf(cfg.sta_file, sizeof(cfg.sta_file), "%s", path);
    cfg.max_dist_km = 500.0;
    cat = bpq_stations_open(&cfg, err, sizeof(err));
    CK(cat != NULL);
    CK(bpq_stations_count(cat) == 4);

    /* P auto en A, P manual en B, P+S auto en C, S auto en D (con SCNL vacio
     * a proposito: lo tiene que completar el catalogo). */
    memset(&sol, 0, sizeof(sol));
    sol.qid = 42;
    sol.version = 163;
    sol.mod = 1;
    sol.otime = 1000000.0;
    sol.lat = 0.0;
    sol.lon = 0.0;
    sol.depth = 30.0;
    sol.npicks = 5;
    set_pick(&sol.picks[0], "A", 'P', 0, 1000012.0, "HHZ");
    set_pick(&sol.picks[1], "B", 'P', 1, 1000013.0, "HHZ");
    set_pick(&sol.picks[2], "C", 'P', 0, 1000018.0, "HHZ");
    set_pick(&sol.picks[3], "C", 'S', 0, 1000026.0, "HHE");
    set_pick(&sol.picks[4], "D", 'S', 0, 1000040.0, "");      /* sin canal */

    n = bpq_stations_build(cat, &sol, rows, 8);
    CK(n == 4);
    CK(strcmp(rows[0].sta, "A") == 0);
    CK(strcmp(rows[1].sta, "B") == 0);
    CK(strcmp(rows[2].sta, "C") == 0);
    CK(strcmp(rows[3].sta, "D") == 0);
    CK(rows[1].p.present == 1 && rows[1].p.origin == 'M');
    CK(rows[2].p.present == 1 && rows[2].s.present == 1);
    CK(rows[2].s.origin == 'A');
    CK(rows[3].p.present == 0 && rows[3].s.present == 1);

    /* --- 1. todos los origenes, sin puerta de distancia -------------------- */
    CK(bpq_locate_build_picks(cat, &sol, rows, n, NULL, 1, 1, 1, 0.0, &out) == 5);
    CK(out.npicks == 5);
    CK(out.nph == 5);
    CK(out.qid == 42 && out.version == 163);      /* base sembrada */
    CK(fabs(out.lat) < 1e-9 && fabs(out.depth - 30.0) < 1e-9);
    CK(count(&out, 'P', 0) == 3);
    CK(count(&out, 'P', 'M') == 1);
    CK(count(&out, 'S', 0) == 2);

    /* --- 2. checks de origen ---------------------------------------------- */
    CK(bpq_locate_build_picks(cat, &sol, rows, n, NULL, 0, 1, 1, 0.0, &out) == 1);
    CK(count(&out, 'P', 'M') == 1);
    CK(strcmp(out.picks[0].sta, "B") == 0);

    CK(bpq_locate_build_picks(cat, &sol, rows, n, NULL, 1, 0, 1, 0.0, &out) == 4);
    CK(bpq_locate_build_picks(cat, &sol, rows, n, NULL, 0, 0, 0, 0.0, &out) == 0);
    /* no hay teoricas: encender Teo no cambia nada */
    CK(bpq_locate_build_picks(cat, &sol, rows, n, NULL, 1, 1, 1, 0.0, &out) == 5);

    /* --- 3. puerta MaxDistKm (del envio, no de la tabla) ------------------- */
    CK(bpq_locate_build_picks(cat, &sol, rows, n, NULL, 1, 1, 1, 300.0, &out) == 4);
    CK(count(&out, 'S', 0) == 1);                 /* D (5 grados) queda fuera */
    CK(bpq_locate_build_picks(cat, &sol, rows, n, NULL, 1, 1, 1, 150.0, &out) == 2);
    CK(count(&out, 'S', 0) == 0);                 /* C (222 km) tambien queda fuera */
    CK(bpq_locate_build_picks(cat, &sol, rows, n, NULL, 1, 1, 1, 1.0, &out) == 1);
    CK(strcmp(out.picks[0].sta, "A") == 0);

    /* --- 4. casillas por fila (P y S independientes) ----------------------- */
    {
        int on[8][2];
        int i;

        for (i = 0; i < n; i++) on[rows[i].idx][0] = on[rows[i].idx][1] = 1;
        on[rows[2].idx][1] = 0;                   /* desmarcar la S de C */
        CK(bpq_locate_build_picks(cat, &sol, rows, n,
                                  (const int (*)[2])on, 1, 1, 1, 0.0, &out) == 4);
        CK(count(&out, 'S', 0) == 1);
        on[rows[0].idx][0] = 0;                   /* y la P de A */
        CK(bpq_locate_build_picks(cat, &sol, rows, n,
                                  (const int (*)[2])on, 1, 1, 1, 0.0, &out) == 3);
    }

    /* --- 4b. conteo por origen (preflight: "solo M" con 0 manuales) -------- */
    {
        int on[8][2];
        int i, cp = 0, cs = 0, ca = 0, cm = 0, ct = 0;

        for (i = 0; i < n; i++) on[rows[i].idx][0] = on[rows[i].idx][1] = 1;
        CK(bpq_locate_count_origins(rows, n, (const int (*)[2])on, 1, 1, 1,
                                    0.0, &cp, &cs, &ca, &cm, &ct) == 5);
        CK(cp == 3 && cs == 2 && cm == 1 && ca == 4 && ct == 0);
        CK(bpq_locate_count_origins(rows, n, (const int (*)[2])on, 0, 1, 0,
                                    0.0, &cp, &cs, &ca, &cm, &ct) == 1);
        CK(cp == 1 && cs == 0 && cm == 1);
        CK(bpq_locate_count_origins(rows, n, (const int (*)[2])on, 1, 0, 0,
                                    0.0, &cp, &cs, &ca, &cm, &ct) == 4);
        CK(cp == 2 && cs == 2 && ca == 4);        /* A P, C P, C S, D S */
        CK(bpq_locate_count_origins(rows, n, (const int (*)[2])on, 1, 1, 1,
                                    300.0, &cp, &cs, &ca, &cm, &ct) == 4);
        CK(cp == 3 && cs == 1 && ca == 3 && cm == 1);  /* la S de D fuera */
    }

    /* --- 5. SCNL completo desde el catalogo -------------------------------- */
    CK(bpq_locate_build_picks(cat, &sol, rows, n, NULL, 1, 1, 1, 0.0, &out) == 5);
    {
        int i;

        for (i = 0; i < out.npicks; i++) {
            CK(out.picks[i].chan[0] != '\0');
            CK(out.picks[i].net[0] != '\0');
            CK(out.picks[i].loc[0] != '\0');
        }
        /* D llego sin canal: se completa con el del catalogo (HHZ). */
        for (i = 0; i < out.npicks; i++) {
            if (strcmp(out.picks[i].sta, "D") != 0) continue;
            CK(strcmp(out.picks[i].chan, "HHZ") == 0);
        }
    }

    /* --- 6. bordes --------------------------------------------------------- */
    CK(bpq_locate_build_picks(cat, &sol, rows, 0, NULL, 1, 1, 1, 0.0, &out) == -1);
    CK(bpq_locate_build_picks(cat, NULL, rows, n, NULL, 1, 1, 1, 0.0, &out) == -1);
    CK(bpq_locate_build_picks(cat, &sol, NULL, n, NULL, 1, 1, 1, 0.0, &out) == -1);

    /* `bpq_locate_run` con argumentos invalidos: no toca el entorno ni el disco. */
    {
        BpqLocateOut lo;

        memset(&lo, 0xAA, sizeof(lo));
        CK(bpq_locate_run(&cfg, cat, &sol, NULL, NULL, 0, NULL, 1, 1, 1, -1.0,
                          NULL, 0, NULL, 0, 0u,
                          &lo, NULL, err, sizeof(err)) == 0);
        CK(err[0] != '\0');
        CK(lo.sol.npicks == 0);
    }

    /* --- 6b. rebase a la candidata: distancias y orden -------------------- */
    CK(bpq_stations_rebase(cat, rows, n, 1.0, 0.0) == n);
    CK(strcmp(rows[0].sta, "B") == 0 && fabs(rows[0].dist_deg) < 1e-6);
    CK(fabs(rows[1].dist_deg - 1.0) < 1e-6);
    CK(fabs(rows[2].dist_deg - 1.0) < 1e-6);
    CK(fabs(rows[3].dist_deg - 4.0) < 1e-6);
    CK(bpq_stations_rebase(cat, rows, n, 0.0, 0.0) == n);
    CK(strcmp(rows[0].sta, "A") == 0 && fabs(rows[0].dist_deg) < 1e-6);

    /* --- 6c. hypos.jsonl conserva el SCNL de la picada ------------------- */
    {
        const char *hp = "/tmp/bpq_test_hypos.jsonl";
        CsnhypodbpSolution hs;
        CsnhypodbpTrace tk[1];
        char buf[2048] = "";
        FILE *f;
        size_t rd;

        memset(&hs, 0, sizeof(hs));
        hs.qid = 42;
        hs.version = 1;
        hs.otime = 1000.0;
        hs.npicks = 2;
        hs.nph = 2;
        set_pick(&hs.picks[0], "A", 'P', 0, 1010.0, "HHZ");
        set_pick(&hs.picks[1], "B", 'P', 0, 1012.0, "");
        snprintf(hs.picks[0].net, sizeof(hs.picks[0].net), "PB");

        memset(tk, 0, sizeof(tk));
        snprintf(tk[0].sta, sizeof(tk[0].sta), "B");
        snprintf(tk[0].net, sizeof(tk[0].net), "CX");
        snprintf(tk[0].chan, sizeof(tk[0].chan), "HHZ");
        snprintf(tk[0].loc, sizeof(tk[0].loc), "--");

        CK(csnhypodbp_hypos_write(hp, &hs, tk, 1) == 0);
        f = fopen(hp, "r");
        CK(f != NULL);
        if (f) {
            rd = fread(buf, 1, sizeof(buf) - 1, f);
            buf[rd] = '\0';
            fclose(f);
        }
        /* A: la red de la picada manda (el trace no la tiene). B: sin SCNL, lo
         * completa el trace por estacion. Nunca "--" si hay un dato real. */
        CK(strstr(buf, "\"sta\":\"A\",\"net\":\"PB\",\"chan\":\"HHZ\"") != NULL);
        CK(strstr(buf, "\"sta\":\"B\",\"net\":\"CX\",\"chan\":\"HHZ\"") != NULL);
        remove(hp);
    }

    /* --- 7. magnitudes (fase 6): guardas sin trazas ------------------------ */
    {
        CsnhypodbpMagOut mo;
        char merr[128] = "";
        CsnhypodbpSolution b;

        memset(&b, 0, sizeof(b));
        CK(bpq_mag_run(NULL, &b, NULL, 0, NULL, 0, 0u, &mo, merr,
                       sizeof(merr)) == 0);
        CK(merr[0] != '\0');
        CK(bpq_mag_result_ok(&mo) == 0);
        snprintf(mo.mwp, sizeof(mo.mwp), "4.8-6");
        CK(bpq_mag_result_ok_mask(&mo, 1u << 1) == 1);
        CK(bpq_mag_result_ok_mask(&mo, 1u << 0) == 0);
        CK(bpq_mag_result_ok_mask(&mo, 0u) == 1);
    }

    /* --- 8. identidad offline: mod del localizador + qid/version de base --- */
    {
        CsnhypodbpLocator l;
        CsnhypodbpSolution b, o;

        memset(&l, 0, sizeof(l));
        snprintf(l.name, sizeof(l.name), "%s", "csnloc");
        snprintf(l.bin, sizeof(l.bin), "%s", "csnloc");
        l.mod = 162;
        CK(csnhypodbp_locator_mod(&l) == 162);
        l.mod = 163;
        CK(csnhypodbp_locator_mod(&l) == 163);
        l.mod = 164;
        CK(csnhypodbp_locator_mod(&l) == 164);
        l.mod = 0;
        CK(csnhypodbp_locator_mod(&l) == 0);

        memset(&b, 0, sizeof(b));
        b.qid = 42;
        b.version = 7;
        b.mod = 162;
        memset(&o, 0, sizeof(o));
        csnhypodbp_locate_identity(&b, &l, &o);   /* unknown: mod=0 */
        CK(o.qid == 42 && o.version == 7 && o.mod == 0);

        l.mod = 162;
        csnhypodbp_locate_identity(&b, &l, &o);
        CK(o.mod == 162);
        o.qid = 99;
        o.version = 3;
        o.mod = 0;
        l.mod = 163;
        csnhypodbp_locate_identity(&b, &l, &o);
        CK(o.mod == 163 && o.qid == 99 && o.version == 3);
    }

    bpq_stations_close(cat);
    printf("== locate checks=%d fallas=%d ==\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
