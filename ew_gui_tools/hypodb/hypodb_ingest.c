/*
 * hypodb_ingest — escribe la información hipocentral de los anillos en
 * PostgreSQL. Núcleo sin GTK; ver hypodb_ingest.h.
 *
 * Anillos: HYPO_RING (162, csnloc), HYPO_RING_REF (163, hyp2000),
 * HYPO_RING_REF2 (164, nlloc) y PICK_RING (picks manuales). Agrupa soluciones
 * por firma de picadas (reutiliza csnhypodbp_group) para asignar un event_id
 * por sismo físico, aunque cambie el qid.
 */

#include "hypodb_ingest.h"

#include <ctype.h>
#include <locale.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <rw_mag.h>

#include "hypodb_ids.h"

#include "ewgui/arc_layout.h"

#define HYPODB_MAX_GROUPS       128
#define HYPODB_MAX_ORIGINS      8
#define HYPODB_MAX_MAGS         16
#define HYPODB_MAX_PENDING_MAGS 256
#define HYPODB_MAX_MANUAL_PICKS 1024
#define HYPODB_MAX_CHAN         512

typedef struct {
    int      mod;
    char     origin_id[64];
    int      manual;
    unsigned version;
    int      nph;
    double   rms;
    double   otime;
} HypodbOriginMeta;

typedef struct {
    char   magnitude_id[64];
    char   type[16];
    double value;
} HypodbMagMeta;

typedef struct {
    CsnhypodbpGroupKey key;   /* PRIMERO: permite pasar &groups[0] como keys */
    char     event_id[64];
    double   otime;           /* hora origen de la 1.ª solución del grupo */
    int      norigins;
    HypodbOriginMeta origins[HYPODB_MAX_ORIGINS];
    int      nmags;
    HypodbMagMeta mags[HYPODB_MAX_MAGS];
    char     pref_mag;        /* tipo preferido del ARC (BMTYPI): L/W/B/S */
} HypodbGroup;

typedef struct {
    int    used;
    int    qid;
    int    mod;
    char   type[16];
    double value;
    double error;
    int    nsta;
    char   algorithm[64];
    char   msg[512];
    int    msg_len;
} HypodbPendingMag;

typedef struct {
    int    used;
    char   sta[8];
    char   phase;
    double t;
} HypodbManualPick;

struct HypodbState {
    HypodbConfig      cfg;
    EwGuiDb          *db;
    HypodbGroup       groups[HYPODB_MAX_GROUPS];
    int               ngroups;
    HypodbPendingMag  pmag[HYPODB_MAX_PENDING_MAGS];
    HypodbManualPick  mpick[HYPODB_MAX_MANUAL_PICKS];
    int               mpick_next;
};

/* ------------------------------------------------------------------------- */
/* Helpers de tiempo / nombres                                                */
/* ------------------------------------------------------------------------- */

static void epoch_to_iso(double t, char *out, size_t n)
{
    time_t s = (time_t)floor(t);
    int ms = (int)lround((t - (double)s) * 1000.0);
    struct tm tm;
    char date[24];

    if (ms >= 1000) {
        ms -= 1000;
        s += 1;
    } else if (ms < 0) {
        ms = 0;
    }
    gmtime_r(&s, &tm);
    if (strftime(date, sizeof date, "%Y-%m-%d %H:%M:%S", &tm) == 0)
        snprintf(date, sizeof date, "1970-01-01 00:00:00");
    snprintf(out, n, "%s.%03d+00", date, ms);
}

static double iso_to_epoch(const char *s)
{
    int y, mo, d, h, mi;
    double sec;
    struct tm tm;
    time_t e;

    if (!s || sscanf(s, "%4d-%2d-%2d %2d:%2d:%lf",
                     &y, &mo, &d, &h, &mi, &sec) != 6)
        return -1.0;
    memset(&tm, 0, sizeof tm);
    tm.tm_year = y - 1900;
    tm.tm_mon = mo - 1;
    tm.tm_mday = d;
    tm.tm_hour = h;
    tm.tm_min = mi;
    tm.tm_sec = (int)sec;
    e = timegm(&tm);
    return (double)e + (sec - (int)sec);
}

