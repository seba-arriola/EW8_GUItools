#define _GNU_SOURCE
#include "wavesource.h"
#include "wstore.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ws_clientII.h>
#include <socket_ew.h>

#define WSRC_MAX_TRACE 200000

typedef struct {
    char   sta[8], net[10], chan[8], loc[4];
    double start, rate;
    long   offset;
    int    nsamp;
} WsTankEnt;

struct WaveSource {
    WaveSrcCfg cfg;
    char       store_root[512];
    char       tank_path[512];
    FILE      *tank;
    WsTankEnt *idx;
    long       nidx;
    WS_MENU_QUEUE_REC *menu;
    WStore    *store;
};

static void scnl_copy(char *dst, size_t dsz, const char *src)
{
    if (!src) { dst[0] = '\0'; return; }
    snprintf(dst, dsz, "%s", src);
}

static int scnl_match(const char *a, const char *b)
{
    if (!b || !b[0]) return 1;
    if (strcmp(b, "--") == 0 && (a[0] == '\0' || strcmp(a, "--") == 0)) return 1;
    return strcmp(a, b) == 0;
}

/* ------------------------------ tank ------------------------------ */

static int tank_index(WaveSource *w)
{
    TRACE2_HEADER head;
    long          offset;
    int           cap = 1024;

    w->idx = (WsTankEnt *)malloc((size_t)cap * sizeof(WsTankEnt));
    w->nidx = 0;
    if (!w->idx) return -1;

    rewind(w->tank);
    for (;;) {
        offset = ftell(w->tank);
        if (fread(&head, sizeof(TRACE2_HEADER), 1, w->tank) != 1) break;
        if (head.nsamp <= 0 || head.nsamp > WSRC_MAX_TRACE) break;
        offset += (long)sizeof(TRACE2_HEADER);
        if (fseek(w->tank, (long)head.nsamp * (long)sizeof(int32_t), SEEK_CUR) != 0) break;

        if (w->nidx >= cap) {
            WsTankEnt *ni;
            cap *= 2;
            ni = (WsTankEnt *)realloc(w->idx, (size_t)cap * sizeof(WsTankEnt));
            if (!ni) return -1;
            w->idx = ni;
        }
        {
            WsTankEnt *e = &w->idx[w->nidx++];
            scnl_copy(e->sta, sizeof(e->sta), head.sta);
            scnl_copy(e->net, sizeof(e->net), head.net);
            scnl_copy(e->chan, sizeof(e->chan), head.chan);
            scnl_copy(e->loc, sizeof(e->loc), head.loc);
            e->start = head.starttime;
            e->nsamp = head.nsamp;
            e->offset = offset;
            e->rate = head.samprate;
        }
    }
    return 0;
}

static int tank_get(WaveSource *w, const char *sta, const char *net, const char *chan,
                    const char *loc, double t0, double t1, WaveTrace *out)
{
    long     total = 0, i, maxsamp = 0;
    int32_t *buf;
    double   dt = 0.0;

    for (i = 0; i < w->nidx; i++) {
        WsTankEnt *e = &w->idx[i];
        double     endt;
        if (!scnl_match(e->sta, sta) || !scnl_match(e->net, net) ||
            !scnl_match(e->chan, chan) || !scnl_match(e->loc, loc)) continue;
        endt = e->start + (double)(e->nsamp - 1) / e->rate;
        if (endt < t0 || e->start > t1) continue;
        total += e->nsamp;
        if (e->nsamp > maxsamp) maxsamp = e->nsamp;
        if (dt == 0.0) dt = 1.0 / e->rate;
    }
    if (total == 0) return -1;

    out->x = (double *)malloc((size_t)total * sizeof(double));
    if (!out->x) return -1;
    out->dt = dt;
    out->samprate = 1.0 / dt;
    out->n = 0;

    buf = (int32_t *)malloc((size_t)maxsamp * sizeof(int32_t));
    if (!buf) { free(out->x); out->x = NULL; return -1; }

    for (i = 0; i < w->nidx; i++) {
        WsTankEnt *e = &w->idx[i];
        double     endt;
        long       j;
        if (!scnl_match(e->sta, sta) || !scnl_match(e->net, net) ||
            !scnl_match(e->chan, chan) || !scnl_match(e->loc, loc)) continue;
        endt = e->start + (double)(e->nsamp - 1) / e->rate;
        if (endt < t0 || e->start > t1) continue;
        if (fseek(w->tank, e->offset, SEEK_SET) != 0) break;
        if (fread(buf, sizeof(int32_t), (size_t)e->nsamp, w->tank) != (size_t)e->nsamp) break;
        if (out->n == 0) out->t0 = e->start;
        for (j = 0; j < e->nsamp; j++) out->x[out->n++] = (double)buf[j];
    }
    free(buf);
    return (out->n > 0) ? 0 : -1;
}

