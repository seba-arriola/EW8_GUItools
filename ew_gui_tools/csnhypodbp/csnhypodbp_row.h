#ifndef CSNHYPODBP_ROW_H
#define CSNHYPODBP_ROW_H

#include <glib-object.h>

/* Modelo de fila para GtkColumnView (GTK4). Sustituye la fila de GtkListStore.
 * `cols` son las 15 celdas visibles (ya formateadas); los escalares son para
 * la lógica (qid/mod identifican el evento; otime/lat/lon/depth la selección). */
#define CSNHYPODBP_TYPE_ROW (csnhypodbp_row_get_type())
G_DECLARE_FINAL_TYPE(CsnhypodbpRow, csnhypodbp_row, CSNHYPODBP, ROW, GObject)

CsnhypodbpRow *csnhypodbp_row_new(const char *c0, const char *c1, const char *c2,
                                  const char *c3, const char *c4, const char *c5,
                                  const char *c6, const char *c7, const char *c8,
                                  const char *c9, const char *c10, const char *c11,
                                  const char *c12, const char *c13, const char *c14,
                                  double otime, int qver, int qid,
                                  double lat, double lon, double depth, int mod);

const char *csnhypodbp_row_col(CsnhypodbpRow *r, int i);   /* celda visible i (0..14) */

#endif /* CSNHYPODBP_ROW_H */
