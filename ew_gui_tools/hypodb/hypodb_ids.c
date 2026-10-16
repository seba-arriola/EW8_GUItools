#include "hypodb_ids.h"

#include "ewgui/db_ids.h"

/*
 * Fachada del módulo sobre la implementación común de los ids (ver
 * hypodb_ids.h y common/include/ewgui/db_ids.h). No hay copia local: los ids
 * los comparten este ingester y la GUI `csnhypodbp`, y tener una sola
 * implementación es lo que evita que diverjan.
 */

int hypodb_event_id(char *out, size_t n, const char *qid, double otime)
{
    return ewgui_event_id(out, n, qid, otime);
}

int hypodb_origin_id(char *out, size_t n, const char *event_id, int mod)
{
    return ewgui_origin_id(out, n, event_id, mod);
}

int hypodb_magnitude_id(char *out, size_t n, const char *origin_id,
                        const char *type)
{
    return ewgui_magnitude_id(out, n, origin_id, type);
}

int hypodb_station_magnitude_id(char *out, size_t n, const char *magnitude_id,
                                const char *channel)
{
    return ewgui_station_magnitude_id(out, n, magnitude_id, channel);
}

int hypodb_pick_id(char *out, size_t n, const char *origin_id,
                   const char *sta, const char *phase)
{
    return ewgui_pick_id(out, n, origin_id, sta, phase);
}

int hypodb_channel_nslco(char *out, size_t n, const char *net,
                         const char *sta, const char *loc, const char *chan)
{
    return ewgui_channel_nslco(out, n, net, sta, loc, chan);
}
