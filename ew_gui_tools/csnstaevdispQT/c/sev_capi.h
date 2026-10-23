#ifndef SEV_CAPI_H
#define SEV_CAPI_H

/* csnstaevdispQT - frontera UNICA entre la capa C (c/) y la capa C++/Qt (ui/).
 *
 * Reglas (heredadas de csnrvQT / csnhypodbpQT, docs/testLibQT.md):
 *   R1. Ningun TU C++ incluye headers de EarthWorm/libpq/cairo/GTK: solo este
 *       header (y los de ewguiqt/).
 *   R2. Todo lo declarado aqui va en `extern "C"`.
 *   R3. Los handles son opacos y los libera el llamante (new/free).
 *   R4. Este header no incluye nada con Qt ni cairo: se compila igual desde C.
 *
 * Hilos: la capa C es MONOHILO. La UI la conduce desde sus QTimer, sin locks.
 *
 * `SevConfig` se PRESTA a core/map: debe vivir mas que ellos (main.cxx la
 * libera despues de app.exec()).
 */

#include <stddef.h>
#include <stdio.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SEV_MAX_STR             512
#define SEV_MAX_OVERLAYS         16
#define SEV_MAX_STATIONS        512   /* MAX_ESTA de csnstaevdisp.c */
#define SEV_MAX_EVENTS         1000   /* MAX_QUAKES de csnstaevdisp.c */

/* ------------------------------------------------------------------------- */
/* Configuracion (.d)                                                        */
/* ------------------------------------------------------------------------- */

typedef struct {
    char my_module[64];                 /* MyModuleId: MOD_STAEVDISP */
    char ring[64];                      /* RingName: HYPO_RING */
    int  heart_beat_int;                /* HeartBeatInt */
    int  log_file;                      /* LogFile */
    char db_env_file[SEV_MAX_STR];      /* DbEnvFile (obligatoria) */
    int  db_hours;                      /* DbHistoryHours, def. 24 */
    char map_image[SEV_MAX_STR];        /* MapImageFile */
    char sta_file[SEV_MAX_STR];         /* StaFile */
    double station_size;                /* StationSize (obligatoria) */

    char overlay_files[SEV_MAX_OVERLAYS][SEV_MAX_STR];
    int  n_overlay_files;
    struct { char layer[64]; char hex[8]; } overlay_colors[SEV_MAX_OVERLAYS];
    int  n_overlay_colors;
    double overlay_width;               /* 0 = sin override */
    char overlay_hides[SEV_MAX_OVERLAYS][64];
    int  n_overlay_hides;
} SevConfig;

/* Carga <path>. Devuelve NULL y el motivo en `err` si falta una obligatoria
 * (MyModuleId/RingName/HeartBeatInt/LogFile/DbEnvFile/MapImageFile/StaFile/
 * StationSize) o hay error de parseo. Las claves desconocidas se ignoran en
 * silencio (paridad con csnstaevdisp.c:185). */
SevConfig *sev_config_load(const char *path, char *err, size_t errlen);
void       sev_config_free(SevConfig *cfg);

/* Diagnostico headless del port (el GTK no lo tiene). */
void sev_print_config(const SevConfig *cfg, FILE *out);

/* Carga los overlays del .d y vuelca el reporte EXACTO de `--check-overlays`
 * (csnstaevdisp.c:889-899). Devuelve 0 siempre que la config sea valida: una
 * capa inexistente solo avisa por `err`. */
int sev_check_overlays(const SevConfig *cfg, FILE *out, FILE *err);

/* ------------------------------------------------------------------------- */
/* Datos: estaciones y sismos                                                */
/* ------------------------------------------------------------------------- */

typedef struct {
    char   sta[16];
    double lat, lon, elev_m;
} SevStation;

typedef struct {
    double lat, lon, mag;
    int    qid;
} SevEvent;

typedef struct {
    SevStation sta[SEV_MAX_STATIONS]; size_t nsta;
    double     elev_min, elev_max;             /* fallback 0/1000 */
    SevEvent   ev[SEV_MAX_EVENTS];    size_t nev;
    int        db_ok;
} SevSnapshot;

/* Carga hasta `max` estaciones (Sta Net Chan Loc Lat Lon Elev Sens); ignora
 * vacias y `#`. Truncado silencioso. */
size_t sev_stations_load(const char *path, SevStation *arr, size_t max);

/* min/max de elevacion; si no hay estaciones o son iguales => 0/1000
 * (paridad csnstaevdisp.c:345-350). */
void sev_stations_range(const SevStation *arr, size_t n,
                        double *elev_min, double *elev_max);

/* Mapeo puro de filas de BD (EwGuiMapEvent) a marcadores: normaliza
 * lon > 180 a -180..180 y saca el qid de `E<qid>-<timestamp>`. */
size_t sev_events_from_rows(const void *rows, int n,
                            SevEvent *out, size_t max);

/* ------------------------------------------------------------------------- */
/* Core: BD + anillo + heartbeat                                             */
/* ------------------------------------------------------------------------- */

typedef struct SevCore SevCore;

