#ifndef TVQ_CAPI_H
#define TVQ_CAPI_H

/* csntvpQT - frontera UNICA entre la capa C (c/) y la capa C++/Qt (ui/).
 *
 * Reglas (heredadas de csnrvQT / ew_controllerQT, docs/testLibQT.md):
 *   R1. Ningun TU C++ incluye headers de EarthWorm/libpq/cairo/GTK: solo este
 *       header (y los de ewguiqt/).
 *   R2. Todo lo declarado aqui va en `extern "C"`.
 *   R3. Los handles son opacos y los libera el llamante (new/free).
 *   R4. Este header no incluye nada con Qt ni cairo: se compila igual desde C.
 *
 * Hilos: la capa C es MONOHILO. La UI la conduce desde sus QTimer, sin locks.
 * (Un hilo de drenaje queda para la etapa 2; el seam es `tvq_core_tick`.)
 *
 * `TvqConfig` se PRESTA a core: debe vivir mas que el (main.cxx la libera
 * despues de app.exec()).
 */

#include <stddef.h>
#include <stdio.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- constantes del visor (paridad con csntvp.h) ------------------------- */
#define TVQ_MAX_STR          512
#define TVQ_MAX_STATIONS     512
#define TVQ_MAX_MINUTES      120
#define TVQ_MAX_SAMP_RATE    250    /* tope de buffer por estacion (muestras/s) */
#define TVQ_PANEL_WIDTH      90     /* panel izquierdo con STA/CHAN             */
#define TVQ_BOTTOM_AXIS_H    30
#define TVQ_STALE_SECS       10.0   /* DATA_STALE_SECS del GTK                  */
#define TVQ_MAX_REALTIME_SKEW 86400.0
#define TVQ_MIN_ZOOM         0.05
#define TVQ_MAX_ZOOM         100.0
#define TVQ_REFRESH_MS       150
#define TVQ_BACKGROUND_MS    1000
#define TVQ_LOOKAHEAD_SECS   15.0   /* picks visibles hasta t_right + 15 s      */

/* Papeles de color (los 6 del GTK). */
enum {
    TVQ_COLOUR_WAVE = 0,
    TVQ_COLOUR_BG,
    TVQ_COLOUR_FONT,
    TVQ_COLOUR_SEP,
    TVQ_COLOUR_P,
    TVQ_COLOUR_S,
    TVQ_COLOUR_COUNT
};

/* ------------------------------------------------------------------------- */
/* Configuracion (.d)                                                        */
/* ------------------------------------------------------------------------- */

typedef struct TvqConfig {
    char   my_module[64];                 /* MyModuleId: MOD_CSNTVPQT        */
    int    module_id;                     /* numero resuelto, -1 si falta     */
    char   in_ring[64];                   /* InRing: SLINK_RING               */
    char   out_ring[64];                  /* OutRing: PICK_RING               */
    int    heartbeat_int;
    int    log_file;
    char   sta_file[TVQ_MAX_STR];         /* StaFile (relativo a $EW_PARAMS)  */
    int    stations_per_screen;           /* def. 12, 1..512                  */
    int    time_window_min;               /* def. 6, 1..120                   */
    double pick_replace_secs;             /* def. 15.0, 0 = off               */
    double colour[TVQ_COLOUR_COUNT][3];   /* los 6 roles del GTK              */
} TvqConfig;

/* Carga <path>. Devuelve NULL y el motivo en `err` si falta una de las 6
 * obligatorias (MyModuleId/InRing/OutRing/HeartBeatInt/LogFile/StaFile) o hay
 * error de parseo. Las claves DESCONOCIDAS se ignoran (asi el .d del GTK y el
 * del port pueden compartir formato). Los avisos de valor invalido van a
 * stderr, como en el GTK.
 * No falla si `MyModuleId` no esta en earthworm.d: lo deja en module_id=-1. */
TvqConfig *tvq_config_load(const char *path, char *err, size_t errlen);
void       tvq_config_free(TvqConfig *cfg);

/* Bloque `key=value` estable de `--print-config` (lo compara test_config.sh). */
void tvq_print_config(const TvqConfig *cfg, FILE *out);

/* ------------------------------------------------------------------------- */
/* Estaciones (StaFile: STA CHAN NET LOC escala)                             */
/* ------------------------------------------------------------------------- */

