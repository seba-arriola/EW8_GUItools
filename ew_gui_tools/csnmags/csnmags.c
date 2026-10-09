#include "csnmags.h"

#include <string.h>
#include <locale.h>
#include <unistd.h>
#include <sys/types.h>

#include <kom.h>
#include <socket_ew.h>
#include <ws_clientII.h>

/*
 * csnmags.c - Motor de magnitudes ML/Mwp/Mb/Ms.
 *
 *   csnmags <csnmags.d>                                   modo anillo
 *   csnmags <csnmags.d> <hypos.jsonl> <tank> [--out F]    modo offline
 *
 * Tipo de magnitud índice: 0=ML 1=Mwp 2=Mb 3=Ms
 */

#define MT_ML   0
#define MT_MWP  1
#define MT_MB   2
#define MT_MS   3
#define MT_N    4

/* MAGTYPE_* de EarthWorm (earthworm_defs.h) */
#define IMAG_ML   1
#define IMAG_MB   3
#define IMAG_MS   4
#define IMAG_MWP  5

typedef struct { char sta[8]; double mag; } MagStaMag;

typedef struct {
    int    active;
    char   event_id[24];
    double t0, lat, lon, depth;
    unsigned int version;
    int    nsta[MT_N];
    MagStaMag sm[MT_N][MAG_MAX_STA];
    double net[MT_N];
    int    used[MT_N];
    double err[MT_N];
    int    calc[MT_N];
    time_t last_update;
} MagEvent;

typedef struct {
    MagConfig      cfg;
    MagStationList stas;
    MagCalib       calib;
    WaveSource     wave;
    MagEvent       ev[MAG_MAX_EVENTS];
    /* Earthworm */
    unsigned char  MyInstId, MyModId, TypeHeartBeat, TypeError,
                   TypeHyp2000Arc, TypeMagnitude;
    SHM_INFO       region;
    long           in_key;
    int            ring_attached;
    pid_t          MyPid;
    /* offline */
    int            offline;
    FILE          *off;
} MagCtx;

/* ------------------------------ eventos ------------------------------ */

static MagEvent *event_get(MagCtx *c, const char *id)
{
    int i, slot = -1;
    time_t now = time(NULL);
    for (i = 0; i < MAG_MAX_EVENTS; i++) {
        if (c->ev[i].active && strcmp(c->ev[i].event_id, id) == 0) return &c->ev[i];
        if (!c->ev[i].active && slot < 0) slot = i;
    }
    if (slot < 0) {
        time_t oldest = now; slot = 0;
        for (i = 0; i < MAG_MAX_EVENTS; i++)
            if (c->ev[i].last_update < oldest) { oldest = c->ev[i].last_update; slot = i; }
    }
    memset(&c->ev[slot], 0, sizeof(c->ev[slot]));
    c->ev[slot].active = 1;
    snprintf(c->ev[slot].event_id, sizeof(c->ev[slot].event_id), "%s", id);
    c->ev[slot].last_update = now;
    return &c->ev[slot];
}

static void event_register(MagCtx *c, MagEvent *ev, int type, const char *sta, double mag)
{
    int i, *n = &ev->nsta[type];
    (void)c;
    for (i = 0; i < *n; i++) {
        if (strcmp(ev->sm[type][i].sta, sta) == 0) { ev->sm[type][i].mag = mag; return; }
    }
    if (*n >= MAG_MAX_STA) return;
    snprintf(ev->sm[type][*n].sta, sizeof(ev->sm[type][*n].sta), "%s", sta);
    ev->sm[type][*n].mag = mag;
    (*n)++;
}

static void event_update_net(MagEvent *ev, int type, const MagConfig *cfg)
{
    double vals[MAG_MAX_STA];
    int i, n = ev->nsta[type];
    for (i = 0; i < n; i++) vals[i] = ev->sm[type][i].mag;
    if (n == 0) { ev->calc[type] = 0; return; }
    ev->net[type] = mag_net(vals, n, cfg->use_median, cfg->trunc_k,
                            &ev->err[type], &ev->used[type]);
    ev->calc[type] = (ev->net[type] > 0.0) ? 1 : 0;
}

