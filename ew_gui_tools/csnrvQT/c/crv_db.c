/* Seed del historial hipocentral (PostgreSQL), replica de
 * `load_history_from_db` (csnrv.c:861-898).
 *
 * `EwGuiMapEvent` NO trae profundidad: el seed deja `has_depth = 0` y la UI
 * mantiene la etiqueta de profundidad vacia hasta el primer ARC vivo (el GTK
 * tampoco pinta el seed; aqui se pinta el resto, que es la intencion del
 * comentario "pobla el resumen hasta que llegue un evento vivo").
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "crv_internal.h"

int crv_db_open(CrvDb *d, const char *envfile)
{
    EwGuiDbConfig dc;

    if (!d)
        return -1;
    memset(d, 0, sizeof(*d));
    if (!envfile || !*envfile)
        return 0;   /* sin BD configurada: solo anillo */

    if (ewguidb_config_load(envfile, &dc) != 0) {
        snprintf(d->error, sizeof(d->error), "no se pudo leer <%s>", envfile);
        return -1;
    }
    if (ewguidb_open(&dc, &d->db) != 0) {
        snprintf(d->error, sizeof(d->error), "%s", ewguidb_last_error(d->db));
        ewguidb_close(d->db);
        d->db = NULL;
        return -1;
    }
    d->ok = 1;
    return 1;
}

void crv_db_close(CrvDb *d)
{
    if (!d)
        return;
    if (d->db)
        ewguidb_close(d->db);
    d->db = NULL;
    d->ok = 0;
}

int crv_db_seed(CrvDb *d, int hours, CrvSnapshot *snap)
{
    EwGuiMapEvent *evs = NULL;
    EwGuiMagRow *mgs = NULL;
    int n = 0, nm = 0, i, qid;

    if (!d || !d->db || !snap)
        return 0;

    if (ewguidb_query_map_events(d->db, hours, &evs, &n) < 0 || n < 1) {
        ewguidb_rows_free(evs);
        return 0;
    }

    snap->lat = evs[0].latitude;
    snap->lon = evs[0].longitude;
    snap->depth_km = 0.0;
    snap->has_depth = 0;
    snap->origin_time = crv_iso_epoch(evs[0].origin_time);
    qid = atoi(evs[0].event_id[0] == 'E' ? evs[0].event_id + 1
                                        : evs[0].event_id);
    crv_mag_reset(&snap->mag, qid);

    if (ewguidb_query_magnitudes(d->db, evs[0].origin_id, &mgs, &nm) > 0) {
        for (i = 0; i < nm; i++) {
            double v = mgs[i].magnitude_value;
            int st = mgs[i].station_count;
            if (strcmp(mgs[i].magnitude_type, "ML") == 0) {
                snap->mag.ml = v;
                snap->mag.n_ml = st;
            } else if (strcmp(mgs[i].magnitude_type, "Mwp") == 0) {
                snap->mag.mwp = v;
                snap->mag.n_mwp = st;
            } else if (strcmp(mgs[i].magnitude_type, "Mb") == 0) {
                snap->mag.mb = v;
                snap->mag.n_mb = st;
            } else if (strcmp(mgs[i].magnitude_type, "Ms") == 0) {
                snap->mag.ms = v;
                snap->mag.n_ms = st;
            }
        }
    }
    ewguidb_rows_free(mgs);

    snap->has_data = 1;
    snap->qid = qid;
    snap->version = 0;
    ewguidb_rows_free(evs);
    return 1;
}
