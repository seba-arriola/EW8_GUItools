#ifndef HYPODB_IDS_H
#define HYPODB_IDS_H

#include <stddef.h>

/*
 * Composición de los IDs de la base hipocentral (VARCHAR(64)).
 *   event_id             E<qid>
 *   origin_id            <event_id>:<mod>            (mod = 162/163/164)
 *   magnitude_id         <origin_id>/mag/<TIPO>
 *   station_magnitude_id <magnitude_id>/<NSLCO>
 *   pick_id              <origin_id>/pick/<STA>.<FASE>
 *
 * Todas devuelven 0 si OK, -1 si algún argumento es inválido o si el
 * resultado no cabe en `out` (garantiza <= 63 caracteres + NUL).
 */

int hypodb_event_id(char *out, size_t n, const char *qid);
int hypodb_origin_id(char *out, size_t n, const char *event_id, int mod);
int hypodb_magnitude_id(char *out, size_t n, const char *origin_id,
                        const char *type);
int hypodb_station_magnitude_id(char *out, size_t n, const char *magnitude_id,
                                const char *channel);
int hypodb_pick_id(char *out, size_t n, const char *origin_id,
                   const char *sta, const char *phase);

/* NSLCO "NET.STA.LOC.CHAN"; loc "--" o vacío => "". */
int hypodb_channel_nslco(char *out, size_t n, const char *net,
                         const char *sta, const char *loc, const char *chan);

#endif /* HYPODB_IDS_H */
