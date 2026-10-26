/*
 * bpq_wavecache.c - Cache en memoria de trazas (LRU por bytes).
 *
 * Antes, cada cambio de estacion o de evento liberaba TODAS las trazas y
 * volvia a pedirlas al store/SDS (bpq_mainwindow.cxx:921-924). El store abre y
 * cierra el .idx/.trace por llamada (wavestore.c:93-170) y el SDS el miniSEED
 * del dia (csnhypodbp_sds.c:47-132), asi que un clic costaba disco aunque la
 * misma estacion se hubiera mirado un segundo antes.
 *
 * Contrato:
 *   - La clave es SCNL + t0 (redondeado a 1 ms) + span. El zoom de la vista no
 *     cambia la clave (no refetchea); una ventana de magnitud de 700 s si.
 *   - Get y put COPIAN la traza (bpq_trace_clone): el hilo de la UI y el del
 *     worker nunca comparten memoria mutable, sin refcounts.
 *   - El mapa lo toca SOLO el hilo del worker; los contadores son atomicos para
 *     que --dump los lea desde la UI.
 */
#include "bpq_capi.h"

#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BPQ_WAVECACHE_KEY_MAX 96

typedef struct {
    char               key[BPQ_WAVECACHE_KEY_MAX];
    BpqTrace          *tr;
    size_t             bytes;
    unsigned long long use;      /* marca LRU (crece con cada acceso) */
} BpqWaveCacheEntry;

struct BpqWaveCache {
    BpqWaveCacheEntry *e;
    int                cap;
    size_t             max_bytes;
    unsigned long long tick;
    _Atomic int        n;
    _Atomic long long  bytes;
    _Atomic long       hits, misses, inserts, evictions, toobig;
};

/* Sin tope no hay cache: el llamador recibe NULL y todo pasa por el fetch. */
BpqWaveCache *bpq_wavecache_new(size_t max_bytes, int max_entries)
{
    BpqWaveCache *c;

    if (max_bytes == 0 || max_entries <= 0) return NULL;
    c = (BpqWaveCache *)calloc(1, sizeof(*c));
    if (!c) return NULL;
    c->cap = max_entries;
    c->max_bytes = max_bytes;
    c->e = (BpqWaveCacheEntry *)calloc((size_t)max_entries, sizeof(*c->e));
    if (!c->e) {
        free(c);
        return NULL;
    }
    return c;
}

void bpq_wavecache_free(BpqWaveCache *c)
{
    int i;

    if (!c) return;
    for (i = 0; i < c->cap; i++)
        if (c->e[i].tr) bpq_trace_free(c->e[i].tr);
    free(c->e);
    free(c);
}

int bpq_wavecache_key(char *out, size_t n, const char *sta, const char *net,
                      const char *chan, const char *loc, double t0, double span)
{
    if (!out || n == 0 || !sta || !sta[0]) return 0;
    if (!isfinite(t0) || !isfinite(span) || !(span > 0.0)) return 0;
    /* %.3f: dos soluciones del mismo evento comparten clave; una ventana
     * distinta (120 s vs 700 s) no colisiona. */
    snprintf(out, n, "%s.%s.%s.%s|%.3f|%.3f", sta, net ? net : "",
             chan ? chan : "", loc ? loc : "", t0, span);
    return 1;
}

static void entry_clear(BpqWaveCache *c, int i)
{
    BpqWaveCacheEntry *e = &c->e[i];

    if (!e->tr) return;
    bpq_trace_free(e->tr);
    e->tr = NULL;
    e->key[0] = '\0';
    atomic_fetch_sub(&c->bytes, (long long)e->bytes);
    atomic_fetch_sub(&c->n, 1);
    e->bytes = 0;
}

/* Hueco libre, o el LRU entre los ocupados (para insertar). */
static int lru_slot(const BpqWaveCache *c)
{
    int i, free_slot = -1, lru = -1;
    unsigned long long use = 0;

    for (i = 0; i < c->cap; i++) {
        if (!c->e[i].tr) {
            free_slot = i;
            break;
        }
        if (lru < 0 || c->e[i].use < use) {
            lru = i;
            use = c->e[i].use;
        }
    }
    return (free_slot >= 0) ? free_slot : lru;
}

/* El LRU entre los OCUPADOS (para desalojar). Nunca devuelve un hueco libre:
 * si lo hiciera, ensure_room no liberaria nada y el put fallaria con la cache
 * a medio llenar pero sin bytes disponibles. */
