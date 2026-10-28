/*
 * bpq_mww.c - Panel `Mww` (fase 6.3): inversion de fase W on-demand.
 *
 * Fachada unica sobre `csnwphase` (libcsnwphase.a): la UI nunca ve su config ni
 * su struct de salida, solo `BpqMww*`/`BpqMwwResult*` y textos ya formateados.
 *
 * El camino es el del modulo de referencia: semilla (solucion + magnitud) ->
 * overrides de la tabla (`mww_over_*`) -> `csnwphase_input_prep` (ondas del
 * store/SDS + SAC_PZs + i_master/CMTSOLUTION) -> `csnwphase_run` (motor
 * vendorizado, deja `run.log`) -> `csnwphase_parse`. Cualquier fallo se reporta
 * con su texto real: primero el del parser, si no el primer error del log.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ewgui/db.h"
#include "ewgui/db_ids.h"

#include "csnwphase.h"
#include "csnhypodbp_mwwerr.h"
#include "csnhypodbp_mww_over.h"

#include "bpq_internal.h"

struct BpqMwwResult {
    CsnwphaseOut out;
};

struct BpqMww {
    CsnwphaseCfg  cfg;
    CsnwphaseInput in;
    MwwOverrides  ov;
    char           rundir[BPQ_MAX_STR];
    char           workdir[BPQ_MAX_STR];
    char           runerr[512];
    char           summary[1024];
    char           logtxt[16384];
    BpqMwwResult   res;
    int            have_out;
    int            seeded;
};

static void copy_str(char *dst, size_t n, const char *src)
{
    snprintf(dst, n, "%s", src ? src : "");
}

BpqMww *bpq_mww_new(const BpqConfig *cfg, char *err, size_t errn)
{
    BpqMww *m;
    char path[BPQ_MAX_STR] = "";

    if (err && errn) err[0] = '\0';
    if (!cfg) {
        if (err && errn) snprintf(err, errn, "Mww: falta la configuracion");
        return NULL;
    }
    m = (BpqMww *)calloc(1, sizeof(*m));
    if (!m) {
        if (err && errn) snprintf(err, errn, "Mww: sin memoria");
        return NULL;
    }
    csnwphase_resolve_path(path, sizeof(path), cfg->mww_config_file);
    if (csnwphase_cfg_load(&m->cfg, path) != 0) {
        if (err && errn)
            snprintf(err, errn, "Mww: no se pudo leer %s", path);
        free(m);
        return NULL;
    }
    copy_str(m->workdir, sizeof(m->workdir), cfg->mww_workdir);
    copy_str(m->summary, sizeof(m->summary), "sin resultado: pulsa `Calculate`");
    return m;
}

void bpq_mww_free(BpqMww *m)
{
    free(m);
}

int bpq_mww_seeded(const BpqMww *m)
{
    return m ? m->seeded : 0;
}

const char *bpq_mww_rundir(const BpqMww *m)
{
    return m ? m->rundir : "";
}

/* Un run dir por solucion: la corrida no se mezcla con la anterior. */
static void build_rundir(BpqMww *m, const CsnhypodbpSolution *sol)
{
    time_t t = (time_t)sol->otime;
    struct tm tm;

    if (!gmtime_r(&t, &tm)) memset(&tm, 0, sizeof(tm));
    snprintf(m->rundir, sizeof(m->rundir), "%s/E%d_%u_%04d%02d%02d_%02d%02d%02d",
             m->workdir[0] ? m->workdir : "/tmp/csnhypodbp_mww", sol->qid,
             sol->version, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec);
}

