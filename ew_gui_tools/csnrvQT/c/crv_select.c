/* Politica de seleccion del evento vivo (contrato en crv_capi.h).
 *
 * El visor es "el ultimo sismo", no "el ultimo mensaje". Un refinador con
 * live_usage=1 republica correcciones de eventos antiguos cada
 * time_recalc_live (medido: 1709 pasadas sobre un evento de 3 dias en 14 h, y
 * versiones de mas de 5000), asi que quedarse con el ultimo ARC aceptado hacia
 * que "Time Since Quake" saltara a decenas/cientos de horas y creciera sin
 * tope hasta reiniciar. La regla "nunca retroceder de hora de origen" es pura
 * y testeable.
 *
 * Ojo: el `version` NO sirve de identidad de evento. csnloc lo escribe con
 * `%04d` en 4 bytes (envuelve 9999 -> 1000) y el qid se repite cada 100000 s
 * (27.77 h), ademas de aliasarse con `% 2147000000`.
 */
#include <math.h>

#include "crv_capi.h"

CrvEventDecision crv_event_decide(const CrvSnapshot *cur, const CrvArc *in,
                                  int event_match_sec)
{
    double match;

    if (!in || in->t0 <= 0.0 || in->qid <= 0)
        return CRV_EVENT_INVALID;
    if (!cur || !cur->has_data || cur->origin_time <= 0.0)
        return CRV_EVENT_FIRST;

    match = (event_match_sec > 0) ? (double)event_match_sec : 0.0;
    if (in->qid == cur->qid && fabs(in->t0 - cur->origin_time) <= match)
        return CRV_EVENT_UPDATE;
    if (in->t0 >= cur->origin_time)
        return CRV_EVENT_NEW;
    return CRV_EVENT_STALE;
}