static int lru_used(const BpqWaveCache *c)
{
    int i, lru = -1;
    unsigned long long use = 0;

    for (i = 0; i < c->cap; i++) {
        if (!c->e[i].tr) continue;
        if (lru < 0 || c->e[i].use < use) {
            lru = i;
            use = c->e[i].use;
        }
    }
    return lru;
}

static int ensure_room(BpqWaveCache *c, size_t need)
{
    while (atomic_load(&c->n) >= c->cap ||
           atomic_load(&c->bytes) + (long long)need > (long long)c->max_bytes) {
        const int slot = lru_used(c);

        if (slot < 0) return 0;
        entry_clear(c, slot);
        atomic_fetch_add(&c->evictions, 1);
    }
    return 1;
}

int bpq_wavecache_has(const BpqWaveCache *c, const char *key)
{
    int i;

    if (!c || !key) return 0;
    for (i = 0; i < c->cap; i++)
        if (c->e[i].tr && strcmp(c->e[i].key, key) == 0) return 1;
    return 0;
}

int bpq_wavecache_get(BpqWaveCache *c, const char *key, BpqTrace **out)
{
    int i;

    if (out) *out = NULL;
    if (!c || !key || !out) return 0;
    for (i = 0; i < c->cap; i++) {
        BpqWaveCacheEntry *e = &c->e[i];

        if (!e->tr || strcmp(e->key, key) != 0) continue;
        if (!bpq_trace_clone(e->tr, out)) {
            atomic_fetch_add(&c->misses, 1);
            return 0;
        }
        e->use = ++c->tick;
        atomic_fetch_add(&c->hits, 1);
        return 1;
    }
    atomic_fetch_add(&c->misses, 1);
    return 0;
}

int bpq_wavecache_put(BpqWaveCache *c, const char *key, const BpqTrace *tr)
{
    BpqTrace *copy = NULL;
    size_t sz;
    int i, slot;

    if (!c || !key || !tr) return 0;
    sz = bpq_trace_bytes(tr);
    if (sz == 0) return 0;

    /* Misma clave: reemplazo (no consume entradas). */
    for (i = 0; i < c->cap; i++) {
        BpqWaveCacheEntry *e = &c->e[i];

        if (!e->tr || strcmp(e->key, key) != 0) continue;
        if (!bpq_trace_clone(tr, &copy)) return 0;
        bpq_trace_free(e->tr);
        atomic_fetch_sub(&c->bytes, (long long)e->bytes);
        e->tr = copy;
        e->bytes = sz;
        snprintf(e->key, sizeof(e->key), "%s", key);
        e->use = ++c->tick;
        atomic_fetch_add(&c->bytes, (long long)sz);
        atomic_fetch_add(&c->inserts, 1);
        return 1;
    }

    /* Ni vaciando la cache cabe: no se guarda (y se avisa en las stats). */
    if (sz > c->max_bytes) {
        atomic_fetch_add(&c->toobig, 1);
        return 0;
    }
    if (!ensure_room(c, sz)) {
        atomic_fetch_add(&c->toobig, 1);
        return 0;
    }
    slot = lru_slot(c);
    if (slot < 0) {
        atomic_fetch_add(&c->toobig, 1);
        return 0;
    }
    if (!bpq_trace_clone(tr, &copy)) return 0;
    c->e[slot].tr = copy;
    c->e[slot].bytes = sz;
    snprintf(c->e[slot].key, sizeof(c->e[slot].key), "%s", key);
    c->e[slot].use = ++c->tick;
    atomic_fetch_add(&c->bytes, (long long)sz);
    atomic_fetch_add(&c->n, 1);
    atomic_fetch_add(&c->inserts, 1);
    return 1;
}

void bpq_wavecache_clear(BpqWaveCache *c)
{
    int i;

    if (!c) return;
    for (i = 0; i < c->cap; i++) entry_clear(c, i);
}

void bpq_wavecache_stats(const BpqWaveCache *c, BpqWaveCacheStats *st)
{
    if (!st) return;
    memset(st, 0, sizeof(*st));
    if (!c) return;
    st->hits = atomic_load(&c->hits);
    st->misses = atomic_load(&c->misses);
    st->inserts = atomic_load(&c->inserts);
    st->evictions = atomic_load(&c->evictions);
    st->toobig = atomic_load(&c->toobig);
    st->entries = atomic_load(&c->n);
    st->bytes = atomic_load(&c->bytes);
}