static double pick_time_to_epoch(const char *s)
{
    int y, mo, d, h, mi;
    double ss;
    struct tm tm;
    time_t e;

    if (sscanf(s, "%4d%2d%2d%2d%2d%lf", &y, &mo, &d, &h, &mi, &ss) != 6)
        return -1.0;
    memset(&tm, 0, sizeof tm);
    tm.tm_year = y - 1900;
    tm.tm_mon = mo - 1;
    tm.tm_mday = d;
    tm.tm_hour = h;
    tm.tm_min = mi;
    tm.tm_sec = (int)ss;
    e = timegm(&tm);
    return (double)e + (ss - (int)ss);
}

/* `origin.software`: la version del framework, de $EW_VERSION (la exporta
   ew8_unix.sh) con fallback para tests y ejecuciones fuera de startstop. */
static const char *ew_software(void)
{
    static char cached[64];
    static int  done = 0;

    if (!done) {
        const char *v = getenv("EW_VERSION");
        snprintf(cached, sizeof cached, "%s", (v && v[0]) ? v : "earthworm_8.0");
        done = 1;
    }
    return cached;
}

static const char *mod_algorithm(int mod)
{
    if (mod == 162) return "csnloc";
    if (mod == 163) return "HYPOINVERSE";
    if (mod == 164) return "NLLoc";
    return "unknown";
}

static const char *mod_status(int mod, int manual)
{
    if (manual) return "REVIEWED";
    if (mod == 162) return "PRELIMINARY";
    return "REVISED";
}

static const char *amplitude_type_for(const char *magtype)
{
    if (strcmp(magtype, "ML") == 0) return "AML";
    if (strcmp(magtype, "Mb") == 0) return "AMb";
    if (strcmp(magtype, "Ms") == 0) return "AMS";
    return "AMP";
}

/* "NET.STA.LOC.CHAN" -> campos (los que no aparezcan quedan vacíos). */
static void parse_nslco(const char *ch, char *net, size_t nn, char *sta,
                        size_t ns, char *loc, size_t nl, char *chan, size_t nc)
{
    char work[128];
    char *fields[4] = { NULL, NULL, NULL, NULL };
    char *p, *dot;
    int   nf = 0;

    net[0] = sta[0] = loc[0] = chan[0] = '\0';
    if (!ch)
        return;
    snprintf(work, sizeof work, "%s", ch);
    p = work;
    while (p && nf < 4) {
        fields[nf++] = p;
        dot = strchr(p, '.');
        if (!dot)
            break;
        *dot = '\0';
        p = dot + 1;
    }
    if (nf >= 1) snprintf(net, nn, "%s", fields[0]);
    if (nf >= 2) snprintf(sta, ns, "%s", fields[1]);
    if (nf >= 3) snprintf(loc, nl, "%s", fields[2]);
    if (nf >= 4) snprintf(chan, nc, "%s", fields[3]);
}

/* ------------------------------------------------------------------------- */
/* Estado                                                                     */
/* ------------------------------------------------------------------------- */

HypodbState *hypodb_state_new(const HypodbConfig *cfg)
{
    HypodbState *st;

    if (!cfg)
        return NULL;
    st = calloc(1, sizeof(*st));
    if (!st)
        return NULL;
    st->cfg = *cfg;
    return st;
}

void hypodb_state_free(HypodbState *st)
{
    free(st);
}

void hypodb_state_set_db(HypodbState *st, EwGuiDb *db)
{
    if (st)
        st->db = db;
}

static HypodbGroup *find_group_by_event(HypodbState *st, const char *event_id)
{
    for (int i = 0; i < st->ngroups; i++)
        if (strcmp(st->groups[i].event_id, event_id) == 0)
            return &st->groups[i];
    return NULL;
}

static HypodbGroup *find_group_by_qid(HypodbState *st, int qid)
{
    for (int i = 0; i < st->ngroups; i++)
        if (st->groups[i].key.qid == qid)
            return &st->groups[i];
    return NULL;
}

