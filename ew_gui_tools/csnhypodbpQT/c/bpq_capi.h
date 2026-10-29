/*
 * bpq_capi.h - Puerta C -> C++ del prototipo csnhypodbpQT.
 *
 * REGLA (R1/R2 del plan docs/testLibQT.md §7.1): este es el UNICO header que un
 * translation unit C++ puede incluir del mundo C. Todo el header va dentro de
 * `extern "C"` y solo arrastra headers PUROS (sin GTK, sin glib, sin EarthWorm,
 * sin cairo). Ningun .cxx debe incluir earthworm.h/kom.h/transport.h/wstore.h
 * ni los csnhypodbp*.h que no esten aqui.
 *
 * Los tipos de datos reutilizados (CsnhypodbpSolution, CsnhypodbpStaRow,
 * CsnhypodbpMagRow, CsnhypodbpPreviewVals, CsnhypodbpProfiles, ...) son los del
 * modulo GTK: se compilan in situ y no se redefinen aqui.
 */
#ifndef BPQ_CAPI_H
#define BPQ_CAPI_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- lista blanca de headers puros de csnhypodbp/common (verificada sin GTK) ---
 *
 * TRAMPA C++ (documentada en docs/testLibQT.md §13): Qt define `slots` como macro
 * (qobjectdefs.h). csnhypodbp_group.h declara un campo llamado `slots`
 * (CsnhypodbpMagStore), que sin este renombrado no compila ni con g++ ni con moc.
 * Se renombra SOLO para C++ y se restaura el macro de Qt al terminar.
 */
#ifdef __cplusplus
#  undef slots
#  define slots bpq_slots_member
#endif
#include "csnhypodbp_group.h"     /* CsnhypodbpPick / CsnhypodbpSolution / parse / group */
#include "csnhypodbp_statable.h"  /* CsnhypodbpStaRow / build / locate_picks */
#include "csnhypodbp_trace.h"     /* CsnhypodbpColorSet / color_* / PickOrigin */
#include "csnhypodbp_traces.h"    /* traces_per_screen_norm */
#include "csnhypodbp_view.h"      /* CsnhypodbpView / ticks / parse_color */
#include "bpq_tt1d.h"             /* BpqTt1dSel: bandas de la P teorica */
#include "csnhypodbp_mapview.h"   /* CsnhypodbpMapPt / Frame / constants */
#include "csnhypodbp_profile.h"   /* CsnhypodbpProfiles / ProfileLine */
#include "csnhypodbp_magcfg.h"    /* CsnhypodbpMagCfg */
#include "csnhypodbp_magrow.h"    /* CsnhypodbpMagRow / build */
#include "csnhypodbp_filtercfg.h" /* CsnhypodbpFilterCfg */
#include "csnhypodbp_locator.h"   /* CsnhypodbpLocator */
#include "csnhypodbp_preview.h"   /* CsnhypodbpPreviewVals */
#include "csnhypodbp_search.h"    /* CsnhypodbpSearch */
#include "csnhypodbp_manual.h"    /* CsnhypodbpManualPick */
#include "csnhypodbp_locate.h"    /* CsnhypodbpTrace / MagOut / locate_run */
#ifdef __cplusplus
#  undef slots
#  define slots Q_SLOTS            /* restaurar el macro de Qt */
#endif

/* --- constantes que en csnhypodbp viven en el header GTK csnhypodbp.h ---
 * Se replican aqui con el MISMO valor (comprobado en bpq_config.c con static
 * assert) para no arrastrar <gtk/gtk.h> a la capa C++.
 *   BPQ_MAX_STR        <- csnhypodbp.h:51   MAX_STR
 *   BPQ_MAX_LOCATORS   <- csnhypodbp.h:96   MAX_LOCATORS
 *   BPQ_MAX_MODLABELS  <- csnhypodbp_main.c:269 MAX_MODLABELS
 *   BPQ_MAX_OVERLAY_FILES/NAME <- ewgui/overlay.h:21-22
 */
#define BPQ_MAX_STR           256
#define BPQ_MAX_LOCATORS        8
#define BPQ_MAX_MODLABELS      16
#define BPQ_MAX_OVERLAY_FILES  16
#define BPQ_MAX_OVERLAY_NAME   64

/* Etiqueta de modulo origen (legado; la etiqueta viva sale de bpq_mod_label). */
typedef struct {
    int  id;
    char label[24];
} BpqModLabel;

/* Resuelve un id a su simbolo de earthworm.d (GetModIdName en produccion,
 * stub en tests). NULL si no lo conoce. */
typedef char *(*BpqGetModIdName)(unsigned char mod);

/*
 * Configuracion completa del modulo: un campo por clave del .d, con los mismos
 * nombres de clave que run_working_v8/params/csnhypodbp.d. Es el port de las
 * globales de csnhypodbp_main.c:13-272 a un struct (la GUI GTK las usa como
 * variables globales; aqui se pasan por puntero).
 *
 * Los sub-structs pesados se reutilizan tal cual del nucleo puro:
 *   filtercfg (CsnhypodbpFilterCfg), magcfg (CsnhypodbpMagCfg),
 *   profiles (CsnhypodbpProfiles), colors_p/colors_s (CsnhypodbpColorSet),
 *   locators[] (CsnhypodbpLocator).
 */
typedef struct {
    /* identidad del modulo */
    char   my_mod_name[BPQ_MAX_STR];

    /* anillos (InRing/InRingRaw/InRingPick/OutRing/ConfRing) */
    char   in_ring[BPQ_MAX_STR];
    char   in_ring_raw[BPQ_MAX_STR];
    char   in_ring_pick[BPQ_MAX_STR];
    char   out_ring[BPQ_MAX_STR];
    char   conf_ring[BPQ_MAX_STR];
    int    heartbeat_s;              /* HeartBeatInt */
    int    logfile;                  /* LogFile */

    /* picking / locate / estado */
    char   tau_model[BPQ_MAX_STR];   /* TauModel */
    char   sta_file[BPQ_MAX_STR];    /* StaFile */
    char   state_file[BPQ_MAX_STR];  /* StateFile */
    char   manual_state_file[BPQ_MAX_STR];
    double pick_capture_window_s;    /* PickCaptureWindowSec */
    double max_dist_km;              /* MaxDistKm */
    double mag_fetch_s;              /* MagFetchSeconds */
    int    locate_keep_tmp;          /* LocateKeepTmp */
    int    locator_timeout_s;        /* LocatorTimeoutSec */
    int    prefetch_neighbors;       /* PrefetchNeighbors */
    int    wave_cache_mb;            /* WaveCacheMB (0 = sin cache) */
    int    theo_model;               /* TheoModel: 1 = capas hyp2000, 0 = iasp91 */
    double theo_max_delta_km;        /* TheoMaxDeltaKm (dominio regional) */
    int    fix_depth_on;             /* Fix (estado inicial de la GUI) */
    double fix_depth_km;             /* FixedDepthKm */

    /* agrupamiento */
    double group_pick_tol_s;         /* GroupPickTolSec */
    int    group_min_match_picks;    /* GroupMinMatchPicks */
    int    group_max_solutions;      /* GroupMaxSolutions */

    /* BD / magnitudes / Mww / ondas */
    char   db_env_file[BPQ_MAX_STR];
    char   mag_config_file[BPQ_MAX_STR];
    char   mww_config_file[BPQ_MAX_STR];
    char   mww_workdir[BPQ_MAX_STR];
    int    db_history_hours;         /* DbHistoryHours */
    char   store_root[512];          /* StoreRoot */
    int    fetch_threads;            /* FetchThreads */
    char   active_locator[32];       /* locator activo elegido en la GUI */

    /* vista de ondas (picking) */
    char   channels_order[128];      /* channelsOrder */
    char   components_order[64];     /* componentsOrder */
    int    traces_per_screen;        /* tracesPerScreen (1..3) */
    CsnhypodbpLocator locators[BPQ_MAX_LOCATORS];
    int    n_locators;
    /* GetModIdName: resuelve el nombre visible de CUALQUIER mod, no solo los
     * de la tabla de locators. NULL en tests = sin resolver. */
    BpqGetModIdName get_mod_name;

    /* busqueda hipocentral y SDS */
    int    search_max_events;        /* SearchMaxEvents */
    int    sds_enabled;              /* SdsEnabled */
    char   sds_root[512];            /* SdsRoot */
    int    sds_fallback_hours;       /* SdsFallbackHours */
    int    hide_sta_without_data;    /* HideStationsWithoutData */

    /* colores de las picadas (la traza es siempre negra) */
    CsnhypodbpColorSet colors_p;     /* pColor/pColorAuto/pColorTheo */
    CsnhypodbpColorSet colors_s;     /* sColor/sColorAuto/sColorTheo */

    /* mapa de planta (contrato csnrv) */
    char   map_image_file[BPQ_MAX_STR];
    double map_min_span_deg;         /* MapMinSpanDeg */
    double map_zoom_factor;          /* MapZoom */
    double map_split_frac;           /* MapSplit (0.1..0.9) */
    char   overlay_files[BPQ_MAX_OVERLAY_FILES][BPQ_MAX_STR];
    int    n_overlay_files;
    char   overlay_color_layer[BPQ_MAX_OVERLAY_FILES][BPQ_MAX_OVERLAY_NAME];
    char   overlay_color_hex[BPQ_MAX_OVERLAY_FILES][8];
    int    n_overlay_colors;
    double overlay_width;            /* OverlayWidth (0 = el del archivo) */
    char   overlay_hide_layer[BPQ_MAX_OVERLAY_FILES][BPQ_MAX_OVERLAY_NAME];
    int    n_overlay_hides;

    /* perfiles de profundidad */
    char   profile_data_dir[BPQ_MAX_STR];
    char   profile_file[BPQ_MAX_STR];
    int    profile_lon_col;          /* ProfileCols */
    int    profile_val_col;
    double profile_depth_min_km;     /* ProfileDepthMinKm */
    CsnhypodbpProfiles profiles;

    /* filtros de la vista de ondas (presets + atajos) */
    CsnhypodbpFilterCfg filtercfg;

    /* parametros de magnitud (leidos del .d de csnmags) */
    CsnhypodbpMagCfg magcfg;

    /* ModuleLabel <id> <nombre> */
    BpqModLabel modlabels[BPQ_MAX_MODLABELS];
    int    n_modlabels;

    /* --state-dir / BPQ_STATE_DIR: dir de runtime del prototipo (vacio = el
     * que diga el .d). Sobrescribe StateFile, ManualStateFile y MwwWorkDir. */
    char   state_dir[BPQ_MAX_STR];
} BpqConfig;

