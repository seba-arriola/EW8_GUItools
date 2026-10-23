#ifndef CRV_CAPI_H
#define CRV_CAPI_H

/* csnrvQT - frontera UNICA entre la capa C (c/) y la capa C++/Qt (ui/).
 *
 * Reglas (heredadas de ew_controllerQT / csnhypodbpQT, docs/testLibQT.md):
 *   R1. Ningun TU C++ incluye headers de EarthWorm/libpq/cairo/GTK: solo este
 *       header (y los de ewguiqt/).
 *   R2. Todo lo declarado aqui va en `extern "C"`.
 *   R3. Los handles son opacos y los libera el llamante (new/free).
 *   R4. Este header no incluye nada con Qt ni cairo: se compila igual desde C.
 *
 * Hilos: la capa C es MONOHILO. La UI la conduce desde su QTimer, sin locks.
 *
 * `CrvConfig` se PRESTA a core/map: debe vivir mas que ellos (main.cxx la
 * libera despues de app.exec()).
 */

#include <stddef.h>
#include <stdio.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CRV_MAX_STR      512
#define CRV_MAX_OVERLAYS 16

/* ------------------------------------------------------------------------- */
/* Configuracion (.d)                                                        */
/* ------------------------------------------------------------------------- */

typedef struct {
    char my_module[64];                 /* MyModuleId: MOD_CSNRV (csnrv.d) */
    char ring[64];                      /* RingName: HYPO_RING_REF */
    int  heart_beat_int;
    int  log_file;
    char db_env_file[CRV_MAX_STR];      /* DbEnvFile (opcional) */
    int  db_hours;                      /* DbHistoryHours, def. 24 */
    char map_image[CRV_MAX_STR];        /* MapImageFile */
    double initial_zoom;                /* def. 8.0, clamp 0.2..50.0 */
    char overlay_files[CRV_MAX_OVERLAYS][CRV_MAX_STR];
    int  n_overlay_files;
    struct { char layer[64]; char hex[8]; } overlay_colors[CRV_MAX_OVERLAYS];
    int  n_overlay_colors;
    double overlay_width;               /* 0 = sin override */
    char overlay_hides[CRV_MAX_OVERLAYS][64];
    int  n_overlay_hides;
} CrvConfig;

/* Carga <path>. Devuelve NULL y el motivo en `err` si falta una obligatoria
 * (MyModuleId/RingName/HeartBeatInt/LogFile/MapImageFile) o hay error de
 * parseo. Los avisos de clamp van a stderr, como en el GTK. */
CrvConfig *crv_config_load(const char *path, char *err, size_t errlen);
void       crv_config_free(CrvConfig *cfg);

/* Bloque EXACTO de `csnrv --print-config` (lo compara el arnes). */
void crv_print_config(const CrvConfig *cfg, FILE *out);

/* Carga los overlays del .d y vuelca el reporte de `--check-overlays`.
 * Devuelve 0 siempre que la config sea valida (una capa inexistente solo
 * avisa por `err`). */
int crv_check_overlays(const CrvConfig *cfg, FILE *out, FILE *err);

/* ------------------------------------------------------------------------- */
/* ARC (TYPE_HYP2000ARC) y magnitudes (TYPE_MAGNITUDE)                       */
/* ------------------------------------------------------------------------- */

typedef struct {
    double t0;          /* epoch UTC, con centesimas */
    double lat, lon;    /* grados; S/W negativos */
    double depth_km;
    int    qid;
    int    version;     /* 178..181; 0 => caracter 161 */
} CrvArc;

/* -1 si el mensaje es demasiado corto o no parsea. No toca `msg`. */
int crv_arc_parse(const char *msg, size_t len, CrvArc *out);

typedef struct {
    int    qid;
    double mag;
    int    nstations;
    char   type[16];    /* ML | Mb | MB | Ms | MS | Mwp */
} CrvMagMsg;

