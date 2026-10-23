/* C4: historial de sismos (normalizacion de longitud y qid desde event_id). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ewgui/db.h"

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
    EwGuiMapEvent rows[3];
    SevEvent out[4];
    size_t n;
    SevConfig cfg;
    SevCore *core;

    memset(rows, 0, sizeof rows);
    rows[0].longitude = 190.0;
    rows[0].latitude = -20.0;
    rows[0].magnitude = 6.1;
    snprintf(rows[0].event_id, sizeof rows[0].event_id, "E123-20260101000000");
    rows[1].longitude = -70.5;
    rows[1].latitude = -33.0;
    rows[1].magnitude = 4.2;
    snprintf(rows[1].event_id, sizeof rows[1].event_id, "456");
    rows[2].longitude = 0.0;
    rows[2].latitude = 0.0;
    rows[2].magnitude = -1.0;
    snprintf(rows[2].event_id, sizeof rows[2].event_id, "E7-x");

    n = sev_events_from_rows(rows, 3, out, 4);
    CHECK(n == 3, "tres sismos");
    CHECK(out[0].lon == -170.0, "lon 190 -> -170");
    CHECK(out[0].qid == 123, "qid desde E123-...");
    CHECK(out[1].lon == -70.5, "lon ya normalizada");
    CHECK(out[1].qid == 456, "qid sin prefijo E");
    CHECK(out[2].qid == 7, "qid desde E7-x");

    /* Tope. */
    n = sev_events_from_rows(rows, 3, out, 1);
    CHECK(n == 1, "tope max=1");
    n = sev_events_from_rows(rows, 3, out, 0);
    CHECK(n == 0, "max=0");

    /* Sin BD: refresh no falla y deja 0. */
    memset(&cfg, 0, sizeof cfg);
    snprintf(cfg.db_env_file, sizeof cfg.db_env_file, "/no/existe/db.env");
    cfg.db_hours = 24;
    core = sev_core_new(&cfg);
    CHECK(core != NULL, "core sin .d (solo config en memoria)");
    if (core) {
        SevSnapshot snap;
        CHECK(sev_core_refresh(core) == 0, "refresh sin BD no falla");
        sev_core_snapshot(core, &snap);
        CHECK(snap.nev == 0, "sin BD no hay sismos");
        CHECK(sev_core_open_db(core) != 0, "db.env inexistente falla");
        CHECK(sev_core_db_error(core)[0] != '\0', "el error de BD se reporta");
        sev_core_free(core);
    }

    if (g_fail == 0)
        printf("OK: historial (lon, qid, tope y BD ausente)\n");
    return g_fail;
}