/* ---------------------------------------------------------------------------
 * Configuracion
 * ------------------------------------------------------------------------- */

/* Carga el .d. Devuelve NULL y escribe el motivo en `err` si falla.
 * `state_dir_or_null` (o NULL) sobrescribe StateFile/ManualStateFile/MwwWorkDir:
 *   <dir>/csnloc.events , <dir>/csnhypodbp_manual.events , <dir>/mww
 * Las claves obligatorias son las mismas que csnhypodbp_main.c:322-333
 * (MyModuleId, InRing, OutRing, HeartBeatInt, LogFile, StaFile, StateFile). */
/* Resuelve `l->mod` de cada locator (MyModuleId de su .d + GetModId de
 * earthworm.d). Llamar DESPUES de bpq_rings_open; devuelve cuantos fallaron. */
int        bpq_locators_resolve(BpqConfig *cfg);

/* Etiqueta UNIFORME de un mod: GetModIdName normalizado (MOD_CSNLOC -> csnloc).
 * Escribe en `buf` y lo devuelve; "" si earthworm.d no conoce el id. */
const char *bpq_mod_label(int mod, char *buf, int bufn);

BpqConfig *bpq_config_load(const char *path, const char *state_dir_or_null,
                           char *err, size_t errn);
void       bpq_config_free(BpqConfig *c);

/* Carga el modelo de tiempos de viaje (`TauModel`, default `iasp91`) que
 * alimenta la P teorica. 1 = cargado; 0 = sin tablas en el cwd (motivo en
 * `err`); nunca aborta. */
int        bpq_init_tt(const BpqConfig *c, char *err, size_t errn);

/* P teorica con el modelo de capas de hyp2000 (el `.crh` del `.hyp` que usa el
 * localizador), para no contradecir la solucion que se esta mostrando. 1 =
 * cargado; 0 = se queda IASP91 (motivo en `err`); nunca aborta.
 * `TheoMaxDeltaKm` acota el dominio: el modelo es plano, sin curvatura. */
int        bpq_tt_theo_init(const BpqConfig *c, char *err, size_t errn);

/* Seleccion de bandas por epicentro: UNA vez por solucion, fuera del bucle de
 * estaciones. 1 = lista para usarse. */
typedef struct {
    int        ready;
    BpqTt1dSel sel;
} BpqTtTheo;

int        bpq_tt_theo_prepare(BpqTtTheo *t, double ev_lat, double ev_lon,
                               double depth_km);
/* T de la P teorica (s) o NAN si la estacion queda fuera del dominio (el
 * llamador cae a `csnhypodbp_tt_p`). */
double     bpq_tt_theo_p(const BpqTtTheo *t, double dist_deg, double depth_km);
/* Nombre de la banda dominante de la seleccion (NULL si no aplica). */
const char *bpq_tt_theo_model(const BpqTtTheo *t);
/* TheoMaxDeltaKm efectivo (para el volcado). */
double     bpq_tt_theo_max_km(void);
/* T de la P teorica con la tabla IASP91 (NAN si no hay tablas). */
double     bpq_tt_iasp_p(double dist_deg, double depth_km);

/* Vuelca la config efectiva (port de maps_print_config). 0 = OK. */
int        bpq_config_print(const BpqConfig *c, FILE *out);

/* Valida los perfiles declarados (port de maps_check_profiles): 0 = todos OK,
 * 2 = hubo fallas, <0 = error de uso. */
int        bpq_config_check_profiles(const BpqConfig *c, FILE *out);

/* Helpers de la version del modulo / test de paridad. */
int        bpq_config_has_key(const BpqConfig *c, const char *key);
const char *bpq_config_state_dir(const BpqConfig *c);

/* ---------------------------------------------------------------------------
 * Lienzos (cairo vive en c/, nunca en ui/)
 * ------------------------------------------------------------------------- */

/* Identificadores estables: el orden NO debe cambiar (lo usan los tests). */
typedef enum {
    BPQ_SCENE_MAP_PLAN = 0,
    BPQ_SCENE_MAP_PROF,
    BPQ_SCENE_TRACES,
    BPQ_SCENE_TRACE_AXIS,
    BPQ_SCENE_MAG,
    BPQ_SCENE_MAG_AXIS,
    BPQ_SCENE_MWW_BALL
} BpqScene;

typedef enum { BPQ_FMT_SVG = 0, BPQ_FMT_PDF, BPQ_FMT_PNG } BpqFmt;

/* Contextos de dibujo: structs planos que la UI rellena y cede al lienzo.
 * Todo lo que necesitan es dato puro (pixeles, grados, epoch): ninguno lleva
 * tipos de backend, asi que ui/ los rellena sin conocer cairo. */
typedef struct {
    /* Mapa base equirectangular ya decodificado por Qt, en ARGB32
     * premultiplicado (mismo formato que el buffer de bpq_scene_paint).
     * NULL o bg_w/bg_h <= 0 => lienzo virtual (CSNHYPODBP_MAP_VIRTUAL_*). */
    const unsigned char *bg;
    int    bg_w, bg_h, bg_stride;
    double zoom, cx, cy, pan_x, pan_y;
    const CsnhypodbpMapPt *pts;     /* soluciones + preferida + candidata */
    int    npts;
    int    preferred;               /* indice en pts o -1 */
    int    selected;                /* elegida en Origins o -1 */
    int    active_profile;          /* perfil resaltado en planta, -1 = ninguno */
    int    show_grid;
    /* Pixeles de dispositivo por pixel logico (HiDPI). El lienzo recibe w/h en
     * pixeles de dispositivo, asi que trazos y textos se escalan con esto para
     * no quedar a media medida en pantallas 2x. <=0 => 1.0. */
    double dpr;
    const struct BpqMap *map;       /* overlays .lines (creado con bpq_map_new) */
} BpqMapCtx;