/* ------------------------------ geofísica ------------------------------ */

static int chan_try(WaveSource *w, const char *sta, const char *net,
                    const char *chan_list[], const char *loc,
                    double t0, double t1, MagTrace *out, char *chan_out, size_t csz)
{
    int i;
    for (i = 0; chan_list[i]; i++) {
        if (wave_get(w, sta, net, chan_list[i], loc, t0, t1, out) == 0) {
            if (chan_out) snprintf(chan_out, csz, "%s", chan_list[i]);
            return 0;
        }
    }
    return -1;
}

/* Deconvoluciona in-place a la respuesta objetivo. Devuelve 0 si OK. */
static int deconv(const MagConfig *cfg, const MagStation *st, const char *chan,
                  double *x, long n, double dt, const MagPZ *target)
{
    MagResponse r;
    MagStation s2 = *st;
    int rc;
    snprintf(s2.chan, sizeof(s2.chan), "%s", chan);
    if (mag_resp_load(cfg, &s2, &r) && r.has_pz) {
        rc = mag_resp_convert(&r.pz, target, x, n, dt);
        mag_resp_free(&r);
        if (rc == 0) return 0;
    }
    /* Fallback sin PZ: escalar por gain (asumiendo counts ~ nm*gain). */
    {
        double g = (st->gain > 0.0) ? st->gain : 1.0e9;
        long i;
        for (i = 0; i < n; i++) x[i] /= g;
    }
    return 0;
}

/* ------------------------------ magnitudes ------------------------------ */

static void compute_ml(MagCtx *c, MagEvent *ev, const MagStation *st,
                       double pick_t, const char *loc, int *ok, double *mag)
{
    static const char *hz[]  = { "HHE", "BHE", "HNE", NULL };
    static const char *hz2[] = { "HHN", "BHN", "HNN", NULL };
    static const char *hz3[] = { "HHZ", "BHZ", "HNZ", NULL };
    MagTrace tr;
    MagPZ wa;
    double dist_deg, epi_km, dist_km, best = 0.0;
    double t0 = pick_t + c->cfg.ml_t_a - 2.0;
    double t1 = pick_t + c->cfg.ml_t_b;
    char chan[8];

    *ok = 0; *mag = 0.0;
    dist_deg = mag_great_circle_deg(ev->lat, ev->lon, st->lat, st->lon);
    if (dist_deg > c->cfg.ml.max_delta) return;
    epi_km = mag_epi_km(dist_deg);
    dist_km = (c->cfg.ml_dist_type == 0) ? mag_hypo_km(epi_km, ev->depth) : epi_km;
    if (dist_km > c->cfg.ml_max_dist) return;

    if (mag_resp_make_wa(&wa, c->cfg.wa_period, c->cfg.wa_damp, c->cfg.wa_gain) != 0) return;

    if (chan_try(&c->wave, st->sta, st->net, hz, loc, t0, t1, &tr, chan, sizeof(chan)) == 0) {
        if (deconv(&c->cfg, st, chan, tr.x, tr.n, tr.dt, &wa) == 0) {
            double a = mag_amp_peak2peak(tr.x, tr.n, 0, tr.n, tr.dt, c->cfg.ml_slide_len);
            if (a > best) best = a;
        }
        free(tr.x);
    }
    if (chan_try(&c->wave, st->sta, st->net, hz2, loc, t0, t1, &tr, chan, sizeof(chan)) == 0) {
        if (deconv(&c->cfg, st, chan, tr.x, tr.n, tr.dt, &wa) == 0) {
            double a = mag_amp_peak2peak(tr.x, tr.n, 0, tr.n, tr.dt, c->cfg.ml_slide_len);
            if (a > best) best = a;
        }
        free(tr.x);
    }
    if (best <= 0.0 && c->cfg.ml_allow_vert &&
        chan_try(&c->wave, st->sta, st->net, hz3, loc, t0, t1, &tr, chan, sizeof(chan)) == 0) {
        if (deconv(&c->cfg, st, chan, tr.x, tr.n, tr.dt, &wa) == 0) {
            double a = mag_amp_peak2peak(tr.x, tr.n, 0, tr.n, tr.dt, c->cfg.ml_slide_len);
            if (a > best) best = a;
        }
        free(tr.x);
    }
    if (best <= 0.0) return;
    {
        const Tab1D *loga0 = (c->calib.loga0_global.n > 0) ? &c->calib.loga0_global : NULL;
        (*mag) = mag_ml(best / c->cfg.wa_gain, dist_km, loga0,
                        c->cfg.ml_c1, c->cfg.ml_c2, c->cfg.ml_c3) + st->corr_ml;
    }
    *ok = 1;
}

