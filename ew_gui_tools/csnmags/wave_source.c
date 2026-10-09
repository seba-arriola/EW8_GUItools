#include "csnmags.h"

#include <string.h>

#include <ws_clientII.h>
#include <socket_ew.h>

/*
 * wave_source.c - Abstracción de la fuente de onda.
 *
 *   WSRC_WS   : wave_serverV por TCP (modo anillo), patrón de csnmags_toy.
 *   WSRC_TANK : lectura directa del tank (modo offline), patrón de pickS.
 *
 * En offline se construye un índice (SCNL + offset) recorriendo el tank una sola
 * vez; wave_get lee sólo los paquetes del SCNL pedido, sin volver a barrer todo.
 */

static void scnl_copy(char *dst, size_t dsz, const char *src)
{
    if (!src) { dst[0] = '\0'; return; }
    snprintf(dst, dsz, "%s", src);
}

/* ------------------------------ offline ------------------------------ */

static int tank_index(WaveSource *w)
{
    TRACE2_HEADER head;
    long offset;
    int  cap = 1024;

    w->idx = (MagTankIndex *)malloc((size_t)cap * sizeof(MagTankIndex));
    w->nidx = 0;
    if (!w->idx) return -1;

    rewind(w->tank);
    for (;;) {
        offset = ftell(w->tank);
        if (fread(&head, sizeof(TRACE2_HEADER), 1, w->tank) != 1) break;
        if (head.nsamp <= 0 || head.nsamp > MAG_MAX_TRACE) break;
        offset += (long)sizeof(TRACE2_HEADER);
        /* skip samples */
        if (fseek(w->tank, (long)head.nsamp * (long)sizeof(int32_t), SEEK_CUR) != 0) break;

        if (w->nidx >= cap) {
            MagTankIndex *ni;
            cap *= 2;
            ni = (MagTankIndex *)realloc(w->idx, (size_t)cap * sizeof(MagTankIndex));
            if (!ni) return -1;
            w->idx = ni;
        }
        {
            MagTankIndex *e = &w->idx[w->nidx++];
            scnl_copy(e->sta, sizeof(e->sta), head.sta);
            scnl_copy(e->net, sizeof(e->net), head.net);
            scnl_copy(e->chan, sizeof(e->chan), head.chan);
            scnl_copy(e->loc, sizeof(e->loc), head.loc);
            e->starttime = head.starttime;
            e->nsamp     = head.nsamp;
            e->offset    = offset;
            e->samprate  = head.samprate;
        }
    }
    return 0;
}

static int scnl_match(const char *a, const char *b)
{
    /* vacío en el filtro => comodín */
    if (!b || !b[0]) return 1;
    if (strcmp(b, "--") == 0 && (a[0] == '\0' || strcmp(a, "--") == 0)) return 1;
    return strcmp(a, b) == 0;
}

static int tank_get(WaveSource *w, const char *sta, const char *net,
                    const char *chan, const char *loc, double t0, double t1,
                    MagTrace *out)
{
    long   total = 0, i;
    int32_t *buf = NULL;
    double dt = 0.0;
    long   maxsamp = 0;

    out->x = NULL; out->n = 0; out->t0 = 0; out->dt = 0; out->samprate = 0;

    for (i = 0; i < w->nidx; i++) {
        MagTankIndex *e = &w->idx[i];
        double endt;
        if (!scnl_match(e->sta, sta)) continue;
        if (!scnl_match(e->net, net)) continue;
        if (!scnl_match(e->chan, chan)) continue;
        if (!scnl_match(e->loc, loc)) continue;
        endt = e->starttime + (double)(e->nsamp - 1) / e->samprate;
        if (endt < t0 || e->starttime > t1) continue;
        total += e->nsamp;
        if (e->nsamp > maxsamp) maxsamp = e->nsamp;
        if (dt == 0.0) dt = 1.0 / e->samprate;
    }
    if (total == 0) return -1;

    out->x = (double *)malloc((size_t)total * sizeof(double));
    if (!out->x) return -1;
    out->dt = dt; out->samprate = 1.0 / dt;

    buf = (int32_t *)malloc((size_t)maxsamp * sizeof(int32_t));
    if (!buf) { free(out->x); out->x = NULL; return -1; }

    out->n = 0;
    for (i = 0; i < w->nidx; i++) {
        MagTankIndex *e = &w->idx[i];
        double endt;
        long   j;
        if (!scnl_match(e->sta, sta)) continue;
        if (!scnl_match(e->net, net)) continue;
        if (!scnl_match(e->chan, chan)) continue;
        if (!scnl_match(e->loc, loc)) continue;
        endt = e->starttime + (double)(e->nsamp - 1) / e->samprate;
        if (endt < t0 || e->starttime > t1) continue;
        if (fseek(w->tank, e->offset, SEEK_SET) != 0) break;
        if (fread(buf, sizeof(int32_t), (size_t)e->nsamp, w->tank) != (size_t)e->nsamp) break;
        if (out->n == 0) out->t0 = e->starttime;
        for (j = 0; j < e->nsamp; j++) out->x[out->n++] = (double)buf[j];
    }
    free(buf);
    return (out->n > 0) ? 0 : -1;
}