static HypodbOriginMeta *find_origin_meta(HypodbGroup *g, int mod)
{
    for (int i = 0; i < g->norigins; i++)
        if (g->origins[i].mod == mod)
            return &g->origins[i];
    return NULL;
}

static void update_origin_meta(HypodbGroup *g, const char *origin_id, int mod,
                               int manual, unsigned version, int nph,
                               double rms, double otime)
{
    HypodbOriginMeta *om = find_origin_meta(g, mod);

    if (!om) {
        if (g->norigins >= HYPODB_MAX_ORIGINS)
            return;
        om = &g->origins[g->norigins++];
        memset(om, 0, sizeof(*om));
        om->mod = mod;
    }
    snprintf(om->origin_id, sizeof om->origin_id, "%s", origin_id);
    om->manual = manual;
    om->version = version;
    om->nph = nph;
    om->rms = rms;
    om->otime = otime;
}

static void update_mag_meta(HypodbGroup *g, const char *magnitude_id,
                            const char *type, double value)
{
    for (int i = 0; i < g->nmags; i++) {
        if (strcmp(g->mags[i].magnitude_id, magnitude_id) == 0) {
            g->mags[i].value = value;
            return;
        }
    }
    if (g->nmags >= HYPODB_MAX_MAGS)
        return;
    snprintf(g->mags[g->nmags].magnitude_id,
             sizeof g->mags[g->nmags].magnitude_id, "%s", magnitude_id);
    snprintf(g->mags[g->nmags].type, sizeof g->mags[g->nmags].type, "%s", type);
    g->mags[g->nmags].value = value;
    g->nmags++;
}

/* ------------------------------------------------------------------------- */
/* Núcleo puro: agrupación                                                    */
/* ------------------------------------------------------------------------- */

/* El atajo de agrupación por qid (`csnhypodbp_group_matches`) no comprueba la
   firma de picadas, así que dos sismos de sesiones distintas de csnloc con el
   mismo qid se fusionarían en un solo evento (el rango de qid se repite cada
   ~27,7 h). Se exige además que la hora origen del grupo esté dentro de
   `EventMaxDtSec`. Sin horas conocidas o con la guarda desactivada (<= 0), el
   comportamiento es el previo. */
static int same_event_in_time(const HypodbGroup *g, double otime, double max_dt)
{
    if (!(max_dt > 0.0) || !(otime > 0.0) || !(g->otime > 0.0))
        return 1;
    return (fabs(g->otime - otime) <= max_dt) ? 1 : 0;
}

int hypodb_assign_event(HypodbState *st, const CsnhypodbpSolution *sol,
                        char *event_id_out, size_t n)
{
    int idx[HYPODB_MAX_GROUPS];
    int nm;
    HypodbGroup *g;
    char qb[16];

    if (!st || !sol || !event_id_out || n == 0)
        return -1;

    nm = csnhypodbp_group_matches((const CsnhypodbpGroupKey *)st->groups,
                                  st->ngroups, sol->qid, sol->picks, sol->npicks,
                                  st->cfg.group_pick_tol_sec,
                                  st->cfg.group_min_match_picks,
                                  idx, HYPODB_MAX_GROUPS);

    /* Mismo qid pero otro sismo (sesiones distintas de csnloc): no fusionar. */
    if (nm > 0 && !same_event_in_time(&st->groups[idx[0]], sol->otime,
                                      st->cfg.event_max_dt_sec))
        nm = 0;

    if (nm > 0) {
        g = &st->groups[idx[0]];
    } else {
        if (st->ngroups >= HYPODB_MAX_GROUPS) {
            /* recicla el grupo más antiguo */
            memmove(&st->groups[0], &st->groups[1],
                    sizeof(HypodbGroup) * (HYPODB_MAX_GROUPS - 1));
            st->ngroups--;
        }
        g = &st->groups[st->ngroups++];
        memset(g, 0, sizeof(*g));
        g->key.qid = sol->qid;
        g->otime = sol->otime;
        snprintf(qb, sizeof qb, "%d", sol->qid);
        if (hypodb_event_id(g->event_id, sizeof g->event_id, qb,
                            sol->otime) != 0)
            return -1;
    }

    csnhypodbp_group_union(&g->key, sol->picks, sol->npicks,
                           st->cfg.group_pick_tol_sec);
    snprintf(event_id_out, n, "%s", g->event_id);
    return 0;
}

