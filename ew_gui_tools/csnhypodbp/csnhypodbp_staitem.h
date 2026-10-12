#ifndef CSNHYPODBP_STAITEM_H
#define CSNHYPODBP_STAITEM_H

#include <glib-object.h>

#include "csnhypodbp_statable.h"

/* Modelo de fila para la tabla de estaciones (GtkColumnView): UNA fila por
 * (estacion, fase) = 2 por estacion, y las DOS son autonomas: ambas muestran
 * estacion, distancia y su casilla. `stat` = M/A/Teo y `res` = residual
 * formateado (o "" si NAN). `has-pick` es la sensibilidad de la casilla.
 *
 * La casilla va JUNTO AL NOMBRE de la estacion y es la del pick de esa fila:
 * como cada fila es una fase suelta, desmarcarla desactiva esa fase y no la
 * estacion. Su estado vive en g_pick_on[] y se relee en cada bind. */
#define CSNHYPODBP_TYPE_STA_ITEM (csnhypodbp_sta_item_get_type())
G_DECLARE_FINAL_TYPE(CsnhypodbpStaItem, csnhypodbp_sta_item, CSNHYPODBP, STA_ITEM, GObject)

/* `phase`: 0 = P, 1 = S. */
CsnhypodbpStaItem *csnhypodbp_sta_item_new(const CsnhypodbpStaRow *r, int phase);

int csnhypodbp_sta_item_idx(CsnhypodbpStaItem *it);    /* indice en StaArray */
int csnhypodbp_sta_item_phase(CsnhypodbpStaItem *it);  /* 0 = P, 1 = S */

#endif /* CSNHYPODBP_STAITEM_H */
