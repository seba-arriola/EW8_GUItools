#define _GNU_SOURCE
#include "wstore_ingest.h"
#include "wstore_filter.h"
#include "wstore_writer.h"
#include "wstore_internal.h"

#include <earthworm.h>
#include <kom.h>
#include <transport.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifndef INST_WILDCARD
#define INST_WILDCARD 0
#endif
#ifndef MOD_WILDCARD
#define MOD_WILDCARD 0
#endif

typedef struct WIngestItem {
    struct WIngestItem *next;
    TRACE2_HEADER       h;
    int32_t             samples[];
} WIngestItem;

typedef struct {
    char          sta[8], net[10], loc[4], chan[8];
    WStoreWriter *wr;
} WChan;

typedef struct {
    pthread_t       th;
    pthread_mutex_t m;
    pthread_cond_t  c;
    WIngestItem    *head, *tail;
    int             stop;
    int             flush_req, flush_done;
    WChan          *ch;
    int             nch, capch;
    long            dropped;
    char            root[512];
} WShard;

struct WStoreIngest {
    WStoreConfig  cfg;
    WShard       *sh;
    int           nsh;
    long          dropped;
    WStoreFilter *filter;   /* ChannelFilterFile; NULL = sin filtro (se guarda todo) */
    long          filtered; /* descartados por el filtro (no es `dropped`) */
    WStore       *store;    /* solo para la retencion */
};

static unsigned scnl_hash(const char *sta, const char *net, const char *loc, const char *chan)
{
    const char *parts[4];
    unsigned    h = 2166136261u;
    int         k;

    parts[0] = sta; parts[1] = net; parts[2] = loc; parts[3] = chan;
    for (k = 0; k < 4; k++) {
        const char *p = parts[k];
        if (!p) continue;
        for (; *p; p++) { h ^= (unsigned char)*p; h *= 16777619u; }
    }
    return h;
}

/* Solo lo llama el hilo del shard: sin lock sobre `ch`. */
static WStoreWriter *shard_writer(WShard *s, const TRACE2_HEADER *h)
{
    int i;
    for (i = 0; i < s->nch; i++) {
        if (strncmp(s->ch[i].sta, h->sta, 7) == 0 &&
            strncmp(s->ch[i].net, h->net, 9) == 0 &&
            strncmp(s->ch[i].loc, h->loc, 3) == 0 &&
            strncmp(s->ch[i].chan, h->chan, 4) == 0)
            return s->ch[i].wr;
    }
    if (s->nch == s->capch) {
        int    cap = s->capch ? s->capch * 2 : 8;
        WChan *nc = (WChan *)realloc(s->ch, (size_t)cap * sizeof(*nc));
        if (!nc) return NULL;
        s->ch = nc;
        s->capch = cap;
    }
    if (wstore_writer_open(&s->ch[s->nch].wr, s->root, h->sta, h->net, h->loc, h->chan) != 0)
        return NULL;
    snprintf(s->ch[s->nch].sta, sizeof(s->ch[s->nch].sta), "%s", h->sta);
    snprintf(s->ch[s->nch].net, sizeof(s->ch[s->nch].net), "%s", h->net);
    snprintf(s->ch[s->nch].loc, sizeof(s->ch[s->nch].loc), "%s", h->loc);
    snprintf(s->ch[s->nch].chan, sizeof(s->ch[s->nch].chan), "%s", h->chan);
    s->nch++;
    return s->ch[s->nch - 1].wr;
}

static void *shard_fn(void *arg)
{
    WShard *s = (WShard *)arg;

    pthread_mutex_lock(&s->m);
    for (;;) {
        while (!s->head && !s->stop && s->flush_done >= s->flush_req)
            pthread_cond_wait(&s->c, &s->m);

        if (!s->head && s->stop) break;

        if (!s->head && s->flush_req > s->flush_done) {
            pthread_mutex_unlock(&s->m);
            for (int i = 0; i < s->nch; i++) wstore_writer_flush(s->ch[i].wr);
            pthread_mutex_lock(&s->m);
            s->flush_done = s->flush_req;
            pthread_cond_broadcast(&s->c);
            continue;
        }

        {
            WIngestItem  *it = s->head;
            WStoreWriter *w;
            s->head = it->next;
            if (!s->head) s->tail = NULL;
            pthread_mutex_unlock(&s->m);

            w = shard_writer(s, &it->h);
            if (!w || wstore_writer_append(w, &it->h, it->samples) != 0) s->dropped++;
            free(it);

            pthread_mutex_lock(&s->m);
        }
    }
    pthread_mutex_unlock(&s->m);
    return NULL;
}