int bpq_mww_seed(BpqMww *m, const CsnhypodbpSolution *sol, double mag,
                 char *err, size_t errn)
{
    struct tm tm;
    time_t t;

    if (err && errn) err[0] = '\0';
    if (!m || !sol) {
        if (err && errn) snprintf(err, errn, "Mww: falta la solucion semilla");
        return 0;
    }
    if (!(sol->otime > 0.0) || !isfinite(sol->lat) || !isfinite(sol->lon)) {
        if (err && errn)
            snprintf(err, errn, "Mww: la solucion no tiene origen/hipocentro");
        return 0;
    }
    if (!isfinite(mag) || mag <= 0.0) {
        if (err && errn)
            snprintf(err, errn, "Mww: falta la magnitud semilla (elegi una)");
        return 0;
    }

    memset(&m->in, 0, sizeof(m->in));
    t = (time_t)sol->otime;
    if (!gmtime_r(&t, &tm)) {
        if (err && errn) snprintf(err, errn, "Mww: origen fuera de rango");
        return 0;
    }
    m->in.year = tm.tm_year + 1900;
    m->in.month = tm.tm_mon + 1;
    m->in.day = tm.tm_mday;
    m->in.hour = tm.tm_hour;
    m->in.minute = tm.tm_min;
    m->in.second = (double)tm.tm_sec;
    m->in.latitude = sol->lat;
    m->in.longitude = sol->lon;
    m->in.depth_km = sol->depth;
    m->in.magnitude = mag;
    m->in.origin_epoch = sol->otime;
    snprintf(m->in.event_id, sizeof(m->in.event_id), "E%d", sol->qid);
    snprintf(m->in.origin_id, sizeof(m->in.origin_id), "%d:%d:%u", sol->qid,
             sol->mod, sol->version);
    copy_str(m->in.location, sizeof(m->in.location), "Chile");

    mww_over_init(&m->in, &m->cfg, &m->ov);
    build_rundir(m, sol);
    m->seeded = 1;
    m->have_out = 0;
    memset(&m->res, 0, sizeof(m->res));
    copy_str(m->summary, sizeof(m->summary), "sin resultado: pulsa `Calculate`");
    m->logtxt[0] = '\0';
    m->runerr[0] = '\0';
    return 1;
}

int bpq_mww_rows(const BpqMww *m, BpqMwwParamRow *rows, int max)
{
    CsnwphaseParamRow raw[32];
    CsnwphaseCfg cfg;
    CsnwphaseInput in;
    int i, n = 0;

    if (!m || !rows || max <= 0 || !m->seeded) return 0;
    /* La tabla muestra lo que se va a ejecutar: overrides aplicados (si no, el
     * valor editado no se veria y la tabla mentiria). */
    cfg = m->cfg;
    in = m->in;
    mww_over_apply(&m->ov, &in, &cfg);
    if (csnwphase_params_build(&cfg, &in, raw, 32, &n) != 0) return 0;
    if (n > max) n = max;
    for (i = 0; i < n; i++) {
        memset(&rows[i], 0, sizeof(rows[i]));
        copy_str(rows[i].name, sizeof(rows[i].name), raw[i].name);
        copy_str(rows[i].value, sizeof(rows[i].value), raw[i].value);
        copy_str(rows[i].source, sizeof(rows[i].source), raw[i].source);
        rows[i].field = raw[i].field;
    }
    return n;
}

int bpq_mww_edit(BpqMww *m, int field, const char *txt)
{
    double v = 0.0;

    if (!m || !m->seeded) return 0;
    if (field == CSNWPHASE_PF_ORIGIN_TIME) {
        if (mww_over_set_time(&m->ov, txt ? txt : "") != 0) {
            (void)mww_over_unset(&m->ov, field);
            return 0;
        }
        /* La tabla se rearma con el t0 nuevo (y su epoch). */
        return 1;
    }
    if (mww_field_limits(field, NULL, NULL) != 0) return 0;
    if (mww_field_parse(field, txt ? txt : "", &v) != 0) {
        (void)mww_over_unset(&m->ov, field);   /* ni un prefijo a medias */
        return 0;
    }
    return mww_over_set(&m->ov, field, v) == 0 ? 1 : 0;
}