static void compute_mwp(MagCtx *c, MagEvent *ev, const MagStation *st,
                        double pick_t, const char *loc, int *ok, double *mag)
{
    static const char *hz[] = { "HHZ", "BHZ", "HNZ", NULL };
    MagTrace tr;
    MagPZ disp;
    double dist_deg, epi_km, dist_km, t0, t1, cum = 0.0, mn = 0.0, mx = 0.0;
    long i, i0, i1;

    *ok = 0; *mag = 0.0;
    dist_deg = mag_great_circle_deg(ev->lat, ev->lon, st->lat, st->lon);
    if (dist_deg < c->cfg.mwp.min_delta || dist_deg > c->cfg.mwp.max_delta) return;
    epi_km = mag_epi_km(dist_deg);
    dist_km = mag_hypo_km(epi_km, ev->depth);

    char chan[8];
    t0 = pick_t + c->cfg.mwp_start_off - 2.0;
    t1 = pick_t + c->cfg.mwp_start_off + c->cfg.mwp_t0;
    if (chan_try(&c->wave, st->sta, st->net, hz, loc, t0, t1, &tr, chan, sizeof(chan)) != 0) return;

    mag_resp_make_disp(&disp, 1.0);
    deconv(&c->cfg, st, chan, tr.x, tr.n, tr.dt, &disp);

    i0 = (long)(((pick_t + c->cfg.mwp_start_off) - tr.t0) / tr.dt);
    i1 = (long)((pick_t + c->cfg.mwp_start_off + c->cfg.mwp_t0 - tr.t0) / tr.dt);
    if (i0 < 0) i0 = 0;
    if (i1 > tr.n) i1 = tr.n;
    if (i1 - i0 < 5) { free(tr.x); return; }

    for (i = i0; i < i1; i++) {
        cum += tr.x[i] * tr.dt;                 /* nm*s */
        if (i == i0) { mn = mx = cum; }
        if (cum < mn) mn = cum;
        if (cum > mx) mx = cum;
    }
    free(tr.x);
    *mag = mag_mwp(mx - mn, dist_km, dist_deg, c->cfg.mwp_rho, c->cfg.mwp_alpha,
                   c->cfg.mwp_fp) + 0.0;
    if (*mag > 0.0) *ok = 1;
}

