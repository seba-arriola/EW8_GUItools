/* Rejilla de trazas por pantalla (`tracesPerScreen`): solo cuentas, sin GTK.
 * Ver csnhypodbp_traces.h para el contrato. */
#include "csnhypodbp_traces.h"

int csnhypodbp_traces_per_screen_norm(int raw)
{
    if (raw < CSNHYPODBP_TRACES_PER_SCREEN_MIN ||
        raw > CSNHYPODBP_TRACES_PER_SCREEN_MAX)
        return 0;   /* invalido: el llamante avisa y conserva el previo */
    return raw;
}

CsnhypodbpTracesLayout csnhypodbp_traces_layout(int n_rows, int per_screen,
                                                int viewport_h, int band_min)
{
    CsnhypodbpTracesLayout l;
    int band;

    if (band_min <= 0) band_min = 1;
    if (per_screen < CSNHYPODBP_TRACES_PER_SCREEN_MIN ||
        per_screen > CSNHYPODBP_TRACES_PER_SCREEN_MAX)
        per_screen = CSNHYPODBP_TRACES_PER_SCREEN_MIN;

    l.per_screen = per_screen;
    /* Rejilla de `per_screen` huecos: con menos canales no se estira la traza
       (queda un hueco vacio abajo); con mas, crece el contenido y hay scroll. */
    l.slot_count = (n_rows > per_screen) ? n_rows : per_screen;

    /* Sin viewport asignado (arranque, antes del primer reparto de GTK) se usa
       el suelo: el siguiente dibujo ya trae el alto real y reajusta. */
    band = (viewport_h > 0)
         ? (viewport_h - CSNHYPODBP_TRACES_MARGIN) / per_screen
         : band_min;
    if (band < band_min) band = band_min;

    l.band_h = band;
    l.canvas_h = l.slot_count * band + CSNHYPODBP_TRACES_MARGIN;
    return l;
}
