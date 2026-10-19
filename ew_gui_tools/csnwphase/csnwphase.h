#ifndef CSNWPHASE_H
#define CSNWPHASE_H

#include <stddef.h>
#include <stdint.h>

/*
 * csnwphase — integración de la inversión de fase W (motor eost/wphase) como
 * cálculo ON-DEMAND sobre una solución hipocentral. Headless: no incluye GTK.
 *
 * El módulo NO es un módulo de anillos: no se declara en startstop_unix.d. Se
 * invoca desde la GUI (pestaña Procesar → magnitudes → Mww) o por CLI:
 *
 *     csnwphase <csnwphase.d> --print-config
 *     csnwphase <csnwphase.d> <hypo.jsonl> [--out F] [--keep-tmp]
 *
 * Reparto de responsabilidades:
 *   - nosotros: ondas (store/SDS), respuesta (SAC PZ), i_master, CMTSOLUTION,
 *     tabla de parámetros, orquestación, parseo de la salida, JSON, GUI, BD.
 *   - el motor vendorizado (binarios C+Fortran de eost/wphase): decimación,
 *     ventanas W, LUT de respuesta, deconvolución+filtrado, kernels,
 *     inversión iterativa y grid search del centroide.
 *
 * Variables de entorno VIVAS del motor (verificado en el codigo del motor;
 * abortan con exit(1) si faltan): WPHASE_HOME (de ahi sale $WPHASE_HOME/P_tt_JB)
 * y GF_PATH. RDSEED/ALT_RESPONSE_FILE/GMT_BIN/HOME/DISPLAY son variables
 * muertas en el C.
 */

#define CSNWPHASE_MAX_PARAMS 40
#define CSNWPHASE_MAX_PZ 64
#define CSNWPHASE_MAX_SAMPLES 240000   /* 1 canal a 100 sps en una ventana larga */

