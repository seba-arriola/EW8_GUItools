/* C5: core (estaciones, BD ausente, anillo invalido y snapshot). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "sev_capi.h"

static int g_fail = 0;

#define CHECK(cond, what)                                                       \
    do {                                                                        \
        if (!(cond)) {                                                          \
            printf("FAIL: %s (%s:%d)\n", what, __FILE__, __LINE__);             \
            g_fail = 1;                                                         \
        }                                                                       \
    } while (0)

int main(void)
{
    char dir[256], path[600], cmd[400];
    FILE *f;
    SevConfig cfg;
    SevCore *core;
    SevSnapshot snap;

    snprintf(dir, sizeof dir, "/tmp/sev_core_XXXXXX");
    if (!mkdtemp(dir)) {
        printf("FAIL: mkdtemp\n");
        return 1;
    }
    snprintf(path, sizeof path, "%s/sta.txt", dir);
    f = fopen(path, "wb");
    if (f) {
        fprintf(f, "AAA C HHZ -- -30.0 -70.0 100.0 1.0\n");
        fprintf(f, "BBB C HHZ -- -33.0 -71.0 500.0 1.0\n");
        fclose(f);
    }

    memset(&cfg, 0, sizeof cfg);
    snprintf(cfg.my_module, sizeof cfg.my_module, "MOD_STAEVDISP");
    snprintf(cfg.ring, sizeof cfg.ring, "NO_EXISTE_RING_CSNSTAEVDISP");
    snprintf(cfg.sta_file, sizeof cfg.sta_file, "%s", path);
    cfg.heart_beat_int = 30;
    cfg.db_hours = 24;
    cfg.station_size = 15.0;

    core = sev_core_new(&cfg);
    CHECK(core != NULL, "core nuevo");
    if (core) {
        sev_core_snapshot(core, &snap);
        CHECK(snap.nsta == 2, "dos estaciones cargadas");
        CHECK(snap.elev_min == 100.0 && snap.elev_max == 500.0,
              "rango de elevacion del snapshot");
        CHECK(snap.nev == 0, "sin sismos al arrancar");
        CHECK(snap.db_ok == 0, "sin BD");

        CHECK(sev_core_refresh(core) == 0, "refresh sin BD no falla");
        sev_core_snapshot(core, &snap);
        CHECK(snap.nev == 0, "refresh sin BD deja 0 sismos");

        CHECK(sev_core_db_error(core)[0] == '\0',
              "sin DbEnvFile no hay error de BD");
        CHECK(sev_core_should_quit(core) == 0, "no debe salir");

        if (getenv("EW_PARAMS")) {
            CHECK(sev_core_attach_ring(core) != 0,
                  "anillo inexistente debe fallar");
        } else {
            printf("SKIP: sin EW_PARAMS no se prueba GetKey\n");
        }
        CHECK(sev_core_background(core, 1000.0) == 0,
              "sin anillo, background no hace nada");
        sev_core_free(core);
    }

    snprintf(cmd, sizeof cmd, "rm -rf '%s'", dir);
    if (system(cmd) != 0)
        printf("nota: no se pudo limpiar %s\n", dir);

    if (g_fail == 0)
        printf("OK: core (estaciones, BD ausente y anillo invalido)\n");
    return g_fail;
}
