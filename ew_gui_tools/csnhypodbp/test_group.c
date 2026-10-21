/*
 * Test headless del motor de agrupacion (csnhypodbp_group.c):
 *  C7  tolerancia de tiempo y picadas comunes
 *  C8  encadenado por picadas comunes (union)
 *  C9  firma vacia / menos de min_match no fusionan entre qid distintos
 *  C10 mismo qid siempre agrupa
 *  C11 seleccion de la solucion preferida
 *  C12 solape sin subconjunto (regresion "sismo repetido")
 */
#include <stdio.h>
#include <string.h>
#include "csnhypodbp_group.h"

static int fails = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); fails++; } \
                              else printf("ok  : %s\n", msg); } while (0)

static CsnhypodbpPick mkp(const char *sta, char phase, double t)
{
    CsnhypodbpPick p; memset(&p, 0, sizeof(p));
    strncpy(p.sta, sta, sizeof(p.sta) - 1);
    p.phase = phase; p.t_pick = t;
    return p;
}

static void key_from(CsnhypodbpGroupKey *g, int qid,
                     const CsnhypodbpPick *p, int n)
{
    memset(g, 0, sizeof(*g));
    g->qid = qid;
    memcpy(g->picks, p, (size_t)n * sizeof(*p));
    g->npicks = n;
}

