/******************************************************************************
 * pickml.h                                                                   *
 *                                                                            *
 * Picker neuronal de ondas P y S sobre un grafo ONNX (EQTransformer).        *
 *                                                                            *
 * Estrategia:                                                                *
 *   - Buffer 3C por estación a 100 Hz, con máscara de validez (los huecos    *
 *     NO se rellenan con ceros: el store los rellena y una red los lee como  *
 *     señal).                                                                *
 *   - Ventanas de 6000 muestras (60 s) con paso configurable; el grafo lleva *
 *     la normalización dentro, así que aquí se pasan muestras crudas.        *
 *   - Post-proceso en C (blinding, umbrales, picos, dedup por fase) porque   *
 *     eso NO está en el grafo.                                               *
 *   - Motor conmutable: `stub` (tests sin ONNX Runtime) u `onnx`.            *
 *                                                                            *
 * Entradas/salidas:                                                          *
 *   offline: pickml <pickml.d> --tank <file> | --store <root> --t0 --t1      *
 *            -> un JSON por pick (stdout), determinista.                     *
 *   anillo : ondas TYPE_TRACEBUF2 de SLINK_RING                              *
 *            -> picks TYPE_PICK_SCNL a PICK_RING.                            *
 *                                                                            *
 * NO asocia ni localiza: eso es de csnloc. No calcula magnitudes.            *
 ******************************************************************************/

#ifndef PICKML_H
#define PICKML_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <time.h>
#include <trace_buf.h>

#define PM_MAX_STA      512
#define PM_SCNL_LEN     16
#define PM_STR_LEN      256
#define PM_ID_LEN       64
#define PM_MAX_COMP     3
#define PM_WIN_SAMPLES  6000        /* default (60 s a 100 Hz, EQT); ver WindowSamples */
#define PM_MAX_EMIT     64
#define PM_DEDUP_SLOTS  256

#define PM_OK      0
#define PM_NODATA  1        /* sin datos suficientes */
#define PM_ERR    (-1)      /* error */
#define PM_GAP    (-2)      /* hueco mayor que MaxGapSec */

/* Orden interno = orden de canales del grafo (Z, N, E). */
enum { PM_COMP_Z = 0, PM_COMP_N = 1, PM_COMP_E = 2 };

enum { PM_ENGINE_STUB = 0, PM_ENGINE_ONNX = 1 };
enum { PM_SRC_TANK = 0, PM_SRC_STORE = 1, PM_SRC_SDS = 2 };

/* ---- Estación de pickml.sta ------------------------------------------------
 * Formato (idéntico a pickS.sta):  PickFlag Pin Sta Net Loc ChanE ChanN ChanZ
 * Ej:  1 1 FAR1 C -- HHE HHN HHZ
 * ------------------------------------------------------------------------- */
typedef struct {
    int  flag;
    int  pin;
    char sta[PM_SCNL_LEN];
    char net[PM_SCNL_LEN];
    char loc[PM_SCNL_LEN];
    char chan[PM_MAX_COMP][PM_SCNL_LEN];   /* Z, N, E */
} PmStation;

