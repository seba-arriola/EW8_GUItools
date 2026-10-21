/******************************************************************************
 * pickml_window.c — geometría de ventanas deslizantes.                       *
 *                                                                            *
 * Una ventana [t0, t0+WindowSec] se procesa cuando la ventana está COMPLETA.  *
 * Consecuencia de latencia (importante, no es un bug): un arribo en t se       *
 * publica en la primera ventana completa que lo contiene, es decir ~WindowSec *
 * después de t. Con EQTransformer (60 s) la latencia inherente es ~50-60 s;   *
 * el consumidor (csnloc) debe tener AssocWindowSec >= esa latencia o los      *
 * picks se descartan por viejos.                                             *
 ******************************************************************************/

#include "pickml.h"

void pm_win_init(PmWin *w, double t_start, double step_sec, double win_sec)
{
    w->t_next   = t_start;
    w->step_sec = step_sec;
    w->win_sec  = win_sec;
}

int pm_win_next(PmWin *w, double t_ready, double *t0)
{
    if (!w || w->step_sec <= 0.0 || w->win_sec <= 0.0) return 0;
    /* +1e-6: el último paquete puede terminar en el borde exacto. */
    if (w->t_next + w->win_sec > t_ready + 1e-6) return 0;

    if (t0) *t0 = w->t_next;
    w->t_next += w->step_sec;
    return 1;
}