static void compute_mb(MagCtx *c, MagEvent *ev, const MagStation *st,
                       double pick_t, const char *loc, int *ok, double *mag)
{
    static const char *hz[] = { "HHZ", "BHZ", "HNZ", NULL };
    MagTrace tr;
    MagPZ disp;
    double dist_deg, epi_km, dist_km, t0, t1, amp, per;
    long i0, i1;

    *ok = 0; *mag = 0.0;
    dist_deg = mag_great_circle_deg(ev->lat, ev->lon, st->lat, st->lon);
    if (dist_deg < c->cfg.mb.min_delta || dist_deg > c->cfg.mb.max_delta) return;
    epi_km = mag_epi_km(dist_deg);
    dist_km = mag_hypo_km(epi_km, ev->depth);
    (void)dist_km;

    char chan[8];
    t0 = pick_t - 1.0;
    t1 = pick_t + c->cfg.mb_window;
    if (chan_try(&c->wave, st->sta, st->net, hz, loc, t0, t1, &tr, chan, sizeof(chan)) != 0) return;

    mag_resp_make_disp(&disp, 1.0);
    deconv(&c->cfg, st, chan, tr.x, tr.n, tr.dt, &disp);
    if (c->cfg.mb.band_lo > 0.0 || c->cfg.mb.band_hi > 0.0)
        mag_bandpass(tr.x, tr.n, tr.dt, c->cfg.mb.band_lo, c->cfg.mb.band_hi, 4);

    i0 = (long)((pick_t - tr.t0) / tr.dt);
    i1 = (long)((pick_t + c->cfg.mb_window - tr.t0) / tr.dt);
    if (i0 < 0) i0 = 0;
    if (i1 > tr.n) i1 = tr.n;
    if (i1 - i0 < 5) { free(tr.x); return; }

    amp = mag_amp_zero2peak(tr.x, tr.n, i0, i1);
    per = mag_amp_period(tr.x, tr.n, i0, i1, tr.dt);
    free(tr.x);
    if (per <= 0.0 || per > 5.0) per = c->cfg.mb_period;
    *mag = mag_mb(amp, per, dist_deg, &c->calib.q_global) + st->corr_mb;
    if (*mag > 0.0) *ok = 1;
}

static void compute_ms(MagCtx *c, MagEvent *ev, const MagStation *st,
                       double pick_t, const char *loc, int *ok, double *mag)
{
    static const char *hz[] = { "HHZ", "BHZ", "HNZ", NULL };
    MagTrace tr;
    MagPZ disp;
    double dist_deg, epi_km, dist_km, t0, t1, amp, per;

    *ok = 0; *mag = 0.0;
    dist_deg = mag_great_circle_deg(ev->lat, ev->lon, st->lat, st->lon);
    if (dist_deg < c->cfg.ms.min_delta || dist_deg > c->cfg.ms.max_delta) return;
    if (c->cfg.ms_deep_corr == 0 && ev->depth > 60.0) return;
    epi_km = mag_epi_km(dist_deg);
    dist_km = mag_hypo_km(epi_km, ev->depth);
    (void)dist_km;

    char chan[8];
    t0 = pick_t;
    t1 = pick_t + 400.0;
    if (chan_try(&c->wave, st->sta, st->net, hz, loc, t0, t1, &tr, chan, sizeof(chan)) != 0) return;

    mag_resp_make_disp(&disp, 1.0);
    deconv(&c->cfg, st, chan, tr.x, tr.n, tr.dt, &disp);
    mag_bandpass(tr.x, tr.n, tr.dt, c->cfg.ms_band_lo, c->cfg.ms_band_hi, 4);

    amp = mag_amp_zero2peak(tr.x, tr.n, 0, tr.n);
    per = mag_amp_period(tr.x, tr.n, 0, tr.n, tr.dt);
    free(tr.x);
    if (per <= 0.0) per = c->cfg.ms_t;
    *mag = mag_ms(amp, per, dist_deg, c->cfg.ms_variant, c->cfg.ms_k1, c->cfg.ms_k2)
           + st->corr_ms;
    if (*mag > 0.0) *ok = 1;
}

/* ------------------------------ pipeline ------------------------------ */

typedef void (*ComputeFn)(MagCtx *, MagEvent *, const MagStation *, double,
                          const char *, int *, double *);

static ComputeFn g_fn[MT_N] = { compute_ml, compute_mwp, compute_mb, compute_ms };
/* Alineado con MT_*: MT_ML=0, MT_MWP=1, MT_MB=2, MT_MS=3 */
static const int g_imag[MT_N] = { IMAG_ML, IMAG_MWP, IMAG_MB, IMAG_MS };