/* ------------------------------------------------------------------------- */
/* Escritura                                                                  */
/* ------------------------------------------------------------------------- */

static int write_station_magnitudes(HypodbState *st, const char *magnitude_id,
                                    const char *type, const char *msg,
                                    size_t len)
{
    int lines = 0, nchan, n = 0;
    MAG_CHAN_INFO *ch;
    EwGuiStaMagRow *rows;
    const char *p;

    for (p = msg; *p; p++)
        if (*p == '\n')
            lines++;
    nchan = lines - 1;   /* sin la línea resumen */
    if (nchan <= 0)
        return 0;
    if (nchan > HYPODB_MAX_CHAN)
        nchan = HYPODB_MAX_CHAN;

    ch = calloc((size_t)nchan, sizeof(*ch));
    rows = calloc((size_t)nchan, sizeof(*rows));
    if (!ch || !rows) {
        free(ch);
        free(rows);
        return -1;
    }

    if (rd_chan_mag((char *)msg, (int)len, ch,
                    (int)((size_t)nchan * sizeof(*ch))) == 0) {
        for (int i = 0; i < nchan; i++) {
            char chan[64];

            if (hypodb_channel_nslco(chan, sizeof chan, ch[i].net, ch[i].sta,
                                     ch[i].loc, ch[i].comp) != 0)
                continue;
            if (hypodb_station_magnitude_id(rows[n].station_magnitude_id,
                                            sizeof rows[n].station_magnitude_id,
                                            magnitude_id, chan) != 0)
                continue;
            snprintf(rows[n].magnitude_id, sizeof rows[n].magnitude_id, "%s",
                     magnitude_id);
            snprintf(rows[n].channel, sizeof rows[n].channel, "%s", chan);
            rows[n].read_amplitude = ch[i].Amp1;
            rows[n].calculated_magnitude = ch[i].mag;
            snprintf(rows[n].amplitude_type, sizeof rows[n].amplitude_type,
                     "%s", amplitude_type_for(type));
            n++;
        }
    }

    if (n > 0)
        ewguidb_replace_station_magnitudes(st->db, magnitude_id, rows, n);

    free(ch);
    free(rows);
    return 0;
}

static int apply_magnitude(HypodbState *st, HypodbGroup *g, int mod,
                           const char *type, double value, double error,
                           int nsta, const char *algorithm,
                           const char *msg, size_t len)
{
    char origin_id[64], mid[64];
    EwGuiMagRow m;

    if (hypodb_origin_id(origin_id, sizeof origin_id, g->event_id, mod) != 0)
        return -1;
    if (hypodb_magnitude_id(mid, sizeof mid, origin_id, type) != 0)
        return -1;

    if (st->db) {
        memset(&m, 0, sizeof m);
        snprintf(m.magnitude_id, sizeof m.magnitude_id, "%s", mid);
        snprintf(m.origin_id, sizeof m.origin_id, "%s", origin_id);
        m.magnitude_value = value;
        snprintf(m.magnitude_type, sizeof m.magnitude_type, "%s", type);
        m.mag_uncertainty = (error >= 0.0) ? error : NAN;
        /* El metodo viaja en el mensaje: "CSNNet" (automatica) o "CSNMan"
           (ajustada a mano en csnhypodbp). Antes se escribia "CSNNet" fijo y la
           marca manual se perdia al releer de la BD. */
        if (algorithm && algorithm[0])
            snprintf(m.calculation_method, sizeof m.calculation_method, "%s",
                     algorithm);
        m.station_count = nsta;
        if (ewguidb_upsert_magnitude(st->db, &m) != 0)
            return -1;
        write_station_magnitudes(st, mid, type, msg, len);
    }

    update_mag_meta(g, mid, type, value);
    return 0;
}