/* ------------------------------ store ------------------------------ */

static int store_get(WaveSource *w, const char *sta, const char *net, const char *chan,
                     const char *loc, double t0, double t1, WaveTrace *out)
{
    WStoreTrace st;
    int         r;

    if (!w->store) return -1;
    memset(&st, 0, sizeof(st));
    r = wstore_get(w->store, sta, net, chan, loc, t0, t1, &st);
    if (r != 0) return -1;

    out->x = (double *)malloc((size_t)st.n * sizeof(double));
    if (!out->x) { wstore_trace_free(&st); return -1; }
    for (long i = 0; i < st.n; i++) out->x[i] = (double)st.samples[i];
    out->n = st.n;
    out->t0 = st.t0;
    out->samprate = st.samprate;
    out->dt = 1.0 / st.samprate;
    scnl_copy(out->sta, sizeof(out->sta), sta);
    scnl_copy(out->net, sizeof(out->net), net);
    scnl_copy(out->chan, sizeof(out->chan), chan);
    scnl_copy(out->loc, sizeof(out->loc), loc);
    wstore_trace_free(&st);
    return 0;
}

/* ------------------------------ ws ------------------------------ */

static int ws_get(WaveSource *w, const char *sta, const char *net, const char *chan,
                  const char *loc, double t0, double t1, WaveTrace *out)
{
    TRACE_REQ req;
    char     *buf;
    int       rc = -1, tries = 0;
    WS_MENU_QUEUE_REC *menu = w->menu;

    buf = (char *)malloc(WSRC_MAX_TRACE * sizeof(int32_t) + 4096);
    if (!buf) return -1;

    memset(&req, 0, sizeof(req));
    scnl_copy(req.sta, sizeof(req.sta), sta);
    scnl_copy(req.net, sizeof(req.net), net);
    scnl_copy(req.chan, sizeof(req.chan), chan);
    scnl_copy(req.loc, sizeof(req.loc), loc);
    req.reqStarttime = t0;
    req.reqEndtime = t1;
    req.pBuf = buf;
    req.bufLen = (int)(WSRC_MAX_TRACE * sizeof(int32_t) + 4096);
    req.timeout = w->cfg.ws_timeout;
    req.fill = 0;

    while (tries < 2) {
        if (!menu || menu->head == NULL) {
            if (wsAppendMenu(w->cfg.ws_ip, w->cfg.ws_port, menu, w->cfg.ws_timeout) != WS_ERR_NONE) {
                tries++;
                sleep_ew(500);
                continue;
            }
        }
        rc = wsGetTraceBinL(&req, menu, w->cfg.ws_timeout);
        if (rc == WS_ERR_NONE) break;
        wsKillMenu(menu);
        menu->head = NULL;
        menu->tail = NULL;
        tries++;
        sleep_ew(500);
    }
    if (rc != WS_ERR_NONE) { free(buf); return -1; }

    {
        char *ptr = req.pBuf, *end = req.pBuf + req.actLen;
        long  total = 0;
        while (ptr + sizeof(TRACE2_HEADER) <= end) {
            TRACE2_HEADER *h = (TRACE2_HEADER *)ptr;
            long data_bytes = (long)h->nsamp * (long)sizeof(int32_t);
            if (h->nsamp <= 0 || ptr + sizeof(TRACE2_HEADER) + data_bytes > end) break;
            if (scnl_match(h->sta, sta) && scnl_match(h->net, net) &&
                scnl_match(h->chan, chan) && scnl_match(h->loc, loc)) total += h->nsamp;
            ptr += sizeof(TRACE2_HEADER) + data_bytes;
        }
        if (total == 0) { free(buf); return -1; }
        out->x = (double *)malloc((size_t)total * sizeof(double));
        if (!out->x) { free(buf); return -1; }
        out->n = 0;
        ptr = req.pBuf;
        while (ptr + sizeof(TRACE2_HEADER) <= end) {
            TRACE2_HEADER *h = (TRACE2_HEADER *)ptr;
            long data_bytes = (long)h->nsamp * (long)sizeof(int32_t);
            int32_t *d;
            long j;
            if (h->nsamp <= 0 || ptr + sizeof(TRACE2_HEADER) + data_bytes > end) break;
            d = (int32_t *)(ptr + sizeof(TRACE2_HEADER));
            if (scnl_match(h->sta, sta) && scnl_match(h->net, net) &&
                scnl_match(h->chan, chan) && scnl_match(h->loc, loc)) {
                if (out->n == 0) {
                    out->t0 = h->starttime;
                    out->samprate = h->samprate;
                    out->dt = 1.0 / h->samprate;
                }
                for (j = 0; j < h->nsamp; j++) out->x[out->n++] = (double)d[j];
            }
            ptr += sizeof(TRACE2_HEADER) + data_bytes;
        }
    }
    free(buf);
    return (out->n > 0) ? 0 : -1;
}