int wstore_ingest_open(WStoreIngest **out, const WStoreConfig *cfg)
{
    WStoreIngest *ing;
    int           i;

    if (!out || !cfg) return -1;
    ing = (WStoreIngest *)calloc(1, sizeof(*ing));
    if (!ing) return -1;
    ing->cfg = *cfg;
    if (cfg->channel_filter_file[0] &&
        wstore_filter_load(cfg->channel_filter_file, &ing->filter) != 0) {
        fprintf(stderr, "wstore_ingest: filtro de canales invalido ('%s')\n",
                cfg->channel_filter_file);
        free(ing);
        return -1;
    }
    ing->nsh = cfg->ingest_shards;
    ing->sh = (WShard *)calloc((size_t)ing->nsh, sizeof(*ing->sh));
    if (!ing->sh) { ing->nsh = 0; wstore_ingest_close(ing); return -1; }

    for (i = 0; i < ing->nsh; i++) {
        WShard *s = &ing->sh[i];
        pthread_mutex_init(&s->m, NULL);
        pthread_cond_init(&s->c, NULL);
        snprintf(s->root, sizeof(s->root), "%s", cfg->root);
        if (pthread_create(&s->th, NULL, shard_fn, s) != 0) {
            /* El shard que fallo ya inicializo su mutex/cond y no entra en el
             * rango que recorre `close` (nsh = i): se liberan aqui. */
            pthread_mutex_destroy(&s->m);
            pthread_cond_destroy(&s->c);
            ing->nsh = i;
            wstore_ingest_close(ing);
            return -1;
        }
    }
    wstore_open(&ing->store, cfg->root, NULL);
    *out = ing;
    return 0;
}

int wstore_ingest_feed(WStoreIngest *ing, const TRACE2_HEADER *h, const int32_t *samples)
{
    WShard      *s;
    WIngestItem *it;
    unsigned     idx;

    if (!ing || !h || !samples) return -1;
    if (!wstore_filter_accept(ing->filter, h->sta, h->net, h->chan, h->loc)) {
        ing->filtered++;
        return 0;
    }
    if (h->nsamp <= 0 || h->nsamp > 200000) return -1;

    it = (WIngestItem *)malloc(sizeof(*it) + (size_t)h->nsamp * sizeof(int32_t));
    if (!it) return -1;
    it->next = NULL;
    it->h = *h;
    memcpy(it->samples, samples, (size_t)h->nsamp * sizeof(int32_t));

    idx = scnl_hash(h->sta, h->net, h->loc, h->chan) % (unsigned)ing->nsh;
    s = &ing->sh[idx];
    pthread_mutex_lock(&s->m);
    if (s->tail) s->tail->next = it;
    else s->head = it;
    s->tail = it;
    pthread_cond_signal(&s->c);
    pthread_mutex_unlock(&s->m);
    return 0;
}

int wstore_ingest_flush(WStoreIngest *ing)
{
    int i;
    if (!ing) return -1;
    for (i = 0; i < ing->nsh; i++) {
        WShard *s = &ing->sh[i];
        pthread_mutex_lock(&s->m);
        while (s->head) {
            pthread_mutex_unlock(&s->m);
            usleep(1000);
            pthread_mutex_lock(&s->m);
        }
        s->flush_req++;
        pthread_cond_signal(&s->c);
        while (s->flush_done < s->flush_req) pthread_cond_wait(&s->c, &s->m);
        pthread_mutex_unlock(&s->m);
    }
    return 0;
}

void wstore_ingest_close(WStoreIngest *ing)
{
    int i;
    if (!ing) return;
    for (i = 0; i < ing->nsh; i++) {
        WShard *s = &ing->sh[i];
        pthread_mutex_lock(&s->m);
        s->stop = 1;
        pthread_cond_broadcast(&s->c);
        pthread_mutex_unlock(&s->m);
    }
    for (i = 0; i < ing->nsh; i++) {
        WShard *s = &ing->sh[i];
        pthread_join(s->th, NULL);
        for (int k = 0; k < s->nch; k++) wstore_writer_close(s->ch[k].wr);
        free(s->ch);
        pthread_mutex_destroy(&s->m);
        pthread_cond_destroy(&s->c);
    }
    if (ing->store) wstore_close(ing->store);
    wstore_filter_free(ing->filter);
    free(ing->sh);
    free(ing);
}

long wstore_ingest_dropped(const WStoreIngest *ing)
{
    long d = 0;
    int  i;
    if (!ing) return 0;
    d = ing->dropped;
    for (i = 0; i < ing->nsh; i++) d += ing->sh[i].dropped;
    return d;
}

