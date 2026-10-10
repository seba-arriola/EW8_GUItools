#define _GNU_SOURCE
#include "wstore.h"
#include "wstore_writer.h"
#include "wavesource.h"

#include <dirent.h>
#include <math.h>
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

static void mkrec(TRACE2_HEADER *h, double t0, double rate, int nsamp)
{
    memset(h, 0, sizeof(*h));
    snprintf(h->sta, sizeof(h->sta), "ABC");
    snprintf(h->net, sizeof(h->net), "CX");
    snprintf(h->chan, sizeof(h->chan), "HHZ");
    snprintf(h->loc, sizeof(h->loc), "--");
    h->starttime = t0;
    h->samprate = rate;
    h->nsamp = nsamp;
    h->endtime = t0 + (nsamp - 1) / rate;
    memcpy(h->datatype, "i4", 3);
    h->version[0] = '2';
    h->version[1] = '0';
}

int main(void)
{
    char          root[] = "/tmp/wstore_ws_XXXXXX";
    char          tankp[1400];
    double        t0 = 1700000000.0, rate = 100.0;
    int           per = 100, nrec = 3;
    int32_t       buf[100];
    FILE         *tf;
    WStoreWriter *wr = NULL;
    WaveSource   *wsrc = NULL;
    WaveTrace     a, b;
    WaveSrcCfg    cfg;
    TRACE2_HEADER h;

    if (!mkdtemp(root)) { perror("mkdtemp"); return 1; }
    snprintf(tankp, sizeof(tankp), "%s/tank.tank", root);

    CHECK(wstore_writer_open(&wr, root, "ABC", "CX", "--", "HHZ") == 0);
    tf = fopen(tankp, "wb");
    CHECK(tf != NULL);

    for (int k = 0; k < nrec; k++) {
        for (int i = 0; i < per; i++) buf[i] = k * per + i + 1;
        mkrec(&h, t0 + (double)k * per / rate, rate, per);
        CHECK(wstore_writer_append(wr, &h, buf) == 0);
        fwrite(&h, sizeof(h), 1, tf);
        fwrite(buf, sizeof(int32_t), (size_t)per, tf);
    }
    wstore_writer_close(wr);
    fclose(tf);

    /* TANK backend */
    memset(&cfg, 0, sizeof(cfg));
    cfg.mode = WSRC_TANK;
    cfg.tank_or_null = tankp;
    CHECK(wave_open(&wsrc, &cfg) == 0);
    memset(&a, 0, sizeof(a));
    CHECK(wave_get(wsrc, "ABC", "CX", "HHZ", "--", t0, t0 + 5.0, &a) == 0);
    wave_close(wsrc);

    /* STORE backend */
    memset(&cfg, 0, sizeof(cfg));
    cfg.mode = WSRC_STORE;
    cfg.store_root = root;
    CHECK(wave_open(&wsrc, &cfg) == 0);
    memset(&b, 0, sizeof(b));
    CHECK(wave_get(wsrc, "ABC", "CX", "HHZ", "--", t0, t0 + 5.0, &b) == 0);
    wave_close(wsrc);

    /* AC8: store == tank */
    CHECK(a.n == nrec * per);
    CHECK(b.n == nrec * per);
    CHECK(a.n == b.n);
    {
        int ok = 1;
        for (long i = 0; i < a.n && i < b.n; i++) if (a.x[i] != b.x[i]) ok = 0;
        CHECK(ok);
    }
    CHECK(fabs(a.t0 - b.t0) < 1e-6);

    free(a.x);
    free(b.x);
    rm_tree(root);

    if (failures) { printf("%d FALLOS\n", failures); return 1; }
    printf("test_wavesource OK\n");
    return 0;
}
