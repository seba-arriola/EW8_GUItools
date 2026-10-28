/*
 * bpq_db.c - Historial y busqueda en la base hipocentral (PostgreSQL).
 *
 * Reutiliza el nucleo puro de `common` (`ewgui/db.c` + `ewgui_db_ids.c`): nada
 * de SQL propio. El recorrido es el de la GUI GTK (csnhypodbp_ws.c:678-830):
 *
 *   eventos  -> ewguidb_query_recent_events / ewguidb_search_events
 *   origenes -> ewguidb_query_origins(event_id)   -> CsnhypodbpSolution
 *   picadas  -> ewguidb_query_picks(origin_id)    -> CsnhypodbpPick
 *   store    -> bpq_events_feed_arc()  (misma agrupacion/preferencia que el vivo)
 *
 * `EWGuiDb` envuelve un unico `PGconn`, que NO es thread-safe: todo el acceso va
 * por el hilo de la GUI, igual que en la version GTK.
 */
#include "bpq_capi.h"
#include "csnhypodbp_magcfg.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ewgui/db.h"
#include "ewgui/db_ids.h"

struct BpqDb {
    EwGuiDb *db;
};

/* --- utilidades ---------------------------------------------------------- */

/* "YYYY-MM-DD HH:MM:SS" o ISO con 'T' (UTC) -> epoch. */
static double iso_epoch(const char *iso)
{
    int Y = 0, M = 0, D = 0, h = 0, m = 0, s = 0;
    struct tm tmv;

    if (!iso || !iso[0]) return 0.0;
    if (sscanf(iso, "%d-%d-%d%*[T ]%d:%d:%d", &Y, &M, &D, &h, &m, &s) < 3)
        return 0.0;
    if (M < 1 || M > 12 || D < 1 || D > 31) return 0.0;

    memset(&tmv, 0, sizeof(tmv));
    tmv.tm_year = Y - 1900;
    tmv.tm_mon = M - 1;
    tmv.tm_mday = D;
    tmv.tm_hour = h;
    tmv.tm_min = m;
    tmv.tm_sec = s;
    return (double)timegm(&tmv);
}

int bpq_db_parse_utc(const char *txt, double *epoch)
{
    double t;

    if (!txt || !epoch) return -1;
    while (*txt == ' ' || *txt == '\t') txt++;
    t = iso_epoch(txt);
    if (!(t > 0.0)) return -1;
    *epoch = t;
    return 0;
}

int bpq_db_fmt_utc(double epoch, char *out, size_t n)
{
    if (!out || n == 0) return -1;
    out[0] = '\0';
    if (!(epoch > 0.0)) return -1;
    ewgui_epoch_to_iso(epoch, out, n);
    return (out[0] != '\0') ? 0 : -1;
}

/* Canal "NET.STA.LOC.CHAN" -> STA (2.o campo), que es lo que empareja statable. */
static void nslco_sta(const char *nslco, char *out, size_t n)
{
    const char *a, *b;

    if (!out || n == 0) return;
    out[0] = '\0';
    if (!nslco) return;
    a = strchr(nslco, '.');
    if (!a) return;
    a++;
    b = strchr(a, '.');
    if (!b) b = a + strlen(a);
    if ((size_t)(b - a) >= n) b = a + n - 1;
    memcpy(out, a, (size_t)(b - a));
    out[b - a] = '\0';
}

/* --- ciclo de vida ------------------------------------------------------- */

BpqDb *bpq_db_open(const BpqConfig *cfg, char *err, size_t errn)
{
    BpqDb *out;
    EwGuiDbConfig dc;

    if (err && errn) err[0] = '\0';
    out = (BpqDb *)calloc(1, sizeof(*out));
    if (!out) return NULL;

    if (!cfg || !cfg->db_env_file[0]) {
        if (err) snprintf(err, errn, "sin DbEnvFile; historial deshabilitado");
        return out;
    }
    memset(&dc, 0, sizeof(dc));
    if (ewguidb_config_load(cfg->db_env_file, &dc) != 0) {
        if (err) snprintf(err, errn, "no se pudo leer <%s>", cfg->db_env_file);
        return out;
    }
    if (ewguidb_open(&dc, &out->db) != 0) {
        out->db = NULL;
        if (err)
            snprintf(err, errn, "no se pudo conectar a %s:%s/%s",
                     dc.host, dc.port, dc.name);
    }
    return out;
}

void bpq_db_close(BpqDb *db){
    if (!db) return;
    if (db->db) ewguidb_close(db->db);
    free(db);
}

/* Handle crudo: solo lo usa c/bpq_mww.c para colgar el tensor de un origin
 * (ui/ nunca lo ve; la frontera pública sigue siendo BpqDb). */
void *bpq_db_ewguidb(BpqDb *d)
{
    return (d && d->db) ? (void *)d->db : NULL;
}

int bpq_db_available(const BpqDb *db)
{
    return (db && db->db) ? 1 : 0;
}

