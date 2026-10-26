#define _GNU_SOURCE
#include "wstore_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

long wstore_record_size(int32_t nsamp)
{
    return (long)sizeof(TRACE2_HEADER) + (long)nsamp * 4L;
}

int wstore_read_record(int fd, int64_t offset, TRACE2_HEADER *h, int32_t **samples)
{
    int32_t *s;
    size_t   nbytes;

    if (!h || !samples || offset < 0) return -1;
    if (pread(fd, h, sizeof(*h), (off_t)offset) != (ssize_t)sizeof(*h)) return -1;
    if (h->nsamp <= 0 || h->nsamp > 200000) return -1;

    nbytes = (size_t)h->nsamp * sizeof(int32_t);
    s = (int32_t *)malloc(nbytes);
    if (!s) return -1;
    if (pread(fd, s, nbytes, (off_t)(offset + (int64_t)sizeof(*h))) != (ssize_t)nbytes) {
        free(s);
        return -1;
    }
    *samples = s;
    return 0;
}

int wstore_normalize_samples(const TRACE2_HEADER *h, const void *raw, int32_t **out)
{
    int32_t *s;

    if (!h || !raw || !out) return -1;
    if (h->nsamp <= 0 || h->nsamp > 200000) return -1;
    s = (int32_t *)malloc((size_t)h->nsamp * sizeof(int32_t));
    if (!s) return -1;

    if (memcmp(h->datatype, "i4", 2) == 0 || memcmp(h->datatype, "s4", 2) == 0) {
        memcpy(s, raw, (size_t)h->nsamp * sizeof(int32_t));
    } else if (memcmp(h->datatype, "i2", 2) == 0 || memcmp(h->datatype, "s2", 2) == 0) {
        const int16_t *r = (const int16_t *)raw;
        for (int i = 0; i < h->nsamp; i++) s[i] = r[i];
    } else {
        free(s);
        return 1;   /* datatype no soportado */
    }
    *out = s;
    return 0;
}

int wstore_append_record(FILE *seg, int64_t at, const TRACE2_HEADER *h,
                         const int32_t *samples, int64_t *new_size)
{
    if (!seg || !h) return -1;
    if (fseeko(seg, (off_t)at, SEEK_SET) != 0) return -1;
    if (fwrite(h, sizeof(*h), 1, seg) != 1) return -1;
    if (h->nsamp > 0) {
        if (fwrite(samples, sizeof(int32_t), (size_t)h->nsamp, seg) != (size_t)h->nsamp)
            return -1;
    }
    if (new_size) *new_size = at + wstore_record_size(h->nsamp);
    return 0;
}

int wstore_scnl_dir(char *buf, size_t n, const char *root, const char *sta,
                    const char *net, const char *loc, const char *chan)
{
    const char *l;
    int         r;
    if (!buf || !root || !sta || !net || !chan) return -1;
    l = (loc && loc[0]) ? loc : "--";
    r = snprintf(buf, n, "%s/%s.%s.%s.%s", root, net, sta, l, chan);
    return (r > 0 && (size_t)r < n) ? 0 : -1;
}

int wstore_day_of(double t_epoch, char *out, size_t n)
{
    time_t   tt = (time_t)floor(t_epoch);
    struct tm tm;
    if (!out || n < 9) return -1;
    if (!gmtime_r(&tt, &tm)) return -1;
    snprintf(out, n, "%04d%02d%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    return 0;
}

int wstore_seg_overlaps(const char *name, double t0, double t1)
{
    int       y, m, d;
    struct tm tm;
    time_t    ds;
    double    a, b;

    if (!name) return 0;
    if (sscanf(name, "%4d%2d%2d.ws", &y, &m, &d) != 3) return 0;
    memset(&tm, 0, sizeof(tm));
    tm.tm_year = y - 1900;
    tm.tm_mon  = m - 1;
    tm.tm_mday = d;
    ds = timegm(&tm);
    if (ds == (time_t)-1) return 0;
    a = (double)ds;
    b = a + 86400.0;
    return (a <= t1 && b >= t0);
}

int wstore_mkdir_p(const char *path)
{
    char  tmp[1200];
    char *p;

    if (!path || !path[0]) return -1;
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return -1;
            *p = '/';
        }
    }
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return -1;
    return 0;
}
