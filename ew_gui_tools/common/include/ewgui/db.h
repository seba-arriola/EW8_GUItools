#ifndef EWGUI_DB_H
#define EWGUI_DB_H

#include <stddef.h>

/*
 * Acceso a PostgreSQL (información hipocentral), sin GTK.
 * Usa libpq y el esquema de `db/schema.sql` (event/origin/pick/magnitude/
 * station_magnitude/tensor).
 *
 * Credenciales: archivo tipo .env con DB_HOST/DB_PORT/DB_NAME/DB_USER/
 * DB_PASSWORD; las variables de entorno tienen precedencia (ver
 * ewgui_config_load). Las fechas se manejan como texto ISO-8601 en UTC
 * ("YYYY-MM-DD HH:MM:SS.sss+00").
 */

/* --- Configuración -------------------------------------------------------- */

typedef struct {
    char host[64];
    char port[16];
    char name[64];
    char user[64];
    char password[128];
} EwGuiDbConfig;

/*
 * Carga credenciales desde `envfile` (líneas KEY=VALUE; `#` comentarios;
 * comillas simples/dobles opcionales). Para cada clave, una variable de
 * entorno con el mismo nombre tiene precedencia.
 * Devuelve 0 si OK; -1 si falta alguna clave obligatoria o hay error.
 */
int ewguidb_config_load(const char *envfile, EwGuiDbConfig *out);

/* --- Conexión ------------------------------------------------------------- */

typedef struct EwGuiDb EwGuiDb;

/* Abre la conexión. Devuelve 0 si OK; -1 (error en ewguidb_last_error). */
int  ewguidb_open(const EwGuiDbConfig *cfg, EwGuiDb **out);
void ewguidb_close(EwGuiDb *db);

/* Último error (de conexión o de la última sentencia). Nunca NULL. */
const char *ewguidb_last_error(const EwGuiDb *db);

/* Ejecuta SQL sin parámetros (uso administrativo/tests). 0 si OK, -1 si error. */
int ewguidb_exec(EwGuiDb *db, const char *sql);

/* --- Filas ---------------------------------------------------------------- */

typedef struct {
    char event_id[64];
    char event_type[32];
    int  felt;
    int  published;
    char preferred_origin_id[64];
    char preferred_magnitude_id[64];
} EwGuiEventRow;

typedef struct {
    char   origin_id[64];
    char   event_id[64];
    char   origin_time[40];      /* ISO-8601 UTC */
    double latitude;
    double longitude;
    double depth;
    char   author[64];
    char   software[64];
    char   location_algorithm[64];
    char   velocity_model[64];
    double azimuthal_gap;
    int    station_count;
    double standard_error;
    char   status[32];
    char   creation_time[40];    /* ISO-8601 UTC: cuando se creo la solucion en la BD */
} EwGuiOriginRow;

typedef struct {
    char   pick_id[64];
    char   origin_id[64];
    char   channel[64];
    char   pick_time[40];        /* ISO-8601 UTC */
    char   phase[16];
    double weight;
    char   pick_status[16];
    double residual;             /* columna `uncertainty`: residual del localizador */
} EwGuiPickRow;

typedef struct {
    char   magnitude_id[64];
    char   origin_id[64];
    double magnitude_value;
    char   magnitude_type[16];
    double mag_uncertainty;
    char   calculation_method[64];
    int    station_count;
} EwGuiMagRow;

typedef struct {
    char   station_magnitude_id[64];
    char   magnitude_id[64];
    char   channel[64];
    double read_amplitude;
    double calculated_magnitude;
    char   amplitude_type[16];
} EwGuiStaMagRow;

/* --- Escritura ------------------------------------------------------------ */

/* INSERT ... ON CONFLICT (PK) DO NOTHING. 0 si OK, -1 si error. */
int ewguidb_upsert_event(EwGuiDb *db, const EwGuiEventRow *row);
int ewguidb_upsert_origin(EwGuiDb *db, const EwGuiOriginRow *row);
int ewguidb_upsert_magnitude(EwGuiDb *db, const EwGuiMagRow *row);

/* Reemplaza todas las filas hijas de `parent_id` (DELETE + INSERT). */
int ewguidb_replace_picks(EwGuiDb *db, const char *origin_id,
                          const EwGuiPickRow *rows, int n);
int ewguidb_replace_station_magnitudes(EwGuiDb *db, const char *magnitude_id,
                                       const EwGuiStaMagRow *rows, int n);

/* UPDATE de las soluciones preferidas de un evento. */
int ewguidb_set_preferred(EwGuiDb *db, const char *event_id,
                          const char *origin_id, const char *magnitude_id);

/* --- Lectura -------------------------------------------------------------- */

/*
 * Consultas: reservan un arreglo con malloc (liberar con ewguidb_rows_free).
 * Devuelven el número de filas (>=0) o -1 si error. `*out` queda NULL si n==0.
 */
int ewguidb_query_recent_events(EwGuiDb *db, int hours,
                                EwGuiEventRow **out, int *n);

/* Vista para mapas: evento con su origen y magnitud preferidos (join). */
typedef struct {
    char   event_id[64];
    char   origin_id[64];
    char   origin_time[40];
    double latitude;
    double longitude;
    double depth;
    double magnitude;
    char   magnitude_type[16];
} EwGuiMapEvent;

int ewguidb_query_map_events(EwGuiDb *db, int hours,
                             EwGuiMapEvent **out, int *n);

/*
 * Búsqueda hipocentral por rangos. `time_min`/`time_max` (ISO-8601 UTC) son
 * obligatorios; el resto de rangos son opcionales y se activan con `have_*`.
 * El filtro se aplica sobre el origen/magnitud PREFERIDOS del evento; un
 * evento sin magnitud preferida no entra en un rango de magnitud.
 * `limit <= 0` usa el default interno (1000). Devuelve filas o -1.
 */
typedef struct {
    const char *time_min;   /* ISO-8601 UTC, obligatorio */
    const char *time_max;   /* ISO-8601 UTC, obligatorio */
    int    have_lat;   double lat_min,   lat_max;
    int    have_lon;   double lon_min,   lon_max;
    int    have_depth; double depth_min, depth_max;
    int    have_mag;   double mag_min,   mag_max;
    int    limit;
} EwGuiSearchFilter;

int ewguidb_search_events(EwGuiDb *db, const EwGuiSearchFilter *f,
                          EwGuiMapEvent **out, int *n);
int ewguidb_query_origins(EwGuiDb *db, const char *event_id,
                          EwGuiOriginRow **out, int *n);
int ewguidb_query_picks(EwGuiDb *db, const char *origin_id,
                        EwGuiPickRow **out, int *n);
int ewguidb_query_magnitudes(EwGuiDb *db, const char *origin_id,
                             EwGuiMagRow **out, int *n);
int ewguidb_query_station_magnitudes(EwGuiDb *db, const char *magnitude_id,
                                     EwGuiStaMagRow **out, int *n);

void ewguidb_rows_free(void *rows);

#endif /* EWGUI_DB_H */