typedef struct {
    char   sta[16], chan[16], net[8], loc[8];
    double scale;
} TvqStaEntry;

/* 1 = entrada valida, 0 = linea vacia/comentario, -1 = malformada.
 * Quirk del GTK conservado: con 4 campos, el 4.o es la escala y LOC = "--". */
int tvq_sta_parse_line(const char *line, TvqStaEntry *out);

/* Carga el fichero completo. Devuelve 0 y `*n` filas, o -1 con `err`. */
int tvq_sta_load(const char *path, TvqStaEntry *arr, int max,
                 int *n, char *err, size_t errlen);

/* ------------------------------------------------------------------------- */
/* Estado vivo                                                               */
/* ------------------------------------------------------------------------- */

typedef struct TvqCore TvqCore;

TvqCore *tvq_core_new(const TvqConfig *cfg, char *err, size_t errlen);
void     tvq_core_free(TvqCore *c);

/* Adjunta los dos anillos del .d y resuelve tipos/pids. 0 = OK; -1 con `err`
 * (incluye el caso "MOD_CSNTVPQT no esta en earthworm.d" con la receta). */
int  tvq_core_open_rings(TvqCore *c, char *err, size_t errlen);
int  tvq_core_load_stations(TvqCore *c, char *err, size_t errlen);
int  tvq_core_reload_stations(TvqCore *c);

/* Ingesta de un mensaje suelto (la usa el tick y los tests, sin anillos). */
int tvq_core_ingest_trace(TvqCore *c, const char *msg, int len);
int tvq_core_ingest_pick (TvqCore *c, const char *msg, int len);

enum {
    TVQ_CH_TRACES   = 1,
    TVQ_CH_PICKS    = 2,
    TVQ_CH_TIME     = 4,
    TVQ_CH_STATIONS = 8
};

/* Drena ambos anillos (no bloqueante), actualiza reloj/envolventes y devuelve
 * el bitmask de lo que cambio. `now_wall` = epoch UTC en segundos. */
unsigned tvq_core_tick(TvqCore *c, double now_wall);
void     tvq_core_heartbeat(TvqCore *c, double now_wall);
int      tvq_core_should_quit(const TvqCore *c);

/* Purga las estaciones sin paquetes en `max_age_s` segundos. Devuelve cuantas
 * quito. `max_age_s <= 0` no quita nada. */
int tvq_core_clean(TvqCore *c, double max_age_s);

/* Ids de anillo/tipo resueltos (para mensajes y tests). */
int tvq_core_module_id(const TvqCore *c);

/* ------------------------------------------------------------------------- */
/* Vista                                                                     */
/* ------------------------------------------------------------------------- */

void tvq_core_set_hold(TvqCore *c, int on);
int  tvq_core_is_hold(const TvqCore *c);
void tvq_core_view_range(const TvqCore *c, double *t_left, double *t_right);
void tvq_core_set_stations_per_screen(TvqCore *c, int n);   /* 1..512 */
void tvq_core_set_time_window_min(TvqCore *c, int m);        /* 1..120 */
void tvq_core_set_filter(TvqCore *c, int type, double f1, double f2, int order);
void tvq_core_get_filter(const TvqCore *c, int *type, double *f1, double *f2,
                         int *order);
void tvq_core_adjust_scale(TvqCore *c, double factor);       /* 0.05..100 */
double tvq_core_zoom(const TvqCore *c);

/* '\0' = sin fase armada (Esc o soltar HOLD); 'P'/'S' = fase del proximo
 * clic. Cualquier otro caracter desarma. Sin fase, el clic no pica. */
void tvq_core_set_pick_phase(TvqCore *c, char phase);
char tvq_core_pick_phase(const TvqCore *c);   /* '\0' | 'P' | 'S' */

/* Guia vertical del puntero para el picado: `x` en px LOGICOS del lienzo
 * (mismo sistema que tvq_core_hit) y valid=0 la oculta. Se pinta solo si hay
 * fase armada y `x` cae dentro del area de trazas (x > TVQ_PANEL_WIDTH y
 * x < ancho). Es estado de vista: no toca picadas ni anillos. */
void tvq_core_set_guide(TvqCore *c, double x, int valid);
int  tvq_core_guide(const TvqCore *c, double *x);   /* 1 = visible, 0 = oculta */

