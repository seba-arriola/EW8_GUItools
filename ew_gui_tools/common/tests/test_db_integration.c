/*
 * Test de integración contra PostgreSQL real (AC3+AC4).
 * Se salta si no está seteada EWGUI_DB_TEST_ENV (ruta a db.env), para que
 * `make -C common check` siga siendo headless.
 *
 *   EWGUI_DB_TEST_ENV=run_working_v8/params/db.env build/test_db_integration
 */

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ewgui/db.h"

#define EID "E9999999999"
#define O1  "E9999999999:162"
#define O2  "E9999999999:163"

static void cleanup(EwGuiDb *db)
{
    ewguidb_exec(db, "DELETE FROM event WHERE event_id='" EID "'");
}

int main(void)
{
    const char *envfile = getenv("EWGUI_DB_TEST_ENV");
    if (!envfile || !*envfile) {
        printf("SKIP test_db_integration (EWGUI_DB_TEST_ENV no seteado)\n");
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

    /* --- event --- */
    EwGuiEventRow ev;
    memset(&ev, 0, sizeof ev);
    snprintf(ev.event_id, sizeof ev.event_id, "%s", EID);
    snprintf(ev.event_type, sizeof ev.event_type, "EARTHQUAKE");
    assert(ewguidb_upsert_event(db, &ev) == 0);

    /* --- origins (dos módulos) --- */
    EwGuiOriginRow o;
    memset(&o, 0, sizeof o);
    snprintf(o.origin_id, sizeof o.origin_id, "%s", O1);
    snprintf(o.event_id, sizeof o.event_id, "%s", EID);
    snprintf(o.origin_time, sizeof o.origin_time, "2026-10-10 12:00:00.000+00");
    o.latitude = -33.45;
    o.longitude = -70.66;
    o.depth = 12.5;
    snprintf(o.author, sizeof o.author, "csnloc");
    snprintf(o.software, sizeof o.software, "csnloc");
    snprintf(o.location_algorithm, sizeof o.location_algorithm, "csnloc");
    o.azimuthal_gap = 180.0;
    o.station_count = 8;
    o.standard_error = 0.35;
    snprintf(o.status, sizeof o.status, "AUTOMATIC");
    assert(ewguidb_upsert_origin(db, &o) == 0);

    snprintf(o.origin_id, sizeof o.origin_id, "%s", O2);
    snprintf(o.status, sizeof o.status, "AUTOMATIC");
    o.latitude = -33.46;
    assert(ewguidb_upsert_origin(db, &o) == 0);

    /* --- picks --- */
    EwGuiPickRow pk[2];
    memset(pk, 0, sizeof pk);
    snprintf(pk[0].pick_id, sizeof pk[0].pick_id, "%s/pick/GO01.P", O1);
    snprintf(pk[0].channel, sizeof pk[0].channel, "C1.GO01..HHZ");
    snprintf(pk[0].pick_time, sizeof pk[0].pick_time,
             "2026-10-10 12:00:05.120+00");
    snprintf(pk[0].phase, sizeof pk[0].phase, "P");
    pk[0].weight = 1.0;
    pk[0].residual = 0.25;      /* columna uncertainty */
    snprintf(pk[0].pick_status, sizeof pk[0].pick_status, "automatic");

    snprintf(pk[1].pick_id, sizeof pk[1].pick_id, "%s/pick/GO01.S", O1);
    snprintf(pk[1].channel, sizeof pk[1].channel, "C1.GO01..HHN");
    snprintf(pk[1].pick_time, sizeof pk[1].pick_time,
             "2026-10-10 12:00:09.900+00");
    snprintf(pk[1].phase, sizeof pk[1].phase, "S");
    pk[1].weight = 0.8;
    pk[1].residual = -0.30;
    snprintf(pk[1].pick_status, sizeof pk[1].pick_status, "manual");
    assert(ewguidb_replace_picks(db, O1, pk, 2) == 0);

    /* --- magnitude + station_magnitude --- */
    EwGuiMagRow mg;
    memset(&mg, 0, sizeof mg);
    snprintf(mg.magnitude_id, sizeof mg.magnitude_id, "%s/mag/ML", O1);
    snprintf(mg.origin_id, sizeof mg.origin_id, "%s", O1);
    mg.magnitude_value = 4.2;
    snprintf(mg.magnitude_type, sizeof mg.magnitude_type, "ML");
    mg.mag_uncertainty = 0.15;
    snprintf(mg.calculation_method, sizeof mg.calculation_method, "CSNNet");
    mg.station_count = 5;
    assert(ewguidb_upsert_magnitude(db, &mg) == 0);

    EwGuiStaMagRow sm[2];
    memset(sm, 0, sizeof sm);
    snprintf(sm[0].station_magnitude_id, sizeof sm[0].station_magnitude_id,
             "%s/C1.GO01..HHZ", mg.magnitude_id);
    snprintf(sm[0].magnitude_id, sizeof sm[0].magnitude_id, "%s",
             mg.magnitude_id);
    snprintf(sm[0].channel, sizeof sm[0].channel, "C1.GO01..HHZ");
    sm[0].read_amplitude = 1234.5;
    sm[0].calculated_magnitude = 4.1;
    snprintf(sm[0].amplitude_type, sizeof sm[0].amplitude_type, "AML");

    snprintf(sm[1].station_magnitude_id, sizeof sm[1].station_magnitude_id,
             "%s/C1.GO02..HHZ", mg.magnitude_id);
    snprintf(sm[1].magnitude_id, sizeof sm[1].magnitude_id, "%s",
             mg.magnitude_id);
    snprintf(sm[1].channel, sizeof sm[1].channel, "C1.GO02..HHZ");
    sm[1].read_amplitude = 900.0;
    sm[1].calculated_magnitude = 4.3;
    snprintf(sm[1].amplitude_type, sizeof sm[1].amplitude_type, "AML");
    assert(ewguidb_replace_station_magnitudes(db, mg.magnitude_id, sm, 2) == 0);

    /* --- tensor: mecanismo focal de la fase W (panel Mww -> Save to DB) --- */
    EwGuiTensorRow tn;
    memset(&tn, 0, sizeof tn);
    snprintf(tn.tensor_id, sizeof tn.tensor_id, "%s/tensor/WPHASE", O1);
    snprintf(tn.origin_id, sizeof tn.origin_id, "%s", O1);
    snprintf(tn.method, sizeof tn.method, "WPHASE");
    snprintf(tn.mechanism, sizeof tn.mechanism, "WCMT");
    tn.have_planes = 1;
    tn.strike1 = 209.4; tn.dip1 = 20.9; tn.rake1 = -46.4;
    tn.strike2 = 343.8; tn.dip2 = 75.0; tn.rake2 = -104.8;
    tn.m_rr = -1.112437e27; tn.m_tt = -3.869568e26; tn.m_pp = 1.499394e27;
    tn.m_rt =  2.920828e26; tn.m_rp = -1.626447e27; tn.m_tp = -2.521048e26;
    tn.scalar_moment = 1.35e27;
    assert(ewguidb_upsert_tensor(db, &tn) == 0);
    /* idempotente: el mismo tensor_id PISA la fila (no duplica) */
    tn.m_rr = -1.0e27;
    assert(ewguidb_upsert_tensor(db, &tn) == 0);
    /* sin planos nodales: van como NULL, no como 0 ni como "nan" (NaN -> NULL) */
    tn.have_planes = 0;
    assert(ewguidb_upsert_tensor(db, &tn) == 0);

    /* --- preferred --- */
    assert(ewguidb_set_preferred(db, EID, O2, mg.magnitude_id) == 0);
    /* Solo el origen: la magnitud preferida se conserva (hypodb la borraba al
       reiniciar, antes de ver ningun TYPE_MAGNITUDE). */
    assert(ewguidb_set_preferred_origin(db, EID, O1) == 0);

    /* --- lecturas --- */
    EwGuiEventRow *evs = NULL;
    int nev = 0;
    assert(ewguidb_query_recent_events(db, 24, &evs, &nev) >= 1);
    int found = -1;
    for (int i = 0; i < nev; i++)
        if (strcmp(evs[i].event_id, EID) == 0)
            found = i;
    assert(found >= 0);
    assert(strcmp(evs[found].preferred_origin_id, O1) == 0);
    assert(strcmp(evs[found].preferred_magnitude_id, mg.magnitude_id) == 0);
    ewguidb_rows_free(evs);

    EwGuiOriginRow *ors = NULL;
    int nor = 0;
    assert(ewguidb_query_origins(db, EID, &ors, &nor) == 2);
    /* El ORDER BY de la consulta es `creation_time DESC` (lo nuevo primero, que
       es el orden por defecto de la tabla *Origins*), asi que el arreglo NO viene
       en orden de insercion: se busca por id. */
    int i1 = -1, i2 = -1;
    for (int k = 0; k < nor; k++) {
        if (strcmp(ors[k].origin_id, O1) == 0) i1 = k;
        if (strcmp(ors[k].origin_id, O2) == 0) i2 = k;
    }
    assert(i1 >= 0 && i2 >= 0);
    assert(fabs(ors[i1].latitude + 33.45) < 1e-6);
    assert(ors[i1].station_count == 8);
    assert(fabs(ors[i1].standard_error - 0.35) < 1e-6);
    assert(strcmp(ors[i2].status, "AUTOMATIC") == 0);
    ewguidb_rows_free(ors);

    EwGuiPickRow *pks = NULL;
    int npk = 0;
    assert(ewguidb_query_picks(db, O1, &pks, &npk) == 2);
    assert(strcmp(pks[0].phase, "P") == 0);
    assert(strcmp(pks[0].pick_status, "automatic") == 0);
    assert(strcmp(pks[1].phase, "S") == 0);
    assert(strcmp(pks[1].pick_status, "manual") == 0);
    /* El residual viaja en `uncertainty`. */
    assert(fabs(pks[0].residual - 0.25) < 1e-6);
    assert(fabs(pks[1].residual - (-0.30)) < 1e-6);
    ewguidb_rows_free(pks);

    EwGuiMagRow *mgs = NULL;
    int nmg = 0;
    assert(ewguidb_query_magnitudes(db, O1, &mgs, &nmg) == 1);
    assert(strcmp(mgs[0].magnitude_type, "ML") == 0);
    assert(fabs(mgs[0].magnitude_value - 4.2) < 1e-6);
    ewguidb_rows_free(mgs);

    EwGuiStaMagRow *sms = NULL;
    int nsm = 0;
    assert(ewguidb_query_station_magnitudes(db, mg.magnitude_id, &sms, &nsm) == 2);
    assert(strcmp(sms[0].amplitude_type, "AML") == 0);
    ewguidb_rows_free(sms);

    /* --- replace_picks es idempotente (reemplaza) --- */
    pk[0].residual = NAN;       /* NaN -> SQL NULL */
    assert(ewguidb_replace_picks(db, O1, pk, 1) == 0);
    assert(ewguidb_query_picks(db, O1, &pks, &npk) == 1);
    assert(isnan(pks[0].residual));   /* NULL -> NaN */
    ewguidb_rows_free(pks);

    /* --- re-upsert de origin actualiza (no duplica) --- */
    snprintf(o.origin_id, sizeof o.origin_id, "%s", O1);
    o.depth = 15.0;
    assert(ewguidb_upsert_origin(db, &o) == 0);
    assert(ewguidb_query_origins(db, EID, &ors, &nor) == 2);
    i1 = -1;
    for (int k = 0; k < nor; k++)
        if (strcmp(ors[k].origin_id, O1) == 0)
            i1 = k;
    assert(i1 >= 0);
    assert(fabs(ors[i1].depth - 15.0) < 1e-6);
    assert(ors[i1].creation_time[0] != '\0');   /* default de la BD */
    ewguidb_rows_free(ors);

    cleanup(db);
    ewguidb_close(db);

    printf("ALL DB INTEGRATION TESTS PASSED\n");
    return 0;
}