int bpq_mww_field_limits(int field, double *lo, double *hi)
{
    return mww_field_limits(field, lo, hi);
}

/* Motivo real de un fallo: el del parser si lo trae; si no, la primera linea de
 * error del log del motor (un run.sh abortado solo deja un rc generico). */
static void why(const BpqMww *m, char *out, size_t n)
{
    if (!out || !n) return;
    out[0] = '\0';
    if (m->res.out.err[0])
        copy_str(out, n, m->res.out.err);
    else if (m->runerr[0])
        copy_str(out, n, m->runerr);
    else if (csnhypodbp_mww_first_error(m->rundir, out, n) != 0)
        return;                              /* ya dejo el texto */
    else
        snprintf(out, n, "sin resultado (run dir %s)", m->rundir);
}

/* Resumen + salida completa del motor (los dos cuadros del panel). */
static void build_texts(BpqMww *m)
{
    const CsnwphaseOut *o = &m->res.out;

    m->logtxt[0] = '\0';
    if (m->rundir[0]) {
        char path[BPQ_MAX_STR];
        int got = 0;

        snprintf(path, sizeof(path), "%s/full_output.txt", m->rundir);
        if (csnwphase_log_write(m->rundir, path) == 0) {
            FILE *f = fopen(path, "r");

            if (f) {
                size_t n = fread(m->logtxt, 1, sizeof(m->logtxt) - 1, f);

                m->logtxt[n] = '\0';
                got = (int)n;
                fclose(f);
            }
        }
        /* Sin `full_output.txt` (p.ej. una corrida que fallo antes del visor) se
         * muestra `run.log`, que es la salida completa del motor: el cuadro
         * derecho nunca queda vacio si el motor dijo algo. */
        if (got <= 0) {
            FILE *f = NULL;

            snprintf(path, sizeof(path), "%s/run.log", m->rundir);
            f = fopen(path, "r");
            if (f) {
                size_t n = fread(m->logtxt, 1, sizeof(m->logtxt) - 1, f);

                m->logtxt[n] = '\0';
                fclose(f);
            }
        }
    }

    if (!m->have_out || !o->ok) {
        char w[512];

        why(m, w, sizeof(w));
        snprintf(m->summary, sizeof(m->summary), "sin resultado\n%s", w);
        return;
    }

    {
        char planes[192] = "";
        char conv[64] = "";

        if (o->have_planes)
            snprintf(planes, sizeof(planes),
                     "planos nodales: (%.0f/%.0f/%.0f) y (%.0f/%.0f/%.0f)",
                     o->strike1, o->dip1, o->rake1, o->strike2, o->dip2,
                     o->rake2);
        if (!o->converged) snprintf(conv, sizeof(conv), "  (sin Wmag en el log)");
        snprintf(m->summary, sizeof(m->summary),
                 "Mw %.2f   M0 %.3e N.m%s\n"
                 "centroide: %.3f, %.3f  %.1f km   (shift %.1f s, half-dur %.1f s)\n"
                 "%s\n"
                 "estaciones usadas: %d   RMS global: %.4g",
                 o->mw, o->m0, conv, o->lat, o->lon, o->depth_km, o->t_shift_s,
                 o->half_dur_s,
                 planes[0] ? planes : "planos nodales: (no declarados)",
                 o->n_used, o->global_rms);
    }
}

