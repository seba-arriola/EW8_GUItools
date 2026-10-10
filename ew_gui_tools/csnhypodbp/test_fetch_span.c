/*
 * test_fetch_span - Regresion de la pestana Ondas de csnhypodbp.
 *
 * La primera descarga de una estacion crea la traza de forma perezosa
 * (StaArray[i].trace == NULL -> ewgui_trace_new). Antes, el unico
 * ewgui_trace_set_oldest estaba bajo un `if (tr)`, asi que la traza nueva
 * quedaba con oldest=0 y el draw (p_start_k = (t - oldest) * rate) no
 * dibujaba nada. Este test reproduce esa secuencia y exige que el span
 * (oldest+rate+length) quede aplicado.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "ewgui/wave.h"

int main(void)
{
    const double req_start = 1600000000.0;
    const double rate = 100.0;
    const long   len = 500;
    EwGuiTrace  *tr = NULL;   /* primera descarga: sin traza */

    /* ws_fetch_finish: crea la traza y aplica el span del fetch. */
    tr = ewgui_trace_new(1000);
    assert(tr != NULL);
    for (long i = 0; i < len; i++) ewgui_trace_raw(tr)[i] = 1000;
    ewgui_trace_set_span(tr, req_start, rate, len);

    assert(ewgui_trace_oldest(tr) == req_start);
    assert(ewgui_trace_rate(tr) == rate);
    assert(ewgui_trace_length(tr) == len);

    /* El post-proceso (finish) no debe perder el oldest. */
    ewgui_trace_finish(tr);
    assert(ewgui_trace_oldest(tr) == req_start);

    /* Invariante del draw: el indice de la ventana cae en [0, length). */
    {
        double win_start = req_start + 2.0;
        long k0 = (long)((win_start - ewgui_trace_oldest(tr)) * ewgui_trace_rate(tr));
        assert(k0 >= 0 && k0 < ewgui_trace_length(tr));
    }

    ewgui_trace_free(tr);
    printf("test_fetch_span OK\n");
    return 0;
}