static void process_station_type(MagCtx *c, MagEvent *ev, const MagStation *st,
                                 int type, double pick_t, const char *loc)
{
    int ok = 0; double mag = 0.0;
    MagGate gate = (type == MT_ML) ? c->cfg.ml :
                   (type == MT_MWP) ? c->cfg.mwp :
                   (type == MT_MB) ? c->cfg.mb : c->cfg.ms;
    if (!gate.enabled) return;
    g_fn[type](c, ev, st, pick_t, loc, &ok, &mag);
    if (ok && mag > 0.0) event_register(c, ev, type, st->sta, mag);
}

/* Devuelve la magnitud de un evento para el tipo dado (0 si no calculada). */

/* ------------------------------ offline ------------------------------ */

static void offline_run(MagCtx *c, const char *hypofile, const char *tank, FILE *out)
{
    MagHypo h;
    (void)tank;

    if (mag_hypos_open(hypofile) != 0) { fprintf(stderr, "csnmags: no abro %s\n", hypofile); return; }

    while (mag_hypos_next(&h) == 1) {
        int p, type;
        MagEvent *ev = event_get(c, h.event_id);
        ev->t0 = h.t0; ev->lat = h.lat; ev->lon = h.lon; ev->depth = h.depth_km;

        for (p = 0; p < h.nphases; p++) {
            int sidx = mag_sta_find(&c->stas, h.ph[p].sta, h.ph[p].net, h.ph[p].chan, h.ph[p].loc);
            if (sidx < 0) continue;
            for (type = 0; type < MT_N; type++)
                process_station_type(c, ev, &c->stas.st[sidx], type, h.ph[p].t_epoch, h.ph[p].loc);
        }
        for (type = 0; type < MT_N; type++) event_update_net(ev, type, &c->cfg);

        mag_json_emit(out, ev->event_id, ev->t0, ev->lat, ev->lon, ev->depth,
                      ev->calc[MT_ML], ev->net[MT_ML], ev->used[MT_ML],
                      ev->calc[MT_MWP], ev->net[MT_MWP], ev->used[MT_MWP],
                      ev->calc[MT_MB], ev->net[MT_MB], ev->used[MT_MB],
                      ev->calc[MT_MS], ev->net[MT_MS], ev->used[MT_MS]);
        printf("csnmags: evento %s  ML=%.2f(%d) Mwp=%.2f(%d) Mb=%.2f(%d) Ms=%.2f(%d)\n",
               ev->event_id, ev->net[MT_ML], ev->used[MT_ML],
               ev->net[MT_MWP], ev->used[MT_MWP],
               ev->net[MT_MB], ev->used[MT_MB],
               ev->net[MT_MS], ev->used[MT_MS]);
        ev->active = 0;
    }
    mag_hypos_close();
}

/* ------------------------------ ring ------------------------------ */

static void lookup(MagCtx *c)
{
    if (GetLocalInst(&c->MyInstId) != 0 ||
        GetModId(c->cfg.my_mod_id, &c->MyModId) != 0 ||
        GetType("TYPE_HEARTBEAT", &c->TypeHeartBeat) != 0 ||
        GetType("TYPE_ERROR", &c->TypeError) != 0 ||
        GetType("TYPE_HYP2000ARC", &c->TypeHyp2000Arc) != 0 ||
        GetType("TYPE_MAGNITUDE", &c->TypeMagnitude) != 0) {
        fprintf(stderr, "csnmags: error en Lookup (revisa MyModuleId/anillos)\n");
        exit(-1);
    }
    if ((c->in_key = GetKey(c->cfg.in_ring)) == -1) {
        fprintf(stderr, "csnmags: no encuentro el anillo %s\n", c->cfg.in_ring);
        exit(-1);
    }
}