typedef struct {
    /* --- rutas del motor ------------------------------------------------- */
    char   wphase_home[512];  /* WphaseHome: $WPHASE_HOME ($WPHASE_HOME/P_tt_JB) */
    char   wphase_bin[512];   /* WphaseBin:  binarios del motor                 */
    char   gf_dir[512];       /* GfDir:      $GF_PATH (base de funciones de Green) */

    /* --- ondas y metadata ------------------------------------------------ */
    char   store_root[256];   /* StoreRoot: store indexado (../wstore)          */
    char   sds_root[512];     /* SdsRoot: archivo SDS                           */
    int    sds_fallback_hours;/* antigüedad a partir de la cual se va directo al SDS */
    char   sta_file[512];     /* StaFile: STA NET CHAN LOC lat lon elev sens    */
    char   resp_dir[512];     /* ResponseDir: *.pz (ZEROS/POLES/CONSTANT)       */
    char   resp_pattern[64];  /* ResponsePattern: %S_%C_%N.pz                   */
    int    resp_in_nm;        /* 1 = los .pz están en nm (se escalan a counts/m) */

    /* --- parámetros de la corrida ---------------------------------------- */
    double dmin_deg;          /* DMIN [°]   (default 5.0, el del CSN)           */
    double dmax_deg;          /* DMAX [°]   (default 50.0)                      */
    double min_magnitude;     /* umbral de disparo (default 6.0)                */
    int    win_pre_sec;       /* ventana de ondas antes de t0 (default 300)     */
    int    win_post_sec;      /* ventana de ondas después de t0 (default 900)   */
    /* Screening RMS del motor: UN solo valor (el usuario pidio una sola
     * ejecucion con `-th` y `-nr` juntos). El patron historico del CSN eran
     * tres pasadas encadenadas (5 3 1); el `ScreenTh` del .d con varios valores
     * se acepta pero se usa el PRIMERO y se avisa por stderr. */
    double screen_th;         /* -th  (default 2.0)                            */
    double screen_nr;         /* amplitude-ratio screening (2.0)                */
    int    grid_search;       /* 1 = ts+xy (wp_grid_search.py -s)               */
    /* Grid search del centroide (wpinversion_gs). Defaults = los del wrapper
     * del CSN (wp_grid_search.py: TS_NIT/TS_DT/XY_*); los `-ts` los derivaba el
     * wrapper de la duracion, aqui son visibles y editables. */
    double gs_ts_min;         /* -ts tsmin (1.0)                                */
    double gs_ts_dt;          /* -ts dts   (4.0)                                */
    double gs_ts_max;         /* -ts tsmax (30.0)                               */
    int    gs_ts_nit;         /* -ts_Nit   (3)                                  */
    double gs_xy_dx;          /* -dx       (0.4 deg)                            */
    int    gs_xy_nx;          /* -Nx       (3 -> semiancho = Nx*dx)             */
    int    gs_xy_nopt;        /* -Nopt     (5)                                  */
    int    gs_xy_nit;         /* -xy_Nit   (3)                                  */
    /* Grid search 3D (xyz) del centroide: replica el `--xyz` de
     * wp_grid_search.py (XYZ_NIT/XYZ_DX/XYZ_NX/XYZ_NOPT + DDEP/MINDEP). La
     * profundidad NO tiene flag propio en wpinversion_gs: entra en la misma
     * pasada xy con `-dz`/`-minz` y su salida se nombra grid_search_xyz_out. */
    double gs_xyz_dz;         /* -dz     (50.0 = DDEP)     */
    double gs_xyz_minz;       /* -minz   (11.5 = MINDEP)   */
    double gs_xyz_dx;         /* -dx     (0.6  = XYZ_DX)   */
    int    gs_xyz_nx;         /* -Nx     (1    = XYZ_NX)   */
    int    gs_xyz_nopt;       /* -Nopt   (4    = XYZ_NOPT) */
    int    gs_xyz_nit;        /* -xy_Nit (1    = XYZ_NIT)  */
    /* --- paridad con el wrapper de produccion del CSN --------------------- */
    int    gs_hdsafe;         /* 1 = -hdsafe: ts_gridsearch re-sintetiza el
                                 kernel con hd=ts en vez de desplazar la ventana
                                 (wp_grid_search.py:187-188 via `-s`)        */
    int    gs_old;            /* 1 = -old: reusa az/gcarc/p2p/avg/wgt de la lista
                                 de 13 columnas o_wpinversion
                                 (wp_grid_search.py:298)                     */
    int    gs_ts_auto;        /* 1 = ts2 = csnwphase_ts_max_for_magnitude(mag)
                                 (wp_grid_search.py:173-185); 0 = GsTsMax    */
    int    corpus_min_nused;  /* n_used minimo para que un evento cuente en el
                                 corpus de validacion (default 7)            */
    int    timeout_sec;       /* tope de la corrida (default 1800)              */
    int    keep_tmp;          /* 1 = conservar el run dir                       */
    int    max_concurrent;    /* 1 = serializar (default)                       */
    int    debug;

    /* --- i_master (formato exacto del generador del CSN) ----------------- */
    char   evname[64];        /* EVNAME (el CSN usa `location`)                 */
    char   seed_ref[512];     /* SEED: solo lo consume extract.csh (no lo corremos) */
    char   cmtfile[64];       /* CMTFILE (CMTSOLUTION)                          */
    char   gfdir_rel[64];     /* GFDIR: subdir del run dir (¡NUNCA GfDir!)      */
    int    filt_order;        /* filt_order */
    int    filt_pass;         /* filt_pass  */
    double idec2_idtr;        /* IDEC_2[1] (2 = quita media)                    */
    double idec2_preevent;    /* IDEC_2[2] [s] */
    double idec2_fend;        /* IDEC_2[3] [s] */
    double idec3_fl;          /* IDEC_3[1] [Hz] */
    double idec3_fh;          /* IDEC_3[2] [Hz] */
    int    idec3_nf;          /* IDEC_3[3] */
    double idec3_tol;         /* IDEC_3[4] */
    double wpwin[3];          /* WP_WIN (3 valores) */
} CsnwphaseCfg;

