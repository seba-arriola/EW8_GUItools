#ifndef PICKML_SDS_H
#define PICKML_SDS_H

#include <stdint.h>

#include "pickml.h"

/*
 * Lector del archivo SDS de SeisComP (headless, sin GTK):
 *
 *   <root>/<YYYY>/<NET>/<STA>/<CHAN>.D/<NET>.<STA>.<LOC>.<CHAN>.D.<YYYY>.<DDD>
 *
 * Dos capas:
 *   1. `pmsds_channel` lee UN canal en [t0,t1), recorre los dias UTC y decima
 *      a `rate` (factores 1/2/4, con el mismo anti-alias que pickml_fetch).
 *   2. `PmSds` es el backend que consume `pm_src_next`: recorre estacion por
 *      estacion, dia por dia, entrelaza Z/N/E por starttime y entrega tramos
 *      de 1 s (nunca el dia completo: `advance` se llama por traza).
 *
 * El location nulo se normaliza a vacio en la ruta (convencion SDS) y la traza
 * devuelta conserva "--", igual que el backend del store.
 */

typedef enum {
    PMSDS_OK = 0,
    PMSDS_NODATA,    /* el dia existe pero no hay muestras en el rango */
    PMSDS_NO_DAY,    /* no hay archivo para ese dia */
    PMSDS_RATE,      /* tasa no soportada (no es Rate, 2x ni 4x) */
    PMSDS_IO         /* error de memoria/argumentos */
} PmSdsWhy;

int pmsds_channel(const char *root, const char *net, const char *sta,
                  const char *loc, const char *chan,
                  double t0, double t1, double rate,
                  int32_t **out, long *n, double *start, PmSdsWhy *why);

typedef struct PmSds PmSds;

PmSds *pmsds_open(const char *root, const PmStation *sta, int nsta,
                  double t0, double t1, double rate);
int    pmsds_next(PmSds *s, PmTrace *tr);   /* 1 hay, 0 fin, <0 error */
void   pmsds_close(PmSds *s);
void   pmsds_counts(const PmSds *s, PmSrcCounts *c);

#endif /* PICKML_SDS_H */