/* Copia defensiva antes de `rd_mag` (rw_mag.c parsea in-place y lee 1 byte
 * mas alla del mensaje; el buffer del anillo NO viene terminado en NUL). */
int crv_magmsg_parse(const char *msg, size_t len, CrvMagMsg *out);

typedef struct {
    int    qid;
    double ml, mwp, mb, ms;
    int    n_ml, n_mwp, n_mb, n_ms;
} CrvMagState;

void crv_mag_reset(CrvMagState *st, int qid);
/* 1 si aplico el tipo (y el qid coincide); 0 si lo ignoro. */
int  crv_mag_apply(CrvMagState *st, const CrvMagMsg *m);
/* Regla EXACTA de csnrv: Mwp>=5.5&n>=3 > Ms(n>=3) > Mb(n>=3) > Ml > Mwp > "--". */
void crv_mag_preferred(const CrvMagState *st, double *mag, int *nst, const char **type);

/* `--test-mag` del GTK (C3..C9). Vive en la capa C para que ui/ no incluya
 * rw_mag.h; imprime ok/FAIL y devuelve el numero de fallos (0 = OK). */
int crv_selftest_mag(FILE *out);

/* ------------------------------------------------------------------------- */
/* Estado vivo                                                               */
/* ------------------------------------------------------------------------- */

typedef struct {
    int    has_data;
    int    has_depth;       /* 0 en el seed de BD (EwGuiMapEvent no trae depth) */
    double origin_time;     /* epoch UTC */
    double lat, lon, depth_km;
    int    qid, version;
    CrvMagState mag;
} CrvSnapshot;

enum {
    CRV_CHANGED_NONE    = 0,
    CRV_CHANGED_SUMMARY = 1 << 0,   /* hipocentro/etiquetas */
    CRV_CHANGED_MAG     = 1 << 1,
    CRV_CHANGED_EVENT   = 1 << 2,   /* qid nuevo: reset de vista */
    CRV_CHANGED_QUIT    = 1 << 3
};

typedef struct CrvCore CrvCore;

CrvCore *crv_core_new(const CrvConfig *cfg);
void     crv_core_free(CrvCore *c);

/* Abre la BD (si hay DbEnvFile) y hace el seed del historial. Un fallo de BD
 * NO es fatal: queda el motivo en crv_core_db_error() y se sigue con el
 * anillo. Devuelve 0 si hay BD o si no estaba configurada. */
int crv_core_open_db(CrvCore *c);
int crv_core_seed_db(CrvCore *c);

/* Resuelve el anillo y los tipos; -1 si no existe o el modulo no esta en
 * earthworm.d. */
int crv_core_attach_ring(CrvCore *c);

/* Drena el anillo, late y comprueba la senal de parada. Devuelve flags
 * CRV_CHANGED_*. */
int crv_core_tick(CrvCore *c, time_t now);

/* Ingesta pura (tests): mismo camino que los mensajes del anillo. */
int crv_core_ingest_arc(CrvCore *c, const char *msg, size_t len);
int crv_core_ingest_mag(CrvCore *c, const char *msg, size_t len);

void crv_core_snapshot(const CrvCore *c, CrvSnapshot *out);
const char *crv_core_db_error(const CrvCore *c);
int crv_core_should_quit(const CrvCore *c);

/* ------------------------------------------------------------------------- */
/* Formatos (paridad con el GTK; los tests los fijan)                        */
/* ------------------------------------------------------------------------- */

void crv_fmt_origin(double t0, char *out, size_t n);            /* HH:MM:SS UTC\nDD/MM/YYYY */
void crv_fmt_coords(double lat, double lon, char *out, size_t n); /* Lat: %.3f  Lon: %.3f */
void crv_fmt_depth(double depth_km, char *out, size_t n);        /* Depth: %.0f km */
void crv_fmt_elapsed(time_t now, double t0, int has_data, char *out, size_t n);
double crv_iso_epoch(const char *s);   /* "YYYY-MM-DD HH:MM:SS[.frac]" -> epoch */