static double epoch_from_str(const char *s14, const char *s_ss)
{
    int y = atoi((char[]){s14[0], s14[1], s14[2], s14[3], 0});
    int mo = atoi((char[]){s14[4], s14[5], 0});
    int d  = atoi((char[]){s14[6], s14[7], 0});
    int hh = atoi((char[]){s14[8], s14[9], 0});
    int mi = atoi((char[]){s14[10], s14[11], 0});
    double ss = s_ss ? atof(s_ss) : atoi((char[]){s14[12], s14[13], 0});
    return mag_ymdhms_epoch(y, mo, d, hh, mi, ss);
}

static int parse_arc(MagCtx *c, const char *msg, MagEvent **evout)
{
    char event_id[24] = {0}, otime[16] = {0};
    char *pe, *line;
    int  line_count = 0, got = 0;
    double eq_lat, eq_lon;

    snprintf(event_id, sizeof(event_id), "%.10s", msg + 136);
    for (pe = event_id + strlen(event_id) - 1; pe >= event_id && *pe == ' '; pe--) *pe = '\0';
    snprintf(otime, sizeof(otime), "%.14s", msg);

    {
        char latd[3] = {0}, latm[6] = {0}, lond[4] = {0}, lonm[6] = {0};
        char ldir, ndir;
        snprintf(latd, sizeof(latd), "%.2s", msg + 16); ldir = msg[18];
        snprintf(latm, sizeof(latm), "%.4s", msg + 19);
        snprintf(lond, sizeof(lond), "%.3s", msg + 23); ndir = msg[26];
        snprintf(lonm, sizeof(lonm), "%.4s", msg + 27);
        eq_lat = atof(latd) + (atof(latm) / 100.0) / 60.0; if (ldir == 'S') eq_lat = -eq_lat;
        eq_lon = atof(lond) + (atof(lonm) / 100.0) / 60.0; if (ndir == 'W') eq_lon = -eq_lon;
    }

    {
        MagEvent *ev = event_get(c, event_id);
        unsigned int ver = 0; char vs[5] = {0};
        snprintf(vs, sizeof(vs), "%.4s", msg + 178); ver = (unsigned int)atoi(vs);
        if (ver && ver <= ev->version) return 0;
        ev->version = ver;
        ev->t0 = epoch_from_str(otime, NULL);
        ev->lat = eq_lat; ev->lon = eq_lon; ev->depth = 10.0;
        *evout = ev;
    }

    line = (char *)msg;
    while (line && *line) {
        char *nl = strchr(line, '\n');
        int   len = nl ? (int)(nl - line) : (int)strlen(line);
        if (len >= 114 && len < 150 && line[0] != '$' && line_count >= 2) {
            char sta[8] = {0}, net[4] = {0}, comp[6] = {0}, loc[6] = {0}, pts[20] = {0};
            char *p;
            snprintf(sta, sizeof(sta), "%.5s", line + 0);
            snprintf(net, sizeof(net), "%.2s", line + 5);
            snprintf(comp, sizeof(comp), "%.3s", line + 9);
            snprintf(loc, sizeof(loc), "%.2s", line + 111);
            snprintf(pts, sizeof(pts), "%.17s", line + 17);
            for (p = sta + strlen(sta) - 1; p >= sta && *p == ' '; p--) *p = '\0';
            for (p = net + strlen(net) - 1; p >= net && *p == ' '; p--) *p = '\0';
            for (p = comp + strlen(comp) - 1; p >= comp && *p == ' '; p--) *p = '\0';
            if (loc[0] == ' ' || loc[0] == '\0') snprintf(loc, sizeof(loc), "--");
            {
                int sidx = mag_sta_find(&c->stas, sta, net, comp, loc);
                double pt;
                if (sidx < 0) goto next;
                pt = epoch_from_str(pts, pts + 12);
                if (pt < (*evout)->t0) pt = (*evout)->t0;   /* saneado mínimo */
                for (int type = 0; type < MT_N; type++)
                    process_station_type(c, *evout, &c->stas.st[sidx], type, pt, loc);
                got = 1;
            }
        }
next:
        if (nl) line = nl + 1; else break;
        line_count++;
    }
    return got;
}

