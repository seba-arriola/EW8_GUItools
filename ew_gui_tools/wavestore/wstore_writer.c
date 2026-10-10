#define _GNU_SOURCE
#include "wstore_writer.h"
#include "wstore_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

struct WStoreWriter {
    char    root[1024];
    char    dir[1300];
    char    cur_day[16];
    FILE   *tank;
    int     idx_fd;
    int64_t count;
    int64_t tank_size;
    int64_t flags;
    double  last_start;
};

static int write_hdr(WStoreWriter *w)
{
    WStoreIdxHdr h;
    memset(&h, 0, sizeof(h));
    memcpy(h.magic, WSTORE_IDX_MAGIC, 8);
    h.version  = WSTORE_IDX_VERSION;
    h.rec_size = WSTORE_IDX_ENT_SIZE;
    h.count    = w->count;
    h.flags    = w->flags;
    return wstore_index_hdr_write(w->idx_fd, &h);
}

static int open_segment(WStoreWriter *w, const char *day)
{
    char         tankp[1500], idxp[1500];
    WStoreIdxHdr h;
    int          need_rebuild = 0;

    snprintf(tankp, sizeof(tankp), "%s/%s.tank", w->dir, day);
    snprintf(idxp, sizeof(idxp), "%s/%s.idx", w->dir, day);

    w->tank = fopen(tankp, "r+b");
    if (!w->tank) w->tank = fopen(tankp, "w+b");
    if (!w->tank) return -1;
    fseeko(w->tank, 0, SEEK_END);
    w->tank_size = (int64_t)ftello(w->tank);

    w->idx_fd = open(idxp, O_RDWR | O_CREAT, 0644);
    if (w->idx_fd < 0) { fclose(w->tank); w->tank = NULL; return -1; }

    if (wstore_index_hdr_read(w->idx_fd, &h) != 0) {
        need_rebuild = 1;
    } else if (h.count == 0 && w->tank_size > 0) {
        need_rebuild = 1;   /* idx vacio pero tank con datos: inconsistente */
    } else {
        w->count = h.count;
        w->flags = h.flags;
    }

    if (need_rebuild) {
        close(w->idx_fd);
        w->idx_fd = -1;
        if (wstore_index_rebuild(tankp, idxp) != 0) { fclose(w->tank); w->tank = NULL; return -1; }
        w->idx_fd = open(idxp, O_RDWR);
        if (w->idx_fd < 0 || wstore_index_hdr_read(w->idx_fd, &h) != 0) {
            if (w->idx_fd >= 0) close(w->idx_fd);
            fclose(w->tank);
            w->tank = NULL;
            w->idx_fd = -1;
            return -1;
        }
        w->count = h.count;
        w->flags = h.flags;
    }

    if (w->count > 0) {
        WStoreIdxEnt e;
        if (wstore_index_ent_read(w->idx_fd, w->count - 1, &e) == 0) w->last_start = e.start;
        else w->last_start = -1.0e300;
    } else {
        w->last_start = -1.0e300;
    }

    snprintf(w->cur_day, sizeof(w->cur_day), "%s", day);
    return 0;
}

int wstore_writer_open(WStoreWriter **out, const char *root, const char *sta,
                       const char *net, const char *loc, const char *chan)
{
    WStoreWriter *w;
    if (!out || !root || !sta || !net || !chan) return -1;
    w = (WStoreWriter *)calloc(1, sizeof(*w));
    if (!w) return -1;
    snprintf(w->root, sizeof(w->root), "%s", root);
    if (wstore_scnl_dir(w->dir, sizeof(w->dir), root, sta, net, loc, chan) != 0) {
        free(w);
        return -1;
    }
    if (wstore_mkdir_p(w->dir) != 0) { free(w); return -1; }
    w->idx_fd = -1;
    w->cur_day[0] = '\0';
    w->last_start = -1.0e300;
    *out = w;
    return 0;
}

int wstore_writer_append(WStoreWriter *w, const TRACE2_HEADER *h, const int32_t *samples)
{
    char          day[16];
    TRACE2_HEADER hh;
    WStoreIdxEnt  e;
    int64_t       off, nsz;

    if (!w || !h) return -1;
    if (wstore_day_of(h->starttime, day, sizeof(day)) != 0) return -1;

    if (w->tank == NULL || strcmp(day, w->cur_day) != 0) {
        if (w->tank) { wstore_writer_flush(w); fclose(w->tank); w->tank = NULL; }
        if (w->idx_fd >= 0) { close(w->idx_fd); w->idx_fd = -1; }
        if (open_segment(w, day) != 0) return -1;
    }

    hh = *h;
    memcpy(hh.datatype, "i4", 3);
    hh.version[0] = TRACE2_VERSION0;
    hh.version[1] = TRACE2_VERSION1;
    if (hh.nsamp > 1 && hh.samprate > 0.0)
        hh.endtime = hh.starttime + (hh.nsamp - 1) / hh.samprate;

    off = w->tank_size;
    if (wstore_append_record(w->tank, off, &hh, samples, &nsz) != 0) return -1;
    w->tank_size = nsz;

    /* I3: los datos deben ser visibles ANTES de publicar la entrada. */
    if (fflush(w->tank) != 0) return -1;

    e.start = hh.starttime;
    e.end = hh.endtime;
    e.rate = hh.samprate;
    e.offset = off;
    e.nsamp = hh.nsamp;
    e.reserved = 0;

    if (w->count > 0 && e.start < w->last_start) w->flags &= ~WSTORE_IDX_FLAG_SORTED;
    if (wstore_index_ent_write(w->idx_fd, w->count, &e) != 0) return -1;
    w->last_start = e.start;
    w->count++;
    return write_hdr(w);
}

int wstore_writer_flush(WStoreWriter *w)
{
    if (!w || !w->tank) return -1;
    if (fflush(w->tank) != 0) return -1;
    fdatasync(fileno(w->tank));
    return 0;
}

void wstore_writer_close(WStoreWriter *w)
{
    if (!w) return;
    if (w->tank) { wstore_writer_flush(w); fclose(w->tank); }
    if (w->idx_fd >= 0) close(w->idx_fd);
    free(w);
}
