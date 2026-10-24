/*
 * test_wavecache.c - Cache de trazas (LRU por bytes) de la capa C.
 *
 * Cubre el contrato que usa el worker:
 *   - hit devuelve una COPIA (liberarla no toca la cache);
 *   - la clave incluye SCNL + t0 + span (una ventana distinta no colisiona);
 *   - el tope de bytes y el de entradas se respetan con eviccion LRU;
 *   - una entrada que no cabe ni con la cache vacia no se guarda (toobig);
 *   - cache deshabilitada (0 MB) = NULL y todas las funciones son no-op.
 *
 *   make -C ew_gui_tools/csnhypodbpQT test_wavecache && ./test_wavecache
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bpq_capi.h"

static int g_checks, g_fail;

#define CK(c) do {                                                        \
        g_checks++;                                                       \
        if (!(c)) {                                                       \
            g_fail++;                                                     \
            fprintf(stderr, "FALLA %s:%d: %s\n", __FILE__, __LINE__, #c); \
        }                                                                 \
    } while (0)

static BpqTrace *mk(double t0)
{
    int32_t buf[100];
    int i;

    for (i = 0; i < 100; i++) buf[i] = i;
    return bpq_trace_new(t0, 100.0, buf, 100);
}

int main(void)
{
    BpqWaveCache *c;
    BpqTrace *t1, *t2, *t3, *hit = NULL;
    char k1[128], k2[128], k3[128], kspan[128], kbad[8];
    const size_t sz = bpq_trace_bytes(mk(10.0));
    BpqWaveCacheStats st;

    printf("== test_wavecache ==\n");

    t1 = mk(10.0);
    t2 = mk(20.0);
    t3 = mk(30.0);
    CK(t1 && t2 && t3);
    CK(sz > sizeof(void *));
    CK(bpq_trace_bytes(NULL) == 0);

    CK(bpq_wavecache_key(k1, sizeof k1, "PB01", "C1", "HHZ", "--", 10.0, 120.0) == 1);
    CK(bpq_wavecache_key(k2, sizeof k2, "PB02", "C1", "HHZ", "--", 10.0, 120.0) == 1);
    CK(bpq_wavecache_key(k3, sizeof k3, "PB01", "C1", "HHN", "--", 10.0, 120.0) == 1);
    CK(bpq_wavecache_key(kspan, sizeof kspan, "PB01", "C1", "HHZ", "--", 10.0,
                         700.0) == 1);
    CK(strcmp(k1, k2) != 0 && strcmp(k1, k3) != 0 && strcmp(k1, kspan) != 0);
    CK(bpq_wavecache_key(kbad, sizeof kbad, "PB01", "C1", "HHZ", "--", NAN,
                         120.0) == 0);
    CK(bpq_wavecache_key(kbad, sizeof kbad, "PB01", "C1", "HHZ", "--", 10.0,
                         0.0) == 0);
    CK(bpq_wavecache_key(kbad, sizeof kbad, "", "C1", "HHZ", "--", 10.0,
                         120.0) == 0);

    /* Deshabilitada y null-safe. */
    CK(bpq_wavecache_new(0, 10) == NULL);
    CK(bpq_wavecache_new(1024, 0) == NULL);
    CK(bpq_wavecache_get(NULL, k1, &hit) == 0 && hit == NULL);
    CK(bpq_wavecache_put(NULL, k1, t1) == 0);
    CK(bpq_wavecache_has(NULL, k1) == 0);
    bpq_wavecache_clear(NULL);
    bpq_wavecache_stats(NULL, &st);
    CK(st.entries == 0);
    bpq_wavecache_free(NULL);

    /* --- hit devuelve una copia independiente ----------------------------- */
    c = bpq_wavecache_new(1024 * 1024, 8);
    CK(c != NULL);
    CK(bpq_wavecache_put(c, k1, t1) == 1);
    CK(bpq_wavecache_has(c, k1) == 1);
    CK(bpq_wavecache_get(c, k1, &hit) == 1);
    CK(hit != NULL && hit != t1);
    CK(bpq_trace_n(hit) == 100);
    bpq_trace_free(hit);
    hit = NULL;
    CK(bpq_wavecache_get(c, k1, &hit) == 1);   /* la cache sigue intacta */
    bpq_trace_free(hit);
    hit = NULL;
    bpq_wavecache_stats(c, &st);
    CK(st.hits == 2 && st.misses == 0 && st.entries == 1);
    CK(st.bytes == (long long)sz);

    /* --- la clave separa ventanas y canales ------------------------------ */
    CK(bpq_wavecache_get(c, k2, &hit) == 0 && hit == NULL);
    CK(bpq_wavecache_put(c, k2, t2) == 1);
    CK(bpq_wavecache_put(c, k3, t3) == 1);
    CK(bpq_wavecache_get(c, k1, &hit) == 1);
    bpq_trace_free(hit);
    hit = NULL;
    CK(bpq_wavecache_get(c, kspan, &hit) == 0);
    bpq_wavecache_stats(c, &st);
    CK(st.entries == 3);
    CK(st.misses == 2);
    CK(st.bytes == (long long)(3 * sz));

    /* --- tope de entradas: se va la menos usada (k2, no se volvio a tocar) */
    {
        BpqWaveCache *cc = bpq_wavecache_new(1024 * 1024, 2);
        char ka[128], kb[128], kc[128];

        CK(cc != NULL);
        CK(bpq_wavecache_key(ka, sizeof ka, "A", "C1", "HHZ", "--", 1.0, 120.0));
        CK(bpq_wavecache_key(kb, sizeof kb, "B", "C1", "HHZ", "--", 1.0, 120.0));
        CK(bpq_wavecache_key(kc, sizeof kc, "C", "C1", "HHZ", "--", 1.0, 120.0));
        CK(bpq_wavecache_put(cc, ka, t1) == 1);
        CK(bpq_wavecache_put(cc, kb, t2) == 1);
        CK(bpq_wavecache_put(cc, kc, t3) == 1);
        CK(bpq_wavecache_has(cc, ka) == 0);    /* LRU */
        CK(bpq_wavecache_has(cc, kb) == 1);
        CK(bpq_wavecache_has(cc, kc) == 1);
        bpq_wavecache_stats(cc, &st);
        CK(st.entries == 2 && st.evictions >= 1);
        bpq_wavecache_free(cc);
    }

    /* --- tope de bytes ---------------------------------------------------- */
    {
        BpqWaveCache *cc = bpq_wavecache_new(sz * 2, 100);
        char ka[128], kb[128], kc[128];

        CK(cc != NULL);
        CK(bpq_wavecache_key(ka, sizeof ka, "A", "C1", "HHZ", "--", 1.0, 120.0));
        CK(bpq_wavecache_key(kb, sizeof kb, "B", "C1", "HHZ", "--", 1.0, 120.0));
        CK(bpq_wavecache_key(kc, sizeof kc, "C", "C1", "HHZ", "--", 1.0, 120.0));
        CK(bpq_wavecache_put(cc, ka, t1) == 1);
        CK(bpq_wavecache_put(cc, kb, t2) == 1);
        CK(bpq_wavecache_put(cc, kc, t3) == 1);
        bpq_wavecache_stats(cc, &st);
        CK(st.entries == 2);
        CK(st.bytes <= (long long)(sz * 2));
        CK(st.evictions >= 1);
        bpq_wavecache_free(cc);
    }

    /* --- entrada mayor que toda la cache --------------------------------- */
    {
        BpqWaveCache *cc = bpq_wavecache_new(sz - 1, 10);
        char ka[128];

        CK(cc != NULL);
        CK(bpq_wavecache_key(ka, sizeof ka, "A", "C1", "HHZ", "--", 1.0, 120.0));
        CK(bpq_wavecache_put(cc, ka, t1) == 0);
        bpq_wavecache_stats(cc, &st);
        CK(st.toobig == 1 && st.entries == 0);
        bpq_wavecache_free(cc);
    }

    /* --- clear ------------------------------------------------------------ */
    bpq_wavecache_clear(c);
    bpq_wavecache_stats(c, &st);
    CK(st.entries == 0 && st.bytes == 0);
    CK(bpq_wavecache_get(c, k1, &hit) == 0);

    bpq_wavecache_free(c);
    bpq_trace_free(t1);
    bpq_trace_free(t2);
    bpq_trace_free(t3);

    printf("== wavecache checks=%d fallas=%d ==\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
