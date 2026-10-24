/*
 * bpq_tt.c - Inicializacion del modelo de tiempos de viaje (P teorica).
 *
 * `csnhypodbp_sds.c`, `csnhypodbp_ws.c` y `c/bpq_stations.c` usan
 * `csnhypodbp_tt_p()` para marcar la P teorica de las estaciones sin picada
 * M/A, pero el modelo hay que cargarlo UNA vez al arrancar (como hace
 * `csnhypodbp_main.c:2564`). Sin esto `csnhypodbp_tt_ready()` queda en 0,
 * `theo_p` en 0 y las ventanas de magnitud de esas estaciones no existen.
 *
 * La capa C no imprime: devuelve el motivo en `err` para que `ui/main.cxx`
 * lo mande a stderr (igual que los avisos de configuracion).
 */
#include <stdio.h>

#include "bpq_capi.h"

#include "csnhypodbp_tt.h"

int bpq_init_tt(const BpqConfig *cfg, char *err, size_t errn)
{
    const char *model = (cfg && cfg->tau_model[0]) ? cfg->tau_model : "iasp91";

    if (csnhypodbp_tt_init(model) == 0) return 1;
    if (err && errn)
        snprintf(err, errn,
                 "sin modelo de tiempos de viaje <%s> en el cwd "
                 "(%s.hed/.tbl); P teorica deshabilitada", model, model);
    return 0;
}