typedef struct BpqProfileData BpqProfileData;

typedef struct {
    const CsnhypodbpMapPt *pts;
    int    npts;
    int    preferred;
    int    selected;
    double depth_min_km;            /* ProfileDepthMinKm */
    double zoom, pan_x, pan_y;
    double dpr;                     /* igual que en BpqMapCtx (<=0 => 1.0) */
    const BpqProfileData *prof;     /* slab+topo ya cargados; NULL = vacio */
} BpqProfileCtx;

/* Overlays y perfiles (cairo queda en c/): la UI los crea una vez. */
typedef struct BpqMap BpqMap;
BpqMap    *bpq_map_new(const BpqConfig *cfg);
void       bpq_map_free(BpqMap *m);
int        bpq_map_nlayers(const BpqMap *m);

/* Carga slab<name>.tmp + topo<name>.tmp del perfil `index` del .d. */
BpqProfileData *bpq_profile_load(const BpqConfig *cfg, int index,
                                 char *err, size_t errn);
void       bpq_profile_free(BpqProfileData *p);
const char *bpq_profile_name(const BpqProfileData *p);
int        bpq_profile_line_n(const BpqProfileData *p);   /* slab, para tests */
/* Indice del perfil mas cercano (csnhypodbp_profile_nearest); -1 si no hay. */
int        bpq_profile_nearest(const BpqConfig *cfg, double lat, double lon);
int        bpq_profile_count(const BpqConfig *cfg);

/* Auto-encuadre del panel de planta: bbox de `pts` con el suelo MapMinSpanDeg y
 * el MapZoom del .d, centrado en la solucion preferida (paridad
 * csnhypodbp_mapfit_at + MapMinSpanDeg). 0 OK, -1 sin puntos. */
int        bpq_map_fit(const BpqConfig *cfg, const BpqMapCtx *c, int w, int h,
                       double margin, double *zoom, double *cx, double *cy);

/* Encuadre del mundo completo (arranque sin soluciones): deja a la vista el mapa
 * y la grilla con sus ejes, en vez de una esquina con zoom 1. */
void       bpq_map_fit_world(const BpqMapCtx *c, int w, int h, double margin,
                             double *zoom, double *cx, double *cy);

/* --- interaccion (zoom/paneo/clic) --------------------------------------- */

/* Arrastre del raton en pixeles de dispositivo: el contenido sigue al cursor. */
void bpq_map_pan(BpqMapCtx *c, double dx, double dy);

/* Zoom alrededor del cursor (`factor` >1 acerca): el punto bajo (mx,my) se queda
 * en el mismo pixel. El zoom se acota a 1e-4..1e4. */
void bpq_map_zoom_at(BpqMapCtx *c, int w, int h, double mx, double my, double factor);

/* Indice en `pts` del marcador mas cercano a (mx,my) a menos de `tol` px de
 * dispositivo; -1 si no hay ninguno. */
int  bpq_map_hit_test(const BpqMapCtx *c, int w, int h, double mx, double my,
                      double tol);

/* Lo mismo para el panel de perfil: el paneo llega en pixeles y se convierte a
 * unidades del marco (grados / profundidad). */
void bpq_profile_pan(BpqProfileCtx *c, int w, int h, double dx, double dy);
void bpq_profile_zoom_at(BpqProfileCtx *c, int w, int h, double mx, double my,
                         double factor);

/* Proyecta (lon, profundidad) al pixel del panel: lo que usan el clic sobre un
 * pick y los tests para verificar el encuadre. 0 OK, -1 area no dibujable. */
int  bpq_profile_project(const BpqProfileCtx *c, int w, int h,
                         double lon, double depth, double *sx, double *sy);

/* --- ondas (fase 3): store -> SDS ---------------------------------------- *
 *
 * Camino portado de csnhypodbp_ws.c (que NO es puro: incluye csnhypodbp.h con
 * GTK). Aqui se rehace el mismo recorrido -- wstore_get primero y
 * csnhypodbp_sds_fetch como respaldo, decidido por csnhypodbp_sds_prefer -- y se
 * guarda en un EwGuiTrace (ewgui/wave.h, puro), que es el contenedor que ya
 * trae finish/filtros. La traza queda OPACA para ui/: solo escalares. */

typedef struct BpqWave  BpqWave;
typedef struct BpqTrace BpqTrace;

BpqWave  *bpq_wave_open(const BpqConfig *cfg, char *err, size_t errn);
void      bpq_wave_close(BpqWave *w);

BpqTrace *bpq_wave_fetch(BpqWave *w, const BpqConfig *cfg,
                         const char *sta, const char *net, const char *chan,
                         const char *loc, double t0, double seconds,
                         char *err, size_t errn);

void      bpq_trace_free(BpqTrace *t);
long      bpq_trace_n(const BpqTrace *t);
double    bpq_trace_rate(const BpqTrace *t);
double    bpq_trace_t0(const BpqTrace *t);     /* epoch de la 1.a muestra */
int       bpq_trace_range(const BpqTrace *t, double *lo, double *hi);

/* Traza a partir de muestras int32 (decode y tests). NULL si no hay memoria. */
BpqTrace *bpq_trace_new(double t0, double rate, const int32_t *samples, long n);

/* Copia independiente (cache -> UI). 1 OK. La `filt` del clon nace vacia: el
 * llamador aplica su filtro de vista al recibirla. */
int       bpq_trace_clone(const BpqTrace *src, BpqTrace **out);

/* Memoria que ocupa la traza (struct + raw + filt), para el tope de la cache. */
size_t    bpq_trace_bytes(const BpqTrace *t);

/* Acceso directo al buffer crudo (para dibujar sin copiar). 1 OK. */
int       bpq_trace_data(const BpqTrace *t, const int32_t **samples, long *n,
                         double *t0, double *rate);

/* Filtro de VISTA: deja el resultado en el buffer filtrado (el crudo no se toca,
 * asi `locate` siempre manda la traza cruda). `type` 0 = solo demean. */
int       bpq_trace_filter(BpqTrace *t, int type, double f1, double f2,
                           int order, int zerophase);

/* Buffer filtrado para dibujar (NULL si la traza no se filtro). */
const int32_t *bpq_trace_filtered(const BpqTrace *t, long *n);

/* --- STA/LTA de la vista (curva CF + disparo estimado) -------------------- *
 * NO crea picks: es solo la curva y una marca estimada en `trigger_t`, que ya
 * lleva la correccion estatica (`corr_s`, en el .d suele ser -sta/2). */

typedef struct BpqStaLta BpqStaLta;

/* --- cache de trazas (LRU por bytes) ------------------------------------- *
 * La usa SOLO el hilo del worker: el mapa no es thread-safe. Los contadores
 * son atomicos para que `--dump` los lea desde el hilo de la UI. */
typedef struct BpqWaveCache BpqWaveCache;

typedef struct {
    long      hits, misses, inserts, evictions, toobig;
    long      entries;
    long long bytes;
} BpqWaveCacheStats;

/* max_bytes == 0 o max_entries <= 0 => NULL (cache deshabilitada). */
BpqWaveCache *bpq_wavecache_new(size_t max_bytes, int max_entries);
void          bpq_wavecache_free(BpqWaveCache *c);

/* Clave canonica SCNL + t0 (1 ms) + span. 0 si la peticion no es valida. */
int           bpq_wavecache_key(char *out, size_t n, const char *sta,
                                const char *net, const char *chan,
                                const char *loc, double t0, double span);

/* 1 = hit: devuelve una COPIA que el llamante libera con bpq_trace_free. */
int           bpq_wavecache_get(BpqWaveCache *c, const char *key, BpqTrace **out);
int           bpq_wavecache_put(BpqWaveCache *c, const char *key,
                                const BpqTrace *tr);
int           bpq_wavecache_has(const BpqWaveCache *c, const char *key);
void          bpq_wavecache_clear(BpqWaveCache *c);
void          bpq_wavecache_stats(const BpqWaveCache *c, BpqWaveCacheStats *st);