/* ------------------------------ online ------------------------------ */

static int ws_get(WaveSource *w, const char *sta, const char *net, const char *chan,
                  const char *loc, double t0, double t1, MagTrace *out)
{
    TRACE_REQ req;
    char     *buf;
    int      rc = -1, tries = 0;
    WS_MENU_QUEUE_REC *menu = (WS_MENU_QUEUE_REC *)w->ws_menu;

    out->x = NULL; out->n = 0; out->t0 = 0; out->dt = 0; out->samprate = 0;

    buf = (char *)malloc(MAG_MAX_TRACE * sizeof(int32_t) + 4096);
    if (!buf) return -1;

    memset(&req, 0, sizeof(req));
    scnl_copy(req.sta, sizeof(req.sta), sta);
    scnl_copy(req.net, sizeof(req.net), net);
    scnl_copy(req.chan, sizeof(req.chan), chan);
    scnl_copy(req.loc, sizeof(req.loc), loc);
    req.reqStarttime = t0;
    req.reqEndtime   = t1;
    req.pBuf         = buf;
    req.bufLen       = (int)(MAG_MAX_TRACE * sizeof(int32_t) + 4096);
    req.timeout      = w->ws_timeout;
    req.fill         = 0;

    while (tries < 2) {
        if (!menu || menu->head == NULL) {
            if (wsAppendMenu(w->ws_ip, w->ws_port, menu, w->ws_timeout) != WS_ERR_NONE) {
                menu = NULL; tries++; sleep_ew(500); continue;
            }
        }
        rc = wsGetTraceBinL(&req, menu, w->ws_timeout);
        if (rc == WS_ERR_NONE) break;
        wsKillMenu(menu); menu->head = NULL; menu->tail = NULL;
        tries++; sleep_ew(500);
    }
    if (rc != WS_ERR_NONE) { free(buf); return -1; }

    /* Concatenar muestras dentro de la ventana */
    {
        char *ptr = req.pBuf, *end = req.pBuf + req.actLen;
        long  total = 0;
        while (ptr + sizeof(TRACE2_HEADER) <= end) {
            TRACE2_HEADER *h = (TRACE2_HEADER *)ptr;
            long data_bytes = (long)h->nsamp * (long)sizeof(int32_t);
            if (h->nsamp <= 0 || ptr + sizeof(TRACE2_HEADER) + data_bytes > end) break;
            if (scnl_match(h->sta, sta) && scnl_match(h->net, net) &&
                scnl_match(h->chan, chan) && scnl_match(h->loc, loc))
                total += h->nsamp;
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
                if (out->n == 0) { out->t0 = h->starttime; out->samprate = h->samprate; out->dt = 1.0 / h->samprate; }
                for (j = 0; j < h->nsamp; j++) out->x[out->n++] = (double)d[j];
            }
            ptr += sizeof(TRACE2_HEADER) + data_bytes;
        }
    }
    free(buf);
    return (out->n > 0) ? 0 : -1;
}

/* ------------------------------ API ------------------------------ */

int wave_open(WaveSource *w, const MagConfig *cfg, const char *tank_or_null)
{
    memset(w, 0, sizeof(*w));
    w->ws_timeout = cfg->ws_timeout;
    scnl_copy(w->ws_ip, sizeof(w->ws_ip), cfg->ws_ip);
    scnl_copy(w->ws_port, sizeof(w->ws_port), cfg->ws_port);

    if (tank_or_null) {
        w->mode = WSRC_TANK;
        w->tank = fopen(tank_or_null, "rb");
        if (!w->tank) { perror(tank_or_null); return -1; }
        if (tank_index(w) != 0) { fclose(w->tank); w->tank = NULL; return -1; }
        return 0;
    }

    w->mode = WSRC_WS;
    w->ws_menu = calloc(1, sizeof(WS_MENU_QUEUE_REC));
    if (!w->ws_menu) return -1;
    return 0;
}

void wave_close(WaveSource *w)
{
    if (!w) return;
    if (w->mode == WSRC_TANK) {
        if (w->tank) fclose(w->tank);
    } else if (w->ws_menu) {
        wsKillMenu((WS_MENU_QUEUE_REC *)w->ws_menu);
    }
    free(w->idx);
    free(w->ws_menu);
    memset(w, 0, sizeof(*w));
}

int wave_get(WaveSource *w, const char *sta, const char *net, const char *chan,
             const char *loc, double t0, double t1, MagTrace *out)
{
    if (!w) return -1;
    if (w->mode == WSRC_TANK) return tank_get(w, sta, net, chan, loc, t0, t1, out);
    return ws_get(w, sta, net, chan, loc, t0, t1, out);
}
