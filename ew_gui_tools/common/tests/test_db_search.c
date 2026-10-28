/*
 * Test de integración de la búsqueda hipocentral por rangos
 * (ewguidb_search_events, criterio C1). Se salta si no está seteada
 * EWGUI_DB_TEST_ENV (ruta a db.env), para que `make -C common check` siga
 * siendo headless.
 *
 *   EWGUI_DB_TEST_ENV=run_working_v8/params/db.env build/test_db_search
 *
 * Usa eventos sintéticos en 2030-2031, fuera del catálogo restaurado (que
 * termina en 2025-03-31), para que los conteos sean deterministas.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ewgui/db.h"

#define EA "S9000000001"
#define EB "S9000000002"
#define EC "S9000000003"

static void cleanup(EwGuiDb *db)
{
    ewguidb_exec(db,
        "DELETE FROM event WHERE event_id IN ('" EA "','" EB "','" EC "')");
}

static void add_event(EwGuiDb *db, const char *eid, const char *oid,
                      const char *t, double lat, double lon, double depth)
{
    EwGuiEventRow ev;
    memset(&ev, 0, sizeof ev);
    snprintf(ev.event_id, sizeof ev.event_id, "%s", eid);
    snprintf(ev.event_type, sizeof ev.event_type, "EARTHQUAKE");
    assert(ewguidb_upsert_event(db, &ev) == 0);

    EwGuiOriginRow o;
    memset(&o, 0, sizeof o);
    snprintf(o.origin_id, sizeof o.origin_id, "%s", oid);
    snprintf(o.event_id, sizeof o.event_id, "%s", eid);
    snprintf(o.origin_time, sizeof o.origin_time, "%s", t);
    o.latitude = lat;
    o.longitude = lon;
    o.depth = depth;
    snprintf(o.status, sizeof o.status, "AUTOMATIC");
    assert(ewguidb_upsert_origin(db, &o) == 0);
}

static void add_mag(EwGuiDb *db, const char *eid, const char *oid,
                    const char *mid, double value)
{
    EwGuiMagRow mg;
    memset(&mg, 0, sizeof mg);
    snprintf(mg.magnitude_id, sizeof mg.magnitude_id, "%s", mid);
    snprintf(mg.origin_id, sizeof mg.origin_id, "%s", oid);
    mg.magnitude_value = value;
    snprintf(mg.magnitude_type, sizeof mg.magnitude_type, "ML");
    assert(ewguidb_upsert_magnitude(db, &mg) == 0);
    assert(ewguidb_set_preferred(db, eid, oid, mid) == 0);
}

int main(void)
{
    const char *envfile = getenv("EWGUI_DB_TEST_ENV");
    if (!envfile || !*envfile) {
        printf("SKIP test_db_search (EWGUI_DB_TEST_ENV no seteado)\n");
        return 0;
    }

    EwGuiDbConfig cfg;
    if (ewguidb_config_load(envfile, &cfg) != 0) {
        fprintf(stderr, "no se pudo cargar %s\n", envfile);
        return 1;
    }

    EwGuiDb *db = NULL;
    if (ewguidb_open(&cfg, &db) != 0) {
        fprintf(stderr, "no conecta: %s\n", ewguidb_last_error(db));
        ewguidb_close(db);
        return 1;
    }

    cleanup(db);

    /*
     * A: 2030-01-01, lat -33, lon -70, depth 10,  ML 4.0
     * B: 2030-06-01, lat -20, lon -68, depth 100, ML 5.5
     * C: 2031-01-01, lat -33, lon -70, depth 30,  sin magnitud preferida
     */
    add_event(db, EA, EA ":162", "2030-01-01 12:00:00+00", -33.0, -70.0, 10.0);
    add_mag(db, EA, EA ":162", EA ":162/ML", 4.0);
    add_event(db, EB, EB ":162", "2030-06-01 12:00:00+00", -20.0, -68.0, 100.0);
    add_mag(db, EB, EB ":162", EB ":162/ML", 5.5);
    add_event(db, EC, EC ":162", "2031-01-01 12:00:00+00", -33.0, -70.0, 30.0);
    assert(ewguidb_set_preferred(db, EC, EC ":162", NULL) == 0);

    EwGuiMapEvent *rows = NULL;
    int n = 0;
    EwGuiSearchFilter f;
    memset(&f, 0, sizeof f);
    f.time_min = "2030-01-01 00:00:00+00";
    f.time_max = "2031-12-31 23:59:59+00";

    /* 1) rango amplio -> 3, orden DESC por origin_time (C, B, A) */
    assert(ewguidb_search_events(db, &f, &rows, &n) == 3);
    assert(strcmp(rows[0].event_id, EC) == 0);
    assert(strcmp(rows[1].event_id, EB) == 0);
    assert(strcmp(rows[2].event_id, EA) == 0);
    ewguidb_rows_free(rows);

    /* 2) subrango temporal -> 2 (B, A) */
    f.time_max = "2030-12-31 23:59:59+00";
    assert(ewguidb_search_events(db, &f, &rows, &n) == 2);
    assert(strcmp(rows[0].event_id, EB) == 0);
    assert(strcmp(rows[1].event_id, EA) == 0);
    ewguidb_rows_free(rows);

    /* 3) rango de latitud -> A y C */
    f.time_max = "2031-12-31 23:59:59+00";
    f.have_lat = 1; f.lat_min = -34.0; f.lat_max = -32.0;
    assert(ewguidb_search_events(db, &f, &rows, &n) == 2);
    ewguidb_rows_free(rows);
    f.have_lat = 0;

    /* 4) rango de profundidad -> A y C */
    f.have_depth = 1; f.depth_min = 5.0; f.depth_max = 50.0;
    assert(ewguidb_search_events(db, &f, &rows, &n) == 2);
    ewguidb_rows_free(rows);
    f.have_depth = 0;

    /* 5) rango de magnitud -> solo B (C no tiene magnitud preferida) */
    f.have_mag = 1; f.mag_min = 4.5; f.mag_max = 6.0;
    assert(ewguidb_search_events(db, &f, &rows, &n) == 1);
    assert(strcmp(rows[0].event_id, EB) == 0);
    ewguidb_rows_free(rows);
    f.have_mag = 0;

    /* 6) límite -> 1 (el más reciente: C) */
    f.limit = 1;
    assert(ewguidb_search_events(db, &f, &rows, &n) == 1);
    assert(strcmp(rows[0].event_id, EC) == 0);
    ewguidb_rows_free(rows);
    f.limit = 0;

    /* 7) fechas obligatorias */
    f.time_min = NULL;
    assert(ewguidb_search_events(db, &f, &rows, &n) == -1);
    f.time_min = "2030-01-01 00:00:00+00";

    /* 8) rango invertido -> error */
    f.have_lat = 1; f.lat_min = -30.0; f.lat_max = -34.0;
    assert(ewguidb_search_events(db, &f, &rows, &n) == -1);
    f.have_lat = 0;

    cleanup(db);
    ewguidb_close(db);

    printf("ALL DB SEARCH TESTS PASSED\n");
    return 0;
}
