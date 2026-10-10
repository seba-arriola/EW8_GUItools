#define _GNU_SOURCE
#include "wstore.h"
#include "wstore_config.h"
#include "wstore_ingest.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

int main(void)
{
    char          root[] = "/tmp/wstore_ingest_XXXXXX";
    WStoreConfig  cfg;
    WStoreIngest *ing = NULL;
    WStore       *s = NULL;
    const int     NCH = 6, NREC = 200, PER = 20;
    double        rate = 100.0, t0 = 1700000000.0;

    if (!mkdtemp(root)) { perror("mkdtemp"); return 1; }

    memset(&cfg, 0, sizeof(cfg));
    snprintf(cfg.root, sizeof(cfg.root), "%s", root);
    cfg.ingest_shards = 4;
    cfg.retention_days = 0;

    CHECK(wstore_ingest_open(&ing, &cfg) == 0);

    for (int c = 0; c < NCH; c++) {
        char    chan[8], sta[8];
        int32_t buf[PER];
        snprintf(chan, sizeof(chan), "HH%c", 'Z' - c);
        snprintf(sta, sizeof(sta), "ST%02d", c);
        for (int k = 0; k < NREC; k++) {
            TRACE2_HEADER h;
            for (int i = 0; i < PER; i++) buf[i] = c * 100000 + k * PER + i + 1;
            memset(&h, 0, sizeof(h));
            snprintf(h.sta, sizeof(h.sta), "%s", sta);
            snprintf(h.net, sizeof(h.net), "CX");
            snprintf(h.chan, sizeof(h.chan), "%s", chan);
            snprintf(h.loc, sizeof(h.loc), "--");
            h.starttime = t0 + (double)k * PER / rate;
            h.samprate = rate;
            h.nsamp = PER;
            h.endtime = h.starttime + (PER - 1) / rate;
            memcpy(h.datatype, "i4", 3);
            CHECK(wstore_ingest_feed(ing, &h, buf) == 0);
        }
    }

    CHECK(wstore_ingest_flush(ing) == 0);
    CHECK(wstore_ingest_dropped(ing) == 0);
    wstore_ingest_close(ing);

    CHECK(wstore_open(&s, root, NULL) == 0);
    for (int c = 0; c < NCH; c++) {
        char         chan[8], sta[8];
        WStoreTrace  tr;
        double       end = t0 + (double)NREC * PER / rate + 1.0;
        int          r, ok;
        snprintf(chan, sizeof(chan), "HH%c", 'Z' - c);
        snprintf(sta, sizeof(sta), "ST%02d", c);
        memset(&tr, 0, sizeof(tr));
        r = wstore_get(s, sta, "CX", chan, "--", t0, end, &tr);
        CHECK(r == 0);
        if (r != 0) continue;
        CHECK(tr.n == NREC * PER);
        ok = 1;
        for (int k = 0; k < NREC; k++)
            for (int i = 0; i < PER; i++)
                if (tr.samples[k * PER + i] != c * 100000 + k * PER + i + 1) ok = 0;
        CHECK(ok);
        wstore_trace_free(&tr);
    }
    wstore_close(s);
    rm_tree(root);

    if (failures) { printf("%d FALLOS\n", failures); return 1; }
    printf("test_ingest OK\n");
    return 0;
}