static int update_preferred(HypodbState *st, HypodbGroup *g)
{
    CsnhypodbpSolMeta metas[HYPODB_MAX_ORIGINS];
    const char *pref_origin = NULL;
    const char *pref_mag = NULL;
    static const char *order[] = { "Mwp", "Ms", "Mb", "ML" };
    int pi;

    if (!st->db)
        return 0;

    for (int i = 0; i < g->norigins; i++) {
        metas[i].manual = g->origins[i].manual;
        metas[i].version = g->origins[i].version;
        metas[i].nph = g->origins[i].nph;
        metas[i].rms = isnan(g->origins[i].rms) ? 9999.0 : g->origins[i].rms;
        metas[i].otime = g->origins[i].otime;
    }
    pi = g->norigins > 0 ? csnhypodbp_preferred_index(metas, g->norigins) : -1;
    if (pi >= 0)
        pref_origin = g->origins[pi].origin_id;

    /* Preferida elegida a mano: el ARC trae en BMTYPI el tipo (L/W/B/S) que la GUI
       puso en el 13.er token de las picadas, y manda sobre el orden fijo. */
    {
        const char *from_arc = NULL;

        switch (g->pref_mag) {
            case 'L': case 'l': from_arc = "ML";  break;
            case 'W': case 'w': from_arc = "Mwp"; break;
            case 'B': case 'b': from_arc = "Mb";  break;
            case 'S': case 's': from_arc = "Ms";  break;
            default: break;
        }
        if (from_arc)
            for (int i = 0; i < g->nmags; i++)
                if (strcmp(g->mags[i].type, from_arc) == 0) {
                    pref_mag = g->mags[i].magnitude_id;
                    break;
                }
    }
    for (int o = 0; o < 4 && !pref_mag; o++)
        for (int i = 0; i < g->nmags; i++)
            if (strcmp(g->mags[i].type, order[o]) == 0) {
                pref_mag = g->mags[i].magnitude_id;
                break;
            }

    return ewguidb_set_preferred(st->db, g->event_id, pref_origin, pref_mag);
}

static int is_manual_pick(HypodbState *st, const CsnhypodbpPick *pk)
{
    for (int i = 0; i < HYPODB_MAX_MANUAL_PICKS; i++) {
        if (!st->mpick[i].used)
            continue;
        if (strcmp(st->mpick[i].sta, pk->sta) != 0)
            continue;
        if (st->mpick[i].phase != pk->phase)
            continue;
        if (fabs(st->mpick[i].t - pk->t_pick) <= st->cfg.pick_match_tol_sec)
            return 1;
    }
    return 0;
}

/* Modelo de velocidades del ARC (linea 1, offsets 123..135). */
static void arc_vmodel(const char *msg, size_t len, char *out, size_t n)
{
    int i;

    out[0] = '\0';
    if (!msg || len < (size_t)(EWGUI_ARC_VMODEL_OFF + EWGUI_ARC_VMODEL_LEN))
        return;
    for (i = 0; i < EWGUI_ARC_VMODEL_LEN && (size_t)i < n - 1; i++)
        out[i] = msg[EWGUI_ARC_VMODEL_OFF + i];
    out[i] = '\0';
    while (i > 0 && (out[i - 1] == ' ' || out[i - 1] == '\t'))
        out[--i] = '\0';
}