/* Color de un papel (0..TVQ_COLOUR_COUNT-1). */
void tvq_core_get_colour(const TvqCore *c, int role, double rgb[3]);
void tvq_core_set_colour(TvqCore *c, int role, const double rgb[3]);
/* Bloque copiable al .d (paridad con LogColorConfig del GTK). */
void tvq_core_print_colour_config(const TvqCore *c, FILE *out);

/* ------------------------------------------------------------------------- */
/* Estaciones/instantanea (para la UI y --dump)                              */
/* ------------------------------------------------------------------------- */

int tvq_core_nstations(const TvqCore *c);

typedef struct {
    char   sta[16], chan[16], net[8], loc[8];
    double rate, scale;
    double t_first, t_last;
    long   cap;
    int    n_picks;
    int    stale;
} TvqStationInfo;

int tvq_core_station_info(const TvqCore *c, int i, TvqStationInfo *out);

/* Muestra cruda en el indice absoluto `abs_idx` (rate * t). 1 = habia dato,
 * 0 = hueco (INT_MAX) o fuera del buffer. Sirve a los tests para comprobar que
 * el filtro NO toca el crudo. */
int tvq_core_sample(const TvqCore *c, int row, long long abs_idx, int *value);

/* Volcado `key=value` estable (lo compara test_ui.sh --dump). */
void tvq_core_dump_text(const TvqCore *c, FILE *out);

/* Siembra determinista para los modos instrumentados (sin anillos):
 * ondas sinteticas para todas las estaciones cargadas + 2 picks automaticos. */
int tvq_core_seed_fixture(TvqCore *c, double t_end, int seconds);

/* Formato de la picada publicada. Byte-identico a csntvp_gui.c:326 (formato
 * TYPE_PICK_SCNL extendido); `mod` es el del .d (175 en csntvpqt.d). */
#define TVQ_PICK_MSG_MAX 256
int tvq_pick_msg_format(char *out, size_t n, int type, int mod, int inst,
                        int seq, const char *sta, const char *chan,
                        const char *net, const char *loc, char phase, double t);

/* ------------------------------------------------------------------------- */
/* Interaccion                                                               */
/* ------------------------------------------------------------------------- */

/* Clic -> (fila, tiempo sismico). `x,y` en pixeles LOGICOS del lienzo;
 * `scroll_y` es el desplazamiento vertical de la lista de pistas.
 * 1 = dentro de la zona de trazas, 0 = panel izquierdo/fuera. */
int tvq_core_hit(TvqCore *c, int x, int y, int w, int h, int scroll_y,
                 int *row, double *t);

/* Coloca una picada manual y la publica en OutRing si el anillo esta adjunto.
 * Aplica PickReplaceWindow. 1 = colocada, 0 = rechazada (sin HOLD, fila
 * invalida, fase invalida). */
int tvq_core_place_pick(TvqCore *c, int row, double t, char phase);

/* Ultima picada colocada ("" si no hay); para --dump y tests. */
int tvq_core_last_pick(TvqCore *c, char *out, size_t n);

/* ------------------------------------------------------------------------- */
/* Render (cairo -> BGRA; solo esta capa conoce cairo)                       */
/* ------------------------------------------------------------------------- */

/* `pw/ph` fisicos, `stride` en bytes, `scale` = devicePixelRatio. Se dibuja en
 * pixeles LOGICOS (w = pw/scale). Devuelve 1 si pinto. */
int tvq_paint_traces(TvqCore *c, unsigned char *bgra, int pw, int ph, int stride,
                     double scale, int scroll_y);
int tvq_paint_axis(TvqCore *c, unsigned char *bgra, int pw, int ph, int stride,
                   double scale);

int tvq_core_track_height(const TvqCore *c, int viewport_h);
int tvq_core_canvas_height(const TvqCore *c, int viewport_h);

/* ------------------------------------------------------------------------- */
/* Log                                                                       */
/* ------------------------------------------------------------------------- */

/* `$EW_LOG/csntvpQT_<fecha>.log` (nombre LITERAL del proceso, no el .d).
 * Sin EW_LOG escribible baja a consola en vez de morir. Devuelve el flag real. */
int  tvq_log_init(int log_file);
void tvq_logf(const char *fmt, ...);

/* Epoch UTC en segundos (reloj del visor y de los tests). */
double tvq_now_epoch(void);

#ifdef __cplusplus
}
#endif

#endif /* TVQ_CAPI_H */
