#define _GNU_SOURCE
#include "wstore.h"
#include "wstore_writer.h"

#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NREC     2000
#define PER_REC  50
#define RATE     100.0
#define T0       1700000000.0
#define PATTERN  0x5A5A5A5A

static const char *g_root;
static volatile int g_bad;

static void *writer_fn(void *arg)
{
    WStoreWriter *w = NULL;
    int32_t       buf[PER_REC];
    (void)arg;

    if (wstore_writer_open(&w, g_root, "ABC", "CX", "--", "HHZ") != 0) {
        g_bad = 1;
        return NULL;
    }
    for (int i = 0; i < PER_REC; i++) buf[i] = PATTERN;
    for (int k = 0; k < NREC; k++) {
        TRACE2_HEADER h;
        memset(&h, 0, sizeof(h));
        snprintf(h.sta, sizeof(h.sta), "ABC");
        snprintf(h.net, sizeof(h.net), "CX");
        snprintf(h.chan, sizeof(h.chan), "HHZ");
        snprintf(h.loc, sizeof(h.loc), "--");
        h.starttime = T0 + (double)k * PER_REC / RATE;
        h.samprate = RATE;
        h.nsamp = PER_REC;
        h.endtime = h.starttime + (PER_REC - 1) / RATE;
        memcpy(h.datatype, "i4", 3);
        if (wstore_writer_append(w, &h, buf) != 0) { g_bad = 1; break; }
    }
    wstore_writer_close(w);
    return NULL;
}

static void *reader_fn(void *arg)
{
    WStore      *s = NULL;
    WStoreTrace  tr;
    double       end = T0 + (double)NREC * PER_REC / RATE + 10.0;
    (void)arg;

    if (wstore_open(&s, g_root, NULL) != 0) { g_bad = 1; return NULL; }
    for (int it = 0; it < 300; it++) {
        memset(&tr, 0, sizeof(tr));
        if (wstore_get(s, "ABC", "CX", "HHZ", "--", T0, end, &tr) == 0) {
            for (long i = 0; i < tr.n; i++) {
                int32_t v = tr.samples[i];
                if (v != 0 && v != PATTERN) g_bad = 1;   /* corrupcion */
            }
            wstore_trace_free(&tr);
        }
        sched_yield();
    }
    wstore_close(s);
    return NULL;
}

int main(void)
{
    char          root[] = "/tmp/wstore_conc_XXXXXX";
    pthread_t     tw, tr;
    WStore       *s = NULL;
    WStoreTrace   t;
    long          expected = (long)NREC * PER_REC;

    if (!mkdtemp(root)) { perror("mkdtemp"); return 1; }
    g_root = root;

    if (pthread_create(&tw, NULL, writer_fn, NULL) != 0) { perror("pthread"); return 1; }
    if (pthread_create(&tr, NULL, reader_fn, NULL) != 0) { perror("pthread"); return 1; }
    pthread_join(tw, NULL);
    pthread_join(tr, NULL);

    if (wstore_open(&s, root, NULL) != 0) { printf("FAIL open\n"); return 1; }
    memset(&t, 0, sizeof(t));
    if (wstore_get(s, "ABC", "CX", "HHZ", "--", T0, T0 + (double)expected / RATE + 10.0, &t) != 0) {
        printf("FAIL get final\n");
        return 1;
    }
    if (t.n != expected) { printf("FAIL n=%ld esperado=%ld\n", t.n, expected); return 1; }
    for (long i = 0; i < t.n; i++) {
        if (t.samples[i] != PATTERN) { printf("FAIL muestra %ld\n", i); return 1; }
    }
    wstore_trace_free(&t);
    wstore_close(s);

    if (g_bad) { printf("FAIL: corrupcion durante la concurrencia\n"); return 1; }
    printf("test_wstore_concurrent OK (%ld muestras)\n", expected);
    return 0;
}
