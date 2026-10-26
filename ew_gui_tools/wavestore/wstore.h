#ifndef WSTORE_H
#define WSTORE_H

/*
 * wstore — store de formas de onda indexado (headless, segment-compatible).
 *
 * Formato en disco:
 *   <root>/<NET>.<STA>.<LOC>.<CHAN>/<YYYYMMDD>.ws   (TRACE2_HEADER + int32)
 *   <root>/<NET>.<STA>.<LOC>.<CHAN>/<YYYYMMDD>.idx    (indice lateral)
 *
 * El `.ws` es un segment valido (lo leen pick_FP/pickS/csnmags sin
 * cambios); el `.idx` permite acceso por ventana O(log n).
 */

#include <stddef.h>
#include <stdint.h>

typedef struct WStore WStore;

typedef struct {
    long retention_days;   /* 0 = sin retencion (solo informativo aqui) */
    int  reorder_ms;       /* reservado (buffer de reorden del ingester) */
    int  normalize_i4;     /* 1 = forzar datatype i4 al escribir */
} WStoreOpts;

typedef struct {
    char     sta[8];
    char     net[10];
    char     chan[8];
    char     loc[4];
    double   t0;           /* epoch de la primera muestra */
    double   samprate;     /* Hz */
    long     n;            /* numero de muestras */
    int32_t *samples;      /* malloc; liberar con wstore_trace_free */
} WStoreTrace;

#define WSTORE_SCNL_LEN 40

/* Abre el store en `root` (no falla si el directorio no existe todavia). */
int   wstore_open (WStore **out, const char *root, const WStoreOpts *opts);
void  wstore_close(WStore *s);

/*
 * Lee la ventana [t0,t1] del SCNL dado, en un buffer contiguo sobre la grilla
 * del primer registro (huecos = 0, solapes = el ultimo gana).
 * Devuelve 0 con datos, 1 sin datos, <0 error. Thread-safe para lecturas
 * concurrentes.
 */
int   wstore_get  (WStore *s, const char *sta, const char *net, const char *chan,
                   const char *loc, double t0, double t1, WStoreTrace *out);

/* Lista los SCNL presentes (nombres de directorio). Devuelve el total hallado. */
int   wstore_scnls(WStore *s, char (*out)[WSTORE_SCNL_LEN], int cap);

/* Conteo de una purga. */
typedef struct {
    long segments_removed;   /* YYYYMMDD.ws borrados                  */
    long idx_removed;     /* .idx companeros borrados                */
    long bytes_freed;     /* suma de tamanos (stat previo al unlink) */
} WStorePruneStats;

#define WSTORE_PRUNE_DRY_RUN 1u   /* cuenta lo que se borraria, sin borrar */

/* Borra segmentos (segment+idx) anteriores a `keep_days` dias UTC. 0 OK. */
int   wstore_prune(WStore *s, long keep_days);

/* Igual, pero devolviendo el conteo (`st` puede ser NULL). */
int   wstore_prune_stats(WStore *s, long keep_days, WStorePruneStats *st);

/* Nucleo: `flags` acepta WSTORE_PRUNE_DRY_RUN. Unico camino real. */
int   wstore_prune_ex(WStore *s, long keep_days, unsigned flags, WStorePruneStats *st);

/* Ruta del segmento diario para un SCNL y un instante. 0 OK, <0 error. */
int   wstore_segment_path(char *buf, size_t n, const char *root, const char *sta,
                          const char *net, const char *loc, const char *chan, double t_epoch);

void  wstore_trace_free(WStoreTrace *t);

#endif /* WSTORE_H */
