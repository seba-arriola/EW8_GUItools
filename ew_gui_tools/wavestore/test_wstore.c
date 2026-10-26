#define _GNU_SOURCE
#include "wstore.h"
#include "wstore_writer.h"

#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static void rm_tree(const char *path)
{
    DIR *d = opendir(path);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            char p[1400];
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            snprintf(p, sizeof(p), "%s/%s", path, e->d_name);
            rm_tree(p);
        }
        closedir(d);
    }
    remove(path);
}

static void mkrec(TRACE2_HEADER *h, const char *sta, const char *net, const char *loc,
                  const char *chan, double t0, double rate, int nsamp)
{
    memset(h, 0, sizeof(*h));
    snprintf(h->sta, sizeof(h->sta), "%s", sta);
    snprintf(h->net, sizeof(h->net), "%s", net);
    snprintf(h->chan, sizeof(h->chan), "%s", chan);
    snprintf(h->loc, sizeof(h->loc), "%s", loc);
    h->starttime = t0;
    h->samprate = rate;
    h->nsamp = nsamp;
    h->endtime = t0 + (nsamp > 1 ? (nsamp - 1) / rate : 0.0);
    memcpy(h->datatype, "i4", 3);
    h->version[0] = '2';
    h->version[1] = '0';
}

int main(void)
{
    char         root[] = "/tmp/wstore_test_XXXXXX";
    WStoreOpts   o;
    WStoreWriter *w = NULL;
    WStore       *s = NULL;
    WStoreTrace  tr;
    TRACE2_HEADER h;
    double        t0 = 1700000000.0, rate = 100.0, t2, t3;
    int           n1 = 100, n2 = 50, n3 = 200;
    int32_t      *s1, *s2, *s3;
    int           ok;

    if (!mkdtemp(root)) { perror("mkdtemp"); return 1; }

    memset(&o, 0, sizeof(o));
    o.normalize_i4 = 1;

    s1 = (int32_t *)malloc((size_t)n1 * 4);
    s2 = (int32_t *)malloc((size_t)n2 * 4);
    s3 = (int32_t *)malloc((size_t)n3 * 4);
    for (int i = 0; i < n1; i++) s1[i] = i + 1;
    for (int i = 0; i < n2; i++) s2[i] = 1000 + i;
    for (int i = 0; i < n3; i++) s3[i] = 5000 + i;

    CHECK(wstore_writer_open(&w, root, "ABC", "CX", "--", "HHZ") == 0);
    mkrec(&h, "ABC", "CX", "--", "HHZ", t0, rate, n1);
    CHECK(wstore_writer_append(w, &h, s1) == 0);
    t2 = t0 + n1 / rate;
    mkrec(&h, "ABC", "CX", "--", "HHZ", t2, rate, n2);
    CHECK(wstore_writer_append(w, &h, s2) == 0);
    t3 = t2 + n2 / rate;
    mkrec(&h, "ABC", "CX", "--", "HHZ", t3, rate, n3);
    CHECK(wstore_writer_append(w, &h, s3) == 0);
    CHECK(wstore_writer_flush(w) == 0);
    wstore_writer_close(w);

    /* --- lectura por ventana (AC2: indice == escaneo secuencial) --------- */
    CHECK(wstore_open(&s, root, &o) == 0);
    memset(&tr, 0, sizeof(tr));
    CHECK(wstore_get(s, "ABC", "CX", "HHZ", "--", t0, t3 + n3 / rate, &tr) == 0);
    CHECK(tr.n == n1 + n2 + n3);
    CHECK(fabs(tr.t0 - t0) < 1e-6);
    CHECK(fabs(tr.samprate - rate) < 1e-9);
    ok = 1;
    for (int i = 0; i < n1; i++) if (tr.samples[i] != s1[i]) ok = 0;
    for (int i = 0; i < n2; i++) if (tr.samples[n1 + i] != s2[i]) ok = 0;
    for (int i = 0; i < n3; i++) if (tr.samples[n1 + n2 + i] != s3[i]) ok = 0;
    CHECK(ok);
    wstore_trace_free(&tr);

    /* --- ventana sin datos / SCNL inexistente ---------------------------- */
    memset(&tr, 0, sizeof(tr));
    CHECK(wstore_get(s, "ABC", "CX", "HHZ", "--", t0 + 100000, t0 + 100010, &tr) == 1);
    CHECK(wstore_get(s, "ZZZ", "CX", "HHZ", "--", t0, t0 + 10, &tr) == 1);
    wstore_close(s);

    /* --- recuperacion del indice (borrado) ------------------------------- */
    {
        char segpath[1500], idxp[1500], *dot;
        CHECK(wstore_segment_path(segpath, sizeof(segpath), root, "ABC", "CX", "--", "HHZ", t0) == 0);
        snprintf(idxp, sizeof(idxp), "%s", segpath);
        dot = strrchr(idxp, '.');
        if (dot) strcpy(dot, ".idx");
        unlink(idxp);

        CHECK(wstore_writer_open(&w, root, "ABC", "CX", "--", "HHZ") == 0);
        mkrec(&h, "ABC", "CX", "--", "HHZ", t3 + n3 / rate, rate, 10);
        {
            int32_t extra[10];
            for (int i = 0; i < 10; i++) extra[i] = 9000 + i;
            CHECK(wstore_writer_append(w, &h, extra) == 0);
        }
        wstore_writer_close(w);

        CHECK(wstore_open(&s, root, &o) == 0);
        memset(&tr, 0, sizeof(tr));
        CHECK(wstore_get(s, "ABC", "CX", "HHZ", "--", t0, t3 + n3 / rate + 0.1, &tr) == 0);
        CHECK(tr.n == n1 + n2 + n3 + 10);
        CHECK(tr.samples[tr.n - 1] == 9009);
        wstore_trace_free(&tr);
        wstore_close(s);
    }

    /* --- rollover de dia (I4) -------------------------------------------- */
    {
        double tmid = 1700000000.0;   /* 2023-11-14T22:13:20Z */
        double dayend = 1700006400.0; /* 2023-11-15T00:00:00Z */
        CHECK(wstore_writer_open(&w, root, "ABC", "CX", "--", "HHN") == 0);
        mkrec(&h, "ABC", "CX", "--", "HHN", dayend - 1.0, rate, 100);
        CHECK(wstore_writer_append(w, &h, s1) == 0);
        mkrec(&h, "ABC", "CX", "--", "HHN", dayend, rate, 100);
        CHECK(wstore_writer_append(w, &h, s2) == 0);
        wstore_writer_close(w);
        CHECK(wstore_open(&s, root, &o) == 0);
        memset(&tr, 0, sizeof(tr));
        CHECK(wstore_get(s, "ABC", "CX", "HHN", "--", dayend - 2.0, dayend + 2.0, &tr) == 0);
        CHECK(tr.n == 200);
        CHECK(tr.samples[0] == s1[0]);
        CHECK(tr.samples[100] == s2[0]);
        wstore_trace_free(&tr);
        wstore_close(s);
        (void)tmid;
    }

    free(s1); free(s2); free(s3);
    rm_tree(root);

    if (failures) { printf("%d FALLOS\n", failures); return 1; }
    printf("test_wstore OK\n");
    return 0;
}
