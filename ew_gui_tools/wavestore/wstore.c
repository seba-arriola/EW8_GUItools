#define _GNU_SOURCE
#include "wstore.h"
#include "wstore_internal.h"

#include <dirent.h>
#include <fcntl.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

struct WStore {
    char       *root;
    WStoreOpts  opts;
};

int wstore_open(WStore **out, const char *root, const WStoreOpts *opts)
{
    WStore *s;
    if (!out || !root) return -1;
    s = (WStore *)calloc(1, sizeof(*s));
    if (!s) return -1;
    s->root = strdup(root);
    if (!s->root) { free(s); return -1; }
    if (opts) {
        s->opts = *opts;
    } else {
        s->opts.retention_days = 0;
        s->opts.reorder_ms = 0;
        s->opts.normalize_i4 = 1;
    }
    *out = s;
    return 0;
}

void wstore_close(WStore *s)
{
    if (!s) return;
    free(s->root);
    free(s);
}

int wstore_segment_path(char *buf, size_t n, const char *root, const char *sta,
                        const char *net, const char *loc, const char *chan, double t_epoch)
{
    char dir[1200], day[16];
    int  r;
    if (!buf) return -1;
    if (wstore_scnl_dir(dir, sizeof(dir), root, sta, net, loc, chan) != 0) return -1;
    if (wstore_day_of(t_epoch, day, sizeof(day)) != 0) return -1;
    r = snprintf(buf, n, "%s/%s.tank", dir, day);
    return (r > 0 && (size_t)r < n) ? 0 : -1;
}

void wstore_trace_free(WStoreTrace *t)
{
    if (!t) return;
    free(t->samples);
    t->samples = NULL;
    t->n = 0;
}

typedef struct {
    WStoreIdxEnt e;
    char         tank[1300];
} WMatch;

static int cmp_match(const void *a, const void *b)
{
    const WMatch *x = (const WMatch *)a, *y = (const WMatch *)b;
    if (x->e.start < y->e.start) return -1;
    if (x->e.start > y->e.start) return 1;
    return 0;
}

int wstore_get(WStore *s, const char *sta, const char *net, const char *chan,
               const char *loc, double t0, double t1, WStoreTrace *out)
{
    char           dir[1200];
    DIR           *dp;
    struct dirent *de;
    WMatch        *m = NULL;
    long           nm = 0, cap = 0;
    double         rate, tt0, last_end = -1.0;
    long           n;
    int32_t       *buf;

    if (!s || !out) return -1;
    if (wstore_scnl_dir(dir, sizeof(dir), s->root, sta, net, loc, chan) != 0) return -1;
    dp = opendir(dir);
    if (!dp) return 1;   /* sin datos */

    while ((de = readdir(dp)) != NULL) {
        char          tankp[1400], idxp[1400], *dot;
        int           tfd, ifd;
        WStoreIdxHdr  h;
        int64_t       f = -1, l = -1, i;

        if (!wstore_seg_overlaps(de->d_name, t0, t1)) continue;
        snprintf(tankp, sizeof(tankp), "%s/%s", dir, de->d_name);
        snprintf(idxp, sizeof(idxp), "%s/%s", dir, de->d_name);
        dot = strrchr(idxp, '.');
        if (!dot) continue;
        strcpy(dot, ".idx");

        tfd = open(tankp, O_RDONLY);
        if (tfd < 0) continue;
        ifd = open(idxp, O_RDONLY);
        if (ifd < 0) { close(tfd); continue; }
        if (wstore_index_hdr_read(ifd, &h) != 0 ||
            wstore_index_lookup(&h, ifd, t0, t1, &f, &l) <= 0 || f < 0 || l < f) {
            close(tfd);
            close(ifd);
            continue;
        }
        for (i = f; i <= l; i++) {
            WStoreIdxEnt e;
            if (wstore_index_ent_read(ifd, i, &e) != 0) continue;
            if (e.end < t0 || e.start > t1) continue;
            if (nm == cap) {
                WMatch *tmp;
                cap = cap ? cap * 2 : 64;
                tmp = (WMatch *)realloc(m, (size_t)cap * sizeof(*m));
                if (!tmp) { free(m); close(tfd); close(ifd); closedir(dp); return -1; }
                m = tmp;
            }
            m[nm].e = e;
            snprintf(m[nm].tank, sizeof(m[nm].tank), "%s", tankp);
            nm++;
        }
        close(tfd);
        close(ifd);
    }
    closedir(dp);

    if (nm == 0) { free(m); return 1; }

    qsort(m, (size_t)nm, sizeof(*m), cmp_match);

    rate = m[0].e.rate;
    tt0 = m[0].e.start;
    for (long k = 0; k < nm; k++)
        if (m[k].e.end > last_end) last_end = m[k].e.end;
    if (rate <= 0.0) { free(m); return -1; }

    n = (long)llround((last_end - tt0) * rate) + 1;
    if (n <= 0) n = 1;

    buf = (int32_t *)calloc((size_t)n, sizeof(int32_t));
    if (!buf) { free(m); return -1; }

    for (long k = 0; k < nm; k++) {
        int           fd;
        TRACE2_HEADER hh;
        int32_t      *sm = NULL;
        fd = open(m[k].tank, O_RDONLY);
        if (fd < 0) continue;
        if (wstore_read_record(fd, m[k].e.offset, &hh, &sm) == 0) {
            for (int j = 0; j < hh.nsamp; j++) {
                double ts  = hh.starttime + (double)j / hh.samprate;
                long   idx = lround((ts - tt0) * rate);
                if (idx >= 0 && idx < n) buf[idx] = sm[j];
            }
            free(sm);
        }
        close(fd);
    }

    memset(out, 0, sizeof(*out));
    snprintf(out->sta, sizeof(out->sta), "%s", sta);
    snprintf(out->net, sizeof(out->net), "%s", net);
    snprintf(out->chan, sizeof(out->chan), "%s", chan);
    snprintf(out->loc, sizeof(out->loc), "%s", (loc && loc[0]) ? loc : "--");
    out->t0 = tt0;
    out->samprate = rate;
    out->n = n;
    out->samples = buf;
    free(m);
    return 0;
}

