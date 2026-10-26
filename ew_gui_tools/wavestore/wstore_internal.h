#ifndef WSTORE_INTERNAL_H
#define WSTORE_INTERNAL_H

#include "wstore.h"
#include "trace_buf.h"

#include <stdio.h>

/* --- Indice lateral ----------------------------------------------------- */

#define WSTORE_IDX_MAGIC      "WSTIDX1"
#define WSTORE_IDX_VERSION    1
#define WSTORE_IDX_HDR_SIZE   56
#define WSTORE_IDX_ENT_SIZE   40
#define WSTORE_IDX_FLAG_SORTED 1

typedef struct {
    char    magic[8];      /* "WSTIDX1\0"                          offset 0  */
    int32_t version;       /* WSTORE_IDX_VERSION                              */
    int32_t rec_size;      /* sizeof(WStoreIdxEnt)                            */
    int64_t count;         /* entradas publicadas                 offset 16 */
    int64_t flags;         /* WSTORE_IDX_FLAG_*                   offset 24 */
    int64_t reserved[3];   /*                                     offset 32 */
} WStoreIdxHdr;

typedef struct {
    double  start;         /* epoch de la primera muestra */
    double  end;           /* epoch de la ultima muestra  */
    double  rate;          /* Hz                          */
    int64_t offset;        /* offset del registro en el .ws */
    int32_t nsamp;
    int32_t reserved;
} WStoreIdxEnt;

/* --- Indice (wstore_index.c) -------------------------------------------- */
int  wstore_index_hdr_read (int fd, WStoreIdxHdr *h);
int  wstore_index_hdr_write(int fd, const WStoreIdxHdr *h);
int  wstore_index_ent_read (int fd, int64_t i, WStoreIdxEnt *e);
int  wstore_index_ent_write(int fd, int64_t i, const WStoreIdxEnt *e);
/* 1 = hay rango [*first,*last], 0 = sin datos, <0 error. */
int  wstore_index_lookup(const WStoreIdxHdr *h, int fd, double t0, double t1,
                         int64_t *first, int64_t *last);
/* Reescribe el .idx escaneando el .ws. 0 OK. */
int  wstore_index_rebuild(const char *seg_path, const char *idx_path);

/* --- I/O de registros y rutas (wstore_io.c) ----------------------------- */
long wstore_record_size(int32_t nsamp);
int  wstore_read_record(int fd, int64_t offset, TRACE2_HEADER *h, int32_t **samples);
/* Normaliza las muestras crudas de un registro a int32.
 * 0 OK (*out malloc), 1 datatype no soportado, <0 error. */
int  wstore_normalize_samples(const TRACE2_HEADER *h, const void *raw, int32_t **out);
int  wstore_append_record(FILE *seg, int64_t at, const TRACE2_HEADER *h,
                          const int32_t *samples, int64_t *new_size);
int  wstore_scnl_dir(char *buf, size_t n, const char *root, const char *sta,
                     const char *net, const char *loc, const char *chan);
int  wstore_day_of(double t_epoch, char *out, size_t n);
int  wstore_seg_overlaps(const char *name, double t0, double t1);
int  wstore_mkdir_p(const char *path);

#endif /* WSTORE_INTERNAL_H */