/* Solución hipocentral que siembra la inversión (del ARC / de la BD). */
typedef struct {
    int    year, month, day, hour, minute;
    double second;
    double latitude, longitude, depth_km, magnitude;
    double origin_epoch;      /* t0 en epoch UTC (lo fija el llamante) */
    char   location[64];      /* Flinn-Engdahl o "Chile" (nombre del evento) */
    char   event_id[64];
    char   origin_id[64];
} CsnwphaseInput;

/* Una fila de la tabla de parámetros que la GUI muestra antes de ejecutar. */
/*
 * Campo de la tabla: qué miembro de Cfg/Input gobierna esa fila. Lo consume la
 * GUI para decidir qué filas llevan widget editable (csnwphase no conoce GTK).
 * NONE = solo lectura (derivado de la magnitud o informativo).
 */
typedef enum {
    CSNWPHASE_PF_NONE = 0,
    CSNWPHASE_PF_ORIGIN_TIME,   /* in->year..in->second (y origin_epoch) */
    CSNWPHASE_PF_LAT,           /* in->latitude   */
    CSNWPHASE_PF_LON,           /* in->longitude  */
    CSNWPHASE_PF_DEPTH,         /* in->depth_km   */
    CSNWPHASE_PF_MAGNITUDE,     /* in->magnitude  */
    CSNWPHASE_PF_WIN_PRE,       /* cfg->win_pre_sec  */
    CSNWPHASE_PF_WIN_POST,      /* cfg->win_post_sec */
    CSNWPHASE_PF_SCREEN_TH,     /* cfg->screen_th    */
    CSNWPHASE_PF_SCREEN_NR,     /* cfg->screen_nr    */
    CSNWPHASE_PF_DMIN,          /* cfg->dmin_deg (cribado de estaciones) */
    CSNWPHASE_PF_DMAX           /* cfg->dmax_deg     */
} CsnwphaseParamField;

typedef struct {
    char name[48];
    char value[96];
    char source[48];          /* config / evento / derivado / i_master */
    int  field;               /* CsnwphaseParamField; NONE = solo lectura */
} CsnwphaseParamRow;

/* Configuración: lee el .d. Devuelve 0 si OK, -1 si falta algo obligatorio. */
int csnwphase_cfg_load(CsnwphaseCfg *cfg, const char *path);
/* Defaults (sin leer archivo), para --print-config y tests. */
void csnwphase_cfg_defaults(CsnwphaseCfg *cfg);

/*
 * Resuelve una ruta del .d: si es relativa se interpreta respecto de
 * $EW_PARAMS (el módulo se invoca desde la GUI/CLI, no desde startstop y por
 * tanto el cwd no es params/).
 */
void csnwphase_resolve_path(char *out, size_t n, const char *in);

/*
 * Guard anti-borrado: `prep_kernels` hace crea_dir() sobre GFDIR (borra y
 * recrea). Si GFDIR resolviera al mismo directorio que GfDir, la base de
 * funciones de Green se perdería. Devuelve 0 si es seguro, -1 si no.
 */
int csnwphase_gfdir_guard(const CsnwphaseCfg *cfg, const char *rundir,
                          char *err, size_t errlen);

/* Contenido de i_master y CMTSOLUTION (byte-exacto con el generador del CSN). */
int csnwphase_i_master_text(const CsnwphaseCfg *cfg, const CsnwphaseInput *in,
                            char *out, size_t outsz);

/* Verifica que el `i_master` recien generado coincida con la config. Es la
 * guarda contra los objetos desfasados: la ABI rota se ve AQUI, no en el motor
 * (que falla mas tarde con `fc=-nan` y un rc!=0 opaco). */
int csnwphase_i_master_selfcheck(const CsnwphaseCfg *cfg, const char *text,
                                 char *err, size_t errlen);

