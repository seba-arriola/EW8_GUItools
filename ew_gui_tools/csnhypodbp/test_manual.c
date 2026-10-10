/*
 * test_manual - overlay local de picadas manuales (headless).
 * add, remove_station (todas las de esa estacion), remove_trace (solo ese
 * canal) y clear; sin tocar el ring.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "csnhypodbp_manual.h"

static void add(const char *sta, const char *chan, char ph, double t) {
    CsnhypodbpManualPick p;
    memset(&p, 0, sizeof(p));
    snprintf(p.sta, sizeof(p.sta), "%s", sta);
    snprintf(p.chan, sizeof(p.chan), "%s", chan);
    p.phase = ph; p.t = t;
    csnhypodbp_manual_add(&p);
}

int main(void)
{
    CsnhypodbpManualPick out[CSNHYPODBP_MANUAL_MAX];
    int n;

    csnhypodbp_manual_clear();
    assert(csnhypodbp_manual_count() == 0);

    add("GO01", "HHZ", 'P', 100.0);
    add("GO01", "HHN", 'S', 110.0);
    add("GO02", "HHZ", 'P', 120.0);
    assert(csnhypodbp_manual_count() == 3);

    /* remove_trace: solo GO01.HHZ */
    assert(csnhypodbp_manual_remove_trace("GO01", "HHZ") == 1);
    n = csnhypodbp_manual_list(out, CSNHYPODBP_MANUAL_MAX);
    assert(n == 2);
    assert(!(strcmp(out[0].sta, "GO01") == 0 && strcmp(out[0].chan, "HHZ") == 0));

    /* remove_station: todas las de GO01 (queda GO01.HHN) */
    assert(csnhypodbp_manual_remove_station("GO01") == 1);
    assert(csnhypodbp_manual_count() == 1);

    /* remove_trace de algo inexistente -> 0 */
    assert(csnhypodbp_manual_remove_trace("XX", "HHZ") == 0);

    csnhypodbp_manual_clear();
    assert(csnhypodbp_manual_count() == 0);

    printf("test_manual OK\n");
    return 0;
}
