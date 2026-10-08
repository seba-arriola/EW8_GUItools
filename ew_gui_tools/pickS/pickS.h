/******************************************************************************
 * pickS.h                                                                    *
 *                                                                            *
 * Módulo Earthworm nativo de picking automático de ONDA S.                   *
 *                                                                            *
 * Estrategia (híbrida):                                                      *
 *   - Detección independiente sobre las componentes horizontales (STA/LTA     *
 *     de la envolvente horizontal H = sqrt(E^2 + N^2)).                       *
 *   - Refinamiento del onset por AIC.                                        *
 *   - Si hay un pick P de la misma estación (de pick_FP), la búsqueda se       *
 *     restringe a la ventana post-P (modo guiado); si no, es independiente.   *
 *   - Criterio de calidad por polarización 3C (Jurkevics): rectilinealidad,   *
 *     planaridad, incidencia y ratio H/V -> weight 0-4.                       *
 *                                                                            *
 * Entradas/salidas:                                                          *
 *   anillo : ondas TYPE_TRACEBUF2 de SLINK_RING + picks P de PICK_RING        *
 *            -> picks S TYPE_PICK_SCNL a PICK_RING                            *
 *   offline: pickS <config> <tankfile> [ppicksfile]                           *
 *            -> imprime una línea TYPE_PICK_SCNL por pick (stdout)            *
 ******************************************************************************/

#ifndef PICKS_H
#define PICKS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <limits.h>
#include <stdint.h>
#include <trace_buf.h>

#define PICKS_MAX_STA   512
#define PICKS_MAX_PREF  8192
#define PICKS_ID        64
#define PICKS_STR       128
#define PICKS_SCNL      16
#define PICKS_MAX_COMP  3

enum { PICKS_COMP_E = 0, PICKS_COMP_N = 1, PICKS_COMP_Z = 2 };

enum { PICKS_GUIDE_INDEPENDENT = 0, PICKS_GUIDE_HYBRID = 1 };

enum { PICKS_REPORT_E = 0, PICKS_REPORT_N = 1, PICKS_REPORT_DETECTED = 2 };

/* ---- Estación del fichero pickS.sta (una línea = una estación 3C) ---- */
typedef struct {
    int  pickflag;                       /* 0 = ignorar, 1 = activo */
    int  pin;                            /* pin numerico (informativo) */
    char sta[PICKS_SCNL];
    char net[PICKS_SCNL];
    char loc[PICKS_SCNL];
    char chan[PICKS_MAX_COMP][PICKS_SCNL];   /* E, N, Z */
} PickS_Station;

/* ---- Parámetros de configuracion (pickS.d) ---- */
typedef struct {
    char  MyModName[PICKS_ID];
    char  InRingName[PICKS_ID];
    char  PickRingName[PICKS_ID];
    char  OutRingName[PICKS_ID];
    long  InKey, PickKey, OutKey;
    int   HeartbeatInt;
    int   LogFile;
    int   Debug;
    char  StaFile[PICKS_STR];

    double FilterLowHz, FilterHighHz;
    int    FilterOrder;

    double StaLenSec, LtaLenSec;
    double TriggerOn, TriggerOff;
    double DeadTimeSec;
    double AicWinSec;
    double BufferSec;

    int    GuideMode;
    double GuideMinDtSec, GuideMaxDtSec;

    double PolWinSec;

    double MinSnr;
    int    MaxWeight;
    int    ReportChan;

    double Q_Snr_W0, Q_Snr_W4;
    double Q_StaLta_W0, Q_StaLta_W4;
    double Q_Rect_W0, Q_Rect_W4;
    double Q_IncidMinDeg, Q_IncidMaxDeg;
    double Q_Plan_W0, Q_Plan_W4;      /* planaridad; W0<=0 => deshabilitado */
    double Q_Hv_W0, Q_Hv_W4;          /* ratio H/V;  W0<=0 => deshabilitado */

    double GateIncidMinDeg, GateIncidMaxDeg;  /* compuerta dura; Max<=Min => off */

    int    MetricsLog;                /* 1 = loguear METRICS por deteccion */
} PickS_Params;

/* ---- Filtro biquad en cascada (hasta 8 secciones de 2do orden) ---- */
#define PICKS_FILT_MAXSECT 8
typedef struct {
    int    nsect;
    double b0[PICKS_FILT_MAXSECT], b1[PICKS_FILT_MAXSECT], b2[PICKS_FILT_MAXSECT];
    double a1[PICKS_FILT_MAXSECT], a2[PICKS_FILT_MAXSECT];
    double x1[PICKS_FILT_MAXSECT], x2[PICKS_FILT_MAXSECT];
    double y1[PICKS_FILT_MAXSECT], y2[PICKS_FILT_MAXSECT];
} PickS_BiQuad;