static int write_origin(HypodbState *st, HypodbGroup *g,
                        const CsnhypodbpSolution *sol, int mod,
                        const char *vmodel)
{
    char origin_id[64];
    EwGuiEventRow ev;
    EwGuiOriginRow o;
    EwGuiPickRow *rows;
    int nrows = 0;

    if (hypodb_origin_id(origin_id, sizeof origin_id, g->event_id, mod) != 0)
        return -1;

    /* event */
    memset(&ev, 0, sizeof ev);
    snprintf(ev.event_id, sizeof ev.event_id, "%s", g->event_id);
    snprintf(ev.event_type, sizeof ev.event_type, "EARTHQUAKE");
    if (ewguidb_upsert_event(st->db, &ev) != 0)
        return -1;

    /* origin */
    memset(&o, 0, sizeof o);
    snprintf(o.origin_id, sizeof o.origin_id, "%s", origin_id);
    snprintf(o.event_id, sizeof o.event_id, "%s", g->event_id);
    epoch_to_iso(sol->otime, o.origin_time, sizeof o.origin_time);
    o.latitude = sol->lat;
    o.longitude = sol->lon;
    o.depth = sol->depth;
    /* `author` = modulo que revisa/produce el registro (constante); el
       localizador real viaja en `location_algorithm`. */
    snprintf(o.author, sizeof o.author, "csnhypodbp");
    snprintf(o.software, sizeof o.software, "%s", ew_software());
    snprintf(o.location_algorithm, sizeof o.location_algorithm, "%s",
             mod_algorithm(mod));
    if (vmodel && vmodel[0])
        snprintf(o.velocity_model, sizeof o.velocity_model, "%s", vmodel);
    o.azimuthal_gap = (sol->gap > 0) ? (double)sol->gap : NAN;
    o.station_count = sol->nph;
    o.standard_error = (sol->rms > 0.0) ? sol->rms : NAN;
    snprintf(o.status, sizeof o.status, "%s", mod_status(mod, sol->manual));
    if (ewguidb_upsert_origin(st->db, &o) != 0)
        return -1;

    /* picks */
    rows = calloc(CSNHYPODBP_MAX_PICKS, sizeof(*rows));
    if (!rows)
        return -1;
    for (int i = 0; i < sol->npicks && nrows < CSNHYPODBP_MAX_PICKS; i++) {
        const CsnhypodbpPick *pk = &sol->picks[i];
        const char *ph = (pk->phase == 'S') ? "S" : "P";

        if (hypodb_pick_id(rows[nrows].pick_id, sizeof rows[nrows].pick_id,
                           origin_id, pk->sta, ph) != 0)
            continue;
        snprintf(rows[nrows].origin_id, sizeof rows[nrows].origin_id, "%s",
                 origin_id);
        hypodb_channel_nslco(rows[nrows].channel, sizeof rows[nrows].channel,
                             pk->net, pk->sta, pk->loc, pk->chan);
        epoch_to_iso(pk->t_pick, rows[nrows].pick_time,
                     sizeof rows[nrows].pick_time);
        snprintf(rows[nrows].phase, sizeof rows[nrows].phase, "%s", ph);
        rows[nrows].weight = pk->weight;
        /* El residual del localizador viaja en `uncertainty`. */
        rows[nrows].residual = pk->residual;
        snprintf(rows[nrows].pick_status, sizeof rows[nrows].pick_status, "%s",
                 is_manual_pick(st, pk) ? "manual" : "automatic");
        nrows++;
    }
    if (ewguidb_replace_picks(st->db, origin_id, rows, nrows) != 0) {
        free(rows);
        return -1;
    }
    free(rows);

    update_origin_meta(g, origin_id, mod, sol->manual, sol->version, sol->nph,
                       sol->rms, sol->otime);
    return 0;
}

static void flush_pending_mags(HypodbState *st, HypodbGroup *g, int mod)
{
    for (int i = 0; i < HYPODB_MAX_PENDING_MAGS; i++) {
        HypodbPendingMag *pm = &st->pmag[i];
        if (!pm->used || pm->qid != g->key.qid || pm->mod != mod)
            continue;
        apply_magnitude(st, g, pm->mod, pm->type, pm->value, pm->error,
                        pm->nsta, pm->algorithm, pm->msg, (size_t)pm->msg_len);
        pm->used = 0;
    }
}