BpqStaLta   *bpq_stalta_new(const BpqTrace *t, double sta_s, double lta_s,
                            double on_thr, double off_thr, double corr_s);
void         bpq_stalta_free(BpqStaLta *s);
const double *bpq_stalta_cf(const BpqStaLta *s, long *n);
double       bpq_stalta_trigger_t(const BpqStaLta *s);   /* 0 = sin disparo */
double       bpq_stalta_peak(const BpqStaLta *s);

/* --- canvas de trazas (panel de picking) --------------------------------- */

typedef struct {
    const CsnhypodbpStaRow *rows;   /* una fila por CANAL (misma estacion) */
    int       n;
    BpqTrace *const *tr;            /* trazas alineadas con rows; NULL = sin datos */
    double    t0, window;           /* ventana de dibujo: epoch y segundos */
    int       selected;             /* fila resaltada, -1 = ninguna */
    int       show_p, show_s;       /* que fases se marcan */
    double    dpr;
    const CsnhypodbpColorSet *colors_p;   /* manual/auto/teo de la P */
    const CsnhypodbpColorSet *colors_s;   /* manual/auto/teo de la S */
    /* Vista: filtro aplicado a las trazas y STA/LTA por fila (opcionales). */
    int       use_filtered;
    const BpqStaLta *const *stalta;
    int       show_stalta;
    /* Ancla del zoom por dos clics (epoch; 0 = sin ancla): se dibuja como linea
     * discontinua para que el segundo clic no sea a ciegas. */
    double    zoom_anchor_t;
    /* Exageracion vertical (Shift+Up/Down de la GTK): multiplica la amplitud
     * dibujada; >1 recorta contra la banda. 1 = normal, <=0 => 1. */
    double    y_gain;
    /* tracesPerScreen (1..3): cuantas bandas caben en el canvas. El resto se
     * alcanza con `scroll_row`. <=0 => 1. */
    int       per_screen;
    int       scroll_row;
    /* Guia vertical del puntero: x en px LOGICOS (el dibujo la pasa a
     * dispositivo con dpr). guide_valid=0 la oculta. */
    double    guide_x;
    int       guide_valid;
    /* Etiqueta de canal por fila ("HHZ"...), opcional: la vista de picking
     * muestra las componentes de UNA estacion, no una fila por estacion. */
    const char *const *chan;
    /* Banda de arrastre sobre el eje de tiempo (px de dispositivo). La pinta
     * bpq_traces_draw_axis mientras el usuario mantiene pulsado el eje. */
    int       rubber_active;
    double    rubber_x0, rubber_x1;
} BpqTracesCtx;

/* Clic sobre el canvas de trazas: fila (indice en `rows`) e instante. 0 OK,
 * -1 fuera del area de dibujo. */
int  bpq_traces_hit(const BpqTracesCtx *c, int w, int h, double mx, double my,
                    int *row, double *t);

/* Decodifica un stream TRACE2 (lo que devuelve csnhypodbp_sds_fetch) sobre una
 * grilla que arranca en `t0`. Si `*rate_inout` <= 0 toma la del primer paquete.
 * Devuelve el numero de muestras usadas (ultimo indice + 1). */
long      bpq_trace2_decode(const char *buf, long len, double t0,
                            double *rate_inout, int32_t *out, long cap);

/* Diagnostico CLI: "<sta>,<net>,<chan>,<loc>,<t0>,<secs>" -> escribe en `out`
 * una linea con lo que devolvio la capa de ondas. 0 si trajo datos. */
int       bpq_wave_probe(BpqWave *w, const BpqConfig *cfg, const char *spec,
                         char *out, size_t outn);

/* --- catalogo de estaciones + tabla de picadas --------------------------- *
 *
 * LoadStationsFromFile() vive en csnhypodbp_ws.c (no puro) y llena globales
 * (StaArray/NumEstaciones), asi que aqui se relee StaFile en un catalogo propio.
 * El formato es el del archivo del repo:
 *
 *   <sta> <net> <chan> <loc> <lat> <lon> [elev] [gain]
 *
 * La distancia y la regla M > A > Teo las resuelve csnhypodbp_statable_build. */

#define BPQ_MAX_STA_ROWS 512

typedef struct BpqStations BpqStations;

BpqStations *bpq_stations_open(const BpqConfig *cfg, char *err, size_t errn);
void         bpq_stations_close(BpqStations *c);
int          bpq_stations_count(const BpqStations *c);

/* Una fila por estacion del catalogo, ordenadas por distancia. Las picadas
 * salen de `sol` (que trae las suyas en `picks`); sin solucion quedan teoricas.
 * Devuelve el numero de filas (<= max). */
int          bpq_stations_build(const BpqStations *c,
                                const CsnhypodbpSolution *sol,
                                CsnhypodbpStaRow *out, int max);

/* Igual, con `sol` para las picadas y `resid` como fuente de residuales: tras
 * un `locate` la solucion elegida manda las picadas y la CANDIDATA los
 * residuales (el `res` de la tabla es el del localizador, sin pulsar locate).
 * `man`/`nman` es el overlay de picadas manuales del picking: entra al ranking
 * como origen 'M' (una picada a mano desplaza a la automatica y a la teorica). */
int          bpq_stations_build_resid(const BpqStations *c,
                                      const CsnhypodbpSolution *sol,
                                      const CsnhypodbpSolution *resid,
                                      const CsnhypodbpManualPick *man, int nman,
                                      CsnhypodbpStaRow *out, int max);

/* Recalcula distancia y orden de una COPIA de las filas contra otro hipocentro
 * (la candidata del locate). La tabla visible no se toca. */
int          bpq_stations_rebase(const BpqStations *c,
                                 CsnhypodbpStaRow *rows, int n,
                                 double lat, double lon);

/* Filas del canvas de trazas: los CANALES de una estacion en el orden del .d
 * (channelsOrder x componentsOrder). La disponibilidad es por BANDA (los dos
 * primeros caracteres: si hay HHZ se asumen HHN/HHE, que es lo que hace la GTK
 * con csnhypodbp_station_has_band), asi que `chans[i]` es el codigo a pedir y
 * `row->idx` apunta al catalogo solo para net/loc. Las picadas P/S son las de
 * `base` para todas las componentes. Devuelve el numero de filas. */
int bpq_stations_chan_rows(const BpqStations *c, const char *sta,
                           const char *channels_order,
                           const char *components_order,
                           const CsnhypodbpStaRow *base,
                           CsnhypodbpStaRow *out, char chans[][8], int max);

/* --- overlay de picadas manuales (el clic sobre la traza) ----------------- *
 *
 * Igual que en la GTK: el clic NO publica al anillo. La picada queda en un
 * overlay local que se ve al instante en la traza y en la tabla (origen M), y
 * recien se publica a PICK_RING al pulsar `Confirm`. Asi el operador ajusta el
 * picking sin que `csnloc` asocie picadas a medio hacer. */
int  bpq_manual_set(const char *sta, const char *chan, char phase, double t);
int  bpq_manual_remove(const char *sta, char phase);   /* 1 si habia */
void bpq_manual_clear(void);
int  bpq_manual_count(void);
int  bpq_manual_list(CsnhypodbpManualPick *out, int max);
/* Persistencia del overlay (texto), POR EVENTO (`<qid>:<otime>`), no por
 * solucion: cambiar de Origin dentro del mismo evento no debe borrar las
 * picadas manuales. El formato viejo `<qid>:<mod>:<version>` se acepta si el
 * qid coincide (migracion); un archivo de otro evento se ignora entero. */
int  bpq_manual_key_matches(const char *file_key, const char *event_key);

/* Ruta del archivo de un evento: "<base>.<qid>_<otime>" (":" y "/" -> "_").
 * `base` es el archivo legado (p. ej. .../csnhypodbp_manual.events.picks). */
int  bpq_manual_path_for_event(const char *base, const char *event_key,
                               char *out, size_t n);

/* save: 0 = guardado; -2 = clave vacia (NO escribe); -1 = error de E/S. */
int  bpq_manual_save(const char *path, const char *event_key);

/* load: 1 = cargado (un archivo del evento vacio limpia la lista); 0 =
 * archivo AUSENTE; -1 = la clave es de otro evento (la lista queda INTACTA);
 * -2 = argumentos invalidos. Nunca limpia por una clave que no coincide: el
 * llamador limpia al cambiar de evento. */
