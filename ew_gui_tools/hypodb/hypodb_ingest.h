#ifndef HYPODB_INGEST_H
#define HYPODB_INGEST_H

#include <stddef.h>

#include "csnhypodbp_group.h"
#include "ewgui/db.h"
#include "hypodb_config.h"

/* Estado del ingester: grupos de evento, magnitudes pendientes y picks manuales. */
typedef struct HypodbState HypodbState;

HypodbState *hypodb_state_new(const HypodbConfig *cfg);
void         hypodb_state_free(HypodbState *st);

/* Conecta la BD al estado (opcional; sin ella el estado solo agrupa). */
void hypodb_state_set_db(HypodbState *st, EwGuiDb *db);

/*
 * Núcleo PURO: decide el event_id de una solución (crea grupo si hace falta).
 * No toca la BD. Devuelve 0 si OK.
 */
int hypodb_assign_event(HypodbState *st, const CsnhypodbpSolution *sol,
                        char *event_id_out, size_t n);

/* Procesa un TYPE_HYP2000ARC (mod = 162/163/164) y lo escribe en la BD. */
int hypodb_feed_arc(HypodbState *st, const char *msg, size_t len, int mod);

/* Procesa un TYPE_MAGNITUDE (resumen + líneas por estación). */
int hypodb_feed_mag(HypodbState *st, const char *msg, size_t len);

/* Registra un pick de PICK_RING (solo para marcar pick_status manual). */
int hypodb_feed_pick(HypodbState *st, const char *msg, size_t len);

/* Reconstruye grupos desde la BD (eventos recientes). Llamar al arrancar. */
int hypodb_load_from_db(HypodbState *st);

#endif /* HYPODB_INGEST_H */
