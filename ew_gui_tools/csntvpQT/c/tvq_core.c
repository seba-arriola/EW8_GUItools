/* Nucleo del visor: estado, tick de anillos, reloj de la ventana y volcados.
 *
 * El reloj es el del GTK (csntvp_data.c:331-352): con datos frescos
 * t_right = ahora - 2 s; con datos parados (stale) se CONGELA; y si la marca de
 * los datos no es de tiempo real (> 24 h de desfase) la ventana deriva con dt
 * en vez de saltar. En HOLD manda hold_t.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tvq_capi.h"
#include "tvq_internal.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Los campos del TRACE2_HEADER tienen ancho fijo (sta[7], chan[4], net[4],
 * loc[3]): se copia con truncado EXPLICITO para no depender del formato. */
static void copy_field(char *dst, size_t n, const char *src)
{
    size_t len = strlen(src);

    if (len >= n)
        len = n - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

/* ------------------------------------------------------------------------- */
/* Construccion / destruccion                                                */
/* ------------------------------------------------------------------------- */

TvqCore *tvq_core_new(const TvqConfig *cfg, char *err, size_t errlen)
{
    TvqCore *c;

    if (err && errlen)
        err[0] = '\0';
    if (!cfg) {
        if (err)
            snprintf(err, errlen, "config nula");
        return NULL;
    }

    c = (TvqCore *)calloc(1, sizeof(*c));
    if (!c) {
        if (err)
            snprintf(err, errlen, "sin memoria");
        return NULL;
    }
    c->sta = (TvqStation *)calloc(TVQ_MAX_STATIONS, sizeof(TvqStation));
    if (!c->sta) {
        free(c);
        if (err)
            snprintf(err, errlen, "sin memoria");
        return NULL;
    }

    c->cfg = cfg;
    /* El id sale del .d desde el arranque: sin anillos (tests, --dump) el
     * mensaje de picada ya lleva el mod real; open_rings lo re-confirma. */
    c->modid = cfg->module_id >= 0 ? (unsigned char)cfg->module_id : 0;
    c->instid = 0;
    c->zoom = 1.0;
    /* Sin fase armada: el modo picada se arma con p/s y se cancela con Esc
     * (o al soltar HOLD). Un clic sin fase no pica. */
    c->pick_phase = '\0';
    c->filter_type = 0;
    c->f1 = 0.7;
    c->f2 = 2.0;
    c->order = 4;
    c->hb.last = 0.0;

    for (int r = 0; r < TVQ_COLOUR_COUNT; r++)
        for (int i = 0; i < 3; i++)
            c->colour[r][i] = cfg->colour[r][i];

    return c;
}

void tvq_core_free(TvqCore *c)
{
    if (!c)
        return;

    for (int i = 0; i < c->n_sta; i++) {
        free(c->sta[i].circ);
        ewgui_trace_cache_free(&c->sta[i].cache);
    }
    free(c->sta);

    ewgui_ring_detach(&c->wave);
    ewgui_ring_detach(&c->pick);
    free(c);
}

/* ------------------------------------------------------------------------- */
/* Estaciones                                                                */
/* ------------------------------------------------------------------------- */

static void free_stations(TvqCore *c)
{
    for (int i = 0; i < c->n_sta; i++) {
        free(c->sta[i].circ);
        c->sta[i].circ = NULL;
        ewgui_trace_cache_free(&c->sta[i].cache);
    }
    memset(c->sta, 0, (size_t)TVQ_MAX_STATIONS * sizeof(TvqStation));
    c->n_sta = 0;
}

int tvq_core_load_stations(TvqCore *c, char *err, size_t errlen)
{
    TvqStaEntry *tmp;
    int n = 0;

    if (err && errlen)
        err[0] = '\0';
    if (!c)
        return -1;

    tmp = (TvqStaEntry *)calloc(TVQ_MAX_STATIONS, sizeof(TvqStaEntry));
    if (!tmp) {
        if (err)
            snprintf(err, errlen, "sin memoria");
        return -1;
    }

    if (tvq_sta_load(c->cfg->sta_file, tmp, TVQ_MAX_STATIONS, &n, err, errlen) != 0) {
        free(tmp);
        return -1;
    }

    free_stations(c);

    for (int i = 0; i < n; i++) {
        TvqStation *s = &c->sta[i];

        snprintf(s->sta, sizeof s->sta, "%s", tmp[i].sta);
        snprintf(s->chan, sizeof s->chan, "%s", tmp[i].chan);
        snprintf(s->net, sizeof s->net, "%s", tmp[i].net);
        snprintf(s->loc, sizeof s->loc, "%s", tmp[i].loc);
        s->scale = tmp[i].scale;
        s->rate = 0.0;
        s->circ = NULL;
        s->circ_size = 0;
        s->last_abs = 0;
        s->last_packet_wall = 0.0;
        s->n_picks = 0;
        s->pick_next = 0;
        ewgui_trace_cache_init(&s->cache);
    }
    c->n_sta = n;
    c->env_ok = 0;
    c->env_w = 0;
    c->changed |= TVQ_CH_STATIONS;

    free(tmp);
    tvq_logf("csntvpQT: %d estaciones cargadas de <%s>\n", n, c->cfg->sta_file);
    return 0;
}

int tvq_core_reload_stations(TvqCore *c)
{
    char err[256];

    if (!c)
        return -1;
    if (tvq_core_load_stations(c, err, sizeof err) != 0) {
        tvq_logf("csntvpQT: no pude recargar estaciones: %s\n", err);
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------------- */
/* Tick                                                                      */
/* ------------------------------------------------------------------------- */

static void drain_waves(TvqCore *c)
{
    char msg[TVQ_TRACE_MSG_MAX];
    MSG_LOGO logo;
    long len;
    int res;

    if (!c->wave.attached)
        return;

    do {
        res = ewgui_ring_drain(&c->wave, c->wave_logo, 1, &logo, &len, msg,
                               (long)sizeof msg);
        /* GET_MISS / GET_NOTRACK SI traen mensaje (transport.c:844-865). */
        if (res == GET_OK || res == GET_MISS || res == GET_NOTRACK) {
            if (logo.type == c->type_trace)
                (void)tvq_core_ingest_trace(c, msg, (int)len);
        }
    } while (res != GET_NONE);
}

static void drain_picks(TvqCore *c)
{
    char msg[TVQ_PICK_MSG_MAX];
    MSG_LOGO logo;
    long len;
    int res;

    if (!c->pick.attached)
        return;

    do {
        res = ewgui_ring_drain(&c->pick, c->pick_logo, 1, &logo, &len, msg,
                               (long)sizeof msg);
        if (res == GET_OK || res == GET_MISS || res == GET_NOTRACK) {
            if (logo.type == c->type_pick)
                (void)tvq_core_ingest_pick(c, msg, (int)len);
        }
    } while (res != GET_NONE);
}

static void update_clock(TvqCore *c, double now, double dt)
{
    double old;

    if (c->hold)
        return;

    old = c->smooth_t;

    if (c->last_data_wall > 0.0 && (now - c->last_data_wall) > TVQ_STALE_SECS) {
        c->stale = 1;
        return;
    }

    c->stale = 0;

    if (c->latest_t <= 0.0 || fabs(now - c->latest_t) <= TVQ_MAX_REALTIME_SKEW) {
        c->latest_t = now - 2.0;
        c->smooth_t = c->latest_t;
    } else if (c->smooth_t == 0.0) {
        c->smooth_t = c->latest_t;
    } else {
        c->smooth_t += dt;
        if (fabs(c->latest_t - c->smooth_t) > 2.0)
            c->smooth_t = c->latest_t;
        else if (c->latest_t > c->smooth_t)
            c->smooth_t += dt * 0.1;
        else if (c->latest_t < c->smooth_t)
            c->smooth_t -= dt * 0.1;
    }

    if (fabs(c->smooth_t - old) >= 0.5)
        c->changed |= TVQ_CH_TIME;
}

unsigned tvq_core_tick(TvqCore *c, double now_wall)
{
    double dt;

    if (!c)
        return 0;

    dt = (c->last_frame_wall > 0.0) ? (now_wall - c->last_frame_wall) : 0.0;
    if (dt < 0.0 || dt > 60.0)
        dt = 0.0;
    c->last_frame_wall = now_wall;

    drain_waves(c);
    drain_picks(c);
    update_clock(c, now_wall, dt);

    /* La envolvente se recalcula a su cadencia (la del GTK) o cuando la UI la
     * invalida; su repintado se traduce aqui para no pintar en cada paquete. */
    tvq_env_update(c, c->view_w - TVQ_PANEL_WIDTH, 0);
    if (c->env_repaint) {
        c->changed |= TVQ_CH_TRACES;
        c->env_repaint = 0;
    }

    c->last_change = c->changed;
    c->changed = 0;
    return c->last_change;
}

/* ------------------------------------------------------------------------- */
/* Vista                                                                     */
/* ------------------------------------------------------------------------- */

void tvq_core_set_hold(TvqCore *c, int on)
{
    if (!c)
        return;

    on = on ? 1 : 0;
    if (on == c->hold)
        return;

    c->hold = on;
    if (on) {
        c->hold_t = c->smooth_t;
    } else {
        c->zoom = 1.0;      /* paridad on_btn_hold_toggled: al soltar, zoom 1 */
        c->pick_phase = '\0'; /* sin HOLD no se puede picar: el modo se apaga */
    }
    tvq_env_invalidate(c);
    tvq_env_update(c, c->view_w - TVQ_PANEL_WIDTH, 1);
    c->changed |= TVQ_CH_TRACES | TVQ_CH_TIME;
}

int tvq_core_is_hold(const TvqCore *c)
{
    return c ? c->hold : 0;
}

void tvq_core_set_guide(TvqCore *c, double x, int valid)
{
    if (!c)
        return;
    c->guide_x = valid ? x : -1.0;
    c->guide_valid = valid ? 1 : 0;
}

int tvq_core_guide(const TvqCore *c, double *x)
{
    if (!c || !c->guide_valid)
        return 0;
    if (x)
        *x = c->guide_x;
    return 1;
}

void tvq_core_view_range(const TvqCore *c, double *t_left, double *t_right)
{
    double right = 0.0;
    double window_secs;

    if (!c)
        return;

    right = c->hold ? c->hold_t : c->smooth_t;
    window_secs = (double)c->cfg->time_window_min * 60.0;
    if (t_right)
        *t_right = right;
    if (t_left)
        *t_left = right - window_secs;
}

void tvq_core_set_stations_per_screen(TvqCore *c, int n)
{
    if (!c)
        return;
    if (n < 1)
        n = 1;
    if (n > TVQ_MAX_STATIONS)
        n = TVQ_MAX_STATIONS;
    if (n == c->cfg->stations_per_screen)
        return;
    ((TvqConfig *)c->cfg)->stations_per_screen = n;
    c->env_ok = 0;
    c->changed |= TVQ_CH_STATIONS;
}

void tvq_core_set_time_window_min(TvqCore *c, int m)
{
    if (!c)
        return;
    if (m < 1)
        m = 1;
    if (m > TVQ_MAX_MINUTES)
        m = TVQ_MAX_MINUTES;
    if (m == c->cfg->time_window_min)
        return;
    ((TvqConfig *)c->cfg)->time_window_min = m;
    tvq_env_invalidate(c);
    tvq_env_update(c, c->view_w - TVQ_PANEL_WIDTH, 1);
    c->changed |= TVQ_CH_TRACES | TVQ_CH_TIME;
}

void tvq_core_set_filter(TvqCore *c, int type, double f1, double f2, int order)
{
    if (!c)
        return;
    if (type < 0 || type > 3)
        type = 0;
    if (order != 2 && order != 4)
        order = 4;
    if (f1 < 0.0)
        f1 = 0.0;
    if (f2 < 0.0)
        f2 = 0.0;

    c->filter_type = type;
    c->f1 = f1;
    c->f2 = f2;
    c->order = order;

    tvq_env_invalidate(c);
    tvq_env_update(c, c->view_w - TVQ_PANEL_WIDTH, 1);
    c->changed |= TVQ_CH_TRACES;
}

void tvq_core_get_filter(const TvqCore *c, int *type, double *f1, double *f2,
                         int *order)
{
    if (!c)
        return;
    if (type)
        *type = c->filter_type;
    if (f1)
        *f1 = c->f1;
    if (f2)
        *f2 = c->f2;
    if (order)
        *order = c->order;
}

void tvq_core_adjust_scale(TvqCore *c, double factor)
{
    if (!c || factor <= 0.0)
        return;
    c->zoom *= factor;
    if (c->zoom < TVQ_MIN_ZOOM)
        c->zoom = TVQ_MIN_ZOOM;
    if (c->zoom > TVQ_MAX_ZOOM)
        c->zoom = TVQ_MAX_ZOOM;
    c->changed |= TVQ_CH_TRACES;
}

double tvq_core_zoom(const TvqCore *c)
{
    return c ? c->zoom : 0.0;
}

void tvq_core_get_colour(const TvqCore *c, int role, double rgb[3])
{
    if (!c || !rgb || role < 0 || role >= TVQ_COLOUR_COUNT)
        return;
    for (int i = 0; i < 3; i++)
        rgb[i] = c->colour[role][i];
}

void tvq_core_set_colour(TvqCore *c, int role, const double rgb[3])
{
    if (!c || !rgb || role < 0 || role >= TVQ_COLOUR_COUNT)
        return;
    for (int i = 0; i < 3; i++)
        c->colour[role][i] = rgb[i];
    c->changed |= TVQ_CH_TRACES;
}

void tvq_core_print_colour_config(const TvqCore *c, FILE *out)
{
    static const char *keys[TVQ_COLOUR_COUNT] = {
        "waveformsColor", "backgroundColor", "fontColor", "separatorColor",
        "pColor", "sColor"
    };

    if (!c || !out)
        return;

    fprintf(out, "csntvpQT: Color config (copy to .d):\n");
    for (int r = 0; r < TVQ_COLOUR_COUNT; r++) {
        int ri = (int)(c->colour[r][0] * 255.0 + 0.5);
        int gi = (int)(c->colour[r][1] * 255.0 + 0.5);
        int bi = (int)(c->colour[r][2] * 255.0 + 0.5);

        if (ri < 0) ri = 0;
        if (ri > 255) ri = 255;
        if (gi < 0) gi = 0;
        if (gi > 255) gi = 255;
        if (bi < 0) bi = 0;
        if (bi > 255) bi = 255;
        fprintf(out, "  %-16s %02X%02X%02X\n", keys[r], ri, gi, bi);
    }
}

/* ------------------------------------------------------------------------- */
/* Instantaneas                                                              */
/* ------------------------------------------------------------------------- */

int tvq_core_nstations(const TvqCore *c)
{
    return c ? c->n_sta : 0;
}

int tvq_core_station_info(const TvqCore *c, int i, TvqStationInfo *out)
{
    const TvqStation *s;

    if (!c || !out || i < 0 || i >= c->n_sta)
        return 0;

    s = &c->sta[i];
    memset(out, 0, sizeof(*out));
    snprintf(out->sta, sizeof out->sta, "%s", s->sta);
    snprintf(out->chan, sizeof out->chan, "%s", s->chan);
    snprintf(out->net, sizeof out->net, "%s", s->net);
    snprintf(out->loc, sizeof out->loc, "%s", s->loc);
    out->rate = s->rate;
    out->scale = s->scale;
    out->cap = s->circ_size;
    out->n_picks = s->n_picks;
    out->t_last = s->rate > 0.0 ? (double)s->last_abs / s->rate : 0.0;
    out->t_first = out->t_last;
    out->stale = ((s->last_packet_wall <= 0.0) ||
                  ((tvq_now_epoch() - s->last_packet_wall) > TVQ_STALE_SECS))
                     ? 1
                     : 0;
    return 1;
}

int tvq_core_sample(const TvqCore *c, int row, long long abs_idx, int *value)
{
    const TvqStation *s;
    int idx;
    int32_t v;

    if (!c || !value || row < 0 || row >= c->n_sta)
        return 0;

    s = &c->sta[row];
    if (!s->circ || s->circ_size <= 0)
        return 0;

    idx = TVQ_CIRC_IDX(abs_idx, s->circ_size);
    v = s->circ[idx];
    if (v == TVQ_GAP)
        return 0;
    *value = (int)v;
    return 1;
}

void tvq_core_dump_text(const TvqCore *c, FILE *out)
{
    double t_left = 0.0, t_right = 0.0;
    int np = 0, ns = 0, nm = 0;
    int vh, track_h;

    if (!c || !out)
        return;

    tvq_core_view_range(c, &t_left, &t_right);
    vh = c->view_h > 0 ? c->view_h : 600;
    track_h = tvq_core_track_height(c, vh);

    fprintf(out, "module=%s\n", c->cfg->my_module);
    fprintf(out, "in_ring=%s\n", c->cfg->in_ring);
    fprintf(out, "out_ring=%s\n", c->cfg->out_ring);
    fprintf(out, "module_id=%d attached=%d\n", (int)c->modid,
            c->wave.attached && c->pick.attached);
    fprintf(out, "n_sta=%d\n", c->n_sta);
    fprintf(out, "stations_per_screen=%d\n", c->cfg->stations_per_screen);
    fprintf(out, "time_window_min=%d\n", c->cfg->time_window_min);
    fprintf(out, "pick_replace_window=%.1f\n", c->cfg->pick_replace_secs);
    fprintf(out, "hold=%d zoom=%.2f stale=%d\n", c->hold, c->zoom, c->stale);
    fprintf(out, "pick_phase=%s\n",
            c->pick_phase == 'P' ? "P" : c->pick_phase == 'S' ? "S" : "none");
    fprintf(out, "filter=%d f1=%.2f f2=%.2f order=%d\n", c->filter_type, c->f1,
            c->f2, c->order);
    fprintf(out, "view=%dx%d track_h=%d canvas_h=%d\n", c->view_w, c->view_h,
            track_h, tvq_core_canvas_height(c, vh));
    fprintf(out, "t_left=%.3f t_right=%.3f\n", t_left, t_right);

    for (int i = 0; i < c->n_sta; i++) {
        TvqStationInfo si;

        if (!tvq_core_station_info(c, i, &si))
            continue;
        for (int k = 0; k < TVQ_MAX_PICKS_PER_STA; k++) {
            const TvqPick *p = &c->sta[i].picks[k];

            if (p->in_use <= 0)
                continue;
            if (p->phase == 'S')
                ns++;
            else
                np++;
            if (p->origin == 'M')
                nm++;
        }
        fprintf(out, "sta=%s chan=%s net=%s loc=%s rate=%.1f npk=%d stale=%d\n",
                si.sta, si.chan, si.net, si.loc, si.rate, si.n_picks, si.stale);
    }
    fprintf(out, "picks_p=%d picks_s=%d picks_m=%d\n", np, ns, nm);
}

/* ------------------------------------------------------------------------- */
/* Fixture determinista (modos instrumentados, sin anillos)                   */
/* ------------------------------------------------------------------------- */

int tvq_core_seed_fixture(TvqCore *c, double t_end, int seconds)
{
    double rate = 100.0;
    int nsamp;
    size_t bytes;
    char *buf;
    int seeded = 0;

    if (!c || c->n_sta <= 0 || seconds <= 0)
        return -1;

    nsamp = (int)(rate * (double)seconds);
    bytes = sizeof(TRACE2_HEADER) + (size_t)nsamp * sizeof(int32_t);
    buf = (char *)malloc(bytes);
    if (!buf)
        return -1;

    for (int i = 0; i < c->n_sta; i++) {
        TRACE2_HEADER *h = (TRACE2_HEADER *)buf;
        int32_t *samples = (int32_t *)(buf + sizeof(TRACE2_HEADER));
        double t0 = t_end - (double)nsamp / rate;

        memset(buf, 0, bytes);
        copy_field(h->sta, sizeof h->sta, c->sta[i].sta);
        copy_field(h->chan, sizeof h->chan, c->sta[i].chan);
        copy_field(h->net, sizeof h->net, c->sta[i].net);
        copy_field(h->loc, sizeof h->loc, c->sta[i].loc);
        copy_field(h->datatype, sizeof h->datatype, "i4");
        h->samprate = rate;
        h->starttime = t0;
        h->endtime = t0 + (double)(nsamp - 1) / rate;
        h->nsamp = nsamp;

        for (int k = 0; k < nsamp; k++) {
            double ph = 2.0 * M_PI * 2.0 * ((double)k / rate);

            samples[k] = (int32_t)(1200.0 * sin(ph)) + 5000;
        }
        (void)tvq_core_ingest_trace(c, buf, (int)bytes);
        seeded++;
    }
    free(buf);

    /* Dos automaticas (mod de pick_FP) para que el volcado tenga P y S. */
    {
        char msg[TVQ_PICK_MSG_MAX];

        if (c->n_sta > 0 &&
            tvq_pick_msg_format(msg, sizeof msg, 50, 151, 0, 1000,
                                c->sta[0].sta, c->sta[0].chan, c->sta[0].net,
                                c->sta[0].loc, 'P', t_end - 5.0) == 0)
            (void)tvq_core_ingest_pick(c, msg, (int)strlen(msg));

        if (c->n_sta > 1 &&
            tvq_pick_msg_format(msg, sizeof msg, 50, 151, 0, 1001,
                                c->sta[1].sta, c->sta[1].chan, c->sta[1].net,
                                c->sta[1].loc, 'S', t_end - 3.0) == 0)
            (void)tvq_core_ingest_pick(c, msg, (int)strlen(msg));
    }

    c->latest_t = t_end;
    c->smooth_t = t_end;
    c->env_ok = 0;
    c->changed |= TVQ_CH_TRACES | TVQ_CH_PICKS;

    return seeded;
}
