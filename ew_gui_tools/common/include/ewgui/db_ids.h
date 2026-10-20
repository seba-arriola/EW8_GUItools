#ifndef EWGUI_DB_IDS_H
#define EWGUI_DB_IDS_H

#include <stddef.h>

/*
 * Identificadores de las filas de la base hipocentral y los metadatos de un
 * origen que NO vienen del ARC.
 *
 * Viven en `common` (no en `hypodb`) porque los usan DOS escritores: el ingester
 * `hypodb_ingest` y la GUI (`csnhypodbp`), que al confirmar una solución puede
 * crear el evento+origen si todavía no están en la base. El esquema está
 * documentado en docs/DATA-MAPPING.md:
 *
 *   event_id             E<qid>[-<yyyymmddhhmmss>]     (hora UTC)
 *   origin_id            <event_id>:<mod>              (fila viva del pipeline)
 *                        <event_id>:<mod>:<version>    (confirmacion manual)
 *   magnitude_id         <origin_id>/mag/<TIPO>
 *   station_magnitude_id <magnitude_id>/<NSLCO>
 *   pick_id              <origin_id>/pick/<STA>.<FASE>
 *
 * El sufijo `:<version>` (1..9999, el campo de 4 caracteres del ARC) marca una
 * solucion CONFIRMADA a mano: cada confirmacion es una fila `origin` propia e
 * inmutable, y `preferred_origin_id` apunta a la confirmada mas reciente. Las
 * filas sin sufijo son las que el pipeline sigue actualizando (csnloc/163/164).
 *
 * Todas devuelven 0 si OK y -1 si el id no cabe en `n` (nunca truncado en
 * silencio: los ids son claves primarias).
 */
int ewgui_event_id(char *out, size_t n, const char *qid, double otime);
int ewgui_origin_id(char *out, size_t n, const char *event_id, int mod);
/* version 0 -> "<event>:<mod>"; 1..9999 -> "<event>:<mod>:<version>" */
int ewgui_origin_id_build(char *out, size_t n, const char *event_id, int mod,
                          unsigned version);
/* rc 0 si el id es valido; confirmed=1 si trae los 3 campos. Los punteros de
 * salida pueden ser NULL. */
int ewgui_origin_id_parse(const char *id, char *event_id, size_t n,
                          int *mod, unsigned *version, int *confirmed);
int ewgui_magnitude_id(char *out, size_t n, const char *origin_id,
                       const char *type);
/* El mecanismo focal se guarda por origen + método (uno por método). */
int ewgui_tensor_id(char *out, size_t n, const char *origin_id,
                    const char *method);
int ewgui_station_magnitude_id(char *out, size_t n, const char *magnitude_id,
                               const char *channel);
int ewgui_pick_id(char *out, size_t n, const char *origin_id, const char *sta,
                  const char *phase);

/* "NET.STA.LOC.CHAN" (la location nula de EarthWorm, "--", va vacía: así la
 * escribe el SDS y así la espera el esquema). */
int ewgui_channel_nslco(char *out, size_t n, const char *net, const char *sta,
                        const char *loc, const char *chan);

/* Versión del framework, de $EW_VERSION (la exporta ew8_unix.sh). */
const char *ewgui_software(void);

/* Localizador real del `mod` del ARC (162 csnloc, 163 HYPOINVERSE, 164 NLLoc). */
const char *ewgui_mod_algorithm(int mod);

/* `origin.status`: REVIEWED si la solución se revisó/confirmó a mano. */
const char *ewgui_mod_status(int mod, int manual);

/* t (epoch UTC) -> "YYYY-MM-DD HH:MM:SS.mmm+00". */
void ewgui_epoch_to_iso(double t, char *out, size_t n);

#endif /* EWGUI_DB_IDS_H */
