#ifndef WSTORE_INGEST_H
#define WSTORE_INGEST_H

#include "wstore.h"
#include "wstore_config.h"
#include "trace_buf.h"

/*
 * Ingester: enruta registros TRACE2 a escritores por canal, repartidos en
 * `ingest_shards` hilos (cada canal pertenece a un unico shard, asi que dos
 * hilos nunca escriben el mismo archivo). El nucleo (`_feed`) es testeable sin
 * anillo: aplica el filtro de canales (`ChannelFilterFile`, ver wstore_filter.h)
 * y descarta el registro antes de copiarlo. `_run_ring` es el bucle fino que lee
 * del anillo.
 */
typedef struct WStoreIngest WStoreIngest;

int  wstore_ingest_open (WStoreIngest **out, const WStoreConfig *cfg);
/* 0 = encolado (o descartado por el filtro), <0 = error. */
int  wstore_ingest_feed (WStoreIngest *ing, const TRACE2_HEADER *h, const int32_t *samples);
int  wstore_ingest_flush(WStoreIngest *ing);
void wstore_ingest_close(WStoreIngest *ing);
long wstore_ingest_dropped(const WStoreIngest *ing);
long wstore_ingest_filtered(const WStoreIngest *ing);

/* Bucle del anillo (bloqueante hasta TERMINATE). 0 = salida limpia. */
int  wstore_ingest_run_ring(WStoreIngest *ing, const WStoreConfig *cfg);

#endif /* WSTORE_INGEST_H */