BpqMwwResult *bpq_mww_run(BpqMww *m, int dry_run, char *err, size_t errn)
{
    CsnwphaseCfg cfg;
    CsnwphaseInput in;
    char ebuf[512] = "";
    int nsta = 0, ntr = 0, rc;

    if (err && errn) err[0] = '\0';
    if (!m || !m->seeded) {
        if (err && errn) snprintf(err, errn, "Mww: falta la semilla");
        return NULL;
    }

    cfg = m->cfg;
    in = m->in;
    mww_over_apply(&m->ov, &in, &cfg);
    m->have_out = 0;
    memset(&m->res, 0, sizeof(m->res));
    m->runerr[0] = '\0';

    rc = csnwphase_input_prep(&cfg, &in, m->rundir, &nsta, &ntr, ebuf,
                              sizeof(ebuf));
    if (rc != 0) {
        copy_str(m->runerr, sizeof(m->runerr),
                 ebuf[0] ? ebuf : "no se pudo preparar la entrada (ondas/PZs)");
        build_texts(m);
        if (err && errn) copy_str(err, errn, m->runerr);
        return NULL;
    }

    rc = csnwphase_run(&cfg, m->rundir, dry_run, in.magnitude, ebuf,
                       sizeof(ebuf));
    if (rc != 0 && ebuf[0]) copy_str(m->runerr, sizeof(m->runerr), ebuf);

    /* Aunque el wrapper salga != 0, si el motor dejo una solucion se muestra
     * (es lo que hace la GTK): el parseo es la fuente de verdad del resultado. */
    if (csnwphase_parse(m->rundir, &m->res.out) == 0 && m->res.out.ok)
        m->have_out = 1;

    build_texts(m);
    if (!m->have_out) {
        char w[512];

        why(m, w, sizeof(w));
        if (err && errn) copy_str(err, errn, w);
        return NULL;
    }
    return &m->res;
}

const BpqMwwResult *bpq_mww_result(const BpqMww *m)
{
    return (m && m->have_out) ? &m->res : NULL;
}

int bpq_mww_result_ok(const BpqMwwResult *r)
{
    return (r && r->out.ok) ? 1 : 0;
}

const char *bpq_mww_summary(const BpqMww *m)
{
    return m ? m->summary : "";
}

const char *bpq_mww_log(const BpqMww *m)
{
    return m ? m->logtxt : "";
}

int bpq_mww_tensor_ready(const BpqMww *m)
{
    return (m && m->have_out && m->res.out.ok && m->res.out.have_mt) ? 1 : 0;
}

/* Resultado crudo para el dibujo de la playera (interno de c/). */
const void *bpq_mww_out_raw(const BpqMww *m)
{
    return (m && m->have_out) ? (const void *)&m->res.out : NULL;
}

/* Rellena la fila de `tensor` con el mecanismo de la corrida. */
static void tensor_row(const BpqMww *m, const char *origin_id, EwGuiTensorRow *r)
{
    const CsnwphaseOut *o = &m->res.out;

    memset(r, 0, sizeof(*r));
    snprintf(r->tensor_id, sizeof(r->tensor_id), "%s:WPHASE", origin_id);
    snprintf(r->origin_id, sizeof(r->origin_id), "%s", origin_id);
    snprintf(r->method, sizeof(r->method), "%s", "WPHASE");
    snprintf(r->mechanism, sizeof(r->mechanism), "%s", "WCMT");
    r->m_rr = o->m_rr;
    r->m_tt = o->m_tt;
    r->m_pp = o->m_pp;
    r->m_rt = o->m_rt;
    r->m_rp = o->m_rp;
    r->m_tp = o->m_tp;
    r->scalar_moment = o->m0;
    r->have_planes = o->have_planes ? 1 : 0;
    if (o->have_planes) {
        r->strike1 = o->strike1;
        r->dip1 = o->dip1;
        r->rake1 = o->rake1;
        r->strike2 = o->strike2;
        r->dip2 = o->dip2;
        r->rake2 = o->rake2;
    }
}

/* epoch (UTC) que lleva pegado un id de evento `E<qid>-<yyyymmddhhmmss>`; <0 si
 * el id no trae la marca de tiempo. */
static double event_id_epoch(const char *eid)
{
    const char *p = eid ? strrchr(eid, '-') : NULL;
    struct tm tm;

    if (!p || strlen(p + 1) < 14) return -1.0;
    memset(&tm, 0, sizeof(tm));
    if (sscanf(p + 1, "%4d%2d%2d%2d%2d%2d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
               &tm.tm_hour, &tm.tm_min, &tm.tm_sec) != 6)
        return -1.0;
    tm.tm_year -= 1900;
    tm.tm_mon -= 1;
    tm.tm_isdst = 0;
    return (double)timegm(&tm);
}