/* ---- Configuración (pickml.d) -------------------------------------------- */
typedef struct {
    char MyModName[PM_ID_LEN];
    char InRingName[PM_ID_LEN];
    char OutRingName[PM_ID_LEN];
    long InKey, OutKey;
    int  HeartbeatInt;
    int  LogFile;
    int  Debug;

    char StaFile[PM_STR_LEN];
    char ModelName[PM_ID_LEN];   /* etiqueta: eqt | phasenet (va a la metadata) */
    char ModelFile[PM_STR_LEN];
    char OrtLib[PM_STR_LEN];
    int  Engine;                 /* PM_ENGINE_STUB | PM_ENGINE_ONNX */
    int  OrtThreads;             /* 0 = auto */
    int  OrtOptLevel;            /* 0 = none, 1 = basic, 2 = all */

    long WindowSamples;          /* muestras por ventana: la nativa del modelo */
    double Rate;                 /* 100.0 */
    double RateTolHz;            /* 0.01 */

    double StepSec;              /* paso entre ventanas */
    double MinPosSec;            /* no se emiten picos antes de esta posición */
    double MaxPosSec;            /* 0 = WindowSec - 1/Rate */
    int    BlindSamples;         /* blinding del borde (muestras) */
    int    Stacking;             /* 0 = avg, 1 = max */

    double PThreshold, SThreshold, DetThreshold;
    double WProb[4];             /* prob -> weight 0..3; debajo de WProb[3], se descarta */

    double DedupSec;
    double MaxGapSec;
    double MinSPSec;             /* descarta S a menos de esto de un P (0 = off) */

    char PReportChan[4];         /* Z|N|E */
    char SReportChan[4];

    char DumpWindowsDir[PM_STR_LEN];

    char PickOutFile[PM_STR_LEN];    /* JSONL de picks; vacío = stdout */
    char PicksOutFile[PM_STR_LEN];   /* líneas TYPE_PICK_SCNL; vacío = no se escribe */
    char RunMetaFile[PM_STR_LEN];    /* cabecera JSON de la corrida */

    int  LiveMaxAgeSec;              /* live: descarta trazas más viejas (0 = off) */

    /* Publicación al anillo (sólo modo --ring). Default: apagada. */
    int  PublishRing;                /* 0/1: tport_putmsg a OutRing */
    int  PublishP;                   /* derivado de PublishPhases */
    int  PublishS;
    int  PublishMaxWeight;           /* 0..3: publica si weight <= valor */
} PmConfig;

/* ---- Traza tal como sale de una fuente ----------------------------------- */
typedef struct {
    char    sta[TRACE2_STA_LEN], net[TRACE2_NET_LEN];
    char    loc[TRACE2_LOC_LEN], chan[TRACE2_CHAN_LEN];
    double  t0;
    double  rate;
    long    n;
    int32_t *samples;            /* prestado: lo libera la fuente */
} PmTrace;

/* ---- Buffer 3C por estación ---------------------------------------------- */
typedef struct {
    double   rate;
    double   t0;                 /* tiempo del índice absoluto 0 */
    long     cap;                /* muestras por componente */
    long     lo[PM_MAX_COMP];
    long     hi[PM_MAX_COMP];    /* exclusivo */
    int32_t *x[PM_MAX_COMP];
    uint8_t *valid[PM_MAX_COMP];
    double   first_t[PM_MAX_COMP];   /* primer tiempo con dato por componente */
    int      started;
    long     late, dup, overrun, shift;
} PmBuf;

/* ---- Pick candidato ------------------------------------------------------- */
typedef struct {
    double t;
    char   phase;
    float  prob;
} PmPick;

/* ---- Estado de deduplicación por estación -------------------------------- */
typedef struct {
    double t[PM_DEDUP_SLOTS];
    char   phase[PM_DEDUP_SLOTS];
    int    n;
} PmDedup;

/* ---- Fuente de trazas (tank | store) ------------------------------------- */
typedef struct PmSrc PmSrc;

/* ---- Motor de inferencia -------------------------------------------------- */
typedef struct PmEngine PmEngine;

/* ============================ config ===================================== */
void pm_config_defaults(PmConfig *c);
int  pm_config_load(const char *file, PmConfig *c, char *err, size_t errn);
void pm_config_print(const PmConfig *c, FILE *f);

/* ============================ estaciones ================================= */
int  pm_sta_read(const char *file, PmStation *out, int maxsta);
int  pm_sta_find(const PmStation *list, int n, const char *sta,
                 const char *chan, const char *net, const char *loc,
                 int *comp_out);
int  pm_sta_chan_for_phase(const PmStation *st, char phase,
                           const PmConfig *c);

/* ============================ buffer ===================================== */
int  pm_buf_init(PmBuf *b, double rate, long cap);
void pm_buf_free(PmBuf *b);
int  pm_buf_add(PmBuf *b, int comp, double t0, double rate,
                const int32_t *x, long n);
