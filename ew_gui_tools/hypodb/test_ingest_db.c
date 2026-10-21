/*
 * Integración del ingester contra PostgreSQL real (ARC -> event/origin/pick y
 * TYPE_MAGNITUDE -> magnitude/station_magnitude). Se salta sin
 * EWGUI_DB_TEST_ENV (ruta a db.env).
 *
 *   EWGUI_DB_TEST_ENV=run_working_v8/params/db.env build/test_ingest_db
 */

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <rw_mag.h>

#include "ewgui/db.h"
#include "ewgui/arc_layout.h"
#include "hypodb_config.h"
#include "hypodb_ingest.h"

/* El ARC de build_arc() tiene hora origen 2026-10-10T12:00:00Z y el event_id
   lleva esa hora como sufijo (ver hypodb_ids.h). */
#define EID "E987654-20261010120000"

static void pad(char *dst, const char *s, int w)
{
    int i, len = (int)strlen(s);

    for (i = 0; i < w; i++)
        dst[i] = (i < len) ? s[i] : ' ';
}

static void add_phase(char *buf, const char *sta, const char *net,
                      const char *chan, char phase, const char *tstr,
                      const char *loc)
{
    char line[115];
    size_t off = strlen(buf);

    memset(line, ' ', 114);
    pad(&line[0], sta, 5);
    pad(&line[5], net, 2);
    line[8] = ' ';
    pad(&line[9], chan, 3);
    line[13] = ' ';
    line[14] = phase;
    line[15] = ' ';
    line[16] = (phase == 'S') ? '4' : '0';
    memcpy(&line[17], tstr, 17);
    if (phase == 'S') {
        memcpy(&line[41], tstr + 12, 5);
        line[46] = 'S';
        line[47] = ' ';
        line[49] = '0';
    }
    pad(&line[111], loc, 2);

    memcpy(buf + off, line, 114);
    buf[off + 114] = '\n';
    buf[off + 115] = '\0';
}

static void build_arc(char *out, int qid, int version, int manual)
{
    char line[200];
    char q[16], v[8];

    out[0] = '\0';
    memset(line, ' ', 197);
    line[197] = '\n';
    line[198] = '\0';
    memcpy(line, "2026101012000000", 16);
    memcpy(line + 16, "33", 2);
    line[18] = 'S';
    memcpy(line + 19, "4500", 4);
    memcpy(line + 23, "070", 3);
    line[26] = 'W';
    memcpy(line + 27, "6600", 4);
    memcpy(line + 31, "01250", 5);
    memcpy(line + 39, "  4", 3);
    memcpy(line + 42, "180", 3);
    memcpy(line + 45, " 10", 3);
    memcpy(line + 48, "  35", 4);
    snprintf(q, sizeof q, "%010d", qid);
    memcpy(line + 136, q, 10);
    line[161] = (char)('0' + (version % 10));
    snprintf(v, sizeof v, "%04d", version);
    memcpy(line + 178, v, 4);
    line[174] = manual ? 'M' : 'A';
    memcpy(line + EWGUI_ARC_VMODEL_OFF, "CHILE_POTIN", 11);
    strcat(out, line);

    {
        char l1[200];
        memset(l1, ' ', 197);
        l1[0] = '$';
        l1[1] = '1';
        l1[197] = '\n';
        l1[198] = '\0';
        strcat(out, l1);
    }

    add_phase(out, "GO01", "C1", "HHZ", 'P', "20261010120005.12", "--");
    add_phase(out, "GO01", "C1", "HHN", 'S', "20261010120009.90", "--");
}

static void check_utc(const char *s, const char *prefix)
{
    if (strncmp(s, prefix, strlen(prefix)) != 0 || !strstr(s, "+00")) {
        fprintf(stderr, "timestamp inesperado: '%s' (esperaba '%s...' y +00)\n",
                s, prefix);
        assert(0);
    }
}

