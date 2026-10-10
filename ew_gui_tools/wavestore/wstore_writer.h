#ifndef WSTORE_WRITER_H
#define WSTORE_WRITER_H

#include "wstore.h"
#include "trace_buf.h"

/* Escritor append-only, un canal por escritor. Abre/rota el segmento diario
 * segun el `starttime` del registro. Thread-safe solo si cada canal tiene su
 * propio escritor (sharding en el ingester). */
typedef struct WStoreWriter WStoreWriter;

int  wstore_writer_open (WStoreWriter **out, const char *root, const char *sta,
                         const char *net, const char *loc, const char *chan);
int  wstore_writer_append(WStoreWriter *w, const TRACE2_HEADER *h, const int32_t *samples);
int  wstore_writer_flush (WStoreWriter *w);
void wstore_writer_close(WStoreWriter *w);

#endif /* WSTORE_WRITER_H */