/* --- origenes -> soluciones ---------------------------------------------- */

/* Rellena `sol` con una fila de `origin` y le cuelga sus picadas. */
static void sol_from_origin(BpqDb *d, const char *event_id,
                            const EwGuiOriginRow *o, CsnhypodbpSolution *sol)
{
    char ev[64] = "";
    int mod = 0, confirmed = 0;
    unsigned version = 0;

    memset(sol, 0, sizeof(*sol));
    (void)ewgui_origin_id_parse(o->origin_id, ev, sizeof(ev), &mod, &version,
                                &confirmed);
    if (ev[0] == 'E') sol->qid = atoi(ev + 1);
    else if (event_id && event_id[0] == 'E') sol->qid = atoi(event_id + 1);
    sol->mod = mod;
    sol->version = version;
    sol->confirmed = confirmed;
    /* origin.status: MANUAL lo marco un analista (la GUI GTK hace lo mismo). */
    sol->manual = (strcmp(o->status, "MANUAL") == 0) ? 1 : 0;
    sol->otime = iso_epoch(o->origin_time);
    sol->lat = o->latitude;
    sol->lon = o->longitude;
    sol->depth = o->depth;
    sol->nph = o->station_count;
    sol->rms = o->standard_error;
    sol->gap = (int)(o->azimuthal_gap + 0.5);
    snprintf(sol->origin_id, sizeof(sol->origin_id), "%s", o->origin_id);

    if (d->db) {
        EwGuiPickRow *pk = NULL;
        int npk = 0, k;

        if (ewguidb_query_picks(d->db, o->origin_id, &pk, &npk) >= 0 && pk) {
            for (k = 0; k < npk && sol->npicks < CSNHYPODBP_MAX_PICKS; k++) {
                CsnhypodbpPick *p = &sol->picks[sol->npicks];

                memset(p, 0, sizeof(*p));
                nslco_sta(pk[k].channel, p->sta, sizeof(p->sta));
                if (!p->sta[0]) continue;
                p->phase = (pk[k].phase[0] == 'S' || pk[k].phase[0] == 's') ? 'S' : 'P';
                p->t_pick = iso_epoch(pk[k].pick_time);
                p->residual = pk[k].residual;   /* columna uncertainty */
                p->manual = (strncmp(pk[k].pick_status, "manual", 6) == 0) ? 1 : 0;
                sol->npicks++;
            }
        }
        ewguidb_rows_free(pk);
    }
}

static int load_event_ids(BpqDb *d, const char *const *ids, int nids,
                          BpqEvents *store)
{
    int i, added = 0;

    if (!d || !d->db || !store || !ids) return 0;

    for (i = 0; i < nids; i++) {
        EwGuiOriginRow *ors = NULL;
        int no = 0, j;

        if (ewguidb_query_origins(d->db, ids[i], &ors, &no) < 0 || !ors) {
            ewguidb_rows_free(ors);
            continue;
        }
        for (j = 0; j < no; j++) {
            CsnhypodbpSolution sol;
            EwGuiMagRow *mg = NULL;
            int nm = 0, k;
            double created;

            sol_from_origin(d, ids[i], &ors[j], &sol);
            /* La hora de creacion vive en `origin`, no en el ARC: sin esto la
             * GUI sellaba time(NULL) y todas las filas de una carga coincidian. */
            created = iso_epoch(ors[j].creation_time);

            /* Magnitudes del origen (una fila por tipo). Se publican al store por
             * el mismo camino que las del anillo, para que la maestra y el
             * detalle las vean igual. */
            if (ewguidb_query_magnitudes(d->db, ors[j].origin_id, &mg, &nm) < 0 ||
                !mg)
                nm = 0;
            if (nm > 0) sol.pref_mag = mg[0].magnitude_type[0];

            if (sol.otime > 0.0 &&
                (created > 0.0 ? bpq_events_feed_arc_at(store, &sol, created)
                               : bpq_events_feed_arc(store, &sol)) == 0) {
                added++;
                for (k = 0; k < nm; k++) {
                    BpqMagMsg m;

                    memset(&m, 0, sizeof(m));
                    m.qid = sol.qid;
                    m.mod = sol.mod;
                    m.version = sol.version;
                    m.value = mg[k].magnitude_value;
                    m.nsta = mg[k].station_count;
                    snprintf(m.type, sizeof(m.type), "%s", mg[k].magnitude_type);
                    snprintf(m.algorithm, sizeof(m.algorithm), "%s",
                             mg[k].calculation_method);
                    (void)bpq_events_feed_mag(store, &m);
                }
            }
            ewguidb_rows_free(mg);
        }
        ewguidb_rows_free(ors);
    }
    return added;
}

/* --- historial y busqueda ------------------------------------------------ */