long wstore_ingest_filtered(const WStoreIngest *ing)
{
    return ing ? ing->filtered : 0;
}

int wstore_ingest_purge_now(WStoreIngest *ing, const WStoreConfig *cfg, WStorePruneStats *st)
{
    if (st) memset(st, 0, sizeof(*st));
    if (!ing || !cfg) return -1;
    if (cfg->retention_days <= 0 || !ing->store) return 0;
    return wstore_prune_stats(ing->store, cfg->retention_days, st);
}

/* Purga por retencion + su linea de log (`when`: arranque u horaria). Solo el
 * hilo principal: `logit` no se usa desde los shards (ver invariante de fuente). */
static void purge_and_log(WStoreIngest *ing, const WStoreConfig *cfg, const char *when)
{
    WStorePruneStats st;

    if (!ing || !cfg || cfg->retention_days <= 0) return;
    if (wstore_ingest_purge_now(ing, cfg, &st) != 0) {
        logit("et", "wstore_ingest: purga fallida\n");
        return;
    }
    if (st.segments_removed > 0)
        logit("t", "wstore_ingest: purga %s: %ld segmentos, %ld MB liberados\n",
              when, st.segments_removed, (st.bytes_freed + 1048575) / 1048576);
}

int wstore_ingest_run_ring(WStoreIngest *ing, const WStoreConfig *cfg)
{
    SHM_INFO      region;
    MSG_LOGO      wavelogo, logo;
    unsigned char type_trace;
    char         *buf;
    char          ring[32];
    long          bufsz, size, key;
    time_t        then, now, last_prune;

    if (!ing || !cfg) return -1;
    if (GetType("TYPE_TRACEBUF2", &type_trace) != 0) {
        logit("e", "wstore_ingest: TYPE_TRACEBUF2 desconocido\n");
        return -1;
    }
    snprintf(ring, sizeof(ring), "%s", cfg->ring_name);
    key = GetKey(ring);
    if (key == -1) {
        logit("e", "wstore_ingest: anillo invalido <%s>\n", ring);
        return -1;
    }
    bufsz = MAX_TRACEBUF_SIZ + sizeof(TRACE2_HEADER) + 64;
    buf = (char *)malloc((size_t)bufsz);
    if (!buf) return -1;
    tport_attach(&region, key);

    wavelogo.instid = INST_WILDCARD;
    wavelogo.mod = MOD_WILDCARD;
    wavelogo.type = type_trace;

    /* Purga inicial: arrancamos desde el presente, no desde el fondo del ring. */
    while (tport_getmsg(&region, &wavelogo, 1, &logo, &size, buf, bufsz) == GET_OK) ;

    time(&then);
    last_prune = then;
    logit("t", "wstore_ingest: anillo %s -> %s\n", cfg->ring_name, cfg->root);
    if (ing->filter)
        logit("t", "wstore_ingest: filtro '%s' con %d patrones\n",
              cfg->channel_filter_file, wstore_filter_count(ing->filter));
    if (cfg->retention_days > 0)
        logit("t", "wstore_ingest: retencion %ld dias (purga al arrancar y cada 3600 s)\n",
              cfg->retention_days);
    else
        logit("t", "wstore_ingest: sin retencion (el store crece sin limite)\n");

    /* Purga inicial: con la horaria sola, un modulo que se reiniciara antes de
     * una hora no purgaba jamas. */
    purge_and_log(ing, cfg, "inicial");

    while (tport_getflag(&region) != TERMINATE) {
        int rc = tport_getmsg(&region, &wavelogo, 1, &logo, &size, buf, bufsz);

        if (rc == GET_OK && size >= (long)sizeof(TRACE2_HEADER)) {
            TRACE2_HEADER *h = (TRACE2_HEADER *)buf;
            const void    *raw = buf + sizeof(TRACE2_HEADER);
            int32_t       *s = NULL;
            int            nr = wstore_normalize_samples(h, raw, &s);
            if (nr == 0) {
                wstore_ingest_feed(ing, h, s);
                free(s);
            } else {
                ing->dropped++;
            }
        }

        time(&now);
        if (now - then >= cfg->heartbeat_int) then = now;
        if (cfg->retention_days > 0 && now - last_prune >= 3600) {
            last_prune = now;
            purge_and_log(ing, cfg, "horaria");
        }
        if (rc == GET_NONE) usleep(50000);
    }

    tport_detach(&region);
    free(buf);
    return 0;
}