SevCore    *sev_core_new(const SevConfig *cfg);   /* presta cfg */
void        sev_core_free(SevCore *c);

/* Un fallo de BD NO es fatal: queda el motivo en sev_core_db_error() y se
 * sigue con 0 sismos. 0 si hay BD o si no estaba configurada. */
int         sev_core_open_db(SevCore *c);

/* Resuelve el anillo (con fallback MOD_WILDCARD), los tipos y se engancha.
 * -1 si el anillo no existe o el modulo no esta en earthworm.d. */
int         sev_core_attach_ring(SevCore *c);

/* Re-consulta el historial (cada 2 s). 0 si OK; -1 si la BD fallo (deja la
 * lista vacia, paridad `NumQuakes=0` de csnstaevdisp.c:365-367). */
int         sev_core_refresh(SevCore *c);

/* Latido y senal de parada (cada 1 s). Devuelve 1 si hay que cerrar. */
int         sev_core_background(SevCore *c, double now);

void        sev_core_snapshot(const SevCore *c, SevSnapshot *out);
const char *sev_core_db_error(const SevCore *c);
int         sev_core_should_quit(const SevCore *c);

/* ------------------------------------------------------------------------- */
/* Mapa (cairo vive SOLO en c/)                                              */
/* ------------------------------------------------------------------------- */

typedef struct {
    double zoom, pan_x, pan_y;
} SevMapView;

typedef struct {
    char   name[64];
    int    visible;
    double rgb[3];          /* 0..1 */
    double width;
    int    polylines;
    int    vertices;
} SevLayerInfo;

typedef enum { SEV_FMT_PNG = 0, SEV_FMT_SVG, SEV_FMT_PDF } SevFmt;

typedef struct SevMap SevMap;

SevMap *sev_map_new(const SevConfig *cfg);
void    sev_map_free(SevMap *m);

/* El fondo lo decodifica la UI (QImage) y lo entrega como BGRA/ARGB32; la
 * capa C no arrastra libjpeg. `sev_map_set_background` COPIA el buffer. */
void sev_map_set_background(SevMap *m, const unsigned char *bgra,
                            int w, int h, int stride);
int  sev_map_has_background(const SevMap *m);

void sev_map_reload_overlays(SevMap *m);
int  sev_map_nlayers(const SevMap *m);
int  sev_map_layer_info(const SevMap *m, int i, SevLayerInfo *out);
int  sev_map_set_visible(SevMap *m, int i, int visible);

/* Cuantos sismos se dibujan: 1..1000, default 10 (g_max_events_display). */
void sev_map_set_max_events(SevMap *m, int n);
int  sev_map_max_events(const SevMap *m);

/* Vista inicial: zoom 2.0 y pan 0; centro geografico FIJO en (-30,-70). */
void sev_map_view_init(SevMapView *v);
void sev_map_reset(SevMapView *v);

/* Zoom ANCLADO al cursor. Con z' = clamp(z*factor, 0.2, 100):
 *
 *     pan' = m - c - (m - c - pan) * (z'/z),   c = (w/2, h/2)
 *
 * Usa el factor APLICADO (tras el clamp): contra el tope 0.2/100 el pan no se
 * mueve. `factor <= 0`, no finito o `z <= 0` es no-op; `w <= 0 || h <= 0`
 * aplica el zoom sin reanclar. */
void sev_map_zoom_at(SevMapView *v, int w, int h,
                     double mx, double my, double factor);

void sev_map_pan(SevMapView *v, double dx, double dy);

/* (lat,lon)->pantalla con el estado dado; `w`/`h` en pixeles FISICOS. */
void sev_map_project(const SevMap *m, const SevMapView *v, int w, int h,
                     double lat, double lon, double *x, double *y);

/* Pinta en BGRA (el buffer de ewguiqt::CanvasWidget). */
void sev_map_paint_bgra(const SevMap *m, const SevMapView *v,
                        const SevSnapshot *s, unsigned char *bgra,
                        int w, int h, int stride);

/* PNG/SVG/PDF re-renderizando con cairo (no `grabImage`). 0 = OK. */
int sev_map_export(const SevMap *m, const SevMapView *v, const SevSnapshot *s,
                   const char *path, SevFmt fmt, int w, int h);

/* ------------------------------------------------------------------------- */
/* Log propio del modulo                                                     */
/* ------------------------------------------------------------------------- */

/* Inicializa `logit` sobre `$EW_LOG/csnstaevdispQT_<YYYYMMDD>.log` con la clave
 * `LogFile` del .d. `config_path` se acepta por contrato pero el nombre del
 * fichero es el LITERAL del proceso (diag.sh log csnstaevdispQT). IDEMPOTENTE:
 * un segundo `logit_init` con logflag 0 re-`fclose`a el FILE* y aborta.
 * Devuelve el flag APLICADO (0 = solo consola). */
int sev_log_init(const char *config_path, int log_file);

void sev_logf(const char *fmt, ...)
#ifdef __GNUC__
    __attribute__((format(printf, 1, 2)))
#endif
    ;

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SEV_CAPI_H */