/* Preferida del evento en la BD (origen y, si se pide, magnitud). */
static void check_preferred(EwGuiDb *db, const char *want_origin,
                            const char *want_mag)
{
    EwGuiEventRow *evs = NULL;
    int nev = 0, f = -1;

    assert(ewguidb_query_recent_events(db, 24, &evs, &nev) >= 1);
    for (int i = 0; i < nev; i++)
        if (strcmp(evs[i].event_id, EID) == 0)
            f = i;
    assert(f >= 0);
    if (want_origin)
        assert(strcmp(evs[f].preferred_origin_id, want_origin) == 0);
    if (want_mag)
        assert(strcmp(evs[f].preferred_magnitude_id, want_mag) == 0);
    ewguidb_rows_free(evs);
}

int main(void)
{
    const char *envfile = getenv("EWGUI_DB_TEST_ENV");
    EwGuiDbConfig dbcfg;
    EwGuiDb *db = NULL;
    HypodbConfig cfg;
    HypodbState *st;
    char arc[8192];

    if (!envfile || !*envfile) {
        printf("SKIP test_ingest_db (EWGUI_DB_TEST_ENV no seteado)\n");
        return 0;
    }
    if (ewguidb_config_load(envfile, &dbcfg) != 0) {
        fprintf(stderr, "no se pudo cargar %s\n", envfile);
        return 1;
    }
    if (ewguidb_open(&dbcfg, &db) != 0) {
        fprintf(stderr, "no conecta: %s\n", ewguidb_last_error(db));
        ewguidb_close(db);
        return 1;
    }

    memset(&cfg, 0, sizeof cfg);
    cfg.group_pick_tol_sec = 0.1;
    cfg.group_min_match_picks = 3;
    st = hypodb_state_new(&cfg);
    assert(st);
    hypodb_state_set_db(st, db);

    ewguidb_exec(db, "DELETE FROM event WHERE event_id='" EID "'");

    build_arc(arc, 987654, 7, 1);
    assert(hypodb_feed_arc(st, arc, strlen(arc), 162) == 0);

    /* origin */
    {
        EwGuiOriginRow *ors = NULL;
        int nor = 0;

        const char *wv = getenv("EW_VERSION");

        assert(ewguidb_query_origins(db, EID, &ors, &nor) == 1);
        assert(strcmp(ors[0].origin_id, EID ":162") == 0);
        assert(strcmp(ors[0].status, "REVIEWED") == 0);
        assert(strcmp(ors[0].velocity_model, "CHILE_POTIN") == 0);
        assert(ors[0].station_count == 4);
        assert(fabs(ors[0].latitude + 33.75) < 0.01);   /* 33°45' */
        assert(fabs(ors[0].longitude + 71.10) < 0.01);  /* 70°66' */
        assert(fabs(ors[0].depth - 12.5) < 0.01);
        /* D2: `author` constante (el modulo) y `software` de $EW_VERSION. */
        assert(strcmp(ors[0].author, "csnhypodbp") == 0);
        assert(strcmp(ors[0].software, (wv && *wv) ? wv : "ew_8.0") == 0);
        assert(strcmp(ors[0].location_algorithm, "csnloc") == 0);
        assert(fabs(ors[0].azimuthal_gap - 180.0) < 1e-9);
        assert(fabs(ors[0].standard_error - 0.35) < 1e-6);
        /* PostgreSQL rinde el timestamptz sin milisegundos si son cero: se
           comprueba el instante (prefijo) y que sea UTC (+00). */
        check_utc(ors[0].origin_time, "2026-10-10 12:00:00");
        ewguidb_rows_free(ors);
    }

    /* picks */
    {
        EwGuiPickRow *pks = NULL;
        int npk = 0;

        assert(ewguidb_query_picks(db, EID ":162", &pks, &npk) == 2);
        assert(strcmp(pks[0].phase, "P") == 0);
        assert(strcmp(pks[0].channel, "C1.GO01..HHZ") == 0);
        check_utc(pks[0].pick_time, "2026-10-10 12:00:05");
        assert(strcmp(pks[1].phase, "S") == 0);
        assert(strcmp(pks[1].channel, "C1.GO01..HHN") == 0);
        /* Peso (LPWT/LSWT 0 -> 1.0), residual ausente -> NULL -> NAN, y
           pick_status automatico: el ARC es manual, pero la marca manual de la
           PICADA la da el cruce con PICK_RING. */
        assert(fabs(pks[0].weight - 1.0) < 1e-9);
        assert(fabs(pks[1].weight - 1.0) < 1e-9);
        assert(isnan(pks[0].residual));
        assert(strcmp(pks[0].pick_status, "automatic") == 0);
        ewguidb_rows_free(pks);
    }

    /* magnitud + estaciones */
    {
        MAG_INFO m;
        MAG_CHAN_INFO ch[1];
        char buf[1024];

        memset(&m, 0, sizeof m);
        memset(ch, 0, sizeof ch);
        snprintf(m.qid, sizeof m.qid, "987654");
        m.origin_version = 162;
        m.imagtype = 1;
        snprintf(m.szmagtype, sizeof m.szmagtype, "ML");
        m.mag = 4.2;
        m.error = 0.1;
        m.nstations = 1;
        m.nchannels = 1;
        snprintf(m.algorithm, sizeof m.algorithm, "CSNNet");
        snprintf(ch[0].sta, sizeof ch[0].sta, "GO01");
        snprintf(ch[0].comp, sizeof ch[0].comp, "HHZ");
        snprintf(ch[0].net, sizeof ch[0].net, "C1");
        ch[0].loc[0] = '\0';
        ch[0].mag = 4.1;
        ch[0].Amp1 = 1234.5f;
        m.pMagAux = (char *)ch;
        m.size_aux = sizeof ch;

        assert(wr_mag_scnl(&m, buf, sizeof buf) == 0);
        assert(hypodb_feed_mag(st, buf, strlen(buf)) == 0);
    }

    {
        EwGuiMagRow *mgs = NULL;
        int nmg = 0;

        assert(ewguidb_query_magnitudes(db, EID ":162", &mgs, &nmg) == 1);
        assert(strcmp(mgs[0].magnitude_type, "ML") == 0);
        assert(fabs(mgs[0].magnitude_value - 4.2) < 1e-6);
        assert(fabs(mgs[0].mag_uncertainty - 0.1) < 1e-6);
        assert(mgs[0].station_count == 1);
        /* D3: el metodo viaja en el mensaje (antes se escribia "CSNNet" fijo). */
        assert(strcmp(mgs[0].calculation_method, "CSNNet") == 0);
        ewguidb_rows_free(mgs);
    }
    /* D3 (regresion): una magnitud ajustada a mano ("CSNMan") se relee con su
       marca, que es la que csnhypodbp busca para preferirla al recargar. */
    {
        MAG_INFO m;
        char buf[1024];
        EwGuiMagRow *mgs = NULL;
        int nmg = 0;

        memset(&m, 0, sizeof m);
        snprintf(m.qid, sizeof m.qid, "987654");
        m.origin_version = 162;
        m.imagtype = 1;
        snprintf(m.szmagtype, sizeof m.szmagtype, "ML");
        m.mag = 4.4;
        m.error = -1.0;
        m.nstations = 2;
        snprintf(m.algorithm, sizeof m.algorithm, "CSNMan");
        assert(wr_mag_scnl(&m, buf, sizeof buf) == 0);
        assert(hypodb_feed_mag(st, buf, strlen(buf)) == 0);
        assert(ewguidb_query_magnitudes(db, EID ":162", &mgs, &nmg) == 1);
        assert(strcmp(mgs[0].calculation_method, "CSNMan") == 0);
        assert(fabs(mgs[0].magnitude_value - 4.4) < 1e-6);
        assert(mgs[0].station_count == 2);
        assert(isnan(mgs[0].mag_uncertainty));   /* error < 0 -> NULL */
        ewguidb_rows_free(mgs);
    }
    {
        EwGuiStaMagRow *sms = NULL;
        int nsm = 0;

        assert(ewguidb_query_station_magnitudes(db, EID ":162/mag/ML",
                                                &sms, &nsm) == 1);
        assert(strcmp(sms[0].channel, "C1.GO01..HHZ") == 0);
        assert(strcmp(sms[0].amplitude_type, "AML") == 0);
        ewguidb_rows_free(sms);
    }

    /* preferred */
    check_preferred(db, EID ":162", EID ":162/mag/ML");

    /* D10 (orden): con dos soluciones del mismo evento, `query_origins` las da
       en el orden del contrato (creation_time DESC, origin_time DESC,
       origin_id): la ultima escrita (163) va primero, como el default `Created`
       DESC de la tabla Origins. */
    {
        EwGuiOriginRow *ors = NULL;
        int nor = 0;

        assert(hypodb_feed_arc(st, arc, strlen(arc), 163) == 0);
        assert(ewguidb_query_origins(db, EID, &ors, &nor) == 2);
        assert(strcmp(ors[0].origin_id, EID ":163") == 0);
        assert(strcmp(ors[1].origin_id, EID ":162") == 0);
        ewguidb_rows_free(ors);
    }

    /* === Confirmaciones manuales: una fila `origin` propia e inmutable === */
    {
        EwGuiOriginRow *ors = NULL;
        char arc2[8192], live3[8192];
        char created7[40] = "";
        int nor = 0;

        /* AC4: confirmar la solucion v7 crea `<E>:162:7`, sin quitar la viva. */
        assert(hypodb_feed_confirmed(st, arc, strlen(arc), 162) == 0);
        assert(ewguidb_query_origins(db, EID, &ors, &nor) == 3);
        {
            int seen_conf = 0, seen_live = 0;

            for (int i = 0; i < nor; i++) {
                if (strcmp(ors[i].origin_id, EID ":162:7") == 0) {
                    seen_conf = 1;
                    assert(strcmp(ors[i].status, "REVIEWED") == 0);
                    assert(strcmp(ors[i].location_algorithm, "csnloc") == 0);
                    snprintf(created7, sizeof created7, "%s",
                             ors[i].creation_time);
                }
                if (strcmp(ors[i].origin_id, EID ":162") == 0)
                    seen_live = 1;
            }
            assert(seen_conf && seen_live);
        }
        ewguidb_rows_free(ors);

        /* La confirmada es la preferida, y la magnitud preferida NO se borra. */
        check_preferred(db, EID ":162:7", EID ":162/mag/ML");

        /* AC5: reentrega del MISMO ARC -> idempotente (no duplica fila). */
        assert(hypodb_feed_confirmed(st, arc, strlen(arc), 162) == 0);
        assert(ewguidb_query_origins(db, EID, &ors, &nor) == 3);
        ewguidb_rows_free(ors);
        check_preferred(db, EID ":162:7", EID ":162/mag/ML");

        /* AC4: una segunda confirmacion (v9) es otra fila y pasa a preferida. */
        build_arc(arc2, 987654, 9, 1);
        assert(hypodb_feed_confirmed(st, arc2, strlen(arc2), 162) == 0);
        assert(ewguidb_query_origins(db, EID, &ors, &nor) == 4);
        ewguidb_rows_free(ors);
        check_preferred(db, EID ":162:9", EID ":162/mag/ML");

        /* Inmutabilidad: un ARC vivo posterior actualiza la fila viva, pero la
           confirmada conserva su creation_time y sigue estando. */
        build_arc(live3, 987654, 10, 1);
        assert(hypodb_feed_arc(st, live3, strlen(live3), 162) == 0);
        assert(ewguidb_query_origins(db, EID, &ors, &nor) == 4);
        {
            int seen = 0;

            for (int i = 0; i < nor; i++)
                if (strcmp(ors[i].origin_id, EID ":162:7") == 0) {
                    seen = 1;
                    assert(strcmp(ors[i].creation_time, created7) == 0);
                }
            assert(seen);
        }
        ewguidb_rows_free(ors);
        /* La confirmada mas reciente (v9) sigue mandando. */
        check_preferred(db, EID ":162:9", EID ":162/mag/ML");

        /* AC6: tras recargar desde la BD, un ARC vivo nuevo no la desplaza. */
        {
            HypodbState *st2 = hypodb_state_new(&cfg);
            char arc4[8192];

            assert(st2);
            hypodb_state_set_db(st2, db);
            assert(hypodb_load_from_db(st2) == 0);
            build_arc(arc4, 987654, 11, 1);
            assert(hypodb_feed_arc(st2, arc4, strlen(arc4), 162) == 0);
            check_preferred(db, EID ":162:9", EID ":162/mag/ML");
            hypodb_state_free(st2);
        }
    }

    ewguidb_exec(db, "DELETE FROM event WHERE event_id='" EID "'");
    ewguidb_close(db);
    hypodb_state_free(st);

    printf("ALL HYPODB INGEST DB TESTS PASSED\n");
    return 0;
}