double pm_buf_ready(const PmBuf *b);   /* último t con las 3 comps; -1 si no */
int  pm_buf_window(const PmBuf *b, double t0, long nsamp, double maxgap_sec,
                   float *out3c, long *interp);

/* ============================ ventanas =================================== */
typedef struct {
    double t_next;
    double step_sec;
    double win_sec;
} PmWin;

void pm_win_init(PmWin *w, double t_start, double step_sec, double win_sec);
int  pm_win_next(PmWin *w, double t_ready, double *t0);

/* ============================ post-proceso =============================== */
int pm_post_peaks(const float *curve, long n, double t0, double rate,
                  double thr, double minpos, double maxpos, char phase,
                  PmPick *out, int cap);
int pm_prob_weight(double prob, const double wprob[4], int *weight);
int pm_dedup_accept(PmDedup *d, double t, char phase, double dedup_sec);

int pm_gate_s_vs_p(PmPick *sp, int ns, const PmPick *pp, int np, double minsp);

/* ============================ reporte ==================================== */
int pm_report_format(char *buf, size_t n, int modid, int instid, int seq,
                     const char *sta, const char *chan, const char *net,
                     const char *loc, double t_epoch, int weight, char phase);

/* ============================ fuente ===================================== */
int  pm_src_open_tank(PmSrc **out, const char *path);
int  pm_src_open_store(PmSrc **out, const char *root, const PmStation *sta,
                       int nsta, double t0, double t1);
int  pm_src_next(PmSrc *s, PmTrace *tr);   /* 1 hay, 0 fin, <0 error */
const char *pm_src_error(const PmSrc *s);
void pm_src_close(PmSrc *s);

/* Contadores de descartes de la fuente (hoy sólo el backend SDS los llena). */
typedef struct { long no_day, nodata, rate; } PmSrcCounts;

/* Modo offline sobre un SDS de SeisComP: lee miniSEED y decima a `rate`. */
int  pm_src_open_sds(PmSrc **out, const char *root, const PmStation *sta,
                     int nsta, double t0, double t1, double rate);
void pm_src_counts(const PmSrc *s, PmSrcCounts *c);

/* ============================ publicacion (anillo) ======================= */
/* Política pura (sin EarthWorm), testeable headless. */
typedef enum { PM_PUB_SEND = 0, PM_PUB_SKIP_PHASE, PM_PUB_SKIP_WEIGHT } PmPubDecision;

/* P|S|PS (case-insensitive) -> flags. 0 ok, -1 inválido. */
int pm_publish_phases_parse(const char *s, int *allow_p, int *allow_s);
/* Texto canónico de las fases habilitadas: "PS", "P" o "S". */
void pm_publish_phases_str(int allow_p, int allow_s, char *buf, size_t n);
/* Decide si un pick se publica, y por qué no. */
PmPubDecision pm_publish_decide(const PmConfig *c, char phase, int weight);

/* ============================ anillo (live) ============================== */
typedef struct PmRing PmRing;

int  pm_ring_open(PmRing **out, const char *ringname, int wait_ms,
                  char *err, size_t errn);
int  pm_ring_next(PmRing *r, PmTrace *tr);   /* 1 hay, 0 nada, <0 error */
long pm_ring_skipped(const PmRing *r);
void pm_ring_close(PmRing *r);

/* ============================ motor ====================================== */
int  pm_engine_open(PmEngine **out, const PmConfig *c, char *err, size_t errn);
void pm_engine_close(PmEngine *e);
/* in: [nwin][3][nsamp]; p/s/det: [nwin][nsamp] */
int  pm_engine_run(PmEngine *e, const float *in, int nwin, long nsamp,
                   float *p, float *s, float *det);
const char *pm_engine_name(const PmEngine *e);

/* ============================ utilidades ================================= */
int  pm_parse_time(const char *s, double *t_epoch);
void pm_fmt_utc(char *buf, size_t n, double t_epoch);

#endif /* PICKML_H */
