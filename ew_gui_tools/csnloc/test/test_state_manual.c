/*
 * C4: la columna de origen (manual) sobrevive State_Save/State_Load, y las
 * lineas P antiguas (8 campos, sin origen) cargan como automaticas.
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
    return p;
}

int main(void)
{
    EventRegistry a, b;
    const char *path = "/tmp/opencode/csnloc_test_state_manual.events";
    FILE *fp;

    EventRegistry_Init(&a);
    a.ev[0].id = 555UL;
    a.ev[0].version = 2;
    a.ev[0].t0 = 1.9e9; a.ev[0].lat = -20.0; a.ev[0].lon = -70.0;
    a.ev[0].depth_km = 25.0; a.ev[0].nphases = 2;
    a.ev[0].last_update_epoch = 1.9e9;
    a.ev[0].phases[0] = mk("BO03", CSLOC_PHASE_P, 1.9e9 + 20.0, 0);
    a.ev[0].residual[0] = 0.1;
    a.ev[0].phases[1] = mk("GO01", CSLOC_PHASE_S, 1.9e9 + 40.0, 1);
    a.ev[0].residual[1] = -0.2;
    a.ev[0].nphases_stored = 2;
    a.ev[0].has_manual = 1;
    a.n = 1;

    CHECK(State_Save(path, &a) == 0, "State_Save ok");
    EventRegistry_Init(&b);
    CHECK(State_Load(path, &b) == 0, "State_Load ok");
    CHECK(b.n == 1, "1 evento");
    CHECK(b.ev[0].nphases_stored == 2, "2 fases");
    CHECK(b.ev[0].phases[0].manual == 0, "P automatica conservada");
    CHECK(b.ev[0].phases[1].manual == 1, "S manual conservada");
    CHECK(b.ev[0].has_manual == 1, "has_manual reconstruido");

    /* Compatibilidad: linea P antigua de 8 campos (sin origen). */
    fp = fopen(path, "w");
    if (!fp) { printf("FAIL: no se pudo escribir fixture\n"); return 1; }
    fprintf(fp, "E 777 1 1.9 0.0 0.0 0.0 1 0.0 0.0 0.0 0.0 0 1.9 1 1 0 1.9\n");
    fprintf(fp, "P BO03 C1 HHZ -- 0 1900000020.000 1 0.100\n");
    fclose(fp);
    EventRegistry_Init(&b);
    CHECK(State_Load(path, &b) == 0, "carga fixture antiguo");
    CHECK(b.n == 1 && b.ev[0].nphases_stored == 1, "fase antigua cargada");
    CHECK(b.ev[0].phases[0].manual == 0, "fase antigua -> automatica");
    CHECK(b.ev[0].has_manual == 0, "antiguo -> has_manual 0");

    if (fails) { printf("\n%d FALLOS\n", fails); return 1; }
    printf("\nOK test_state_manual\n");
    return 0;
}
