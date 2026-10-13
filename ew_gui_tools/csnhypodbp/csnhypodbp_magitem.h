#ifndef CSNHYPODBP_MAGITEM_H
#define CSNHYPODBP_MAGITEM_H

#include <glib-object.h>

#include "csnhypodbp_magrow.h"

/* Modelo de fila para la tabla de magnitudes (GtkColumnView): UNA fila por
 * ESTACION, con las celdas ya formateadas: `sta`, `dist` (km), `anchor` (la P
 * efectiva: "M"/"A"/"Teo"/"-"), `t0`/`t1` (la ventana vigente en segundos
 * respecto del ancla), `mag` ("5.25" o "-"), `res` ("+0.25" o "-") y `stat`
 * (M/A/-). `chan` es el canal que usa esa magnitud y `idx` el indice en la tabla
 * de filas (para volver a la estacion). `hasdata` dice si la estacion tiene
 * traza descargada: sin ella la fila no se puede forzar (casilla inerte).
 *
 * La casilla de la columna `station` marca si la estacion entra en el calculo
 * de la magnitud de red: su estado vive en `CsnhypodbpMagRow.used` y se relee en
 * cada bind (igual que las picadas de la tabla de estaciones). */
#define CSNHYPODBP_TYPE_MAG_ITEM (csnhypodbp_mag_item_get_type())
G_DECLARE_FINAL_TYPE(CsnhypodbpMagItem, csnhypodbp_mag_item, CSNHYPODBP, MAG_ITEM, GObject)

/* `idx` es la posicion de la fila en la tabla (para volver a la estacion). */
CsnhypodbpMagItem *csnhypodbp_mag_item_new(const CsnhypodbpMagRow *r, int idx);

int csnhypodbp_mag_item_idx(CsnhypodbpMagItem *it);   /* indice en la tabla de filas */

#endif /* CSNHYPODBP_MAGITEM_H */