static const char *pref_mag_type(const MagCtx *c, MagEvent *ev, double *val)
{
    (void)c;
    if (ev->calc[MT_MWP] && ev->used[MT_MWP] >= 3 && ev->net[MT_MWP] >= 5.5) { *val = ev->net[MT_MWP]; return "Mwp"; }
    if (ev->calc[MT_MS]  && ev->used[MT_MS]  >= 3) { *val = ev->net[MT_MS];  return "Ms"; }
    if (ev->calc[MT_MB]  && ev->used[MT_MB]  >= 3) { *val = ev->net[MT_MB];  return "Mb"; }
    if (ev->calc[MT_ML]  && ev->used[MT_ML]  >  0) { *val = ev->net[MT_ML];  return "ML"; }
    *val = 0.0; return "None";
}

static void status_beat(MagCtx *c)
{
    MSG_LOGO logo;
    char msg[64];
    time_t t;
    logo.instid = c->MyInstId; logo.mod = c->MyModId; logo.type = c->TypeHeartBeat;
    time(&t);
    snprintf(msg, sizeof(msg), "%ld %d\n", (long)t, (int)getpid());
    tport_putmsg(&c->region, &logo, (long)strlen(msg), msg);
}

static void ring_run(MagCtx *c)
{
    MSG_LOGO getlogo[1], reclogo;
    long recsize;
    char msg[65536];
    int  res;
    time_t now, lastbeat = 0;

    c->MyPid = getpid();
    tport_attach(&c->region, c->in_key);
    c->ring_attached = 1;
    getlogo[0].instid = 0; getlogo[0].mod = 0; getlogo[0].type = c->TypeHyp2000Arc;

    logit("t", "csnmags: Listo. Esperando hipocentros en %s\n", c->cfg.in_ring);

    while (tport_getflag(&c->region) != TERMINATE &&
           tport_getflag(&c->region) != c->MyPid) {
        MagEvent *ev = NULL;
        time(&now);
        if (now - lastbeat >= c->cfg.heartbeat) { lastbeat = now; status_beat(c); }

        res = tport_getmsg(&c->region, getlogo, 1, &reclogo, &recsize, msg, sizeof(msg) - 1);
        if (res == GET_OK || res == GET_MISS) {
            int type;
            msg[recsize] = '\0';
            if (parse_arc(c, msg, &ev) && ev) {
                MSG_LOGO logo; double pref; const char *pt;
                for (type = 0; type < MT_N; type++) event_update_net(ev, type, &c->cfg);
                pt = pref_mag_type(c, ev, &pref);
                logit("t", "CSNmags_Red: [ID %s] ML=%.2f (%d) | Mb=%.2f (%d) | Ms=%.2f (%d) | MWp=%.2f (%d) -> PREF: %s %.2f\n",
                      ev->event_id, ev->net[MT_ML], ev->used[MT_ML],
                      ev->net[MT_MB], ev->used[MT_MB],
                      ev->net[MT_MS], ev->used[MT_MS],
                      ev->net[MT_MWP], ev->used[MT_MWP], pt, pref);
                logo.instid = c->MyInstId; logo.mod = c->MyModId; logo.type = c->TypeMagnitude;
                for (type = 0; type < MT_N; type++) {
                    if (ev->calc[type] && ev->used[type] > 0)
                        mag_out_publish(&c->region, &logo, ev->event_id, g_imag[type],
                                        mag_magtype_name(g_imag[type]), ev->net[type],
                                        ev->err[type], 1.0, 0.0, -1, ev->used[type]);
                }
            }
        } else {
            sleep_ew(500);
        }
    }
}

/* ------------------------------ main ------------------------------ */