static void store_pending_mag(HypodbState *st, int qid, int mod,
                              const char *type, double value, double error,
                              int nsta, const char *algorithm,
                              const char *msg, size_t len)
{
    for (int i = 0; i < HYPODB_MAX_PENDING_MAGS; i++) {
        HypodbPendingMag *pm = &st->pmag[i];
        if (pm->used)
            continue;
        pm->used = 1;
        pm->qid = qid;
        pm->mod = mod;
        snprintf(pm->type, sizeof pm->type, "%s", type);
        pm->value = value;
        pm->error = error;
        pm->nsta = nsta;
        snprintf(pm->algorithm, sizeof pm->algorithm, "%s",
                 (algorithm && algorithm[0]) ? algorithm : "");
        pm->msg_len = (len < sizeof(pm->msg)) ? (int)len
                                              : (int)sizeof(pm->msg) - 1;
        memcpy(pm->msg, msg, (size_t)pm->msg_len);
        pm->msg[pm->msg_len] = '\0';
        return;
    }
}

/* ------------------------------------------------------------------------- */
/* Entradas                                                                   */
/* ------------------------------------------------------------------------- */

int hypodb_feed_arc(HypodbState *st, const char *msg, size_t len, int mod)
{
    CsnhypodbpSolution sol;
    HypodbGroup *g;
    HypodbOriginMeta *om;
    char eid[64];

    if (!st || !msg)
        return -1;
    if (csnhypodbp_parse_solution(msg, len, mod, &sol) != 0)
        return -1;
    if (hypodb_assign_event(st, &sol, eid, sizeof eid) != 0)
        return -1;

    g = find_group_by_event(st, eid);
    if (!g)
        return -1;

    om = find_origin_meta(g, mod);
    if (om && sol.version <= om->version)
        return 0;   /* versión vieja o repetida: la última manda */

    if (st->db) {
        char vmodel[64];
        arc_vmodel(msg, len, vmodel, sizeof vmodel);
        if (write_origin(st, g, &sol, mod, vmodel) != 0)
            return -1;
        flush_pending_mags(st, g, mod);
        if (sol.pref_mag) g->pref_mag = sol.pref_mag;
        update_preferred(st, g);
    } else {
        char origin_id[64];
        if (hypodb_origin_id(origin_id, sizeof origin_id, g->event_id, mod) == 0)
            update_origin_meta(g, origin_id, mod, sol.manual, sol.version,
                               sol.nph, sol.rms, sol.otime);
    }
    return 0;
}

int hypodb_feed_mag(HypodbState *st, const char *msg, size_t len)
{
    MAG_INFO mag;
    HypodbGroup *g;
    int qid, mod;
    const char *type;

    if (!st || !msg || len == 0)
        return -1;

    setlocale(LC_NUMERIC, "C");
    memset(&mag, 0, sizeof mag);
    if (rd_mag((char *)msg, (int)len, &mag) != 0)
        return -1;

    qid = atoi(mag.qid);
    mod = (int)mag.origin_version;
    type = mag.szmagtype;
    if (!type[0])
        return -1;

    g = find_group_by_qid(st, qid);
    if (g && find_origin_meta(g, mod)) {
        apply_magnitude(st, g, mod, type, mag.mag, mag.error, mag.nstations,
                        mag.algorithm, msg, len);
        if (st->db)
            update_preferred(st, g);
    } else {
        store_pending_mag(st, qid, mod, type, mag.mag, mag.error,
                          mag.nstations, mag.algorithm, msg, len);
    }
    return 0;
}

