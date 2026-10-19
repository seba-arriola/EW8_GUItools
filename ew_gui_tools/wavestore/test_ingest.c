#define _GNU_SOURCE
#include "wstore.h"
#include "wstore_config.h"
#include "wstore_ingest.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
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

/* Fecha YYYYMMDD (UTC) de "hoy - ago" dias: el MISMO reloj y la MISMA regla
 * que usa la purga (medianoche UTC), nunca fechas fijas. */
static void day_at(char *out, size_t n, int ago)
{
    time_t    now = time(NULL), t;
    struct tm tm;

    gmtime_r(&now, &tm);
    tm.tm_hour = 0; tm.tm_min = 0; tm.tm_sec = 0;
    t = timegm(&tm) - (time_t)ago * 86400;
    gmtime_r(&t, &tm);
    snprintf(out, n, "%04d%02d%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
}

static void mk_seg_dir(const char *dir, int ago)
{
    char day[64], p[1700];
    FILE *f;

    day_at(day, sizeof(day), ago);
    snprintf(p, sizeof(p), "%s/%s.tank", dir, day);
    f = fopen(p, "w"); if (f) fclose(f);
    snprintf(p, sizeof(p), "%s/%s.idx", dir, day);
    f = fopen(p, "w"); if (f) fclose(f);
}

static int seg_at(const char *dir, int ago, const char *ext)
{
    char day[64], p[1700];

    day_at(day, sizeof(day), ago);
    snprintf(p, sizeof(p), "%s/%s.%s", dir, day, ext);
    return access(p, F_OK) == 0;
}

/* Inyecta NCH canales (ST00..ST0N-1, chan HHZ, HHY, ...) x NREC registros. */
static void feed_chans(WStoreIngest *ing, int nch, int nrec, int per, double rate, double t0)
{
    for (int c = 0; c < nch; c++) {
        char    chan[8], sta[8];
        int32_t buf[per];
        snprintf(chan, sizeof(chan), "HH%c", 'Z' - c);
        snprintf(sta, sizeof(sta), "ST%02d", c);
        for (int k = 0; k < nrec; k++) {
            TRACE2_HEADER h;
            for (int i = 0; i < per; i++) buf[i] = c * 100000 + k * per + i + 1;
            memset(&h, 0, sizeof(h));
            snprintf(h.sta, sizeof(h.sta), "%s", sta);
            snprintf(h.net, sizeof(h.net), "CX");
            snprintf(h.chan, sizeof(h.chan), "%s", chan);
            snprintf(h.loc, sizeof(h.loc), "--");
            h.starttime = t0 + (double)k * per / rate;
            h.samprate = rate;
            h.nsamp = per;
            h.endtime = h.starttime + (per - 1) / rate;
            memcpy(h.datatype, "i4", 3);
            CHECK(wstore_ingest_feed(ing, &h, buf) == 0);
        }
    }
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

    feed_chans(ing, NCH, NREC, PER, rate, t0);

    CHECK(wstore_ingest_flush(ing) == 0);
    CHECK(wstore_ingest_dropped(ing) == 0);
    CHECK(wstore_ingest_filtered(ing) == 0);   /* sin ChannelFilter: se guarda todo */
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

    /* --- Escenario 2: ChannelFilterFile (se guarda solo ST01) ------------ */
    {
        char          froot[] = "/tmp/wstore_ingest_filter_XXXXXX";
        char          fpath[512];
        char          scnl[8][WSTORE_SCNL_LEN];
        WStoreConfig  fcfg;
        WStoreIngest *fing = NULL;
        WStore       *fs = NULL;
        WStoreTrace   tr;
        double        end = t0 + (double)NREC * PER / rate + 1.0;
        FILE         *fp;

        if (!mkdtemp(froot)) { perror("mkdtemp"); return 1; }
        snprintf(fpath, sizeof(fpath), "%s/filter.sta", froot);
        fp = fopen(fpath, "w");
        CHECK(fp != NULL);
        if (fp) {
            fputs("# ChannelFilterFile: STA NET CHAN LOC\n", fp);
            fputs("\n", fp);
            fputs("ST01  CX  HH?  --  # solo ST01, con localizacion nula\n", fp);
            fclose(fp);
        }

        memset(&fcfg, 0, sizeof(fcfg));
        snprintf(fcfg.root, sizeof(fcfg.root), "%s/store", froot);
        snprintf(fcfg.channel_filter_file, sizeof(fcfg.channel_filter_file), "%s", fpath);
        fcfg.ingest_shards = 4;
        fcfg.retention_days = 0;

        CHECK(wstore_ingest_open(&fing, &fcfg) == 0);
        feed_chans(fing, NCH, NREC, PER, rate, t0);
        CHECK(wstore_ingest_filtered(fing) == (long)(NCH - 1) * NREC);
        CHECK(wstore_ingest_dropped(fing) == 0);
        CHECK(wstore_ingest_flush(fing) == 0);
        wstore_ingest_close(fing);

        CHECK(wstore_open(&fs, fcfg.root, NULL) == 0);
        CHECK(wstore_scnls(fs, scnl, 8) == 1);   /* solo el canal que pasa el filtro */
        CHECK(strcmp(scnl[0], "CX.ST01.--.HHY") == 0);

        memset(&tr, 0, sizeof(tr));
        CHECK(wstore_get(fs, "ST01", "CX", "HHY", "--", t0, end, &tr) == 0);
        CHECK(tr.n == NREC * PER);
        if (tr.samples) wstore_trace_free(&tr);

        memset(&tr, 0, sizeof(tr));
        CHECK(wstore_get(fs, "ST00", "CX", "HHZ", "--", t0, end, &tr) == 1);  /* filtrado */
        if (tr.samples) wstore_trace_free(&tr);

        wstore_close(fs);
        rm_tree(froot);
    }

    /* --- Escenario 3: purga por retencion (wstore_ingest_purge_now) ------ */
    {
        char             proot[] = "/tmp/wstore_ingest_prune_XXXXXX";
        char             pdir[1400];
        WStoreConfig     pcfg;
        WStoreIngest    *ping = NULL;
        WStorePruneStats pst;
        int              k;

        if (!mkdtemp(proot)) { perror("mkdtemp"); return 1; }
        snprintf(pdir, sizeof(pdir), "%s/CX.ST00.--.HHZ", proot);
        CHECK(mkdir(pdir, 0777) == 0);
        mk_seg_dir(pdir, 0);                        /* hoy: nunca se purga */
        for (k = 1; k < 4; k++) mk_seg_dir(pdir, 29 + k);   /* 30, 31, 32 dias */

        memset(&pcfg, 0, sizeof(pcfg));
        snprintf(pcfg.root, sizeof(pcfg.root), "%s", proot);
        pcfg.ingest_shards = 4;
        pcfg.retention_days = 7;

        CHECK(wstore_ingest_open(&ping, &pcfg) == 0);

        memset(&pst, 0, sizeof(pst));
        CHECK(wstore_ingest_purge_now(ping, &pcfg, &pst) == 0);
        CHECK(pst.tanks_removed == 3);
        CHECK(pst.idx_removed == 3);
        CHECK(seg_at(pdir, 0, "tank"));             /* el dia en curso queda */
        for (k = 1; k < 4; k++) {
            CHECK(!seg_at(pdir, 29 + k, "tank"));
            CHECK(!seg_at(pdir, 29 + k, "idx"));
        }

        /* Segunda pasada: nada que borrar, y el conteo queda a cero. */
        CHECK(wstore_ingest_purge_now(ping, &pcfg, &pst) == 0);
        CHECK(pst.tanks_removed == 0 && pst.idx_removed == 0);

        /* RetentionDays 0 = sin retencion: no toca nada. */
        mk_seg_dir(pdir, 40);
        pcfg.retention_days = 0;
        CHECK(wstore_ingest_purge_now(ping, &pcfg, &pst) == 0);
        CHECK(pst.tanks_removed == 0);
        CHECK(seg_at(pdir, 40, "tank"));

        wstore_ingest_close(ping);
        rm_tree(proot);
    }

    if (failures) { printf("%d FALLOS\n", failures); return 1; }
    printf("test_ingest OK\n");
    return 0;
}