/* Guardia de ABI: el consumidor pasa SUS `sizeof` y se comparan con los de la
 * libreria. 0 = coinciden; -1 = compilo con otro `csnwphase.h`. */
int csnwphase_abi_check(size_t cfg_size, size_t in_size);
size_t csnwphase_cfg_size(void);
size_t csnwphase_input_size(void);
int csnwphase_cmt_text(const CsnwphaseCfg *cfg, const CsnwphaseInput *in,
                       char *out, size_t outsz);

/* Escritura del run dir (i_master + CMTSOLUTION). Devuelve 0 si OK. */
/* Crea `path` y todos sus padres (mkdir -p). */
int csnwphase_mkdir_p(const char *path);

int csnwphase_prep_write(const CsnwphaseCfg *cfg, const CsnwphaseInput *in,
                         const char *rundir, char *err, size_t errlen);

/*
 * Convierte un .pz del repo (CONSTANT/ZEROS/POLES) al fichero que consume el
 * motor: SAC_PZs_<NET>_<STA>_<CHAN>_<LOC>_<X>_<Y>.
 *
 * El nombre NO es decorativo: make_resp_lookup_table::pzfile2id exige 7 guiones
 * bajos y arma el id NET_STA_LOC_CHAN, que es justo lo que consulta
 * rec_dec_filt::makeid. Con otro orden el canal se descarta en silencio.
 * Devuelve 0 si OK (y la ruta en out_path).
 */
int csnwphase_pz_write(const CsnwphaseCfg *cfg, const char *dir, const char *net,
                       const char *sta, const char *chan, const char *loc,
                       char *out_path, size_t outsz);

/*
 * Traza de entrada al motor, en SAC. La cabecera se escribe en el orden
 * nativo (el motor hace fread directo, sin detección de byte order) y con los
 * campos que el motor LEE de verdad (verificado en su código):
 *   trim_sac_files: stla/stlo (->distaz), nz* (tiempo de referencia), b, delta,
 *                   npts; y EXIGE delta == 1 s (por eso el orden decim->trim).
 *   rec_dec_filt:   npts, delta, knetwk/kstnm/khole/kcmpnm (id de la LUT).
 *   wpinversion:    gcarc (ventana W), o (origen), b, delta, npts, evdp,
 *                   kcmpnm[2] (pesos) y los k* para el nombre de las GF.
 * Los campos de texto se rellenan con NUL (no "-12345"): nbchar() cuenta
 * hasta el último blanco, así que una location vacía da 0 y makeid usa "--",
 * que es lo que espera el nombre de los SAC_PZs.
 */
typedef struct {
    char   sta[16], net[16], chan[16], loc[16];
    double stla, stlo, stel_m, stdp_m;
    double evla, evlo, evel_m, evdp_km;
    double mag;
    double rate;          /* muestras/s del fichero (el motor diezma a 1 sps) */
    double start_epoch;   /* t del primer sample [s epoch UTC]                 */
    double origin_epoch;  /* t0 del evento [s epoch UTC]                       */
    long   npts;
    const float *data;
} CsnwphaseSAC;

/* Escribe un SAC (632 B de cabecera + float32 nativo). Devuelve 0 si OK. */
int csnwphase_sac_write(const char *path, const CsnwphaseSAC *s);

/* Distancia/azimuts entre evento y estación (esférico). Ángulos en grados. */
void csnwphase_distaz(double evla, double evlo, double stla, double stlo,
                      double *dist_km, double *az, double *baz, double *gcarc_deg);

/* ------------------------------------------------------------------ */
/* Ondas: store indexado (wstore) + SDS (libmseed2).                   */
/* ------------------------------------------------------------------ */

typedef struct {
    char     sta[16], net[16], chan[16], loc[16];
    double   t0;         /* tiempo del primer sample [s epoch UTC] */
    double   samprate;
    long     n;
    int32_t *samples;    /* propiedad del llamante: csnwphase_trace_free() */
} CsnwphaseTrace;

