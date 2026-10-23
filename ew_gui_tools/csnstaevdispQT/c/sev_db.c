/* Historial de sismos desde PostgreSQL: port de `LoadQuakeHistory`
 * (csnstaevdisp.c:361-382).
 *
 * La consulta (`ewguidb_query_map_events`) ya llega ordenada
 * `origin_time DESC, event_id`, es decir el sismo mas nuevo primero; el mapa
 * pinta del ultimo al primero para dejar el mas nuevo encima.
 *
 * Paridad: un fallo de consulta deja la lista VACIA y no loguea (el GTK hace
 * `NumQuakes=0` y `return`).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ewgui/db.h"

#include "sev_capi.h"
#include "sev_internal.h"

struct SevDb {
    EwGuiDb *db;    /* NULL si no hay conexion */
};

size_t sev_events_from_rows(const void *rows_, int n,
                            SevEvent *out, size_t max)
{
    const EwGuiMapEvent *rows = (const EwGuiMapEvent *)rows_;
    size_t k = 0;
    int i;

    if (!rows || !out || n <= 0 || max == 0)
        return 0;

    for (i = 0; i < n && k < max; i++) {
        double lon = rows[i].longitude;

        if (lon > 180.0)
            lon -= 360.0;   /* -180..180 */

        out[k].lat = rows[i].latitude;
        out[k].lon = lon;
        out[k].mag = rows[i].magnitude;
        out[k].qid = atoi(rows[i].event_id[0] == 'E'
                              ? rows[i].event_id + 1
                              : rows[i].event_id);
        k++;
    }
    return k;
}

SevDb *sev_db_open(const SevConfig *cfg, char *err, size_t errlen)
{
    EwGuiDbConfig dbcfg;
    EwGuiDb *db = NULL;
    SevDb *d;

    if (err && errlen)
        err[0] = '\0';
    if (!cfg || !cfg->db_env_file[0]) {
        if (err)
            snprintf(err, errlen, "sin DbEnvFile");
        return NULL;
    }
    if (ewguidb_config_load(cfg->db_env_file, &dbcfg) != 0) {
        if (err)
            snprintf(err, errlen, "no se pudo leer %s", cfg->db_env_file);
        return NULL;
    }
    if (ewguidb_open(&dbcfg, &db) != 0) {
        if (err)
            snprintf(err, errlen, "%s", ewguidb_last_error(db));
        ewguidb_close(db);
        return NULL;
    }

    d = calloc(1, sizeof(*d));
    if (!d) {
        if (err)
            snprintf(err, errlen, "sin memoria");
        ewguidb_close(db);
        return NULL;
    }
    d->db = db;
    return d;
}

void sev_db_free(SevDb *db)
{
    if (!db)
        return;
    ewguidb_close(db->db);
    free(db);
}

int sev_db_refresh(SevDb *d, int hours,
                   SevEvent *out, size_t max, size_t *n)
{
    EwGuiMapEvent *evs = NULL;
    int cnt = 0;

    if (n)
        *n = 0;
    if (!d || !d->db || !out || max == 0)
        return -1;

    if (ewguidb_query_map_events(d->db, hours, &evs, &cnt) < 0)
        return -1;   /* paridad: la lista queda vacia */

    if (n)
        *n = sev_events_from_rows(evs, cnt, out, max);
    ewguidb_rows_free(evs);
    return 0;
}
