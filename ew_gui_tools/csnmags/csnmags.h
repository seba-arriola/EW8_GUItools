#ifndef CSNMAGS_H
#define CSNMAGS_H

/*
 * csnmags.h - Motor de magnitudes (ML, Mwp, Mb, Ms) de EW8_GUItools.
 *
 * Módulo C nativo con dos modos (el modo lo decide argv, patrón csnloc/pickS):
 *   csnmags <csnmags.d>                                  -> anillo (producción/replay)
 *   csnmags <csnmags.d> <hypos.jsonl> <tank> [--out F]   -> offline (reloj virtual, stdout)
 *
 * La respuesta instrumental se lee de ficheros SAC PZ en disco (generados por
 * tools/fetch_responses.py desde el FDSNWS de metadata). El modo offline no toca
 * anillos ni wave_serverV: lee el tank directamente (patrón pickS/pick_FP).
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>

#include <earthworm.h>
#include <transport.h>
#include <trace_buf.h>
#include <rw_mag.h>

#define MAG_STR        256
#define MAG_MAX_STA    512
#define MAG_MAX_EVENTS 100
#define MAG_MAX_GRIDS  16
#define MAG_MAX_TRACE  2000000

/* ============================ Configuración ============================ */

typedef struct {
    char name[MAG_STR];
    int  level;              /* 0 global, 1 regional, 2 local */
    char file[MAG_STR];
} MagGridRef;

typedef struct {
    int    enabled;
    double min_delta, max_delta;
    double win_pre, win_post;   /* ventana alrededor de la fase, s */
    double band_lo, band_hi;    /* banda de trabajo, Hz */
} MagGate;

typedef struct {
    /* Módulo / anillos */
    char my_mod_id[MAG_STR];
    char in_ring[MAG_STR];
    char out_ring[MAG_STR];
    int  heartbeat;
    int  logfile;
    int  debug;

    /* Respuesta instrumental */
    char resp_dir[MAG_STR];
    char resp_pattern[MAG_STR];   /* printf con %S %C %N %L */
    int  resp_in_meters;
    char sta_file[MAG_STR];
    char calib_file[MAG_STR];
    char sta_corr_file[MAG_STR];
    char tau_model[64];

    /* Wave server (modo anillo) */
    char ws_ip[64];
    char ws_port[16];
    int  ws_timeout;

    /* ML */
    double wa_period, wa_damp, wa_gain;   /* Wood-Anderson */
    int    ml_dist_type;                  /* 0 hipocentral, 1 epicentral */
    int    ml_amp_mode;                   /* 0 zero-to-peak, 1 peak-to-peak */
    double ml_slide_len;                  /* ventana deslizante p2p, s */
    int    ml_allow_vert, ml_require2h;
    double ml_c1, ml_c2, ml_c3;           /* C(R)=c1*log10 R + c2*R + c3 */
    double ml_snr;
    double ml_sg_speed, ml_t_a, ml_t_b;
    double ml_max_dist;
    int    ml_min_sta;
    MagGate ml;

    /* Mwp */
    double mwp_t0, mwp_start_off, mwp_highpass;
    double mwp_rho, mwp_alpha, mwp_fp;
    int    mwp_min_sta;
    MagGate mwp;

    /* Mb */
    double mb_period, mb_window, mb_snr;
    char   mb_q_table[MAG_STR];
    int    mb_min_sta;
    MagGate mb;

    /* Ms */
    int    ms_variant;                    /* 0 Ms_20, 1 Ms_BB */
    double ms_t, ms_band_lo, ms_band_hi, ms_k1, ms_k2;
    int    ms_deep_corr;
    int    ms_min_sta;
    MagGate ms;

    /* Red */
    int    use_median;
    double trunc_k;

    /* Grillas (reuso de csnloc) */
    MagGridRef grids[MAG_MAX_GRIDS];
    int    n_grids;
} MagConfig;

/* ============================ Estaciones ============================ */

typedef struct {
    char   sta[8], net[8], chan[8], loc[8];
    double lat, lon, elev_m, gain;
    double corr_ml, corr_mb, corr_ms;
} MagStation;

typedef struct {
    int n;
    MagStation st[MAG_MAX_STA];
} MagStationList;

/* ============================ Calibración ============================ */

typedef struct { int n; double x[256], y[256]; } Tab1D;

typedef struct {
    char   name[64];
    double latmin, latmax, lonmin, lonmax;
    char   loga0[MAG_STR], qtab[MAG_STR], stacorr[MAG_STR];
} RegionCalib;

typedef struct {
    int       n_reg;
    RegionCalib regs[32];
    Tab1D     loga0_global;   /* -logA0 por defecto (Hutton-Boore) */
    Tab1D     q_global;       /* Q(Delta) por defecto (Gutenberg-Richter) */
} MagCalib;

/* ============================ Respuesta ============================ */

/* Polos/ceros propios (autocontenido; no depende del transfer de EarthWorm). */
typedef struct { double re, im; } MagCx;

typedef struct {
    double constant;           /* CONSTANT del SAC PZ */
    int    nz, np;
    MagCx  zeros[32];
    MagCx  poles[32];
} MagPZ;

typedef struct {
    int    loaded;
    int    has_pz;      /* PZ leído; si no, fallback al gain escalar */
    MagPZ  pz;
} MagResponse;

/* ============================ Traza ============================ */

typedef struct {
    double   t0;        /* época del primer sample */
    double   dt;        /* 1/samprate */
    double   samprate;
    long     n;
    double  *x;         /* muestras (cuentas o unidades tras deconvolución) */
} MagTrace;