typedef struct {
    void   *store;              /* WStore*, opaco aquí (wstore.h en el .c) */
    int     have_store;
    char    store_root[256];
    char    sds_root[512];
    int     sds_enabled;
    int     sds_fallback_hours;
    long    n_store;            /* trazas servidas por el store (diagnóstico) */
    long    n_sds;
} CsnwphaseWave;

void csnwphase_trace_free(CsnwphaseTrace *t);

/* Abre store (opcional) y SDS. Devuelve 0 si al menos una fuente está lista. */
int  csnwphase_wave_open(CsnwphaseWave *w, const CsnwphaseCfg *cfg);
void csnwphase_wave_close(CsnwphaseWave *w);

/*
 * Traza de un canal en [t0,t1]. Política del repo: para eventos más viejos que
 * SdsFallbackHours se consulta primero el SDS (el store no retiene tanto) y si
 * no hay datos se prueba la otra fuente. Devuelve 1 datos, 0 sin datos, <0 error.
 */
int  csnwphase_wave_get(CsnwphaseWave *w, const char *net, const char *sta,
                        const char *chan, const char *loc, double t0, double t1,
                        CsnwphaseTrace *out);

/* ------------------------------------------------------------------ */
/* Selección de estaciones por distancia epicentral                    */
/* ------------------------------------------------------------------ */

typedef struct {
    char   sta[16], net[16], chan[16], loc[16];
    double lat, lon, elev_m, sens;
    double dist_km, gcarc_deg, az, baz;
} CsnwphaseStation;

/*
 * Lee StaFile (STA NET CHAN LOC lat lon elev [sens]) y devuelve las estaciones
 * dentro de [dmin_deg, dmax_deg] ordenadas por distancia. Devuelve el número,
 * o -1. `out` debe tener capacidad `max`.
 */
int csnwphase_sta_select(const CsnwphaseCfg *cfg, double evla, double evlo,
                         double dmin_deg, double dmax_deg,
                         CsnwphaseStation *out, int max);

/* Canales horizontales/vertical a partir del canal del StaFile (HHZ->HHN/HHE). */
void csnwphase_chan_variants(const char *chan_in, char out[3][16], int *n);

/* ------------------------------------------------------------------ */
/* Preparación de la entrada y corrida                                 */
/* ------------------------------------------------------------------ */

/*
 * Llena <rundir>/DATA_org con las trazas de las estaciones en [Dmin,Dmax] y sus
 * SAC_PZs_* (onjas del store/SDS), y escribe i_master + CMTSOLUTION.
 * Devuelve 0 si OK (y cuántas estaciones/trazas).
 */
int csnwphase_input_prep(const CsnwphaseCfg *cfg, const CsnwphaseInput *in,
                         const char *rundir, int *n_sta_out, int *n_traces_out,
                         char *err, size_t errlen);

/*
 * Escribe <rundir>/run.sh (auditable: es lo que se ejecuta). `magnitude` es la
 * del evento sembrado: con `GsTsAuto 1` fija el ts2 del barrido de time-shift
 * (el wrapper del CSN lo derivaba de la PDE del CMT).
 */
int csnwphase_run_script(const CsnwphaseCfg *cfg, const char *rundir,
                         const char *script, double magnitude);

/*
 * Ejecuta la secuencia del motor en <rundir> acumulando la salida completa en
 * <rundir>/run.log. Con dry_run=1 sólo deja el script y el log con las órdenes.
 */
int csnwphase_run(const CsnwphaseCfg *cfg, const char *rundir, int dry_run,
                  double magnitude, char *err, size_t errlen);

/* Tabla de parámetros (lógica pura; misma fuente que i_master y los comandos). */
int csnwphase_params_build(const CsnwphaseCfg *cfg, const CsnwphaseInput *in,
                           CsnwphaseParamRow *rows, int max, int *n);

/* ------------------------------------------------------------------ */
/* Campos editables de la tabla                                        */
/* ------------------------------------------------------------------ */