int main(void)
{
    /* --- C7: tolerancia 0.1 s --- */
    {
        CsnhypodbpPick a[2], b[2];
        a[0] = mkp("BO03", 'P', 1000.0);
        a[1] = mkp("GO01", 'S', 1010.0);
        b[0] = mkp("BO03", 'P', 1000.09);   /* dentro */
        b[1] = mkp("GO01", 'S', 1010.09);
        CHECK(csnhypodbp_picks_compatible(a, 2, b, 2, 0.1, 3) == 0,
              "C7: min_match no alcanzado (2<3) no fusiona");
        CHECK(csnhypodbp_picks_compatible(a, 2, b, 2, 0.1, 2) == 1,
              "C7: mismos tiempos (0.09) fusiona con min 2");

        b[0] = mkp("BO03", 'P', 1000.11);   /* fuera */
        CHECK(csnhypodbp_picks_compatible(a, 2, b, 2, 0.1, 2) == 0,
              "C7: 0.11 fuera de tolerancia no fusiona");
    }

    /* --- C8: subconjuntos encadenados --- */
    {
        CsnhypodbpPick A[1] = { mkp("BO03", 'P', 1000.0) };
        CsnhypodbpPick B[3] = { mkp("BO03", 'P', 1000.0),
                                mkp("GO01", 'P', 1010.0),
                                mkp("LC01", 'P', 1020.0) };
        CsnhypodbpPick C[3] = { mkp("BO03", 'P', 1000.0),
                                mkp("GO01", 'P', 1010.0),
                                mkp("MT01", 'P', 1030.0) };
        CsnhypodbpGroupKey grp[2];
        int out[4];
        int n;
        key_from(&grp[0], 10, B, 3);
        key_from(&grp[1], 20, C, 3);
        n = csnhypodbp_group_matches(grp, 2, 99, A, 1, 0.1, 1, out, 4);
        CHECK(n == 2, "C8: subconjunto A encaja en B y C");
        n = csnhypodbp_group_matches(grp, 2, 99, A, 1, 0.1, 2, out, 4);
        CHECK(n == 0, "C8: con min 2, A (1 pick) no fusiona");
    }

    /* --- C9: firma vacia / disjuntos --- */
    {
        CsnhypodbpPick B[2] = { mkp("BO03", 'P', 1000.0),
                                mkp("GO01", 'P', 1010.0) };
        CsnhypodbpPick D[2] = { mkp("ZZ01", 'P', 5000.0),
                                mkp("ZZ02", 'P', 5010.0) };
        CsnhypodbpGroupKey grp;
        int out[2];
        key_from(&grp, 10, B, 2);
        CHECK(csnhypodbp_group_matches(&grp, 1, 99, NULL, 0, 0.1, 1, out, 2) == 0,
              "C9: sin picadas no fusiona entre qid distintos");
        CHECK(csnhypodbp_group_matches(&grp, 1, 99, D, 2, 0.1, 1, out, 2) == 0,
              "C9: disjuntos no fusionan");
    }

    /* --- C10: mismo qid siempre agrupa --- */
    {
        CsnhypodbpPick B[2] = { mkp("BO03", 'P', 1000.0),
                                mkp("GO01", 'P', 1010.0) };
        CsnhypodbpPick D[2] = { mkp("ZZ01", 'P', 9000.0),
                                mkp("ZZ02", 'P', 9010.0) };
        CsnhypodbpGroupKey grp;
        int out[2];
        key_from(&grp, 42, B, 2);
        CHECK(csnhypodbp_group_matches(&grp, 1, 42, D, 2, 0.1, 1, out, 2) == 1,
              "C10: mismo qid agrupa aunque las picadas difieran");
    }

    /* --- C12: solape sin subconjunto (regresion "sismo repetido") --- */
    {
        /* Emision A: 6 P e incluye CO06. Emision B: 7 P, sin CO06 pero con
           LMEL y BO01. Solapan en 5 estaciones. Con el criterio viejo de
           subconjunto NO fusionaban y la maestra mostraba el mismo sismo dos
           veces (dos qid distintos que csnloc creo al re-detectar). */
        CsnhypodbpPick A[6] = { mkp("CO06", 'P', 407.0), mkp("CO03", 'P', 410.0),
                                mkp("VA01", 'P', 415.0), mkp("ROC1", 'P', 416.0),
                                mkp("MT07", 'P', 416.0), mkp("VA03", 'P', 417.0) };
        CsnhypodbpPick B[7] = { mkp("CO03", 'P', 410.0), mkp("VA01", 'P', 415.0),
                                mkp("ROC1", 'P', 416.0), mkp("MT07", 'P', 416.0),
                                mkp("VA03", 'P', 417.0), mkp("LMEL", 'P', 432.0),
                                mkp("BO01", 'P', 434.0) };
        CsnhypodbpGroupKey grp;
        int out[4];
        key_from(&grp, 10, A, 6);
        CHECK(csnhypodbp_picks_overlap(A, 6, B, 7, 0.1) == 5,
              "C12: el solape cuenta 5 picadas");
        CHECK(csnhypodbp_picks_compatible(A, 6, B, 7, 0.1, 3) == 1,
              "C12: solape (sin subconjunto) fusiona");
        CHECK(csnhypodbp_group_matches(&grp, 1, 99, B, 7, 0.1, 3, out, 4) == 1,
              "C12: grupo compatible por solape");
        {
            CsnhypodbpPick C[3] = { mkp("CO03", 'P', 410.0),
                                    mkp("VA01", 'P', 415.0),
                                    mkp("ZZ99", 'P', 999.0) };
            CHECK(csnhypodbp_group_matches(&grp, 1, 99, C, 3, 0.1, 3, out, 4) == 0,
                  "C12: solape 2<3 no fusiona");
        }
    }

    /* --- C11: preferida --- */
    {
        CsnhypodbpSolMeta s[4];
        memset(s, 0, sizeof(s));
        /* sin manuales: gana la de mas fases; empate -> menor rms */
        s[0].nph = 5; s[0].rms = 0.3; s[0].version = 2;
        s[1].nph = 7; s[1].rms = 0.5; s[1].version = 1;
        s[2].nph = 7; s[2].rms = 0.2; s[2].version = 3;
        CHECK(csnhypodbp_preferred_index(s, 3) == 2,
              "C11: sin manual, mas fases y menor RMS");

        /* con manuales: la manual mas reciente gana aunque tenga menos fases */
        s[0].manual = 1; s[0].nph = 2; s[0].version = 4;
        s[3].manual = 1; s[3].nph = 3; s[3].version = 5;
        CHECK(csnhypodbp_preferred_index(s, 4) == 3,
              "C11: manual mas reciente es la preferida");
    }

    /* --- C11b: confirmadas en la BD --- */
    {
        CsnhypodbpSolMeta s[5];
        memset(s, 0, sizeof(s));

        /* Una confirmada vence a un manual no confirmado aunque este tenga mas
           fases, mejor RMS y version mayor. */
        s[0].manual = 1; s[0].nph = 20; s[0].rms = 0.05; s[0].version = 9;
        s[0].otime = 1000.0;
        s[1].manual = 1; s[1].confirmed = 1; s[1].nph = 2; s[1].rms = 0.9;
        s[1].version = 3; s[1].otime = 400.0; s[1].created = 50000.0;
        CHECK(csnhypodbp_preferred_index(s, 2) == 1,
              "C11b: la confirmada vence al manual no confirmado");

        /* Entre confirmadas manda el instante de confirmacion (created), no la
           version (contador de csnloc). */
        s[2].manual = 1; s[2].confirmed = 1; s[2].version = 4;
        s[2].otime = 410.0; s[2].created = 50010.0;
        s[3].manual = 1; s[3].confirmed = 1; s[3].version = 8;
        s[3].otime = 420.0; s[3].created = 50005.0;
        CHECK(csnhypodbp_preferred_index(s, 4) == 2,
              "C11b: la confirmada mas reciente (created) es la preferida");

        /* Empate de created: desempata la version mayor. */
        s[4].manual = 1; s[4].confirmed = 1; s[4].version = 9;
        s[4].otime = 430.0; s[4].created = 50010.0;
        CHECK(csnhypodbp_preferred_index(s, 5) == 4,
              "C11b: empate de created -> mayor version");
    }

    if (fails) { printf("\n%d FALLOS\n", fails); return 1; }
    printf("\nOK test_group\n");
    return 0;
}