/* ------------------------------------------------------------------------- */
/* Mapa (cairo vive SOLO en c/)                                              */
/* ------------------------------------------------------------------------- */

typedef struct {
    double zoom, pan_x, pan_y, initial_zoom;
} CrvMapView;

typedef struct {
    char     name[64];
    int      visible;
    unsigned rgb;          /* 0xRRGGBB */
    int      polylines;
    int      vertices;
} CrvLayerInfo;

typedef enum { CRV_FMT_PNG = 0, CRV_FMT_SVG, CRV_FMT_PDF } CrvFmt;

typedef struct CrvMap CrvMap;

CrvMap *crv_map_new(const CrvConfig *cfg);
void    crv_map_free(CrvMap *m);

/* El fondo lo decodifica la UI (QImage) y lo entrega como BGRA/ARGB32; la
 * capa C no arrastra libjpeg. `crv_map_set_background` COPIA el buffer. */
void crv_map_set_background(CrvMap *m, const unsigned char *bgra, int w, int h, int stride);
int  crv_map_has_background(const CrvMap *m);

void crv_map_reload(CrvMap *m);
int  crv_map_nlayers(const CrvMap *m);
int  crv_map_layer_info(const CrvMap *m, int i, CrvLayerInfo *out);
int  crv_map_set_visible(CrvMap *m, int i, int visible);

void crv_map_view_init(CrvMapView *v, double initial_zoom);
void crv_map_reset(CrvMapView *v);
void crv_map_zoom_factor(CrvMapView *v, double factor);

/* Zoom ANCLADO: deja fijo el punto del mundo que cae bajo (mx,my), dados en
 * pixeles FISICOS del canvas. Con z' = clamp(z*factor, 0.2, 50):
 *
 *     pan' = m - c - (m - c - pan) * (z'/z),   c = (w/2, h/2)
 *
 * El ajuste usa el factor APLICADO (tras el clamp), no el pedido: al topar en
 * 0.2/50 el pan no se mueve. `factor <= 0`, no finito, o `z <= 0` es no-op;
 * `w <= 0 || h <= 0` aplica el zoom sin reanclar (equivale al zoom al centro). */
void crv_map_zoom_at(CrvMapView *v, int w, int h,
                     double mx, double my, double factor);

void crv_map_pan(CrvMapView *v, double dx, double dy);

/* Transformacion (lat,lon)->pantalla con el estado dado; para tests.
 * `w`/`h` son el tamano del canvas en pixeles fisicos. */
void crv_map_project(const CrvMap *m, const CrvMapView *v, const CrvSnapshot *s,
                     int w, int h, double lat, double lon,
                     double *x, double *y);

/* Pinta en BGRA (el buffer de ewguiqt::CanvasWidget). */
void crv_map_paint_bgra(const CrvMap *m, const CrvMapView *v, const CrvSnapshot *s,
                        unsigned char *bgra, int w, int h, int stride);

/* PNG/SVG/PDF re-renderizando con cairo (no `grabImage`). 0 = OK. */
int crv_map_export(const CrvMap *m, const CrvMapView *v, const CrvSnapshot *s,
                   const char *path, CrvFmt fmt, int w, int h);

/* ------------------------------------------------------------------------- */
/* Log propio del modulo                                                     */
/* ------------------------------------------------------------------------- */

/* Inicializa `logit` sobre `$EW_LOG/csnrvQT_<YYYYMMDD>.log` con la clave
 * `LogFile` del .d. Devuelve el flag APLICADO (0 = solo consola). IDEMPOTENTE:
 * un segundo `logit_init` con logflag 0 re-`fclose`a el FILE* y aborta. */
int crv_log_init(int log_file);

void crv_logf(const char *fmt, ...)
#ifdef __GNUC__
    __attribute__((format(printf, 1, 2)))
#endif
    ;

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* CRV_CAPI_H */