int wstore_scnls(WStore *s, char (*out)[WSTORE_SCNL_LEN], int cap)
{
    DIR           *dp;
    struct dirent *de;
    int            n = 0;

    if (!s) return -1;
    dp = opendir(s->root);
    if (!dp) return 0;
    while ((de = readdir(dp)) != NULL) {
        char        p[1400];
        struct stat st;
        if (de->d_name[0] == '.') continue;
        snprintf(p, sizeof(p), "%s/%s", s->root, de->d_name);
        if (stat(p, &st) != 0 || !S_ISDIR(st.st_mode)) continue;
        if (out && n < cap) snprintf(out[n], WSTORE_SCNL_LEN, "%s", de->d_name);
        n++;
    }
    closedir(dp);
    return n;
}

/* Borra (o, en dry-run, solo mide) un fichero de segmento. Solo cuenta lo que
 * se borro DE VERDAD: si el `unlink` falla, no suma ni bytes ni fichero. */
static void prune_one(const char *path, int dry, long *removed, long *bytes)
{
    struct stat sb;

    if (stat(path, &sb) != 0) return;          /* no existe: nada que contar */
    if (dry) { (*removed)++; *bytes += (long)sb.st_size; return; }
    if (unlink(path) == 0) { (*removed)++; *bytes += (long)sb.st_size; }
}

int wstore_prune_ex(WStore *s, long keep_days, unsigned flags, WStorePruneStats *st)
{
    WStorePruneStats c = { 0, 0, 0 };
    DIR           *dp;
    struct dirent *de;
    time_t         now, today, cutoff;
    struct tm      tm;
    int            dry = (flags & WSTORE_PRUNE_DRY_RUN) != 0;

    if (st) memset(st, 0, sizeof(*st));
    if (!s) return -1;
    if (keep_days <= 0) return 0;
    now = time(NULL);
    if (!gmtime_r(&now, &tm)) return -1;
    tm.tm_hour = 0; tm.tm_min = 0; tm.tm_sec = 0;
    today = timegm(&tm);
    cutoff = today - (time_t)keep_days * 86400;

    dp = opendir(s->root);
    if (!dp) return 0;
    while ((de = readdir(dp)) != NULL) {
        char           dir[1400];
        DIR           *sd;
        struct dirent *fe;
        if (de->d_name[0] == '.') continue;
        snprintf(dir, sizeof(dir), "%s/%s", s->root, de->d_name);
        sd = opendir(dir);
        if (!sd) continue;
        while ((fe = readdir(sd)) != NULL) {
            char      *nm = fe->d_name;
            int        i, y, mo, d, ok = 1;
            struct tm  ft, chk;
            time_t     day;
            /* Nombre EXACTO `YYYYMMDD.tank` con una fecha REAL. Un `sscanf` a
             * secas daba 3 aciertos con cola (`20260101.txt`), con 7 digitos
             * (`2026010.tank`), o con una fecha imposible (`20260100.tank`), y
             * `timegm` normaliza lo imposible, asi que los tres se borraban. */
            if (strlen(nm) != 13 || strcmp(nm + 8, ".tank") != 0) continue;
            for (i = 0; i < 8; i++) if (nm[i] < '0' || nm[i] > '9') ok = 0;
            if (!ok) continue;
            if (sscanf(nm, "%4d%2d%2d", &y, &mo, &d) != 3) continue;
            memset(&ft, 0, sizeof(ft));
            ft.tm_year = y - 1900;
            ft.tm_mon = mo - 1;
            ft.tm_mday = d;
            day = timegm(&ft);
            /* El ida-y-vuelta descarta fechas que `timegm` tuvo que normalizar
             * (mes 13, dia 00, 31 de febrero). */
            if (!gmtime_r(&day, &chk)) continue;
            if (chk.tm_year + 1900 != y || chk.tm_mon + 1 != mo || chk.tm_mday != d) continue;
            if (day < cutoff) {
                char p[1500], ip[1500];
                snprintf(p, sizeof(p), "%s/%s", dir, fe->d_name);
                snprintf(ip, sizeof(ip), "%s/%.8s.idx", dir, fe->d_name);
                prune_one(p,  dry, &c.tanks_removed, &c.bytes_freed);
                prune_one(ip, dry, &c.idx_removed,   &c.bytes_freed);
            }
        }
        closedir(sd);
    }
    closedir(dp);
    if (st) *st = c;
    return 0;
}

int wstore_prune(WStore *s, long keep_days)
{
    return wstore_prune_ex(s, keep_days, 0u, NULL);
}

int wstore_prune_stats(WStore *s, long keep_days, WStorePruneStats *st)
{
    return wstore_prune_ex(s, keep_days, 0u, st);
}
