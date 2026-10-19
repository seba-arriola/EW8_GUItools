#ifndef CSNHYPODBP_MWW_H
#define CSNHYPODBP_MWW_H

#include <gtk/gtk.h>
#include <stddef.h>

#include "csnhypodbp_group.h"
#include "csnwphase.h"

/*
 * Panel `Mww` (inversión de fase W) de la pestaña *magnitudes*.
 *
 * Es un cálculo ON-DEMAND: nada se ejecuta hasta que el usuario pulsa
 * `Calculate`. El trabajo pesado (ondas del store/SDS, motor vendorizado
 * eost/wphase) corre en un hilo de trabajo y la UI se actualiza en el hilo de
 * GTK con g_idle_add, igual que el `locate` de la pestaña *picking*.
 *
 * La configuración NO vive aquí: se lee el `MwwConfigFile` de csnhypodbp.d
 * (default `csnwphase.d`, el mismo `.d` del CLI) y el directorio de trabajo de
 * las corridas sale de `MwwWorkDir`.
 */

/* Construye el panel (una sola vez). */
GtkWidget *csnhypodbp_mww_panel_new(void);

/*
 * ÚNICO punto de render del panel: estado, tabla de parámetros, resumen,
 * playera y visor de la salida. Todo cambio de estado llama AQUÍ, para que no
 * se desincronicen los botones, los valores y el color.
 */
void csnhypodbp_mww_update(void);

/*
 * Resultado Mww VIGENTE para una solución (qid + mod del localizador). Devuelve
 * el resultado (no NULL) solo si la última corrida es de ESA solución: el panel
 * no borra el resultado viejo al cambiar de selección, así que sin esta
 * comprobación el preview le atribuiría a una solución el Mww de otra.
 */
const CsnwphaseOut *csnhypodbp_mww_result_for(int qid, int mod);

/*
 * Guarda el mecanismo focal y la magnitud Mww del resultado pendiente sobre la
 * solución confirmada: la llama `on_preview_confirm` (csnhypodbp_gui.c). Crea el
 * evento y el origen si aún no están en la base. Devuelve 0 si OK y -1 con el
 * error real en `err`.
 */
int csnhypodbp_mww_persist(const CsnhypodbpSolution *sol, const CsnwphaseOut *out,
                           char *err, size_t errlen);

/* Refleja en el estado del panel el resultado de `csnhypodbp_mww_persist`
 * (mensaje y color). */
void csnhypodbp_mww_report(int ok, const char *msg);

#endif /* CSNHYPODBP_MWW_H */