int  bpq_manual_load(const char *path, const char *event_key);

/* --- ventana de las trazas con el raton ----------------------------------- *
 * `w` = ancho del canvas en pixeles de dispositivo. La ventana se acota a
 * 1..3600 s y el zoom conserva el instante que hay bajo el cursor (misma
 * geometria que `bpq_traces_hit`, para que el pico no se escape). */
void bpq_traces_zoom_at(BpqTracesCtx *c, int w, double mx, double factor);
void bpq_traces_pan(BpqTracesCtx *c, int w, double dx);

/* Zoom a un RANGO de pixeles (el gesto de dos clics): deja la ventana entre los
 * dos instantes. 1 = aplicado; 0 = rango menor a 0.5 s (no toca la ventana). */
int  bpq_traces_zoom_range(BpqTracesCtx *c, int w, double x0, double x1);

/* Instante (epoch) que hay bajo el pixel `mx`, con la geometria del dibujo.
 * 0 si no se puede calcular (asi 0 sirve de "sin ancla": los epochs son ~1.7e9). */
double bpq_traces_time_at(const BpqTracesCtx *c, int w, double mx);

/* Fin del arrastre sobre el eje de tiempo. x0/x1 en px de dispositivo y
 * def_t0/def_win = vista original del evento (Shift+0). Devuelve:
 *   BPQ_AXIS_ZOOM  = izq->der, ventana = intervalo marcado;
 *   BPQ_AXIS_RESET = der->izq, ventana = def_t0/def_win;
 *   BPQ_AXIS_NONE  = gesto demasiado corto o sin evento.
 * Siempre apaga `rubber_active`; en RESET limpia tambien el ancla de dos clics. */
#define BPQ_AXIS_NONE  0
#define BPQ_AXIS_ZOOM  1
#define BPQ_AXIS_RESET 2
int bpq_traces_axis_release(BpqTracesCtx *c, int w, double x0, double x1,
                            double def_t0, double def_win);

/* Margen izquierdo de la cuneta de etiquetas, en px de dispositivo. Lo usan el
 * dibujo, el hit-test, el zoom y el gesto del eje: NO puede haber dos valores. */
double bpq_traces_left_margin(double dpr);

/* Etiqueta de cada banda del picking: SOLO el canal ("HHZ"...). */
int bpq_traces_band_label(const BpqTracesCtx *c, int i, char *out, size_t n);

/* Zoom temporal CENTRADO (Shift+Left/Right de la GTK): `dir` < 0 contrae el
 * span /1.5, > 0 lo expande *1.5, 0 no toca. Se acota a 0.1..86400 s. */
void bpq_traces_zoom_span(double *t0, double *window, int dir);

/* Primera fila visible con `per_screen` + `scroll_row`, y cuantas caben. Lo usan
 * el dibujo, el hit-test y el desplazamiento con las flechas. */
int bpq_traces_visible(const BpqTracesCtx *c, int h, int *nvis_out);

/* Filas que caben de verdad en el viewport (misma cuenta que el dibujo). */
int bpq_traces_page_rows(const BpqTracesCtx *c, int h);

/* Ultima `first` valida: n - nvis (0 si todo cabe). */
int bpq_traces_scroll_max(const BpqTracesCtx *c, int h);

/* Fija `scroll_row` acotado a [0, scroll_max] y devuelve la fila aplicada. */
int bpq_traces_scroll_to(BpqTracesCtx *c, int h, int row);

/* Alto de banda en pixeles de dispositivo: (viewport - margen)/tracesPerScreen
 * con el suelo CSNHYPODBP_TRACES_BAND_MIN. */
int bpq_traces_band_h(const BpqTracesCtx *c, int h);

/* Paso de `tracesPerScreen` con Shift+rueda: normaliza 1..3, aplica `dir`
 * (+1 arriba, -1 abajo) y vuelve a acotar. */
int bpq_traces_per_screen_step(int current, int dir);

/* SCNL del catalogo para el `idx` de una fila (el que devuelve statable). 1 OK. */
int          bpq_stations_scnl(const BpqStations *c, int idx, const char **sta,
                               const char **net, const char **chan, const char **loc);

/* --- locate (fase 4): picadas efectivas -> csnloc + refinador ------------- *
 *
 * La cadena es la MISMA del modulo GTK (csnhypodbp_locate.c, pura): escribe el
 * ARC de picadas, corre csnloc, refina con hyp2000_ring/nlloc_ring y lee el
 * JSONL con el residual de cada fase. Nada de eso se reimplementa aqui. */

/* Resultado: la solucion refinada -- con `picks[].residual` ya puesto por el
 * localizador -- y las magnitudes que calculo csnmags. */
typedef struct {
    CsnhypodbpSolution sol;
    CsnhypodbpMagOut   mag;
    int                sent;   /* picadas construidas antes del refinador */
} BpqLocateOut;

/* Rellena un CsnhypodbpTrace con las muestras CRUDAS de la traza (el trace
 * es lo que consume csnmags y el refinador: nada de filtros de vista). */
int bpq_trace_copy(const BpqTrace *t, const char *sta, const char *net,
                   const char *chan, const char *loc,
                   CsnhypodbpTrace *out);

/* --- magnitudes (fase 6) -------------------------------------------------- *
 * La cadena es la del modulo: hypos.jsonl de magnitudes + trace + `csnmags --out`
 * (csnhypodbp_mag_run), con las ventanas y puertas de MagConfigFile. */
int bpq_mag_run(const BpqConfig *cfg, const CsnhypodbpSolution *base,
                const CsnhypodbpTrace *trace, int ntr,
                const CsnhypodbpMagOvrRow *ovr, int novr, unsigned kind_mask,
                CsnhypodbpMagOut *out, char *err, size_t errn);

/* ¿Alguna de las 4 trae valor? (una corrida sin magnitudes no es resultado) */
int bpq_mag_result_ok(const CsnhypodbpMagOut *mag);

/* Igual pero limitado a la mascara pedida (bit 0=ML..3=Ms; 0 = las cuatro). */
int bpq_mag_result_ok_mask(const CsnhypodbpMagOut *mag, unsigned kind_mask);

/* --- pestana magnitudes (fase 6.1) --------------------------------------- *
 * Estado de la sub-pestana: ventanas ajustadas a mano por (estacion, magnitud)
 * (el nucleo `csnhypodbp_magmarks`), la magnitud activa, el filtro A/M/Teo y el
 * override de la puerta de distancia. Las filas de la tabla salen de
 * `csnhypodbp_magrow_build` + las funciones de texto del mismo nucleo. */

#define BPQ_MAG_KINDS 4

typedef struct BpqMagPanel BpqMagPanel;

/* Una banda del canvas de magnitudes: la traza de UN canal de la estacion. */
#define BPQ_MAG_MAX_TRACES 16   /* = filas maximas de bpq_stations_chan_rows */

/* Tope de trazas que se bajan para el trace de magnitudes/locate. Una por
 * (estacion, canal): vertical para Mwp/Mb/Ms y horizontales para ML. */
#define BPQ_TRACE_MAX 192

typedef struct {
    const BpqTrace *tr;        /* NULL = sin datos para este canal */
    const char     *chan;      /* "HHZ", "HHN", ... */
    double          dist_km;
} BpqMagTrace;

/* Contexto del canvas de magnitudes: una banda por canal de la estacion (en el
 * orden del .d) y la franja de su ventana. Todo puro; cairo no aparece. */
typedef struct {
    const BpqMagTrace      *trs;     /* ntr bandas, en orden channels x comps */
    int                     ntr;
    const CsnhypodbpMagCfg *magcfg;  /* ventanas/bandas del .d de csnmags */
    int     kind;                    /* 0=ML 1=Mwp 2=Mb 3=Ms */
    double  anchor;                  /* epoch de la P: ancla de la ventana */
    double  t0, t1;                  /* ventana vigente, offsets desde el ancla (s) */
    double  view_t0, view_span;      /* zoom del canvas (0/0 = encuadre automatico) */
    double  dpr;
    double  y_gain;                  /* exageracion vertical (<=0 => 1) */
    const char *sta;                 /* rotulo (NULL = sin titulo) */
    int     touched;                 /* 1 = ventana ajustada a mano */
} BpqMagCtx;