int hypodb_feed_pick(HypodbState *st, const char *msg, size_t len)
{
    char scnl[64] = "", fmwt[16] = "", tstr[64] = "";
    char ptok[8] = "", otok[8] = "";
    int  type, mod, inst, seq, amp1, amp2, amp3;
    char *dot, sta[8] = "";
    char ph;
    double t;

    if (!st || !msg || len == 0)
        return -1;

    if (sscanf(msg, "%d %d %d %d %63s %15s %63s %d %d %d",
               &type, &mod, &inst, &seq, scnl, fmwt, tstr,
               &amp1, &amp2, &amp3) < 7)
        return -1;

    sscanf(msg, "%*d %*d %*d %*d %*63s %*15s %*63s %*d %*d %*d %7s %7s",
           ptok, otok);
    if (!(otok[0] == 'M' || otok[0] == 'm'))
        return 0;   /* solo interesan los manuales */

    dot = strchr(scnl, '.');
    if (dot) {
        size_t l = (size_t)(dot - scnl);
        if (l >= sizeof sta)
            l = sizeof sta - 1;
        memcpy(sta, scnl, l);
        sta[l] = '\0';
    } else {
        snprintf(sta, sizeof sta, "%s", scnl);
    }

    ph = (ptok[0] == 'S' || ptok[0] == 's') ? 'S' : 'P';
    t = pick_time_to_epoch(tstr);
    if (t <= 0.0)
        return -1;

    HypodbManualPick *mp = &st->mpick[st->mpick_next];
    st->mpick_next = (st->mpick_next + 1) % HYPODB_MAX_MANUAL_PICKS;
    mp->used = 1;
    snprintf(mp->sta, sizeof mp->sta, "%s", sta);
    mp->phase = ph;
    mp->t = t;
    return 0;
}

int hypodb_load_from_db(HypodbState *st)
{
    EwGuiEventRow *evs = NULL;
    int nev = 0;

    if (!st || !st->db)
        return 0;
    if (ewguidb_query_recent_events(st->db, st->cfg.event_load_hours, &evs,
                                    &nev) < 0)
        return -1;

    for (int i = 0; i < nev && st->ngroups < HYPODB_MAX_GROUPS; i++) {
        HypodbGroup *g = &st->groups[st->ngroups];
        EwGuiOriginRow *ors = NULL;
        int nor = 0;

        memset(g, 0, sizeof(*g));
        snprintf(g->event_id, sizeof g->event_id, "%s", evs[i].event_id);
        g->key.qid = atoi(evs[i].event_id[0] == 'E' ? evs[i].event_id + 1
                                                    : evs[i].event_id);

        if (ewguidb_query_origins(st->db, evs[i].event_id, &ors, &nor) > 0) {
            for (int j = 0; j < nor; j++) {
                const char *colon = strrchr(ors[j].origin_id, ':');
                int mod = colon ? atoi(colon + 1) : 0;
                EwGuiPickRow *pks = NULL;
                int npk = 0;

                update_origin_meta(g, ors[j].origin_id, mod,
                                   strcmp(ors[j].status, "REVIEWED") == 0,
                                   0, ors[j].station_count,
                                   isnan(ors[j].standard_error)
                                       ? 9999.0 : ors[j].standard_error, 0.0);
                if (!(g->otime > 0.0))
                    g->otime = iso_to_epoch(ors[j].origin_time);

                if (ewguidb_query_picks(st->db, ors[j].origin_id, &pks, &npk)
                    > 0) {
                    CsnhypodbpPick tmp[CSNHYPODBP_MAX_PICKS];
                    int nt = 0;

                    for (int k = 0; k < npk && nt < CSNHYPODBP_MAX_PICKS; k++) {
                        char net[4], sta[8], loc[4], chan[8];
                        double t;

                        parse_nslco(pks[k].channel, net, sizeof net,
                                    sta, sizeof sta, loc, sizeof loc,
                                    chan, sizeof chan);
                        t = iso_to_epoch(pks[k].pick_time);
                        if (t <= 0.0 || !sta[0])
                            continue;
                        memset(&tmp[nt], 0, sizeof tmp[nt]);
                        snprintf(tmp[nt].sta, sizeof tmp[nt].sta, "%s", sta);
                        tmp[nt].phase = (pks[k].phase[0] == 'S') ? 'S' : 'P';
                        tmp[nt].t_pick = t;
                        nt++;
                    }
                    if (nt > 0)
                        csnhypodbp_group_union(&g->key, tmp, nt,
                                               st->cfg.group_pick_tol_sec);
                    ewguidb_rows_free(pks);
                }
            }
            ewguidb_rows_free(ors);
        }
        st->ngroups++;
    }

    ewguidb_rows_free(evs);
    return 0;
}