/* ---- STA/LTA ---- */
typedef struct {
    double  fs, stasec, ltasec;
    int     nsta, nlta;
    double *bsta, *blta;
    int     ista, ilta;
    int     csta, clta;
    double  ssta, slta;
    double  ratio;
} PickS_StaLta;

/* ---- Resultado de polarizacion 3C ---- */
typedef struct {
    double lambda[3];
    double rectilinearity;
    double planarity;
    double incidence_deg;
    double azimuth_deg;
    double hv_ratio;
} PickS_Pol;

/* ---- Metricas de calidad ---- */
typedef struct {
    double snr;
    double stalta_peak;
    double rectilinearity;
    double planarity;
    double incidence_deg;
    double hv_ratio;
} PickS_Metrics;

/* ---- Buffer 3C + estado de deteccion por estacion ---- */
typedef struct {
    int     active;
    int     inited;
    int     started;            /* t0 fijado por el primer paquete */
    char    sta[PICKS_SCNL];
    char    net[PICKS_SCNL];
    char    loc[PICKS_SCNL];
    double  fs;
    double  t0;                 /* tiempo del indice absoluto 0 */
    long    cap;
    double *buf[PICKS_MAX_COMP];
    long    lo[PICKS_MAX_COMP];
    long    hi[PICKS_MAX_COMP]; /* exclusivo */
    long    ntotal;
    long    filt_hi[PICKS_MAX_COMP];
    PickS_BiQuad filt[PICKS_MAX_COMP];
    PickS_StaLta st;
    long    proc_idx;
    int     triggered;
    double  dead_until;
    double  last_add_t;
} PickS_Ring3C;

/* ---- Referencia a un pick P (guia) ---- */
typedef struct {
    char   sta[PICKS_SCNL];
    double t;
} PickS_PickRef;

/* ---- context.c: logging minimo ---- */
extern int picks_debug;

/* config.c */
void PickS_SetDefaults(PickS_Params *p);
int  PickS_ReadConfig(const char *file, PickS_Params *p);
int  PickS_ResolveKeys(PickS_Params *p);

/* sta_list.c */
int  PickS_ReadStaList(const char *file, PickS_Station *out, int maxsta);
int  PickS_FindStation(const PickS_Station *list, int n,
                       const char *sta, const char *chan, const char *net,
                       const char *loc, int *comp_out);

/* filters.c */
void   PickS_FilterInit(PickS_BiQuad *bq, double fs, double f1, double f2, int order);
double PickS_FilterApply(PickS_BiQuad *bq, double x);
void   PickS_FilterReset(PickS_BiQuad *bq);

/* ring3c.c */
int  PickS_Ring3C_Init(PickS_Ring3C *r, const PickS_Station *st, double fs,
                       double bufsec, double f1, double f2, int order);
void PickS_Ring3C_Free(PickS_Ring3C *r);
int  PickS_Ring3C_Add(PickS_Ring3C *r, int comp, double starttime, double fs,
                      const int32_t *data, int nsamp);
int  PickS_Ring3C_Window(const PickS_Ring3C *r, long idx, int len,
                         double *e, double *n, double *z);

/* stalta.c */
void   PickS_StaLta_Init(PickS_StaLta *s, double fs, double stasec, double ltasec);
void   PickS_StaLta_Free(PickS_StaLta *s);
double PickS_StaLta_Update(PickS_StaLta *s, double x);

/* aic.c */
int PickS_Aic_Onset(const double *v, int n, int tguess, int halfwin,
                    double *onset_idx);

/* polarization.c */
int PickS_Pol_Compute(const double *e, const double *n, const double *z,
                      int len, PickS_Pol *out);

/* quality.c */
double PickS_Quality_Weight(const PickS_Params *p, const PickS_Metrics *m,
                            int *weight_out);

/* report.c */
int PickS_Report_Format(char *buf, size_t n, int modid, int instid, int seq,
                        const char *sta, const char *chan, const char *net,
                        const char *loc, double t_epoch, char fm, int weight,
                        long amp, char phase);

/* picks_ref.c: lista de picks P de guia + parser comun */
int PickS_ParsePickLine(const char *line, char *sta, double *t, char *phase);
void PickS_PickRef_Add(PickS_PickRef *list, int *n, int max,
                       const char *sta, double t);
double PickS_PickRef_Find(const PickS_PickRef *list, int n, const char *sta,
                          double t_before, double dt_min, double dt_max);

#endif /* PICKS_H */
