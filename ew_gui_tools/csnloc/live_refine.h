#ifndef LIVE_REFINE_H
#define LIVE_REFINE_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "live_assoc.h"

/*
 * Motor vivo COMPARTIDO por los localizadores con `live_usage=1`.
 *
 * Contrato (el mismo para hyp2000_ring, nlloc_ring o cualquier refinador):
 *   1. El modulo recibe la semilla de HYPO_RING (`live_refine_seed`) y las
 *      confirmaciones de HYPO_RING_REF2 (`live_refine_prefer`).
 *   2. Acumula las picadas automaticas de PICK_RING
 *      (`live_refine_add_pick_msg`).
 *   3. Cada `time_recalc_live` construye un ARC SEMBRADO con el hipocentro del
 *      seed y sus fases + las picadas asociadas por tiempo de viaje, llama al
 *      localizador del modulo, valida, poda/reintenta y publica CADA pasada
 *      valida con version nueva (`<event>:<mod>:<version>:I` en la BD).
 *
 * El motor NO conoce anillos, NLLoc/HYPOINVERSE ni ids de modulo: todo eso
 * entra por los callbacks y por `LiveRefineOps`. El mod lo pone el modulo al
 * publicar (logo del anillo).
 */

typedef struct LiveRefine LiveRefine;

typedef int (*LiveRefineLocate)(void *ud, const char *arc_in, int in_len,
                                char *arc_out, int out_cap, int *out_len);
typedef int (*LiveRefineRewrite)(void *ud, char *arc, int arc_cap, int len,
                                 const char *arc_in, int in_len);
typedef int (*LiveRefinePublish)(void *ud, const char *arc_in, int in_len,
                                 const char *arc_out, int out_len);
/* Opcional: descarta una salida fuera del bbox del modulo. */
typedef int (*LiveRefineInRegion)(void *ud, double lat, double lon);

typedef struct {
    LiveRefineLocate   locate;
    LiveRefineRewrite  rewrite;
    LiveRefinePublish  publish;
    LiveRefineInRegion in_region;   /* NULL = sin bbox */
    void              *ud;
    const StationList *stations;    /* prestado por el modulo */
    TTModel           *tt;          /* prestado por el modulo */
    LiveAssocCfg       assoc;
    LiveLocCheckCfg    check;
    int                recalc_sec;  /* time_recalc_live de la tabla */
    int                max_iter;    /* LiveMaxIter */
    int                n_retry;     /* N_retry de la tabla */
    int                max_events;
    int                debug;
    double             event_ttl_sec;
    const char        *tag;         /* prefijo de los logs */
} LiveRefineOps;

LiveRefine *live_refine_new(const LiveRefineOps *ops);
void        live_refine_free(LiveRefine *r);

void live_refine_seed(LiveRefine *r, const char *arc, int len, int mod);
void live_refine_prefer(LiveRefine *r, const char *arc, int len, int mod);
int  live_refine_add_pick(LiveRefine *r, const Pick *p);
int  live_refine_add_pick_msg(LiveRefine *r, const char *msg, int len);
void live_refine_prune(LiveRefine *r, time_t now);

/* Una pasada. Devuelve cuantas soluciones publico. */
int  live_refine_tick(LiveRefine *r, time_t now);

#endif /* LIVE_REFINE_H */
