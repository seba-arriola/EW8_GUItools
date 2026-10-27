/* Envolvente min/max por columna, cacheada por estacion.
 *
 * Port de csntvp_data.c:414-471 con la misma politica de cadencia (el
 * intervalo sale del ancho de ventana en pixeles, acotado a 0.5..1.5 s) y el
 * filtro SIEMPRE causal (zerophase=0), que es el comportamiento historico de
 * csntvp. El crudo no se toca: ewgui_trace_envelope trabaja sobre su copia.
 */
#include <math.h>
#include <stdio.h>

#include "tvq_capi.h"
#include "tvq_internal.h"

void tvq_env_invalidate(TvqCore *c)
{
    if (!c)
        return;
    for (int i = 0; i < c->n_sta; i++)
        ewgui_trace_cache_invalidate(&c->sta[i].cache);
    c->env_ok = 0;
    c->env_w = 0;
}

void tvq_env_update(TvqCore *c, int width, int force)
{
    double t_right;
    double now;
    int should = 0;
    EwFilterParams fp;

    if (!c || c->n_sta <= 0 || width <= 0)
        return;

    t_right = c->hold ? c->hold_t : c->smooth_t;
    if (t_right <= 0.0)
        return;

    now = tvq_now_epoch();

    if (force) {
        should = 1;
    } else if (c->hold) {
        if (c->env_w != width || !c->env_ok)
            should = 1;
    } else if (!(fabs(t_right - c->env_t_right) < 0.5 && c->stale)) {
        double window_secs = (double)c->cfg->time_window_min * 60.0;
        double interval_ms = (window_secs / (double)width) * 1000.0;

        if (interval_ms < 500.0)
            interval_ms = 500.0;
        if (interval_ms > 1500.0)
            interval_ms = 1500.0;
        if ((now - c->env_wall) * 1000.0 >= interval_ms || c->env_w != width)
            should = 1;
    }
    if (!should)
        return;

    c->env_w = width;
    c->env_t_right = t_right;
    c->env_wall = now;
    c->env_ok = 1;
    c->env_repaint = 1;

    fp.filter_type = c->filter_type;
    fp.f1 = c->f1;
    fp.f2 = c->f2;
    fp.order = c->order;
    fp.zerophase = 0;   /* csntvp mantiene el filtro causal historico */

    for (int i = 0; i < c->n_sta; i++) {
        TvqStation *s = &c->sta[i];

        if (!s->circ || s->circ_size <= 0)
            continue;
        (void)ewgui_trace_envelope(s->circ, s->circ_size, s->last_abs, s->rate,
                                   t_right, (double)c->cfg->time_window_min * 60.0,
                                   30.0, width, &fp, &s->cache);
    }
}