/* t0 en epoch UTC a partir de sus componentes (independiente de TZ/locale:
 * usa timegm). */
double csnwphase_epoch_utc(int year, int mon, int day, int hh, int mm, double ss);

/*
 * Lee el texto de la celda "origen t0 (UTC)" ("YYYY-MM-DD HH:MM:SS[.ss]", el
 * mismo que se muestra; se acepta 'T' en lugar del espacio) y deja el
 * hipocentro en `in` (incluido `origin_epoch`). Devuelve 0 si OK, -1 si el
 * texto no es una fecha válida (no se aplica nada a medias).
 */
int csnwphase_time_parse(const char *txt, CsnwphaseInput *in);

/* Banda de la inversión por magnitud (i_master.py:31-40): es el único par de
 * coeficientes del filtro que depende de la magnitud (filt_cf1/filt_cf2). */
void   csnwphase_band_for_magnitude(double mag, double *cf1, double *cf2);

/* half duration / time shift del CMT semilla (cmtsolution.py:39-41). */
double csnwphase_half_duration(double mag);

/*
 * ts2 del barrido de time-shift por magnitud (wp_grid_search.py:173-185): el
 * wrapper del CSN lo derivaba de la magnitud PDE (`<=7` -> 30 s, `<=8` ->
 * 100 s, `>8` -> 168 s) y con un tope fijo de 30 s el barrido se quedaba corto
 * para M>7 (la half duration de un 8.3 es 39.28 s). La rama `mag < 5.5` del
 * wrapper queda fuera de alcance: MinMagnitude (6.0) la bloquea antes.
 */
double csnwphase_ts_max_for_magnitude(double mag);

/* Resultado normalizado de una corrida: lo único que ven la GUI y la BD. */
typedef struct {
    int    ok;                 /* 1 = se pudo leer una solución del run dir  */
    int    converged;          /* 1 = el log trae Wmag (hubo convergencia)   */
    int    n_used;             /* n_used_rec: estaciones usadas              */
    int    n_sta;              /* nº de líneas `stat:` del log               */
    double mw;                 /* Wmag                                       */
    double m0;                 /* Wmom [N·m]                                 */
    double lat, lon, depth_km; /* centroide del CMT final                    */
    double t_shift_s;          /* time shift (centroide - origen) [s]        */
    double half_dur_s;         /* half duration [s]                          */
    double origin_epoch;       /* t0 del PDE [s epoch UTC]                   */
    double centroid_epoch;     /* origin_epoch + t_shift [s epoch UTC]       */
    double m_rr, m_tt, m_pp, m_rt, m_rp, m_tp;
    double strike1, dip1, rake1, strike2, dip2, rake2;
    double eig[3];             /* W_eigenvalues                              */
    double global_rms;         /* si el log lo expone (best effort)          */
    int    have_mt;            /* 1 = hay componentes del tensor             */
    int    have_planes;        /* 1 = hay W_bestnodal planes                 */
    char   cmt_file[64];       /* de qué archivo salió la solución           */
    char   log_file[64];       /* de qué log salieron Wmag/Wmom/n_used_rec   */
    char   err[256];
} CsnwphaseOut;

/* Lee el run dir (xy_WCMTSOLUTION/WCMTSOLUTION + los LOG/wpinversion.log). */
int csnwphase_parse(const char *rundir, CsnwphaseOut *out);

/* Resultado en JSON (contrato estable para la GUI y para wphase.json). */
int csnwphase_json_write(const char *path, const CsnwphaseOut *out);

/*
 * Captura la salida COMPLETA del motor en un solo texto: comandos, i_master,
 * CMTSOLUTION, WCMTSOLUTION, xy_WCMTSOLUTION, los LOG/wpinversion.log,
 * o_wpinversion y grid_search_..._out. Es lo que muestra el visor de la GUI (y
 * lo que se guarda). Devuelve los bytes escritos, o -1.
 */
int csnwphase_log_write(const char *rundir, const char *path);

#endif /* CSNWPHASE_H */