/* Reusa el `event` del pipeline para el MISMO sismo: hypodb arma el id con la
 * hora de la PRIMERA solucion del grupo, que puede diferir en segundos de la
 * hora refinada; si no se reusa, el sismo se parte en dos eventos. Se exige el
 * mismo qid y |dt| <= 300 s (EventMaxDtSec). */
static int reuse_event(EwGuiDb *d, const char *qid, double otime, char *eid,
                       size_t n)
{
    EwGuiEventRow *rows = NULL;
    char prefix[32];
    int nr = 0, i, ok = 0;

    snprintf(prefix, sizeof(prefix), "E%s-", qid);
    if (ewguidb_query_recent_events(d, 168, &rows, &nr) < 0) return 0;
    for (i = 0; i < nr && !ok; i++) {
        double ot;

        if (strncmp(rows[i].event_id, prefix, strlen(prefix)) != 0) continue;
        ot = event_id_epoch(rows[i].event_id);
        if (ot > 0.0 && fabs(ot - otime) <= 300.0) {
            copy_str(eid, n, rows[i].event_id);
            ok = 1;
        }
    }
    if (rows) ewguidb_rows_free(rows);
    return ok;
}

int bpq_mww_save(BpqDb *db, const CsnhypodbpSolution *sol, int mod,
                 const char *algorithm, const BpqMww *m, char *msg, size_t n)
{
    EwGuiEventRow ev;
    EwGuiOriginRow ori;
    EwGuiMagRow mag;
    EwGuiTensorRow row;
    char eid[64] = "", oid[96] = "", mid[128] = "", tid[128] = "", qid[24] = "";
    void *ewdb;
    EwGuiDb *d;

    if (msg && n) msg[0] = '\0';
    if (!db || !sol) {
        if (msg && n) snprintf(msg, n, "sin solucion o sin base de datos");
        return 0;
    }
    if (!bpq_mww_tensor_ready(m)) {
        if (msg && n)
            snprintf(msg, n, "no hay mecanismo focal (pulsa Calculate)");
        return 0;
    }
    if (mod <= 0) {
        if (msg && n)
            snprintf(msg, n, "sin modulo de origen (mod) para el origin_id");
        return 0;
    }
    ewdb = bpq_db_ewguidb(db);
    if (!ewdb) {
        if (msg && n) snprintf(msg, n, "no hay conexion con la base hipocentral");
        return 0;
    }
    d = (EwGuiDb *)ewdb;

    /* Identidad del origin: si la solucion viene de una fila de *Origins* (p. ej.
     * una iteracion `<event>:<mod>:<version>:I` o una confirmada), el mecanismo
     * se cuelga de ESA fila. Solo se crea la fila viva `<event>:<mod>` cuando es
     * una solucion nueva (candidata del locate), igual que la GTK. */
    snprintf(qid, sizeof(qid), "%d", sol->qid);
    if (!reuse_event(d, qid, sol->otime, eid, sizeof(eid)) || eid[0] == '\0') {
        if (ewgui_event_id(eid, sizeof(eid), qid, sol->otime) != 0) {
            if (msg && n) snprintf(msg, n, "id de evento demasiado largo");
            return 0;
        }
    }
    {
        int r1;
        char oev[64] = "";
        int omod = 0, oconf = 0;
        unsigned over = 0;
        int written;

        if (sol->origin_id[0] &&
            ewgui_origin_id_parse(sol->origin_id, oev, sizeof oev, &omod, &over,
                                  &oconf) == 0 &&
            omod == mod && strcmp(oev, eid) == 0) {
            written = snprintf(oid, sizeof oid, "%s", sol->origin_id);
            r1 = (written >= 0 && (size_t)written < sizeof oid) ? 0 : -1;
        } else {
            r1 = ewgui_origin_id(oid, sizeof(oid), eid, mod);
        }
        const int r2 = r1 == 0 ? ewgui_magnitude_id(mid, sizeof(mid), oid, "Mww")
                               : -9;
        const int r3 = r2 == 0 ? ewgui_tensor_id(tid, sizeof(tid), oid, "WPHASE")
                               : -9;

        if (r1 != 0 || r2 != 0 || r3 != 0) {
            if (msg && n)
                snprintf(msg, n, "ids (o=%d m=%d t=%d eid='%s' mod=%d)", r1, r2,
                         r3, eid, mod);
            return 0;
        }
    }

    /* 1. evento + origen (solo si faltan: si hypodb ya los creo, no se tocan). */
    memset(&ev, 0, sizeof(ev));
    copy_str(ev.event_id, sizeof(ev.event_id), eid);
    copy_str(ev.event_type, sizeof(ev.event_type), "EARTHQUAKE");
    if (ewguidb_upsert_event(d, &ev) != 0) {
        if (msg && n) snprintf(msg, n, "%s", ewguidb_last_error(d));
        return 0;
    }
    memset(&ori, 0, sizeof(ori));
    copy_str(ori.origin_id, sizeof(ori.origin_id), oid);
    copy_str(ori.event_id, sizeof(ori.event_id), eid);
    ewgui_epoch_to_iso(sol->otime, ori.origin_time, sizeof(ori.origin_time));
    ori.latitude = sol->lat;
    ori.longitude = sol->lon;
    ori.depth = sol->depth;
    copy_str(ori.author, sizeof(ori.author), "csnhypodbp");
    copy_str(ori.software, sizeof(ori.software), ewgui_software());
    copy_str(ori.location_algorithm, sizeof(ori.location_algorithm),
             algorithm ? algorithm : "");
    ori.azimuthal_gap = (sol->gap > 0) ? (double)sol->gap : NAN;
    ori.station_count = sol->nph;
    ori.standard_error = (sol->rms > 0.0) ? sol->rms : NAN;
    copy_str(ori.status, sizeof(ori.status), ewgui_origin_status(1));
    if (ewguidb_upsert_origin_if_absent(d, &ori) != 0) {
        if (msg && n) snprintf(msg, n, "%s", ewguidb_last_error(d));
        return 0;
    }

    /* 2. lo que aporta la fase W: magnitud Mww + mecanismo focal. */
    memset(&mag, 0, sizeof(mag));
    copy_str(mag.magnitude_id, sizeof(mag.magnitude_id), mid);
    copy_str(mag.origin_id, sizeof(mag.origin_id), oid);
    mag.magnitude_value = m->res.out.mw;
    copy_str(mag.magnitude_type, sizeof(mag.magnitude_type), "Mww");
    mag.mag_uncertainty = NAN;
    copy_str(mag.calculation_method, sizeof(mag.calculation_method), "WPHASE");
    mag.station_count = m->res.out.n_used;

    tensor_row(m, oid, &row);
    copy_str(row.tensor_id, sizeof(row.tensor_id), tid);

    if (ewguidb_upsert_magnitude(d, &mag) != 0 ||
        ewguidb_upsert_tensor(d, &row) != 0) {
        if (msg && n) snprintf(msg, n, "%s", ewguidb_last_error(d));
        return 0;
    }

    /* 3. la solucion con Mww pasa a preferida del evento (con su magnitud). */
    if (ewguidb_set_preferred(d, eid, oid, mid) != 0) {
        if (msg && n) snprintf(msg, n, "%s", ewguidb_last_error(d));
        return 0;
    }

    if (msg && n)
        snprintf(msg, n, "Mww %.2f + tensor (WPHASE) en %s", m->res.out.mw, oid);
    return 1;
}
