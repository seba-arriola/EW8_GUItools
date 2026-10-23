#ifndef CRV_INTERNAL_H
#define CRV_INTERNAL_H

/* Contratos entre los .c de la capa C. NO se incluye desde ui/. */

#include <cairo.h>

#include "ewgui/db.h"
#include "ewgui/overlay.h"

#include "crv_capi.h"

/* --- Overlays (crv_config.c) --------------------------------------------- */

/* Construye las capas del .d y aplica OverlayColor/Width/Hide. NULL si no hay
 * OverlayFile (o sin memoria). Los avisos van a `err` (stderr en headless). */
EwOverlay *crv_overlays_load(const CrvConfig *cfg, FILE *err);

/* --- Base hipocentral (crv_db.c) ----------------------------------------- */

typedef struct {
    EwGuiDb *db;
    int      ok;
    char     error[256];   /* "" si todo bien */
} CrvDb;

int  crv_db_open(CrvDb *d, const char *envfile);
void crv_db_close(CrvDb *d);
/* Llena `snap` con la solucion mas reciente de la BD. 1 = puso datos. */
int  crv_db_seed(CrvDb *d, int hours, CrvSnapshot *snap);

/* --- Dibujo (crv_map.c) -------------------------------------------------- */

void crv_map_draw(cairo_t *cr, int width, int height,
                  const CrvMap *m, const CrvMapView *v, const CrvSnapshot *s);

#endif /* CRV_INTERNAL_H */
