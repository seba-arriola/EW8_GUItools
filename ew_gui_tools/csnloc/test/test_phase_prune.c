/*
 * C13/C14: poda por calidad de fases.
 *
 * - Una fase con residual > umbral se poda si la solucion nueva es mejor.
 * - La primera estacion solo se poda con residual grosero (2x umbral).
 * - Una fase podada no se re-incorpora si vuelve identica.
 */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "../csnloc.h"

static int fails = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); fails++; } \
                              else printf("ok  : %s\n", msg); } while (0)

static Pick mk(const char *sta, int phase, double t)
{
    Pick p; memset(&p, 0, sizeof(p));
    strncpy(p.sta, sta, sizeof(p.sta)-1);
    strcpy(p.net, "C"); strcpy(p.chan, "BHZ"); strcpy(p.loc, "--");
    p.phase = phase; p.t_epoch = t; p.weight = 1;
    return p;
}

int main(void)
{
    CSLocParams cfg;
    EventRecord e;
    Pick p;
    int removed;

    memset(&cfg, 0, sizeof(cfg));
    cfg.PhaseResidualMaxSec = 3.0;
    cfg.PhaseResidualMaxSecS = 5.0;

    memset(&e, 0, sizeof(e));

    /* 4 fases: la 1a (protegida) con residual 4.0, la 2a con 10.0 (poda),
       la 3a con 1.0 (ok), la 4a con 6.0 (poda). */
    e.phases[0] = mk("BO03", CSLOC_PHASE_P, 100.0); e.residual[0] = 4.0;
    e.phases[1] = mk("GO01", CSLOC_PHASE_P, 110.0); e.residual[1] = 10.0;
    e.phases[2] = mk("MT01", CSLOC_PHASE_P, 120.0); e.residual[2] = 1.0;
    e.phases[3] = mk("VA01", CSLOC_PHASE_P, 130.0); e.residual[3] = 6.0;
    e.nphases_stored = 4;

    removed = EventRegistry_PrunePhases(&e, &cfg);
    CHECK(removed == 2, "C13: poda 2 fases con residual alto");
    CHECK(e.nphases_stored == 2, "quedan 2 fases");
    CHECK(strcmp(e.phases[0].sta, "BO03") == 0,
          "primera estacion protegida (residual 4.0 < 2x3.0)");
    CHECK(strcmp(e.phases[1].sta, "MT01") == 0, "fase buena conservada");
    CHECK(e.npruned == 2, "2 fases en la lista de podadas");

    /* C14: la fase podada no se re-incorpora si vuelve identica. */
    p = mk("GO01", CSLOC_PHASE_P, 110.0);
    CHECK(EventRegistry_AddPhase(&e, &p, &cfg) == 0,
          "C14: fase podada identica no se re-incorpora");

    /* Pero un re-pick con tiempo distinto si entra. */
    p = mk("GO01", CSLOC_PHASE_P, 111.5);
    CHECK(EventRegistry_AddPhase(&e, &p, &cfg) == 1,
          "re-pick con tiempo distinto si se incorpora");

    if (fails) { printf("\n%d FALLOS\n", fails); return 1; }
    printf("\nOK test_phase_prune\n");
    return 0;
}
