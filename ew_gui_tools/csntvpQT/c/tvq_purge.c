/* Limpieza de la vista: quita las pistas cuya estacion lleva mas de N segundos
 * sin paquetes. Port de csntvp_gui.c:233-264, sin dialogo (la UI pide el
 * numero) y compactando el array igual que el GTK.
 *
 * Una estacion que NUNCA recibio datos (last_packet_wall == 0) tambien se
 * expulsa: es justo lo que el operador quiere limpiar.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tvq_capi.h"
#include "tvq_internal.h"

int tvq_core_clean(TvqCore *c, double max_age_s)
{
    double now;
    int kept = 0, removed = 0;

    if (!c || c->n_sta <= 0 || max_age_s <= 0.0)
        return 0;

    now = tvq_now_epoch();

    for (int i = 0; i < c->n_sta; i++) {
        TvqStation *s = &c->sta[i];
        int keep = (s->last_packet_wall > 0.0) &&
                   ((now - s->last_packet_wall) <= max_age_s);

        if (keep) {
            if (kept != i) {
                c->sta[kept] = *s;
                memset(s, 0, sizeof(*s));   /* propiedad transferida */
            }
            kept++;
        } else {
            free(s->circ);
            s->circ = NULL;
            ewgui_trace_cache_free(&s->cache);
            memset(s, 0, sizeof(*s));
            removed++;
        }
    }

    if (removed > 0) {
        c->n_sta = kept;
        tvq_env_invalidate(c);
        c->changed |= TVQ_CH_STATIONS;
    }
    return removed;
}
