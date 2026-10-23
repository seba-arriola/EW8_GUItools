#ifndef SEV_INTERNAL_H
#define SEV_INTERNAL_H

/* Contratos entre los .c de la capa C. NO se incluye desde ui/. */

#include <cairo.h>

#include "ewgui/db.h"
#include "ewgui/overlay.h"

#include "sev_capi.h"

/* --- Overlays (sev_config.c) --------------------------------------------- */

/* Construye las capas del .d y aplica OverlayColor/Width/Hide. NULL si no hay
 * OverlayFile (o sin memoria). Los avisos van a `err` (stderr en headless). */
EwOverlay *sev_overlays_load(const SevConfig *cfg, FILE *err);

/* --- Base hipocentral (sev_db.c) ----------------------------------------- */

typedef struct SevDb SevDb;

SevDb *sev_db_open(const SevConfig *cfg, char *err, size_t errlen);
void   sev_db_free(SevDb *db);

/* Re-consulta el historial. 0 si OK; -1 si error (y *n = 0). */
int    sev_db_refresh(SevDb *db, int hours,
                      SevEvent *out, size_t max, size_t *n);

/* --- Dibujo (sev_map.c) -------------------------------------------------- */

void sev_map_draw(cairo_t *cr, int width, int height,
                  const SevMap *m, const SevMapView *v,
                  const SevSnapshot *s);

#endif /* SEV_INTERNAL_H */
