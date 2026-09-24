/*
 * C3: persistencia y recuperacion del registro de eventos.
 *
 * Guarda un registro con fases y lo vuelve a cargar; verifica que id,
 * version, hipocentro y fases se conservan.
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
    EventRegistry a, b;
    const char *path = "/tmp/opencode/csnloc_test_state.events";

    EventRegistry_Init(&a);
    a.ev[0].id = 1234567890UL;
    a.ev[0].version = 3;
    a.ev[0].t0 = 1.8e9; a.ev[0].lat = -23.5; a.ev[0].lon = -70.0;
    a.ev[0].depth_km = 30.0; a.ev[0].nphases = 2;
    a.ev[0].rms_sec = 0.4; a.ev[0].gap_deg = 120.0; a.ev[0].dmin_km = 50.0;
    a.ev[0].score = 1.0; a.ev[0].grid_level = GRID_LEVEL_LOCAL;
    a.ev[0].last_update_epoch = 1.8e9 + 10.0; a.ev[0].emitted = 1;
    a.ev[0].phases[0] = mk("BO03", CSLOC_PHASE_P, 1.8e9 + 20.0);
    a.ev[0].residual[0] = 0.1;
    a.ev[0].phases[1] = mk("GO01", CSLOC_PHASE_S, 1.8e9 + 40.0);
    a.ev[0].residual[1] = -0.2;
    a.ev[0].nphases_stored = 2;
    a.ev[0].pruned[0] = mk("XX01", CSLOC_PHASE_P, 1.8e9 + 25.0);
    a.ev[0].npruned = 1;
    a.n = 1;

    CHECK(State_Save(path, &a) == 0, "State_Save ok");

    EventRegistry_Init(&b);
    CHECK(State_Load(path, &b) == 0, "State_Load ok");
    CHECK(b.n == 1, "1 evento recuperado");
    CHECK(b.ev[0].id == 1234567890UL, "id conservado");
    CHECK(b.ev[0].version == 3, "version conservada");
    CHECK(fabs(b.ev[0].lat - (-23.5)) < 1e-6, "lat conservada");
    CHECK(b.ev[0].nphases_stored == 2, "2 fases conservadas");
    CHECK(strcmp(b.ev[0].phases[0].sta, "BO03") == 0, "fase 0 conservada");
    CHECK(strcmp(b.ev[0].phases[1].sta, "GO01") == 0, "fase 1 conservada");
    CHECK(b.ev[0].phases[1].phase == CSLOC_PHASE_S, "fase S conservada");
    CHECK(b.ev[0].npruned == 1, "1 fase podada conservada");
    CHECK(strcmp(b.ev[0].pruned[0].sta, "XX01") == 0, "fase podada conservada");

    /* Cargar un archivo inexistente -> registro vacio, sin error. */
    EventRegistry_Init(&b);
    CHECK(State_Load("/tmp/opencode/no_existe_xyz.events", &b) == 0 && b.n == 0,
          "archivo inexistente -> registro vacio");

    if (fails) { printf("\n%d FALLOS\n", fails); return 1; }
    printf("\nOK test_state_recover\n");
    return 0;
}
