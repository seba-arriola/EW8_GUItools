/******************************************************************************
 * csnhypodbp_search.h                                                        *
 *                                                                            *
 * Nucleo PURO (sin GTK) del formulario de busqueda hipocentral: parseo y      *
 * validacion de los rangos min/max. Se testea headless.                      *
 *                                                                            *
 * Solo el rango de tiempo de origen es obligatorio; el resto de rangos son    *
 * opcionales. Un rango a un solo lado (solo min o solo max) se completa con   *
 * la cota natural del parametro.                                             *
 ******************************************************************************/
#ifndef CSNHYPODBP_SEARCH_H
#define CSNHYPODBP_SEARCH_H

#include <stddef.h>
#include <time.h>

/* Orden de los campos de texto del dialogo. */
#define CSNHYPODBP_SEARCH_FIELDS 10
enum {
    CSNHYPODBP_SEARCH_TMIN = 0,
    CSNHYPODBP_SEARCH_TMAX,
    CSNHYPODBP_SEARCH_LATMIN,
    CSNHYPODBP_SEARCH_LATMAX,
    CSNHYPODBP_SEARCH_LONMIN,
    CSNHYPODBP_SEARCH_LONMAX,
    CSNHYPODBP_SEARCH_DEPMIN,
    CSNHYPODBP_SEARCH_DEPMAX,
    CSNHYPODBP_SEARCH_MAGMIN,
    CSNHYPODBP_SEARCH_MAGMAX
};

#define CSNHYPODBP_SEARCH_TIME_LEN 32
#define CSNHYPODBP_SEARCH_DEFAULT_LIMIT 1000

typedef struct {
    char time_min[CSNHYPODBP_SEARCH_TIME_LEN];  /* ISO-8601 UTC "+00" */
    char time_max[CSNHYPODBP_SEARCH_TIME_LEN];
    int  have_lat;   double lat_min,   lat_max;
    int  have_lon;   double lon_min,   lon_max;
    int  have_depth; double depth_min, depth_max;
    int  have_mag;   double mag_min,   mag_max;
    int  limit;
} CsnhypodbpSearch;

/*
 * Parsea/valida los 10 campos de texto del dialogo. `max_events` es el tope
 * de la config (SearchMaxEvents); si <= 0 se usa CSNHYPODBP_SEARCH_DEFAULT_LIMIT.
 * Devuelve 1 si OK (out listo) o 0 si error, dejando el mensaje en `err`.
 *
 * Reglas:
 *   - fecha min y max obligatorias, formato "YYYY-MM-DD" o
 *     "YYYY-MM-DD HH:MM:SS" (UTC);
 *   - cada rango opcional presente exige min <= max;
 *   - un rango a un solo lado se completa con la cota natural.
 */
/* Fecha y hora UTC en "YYYY-MM-DD HH:MM:SS" (lo que muestran las tablas; al ser
 * ISO ordenan solas). Con `t` invalido escribe "--". */
void csnhypodbp_search_fmt_utc(time_t t, char *out, size_t n);

/* Escribe la fecha elegida en el campo, conservando la hora si ya la tenia (y
 * poniendo 00:00:00 si no). Devuelve 1 si escribio. */
int csnhypodbp_search_set_date(char *entry, size_t n, int y, int m, int d);

int csnhypodbp_search_build(const char *const fields[CSNHYPODBP_SEARCH_FIELDS],
                            int max_events, CsnhypodbpSearch *out,
                            char *err, size_t errsz);

#endif /* CSNHYPODBP_SEARCH_H */