double bpq_mag_left_margin(double dpr);
/* Etiqueta de cada banda de magnitudes: SOLO el canal ("HHZ"...). */
int    bpq_mag_band_label(const BpqMagCtx *c, int i, char *out, size_t n);

BpqMagPanel *bpq_magpanel_new(const BpqConfig *cfg);
void         bpq_magpanel_free(BpqMagPanel *p);

int  bpq_magpanel_kind(const BpqMagPanel *p);
void bpq_magpanel_set_kind(BpqMagPanel *p, int kind);
void bpq_magpanel_set_masks(BpqMagPanel *p, int auto_on, int manual_on, int theo_on);

/* Ventana por defecto de la magnitud (offsets respecto del ancla, del .d). */
void bpq_magpanel_defaults_window(const BpqMagPanel *p, int kind,
                                  double *t0, double *t1);
/* Ventana vigente de (sta, kind): la ajustada a mano si existe, si no la del .d. */
void bpq_magpanel_window(const BpqMagPanel *p, const char *sta, int kind,
                         double *t0, double *t1);
int  bpq_magpanel_set_window(BpqMagPanel *p, const char *sta, int kind,
                             double t0, double t1);
int  bpq_magpanel_touched(const BpqMagPanel *p, const char *sta, int kind);
int  bpq_magpanel_touched_count(const BpqMagPanel *p);

/* Filas de la tabla (una por estacion): valor de `vals` emparejado por nombre,
 * ancla = P efectiva, ventana vigente, puerta de distancia aplicada (con el
 * override) y elegibilidad segun el filtro A/M/Teo. */
int  bpq_magpanel_rows(const BpqMagPanel *p, const CsnhypodbpStaRow *stas, int n,
                       const CsnhypodbpMagIn *vals, int nval, double red,
                       CsnhypodbpMagRow *out, int max);

/* Filas que se piden al tanque para `calculate`/`locate`: P presente, origen
 * permitido y puerta (distancia + profundidad) de ALGUNA magnitud de
 * `kind_mask` (0 = las cuatro). Conserva el orden de `stas` (distancia) y
 * devuelve los indices de fila; el llamador las traduce a SCNL. */
int  bpq_magpanel_fetch_rows(const BpqMagPanel *p, const CsnhypodbpStaRow *stas,
                             int n, unsigned kind_mask, double depth_km,
                             int auto_on, int manual_on, int theo_on,
                             int *out_rows, int max);

/* Subconjunto para la TABLA: solo `in_range != 0` (fuera de la puerta de la
 * magnitud activa se ocultan; siguen en picking y en el canvas). Conserva el
 * orden y no reserva memoria. Devuelve las copiadas (<= max). */
int  bpq_magrows_filter_in_range(const CsnhypodbpMagRow *in, int n,
                                 CsnhypodbpMagRow *out, int max);

/* Overrides para `calculate`: ventanas ajustadas llevadas a t0/t1 ABSOLUTOS por
 * (ancla, magnitud), que es el formato de `WindowOverridesFile`. */
int  bpq_magpanel_ovr(const BpqMagPanel *p, const CsnhypodbpStaRow *stas, int n,
                      unsigned kind_mask, CsnhypodbpMagOvrRow *out, int max);

/* Ventana de traza que hay que pedir para (estacion, magnitud): la del .d
 * (csnhypodbp_magcfg_window) o la ajustada a mano en `ovr`, mas margen para
 * cubrir el recorte de csnmags. `kind` es 0=ML..3=Ms. 1 OK, 0 sin P efectiva. */
int  bpq_magpanel_req_window(const BpqMagPanel *p, const CsnhypodbpStaRow *row,
                             int kind, const CsnhypodbpMagOvrRow *ovr, int novr,
                             double *t0, double *seconds);
int  bpq_magpanel_ovr_write(const BpqMagPanel *p, const CsnhypodbpStaRow *stas,
                            int n, const char *path);

/* Solucion para `calculate`: copia `base` y reemplaza sus picadas por las P/S
 * efectivas permitidas por (auto, manual, theo). Devuelve el numero de
 * picadas; -1 si los argumentos no valen. */
int  bpq_magpanel_calc_solution(const CsnhypodbpStaRow *stas, int n,
                                int auto_on, int manual_on, int theo_on,
                                const CsnhypodbpSolution *base,
                                CsnhypodbpSolution *out);

/* Cajas de la puerta de distancia de la magnitud activa ("%.1f" o "" sin
 * override) y aplicacion de lo tecleado (rango de `csnhypodbp_mag_ov_parse`). */
void bpq_magpanel_gate_texts(const BpqMagPanel *p, int kind,
                             char *lo, size_t nlo, char *hi, size_t nhi);
int  bpq_magpanel_gate_set(BpqMagPanel *p, const char *lo_txt, const char *hi_txt);
void bpq_magpanel_gate_reset(BpqMagPanel *p);

/* Linea de estado de la barra ("N estaciones | K en uso | ..."). */
void bpq_magpanel_status(const CsnhypodbpMagRow *rows, int n, char *out, size_t errn);

/* EwGuiTrace CRUDO de una traza (lo piden los nucleos que trabajan sobre la
 * traza, p.ej. `csnhypodbp_magview_build`). NULL si no hay. */
void *bpq_trace_ew(const BpqTrace *t);

/* Clic sobre el canvas de magnitudes: 1 si agarra la franja (solo el handle de
 * arriba), con `grab` 1=t0 2=t1 3=ambos y el instante bajo el puntero. */
int  bpq_mag_hit(const BpqMagCtx *c, int w, int h, double mx, double my,
                 int *grab, double *t_at);

/* Zona de la franja bajo el puntero para el cursor del canvas de magnitudes:
 *   NONE   = fuera de la franja (o sin ancla)
 *   EDGE   = borde vertical (t0/t1): se redimensiona
 *   HANDLE = banda superior: se mueve la franja
 * `mx/my` en pixeles de dispositivo (la UI multiplica por dpr). */
enum {
    BPQ_MAG_HOVER_NONE   = 0,
    BPQ_MAG_HOVER_EDGE   = 1,
    BPQ_MAG_HOVER_HANDLE = 2
};
int  bpq_mag_hover(const BpqMagCtx *c, int w, int h, double mx, double my);

/* Instante (epoch) bajo `mx` en el canvas de magnitudes: lo usa el arrastre de
 * la franja, que recibe deltas de pixel y necesita la escala real del dibujo. */
double bpq_mag_time_at(const BpqMagCtx *c, int w, int h, double mx);

/* Margen (s) de contexto a cada lado de la ventana [t0,t1] (offsets desde el
 * ancla): el mismo que usa el encuadre automatico del canvas. */
double bpq_mag_margin(double t0, double t1);

/* --- panel `Mww` (fase 6.3): inversion de fase W on-demand ---------------- *
 * `csnwphase` vive DETRAS de esta fachada: ni su config ni su resultado se
 * exponen a ui/ (la unica frontera sigue siendo este header, sin GTK ni EW). */

typedef struct BpqDb      BpqDb;   /* historial (bpq_db.c) */
typedef struct BpqMww       BpqMww;
typedef struct BpqMwwResult  BpqMwwResult;

/* Fila de la tabla de parametros (ya formateada por csnwphase_params_build). */
typedef struct {
    char name[48];
    char value[96];
    char source[48];
    int  field;                 /* CSNWPHASE_PF_* ; 0 = solo lectura */
} BpqMwwParamRow;

/* Abre la config (`MwwConfigFile` de este .d, resuelta contra EW_PARAMS). */
BpqMww *bpq_mww_new(const BpqConfig *cfg, char *err, size_t errn);
void    bpq_mww_free(BpqMww *m);

/* Solucion semilla + magnitud: rearma la tabla y los overrides (sin tocar los
 * del usuario: cada semilla empieza limpia, como en la GTK). 1 = OK. */
int     bpq_mww_seed(BpqMww *m, const CsnhypodbpSolution *sol, double mag,
                     char *err, size_t errn);
