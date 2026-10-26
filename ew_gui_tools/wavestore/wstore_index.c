#define _GNU_SOURCE
#include "wstore_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

int wstore_index_hdr_read(int fd, WStoreIdxHdr *h)
{
    ssize_t r;
    if (!h) return -1;
    r = pread(fd, h, sizeof(*h), 0);
    if (r != (ssize_t)sizeof(*h)) return -1;
    if (memcmp(h->magic, WSTORE_IDX_MAGIC, 8) != 0) return -1;
    if (h->version != WSTORE_IDX_VERSION) return -1;
    if (h->rec_size != WSTORE_IDX_ENT_SIZE) return -1;
    if (h->count < 0) return -1;
    return 0;
}

int wstore_index_hdr_write(int fd, const WStoreIdxHdr *h)
{
    if (!h) return -1;
    return (pwrite(fd, h, sizeof(*h), 0) == (ssize_t)sizeof(*h)) ? 0 : -1;
}

int wstore_index_ent_read(int fd, int64_t i, WStoreIdxEnt *e)
{
    off_t off;
    if (!e || i < 0) return -1;
    off = (off_t)WSTORE_IDX_HDR_SIZE + (off_t)i * WSTORE_IDX_ENT_SIZE;
    return (pread(fd, e, sizeof(*e), off) == (ssize_t)sizeof(*e)) ? 0 : -1;
}

int wstore_index_ent_write(int fd, int64_t i, const WStoreIdxEnt *e)
{
    off_t off;
    if (!e || i < 0) return -1;
    off = (off_t)WSTORE_IDX_HDR_SIZE + (off_t)i * WSTORE_IDX_ENT_SIZE;
    return (pwrite(fd, e, sizeof(*e), off) == (ssize_t)sizeof(*e)) ? 0 : -1;
}

int wstore_index_lookup(const WStoreIdxHdr *h, int fd, double t0, double t1,
                        int64_t *first, int64_t *last)
{
    int64_t lo, hi, f = -1, l = -1;

    if (!h || !first || !last) return -1;
    if (h->count <= 0) return 0;

    /* Indice no ordenado (hubo desorden): rango completo, el llamador filtra. */
    if (!(h->flags & WSTORE_IDX_FLAG_SORTED)) {
        *first = 0;
        *last = h->count - 1;
        return 1;
    }

    /* Primera entrada cuyo `end` alcanza t0. */
    lo = 0; hi = h->count - 1;
    while (lo <= hi) {
        int64_t m = lo + (hi - lo) / 2;
        WStoreIdxEnt e;
        if (wstore_index_ent_read(fd, m, &e) != 0) return -1;
        if (e.end >= t0) { f = m; hi = m - 1; }
        else lo = m + 1;
    }
    if (f < 0) return 0;

    /* Ultima entrada cuyo `start` no supera t1. */
    lo = f; hi = h->count - 1;
    while (lo <= hi) {
        int64_t m = lo + (hi - lo) / 2;
        WStoreIdxEnt e;
        if (wstore_index_ent_read(fd, m, &e) != 0) return -1;
        if (e.start <= t1) { l = m; lo = m + 1; }
        else hi = m - 1;
    }
    if (l < f) return 0;

    *first = f;
    *last = l;
    return 1;
}

int wstore_index_rebuild(const char *seg_path, const char *idx_path)
{
    FILE       *tf;
    int         fd;
    struct stat st;
    WStoreIdxHdr h;
    int64_t     off = 0;
    double      last_start = -1.0;

    tf = fopen(seg_path, "rb");
    if (!tf) return -1;
    if (stat(seg_path, &st) != 0) { fclose(tf); return -1; }

    fd = open(idx_path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { fclose(tf); return -1; }

    memset(&h, 0, sizeof(h));
    memcpy(h.magic, WSTORE_IDX_MAGIC, 8);
    h.version  = WSTORE_IDX_VERSION;
    h.rec_size = WSTORE_IDX_ENT_SIZE;
    h.count    = 0;
    h.flags    = WSTORE_IDX_FLAG_SORTED;
    if (wstore_index_hdr_write(fd, &h) != 0) { close(fd); fclose(tf); return -1; }

    for (;;) {
        TRACE2_HEADER head;
        WStoreIdxEnt  e;
        long          recsz;

        if (fread(&head, sizeof(head), 1, tf) != 1) break;
        if (head.nsamp <= 0 || head.nsamp > 200000) break;
        recsz = wstore_record_size(head.nsamp);
        /* Registro truncado al final (crash a medias): no lo indexamos. */
        if (off + recsz > (int64_t)st.st_size) break;

        e.start    = head.starttime;
        e.end      = head.starttime + (head.nsamp > 1 ? (head.nsamp - 1) / head.samprate : 0.0);
        e.rate     = head.samprate;
        e.offset   = off;
        e.nsamp    = head.nsamp;
        e.reserved = 0;
        if (last_start >= 0.0 && e.start < last_start) h.flags &= ~WSTORE_IDX_FLAG_SORTED;
        last_start = e.start;

        if (wstore_index_ent_write(fd, h.count, &e) != 0) { close(fd); fclose(tf); return -1; }
        h.count++;
        off += recsz;

        if (fseek(tf, 4L * head.nsamp, SEEK_CUR) != 0) break;
    }

    if (wstore_index_hdr_write(fd, &h) != 0) { close(fd); fclose(tf); return -1; }
    close(fd);
    fclose(tf);
    return 0;
}