/* ------------------------------ API ------------------------------ */

int wave_open(WaveSource **out, const WaveSrcCfg *cfg)
{
    WaveSource *w;

    if (!out || !cfg) return -1;
    w = (WaveSource *)calloc(1, sizeof(*w));
    if (!w) return -1;
    w->cfg = *cfg;
    if (cfg->store_root) scnl_copy(w->store_root, sizeof(w->store_root), cfg->store_root);
    if (cfg->tank_or_null) scnl_copy(w->tank_path, sizeof(w->tank_path), cfg->tank_or_null);

    if (cfg->mode == WSRC_TANK) {
        w->tank = fopen(w->tank_path, "rb");
        if (!w->tank) { free(w); return -1; }
        if (tank_index(w) != 0) { fclose(w->tank); free(w); return -1; }
    }
    if (cfg->mode == WSRC_STORE || cfg->mode == WSRC_AUTO) {
        if (w->store_root[0]) wstore_open(&w->store, w->store_root, NULL);
    }
    if (cfg->mode == WSRC_WS || cfg->mode == WSRC_AUTO) {
        w->menu = (WS_MENU_QUEUE_REC *)calloc(1, sizeof(WS_MENU_QUEUE_REC));
        if (!w->menu) { if (w->store) wstore_close(w->store); free(w); return -1; }
    }
    *out = w;
    return 0;
}

int wave_get(WaveSource *w, const char *sta, const char *net, const char *chan,
             const char *loc, double t0, double t1, WaveTrace *out)
{
    if (!w || !out) return -1;
    memset(out, 0, sizeof(*out));

    if (w->cfg.mode == WSRC_STORE) return store_get(w, sta, net, chan, loc, t0, t1, out);
    if (w->cfg.mode == WSRC_TANK)  return tank_get(w, sta, net, chan, loc, t0, t1, out);
    if (w->cfg.mode == WSRC_AUTO) {
        if (store_get(w, sta, net, chan, loc, t0, t1, out) == 0) return 0;
        return ws_get(w, sta, net, chan, loc, t0, t1, out);
    }
    return ws_get(w, sta, net, chan, loc, t0, t1, out);
}

void wave_close(WaveSource *w)
{
    if (!w) return;
    if (w->tank) fclose(w->tank);
    if (w->menu) wsKillMenu(w->menu);
    if (w->store) wstore_close(w->store);
    free(w->idx);
    free(w->menu);
    free(w);
}