int main(int argc, char **argv)
{
    MagCtx c;

    /* Los mensajes TYPE_MAGNITUDE usan sprintf/sscanf de EarthWorm, sensibles
     * al locale (separador decimal). Forzamos C para publicar siempre con
     * punto, independientemente del entorno del proceso. */
    setlocale(LC_NUMERIC, "C");

    /* Modo de prueba: parsea un ARC real y calcula con un tank (ruta online). */
    if (argc >= 5 && strcmp(argv[1], "--test-arc") == 0) {
        const char *arcf = argv[3];
        FILE *fp;
        char *buf;
        size_t n;
        int type;
        MagEvent *ev = NULL;

        memset(&c, 0, sizeof(c));
        if (mag_config_load(argv[2], &c.cfg) != 0) { fprintf(stderr, "config?\n"); return 1; }
        mag_sta_load(c.cfg.sta_file, &c.stas);
        mag_sta_apply_corr(&c.stas, c.cfg.sta_corr_file);
        mag_calib_load(&c.cfg, &c.calib);
        if (wave_open(&c.wave, &c.cfg, argv[4]) != 0) { fprintf(stderr, "tank?\n"); return 1; }
        fp = fopen(arcf, "rb");
        if (!fp) { perror(arcf); return 1; }
        buf = malloc(524288);
        n = fread(buf, 1, 524287, fp);
        buf[n] = '\0';
        fclose(fp);
        if (parse_arc(&c, buf, &ev) && ev) {
            for (type = 0; type < MT_N; type++) event_update_net(ev, type, &c.cfg);
            printf("ARC %s: ML=%.2f(%d) Mwp=%.2f(%d) Mb=%.2f(%d) Ms=%.2f(%d)\n",
                   ev->event_id, ev->net[MT_ML], ev->used[MT_ML],
                   ev->net[MT_MWP], ev->used[MT_MWP], ev->net[MT_MB], ev->used[MT_MB],
                   ev->net[MT_MS], ev->used[MT_MS]);
        } else {
            printf("ARC: sin fases resueltas\n");
        }
        free(buf);
        wave_close(&c.wave);
        return 0;
    }

    if (argc != 2 && argc < 4) {
        fprintf(stderr, "Uso: csnmags <csnmags.d>\n");
        fprintf(stderr, "     csnmags <csnmags.d> <hypos.jsonl> <tank> [--out F]\n");
        return 1;
    }

    memset(&c, 0, sizeof(c));
    if (mag_config_load(argv[1], &c.cfg) != 0) {
        fprintf(stderr, "csnmags: error leyendo %s\n", argv[1]);
        return 1;
    }

    if (mag_sta_load(c.cfg.sta_file, &c.stas) < 0)
        fprintf(stderr, "csnmags: advertencia - no cargué %s\n", c.cfg.sta_file);
    mag_sta_apply_corr(&c.stas, c.cfg.sta_corr_file);
    mag_calib_load(&c.cfg, &c.calib);

    /* Aplica las tablas por región a la config global (por ahora el Q global). */
    if (c.calib.q_global.n > 0) { /* usado por compute_mb vía tabla */ }

    if (argc >= 4) {
        FILE *out = stdout;
        int   i;
        const char *tank = argv[3];
        for (i = 4; i < argc; i++) {
            if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) out = fopen(argv[++i], "w");
        }
        c.offline = 1; c.off = out;
        WaveSource *w = &c.wave;
        if (wave_open(w, &c.cfg, tank) != 0) {
            fprintf(stderr, "csnmags: no abro el tank %s\n", tank);
            return 1;
        }
        offline_run(&c, argv[2], tank, out);
        wave_close(w);
        if (out != stdout) fclose(out);
        return 0;
    }

    /* Modo anillo: requiere claves EarthWorm */
    lookup(&c);
    logit_init(argv[1], c.MyModId, 256, c.cfg.logfile);
    SocketSysInit();
    setWsClient_ewDebug(0);
    if (wave_open(&c.wave, &c.cfg, NULL) != 0) { logit("e", "csnmags: no abro wave_server\n"); return 1; }
    ring_run(&c);
    wave_close(&c.wave);
    tport_detach(&c.region);
    return 0;
}