int bpq_db_load_recent(BpqDb *db, const BpqConfig *cfg, BpqEvents *store,
                       int hours, char *err, size_t errn)
{
    EwGuiEventRow *ev = NULL;
    const char **ids = NULL;
    int n = 0, i, added = 0;

    (void)cfg;
    if (err && errn) err[0] = '\0';
    if (!db || !db->db) {
        if (err) snprintf(err, errn, "sin conexion a la base");
        return 0;
    }
    if (ewguidb_query_recent_events(db->db, hours, &ev, &n) < 0 || !ev || n <= 0) {
        if (err) snprintf(err, errn, "historial: %s", ewguidb_last_error(db->db));
        ewguidb_rows_free(ev);
        return 0;
    }
    if (n > 60) n = 60;   /* tope de arranque: 2 consultas por evento */

    ids = (const char **)calloc((size_t)n, sizeof(*ids));
    if (!ids) {
        ewguidb_rows_free(ev);
        return 0;
    }
    for (i = 0; i < n; i++) ids[i] = ev[i].event_id;
    added = load_event_ids(db, ids, n, store);
    free(ids);
    ewguidb_rows_free(ev);
    return added;
}

int bpq_db_search(BpqDb *db, const BpqConfig *cfg, BpqEvents *store,
                  double t_min, double t_max, int limit,
                  char *err, size_t errn)
{
    EwGuiSearchFilter f;
    EwGuiMapEvent *ev = NULL;
    const char **ids = NULL;
    char iso_min[40], iso_max[40];
    int n = 0, i, added = 0;

    if (err && errn) err[0] = '\0';
    if (!db || !db->db) {
        if (err) snprintf(err, errn, "sin conexion a la base");
        return 0;
    }
    if (!(t_min > 0.0) || !(t_max > t_min)) {
        if (err) snprintf(err, errn, "rango de tiempo invalido");
        return 0;
    }

    memset(&f, 0, sizeof(f));
    ewgui_epoch_to_iso(t_min, iso_min, sizeof(iso_min));
    ewgui_epoch_to_iso(t_max, iso_max, sizeof(iso_max));
    f.time_min = iso_min;
    f.time_max = iso_max;
    f.limit = (limit > 0) ? limit
                          : ((cfg && cfg->search_max_events > 0)
                                 ? cfg->search_max_events
                                 : 200);

    if (ewguidb_search_events(db->db, &f, &ev, &n) < 0) {
        if (err) snprintf(err, errn, "busqueda: %s", ewguidb_last_error(db->db));
        ewguidb_rows_free(ev);
        return 0;
    }
    if (!ev || n <= 0) {
        ewguidb_rows_free(ev);
        return 0;
    }

    ids = (const char **)calloc((size_t)n, sizeof(*ids));
    if (!ids) {
        ewguidb_rows_free(ev);
        return 0;
    }
    for (i = 0; i < n; i++) ids[i] = ev[i].event_id;
    added = load_event_ids(db, ids, n, store);
    free(ids);
    ewguidb_rows_free(ev);
    return added;
}

/* Magnitudes por estacion de un origen concreto. Prefiere la magnitud manual
 * (CSNMan) del mismo tipo; si no, la automatica. La conversion de filas la
 * hace el mismo nucleo puro que la GTK. */
int bpq_db_mag_stations(BpqDb *db, const char *origin_id, int kind,
                        CsnhypodbpMagIn *out, int max)
{
    EwGuiMagRow *mg = NULL;
    int nm = 0, i, best = -1, n = 0;
    const char *want;

    if (!db || !db->db || !origin_id || !origin_id[0] || !out || max <= 0)
        return 0;
    want = csnhypodbp_magcfg_name(kind);
    if (!want || !want[0]) return 0;
    if (ewguidb_query_magnitudes(db->db, origin_id, &mg, &nm) < 0 || !mg) {
        ewguidb_rows_free(mg);
        return 0;
    }
    for (i = 0; i < nm; i++) {
        if (strcmp(mg[i].magnitude_type, want) != 0) continue;
        if (best < 0 || strcmp(mg[i].calculation_method, "CSNMan") == 0) {
            best = i;
            if (strcmp(mg[i].calculation_method, "CSNMan") == 0) break;
        }
    }
    if (best >= 0) {
        EwGuiStaMagRow *st = NULL;
        int ns = 0;

        if (ewguidb_query_station_magnitudes(db->db, mg[best].magnitude_id,
                                             &st, &ns) >= 0 && st && ns > 0) {
            CsnhypodbpMagDbRow *rows =
                (CsnhypodbpMagDbRow *)calloc((size_t)ns, sizeof(*rows));

            if (rows) {
                for (i = 0; i < ns; i++) {
                    rows[i].channel = st[i].channel;
                    rows[i].read_amplitude = st[i].read_amplitude;
                    rows[i].calculated_magnitude = st[i].calculated_magnitude;
                }
                n = csnhypodbp_magrow_from_db(out, max, rows, ns);
                free(rows);
            }
        }
        ewguidb_rows_free(st);
    }
    ewguidb_rows_free(mg);
    return n;
}