int     bpq_mww_seeded(const BpqMww *m);
const char *bpq_mww_rundir(const BpqMww *m);

/* Tabla de parametros vigente (nombre, valor, origen y campo editable). */
int     bpq_mww_rows(const BpqMww *m, BpqMwwParamRow *rows, int max);

/* Celda editada: aplica el texto al override. 1 = aplicado; 0 = texto invalido
 * (se descarta el campo, nunca un prefijo a medias) o campo no editable. */
int     bpq_mww_edit(BpqMww *m, int field, const char *txt);
/* Rango [lo,hi] de un campo numerico editable. 0 = si, -1 = no lo es. */
int     bpq_mww_field_limits(int field, double *lo, double *hi);

/* Corre la inversion (prep + motor + parseo). BLOQUEANTE: va en un hilo.
 * Devuelve el resultado o NULL; `err` trae el motivo real (timeout, error del
 * motor, ARC/entrada invalida, ...) tomado del log de la corrida. */
BpqMwwResult *bpq_mww_run(BpqMww *m, int dry_run, char *err, size_t errn);
const BpqMwwResult *bpq_mww_result(const BpqMww *m);
int     bpq_mww_result_ok(const BpqMwwResult *r);

/* Textos del panel (ya formateados, uno por cuadro): resumen y salida completa
 * del motor (vacio si no hay). */
const char *bpq_mww_summary(const BpqMww *m);
const char *bpq_mww_log(const BpqMww *m);

/* 1 si el resultado tiene mecanismo focal (se puede guardar el tensor). */
int     bpq_mww_tensor_ready(const BpqMww *m);
/* Guarda la solucion en la BD si falta (event + origin `<E<qid>-<hora>>:<mod>`)
 * y le cuelga su Mww y su mecanismo focal, dejandola preferida. Es el mismo
 * camino que `csnhypodbp_mww_persist` de la GTK (upsert_event / origin_if_absent
 * / magnitude / tensor / set_preferred). `mod` > 0; `algorithm` es el nombre
 * del locator resuelto de la tabla (NULL -> "").
 * 1 = OK; si no, `msg` trae el error real. */
int     bpq_mww_save(BpqDb *db, const CsnhypodbpSolution *sol, int mod,
                     const char *algorithm, const BpqMww *m, char *msg,
                     size_t n);

/* Picadas que se mandan al localizador: una P y una S por estacion, ranking
 * M > A > Teo, con los checks de origen y la casilla de cada fila
 * (`pick_on[i][0]` = P, `[1]` = S; NULL = todas marcadas), mas la puerta
 * `max_dist_km` (<= 0 = sin puerta). `base` siembra lat/lon/prof/qid. Deja las
 * picadas en `out->picks`, completa chan/net/loc desde el catalogo y devuelve
 * cuantas van; -1 = argumentos invalidos. */
int bpq_locate_build_picks(const BpqStations *st, const CsnhypodbpSolution *base,
                           const CsnhypodbpStaRow *rows, int nrows,
                           const int (*pick_on)[2],
                           int auto_on, int manual_on, int theo_on,
                           double max_dist_km, CsnhypodbpSolution *out);

/* Conteo de las picadas que saldrian, por fase y origen (misma mascara y
 * puerta que bpq_locate_build_picks). Devuelve el total; los punteros NULL se
 * ignoran. Sirve al preflight: "solo M" con 0 manuales debe decir M=0 A=3. */
int bpq_locate_count_origins(const CsnhypodbpStaRow *rows, int nrows,
                             const int (*pick_on)[2],
                             int auto_on, int manual_on, int theo_on,
                             double max_dist_km,
                             int *np, int *ns, int *na, int *nm, int *nt);

/* Corre la cadena completa EN EL HILO DEL LLAMADOR (bloquea segundos: la UI la
 * lanza en su worker). Localizador = `cfg->active_locator` (o el primero
 * declarado), timeout `LocatorTimeoutSec`, `Fix`/`FixedDepthKm` y `LocateKeepTmp`
 * del .d. Devuelve 1 con `out` lleno, 0 sin solucion (el motivo REAL -- `timeout`,
 * `ref.err`, ARC invalido -- va en `err`). `cancel` (opcional) aborta al ponerse
 * a 1. */
int bpq_locate_run(const BpqConfig *cfg, const BpqStations *st,
                   const CsnhypodbpSolution *base, const char *locator,
                   const CsnhypodbpStaRow *rows, int nrows,
                   const int (*pick_on)[2],
                   int auto_on, int manual_on, int theo_on,
                   double fix_depth_km,
                   const CsnhypodbpTrace *trace, int ntr,
                   const CsnhypodbpMagOvrRow *ovr, int novr, unsigned kind_mask,
                   BpqLocateOut *out, volatile int *cancel,
                   char *err, size_t errn);

/* Pinta la escena en `bgra` (BGRA premultiplicado, como
 * QImage::Format_ARGB32_Premultiplied). `stride` <= 0 => 4*w. 0 = OK. */
int         bpq_scene_paint (BpqScene s, const void *ctx, unsigned char *bgra,
                             int w, int h, int stride);

/* Exporta a SVG/PDF/PNG. `scale` <= 0 => 1.0. 0 = OK. */
int         bpq_scene_export(BpqScene s, const void *ctx, const char *path,
                             BpqFmt fmt, int w, int h, double scale);

/* Tamano minimo razonable del lienzo (para no inflar el minimo de la ventana). */
int         bpq_scene_min_size(BpqScene s, int *w, int *h);

const char *bpq_scene_name(BpqScene s);

/* ---------------------------------------------------------------------------
 * Anillos y mensajes
 * ------------------------------------------------------------------------- */

/* Un TYPE_MAGNITUDE ya interpretado (MAG_INFO + atoi del qid). */
typedef struct {
    int  qid;
    int  mod;               /* MAG_INFO.origin_version: modulo origen del ARC */
    unsigned version;       /* MAG_INFO.qdds_version (no usado aun) */
    double value;           /* MAG_INFO.mag */
    int  nsta;              /* MAG_INFO.nstations */
    char type[8];           /* "ML"/"Mwp"/"Mb"/"Ms" */
    char algorithm[16];
    CsnhypodbpMagIn vals[CSNHYPODBP_MAGSTA_MAX];
    int  nvals;
} BpqMagMsg;

typedef void (*BpqArcFn) (const CsnhypodbpSolution *sol, const char *raw,
                          size_t rawlen, void *ud);
typedef void (*BpqMagFn) (const BpqMagMsg *m, void *ud);
typedef void (*BpqPickFn)(const CsnhypodbpTracePick *p, void *ud);

typedef struct BpqRings BpqRings;

/* Abre los anillos del .d (InRing, InRingRaw opcional, InRingPick, OutRing,
 * ConfRing opcional) y resuelve tipos/instancia/modulo.
 * Fail-fast: NULL + `err` si falta el anillo de entrada/salida, el modulo en
 * earthworm.d o un tipo. NO adjunta nada si la validacion falla. */
BpqRings *bpq_rings_open(const BpqConfig *cfg, char *err, size_t errn);
void      bpq_rings_close(BpqRings *r);

/* Drena InRing y (si existe) InRingRaw: entrega ARC y magnitudes por callback.
 * Drena tambien InRingPick: entrega picadas. Devuelve el nº de mensajes
 * procesados o -1 (con `err`). No deja el anillo a medias. */
int bpq_rings_drain(BpqRings *r, BpqArcFn arc, BpqMagFn mag, BpqPickFn pick,
                    void *ud, char *err, size_t errn);

int bpq_rings_heartbeat(BpqRings *r, double now_epoch);
int bpq_rings_should_quit(BpqRings *r);

/* Tipo pick manual -> OutRing. `seq` lo lleva el llamador (0..255). */
int bpq_emit_pick(BpqRings *r, const char *sta, const char *chan,
                  const char *net, const char *loc, char phase, double t,
                  char pref);
/* Magnitud manual (CSNMan) -> InRing. `text` es "valor-nestaciones". */
int bpq_emit_mag(BpqRings *r, const BpqMagMsg *m, const char *text);
/* ARC confirmado -> ConfRing. 0 OK; -1 ARC invalido (qid/version);
 * -2 ConfRing no configurado; -3 error de publicacion. */
