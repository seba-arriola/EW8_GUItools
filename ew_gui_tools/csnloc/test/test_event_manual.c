/*
 * C2: EventRegistry_AddPhase marca has_manual cuando entra una picada manual,
 * incluso si es un re-pick con el mismo tiempo que una automatica.
 */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "../csnloc.h"

static int fails = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); fails++; } \
                              else printf("ok  : %s\n", msg); } while (0)

static Pick mk(const char *sta, int phase, double t, int manual)
{
    Pick p; memset(&p, 0, sizeof(p));
    strncpy(p.sta, sta, sizeof(p.sta)-1);
    strcpy(p.net, "C1"); strcpy(p.chan, "HHZ"); strcpy(p.loc, "--");
    p.phase = phase; p.t_epoch = t; p.weight = 1; p.manual = manual;
    strncpy(p.phase_name, (phase == CSLOC_PHASE_S) ? "S" : "P",
            sizeof(p.phase_name)-1);
    return p;
}

int main(void)
{
    EventRecord e;
    CSLocParams cfg;

    memset(&e, 0, sizeof(e));
    memset(&cfg, 0, sizeof(cfg));

    /* Solo automaticas: has_manual = 0. */
    {
        Pick a = mk("BO03", CSLOC_PHASE_P, 1000.0, 0);
        CHECK(EventRegistry_AddPhase(&e, &a, &cfg) == 1, "fase automatica");
        CHECK(e.has_manual == 0, "sin manual -> has_manual 0");
    }

    /* Fase manual nueva: has_manual = 1. */
    {
        Pick b = mk("GO01", CSLOC_PHASE_P, 1010.0, 1);
        CHECK(EventRegistry_AddPhase(&e, &b, &cfg) == 1, "fase manual");
        CHECK(e.has_manual == 1, "manual -> has_manual 1");
    }

    /* Re-pick con el MISMO tiempo pero pasando a manual: debe marcar y contar. */
    {
        EventRecord e2; memset(&e2, 0, sizeof(e2));
        Pick a = mk("BO03", CSLOC_PHASE_P, 2000.0, 0);
        Pick b = mk("BO03", CSLOC_PHASE_P, 2000.0, 1);
        CHECK(EventRegistry_AddPhase(&e2, &a, &cfg) == 1, "auto base");
        CHECK(e2.has_manual == 0, "auto base sin manual");
        CHECK(EventRegistry_AddPhase(&e2, &b, &cfg) == 1,
              "mismo tiempo auto->manual cuenta como cambio");
        CHECK(e2.has_manual == 1, "auto->manual marca has_manual");
        CHECK(e2.phases[0].manual == 1, "picada quedo manual");
    }

    if (fails) { printf("\n%d FALLOS\n", fails); return 1; }
    printf("\nOK test_event_manual\n");
    return 0;
}
