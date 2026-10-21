#ifndef HYPODB_IDS_H
#define HYPODB_IDS_H

#include <stddef.h>

/*
 * Composición de los IDs de la base hipocentral (VARCHAR(64)).
 *   event_id             E<qid>-<yyyymmddhhmmss>    (UTC; sin hora: E<qid>)
 *   origin_id            <event_id>:<mod>            (mod = 162/163/164)
 *                        <event_id>:<mod>:<version>   (confirmacion manual)
 *   magnitude_id         <origin_id>/mag/<TIPO>
 *   station_magnitude_id <magnitude_id>/<NSLCO>
 *   pick_id              <origin_id>/pick/<STA>.<FASE>
 *
 * El sufijo `:<version>` (1..9999) identifica una solucion CONFIRMADA a mano:
 * cada confirmacion es una fila `origin` propia e inmutable (ver
 * `ewgui_origin_id_build`/`ewgui_origin_id_parse` y docs/DATA-MAPPING.md).
 *
 * Todas devuelven 0 si OK, -1 si algún argumento es inválido o si el
 * resultado no cabe en `out` (garantiza <= 63 caracteres + NUL).
 *
 * La IMPLEMENTACIÓN es común (`ewgui/db_ids.h`) porque hay dos escritores de la
 * base: este ingester y la GUI `csnhypodbp` (al confirmar una solución). Estas
 * firmas se conservan como fachada del módulo para sus llamantes y tests.
 */

/* event_id = "E<qid>-<yyyymmddhhmmss>". El sufijo (hora origen UTC) hace único
 * el id aunque dos sesiones de csnloc compartan qid; `otime <= 0` -> "E<qid>". */
int hypodb_event_id(char *out, size_t n, const char *qid, double otime);
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