int bpq_emit_confirm(BpqRings *r, const CsnhypodbpSolution *sol,
                     char *arc, size_t arcn);

/* --- formateadores PUROS (sin anillo): lo que se publica, byte a byte --- */
/* Mismo formato que csnhypodbp_ws.c:1728. `pref` 0 -> '-'. */
int bpq_pick_msg_format(char *out, size_t n, unsigned char type,
                        unsigned char mod, unsigned char inst, int seq,
                        const char *sta, const char *chan, const char *net,
                        const char *loc, char phase, double t, char pref);
/* Serializa un TYPE_MAGNITUDE "CSNMan" (wr_mag_scnl). `text` = "valor-nst". */
int bpq_mag_msg_format(char *out, size_t n, const BpqMagMsg *m, const char *text);

/* Parsea un TYPE_MAGNITUDE. 0 OK, -1 error. Nunca toca `raw` (copia interna
 * con holgura: rd_mag parsea in-place y lee 1 byte de mas). */
int bpq_magmsg_parse(const char *raw, size_t len, BpqMagMsg *out);

/* ---------------------------------------------------------------------------
 * Store de eventos (agrupacion + fila maestra)
 * ------------------------------------------------------------------------- */

typedef struct BpqEvents BpqEvents;
typedef struct BpqEvent  BpqEvent;   /* vista de solo lectura de un grupo */

/* Tope duro de la capacidad del store. La busqueda dimensiona el store a su
 * `Max events` (bpq_events_reserve); esto solo evita una reserva absurda
 * (~16 KB por grupo: la clave lleva 256 picadas embebidas). */
#define BPQ_EVENTS_MAX_GROUPS 4096

/* Crea el store. Copia la config (tolerancia/min_match/tope salen de ella).
 * Arranca con la capacidad por defecto del camino vivo (128 grupos). */
BpqEvents *bpq_events_new(const BpqConfig *cfg);
void       bpq_events_free(BpqEvents *e);

/* Ajusta la capacidad del store a `cap` grupos (idempotente; solo crece y se
 * recorta a BPQ_EVENTS_MAX_GROUPS). La busqueda la fija con su `Max events`
 * para que la vista no recorte en silencio. 0 OK, -1 argumentos o sin memoria. */
int bpq_events_reserve(BpqEvents *e, int cap);

/* Incorpora una solucion: busca grupo por qid o por firma de picadas
 * (csnhypodbp_group_matches), deduplica por (qid,mod,version) y aplica las
 * magnitudes que hubieran llegado antes. 0 OK, -1 argumentos, -2 sin capacidad. */
int bpq_events_feed_arc(BpqEvents *e, const CsnhypodbpSolution *sol);
/* Igual que feed_arc, pero con la hora de creacion de la fila de `origin`
 * (epoch UTC). `created <= 0` = sellar con time(NULL) como el camino vivo; al
 * re-alimentar una solucion existente con 0 se CONSERVA su `created` (el anillo
 * repite mensajes y no debe pisar la hora de la BD). */
int bpq_events_feed_arc_at(BpqEvents *e, const CsnhypodbpSolution *sol,
                           double created);
/* Igual que feed_arc_at, pero con la identidad del evento en la BD: dos
 * soluciones con el MISMO `event_id` van al mismo grupo aunque su qid sea 0
 * (ids de otra fuente, p.ej. "csn2024mpmd" -> atoi() = 0). El qid/signatura
 * solo puede unir con un grupo SIN event_id (camino vivo), de modo que el
 * historial y el anillo del mismo evento no se duplican. `event_id` NULL o
 * vacio == camino vivo. */
int bpq_events_feed_arc_event(BpqEvents *e, const CsnhypodbpSolution *sol,
                              double created, const char *event_id);
/* Olvida todos los grupos (busqueda: el resultado reemplaza la vista). */
void bpq_events_clear(BpqEvents *e);

/* Historial / busqueda en la base hipocentral (PostgreSQL, comun). */

/* Abre con DbEnvFile; NULL solo si no hay memoria (si la BD no responde el
 * objeto es valido con `err` y las cargas devuelven 0). */
BpqDb *bpq_db_open(const BpqConfig *cfg, char *err, size_t errn);
void   bpq_db_close(BpqDb *db);
int    bpq_db_available(const BpqDb *db);

/* Vuelca al store los origenes de las ultimas `hours` horas (historial vivo). */
int    bpq_db_load_recent(BpqDb *db, const BpqConfig *cfg, BpqEvents *store,
                          int hours, char *err, size_t errn);

/* Busqueda por rango de tiempo de origen (epoch, UTC) con tope de eventos. */
int    bpq_db_search(BpqDb *db, const BpqConfig *cfg, BpqEvents *store,
                     double t_min, double t_max, int limit,
                     char *err, size_t errn);
/* Magnitudes por estacion de un origen y una magnitud (ML/Mwp/Mb/Ms). */
int    bpq_db_mag_stations(BpqDb *db, const char *origin_id, int kind,
                           CsnhypodbpMagIn *out, int max);

/* "YYYY-MM-DD HH:MM:SS" o ISO con 'T' (UTC) -> epoch. 0 OK, -1 formato. */
int    bpq_db_parse_utc(const char *txt, double *epoch);

/* epoch -> "YYYY-MM-DD HH:MM:SS" (UTC), para prellenar el dialogo. 0 OK. */
int    bpq_db_fmt_utc(double epoch, char *out, size_t n);
/* Magnitud de una solucion ("%.1f-%d"). Si la solucion aun no existe se guarda
 * pendiente y se aplica al llegar su ARC. 0 OK, -1 argumentos. */
int bpq_events_feed_mag(BpqEvents *e, const BpqMagMsg *m);
/* Igual que feed_mag, pero la magnitud se aplica SOLO al grupo de ese
 * `event_id` (una fila de la BD): con qid=0 el camino por qid la aplicaria a
 * cualquier evento ajeno. `event_id` NULL o vacio == camino vivo. */
int bpq_events_feed_mag_event(BpqEvents *e, const BpqMagMsg *m,
                              const char *event_id);

int bpq_events_count(const BpqEvents *e);
const BpqEvent *bpq_events_at(const BpqEvents *e, int i);
int bpq_event_nsols(const BpqEvent *ev);
const CsnhypodbpSolution *bpq_event_sol(const BpqEvent *ev, int j);
const CsnhypodbpSolMeta  *bpq_event_sol_meta(const BpqEvent *ev, int j);
/* Indice de la solucion preferida (csnhypodbp_preferred_index); -1 si no hay. */
int bpq_event_preferred(const BpqEvent *ev);

/* Tabla maestra: 15 columnas con las cabeceras LITERALES de csnhypodbp
 * (Date, Lat., Lon., Dep, Res, Azm, #Stn, ID, Ml, Mwp, Mb, Ms, Mod, Ver, Est.).
 * `bpq_event_cell` usa la solucion preferida; `bpq_event_sol_cell` una concreta. */
int         bpq_event_ncells(void);
const char *bpq_event_header(int col);
void        bpq_event_cell(const BpqEvent *ev, int col, char *out, size_t n);
void        bpq_event_sol_cell(const BpqEvent *ev, int j, int col, char *out, size_t n);

/* Tabla de origenes (detalle): 15 columnas Mod,Ver,Est.,Created,Date,Lat.,Lon.,
 * Dep,Res,#Stn,ID,Ml,Mwp,Mb,Ms */
int         bpq_origin_ncells(void);
const char *bpq_origin_header(int col);
void        bpq_origin_cell(const BpqEvent *ev, int j, int col, char *out, size_t n);

/* Orden por cabecera: 0 = texto, 1 = numerico, 2 = fecha; y el valor numerico
 * crudo (para Lat/Lon el valor real, no el texto con S/N). NAN = sin dato. */
int         bpq_event_sort_kind(int col);
double      bpq_event_sort_num(const BpqEvent *ev, int col);
double      bpq_origin_sort_num(const BpqEvent *ev, int j, int col);

#ifdef __cplusplus
}
#endif

#endif /* BPQ_CAPI_H */
