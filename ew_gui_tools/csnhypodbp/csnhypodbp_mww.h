#ifndef CSNHYPODBP_MWW_H
#define CSNHYPODBP_MWW_H

#include <gtk/gtk.h>

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

#endif /* CSNHYPODBP_MWW_H */