/* ============================ Fuente de onda ============================ */

typedef enum { WSRC_WS, WSRC_TANK } WaveSourceMode;

typedef struct {
    char   sta[8], net[8], chan[8], loc[8];
    double starttime;
    long   nsamp;
    long   offset;       /* offset del int32[0] en el fichero tank */
    double samprate;
} MagTankIndex;

typedef struct {
    WaveSourceMode mode;
    char  ws_ip[64], ws_port[16];
    int   ws_timeout;
    void *ws_menu;                 /* WS_MENU_QUEUE_REC */
    FILE *tank;                    /* modo offline */
    char  tank_path[MAG_STR];
    MagTankIndex *idx;             /* índice del tank (offline) */
    int   nidx;
} WaveSource;

/* ============================ Fórmulas ============================ */

double tab1d_interp(const Tab1D *t, double x);
int    tab1d_load(const char *path, Tab1D *t);

double mag_ml(double amp_nm, double dist_km, const Tab1D *loga0,
              double c1, double c2, double c3);
double mag_mwp(double int_disp_nm_s, double dist_km, double delta_deg,
               double rho, double alpha, double fp);
double mag_mb(double amp_nm, double period_s, double delta_deg,
              const Tab1D *qtab);
double mag_ms(double amp_nm, double period_s, double delta_deg, int variant,
              double k1, double k2);

/* Agregación de red. Devuelve la magnitud; puebla sigma y n_used. */
double mag_net(const double *per_sta, int n, int use_median, double trunc_k,
               double *sigma_out, int *n_used_out);

/* ============================ Amplitud ============================ */

double mag_amp_zero2peak(const double *x, long n, long i0, long i1);
double mag_amp_peak2peak(const double *x, long n, long i0, long i1, double dt,
                         double window_s);
double mag_amp_period(const double *x, long n, long i0, long i1, double dt);
double mag_rms(const double *x, long n);
void   mag_bandpass(double *x, long n, double dt, double f_lo, double f_hi,
                    int order);

/* ============================ Respuesta ============================ */

int  mag_resp_load(const MagConfig *cfg, const MagStation *st, MagResponse *out);
void mag_resp_free(MagResponse *r);
int  mag_resp_make_wa(MagPZ *wa, double period, double damp, double gain);
int  mag_resp_make_disp(MagPZ *disp, double gain);   /* plano, nm->1 */
/* Convierte `x` (cuentas) desde `orig` hacia la respuesta `target`. 0 si OK. */
int  mag_resp_convert(const MagPZ *orig, const MagPZ *target,
                      double *x, long n, double dt);
/* Evaluación/lectura expuestas para tests. */
int  mag_pz_read(const char *path, MagPZ *out);
int  mag_pz_eval(const MagPZ *pz, double f_hz, MagCx *out);
void mag_fft(double *re, double *im, long n, int inverse);

/* ============================ Config / STA / Calib ============================ */

int  mag_config_load(const char *path, MagConfig *cfg);
void mag_config_defaults(MagConfig *cfg);

int  mag_sta_load(const char *path, MagStationList *out);
int  mag_sta_find(const MagStationList *list, const char *sta, const char *net,
                  const char *chan, const char *loc);
void mag_sta_apply_corr(MagStationList *list, const char *sta_corr_file);

int  mag_calib_load(const MagConfig *cfg, MagCalib *out);
const RegionCalib *mag_calib_for(const MagCalib *c, double lat, double lon);
void mag_calib_free(MagCalib *c);

/* ============================ Fuente de onda ============================ */

int  wave_open(WaveSource *w, const MagConfig *cfg, const char *tank_or_null);
void wave_close(WaveSource *w);
/* Extrae la ventana [t0,t1] de un SCNL. Devuelve 0 si OK, -1 si no hay datos. */
int  wave_get(WaveSource *w, const char *sta, const char *net, const char *chan,
              const char *loc, double t0, double t1, MagTrace *out);

/* ============================ Offline IO ============================ */

/* Un origen leído de hypos.jsonl (schema del csnloc offline). */
typedef struct {
    char   event_id[24];
    double t0, lat, lon, depth_km;
    int    nphases;
    struct {
        char   sta[8], net[8], chan[8], loc[8];
        char   phase[4];
        double t_epoch;
    } ph[256];
} MagHypo;

int mag_hypos_open(const char *path);
int mag_hypos_next(MagHypo *out);   /* 1 = hay, 0 = fin, -1 = error */
void mag_hypos_close(void);

/* Emite una línea JSON por evento (magnitudes + n_sta + motivo "n/d"). */
void mag_json_emit(FILE *out, const char *event_id, double t0, double lat,
                   double lon, double depth_km,
                   int has_ml, double ml, int nml,
                   int has_mwp, double mwp, int nmwp,
                   int has_mb, double mb, int nmb,
                   int has_ms, double ms, int nms);

/* ============================ Utilidades ============================ */

double mag_great_circle_deg(double lat1, double lon1, double lat2, double lon2);
double mag_epi_km(double delta_deg);
double mag_hypo_km(double epi_km, double depth_km);
double mag_ymdhms_epoch(int y, int m, int d, int hh, int mi, double ss);
const char *mag_magtype_name(int imagtype);

/* ============================ Publicación ============================ */

int mag_out_publish(SHM_INFO *region, MSG_LOGO *logo, const char *event_id,
                    int imagtype, const char *szmagtype, double mag,
                    double err, double qual, double mindist, int gap,
                    int nsta);

#endif /* CSNMAGS_H */
